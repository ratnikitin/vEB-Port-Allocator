/**
 * benchmark.c — Performance benchmark for vEB tree and pool manager.
 *
 * Measures:
 *   1. Raw vEB insert throughput
 *   2. Raw vEB delete (alloc) throughput — minimum() + delete()
 *   3. Pool alloc/free round-trip latency (ns per operation)
 *   4. pool_alloc_from() performance
 *
 * Reports median, p95, p99, min, max latency using a latency histogram.
 *
 * Build:
 *   gcc -O2 -Wall -o benchmark benchmark.c \
 *       ../src/veb/veb.c ../src/manager/pool.c ../src/utils/log.c \
 *       -I../include -lm
 *
 * MIT License — see LICENSE
 */

#define _GNU_SOURCE
#include "../include/veb.h"
#include "../include/port_manager.h"
#include "../src/utils/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#define WARMUP_OPS   100000
#define BENCH_OPS   1000000

/* ── Timer ───────────────────────────────────────────────────────────── */

static inline uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ── Stats helper ────────────────────────────────────────────────────── */

typedef struct {
    uint64_t *samples;
    size_t    count;
    size_t    capacity;
} latency_vec;

static void lv_init(latency_vec *lv, size_t cap)
{
    lv->samples  = (uint64_t *)malloc(cap * sizeof(uint64_t));
    lv->count    = 0;
    lv->capacity = cap;
}

