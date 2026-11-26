/*
 * atomic_bench.c — small T3-D benchmark probe for the SRFI 230 substrate.
 *
 * This is a C probe, not the repository benchmark runner: it measures the
 * locked reference path and the T3-D scalar fast paths on one process.  Run
 * with NPL_ATOMIC_FORCE_LOCKED=1 to compare the same weak-order workloads
 * against the common locked domain.  No managed allocation happens in the
 * timed loops, so these are pure operation costs.
 */
#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now_seconds(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        perror("clock_gettime");
        exit(1);
    }
    return (double)value.tv_sec + (double)value.tv_nsec / 1e9;
}

static NplVal order_value(int order) { return npl_fixnum(order); }

static void report(const char *workload, int order, int objects,
                   long operations, double elapsed, int64_t checksum)
{
    double ns = elapsed * 1e9 / (double)operations;
    printf("%s,%d,%d,%ld,%.2f,%lld\n", workload, order, objects, operations,
           ns, (long long)checksum);
}

static void bench_fxbox_add(int order, int objects, long operations)
{
    NplVal *boxes = (NplVal *)malloc((size_t)objects * sizeof(NplVal));
    if (!boxes) exit(1);
    for (int i = 0; i < objects; i++) {
        boxes[i] = npl_atomic_fxbox_make(npl_fixnum(0));
        npl_gc_root(&boxes[i]);
    }
    int64_t checksum = 0;
    for (long i = 0; i < 100000; i++)
        checksum += npl_fixnum_value(npl_atomic_fxbox_add_fetch(
            boxes[i % objects], npl_fixnum(1), order_value(order)));
    double start = now_seconds();
    for (long i = 0; i < operations; i++)
        checksum += npl_fixnum_value(npl_atomic_fxbox_add_fetch(
            boxes[i % objects], npl_fixnum(1), order_value(order)));
    double elapsed = now_seconds() - start;
    report("fxbox+/fetch!", order, objects, operations, elapsed, checksum);
    for (int i = 0; i < objects; i++) npl_gc_unroot(&boxes[i]);
    free(boxes);
}

static void bench_box_ref(int order, int objects, long operations)
{
    NplVal *boxes = (NplVal *)malloc((size_t)objects * sizeof(NplVal));
    if (!boxes) exit(1);
    for (int i = 0; i < objects; i++) {
        boxes[i] = npl_atomic_box_make(npl_fixnum(i));
        npl_gc_root(&boxes[i]);
    }
    int64_t checksum = 0;
    for (long i = 0; i < 100000; i++)
        checksum += npl_fixnum_value(npl_atomic_box_ref(
            boxes[i % objects], order_value(order)));
    double start = now_seconds();
    for (long i = 0; i < operations; i++)
        checksum += npl_fixnum_value(npl_atomic_box_ref(
            boxes[i % objects], order_value(order)));
    double elapsed = now_seconds() - start;
    report("box-ref", order, objects, operations, elapsed, checksum);
    for (int i = 0; i < objects; i++) npl_gc_unroot(&boxes[i]);
    free(boxes);
}

static void bench_box_swap(int order, int objects, long operations)
{
    NplVal *boxes = (NplVal *)malloc((size_t)objects * sizeof(NplVal));
    if (!boxes) exit(1);
    for (int i = 0; i < objects; i++) {
        boxes[i] = npl_atomic_box_make(npl_fixnum(i));
        npl_gc_root(&boxes[i]);
    }
    int64_t checksum = 0;
    for (long i = 0; i < 100000; i++)
        checksum += npl_fixnum_value(npl_atomic_box_swap(
            boxes[i % objects], npl_fixnum(i), order_value(order)));
    double start = now_seconds();
    for (long i = 0; i < operations; i++)
        checksum += npl_fixnum_value(npl_atomic_box_swap(
            boxes[i % objects], npl_fixnum(i), order_value(order)));
    double elapsed = now_seconds() - start;
    report("box-swap!", order, objects, operations, elapsed, checksum);
    for (int i = 0; i < objects; i++) npl_gc_unroot(&boxes[i]);
    free(boxes);
}

static void bench_flag(int order, long operations)
{
    NplVal flag = npl_atomic_flag_make();
    npl_gc_root(&flag);
    int64_t checksum = 0;
    for (long i = 0; i < 100000; i++) {
        checksum += npl_atomic_flag_test_and_set(flag, order_value(order)) ==
                    npl_true();
        (void)npl_atomic_flag_clear(flag, order_value(order));
    }
    double start = now_seconds();
    for (long i = 0; i < operations; i++) {
        checksum += npl_atomic_flag_test_and_set(flag, order_value(order)) ==
                    npl_true();
        (void)npl_atomic_flag_clear(flag, order_value(order));
    }
    double elapsed = now_seconds() - start;
    report("flag-set+clear", order, 1, operations, elapsed, checksum);
    npl_gc_unroot(&flag);
}

static void bench_pair(int order, long operations)
{
    NplVal pair = npl_atomic_pair_make(npl_fixnum(0), npl_fixnum(0));
    npl_gc_root(&pair);
    int64_t checksum = 0;
    for (long i = 0; i < 100000; i++) {
        NplVal result = npl_atomic_pair_swap(pair, npl_fixnum(i),
                                             npl_fixnum(i), order_value(order));
        checksum += npl_fixnum_value(((NplValues *)npl_header(result))->data[0]);
    }
    double start = now_seconds();
    for (long i = 0; i < operations; i++) {
        NplVal result = npl_atomic_pair_swap(pair, npl_fixnum(i),
                                             npl_fixnum(i), order_value(order));
        checksum += npl_fixnum_value(((NplValues *)npl_header(result))->data[0]);
    }
    double elapsed = now_seconds() - start;
    report("pair-swap!", order, 1, operations, elapsed, checksum);
    npl_gc_unroot(&pair);
}

int main(void)
{
    npl_runtime_init();
    const long scalar_ops = 5000000;
    const long pair_ops = 1000000;
    puts("workload,order,objects,operations,ns_per_op,checksum");
    fflush(stdout);
    for (int order = 0; order <= 4; order++) {
        bench_fxbox_add(order, 1, scalar_ops);
        bench_fxbox_add(order, 8, scalar_ops);
        bench_box_ref(order, 1, scalar_ops);
        bench_box_swap(order, 1, scalar_ops);
        bench_flag(order, scalar_ops / 2);
        bench_pair(order, pair_ops);
    }
    return 0;
}
