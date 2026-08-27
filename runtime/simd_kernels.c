/*
 * simd_kernels.c — NEON implementations (Phase 9).
 *
 * Strategy per DESIGN.md §5: precompiled C kernels with NEON intrinsics,
 * called from NPL-generated code via QBE `call`. Two q-register blocks
 * per iteration to give the pipeline independent work; scalar epilogue
 * for the tail.
 */

#include "simd_kernels.h"

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#define NPL_HAVE_NEON 1
#endif

void npl_simd_fadd(const float *a, const float *b, float *out, size_t n)
{
#if defined(NPL_HAVE_NEON)
    size_t i = 0;
    /* 16 lanes/iter: keeps both scalar pipes and the load/store unit busy */
    for (; i + 32 <= n; i += 32) {
        for (int k = 0; k < 32; k += 8) {
            float32x4_t a0 = vld1q_f32(a + i + k),     b0 = vld1q_f32(b + i + k);
            float32x4_t a1 = vld1q_f32(a + i + k + 4), b1 = vld1q_f32(b + i + k + 4);
            vst1q_f32(out + i + k,     vaddq_f32(a0, b0));
            vst1q_f32(out + i + k + 4, vaddq_f32(a1, b1));
        }
    }
    for (; i + 8 <= n; i += 8) {
        float32x4_t a0 = vld1q_f32(a + i),     b0 = vld1q_f32(b + i);
        float32x4_t a1 = vld1q_f32(a + i + 4), b1 = vld1q_f32(b + i + 4);
        vst1q_f32(out + i,     vaddq_f32(a0, b0));
        vst1q_f32(out + i + 4, vaddq_f32(a1, b1));
    }
    for (; i < n; i++)
        out[i] = a[i] + b[i];
#else
    for (size_t i = 0; i < n; i++)
        out[i] = a[i] + b[i];
#endif
}

void npl_simd_fmul(const float *a, const float *b, float *out, size_t n)
{
#if defined(NPL_HAVE_NEON)
    size_t i = 0;
    for (; i + 32 <= n; i += 32) {
        for (int k = 0; k < 32; k += 8) {
            float32x4_t a0 = vld1q_f32(a + i + k),     b0 = vld1q_f32(b + i + k);
            float32x4_t a1 = vld1q_f32(a + i + k + 4), b1 = vld1q_f32(b + i + k + 4);
            vst1q_f32(out + i + k,     vmulq_f32(a0, b0));
            vst1q_f32(out + i + k + 4, vmulq_f32(a1, b1));
        }
    }
    for (; i + 8 <= n; i += 8) {
        float32x4_t a0 = vld1q_f32(a + i),     b0 = vld1q_f32(b + i);
        float32x4_t a1 = vld1q_f32(a + i + 4), b1 = vld1q_f32(b + i + 4);
        vst1q_f32(out + i,     vmulq_f32(a0, b0));
        vst1q_f32(out + i + 4, vmulq_f32(a1, b1));
    }
    for (; i < n; i++)
        out[i] = a[i] * b[i];
#else
    for (size_t i = 0; i < n; i++)
        out[i] = a[i] * b[i];
#endif
}

float npl_simd_fdot(const float *a, const float *b, size_t n)
{
#if defined(NPL_HAVE_NEON)
    float32x4_t acc0 = vdupq_n_f32(0.0f);
    float32x4_t acc1 = vdupq_n_f32(0.0f);
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        acc0 = vfmaq_f32(acc0, vld1q_f32(a + i),     vld1q_f32(b + i));
        acc1 = vfmaq_f32(acc1, vld1q_f32(a + i + 4), vld1q_f32(b + i + 4));
    }
    float s = vaddvq_f32(acc0) + vaddvq_f32(acc1);
    for (; i < n; i++)
        s += a[i] * b[i];
    return s;
#else
    float s = 0.0f;
    for (size_t i = 0; i < n; i++)
        s += a[i] * b[i];
    return s;
#endif
}

void npl_simd_fadd_ip(float *a, const float *b, size_t n)
{
#if defined(NPL_HAVE_NEON)
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        float32x4_t va = vld1q_f32(a + i);
        float32x4_t vb = vld1q_f32(b + i);
        float32x4_t va1 = vld1q_f32(a + i + 4);
        float32x4_t vb1 = vld1q_f32(b + i + 4);
        vst1q_f32(a + i,     vaddq_f32(va, vb));
        vst1q_f32(a + i + 4, vaddq_f32(va1, vb1));
    }
    for (; i < n; i++)
        a[i] += b[i];
#else
    for (size_t i = 0; i < n; i++)
        a[i] += b[i];
#endif
}