static void lv_push(latency_vec *lv, uint64_t ns)
{
    if (lv->count < lv->capacity) lv->samples[lv->count++] = ns;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static void lv_print(const char *label, latency_vec *lv)
{
    qsort(lv->samples, lv->count, sizeof(uint64_t), cmp_u64);
    uint64_t min_v = lv->samples[0];
    uint64_t max_v = lv->samples[lv->count - 1];
    uint64_t med   = lv->samples[lv->count / 2];
    uint64_t p95   = lv->samples[(size_t)(lv->count * 0.95)];
    uint64_t p99   = lv->samples[(size_t)(lv->count * 0.99)];
    double   sum   = 0;
    for (size_t i = 0; i < lv->count; i++) sum += (double)lv->samples[i];
    double mean = sum / (double)lv->count;

    printf("%-30s  min=%4llu  med=%4llu  mean=%6.1f  p95=%5llu  "
           "p99=%5llu  max=%6llu  (ns/op)\n",
           label,
           (unsigned long long)min_v,
           (unsigned long long)med,
           mean,
           (unsigned long long)p95,
           (unsigned long long)p99,
           (unsigned long long)max_v);
}

/* ── Benchmark 1: raw vEB insert + delete ─────────────────────────────── */

static void bench_veb_raw(void)
{
    printf("\n[1] Raw vEB: insert + minimum + delete (simulating alloc)\n");

    veb_t *v = veb_create_full(1024, 65535);
    latency_vec lv;
    lv_init(&lv, BENCH_OPS);

    /* Warmup */
    for (int i = 0; i < WARMUP_OPS / 2; i++) {
        uint16_t p = veb_minimum(v); veb_delete(v, p);
        veb_insert(v, p);
    }

    for (int i = 0; i < BENCH_OPS; i++) {
        uint64_t t0 = now_ns();
        uint16_t p  = veb_minimum(v);
        veb_delete(v, p);
        uint64_t t1 = now_ns();
        veb_insert(v, p); /* restore */
        lv_push(&lv, t1 - t0);
    }
    lv_print("veb_minimum + veb_delete", &lv);
    free(lv.samples);
    veb_destroy(v);
}

/* ── Benchmark 2: pool alloc + free round-trip ─────────────────────────── */

static void bench_pool_roundtrip(void)
{
    printf("\n[2] Pool alloc + free round-trip\n");

    port_pool_t *p = pool_create(1024, 65535, "bench");
    latency_vec lv;
    lv_init(&lv, BENCH_OPS);

    /* Warmup */
    for (int i = 0; i < WARMUP_OPS; i++) {
        uint16_t port = pool_alloc(p);
        pool_free(p, port);
    }

    for (int i = 0; i < BENCH_OPS; i++) {
        uint64_t t0   = now_ns();
        uint16_t port = pool_alloc(p);
        pool_free(p, port);
        uint64_t t1   = now_ns();
        lv_push(&lv, (t1 - t0) / 2); /* per-operation */
    }
    lv_print("pool_alloc + pool_free / 2", &lv);
    free(lv.samples);
    pool_destroy(p);
}

/* ── Benchmark 3: alloc_from ────────────────────────────────────────── */

static void bench_alloc_from(void)
{
    printf("\n[3] pool_alloc_from (random desired port)\n");

    port_pool_t *p = pool_create(1024, 65535, "bench_from");
    latency_vec lv;
    lv_init(&lv, BENCH_OPS);

    uint32_t rng = 0xCAFEBABE;

    /* Warmup */
    for (int i = 0; i < WARMUP_OPS; i++) {
        rng = rng * 1664525u + 1013904223u;
        uint16_t desired = 1024 + (uint16_t)(rng % 64511);
        uint16_t port = pool_alloc_from(p, desired);
        if (port) pool_free(p, port);
    }

    for (int i = 0; i < BENCH_OPS; i++) {
        rng = rng * 1664525u + 1013904223u;
        uint16_t desired = 1024 + (uint16_t)(rng % 64511);

        uint64_t t0   = now_ns();
        uint16_t port = pool_alloc_from(p, desired);
        uint64_t t1   = now_ns();

        if (port) pool_free(p, port);
        lv_push(&lv, t1 - t0);
    }
    lv_print("pool_alloc_from", &lv);
    free(lv.samples);
    pool_destroy(p);
}

/* ── Benchmark 4: veb_successor walk ──────────────────────────────── */

static void bench_successor_scan(void)
{
    printf("\n[4] Full successor scan (iterate entire pool)\n");

    veb_t *v = veb_create_full(1024, 65535);
    uint64_t t0 = now_ns();
    uint32_t count = 0;
    uint32_t cur = veb_minimum(v);
    while (cur != VEB_NIL) {
        count++;
        cur = veb_successor(v, (uint16_t)cur);
    }
    uint64_t t1 = now_ns();
    printf("  Scanned %u ports in %llu ms  (%.1f ns/port)\n",
           count,
           (unsigned long long)((t1 - t0) / 1000000),
           (double)(t1 - t0) / count);
    veb_destroy(v);
}

/* ── Benchmark 5: throughput (ops/sec) ──────────────────────────────── */

static void bench_throughput(void)
{
    printf("\n[5] Throughput (alloc ops per second)\n");

    port_pool_t *p = pool_create(1024, 65535, "tp");
    uint64_t t0 = now_ns();
    uint64_t ops = 0;
    for (int i = 0; i < BENCH_OPS; i++) {
        uint16_t port = pool_alloc(p);
        pool_free(p, port);
        ops++;
    }
    uint64_t t1 = now_ns();
    double elapsed = (double)(t1 - t0) / 1e9;
    printf("  %llu alloc+free pairs in %.3f s  =>  %.0f Mops/s\n",
           (unsigned long long)ops, elapsed,
           (double)ops / elapsed / 1e6);
    pool_destroy(p);
}

/* ── Main ────────────────────────────────────────────────────────────── */

int main(void)
{
    log_init(false, LOG_LEVEL_NONE, "bench");
    printf("=== vEB Port Allocator Benchmark ===\n");
    printf("Ops per test: %d  (warmup: %d)\n", BENCH_OPS, WARMUP_OPS);

    bench_veb_raw();
    bench_pool_roundtrip();
    bench_alloc_from();
    bench_successor_scan();
    bench_throughput();

    printf("\n=== Done ===\n");
    return 0;
}
