/**
 * port_manager.h — Thread-safe port pool manager built on vEB tree.
 *
 * A pool represents a contiguous range of ports. Multiple pools
 * can coexist (e.g. ephemeral NAT pool vs. reserved ranges).
 *
 * MIT License — see LICENSE
 */
#ifndef PORT_MANAGER_H
#define PORT_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returned on allocation failure */
#define PORT_NONE 0

/* Maximum number of pools per daemon instance */
#define POOL_MAX_COUNT 16

/* Maximum number of pools per daemon instance */
#define POOL_NAME_MAX 64

typedef struct port_pool port_pool_t;

/**
 * Pool statistics snapshot.
 */
typedef struct {
    uint16_t range_start;
    uint16_t range_end;
    uint32_t total;         /* total ports in pool */
    uint32_t free_count;    /* currently available */
    uint32_t allocated;     /* currently in use */
    uint64_t alloc_calls;   /* lifetime allocation count */
    uint64_t free_calls;    /* lifetime free count */
    uint64_t fail_calls;    /* allocation failures (pool exhausted) */
    char     name[POOL_NAME_MAX];
} pool_stats_t;

/**
 * pool_create(start, end, name)
 *   Create a pool owning [start..end] inclusive.
 *   All ports start as FREE (available for allocation).
 *   Returns NULL on failure (bad range, OOM).
 */
port_pool_t *pool_create(uint16_t start, uint16_t end, const char *name);

/** pool_destroy() — release pool resources. Safe to call with NULL. */
void pool_destroy(port_pool_t *p);

/**
 * pool_alloc(p) — allocate the lowest available port.
 *   Returns PORT_NONE (0) if pool is exhausted.
 *   Thread-safe.
 */
uint16_t pool_alloc(port_pool_t *p);

/**
 * pool_alloc_from(p, desired) — try to allocate exactly `desired`.
 *   If taken, allocate the next available port >= desired.
 *   Returns PORT_NONE if no port is available in [desired..end].
 *   Thread-safe.
 */
uint16_t pool_alloc_from(port_pool_t *p, uint16_t desired);

/**
 * pool_free(p, port) — return port to pool.
 *   Returns 0 on success, -1 if port is outside pool range or already free.
 *   Thread-safe.
 */
int pool_free(port_pool_t *p, uint16_t port);

/** pool_is_available(p, port) — true if port is free (not allocated). */
bool pool_is_available(port_pool_t *p, uint16_t port);

/** pool_get_stats(p, out) — fill *out with a snapshot of pool statistics. */
void pool_get_stats(const port_pool_t *p, pool_stats_t *out);

/** pool_reset(p) — mark all ports as free (reset to initial state). */
void pool_reset(port_pool_t *p);

#ifdef __cplusplus
}
#endif
#endif /* PORT_MANAGER_H */
