/**
 * test_veb.c — Unit tests for the vEB tree.
 *
 * Build:
 *   gcc -O2 -Wall -o test_veb test_veb.c ../src/veb/veb.c -I../include
 */

#include "../include/veb.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(expr) do { \
    if (expr) { g_pass++; } \
    else { fprintf(stderr, "FAIL: %s  [%s:%d]\n", #expr, __FILE__, __LINE__); g_fail++; } \
} while(0)
#define SECTION(name) printf("\n--- %s ---\n", name)

static void test_empty(void)
{
    SECTION("Empty tree");
    veb_t *v = veb_create();
    CHECK(v != NULL);
    CHECK(veb_empty(v));
    CHECK(veb_size(v) == 0);
    CHECK(veb_minimum(v) == VEB_NIL);
    CHECK(veb_maximum(v) == VEB_NIL);
    CHECK(veb_successor(v, 0) == VEB_NIL);
    CHECK(veb_successor(v, 65534) == VEB_NIL);
    CHECK(veb_predecessor(v, 65535) == VEB_NIL);
    CHECK(!veb_contains(v, 0));
    CHECK(!veb_contains(v, 1024));
    CHECK(!veb_contains(v, 65535));
    veb_destroy(v);
}

static void test_single(void)
{
    SECTION("Single element");
    veb_t *v = veb_create();

    veb_insert(v, 1024);
    CHECK(!veb_empty(v));
    CHECK(veb_size(v) == 1);
    CHECK(veb_contains(v, 1024));
    CHECK(!veb_contains(v, 1023));
    CHECK(!veb_contains(v, 1025));
    CHECK(veb_minimum(v) == 1024);
    CHECK(veb_maximum(v) == 1024);
    CHECK(veb_successor(v, 1024) == VEB_NIL);
    CHECK(veb_predecessor(v, 1024) == VEB_NIL);
    CHECK(veb_successor(v, 0) == 1024);
    CHECK(veb_predecessor(v, 65535) == 1024);

    veb_delete(v, 1024);
    CHECK(veb_empty(v));
    CHECK(!veb_contains(v, 1024));
    CHECK(veb_minimum(v) == VEB_NIL);
    veb_destroy(v);
}

static void test_boundaries(void)
{
    SECTION("Boundary values (0 and 65535)");
    veb_t *v = veb_create();

    veb_insert(v, 0);
    veb_insert(v, 65535);
    CHECK(veb_size(v) == 2);
    CHECK(veb_minimum(v) == 0);
    CHECK(veb_maximum(v) == 65535);
    CHECK(veb_successor(v, 0) == 65535);
    CHECK(veb_predecessor(v, 65535) == 0);

    /* 65535 is a valid key, not VEB_NIL (which is UINT32_MAX = 4294967295) */
    CHECK(veb_successor(v, 0) != VEB_NIL);
    CHECK(veb_successor(v, 65535) == VEB_NIL);

    veb_delete(v, 0);
    CHECK(veb_minimum(v) == 65535);
    veb_delete(v, 65535);
    CHECK(veb_empty(v));
    veb_destroy(v);
}

static void test_successor_predecessor(void)
{
    SECTION("Successor / predecessor across cluster boundaries");
    veb_t *v = veb_create();

    veb_insert(v, 0x00FF);
    veb_insert(v, 0x0100);
    veb_insert(v, 0x01FF);
    veb_insert(v, 0x0200);
    veb_insert(v, 0xFF00);

    CHECK(veb_successor(v, 0x00FF) == 0x0100);
    CHECK(veb_successor(v, 0x01FF) == 0x0200);
    CHECK(veb_successor(v, 0x0200) == 0xFF00);
    CHECK(veb_successor(v, 0xFF00) == VEB_NIL);

    CHECK(veb_predecessor(v, 0x0100) == 0x00FF);
    CHECK(veb_predecessor(v, 0x0200) == 0x01FF);
    CHECK(veb_predecessor(v, 0xFF00) == 0x0200);
    CHECK(veb_predecessor(v, 0x00FF) == VEB_NIL);
    veb_destroy(v);
}

static void test_duplicate(void)
{
    SECTION("Duplicate insert / double free idempotency");
    veb_t *v = veb_create();
    veb_insert(v, 5000);
    veb_insert(v, 5000);
    CHECK(veb_size(v) == 1);
    veb_delete(v, 5000);
    veb_delete(v, 5000);
    CHECK(veb_size(v) == 0);
    veb_destroy(v);
}

static void test_create_full(void)
{
    SECTION("veb_create_full [1024..2047]");
    veb_t *v = veb_create_full(1024, 2047);
    CHECK(v != NULL);
    CHECK(veb_size(v) == 1024);
    CHECK(veb_minimum(v) == 1024);
    CHECK(veb_maximum(v) == 2047);
    CHECK(veb_contains(v, 1024));
    CHECK(veb_contains(v, 2047));
    CHECK(!veb_contains(v, 1023));
    CHECK(!veb_contains(v, 2048));

    /* Sequential extraction must yield exactly 1024, 1025, ..., 2047 */
    for (uint32_t p = 1024; p <= 2047; p++) {
        uint32_t m = veb_minimum(v);
        CHECK(m == p);
        veb_delete(v, (uint16_t)m);
    }
    CHECK(veb_empty(v));
    veb_destroy(v);
}

static void test_full_cluster(void)
{
    SECTION("Full 256-port cluster (boundary of uint8_t count overflow)");
    /* Cluster 4 = ports 0x0400..0x04FF = 1024..1279 (256 ports exactly) */
    veb_t *v = veb_create_full(0x0400, 0x04FF);
    CHECK(veb_size(v) == 256);
    CHECK(veb_minimum(v) == 0x0400);
    CHECK(veb_maximum(v) == 0x04FF);

    /* Extract all 256 */
    uint32_t extracted = 0;
    uint32_t expected  = 0x0400;
    uint32_t cur = veb_minimum(v);
    while (cur != VEB_NIL) {
        CHECK(cur == expected);
        veb_delete(v, (uint16_t)cur);
        expected++;
        extracted++;
        cur = veb_minimum(v);
    }
    CHECK(extracted == 256);
    CHECK(veb_empty(v));
    veb_destroy(v);
}

static void test_random_pattern(void)
{
    SECTION("Pseudo-random insert/delete/successor cycle (200k ops)");
    veb_t  *v   = veb_create();
    uint8_t ref[65536] = {0};
    uint32_t count = 0;
    uint32_t rng = 0xDEADBEEF;

    for (int iter = 0; iter < 200000; iter++) {
        rng = rng * 1664525u + 1013904223u;
        uint16_t key = (uint16_t)(rng >> 16);
        int op = rng & 1;
        if (op == 0) {
            if (!ref[key]) { veb_insert(v, key); ref[key]=1; count++; }
        } else {
            if (ref[key])  { veb_delete(v, key); ref[key]=0; count--; }
        }
    }

    CHECK(veb_size(v) == count);

    /* Full successor walk — count every element */
    uint32_t veb_seen = 0;
    uint32_t cur = veb_minimum(v);
    while (cur != VEB_NIL) {
        CHECK(ref[(uint16_t)cur] == 1);
        veb_seen++;
        cur = veb_successor(v, (uint16_t)cur);
    }
    CHECK(veb_seen == count);
    veb_destroy(v);
}

static void test_full_range(void)
{
    SECTION("Full 64K range [0..65535]: insert all, delete evens, walk odds");
    veb_t *v = veb_create_full(0, 65535);
    CHECK(veb_size(v) == 65536);
    CHECK(veb_minimum(v) == 0);
    CHECK(veb_maximum(v) == 65535);

    /* Delete all evens */
    for (uint32_t k = 0; k <= 65534; k += 2)
        veb_delete(v, (uint16_t)k);

    CHECK(veb_size(v) == 32768);
    CHECK(veb_minimum(v) == 1);
    CHECK(veb_maximum(v) == 65535);

    /* Walk all odds via successor */
    uint32_t count = 0;
    uint32_t cur = veb_minimum(v);
    uint32_t prev = VEB_NIL;
    while (cur != VEB_NIL) {
        CHECK((cur & 1) == 1);
        if (prev != VEB_NIL) CHECK(cur == prev + 2);
        count++;
        prev = cur;
        cur = veb_successor(v, (uint16_t)cur);
    }
    CHECK(count == 32768);
    veb_destroy(v);
}

static void test_max_key_65535(void)
{
    SECTION("Key 65535 is valid (not confused with VEB_NIL=UINT32_MAX)");
    veb_t *v = veb_create();
    veb_insert(v, 65535);
    CHECK(veb_contains(v, 65535));
    CHECK(veb_minimum(v) == 65535);
    CHECK(veb_maximum(v) == 65535);
    /* 65535 != VEB_NIL (UINT32_MAX) */
    CHECK(veb_minimum(v) != VEB_NIL);
    /* successor of 65535 should be VEB_NIL (nothing beyond) */
    CHECK(veb_successor(v, 65535) == VEB_NIL);
    
    /* Insert another key and check successor chain reaches 65535 */
    veb_insert(v, 100);
    CHECK(veb_successor(v, 100) == 65535);
    CHECK(veb_successor(v, 65535) == VEB_NIL);
    veb_destroy(v);
}

int main(void)
{
    printf("=== vEB Unit Tests ===\n");
    test_empty();
    test_single();
    test_boundaries();
    test_successor_predecessor();
    test_duplicate();
    test_create_full();
    test_full_cluster();
    test_random_pattern();
    test_full_range();
    test_max_key_65535();
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
