/*
 * bias_lab: deterministic slope/texel bias experiment for stone, metal and cloth.
 *
 * The lab is a geometric nearest-texel model:
 *   - h is the world-space size of one light-space texel;
 *   - gx,gy are dz_light/dx_light slopes on a local surface patch;
 *   - a shaded sample lies in the texel square, dx,dy in [-h/2,h/2];
 *   - EPS is the worst-case linear-depth quantization floor.
 *
 * Equal stratification over slope, orientation and sub-texel position is used
 * as a proxy for material pixel area.  Replace the stratification with mesh
 * fragments for a production asset audit.
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <math.h>
#include <time.h>

#define ACNE_LIMIT_PCT   0.25
#define DETACH_LIMIT_M   0.050
#define TEXEL_M          0.005f
#define DEPTH_EPS_M      0.001f
#define SAFETY_K         1.0f

#define STONE_G_LO       0.70f
#define STONE_G_HI       1.70f

typedef struct {
    const char* name;
    float g_lo;
    float g_hi;
} Material;

typedef enum {
    BIAS_GLOBAL_STONE = 0,
    BIAS_GLOBAL_MIN_SLOPE,
    BIAS_GLOBAL_MAX_SLOPE,
    BIAS_PER_PIXEL
} BiasMethod;

#define GLOBAL_G_MIN 0.06f
#define GLOBAL_G_MAX 6.00f

typedef struct {
    long total;
    long acne;
    double acne_pct;
    double endpoint_lo_pct;
    double endpoint_hi_pct;
} AcneMetric;

static float weak_global_bound(float g) {
    /* Smallest possible nearest-texel bound: EPS + half a worst texel corner. */
    return DEPTH_EPS_M + 0.5f * TEXEL_M * sqrtf(2.0f) * g;
}

static float stone_global_bias(void) {
    /* Minimum bound from the stone's steepest corner, plus a small margin. */
    return weak_global_bound(STONE_G_HI) + 0.0001f;
}

static float global_bias(BiasMethod method) {
    if (method == BIAS_GLOBAL_STONE) return stone_global_bias();
    if (method == BIAS_GLOBAL_MIN_SLOPE) return weak_global_bound(GLOBAL_G_MIN);
    if (method == BIAS_GLOBAL_MAX_SLOPE) return weak_global_bound(GLOBAL_G_MAX);
    return 0.0f;
}

/* Conservative one-texel slope-scale bias.  L1 is the exact square-corner bound. */
static float per_pixel_bias(float gx, float gy) {
    return DEPTH_EPS_M + SAFETY_K * TEXEL_M * (fabsf(gx) + fabsf(gy));
}

static float global_or_pixel_bias(BiasMethod method, float g, float phi) {
    if (method == BIAS_PER_PIXEL) {
        float gx = g * cosf(phi);
        float gy = g * sinf(phi);
        return per_pixel_bias(gx, gy);
    }
    return global_bias(method);
}

static double acne_fraction_at_slope(float g, BiasMethod method) {
    enum { ORIENT_BINS = 64, SUB_BINS = 48 };
    const float pi = 3.14159265358979323846f;
    long total = 0, bad = 0;

    for (int oi = 0; oi < ORIENT_BINS; ++oi) {
        float phi = (pi * 0.5f) * ((float)oi + 0.5f) / (float)ORIENT_BINS;
        float gx = g * cosf(phi);
        float gy = g * sinf(phi);
        float bias = global_or_pixel_bias(method, g, phi);

        for (int iy = 0; iy < SUB_BINS; ++iy) {
            float dy = (((float)iy + 0.5f) / SUB_BINS - 0.5f) * TEXEL_M;
            for (int ix = 0; ix < SUB_BINS; ++ix) {
                float dx = (((float)ix + 0.5f) / SUB_BINS - 0.5f) * TEXEL_M;
                float depth_delta = gx * dx + gy * dy;
                int black = (depth_delta + DEPTH_EPS_M > bias);
                ++total;
                bad += black;
            }
        }
    }
    return 100.0 * (double)bad / (double)total;
}

