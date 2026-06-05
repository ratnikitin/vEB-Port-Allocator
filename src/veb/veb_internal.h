/**
 * veb_internal.h — Internal structures for the 2-level vEB tree.
 *
 * Universe U = 65536 (uint16_t).
 *   high(k) = k >> 8          (cluster index,  0..255)
 *   low(k)  = k & 0xFF        (position inside cluster, 0..255)
 *
 * NOT to be included by external code.
 */
#ifndef VEB_INTERNAL_H
#define VEB_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>

/* 256-bit word: 4 × 64-bit lanes */
typedef struct {
    uint64_t w[4];
} bits256_t;

/* ── Bit-manipulation helpers ────────────────────────────────────────── */

static inline void bits256_set(bits256_t *b, uint8_t pos) {
    b->w[pos >> 6] |= (1ULL << (pos & 63));
}

static inline void bits256_clear(bits256_t *b, uint8_t pos) {
    b->w[pos >> 6] &= ~(1ULL << (pos & 63));
}

static inline bool bits256_test(const bits256_t *b, uint8_t pos) {
    return (b->w[pos >> 6] >> (pos & 63)) & 1ULL;
}

static inline bool bits256_is_empty(const bits256_t *b) {
    return (b->w[0] | b->w[1] | b->w[2] | b->w[3]) == 0ULL;
}

/* bits256_ctz — index of lowest set bit. Returns 255 if empty (caller checks). */
static inline uint8_t bits256_ctz(const bits256_t *b) {
    if (b->w[0]) return (uint8_t)__builtin_ctzll(b->w[0]);
    if (b->w[1]) return (uint8_t)(64  + __builtin_ctzll(b->w[1]));
    if (b->w[2]) return (uint8_t)(128 + __builtin_ctzll(b->w[2]));
    if (b->w[3]) return (uint8_t)(192 + __builtin_ctzll(b->w[3]));
    return 255; /* empty sentinel */
}

/* bits256_lowest_from — lowest set bit >= pos, or 256 if none. */
static inline int bits256_lowest_from(const bits256_t *b, uint8_t pos) {
    uint8_t lane = pos >> 6;
    uint8_t bit  = pos & 63;
    uint64_t masked = b->w[lane] & (~0ULL << bit);
    if (masked) return (int)(lane * 64 + __builtin_ctzll(masked));
    for (uint8_t l = lane + 1; l < 4; l++) {
        if (b->w[l]) return (int)(l * 64 + __builtin_ctzll(b->w[l]));
    }
    return 256;
}

/* bits256_highest_before — highest set bit < pos, or -1 if none. */
static inline int bits256_highest_before(const bits256_t *b, uint8_t pos) {
    if (pos == 0) return -1;
    uint8_t lane = (pos - 1) >> 6;
    uint8_t bit  = (pos - 1) & 63;
    uint64_t masked = b->w[lane] & (~0ULL >> (63 - bit));
    if (masked) return (int)(lane * 64 + 63 - __builtin_clzll(masked));
    for (int l = (int)lane - 1; l >= 0; l--) {
        if (b->w[l]) return l * 64 + 63 - __builtin_clzll(b->w[l]);
    }
    return -1;
}

/* bits256_highest — highest set bit, or -1 if empty. */
static inline int bits256_highest(const bits256_t *b) {
    for (int l = 3; l >= 0; l--) {
        if (b->w[l]) return l * 64 + 63 - __builtin_clzll(b->w[l]);
    }
    return -1;
}

/* ── Tree structure ──────────────────────────────────────────────────── */

/* NOTE: no count field — we use bits256_is_empty() to avoid uint8_t overflow
   when a cluster is fully populated (256 entries, wraps uint8_t to 0). */
typedef struct {
    bits256_t bits; /* bit i = key compose(cluster_idx, i) in set */
} veb_cluster_t;

struct veb {
    bits256_t      summary;        /* bit j = cluster[j] non-empty       */
    veb_cluster_t  clusters[256];  /* 256 clusters × 256 bits each        */
    uint32_t       size;           /* total elements in set               */
};

/* ── Key decomposition ───────────────────────────────────────────────── */

#define VEB_HIGH(k)       ((uint8_t)((k) >> 8))
#define VEB_LOW(k)        ((uint8_t)((k) & 0xFF))
#define VEB_COMPOSE(h, l) ((uint16_t)(((unsigned)(h) << 8) | (unsigned)(l)))

#endif /* VEB_INTERNAL_H */
