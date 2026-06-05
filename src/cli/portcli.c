/**
 * portcli.c — Command-line client for portd.
 *
 * Usage:
 *   portcli [-s unix:///var/run/portd.sock] [-p pool_id] <command> [args]
 *
 * Commands:
 *   alloc               Allocate the lowest free port
 *   allocfrom <port>    Allocate port >= given value
 *   free <port>         Release a port back to the pool
 *   status              Print pool statistics as JSON
 *   ping                Check daemon is alive
 *
 * Exit codes: 0 = success, 1 = error.
 *
 * MIT License — see LICENSE
 */

#include "../../include/client.h"
#include "../manager/protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SOCKET "unix:///var/run/portd.sock"
#define DEFAULT_POOL   1

static void usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s [-s SOCKET] [-p POOL_ID] <command> [args]\n"
        "\n"
        "Commands:\n"
        "  alloc               Allocate lowest free port\n"
        "  allocfrom PORT      Allocate first free port >= PORT\n"
        "  free PORT           Release PORT back to pool\n"
        "  status              Show pool stats (JSON)\n"
        "  ping                Ping daemon\n"
        "\n"
        "Options:\n"
        "  -s SOCKET    Daemon socket (default: " DEFAULT_SOCKET ")\n"
        "  -p POOL_ID   Pool id (default: %d)\n",
        argv0, DEFAULT_POOL);
}

int main(int argc, char *argv[])
{
    const char *socket_addr = DEFAULT_SOCKET;
    uint8_t     pool_id     = DEFAULT_POOL;
    int         i           = 1;

    /* Parse options */
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-s") == 0 && i+1 < argc) {
            socket_addr = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i+1 < argc) {
            pool_id = (uint8_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
        i++;
    }

    if (i >= argc) {
        fprintf(stderr, "Error: no command given\n");
        usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[i++];

    /* Connect */
    port_client_t *pc = port_client_connect(socket_addr);
    if (!pc) {
        fprintf(stderr, "Error: cannot connect to portd at '%s'\n"
                        "Is portd running? Try: sudo systemctl start portd\n",
                socket_addr);
        return 1;
    }

    int ret = 0;

    if (strcmp(cmd, "alloc") == 0) {
        uint16_t port = port_client_alloc(pc, pool_id);
        if (port == 0) {
            fprintf(stderr, "alloc failed: %s\n",
                    port_client_strerror(port_client_last_error(pc)));
            ret = 1;
        } else {
            printf("%u\n", port);
        }

    } else if (strcmp(cmd, "allocfrom") == 0) {
        if (i >= argc) { fprintf(stderr, "allocfrom requires a port number\n"); ret=1; goto done; }
        uint16_t desired = (uint16_t)atoi(argv[i]);
        uint16_t port = port_client_alloc_from(pc, pool_id, desired);
        if (port == 0) {
            fprintf(stderr, "allocfrom failed: %s\n",
                    port_client_strerror(port_client_last_error(pc)));
            ret = 1;
        } else {
            printf("%u\n", port);
        }

    } else if (strcmp(cmd, "free") == 0) {
        if (i >= argc) { fprintf(stderr, "free requires a port number\n"); ret=1; goto done; }
        uint16_t port = (uint16_t)atoi(argv[i]);
        int rc = port_client_free(pc, pool_id, port);
        if (rc != PORT_CLIENT_OK) {
            fprintf(stderr, "free(%u) failed: %s\n", port,
                    port_client_strerror(rc));
            ret = 1;
        } else {
            printf("ok\n");
        }

    } else if (strcmp(cmd, "status") == 0) {
        char json[PORTD_STATUS_JSON_MAX + 1];
        int rc = port_client_status(pc, pool_id, json, sizeof(json));
        if (rc != PORT_CLIENT_OK) {
            fprintf(stderr, "status failed: %s\n", port_client_strerror(rc));
            ret = 1;
        } else {
            /* Pretty-print JSON manually */
            printf("%s\n", json);
        }

    } else if (strcmp(cmd, "ping") == 0) {
        /* Alloc + immediate free to check liveness */
        uint16_t port = port_client_alloc(pc, pool_id);
        if (port != 0) {
            port_client_free(pc, pool_id, port);
            printf("pong (daemon alive, pool id=%u)\n", pool_id);
        } else {
            fprintf(stderr, "ping failed: %s\n",
                    port_client_strerror(port_client_last_error(pc)));
            ret = 1;
        }

    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        usage(argv[0]);
        ret = 1;
    }

done:
    port_client_disconnect(pc);
    return ret;
}
