/**
 * veb.h — Public API for the van Emde Boas tree (uint16_t universe)
 *
 * Universe U = 65536 (port range 0..65535).
 * All operations run in O(log log U) = O(4) worst case.
 *
 * Return type for min/max/successor/predecessor is uint32_t.
 * VEB_NIL = UINT32_MAX is the sentinel for "not found" / "empty".
 * This allows all 65536 valid uint16_t keys (including 0 and 65535)
 * to be stored without ambiguity.
 *
 * Thread safety: NOT thread-safe. Use external locking (pool manager does this).
 *
 * MIT License — see LICENSE
 */
#ifndef VEB_H
#define VEB_H

#include <stdint.h>
#include <stdbool.h>

/* Sentinel: returned when tree is empty or key not found */
#define VEB_NIL UINT32_MAX

#ifdef __cplusplus
extern "C" {
#endif

typedef struct veb veb_t;

/** veb_create() — empty tree. Returns NULL on OOM. */
veb_t *veb_create(void);

/** veb_create_full(start, end) — pre-populate with [start..end]. */
veb_t *veb_create_full(uint16_t start, uint16_t end);

/** veb_destroy() — free all resources. Safe with NULL. */
void veb_destroy(veb_t *v);

/** veb_insert(v, key) — add key. No-op if already present. */
void veb_insert(veb_t *v, uint16_t key);

/** veb_delete(v, key) — remove key. No-op if absent. */
void veb_delete(veb_t *v, uint16_t key);

/** veb_contains(v, key) — true if key is in the set. */
bool veb_contains(veb_t *v, uint16_t key);

/** veb_minimum(v) — smallest element, or VEB_NIL if empty. */
uint32_t veb_minimum(const veb_t *v);

/** veb_maximum(v) — largest element, or VEB_NIL if empty. */
uint32_t veb_maximum(const veb_t *v);

/**
 * veb_successor(v, key) — smallest element strictly greater than key,
 * or VEB_NIL if none. key need not be in the set.
 */
uint32_t veb_successor(const veb_t *v, uint16_t key);

/**
 * veb_predecessor(v, key) — largest element strictly less than key,
 * or VEB_NIL if none.
 */
uint32_t veb_predecessor(const veb_t *v, uint16_t key);

/** veb_size(v) — number of elements currently in the set. */
uint32_t veb_size(const veb_t *v);

/** veb_empty(v) — true if set is empty. */
bool veb_empty(const veb_t *v);

#ifdef __cplusplus
}
#endif
#endif /* VEB_H */