static AcneMetric measure_acne(const Material* mat, BiasMethod method) {
    enum { SLOPE_BINS = 64, ORIENT_BINS = 32, SUB_BINS = 32 };
    AcneMetric m = {0, 0, 0.0, 0.0, 0.0};
    const float pi = 3.14159265358979323846f;

    for (int si = 0; si < SLOPE_BINS; ++si) {
        float g = mat->g_lo + (mat->g_hi - mat->g_lo) *
                  ((float)si + 0.5f) / (float)SLOPE_BINS;

        for (int pi_i = 0; pi_i < ORIENT_BINS; ++pi_i) {
            /* Symmetry makes the first quadrant sufficient. */
            float phi = (pi * 0.5f) * ((float)pi_i + 0.5f) / (float)ORIENT_BINS;
            float gx = g * cosf(phi);
            float gy = g * sinf(phi);
            float bias = global_or_pixel_bias(method, g, phi);

            for (int iy = 0; iy < SUB_BINS; ++iy) {
                float dy = (((float)iy + 0.5f) / SUB_BINS - 0.5f) * TEXEL_M;
                for (int ix = 0; ix < SUB_BINS; ++ix) {
                    float dx = (((float)ix + 0.5f) / SUB_BINS - 0.5f) * TEXEL_M;
                    float depth_delta = gx * dx + gy * dy;

                    /* Stored map depth can be low by DEPTH_EPS_M. */
                    int black = (depth_delta + DEPTH_EPS_M > bias);
                    ++m.total;
                    m.acne += black;
                }
            }
        }
    }

    m.acne_pct = 100.0 * (double)m.acne / (double)m.total;
    m.endpoint_lo_pct = acne_fraction_at_slope(mat->g_lo, method);
    m.endpoint_hi_pct = acne_fraction_at_slope(mat->g_hi, method);
    return m;
}

/* World-surface distance from the true contact boundary to the bias boundary. */
static float detach_distance(float g, float phi, BiasMethod method) {
    float bias = global_or_pixel_bias(method, g, phi);

    /* Along the gradient direction, receiver depth is z = g*t.
     * Lit when g*t - bias <= map_depth(=0); the first shadowed point solves
     * g*t = bias.  Bisect the actual comparison instead of trusting algebra. */
    float lo = 0.0f;
    float hi = fmaxf(1.0f, 4.0f * bias / g);
    for (int i = 0; i < 60; ++i) {
        float mid = 0.5f * (lo + hi);
        int shadowed = (g * mid - bias > 0.0f);
        if (shadowed) hi = mid;
        else lo = mid;
    }

    float t = 0.5f * (lo + hi);
    float z = g * t;
    return sqrtf(t * t + z * z);
}

static void endpoint_detach(const Material* mat, BiasMethod method,
                            float* d_lo, float* d_hi) {
    const int ORIENT = 129;
    const float pi = 3.14159265358979323846f;
    *d_lo = 0.0f;
    *d_hi = 0.0f;
    for (int i = 0; i < ORIENT; ++i) {
        float phi = 0.25f * pi * (float)i / (ORIENT - 1); /* [0, pi/4], worst is corner */
        *d_lo = fmaxf(*d_lo, detach_distance(mat->g_lo, phi, method));
        *d_hi = fmaxf(*d_hi, detach_distance(mat->g_hi, phi, method));
    }
}

static float maximum_detach(const Material* mat, BiasMethod method) {
    enum { SLOPE_BINS = 128, ORIENT = 65 };
    const float pi = 3.14159265358979323846f;
    float dmax = 0.0f;

    for (int si = 0; si <= SLOPE_BINS; ++si) {
        float g = mat->g_lo + (mat->g_hi - mat->g_lo) *
                  (float)si / (float)SLOPE_BINS;
        for (int oi = 0; oi < ORIENT; ++oi) {
            float phi = 0.25f * pi * (float)oi / (ORIENT - 1);
            dmax = fmaxf(dmax, detach_distance(g, phi, method));
        }
    }
    return dmax;
}

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

static float sink;

