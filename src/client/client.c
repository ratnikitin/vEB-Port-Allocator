/**
 * client.c — portd client library.
 *
 * Connects to portd via Unix socket or TCP, sends fixed 8-byte requests,
 * reads 8-byte responses. All I/O is blocking with a configurable timeout.
 *
 * MIT License — see LICENSE
 */

#define _GNU_SOURCE
#include "../../include/client.h"
#include "../manager/protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define IO_TIMEOUT_SEC 5

struct port_client {
    int  fd;
    int  last_error;
};

/* ── I/O helpers ────────────────────────────────────────────────────── */

static int set_timeout(int fd, int secs)
{
    struct timeval tv = { .tv_sec = secs, .tv_usec = 0 };
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) |
           setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

static ssize_t read_all(int fd, void *buf, size_t len)
{
    size_t received = 0;
    uint8_t *p = (uint8_t *)buf;
    while (received < len) {
        ssize_t n = recv(fd, p + received, len - received, 0);
        if (n <= 0) return n == 0 ? (ssize_t)received : -1;
        received += (size_t)n;
    }
    return (ssize_t)received;
}

/* ── Connection ─────────────────────────────────────────────────────── */

port_client_t *port_client_connect(const char *addr)
{
    if (!addr) return NULL;

    int fd = -1;

    if (strncmp(addr, "unix://", 7) == 0) {
        const char *path = addr + 7;
        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) return NULL;

        struct sockaddr_un sa = {0};
        sa.sun_family = AF_UNIX;
        strncpy(sa.sun_path, path, sizeof(sa.sun_path) - 1);
        if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
            close(fd);
            return NULL;
        }
    } else if (strncmp(addr, "tcp://", 6) == 0) {
        char host[64] = "127.0.0.1";
        int  port_num = 9500;
        sscanf(addr + 6, "%63[^:]:%d", host, &port_num);

        fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) return NULL;

        struct sockaddr_in sa = {0};
        sa.sin_family = AF_INET;
        sa.sin_port   = htons((uint16_t)port_num);
        inet_aton(host, &sa.sin_addr);
        if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
            close(fd);
            return NULL;
        }
    } else {
        return NULL;
    }

    set_timeout(fd, IO_TIMEOUT_SEC);

    port_client_t *pc = (port_client_t *)calloc(1, sizeof(port_client_t));
    if (!pc) { close(fd); return NULL; }
    pc->fd = fd;
    return pc;
}

void port_client_disconnect(port_client_t *pc)
{
    if (!pc) return;
    close(pc->fd);
    free(pc);
}

/* ── Request/response dispatch ──────────────────────────────────────── */

static int do_request(port_client_t *pc, const portd_req_t *req,
                      portd_resp_t *resp)
{
    ssize_t n = send(pc->fd, req, sizeof(*req), MSG_NOSIGNAL);
    if (n != (ssize_t)sizeof(*req)) {
        pc->last_error = PORT_CLIENT_ERR_IO;
        return PORT_CLIENT_ERR_IO;
    }
    n = read_all(pc->fd, resp, sizeof(*resp));
    if (n != (ssize_t)sizeof(*resp)) {
        pc->last_error = PORT_CLIENT_ERR_IO;
        return PORT_CLIENT_ERR_IO;
    }
    if (resp->magic != PORTD_MAGIC_RESP || resp->op != req->op) {
        pc->last_error = PORT_CLIENT_ERR_PROTO;
        return PORT_CLIENT_ERR_PROTO;
    }
    return PORT_CLIENT_OK;
}

/* ── Public API ─────────────────────────────────────────────────────── */

uint16_t port_client_alloc(port_client_t *pc, uint8_t pool_id)
{
    portd_req_t  req  = { PORTD_MAGIC_REQ, OP_ALLOC, pool_id, 0, 0, 0 };
    portd_resp_t resp = {0};

    if (do_request(pc, &req, &resp) != PORT_CLIENT_OK) return 0;
    if (resp.error != ERR_OK) {
        pc->last_error = (resp.error == ERR_EXHAUSTED)
                       ? PORT_CLIENT_ERR_FULL : PORT_CLIENT_ERR_PROTO;
        return 0;
    }
    pc->last_error = PORT_CLIENT_OK;
    return resp.port;
}

uint16_t port_client_alloc_from(port_client_t *pc, uint8_t pool_id,
                                 uint16_t desired)
{
    portd_req_t  req  = { PORTD_MAGIC_REQ, OP_ALLOC_FROM, pool_id, 0, desired, 0 };
    portd_resp_t resp = {0};

    if (do_request(pc, &req, &resp) != PORT_CLIENT_OK) return 0;
    if (resp.error != ERR_OK) {
        pc->last_error = PORT_CLIENT_ERR_FULL;
        return 0;
    }
    pc->last_error = PORT_CLIENT_OK;
    return resp.port;
}

int port_client_free(port_client_t *pc, uint8_t pool_id, uint16_t port)
{
    portd_req_t  req  = { PORTD_MAGIC_REQ, OP_FREE, pool_id, 0, port, 0 };
    portd_resp_t resp = {0};

    int rc = do_request(pc, &req, &resp);
    if (rc != PORT_CLIENT_OK) return rc;
    if (resp.error != ERR_OK) {
        pc->last_error = PORT_CLIENT_ERR_RANGE;
        return PORT_CLIENT_ERR_RANGE;
    }
    pc->last_error = PORT_CLIENT_OK;
    return PORT_CLIENT_OK;
}

int port_client_status(port_client_t *pc, uint8_t pool_id,
                       char *out_json, uint32_t out_len)
{
    portd_req_t  req  = { PORTD_MAGIC_REQ, OP_STATUS, pool_id, 0, 0, 0 };
    portd_resp_t resp = {0};

    int rc = do_request(pc, &req, &resp);
    if (rc != PORT_CLIENT_OK) return rc;
    if (resp.error != ERR_OK) return PORT_CLIENT_ERR_PROTO;

    /* Read JSON payload */
    uint16_t jlen = 0;
    if (read_all(pc->fd, &jlen, sizeof(jlen)) != sizeof(jlen))
        return PORT_CLIENT_ERR_IO;
    if (jlen == 0) { if (out_json) out_json[0] = '\0'; return PORT_CLIENT_OK; }

    uint16_t to_read = (jlen < (uint16_t)(out_len - 1)) ? jlen : (uint16_t)(out_len - 1);
    ssize_t n = read_all(pc->fd, out_json, to_read);
    out_json[n > 0 ? n : 0] = '\0';

    /* Drain remaining bytes if truncated */
    if (jlen > to_read) {
        char discard[64];
        uint16_t remaining = jlen - to_read;
        while (remaining > 0) {
            uint16_t chunk = remaining < 64 ? remaining : 64;
            read_all(pc->fd, discard, chunk);
            remaining -= chunk;
        }
    }
    return PORT_CLIENT_OK;
}

int port_client_last_error(const port_client_t *pc)
{
    return pc ? pc->last_error : PORT_CLIENT_ERR_PARAM;
}

const char *port_client_strerror(int err)
{
    switch (err) {
    case PORT_CLIENT_OK:         return "success";
    case PORT_CLIENT_ERR_CONN:   return "connection failed";
    case PORT_CLIENT_ERR_IO:     return "I/O error";
    case PORT_CLIENT_ERR_PROTO:  return "protocol error";
    case PORT_CLIENT_ERR_PARAM:  return "invalid parameter";
    case PORT_CLIENT_ERR_FULL:   return "pool exhausted";
    case PORT_CLIENT_ERR_RANGE:  return "port out of range or already free";
    default:                     return "unknown error";
    }
}
