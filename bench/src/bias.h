/*
 * bias.h - slope-scaled depth bias, three strategies.
 *
 * The shadow test is   occluded := frag_depth - bias > map_depth.
 * Only the slope factor inside bias differs between pipelines; everything
 * else (scene, map, PCF, quantization) is shared byte-for-byte.
 *
 *   STRAT_REF_D : double intermediates, libm tan(acos(x))      (slow, fully
 *                 controllable; reference-grade precision)
 *   STRAT_NAT_F : the algebraic sqrt form used in shadowmap.c, float
 *   STRAT_FAST_K: float bit-hack rsqrt + k Newton-Raphson steps (no libm,
 *                 no sqrt instruction); k is the controllability knob
 *
 * All three return the slope factor s(x) = tan(arccos x) = sqrt(1-x^2)/x,
 * the same math at three points on the precision/cost curve.
 */
#ifndef SHADOW_BENCH_BIAS_H
#define SHADOW_BENCH_BIAS_H

#include <stdint.h>
#include "common.h"

enum { STRAT_REF_D = 0, STRAT_NAT_F = 1, STRAT_FAST_K = 2 };

typedef struct {
    int kind;
    int nr_iters;   /* STRAT_FAST_K only: Newton steps after the bit hack */
} BiasStrategy;

/* Quake-style initial rsqrt guess: integer magic, no sqrt instruction. */
static inline float rsqrt_seed(float x) {
    union { float f; int32_t i; } u;
    u.f = x;
    u.i = 0x5f3759df - (u.i >> 1);
    return u.f;
}

static inline float rsqrt_nr(float x, int iters) {
    float y = rsqrt_seed(x);
    for (int k = 0; k < iters; k++)
        y = y * (1.5f - 0.5f * x * y * y);
    return y;
}

/*
 * slope factor s(x), x = clamp(n.l, 0, 1).
 * Returns 0 for x at the facing pole and a large finite value at grazing.
 */
static inline float slope_factor(float ndotl, const BiasStrategy* s) {
    float x = ndotl;
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    if (x > 1.0f - 1e-7f) return 0.0f;      /* facing pole: sqrt term 0 */

    switch (s->kind) {
    case STRAT_REF_D: {
        double xd = (double)x;
        double sd = tan(acos(xd));           /* double-precision reference */
        return (float)sd;
    }
    case STRAT_NAT_F: {
        /* same expression as depth_bias() in shadowmap/src/shadowmap.c */
        float xc = x > 1e-4f ? x : 1e-4f;
        return sqrtf(1.0f - x * x) / xc;
    }
    case STRAT_FAST_K:
    default: {
        /* constraint: u = 1-x^2 must stay strictly positive; handled by the
         * facing-pole guard above and by the grazing clamp at the caller */
        float u = 1.0f - x * x;
        float xc = x > 1e-4f ? x : 1e-4f;
        return u * rsqrt_nr(u, s->nr_iters) / xc;
    }
    }
}

static inline float compute_bias(float ndotl, const Config* cfg,
                                 const BiasStrategy* s) {
    return cfg->base_bias + cfg->slope_scale * slope_factor(ndotl, s);
}

#endif