int main(void) {
    Material mats[3] = {
        { "stone", STONE_G_LO, STONE_G_HI },
        { "metal_pipe", 1.20f, 6.00f },
        { "cloth", 0.06f, 2.40f }
    };

    float b_stone = stone_global_bias();
    float sigma_all_max = sqrtf(2.0f) * 6.00f;
    float lower_even_half_texel = DEPTH_EPS_M + 0.5f * TEXEL_M * sigma_all_max;
    float lower_conservative = DEPTH_EPS_M + TEXEL_M * sigma_all_max;
    float upper_detach = DETACH_LIMIT_M * 0.06f / sqrtf(1.0f + 0.06f * 0.06f);
    float b_at_global_min = DEPTH_EPS_M + 0.5f * TEXEL_M * sqrtf(2.0f) * 0.06f;
    float d_at_global_max = lower_even_half_texel * sqrtf(1.0f + 0.06f * 0.06f) / 0.06f;
    float d_practical_max = lower_conservative * sqrtf(1.0f + 0.06f * 0.06f) / 0.06f;

    printf("# geometry\n");
    printf("texel_m=%.6f depth_eps_m=%.6f stone_global_bias_m=%.6f\n",
           TEXEL_M, DEPTH_EPS_M, b_stone);
    printf("global_g_min=%.3f global_g_max=%.3f sigma_l1_max=%.5f\n",
           0.06f, 6.0f, sigma_all_max);
    printf("feasible_lower_weak_m=%.6f feasible_upper_detach_m=%.6f empty=%d ratio=%.2f\n",
           lower_even_half_texel, upper_detach,
           lower_even_half_texel > upper_detach,
           lower_even_half_texel / upper_detach);
    printf("practical_lower_m=%.6f min_slope_bias_m=%.6f\n",
           lower_conservative, b_at_global_min);
    printf("max_slope_global_cloth_detach_m=%.4f practical_cloth_detach_m=%.4f\n",
           d_at_global_max, d_practical_max);

    printf("\n# metrics\n");
    printf("material,method,g_lo,g_hi,samples,acne_pct,acne_lo_pct,acne_hi_pct,");
    printf("acne_limit_pct,detach_lo_m,detach_hi_m,detach_max_m,detach_limit_m,status\n");

    int all_adaptive_ok = 1;
    const char* method_names[4] = {
        "global_stone", "global_min_slope", "global_max_slope", "per_pixel"
    };
    for (int mi = 0; mi < 3; ++mi) {
        for (int method_i = 0; method_i < 4; ++method_i) {
            BiasMethod method = (BiasMethod)method_i;
            AcneMetric a = measure_acne(&mats[mi], method);
            float dlo, dhi;
            endpoint_detach(&mats[mi], method, &dlo, &dhi);
            float dmax = maximum_detach(&mats[mi], method);
            int acne_ok = a.acne_pct <= ACNE_LIMIT_PCT;
            int detach_ok = dmax <= DETACH_LIMIT_M;
            int ok = acne_ok && detach_ok;
            if (method == BIAS_PER_PIXEL) all_adaptive_ok &= ok;

            printf("%s,%s,%.3f,%.3f,%ld,%.5f,%.5f,%.5f,%.2f,%.5f,%.5f,%.5f,%.3f,%s\n",
                   mats[mi].name, method_names[method_i],
                   mats[mi].g_lo, mats[mi].g_hi, a.total,
                   a.acne_pct, a.endpoint_lo_pct, a.endpoint_hi_pct,
                   ACNE_LIMIT_PCT, dlo, dhi, dmax, DETACH_LIMIT_M,
                   ok ? "PASS" : "FAIL");
        }
    }

    /* Small CPU-side arithmetic proxy.  GPU derivatives are quad instructions,
     * so treat ns/pixel as a host sanity check, not a GPU benchmark. */
    enum { BENCH_N = 20000000 };
    double t0 = now_seconds();
    float acc = 0.0f;
    for (int i = 0; i < BENCH_N; ++i) {
        float ndotl = 0.05f + 0.949f * ((float)(i & 1023) / 1023.0f);
        float tan_theta = sqrtf(fmaxf(0.0f, 1.0f - ndotl * ndotl)) /
                          fmaxf(ndotl, 1e-4f);
        float b = DEPTH_EPS_M + TEXEL_M * tan_theta;
        acc += b * (float)(i & 7);
    }
    double t1 = now_seconds();
    sink = acc;
    double per_pixel_ns = 1e9 * (t1 - t0) / BENCH_N;

    t0 = now_seconds();
    acc = 0.0f;
    for (int i = 0; i < BENCH_N; ++i) {
        float b = b_stone;
        acc += b * (float)(i & 7);
    }
    t1 = now_seconds();
    sink += acc;
    double global_ns = 1e9 * (t1 - t0) / BENCH_N;

    printf("\n# cost_proxy\n");
    printf("global_extra_slope_evals_per_pixel=0 global_loop_ns_per_op=%.4f\n", global_ns);
    printf("per_pixel_extra_slope_evals_per_pixel=1 per_pixel_loop_ns_per_op=%.4f\n",
           per_pixel_ns);
    printf("sink=%.3f adaptive_all_pass=%d\n", (double)sink, all_adaptive_ok);

    return all_adaptive_ok ? 0 : 1;
}
