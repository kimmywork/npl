/*
 * simd_kernels_scalar.c — sequential-scalar reference kernels.
 *
 * Compiled with -fno-vectorize/-fno-slp-vectorize (see CMakeLists) so the
 * Phase-9 speedup baseline measures plain scalar loops against the hand
 * written NEON kernels. Keep this file free of intrinsics and pragmas.
 */

#include "simd_kernels.h"

void npl_scalar_fadd(const float *a, const float *b, float *out, size_t n)
{
    for (size_t i = 0; i < n; i++)
        out[i] = a[i] + b[i];
}

void npl_scalar_fmul(const float *a, const float *b, float *out, size_t n)
{
    for (size_t i = 0; i < n; i++)
        out[i] = a[i] * b[i];
}

float npl_scalar_fdot(const float *a, const float *b, size_t n)
{
    float s = 0.0f;
    for (size_t i = 0; i < n; i++)
        s += a[i] * b[i];
    return s;
}

void npl_scalar_fadd_ip(float *a, const float *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        a[i] += b[i];
}
