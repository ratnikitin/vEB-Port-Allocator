/**
 * server.c — portd daemon: epoll-based Unix/TCP socket server.
 *
 * Architecture:
 *   - One accept loop thread (main).
 *   - Client requests processed inline (operations are microseconds).
 *   - epoll with EPOLLET (edge-triggered) for scalability.
 *   - Graceful shutdown on SIGTERM / SIGINT.
 *
 * Security:
 *   - Socket permissions set to 0660 (group-writable).
 *   - All pool IDs bounds-checked before dispatch.
 *   - Protocol magic bytes validated on every request.
 *   - No stack buffers fed to printf without bounds.
 *
 * MIT License — see LICENSE
 */

#define _GNU_SOURCE
#include "server.h"
#include "protocol.h"
#include "../../include/port_manager.h"
#include "../utils/log.h"
#include "../utils/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_EVENTS    512
#define BACKLOG        64

/* ── Server state ───────────────────────────────────────────────────── */

typedef struct {
    port_pool_t   *pools[POOL_MAX_COUNT];
    int            pool_count;
    int            listen_fd;
    int            epoll_fd;
    volatile int   running;
    char           socket_path[128]; /* for cleanup on exit */
    int            is_unix;
} server_t;

static server_t g_server;

/* ── Helpers ────────────────────────────────────────────────────────── */

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int epoll_add(int epoll_fd, int fd, uint32_t events)
{
    struct epoll_event ev = { .events = events | EPOLLET, .data.fd = fd };
    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

/* ── Request handler ────────────────────────────────────────────────── */

static void handle_request(int client_fd, const daemon_config_t *cfg)
{
    portd_req_t req;
    ssize_t n = recv(client_fd, &req, sizeof(req), MSG_WAITALL);
    if (n != (ssize_t)sizeof(req)) {
        if (n > 0) log_warn("server: short read %zd bytes from fd %d", n, client_fd);
        return;
    }

    /* Validate magic */
    if (req.magic != PORTD_MAGIC_REQ) {
        log_warn("server: bad magic 0x%02X from fd %d", req.magic, client_fd);
        return;
    }

    portd_resp_t resp = {
        .magic    = PORTD_MAGIC_RESP,
        .op       = req.op,
        .error    = ERR_OK,
        .port     = 0,
        .reserved = 0,
    };

    /* Bounds-check pool_id */
    if (req.pool_id >= (uint8_t)g_server.pool_count ||
        g_server.pools[req.pool_id] == NULL) {
        resp.error = ERR_BAD_POOL;
        send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
        return;
    }

    port_pool_t *pool = g_server.pools[req.pool_id];

    switch ((portd_op_t)req.op) {
    case OP_ALLOC: {
        uint16_t port = pool_alloc(pool);
        if (port == PORT_NONE) resp.error = ERR_EXHAUSTED;
        else resp.port = port;
        break;
    }
    case OP_ALLOC_FROM: {
        uint16_t port = pool_alloc_from(pool, req.port);
        if (port == PORT_NONE) resp.error = ERR_EXHAUSTED;
        else resp.port = port;
        break;
    }
    case OP_FREE: {
        int rc = pool_free(pool, req.port);
        if (rc != 0) resp.error = ERR_RANGE;
        break;
    }
    case OP_STATUS: {
        pool_stats_t stats;
        pool_get_stats(pool, &stats);

        send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);

        /* Append JSON payload */
        char json[PORTD_STATUS_JSON_MAX];
        int jlen = snprintf(json, sizeof(json),
            "{\"name\":\"%s\",\"range\":[%u,%u],"
            "\"total\":%u,\"free\":%u,\"allocated\":%u,"
            "\"alloc_calls\":%llu,\"free_calls\":%llu,"
            "\"fail_calls\":%llu}",
            stats.name, stats.range_start, stats.range_end,
            stats.total, stats.free_count, stats.allocated,
            (unsigned long long)stats.alloc_calls,
            (unsigned long long)stats.free_calls,
            (unsigned long long)stats.fail_calls);

        uint16_t jlen16 = (uint16_t)(jlen < 0 ? 0 : (size_t)jlen);
        send(client_fd, &jlen16, sizeof(jlen16), MSG_NOSIGNAL);
        if (jlen16 > 0) send(client_fd, json, jlen16, MSG_NOSIGNAL);
        return; /* already sent response */
    }
    case OP_PING:
        resp.port = 0xBEEF; /* ping pong */
        break;
    case OP_RESET:
        pool_reset(pool);
        break;
    default:
        resp.error = ERR_PROTO;
        break;
    }

    send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
    (void)cfg;
}

/* ── Socket creation ────────────────────────────────────────────────── */

