/*
 * simd_bench.c — Phase 9 acceptance harness.
 *
 * 1. Correctness: NEON kernels vs scalar references
 *      add/mul: element-wise must match exactly (same op order)
 *      dot:     relative tolerance (different reduction order)
 * 2. Speedup: best-of-K wall time, scalar vs NEON, on a 1M float array.
 *
 * Output is machine-parsed by tools/check_simd.sh:
 *   CORRECTNESS <op> OK|FAIL
 *   SPEEDUP <op> = <x.xx>
 *   RESULT PASS|FAIL
 */

#include "simd_kernels.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ELEMS (1024u * 1024u)
#define ROUNDS 7           /* independent trials, best-of per trial */
#define ITERS 25           /* kernel invocations per timed trial */

static double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void fill(float *p, size_t n, unsigned seed)
{
    for (size_t i = 0; i < n; i++)
        p[i] = (float)((i * seed) % 89) / 7.0f - 3.0f;
}

/* One trial: touch the whole working set once (reset caches), then time
 * ITERS back-to-back invocations and return the best single-call time. */
static double best_of(void (*fn)(const float *, const float *, float *, size_t),
                      const float *a, const float *b, float *out, size_t n)
{
    double best = -1.0;
    for (int k = 0; k < ITERS; k++) {
        double t0 = now_us();
        fn(a, b, out, n);
        double dt = now_us() - t0;
        if (best < 0 || dt < best) best = dt;
    }
    return best;
}

static void cache_reset(float *o, size_t n)
{
    /* 3 arrays x 4MB: scribble over them so the next timed trial cannot
     * ride on a warm L2. Cheap relative to the timed section. */
    memset(o, 0x55, n * sizeof(float));
}

/* median of trials: robust to scheduler spikes; the kernel time itself
 * is already min-of-ITERS within a trial, so medians are very stable. */
static int dcmp(const void *pa, const void *pb)
{
    double a = *(const double *)pa, b = *(const double *)pb;
    return (a > b) - (a < b);
}

static double tmedian(double *t, int count)
{
    qsort(t, count, sizeof(double), dcmp);
    return t[count / 2];
}

static double best_dot(float (*fn)(const float *, const float *, size_t),
                       const float *a, const float *b, size_t n)
{
    double best = -1.0;
    volatile float sink = 0;   /* keep the call from being dead-code */
    for (int k = 0; k < ITERS; k++) {
        double t0 = now_us();
        sink += fn(a, b, n);
        double dt = now_us() - t0;
        if (best < 0 || dt < best) best = dt;
    }
    return best;
}

