/**
 * pool.c — Thread-safe port pool built on top of vEB tree.
 *
 * The vEB set holds FREE ports. Allocating = min() + delete(). Freeing = insert().
 *
 * MIT License — see LICENSE
 */

#include "../../include/port_manager.h"
#include "../../include/veb.h"
#include "../../src/utils/log.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>

struct port_pool {
    veb_t           *veb;
    uint16_t         range_start;
    uint16_t         range_end;
    uint32_t         total;
    char             name[POOL_NAME_MAX];
    pthread_mutex_t  lock;
    atomic_uint_fast64_t alloc_calls;
    atomic_uint_fast64_t free_calls;
    atomic_uint_fast64_t fail_calls;
};

port_pool_t *pool_create(uint16_t start, uint16_t end, const char *name)
{
    if (start > end) {
        log_error("pool_create: invalid range [%u..%u]", start, end);
        return NULL;
    }
    if (start == 0) {
        log_warn("pool_create: port 0 reserved; adjusting start to 1");
        start = 1;
    }

    port_pool_t *p = (port_pool_t *)calloc(1, sizeof(port_pool_t));
    if (!p) return NULL;

    p->veb = veb_create_full(start, end);
    if (!p->veb) { free(p); return NULL; }

    p->range_start = start;
    p->range_end   = end;
    p->total       = (uint32_t)(end - start + 1);
    strncpy(p->name, name ? name : "default", POOL_NAME_MAX - 1);

    if (pthread_mutex_init(&p->lock, NULL) != 0) {
        veb_destroy(p->veb); free(p);
        log_error("pool_create: mutex init failed: %s", strerror(errno));
        return NULL;
    }

    log_info("pool_create: pool '%s' [%u..%u] (%u ports)", p->name, start, end, p->total);
    return p;
}

void pool_destroy(port_pool_t *p)
{
    if (!p) return;
    pthread_mutex_destroy(&p->lock);
    veb_destroy(p->veb);
    free(p);
}

uint16_t pool_alloc(port_pool_t *p)
{
    atomic_fetch_add_explicit(&p->alloc_calls, 1, memory_order_relaxed);

    pthread_mutex_lock(&p->lock);
    uint32_t r = veb_minimum(p->veb);
    if (__builtin_expect(r == VEB_NIL, 0)) {
        pthread_mutex_unlock(&p->lock);
        atomic_fetch_add_explicit(&p->fail_calls, 1, memory_order_relaxed);
        log_warn("pool '%s': allocation failed — pool exhausted", p->name);
        return PORT_NONE;
    }
    uint16_t port = (uint16_t)r;
    veb_delete(p->veb, port);
    pthread_mutex_unlock(&p->lock);

    log_debug("pool '%s': allocated port %u", p->name, port);
    return port;
}

uint16_t pool_alloc_from(port_pool_t *p, uint16_t desired)
{
    atomic_fetch_add_explicit(&p->alloc_calls, 1, memory_order_relaxed);

    if (desired < p->range_start || desired > p->range_end) {
        atomic_fetch_add_explicit(&p->fail_calls, 1, memory_order_relaxed);
        return PORT_NONE;
    }

    pthread_mutex_lock(&p->lock);
    uint16_t port;
    if (veb_contains(p->veb, desired)) {
        port = desired;
    } else {
        /* next available >= desired: use successor of (desired-1)
           but if desired==0 use minimum */
        uint32_t r;
        if (desired == 0) {
            r = veb_minimum(p->veb);
        } else {
            r = veb_successor(p->veb, (uint16_t)(desired - 1));
        }
        if (r == VEB_NIL || (uint16_t)r > p->range_end) {
            pthread_mutex_unlock(&p->lock);
            atomic_fetch_add_explicit(&p->fail_calls, 1, memory_order_relaxed);
            return PORT_NONE;
        }
        port = (uint16_t)r;
    }
    veb_delete(p->veb, port);
    pthread_mutex_unlock(&p->lock);

    log_debug("pool '%s': alloc_from(%u) -> %u", p->name, desired, port);
    return port;
}

int pool_free(port_pool_t *p, uint16_t port)
{
    atomic_fetch_add_explicit(&p->free_calls, 1, memory_order_relaxed);

    if (port < p->range_start || port > p->range_end) {
        log_warn("pool '%s': free(%u) out of range [%u..%u]",
                 p->name, port, p->range_start, p->range_end);
        return -1;
    }

    pthread_mutex_lock(&p->lock);
    if (veb_contains(p->veb, port)) {
        pthread_mutex_unlock(&p->lock);
        log_warn("pool '%s': free(%u) — port already free", p->name, port);
        return -1;
    }
    veb_insert(p->veb, port);
    pthread_mutex_unlock(&p->lock);

    log_debug("pool '%s': freed port %u", p->name, port);
    return 0;
}

bool pool_is_available(port_pool_t *p, uint16_t port)
{
    pthread_mutex_lock(&p->lock);
    bool avail = veb_contains(p->veb, port);
    pthread_mutex_unlock(&p->lock);
    return avail;
}

void pool_get_stats(const port_pool_t *p, pool_stats_t *out)
{
    pthread_mutex_lock((pthread_mutex_t *)&p->lock);
    uint32_t free_now = veb_size(p->veb);
    pthread_mutex_unlock((pthread_mutex_t *)&p->lock);

    out->range_start = p->range_start;
    out->range_end   = p->range_end;
    out->total       = p->total;
    out->free_count  = free_now;
    out->allocated   = p->total - free_now;
    out->alloc_calls = atomic_load_explicit(&p->alloc_calls, memory_order_relaxed);
    out->free_calls  = atomic_load_explicit(&p->free_calls,  memory_order_relaxed);
    out->fail_calls  = atomic_load_explicit(&p->fail_calls,  memory_order_relaxed);
    strncpy(out->name, p->name, POOL_NAME_MAX - 1);
    out->name[POOL_NAME_MAX - 1] = '\0';
}

void pool_reset(port_pool_t *p)
{
    pthread_mutex_lock(&p->lock);
    veb_destroy(p->veb);
    p->veb = veb_create_full(p->range_start, p->range_end);
    pthread_mutex_unlock(&p->lock);
    log_info("pool '%s': reset to full", p->name);
}