static int create_unix_socket(const char *path)
{
    /* Remove stale socket file */
    unlink(path);

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) { log_error("socket: %s", strerror(errno)); return -1; }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("bind(%s): %s", path, strerror(errno));
        close(fd); return -1;
    }

    /* Owner rw, group rw, others none */
    chmod(path, 0660);

    if (listen(fd, BACKLOG) < 0) {
        log_error("listen: %s", strerror(errno));
        close(fd); return -1;
    }

    strncpy(g_server.socket_path, path, sizeof(g_server.socket_path) - 1);
    g_server.is_unix = 1;
    log_info("server: listening on unix://%s", path);
    return fd;
}

static int create_tcp_socket(const char *host, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) { log_error("socket: %s", strerror(errno)); return -1; }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    inet_aton(host, &addr.sin_addr);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("bind(%s:%d): %s", host, port, strerror(errno));
        close(fd); return -1;
    }
    if (listen(fd, BACKLOG) < 0) {
        log_error("listen: %s", strerror(errno));
        close(fd); return -1;
    }

    log_info("server: listening on tcp://%s:%d", host, port);
    return fd;
}

/* ── Signal handling ────────────────────────────────────────────────── */

static void sighandler(int sig)
{
    (void)sig;
    g_server.running = 0;
}

/* ── Public API ─────────────────────────────────────────────────────── */

int server_init(const daemon_config_t *cfg)
{
    memset(&g_server, 0, sizeof(g_server));
    g_server.running = 1;

    /* Build pool array indexed by pool id */
    for (int i = 0; i < cfg->pool_count; i++) {
        const pool_config_t *pc = &cfg->pools[i];
        if (pc->id >= POOL_MAX_COUNT) {
            log_error("server: pool id %u out of range", pc->id);
            return -1;
        }
        port_pool_t *pool = pool_create(pc->range_start, pc->range_end, pc->name);
        if (!pool) return -1;
        g_server.pools[pc->id] = pool;
        if (pc->id >= (uint8_t)g_server.pool_count)
            g_server.pool_count = pc->id + 1;
    }

    /* Parse listen address */
    const char *addr = cfg->listen;
    if (strncmp(addr, "unix://", 7) == 0) {
        g_server.listen_fd = create_unix_socket(addr + 7);
    } else if (strncmp(addr, "tcp://", 6) == 0) {
        char host[64] = "127.0.0.1";
        int  port_num = 9500;
        sscanf(addr + 6, "%63[^:]:%d", host, &port_num);
        g_server.listen_fd = create_tcp_socket(host, port_num);
    } else {
        log_error("server: unknown listen scheme: '%s'", addr);
        return -1;
    }

    if (g_server.listen_fd < 0) return -1;

    /* Create epoll */
    g_server.epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (g_server.epoll_fd < 0) {
        log_error("epoll_create1: %s", strerror(errno));
        return -1;
    }
    epoll_add(g_server.epoll_fd, g_server.listen_fd,
              EPOLLIN | EPOLLRDHUP);

    /* Signals */
    signal(SIGTERM, sighandler);
    signal(SIGINT,  sighandler);
    signal(SIGPIPE, SIG_IGN);

    return 0;
}

void server_run(const daemon_config_t *cfg)
{
    struct epoll_event events[MAX_EVENTS];
    log_info("server: event loop started");

    while (g_server.running) {
        int nev = epoll_wait(g_server.epoll_fd, events, MAX_EVENTS, 1000);
        if (nev < 0) {
            if (errno == EINTR) continue;
            log_error("epoll_wait: %s", strerror(errno));
            break;
        }

        for (int i = 0; i < nev; i++) {
            int fd = events[i].data.fd;

            if (fd == g_server.listen_fd) {
                /* Accept all pending connections */
                while (1) {
                    int cfd = accept4(g_server.listen_fd, NULL, NULL,
                                      SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if (cfd < 0) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK)
                            log_warn("accept: %s", strerror(errno));
                        break;
                    }
                    epoll_add(g_server.epoll_fd, cfd, EPOLLIN | EPOLLRDHUP);
                    log_debug("server: new connection fd=%d", cfd);
                }
            } else {
                if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    epoll_ctl(g_server.epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    log_debug("server: client disconnected fd=%d", fd);
                    continue;
                }
                if (events[i].events & EPOLLIN) {
                    handle_request(fd, cfg);
                }
            }
        }
    }

    log_info("server: shutting down");
}

void server_shutdown(void)
{
    if (g_server.epoll_fd > 0) close(g_server.epoll_fd);
    if (g_server.listen_fd > 0) close(g_server.listen_fd);
    if (g_server.is_unix && g_server.socket_path[0])
        unlink(g_server.socket_path);
    for (int i = 0; i < POOL_MAX_COUNT; i++)
        if (g_server.pools[i]) {
            pool_destroy(g_server.pools[i]);
            g_server.pools[i] = NULL;
        }
}