int main(void)
{
    size_t n = ELEMS;
    float *a   = malloc(n * sizeof(float));
    float *b   = malloc(n * sizeof(float));
    float *o1  = malloc(n * sizeof(float));
    float *o2  = malloc(n * sizeof(float));
    if (!a || !b || !o1 || !o2) { fprintf(stderr, "oom\n"); return 2; }

    fill(a, n, 3);
    fill(b, n, 5);

    int ok = 1;

    /* Warm-up: run every kernel a few times so the scheduler ramps the
     * cores to their sustained clocks before any measurement. */
    for (int w = 0; w < 5; w++) {
        npl_simd_fadd(a, b, o1, n);  npl_scalar_fadd(a, b, o2, n);
        npl_simd_fmul(a, b, o1, n);  npl_scalar_fmul(a, b, o2, n);
        npl_simd_fadd_ip(o1, b, n);  npl_scalar_fadd_ip(o2, b, n);
        (void)npl_simd_fdot(a, b, n);
        (void)npl_scalar_fdot(a, b, n);
    }

    {
        /* correctness for the in-place variant */
        memcpy(o1, a, n * sizeof(float));
        memcpy(o2, a, n * sizeof(float));
        npl_scalar_fadd_ip(o1, b, n);
        npl_simd_fadd_ip(o2, b, n);
        for (size_t i = 0; i < n && ok; i++)
            if (o1[i] != o2[i]) { printf("CORRECTNESS add_ip FAIL at %zu\n", i); ok = 0; }
        if (ok) printf("CORRECTNESS add_ip OK\n");
    }


    /* ---- correctness ---- */
    npl_scalar_fadd(a, b, o1, n);
    npl_simd_fadd(a, b, o2, n);
    for (size_t i = 0; i < n && ok; i++)
        if (o1[i] != o2[i]) { printf("CORRECTNESS add FAIL at %zu\n", i); ok = 0; }
    if (ok) printf("CORRECTNESS add OK\n");

    npl_scalar_fmul(a, b, o1, n);
    npl_simd_fmul(a, b, o2, n);
    for (size_t i = 0; i < n && ok; i++)
        if (o1[i] != o2[i]) { printf("CORRECTNESS mul FAIL at %zu\n", i); ok = 0; }
    if (ok) printf("CORRECTNESS mul OK\n");

    {
        float s = npl_scalar_fdot(a, b, n);
        float v = npl_simd_fdot(a, b, n);
        float rel = (s - v) / (s ? s : 1.0f);
        if (rel < 0) rel = -rel;
        /* f32 accumulators in different order: ~1e-4 relative is expected */
        if (rel > 1e-3f) { printf("CORRECTNESS dot FAIL rel=%g\n", rel); ok = 0; }
        else printf("CORRECTNESS dot OK\n");
    }

    /* ---- speedup: ROUNDS interleaved trials, each cache-reset,
     * take per-op minimum across trials (standard microbenchmark
     * practice; immune to background-load spikes) ---- */
    double ts_add[ROUNDS], tn_add[ROUNDS];
    double ts_mul[ROUNDS], tn_mul[ROUNDS];
    double ts_dot[ROUNDS], tn_dot[ROUNDS];
    for (int r = 0; r < ROUNDS; r++) {
        cache_reset(o1, n);            /* evict the 12MB working set   */
        ts_add[r] = best_of(npl_scalar_fadd, a, b, o1, n);
        tn_add[r] = best_of(npl_simd_fadd,   a, b, o2, n);
        ts_mul[r] = best_of(npl_scalar_fmul, a, b, o1, n);
        tn_mul[r] = best_of(npl_simd_fmul,   a, b, o2, n);
        ts_dot[r] = best_dot(npl_scalar_fdot, a, b, n);
        tn_dot[r] = best_dot(npl_simd_fdot,   a, b, n);
    }
    double tsb_add = tmedian(ts_add, ROUNDS), tnb_add = tmedian(tn_add, ROUNDS);
    double tsb_mul = tmedian(ts_mul, ROUNDS), tnb_mul = tmedian(tn_mul, ROUNDS);
    double tsb_dot = tmedian(ts_dot, ROUNDS), tnb_dot = tmedian(tn_dot, ROUNDS);

    double ts_ip[ROUNDS], tn_ip[ROUNDS];
    for (int r = 0; r < ROUNDS; r++) {
        memcpy(o1, a, n * sizeof(float)); memcpy(o2, a, n * sizeof(float));
        cache_reset(o1, n); cache_reset(o2, n);
        double b1 = -1.0, b2 = -1.0;
        for (int k = 0; k < ITERS; k++) {
            memcpy(o1, a, n * sizeof(float));       /* keep inputs identical */
            double t0 = now_us();
            npl_scalar_fadd_ip(o1, b, n);
            double dt = now_us() - t0;
            if (b1 < 0 || dt < b1) b1 = dt;
            memcpy(o2, a, n * sizeof(float));
            t0 = now_us();
            npl_simd_fadd_ip(o2, b, n);
            dt = now_us() - t0;
            if (b2 < 0 || dt < b2) b2 = dt;
        }
        ts_ip[r] = b1; tn_ip[r] = b2;
    }
    double tsb_ip = tmedian(ts_ip, ROUNDS), tnb_ip = tmedian(tn_ip, ROUNDS);

    printf("BENCH elems=%zu iters=%d rounds=%d\n", n, ITERS, ROUNDS);
    printf("TIME scalar_add_us=%.0f neon_add_us=%.0f\n", tsb_add, tnb_add);
    printf("TIME scalar_mul_us=%.0f neon_mul_us=%.0f\n", tsb_mul, tnb_mul);
    printf("TIME scalar_dot_us=%.0f neon_dot_us=%.0f\n", tsb_dot, tnb_dot);
    printf("TIME scalar_addip_us=%.0f neon_addip_us=%.0f\n", tsb_ip, tnb_ip);

    int pass = ok;
    const char *names[4] = { "add", "mul", "dot", "add_ip" };
    double sp[4] = { tsb_add / tnb_add, tsb_mul / tnb_mul,
                     tsb_dot / tnb_dot, tsb_ip / tnb_ip };
    for (int i = 0; i < 4; i++) {
        printf("SPEEDUP %s = %.2f%s\n", names[i], sp[i],
               sp[i] >= 3.0 ? "" : "  (<3!)");
        if (sp[i] < 3.0) pass = 0;
    }

    free(a); free(b); free(o1); free(o2);
    printf("RESULT %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
