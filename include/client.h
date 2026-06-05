/**
 * client.h — Client library for communicating with portd daemon.
 *
 * Connects to portd via Unix domain socket or TCP.
 * Provides a simple synchronous API for port allocation/release.
 *
 * Thread safety: a port_client_t handle must not be shared across
 * threads without external locking. Create one handle per thread or
 * use a connection pool.
 *
 * MIT License — see LICENSE
 */
#ifndef PORTD_CLIENT_H
#define PORTD_CLIENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes returned by client functions */
#define PORT_CLIENT_OK         0
#define PORT_CLIENT_ERR_CONN  -1   /* connection failure */
#define PORT_CLIENT_ERR_IO    -2   /* send/recv failure */
#define PORT_CLIENT_ERR_PROTO -3   /* unexpected response */
#define PORT_CLIENT_ERR_PARAM -4   /* bad parameter */
#define PORT_CLIENT_ERR_FULL  -5   /* pool exhausted */
#define PORT_CLIENT_ERR_RANGE -6   /* port out of range / already free */

typedef struct port_client port_client_t;

/**
 * port_client_connect(addr)
 *   Connect to portd at `addr`.
 *   Accepted formats:
 *     "unix:///var/run/portd.sock"   (Unix domain socket)
 *     "tcp://127.0.0.1:9500"         (TCP socket)
 *   Returns NULL on failure. Call port_client_strerror() for details.
 */
port_client_t *port_client_connect(const char *addr);

/** port_client_disconnect() — close connection and free handle. */
void port_client_disconnect(port_client_t *pc);

/**
 * port_client_alloc(pc, pool_id)
 *   Allocate the lowest free port from pool `pool_id`.
 *   Returns the allocated port (>0) on success, or 0 on failure.
 *   Sets internal error code accessible via port_client_last_error().
 */
uint16_t port_client_alloc(port_client_t *pc, uint8_t pool_id);

/**
 * port_client_alloc_from(pc, pool_id, desired)
 *   Allocate `desired` port or the next available one >= desired.
 *   Returns allocated port or 0 on failure.
 */
uint16_t port_client_alloc_from(port_client_t *pc, uint8_t pool_id,
                                 uint16_t desired);

/**
 * port_client_free(pc, pool_id, port)
 *   Return `port` to pool `pool_id`.
 *   Returns PORT_CLIENT_OK on success, negative error code otherwise.
 */
int port_client_free(port_client_t *pc, uint8_t pool_id, uint16_t port);

/**
 * port_client_status(pc, pool_id, out_json, out_len)
 *   Request pool status as JSON string.
 *   Writes into out_json buffer of out_len bytes.
 *   Returns PORT_CLIENT_OK on success.
 */
int port_client_status(port_client_t *pc, uint8_t pool_id,
                       char *out_json, uint32_t out_len);

/** port_client_last_error() — last error code from this handle. */
int port_client_last_error(const port_client_t *pc);

/** port_client_strerror(err) — human-readable description of error code. */
const char *port_client_strerror(int err);

#ifdef __cplusplus
}
#endif
#endif /* PORTD_CLIENT_H */
