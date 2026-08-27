#ifndef NPL_SIMD_KERNELS_H
#define NPL_SIMD_KERNELS_H

#include <stddef.h>

/*
 * simd_kernels.h — Phase 9 NEON kernels (CPU SIMD tier, per DESIGN.md §5).
 *
 * Conventions:
 *   - All arrays are dense, 16-byte aligned by caller preference but NOT
 *     required; kernels use unaligned-safe vld1q/vst1q.
 *   - n >= 0; tail elements handled by scalar epilogue.
 *   - The SIMD variants live in simd_kernels.c (NEON intrinsics on arm64,
 *     portable scalar fallback elsewhere).
 *   - The *_scalar variants live in simd_kernels_scalar.c and serve as the
 *     reference for correctness checks and the >=3x speedup baseline.
 *     They are compiled with auto-vectorization disabled so they measure
 *     a true sequential-scalar loop.
 */

void  npl_simd_fadd(const float *a, const float *b, float *out, size_t n);
void  npl_simd_fmul(const float *a, const float *b, float *out, size_t n);
float npl_simd_fdot(const float *a, const float *b, size_t n);

void  npl_scalar_fadd(const float *a, const float *b, float *out, size_t n);
void  npl_scalar_fmul(const float *a, const float *b, float *out, size_t n);
float npl_scalar_fdot(const float *a, const float *b, size_t n);

#endif /* NPL_SIMD_KERNELS_H */

/* In-place element-wise add: a[i] = a[i] + b[i]. Halves memory traffic. */
void  npl_simd_fadd_ip(float *a, const float *b, size_t n);
void  npl_scalar_fadd_ip(float *a, const float *b, size_t n);
