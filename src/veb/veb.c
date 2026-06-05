/**
 * veb.c — van Emde Boas tree implementation (U = 65536).
 *
 * Return type uint32_t with VEB_NIL = UINT32_MAX allows all 65536 keys
 * (including 0 and 65535) to be stored without sentinel collision.
 *
 * MIT License — see LICENSE
 */
#include "../../include/veb.h"
#include "veb_internal.h"
#include <stdlib.h>
#include <string.h>

veb_t *veb_create(void)
{
    return (veb_t *)calloc(1, sizeof(veb_t));
}

veb_t *veb_create_full(uint16_t start, uint16_t end)
{
    if (start > end) return NULL;
    veb_t *v = (veb_t *)calloc(1, sizeof(veb_t));
    if (!v) return NULL;

    unsigned h_start = VEB_HIGH(start);
    unsigned h_end   = VEB_HIGH(end);

    for (unsigned h = h_start; h <= h_end; h++) {
        unsigned l_lo = (h == h_start) ? VEB_LOW(start) : 0u;
        unsigned l_hi = (h == h_end)   ? VEB_LOW(end)   : 255u;
        veb_cluster_t *cl = &v->clusters[h];
        for (unsigned l = l_lo; l <= l_hi; l++) {
            bits256_set(&cl->bits, (uint8_t)l);
            v->size++;
        }
        bits256_set(&v->summary, (uint8_t)h);
    }
    return v;
}

void veb_destroy(veb_t *v) { free(v); }

bool veb_contains(veb_t *v, uint16_t key)
{
    return bits256_test(&v->clusters[VEB_HIGH(key)].bits, VEB_LOW(key));
}

void veb_insert(veb_t *v, uint16_t key)
{
    uint8_t h = VEB_HIGH(key), l = VEB_LOW(key);
    if (__builtin_expect(bits256_test(&v->clusters[h].bits, l), 0)) return;
    bits256_set(&v->clusters[h].bits, l);
    bits256_set(&v->summary, h);
    v->size++;
}

void veb_delete(veb_t *v, uint16_t key)
{
    uint8_t h = VEB_HIGH(key), l = VEB_LOW(key);
    if (__builtin_expect(!bits256_test(&v->clusters[h].bits, l), 0)) return;
    bits256_clear(&v->clusters[h].bits, l);
    if (bits256_is_empty(&v->clusters[h].bits))
        bits256_clear(&v->summary, h);
    v->size--;
}

uint32_t veb_minimum(const veb_t *v)
{
    if (__builtin_expect(v->size == 0, 0)) return VEB_NIL;
    uint8_t h = bits256_ctz(&v->summary);
    uint8_t l = bits256_ctz(&v->clusters[h].bits);
    return VEB_COMPOSE(h, l);
}

uint32_t veb_maximum(const veb_t *v)
{
    if (__builtin_expect(v->size == 0, 0)) return VEB_NIL;
    int h = bits256_highest(&v->summary);
    if (h < 0) return VEB_NIL;
    int l = bits256_highest(&v->clusters[(uint8_t)h].bits);
    if (l < 0) return VEB_NIL;
    return VEB_COMPOSE((uint8_t)h, (uint8_t)l);
}

uint32_t veb_successor(const veb_t *v, uint16_t key)
{
    if (__builtin_expect(v->size == 0, 0)) return VEB_NIL;
    uint8_t h = VEB_HIGH(key), l = VEB_LOW(key);

    /* 1. Higher bit in same cluster */
    if (l < 255) {
        int found = bits256_lowest_from(&v->clusters[h].bits, (uint8_t)(l + 1));
        if (found < 256) return VEB_COMPOSE(h, (uint8_t)found);
    }

    /* 2. Next non-empty cluster */
    if (h < 255) {
        int next_h = bits256_lowest_from(&v->summary, (uint8_t)(h + 1));
        if (next_h < 256) {
            uint8_t next_l = bits256_ctz(&v->clusters[(uint8_t)next_h].bits);
            return VEB_COMPOSE((uint8_t)next_h, next_l);
        }
    }
    return VEB_NIL;
}

uint32_t veb_predecessor(const veb_t *v, uint16_t key)
{
    if (__builtin_expect(v->size == 0, 0)) return VEB_NIL;
    uint8_t h = VEB_HIGH(key), l = VEB_LOW(key);

    /* 1. Lower bit in same cluster */
    if (l > 0) {
        int found = bits256_highest_before(&v->clusters[h].bits, l);
        if (found >= 0) return VEB_COMPOSE(h, (uint8_t)found);
    }

    /* 2. Previous non-empty cluster */
    if (h > 0) {
        int prev_h = bits256_highest_before(&v->summary, h);
        if (prev_h >= 0) {
            int prev_l = bits256_highest(&v->clusters[(uint8_t)prev_h].bits);
            if (prev_l >= 0) return VEB_COMPOSE((uint8_t)prev_h, (uint8_t)prev_l);
        }
    }
    return VEB_NIL;
}

uint32_t veb_size(const veb_t *v)  { return v->size; }
bool     veb_empty(const veb_t *v) { return v->size == 0; }
