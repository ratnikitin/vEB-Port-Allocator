/**
 * test_manager.c — Integration tests for port_pool_t.
 *
 * Tests:
 *   - Basic alloc/free cycle
 *   - Pool exhaustion
 *   - pool_alloc_from() exact and fallback
 *   - Double-free safety
 *   - Out-of-range free
 *   - Statistics accuracy
 *   - Thread-safety stress test (4 threads, 50k ops each)
 *
 * Build:
 *   gcc -O2 -Wall -pthread -o test_manager test_manager.c \
 *       ../src/manager/pool.c ../src/veb/veb.c ../src/utils/log.c \
 *       -I../include
 *
 * MIT License — see LICENSE
 */

#include "../include/port_manager.h"
#include "../include/veb.h"
#include "../src/utils/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

/* ── Mini test framework ─────────────────────────────────────────────── */

static int g_pass = 0, g_fail = 0;
#define CHECK(expr) do { \
    if (expr) { g_pass++; } \
    else { fprintf(stderr, "FAIL: %s  [%s:%d]\n", #expr, __FILE__, __LINE__); g_fail++; } \
} while(0)
#define SECTION(name) printf("\n--- %s ---\n", name)

/* ── Tests ───────────────────────────────────────────────────────────── */

static void test_basic(void)
{
    SECTION("Basic alloc/free");
    port_pool_t *p = pool_create(1024, 1035, "test");
    CHECK(p != NULL);

    uint16_t port = pool_alloc(p);
    CHECK(port == 1024); /* lowest free port */
    CHECK(!pool_is_available(p, 1024));

    uint16_t port2 = pool_alloc(p);
    CHECK(port2 == 1025);

    CHECK(pool_free(p, 1024) == 0);
    CHECK(pool_is_available(p, 1024));

    /* After freeing 1024, next alloc should return 1024 again */
    uint16_t port3 = pool_alloc(p);
    CHECK(port3 == 1024);

    pool_destroy(p);
}

static void test_exhaustion(void)
{
    SECTION("Pool exhaustion");
    port_pool_t *p = pool_create(5000, 5004, "tiny"); /* 5 ports */

    uint16_t ports[5];
    for (int i = 0; i < 5; i++) {
        ports[i] = pool_alloc(p);
        CHECK(ports[i] != PORT_NONE);
    }

    /* Pool is now empty */
    uint16_t extra = pool_alloc(p);
    CHECK(extra == PORT_NONE);

    pool_stats_t st;
    pool_get_stats(p, &st);
    CHECK(st.free_count == 0);
    CHECK(st.allocated == 5);
    CHECK(st.fail_calls >= 1);

    /* Free all */
    for (int i = 0; i < 5; i++) pool_free(p, ports[i]);
    pool_get_stats(p, &st);
    CHECK(st.free_count == 5);

    pool_destroy(p);
}

static void test_alloc_from(void)
{
    SECTION("pool_alloc_from");
    port_pool_t *p = pool_create(1024, 65535, "full");

    /* Exact desired port */
    uint16_t port = pool_alloc_from(p, 8080);
    CHECK(port == 8080);

    /* Desired is taken — should get next */
    uint16_t port2 = pool_alloc_from(p, 8080);
    CHECK(port2 == 8081);

    /* Out of range */
    uint16_t oob = pool_alloc_from(p, 100);
    CHECK(oob == PORT_NONE);

    pool_destroy(p);
}

static void test_double_free(void)
{
    SECTION("Double-free safety");
    port_pool_t *p = pool_create(2000, 3000, "df");
    uint16_t port = pool_alloc(p);
    CHECK(port != PORT_NONE);
    CHECK(pool_free(p, port) == 0);
    CHECK(pool_free(p, port) == -1); /* already free */
    pool_destroy(p);
}

static void test_out_of_range_free(void)
{
    SECTION("Out-of-range free");
    port_pool_t *p = pool_create(1024, 2048, "range");
    CHECK(pool_free(p, 1000) == -1); /* below range */
    CHECK(pool_free(p, 2049) == -1); /* above range */
    pool_destroy(p);
}

static void test_stats(void)
{
    SECTION("Statistics accuracy");
    port_pool_t *p = pool_create(3000, 3009, "stats"); /* 10 ports */

    pool_stats_t st;
    pool_get_stats(p, &st);
    CHECK(st.total      == 10);
    CHECK(st.free_count == 10);
    CHECK(st.allocated  == 0);

    uint16_t a = pool_alloc(p);
    uint16_t b = pool_alloc(p);
    uint16_t c = pool_alloc(p);
    (void)a; (void)b;

    pool_get_stats(p, &st);
    CHECK(st.allocated  == 3);
    CHECK(st.free_count == 7);
    CHECK(st.alloc_calls == 3);

    pool_free(p, c);
    pool_get_stats(p, &st);
    CHECK(st.allocated  == 2);
    CHECK(st.free_count == 8);
    CHECK(st.free_calls == 1);

    pool_destroy(p);
}

/* ── Thread-safety stress test ──────────────────────────────────────── */

#define NTHREADS  4
#define OPS_EACH  50000

typedef struct {
    port_pool_t *pool;
    int          thread_id;
    int          errors;
} thread_arg_t;

static void *stress_worker(void *arg)
{
    thread_arg_t *ta = (thread_arg_t *)arg;
    port_pool_t *p   = ta->pool;

    uint32_t rng = (uint32_t)(0x1337 * (ta->thread_id + 1));

    uint16_t held[32];
    int      held_count = 0;

    for (int i = 0; i < OPS_EACH; i++) {
        rng = rng * 1664525u + 1013904223u;

        if ((rng & 1) && held_count < 32) {
            uint16_t port = pool_alloc(p);
            if (port != PORT_NONE) {
                held[held_count++] = port;
            }
        } else if (held_count > 0) {
            int idx = (int)((rng >> 8) % (uint32_t)held_count);
            if (pool_free(p, held[idx]) != 0) {
                ta->errors++;
            }
            held[idx] = held[--held_count];
        }
    }

    /* Free remaining */
    for (int j = 0; j < held_count; j++) {
        pool_free(p, held[j]);
    }
    return NULL;
}

static void test_thread_safety(void)
{
    SECTION("Thread-safety stress (4 threads × 50k ops)");
    port_pool_t *p = pool_create(1024, 65535, "stress");

    pthread_t threads[NTHREADS];
    thread_arg_t args[NTHREADS];
    for (int i = 0; i < NTHREADS; i++) {
        args[i].pool = p;
        args[i].thread_id = i;
        args[i].errors = 0;
        pthread_create(&threads[i], NULL, stress_worker, &args[i]);
    }
    for (int i = 0; i < NTHREADS; i++) {
        pthread_join(threads[i], NULL);
        CHECK(args[i].errors == 0);
    }

    /* After all threads done, pool should be fully free */
    pool_stats_t st;
    pool_get_stats(p, &st);
    CHECK(st.free_count == st.total);
    printf("  Alloc calls: %llu  Free calls: %llu  Failures: %llu\n",
           (unsigned long long)st.alloc_calls,
           (unsigned long long)st.free_calls,
           (unsigned long long)st.fail_calls);

    pool_destroy(p);
}

static void test_reset(void)
{
    SECTION("Pool reset");
    port_pool_t *p = pool_create(1024, 1535, "reset"); /* 512 ports */
    /* Allocate half */
    for (int i = 0; i < 256; i++) pool_alloc(p);

    pool_stats_t st;
    pool_get_stats(p, &st);
    CHECK(st.allocated == 256);

    pool_reset(p);
    pool_get_stats(p, &st);
    CHECK(st.free_count == 512);
    CHECK(st.allocated  == 0);

    pool_destroy(p);
}

/* ── Main ────────────────────────────────────────────────────────────── */

int main(void)
{
    log_init(false, LOG_LEVEL_WARN, "test_manager");
    printf("=== Pool Manager Tests ===\n");

    test_basic();
    test_exhaustion();
    test_alloc_from();
    test_double_free();
    test_out_of_range_free();
    test_stats();
    test_thread_safety();
    test_reset();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
