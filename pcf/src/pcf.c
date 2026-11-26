
/* pcf: percentage-closer filtering over a depth window. */
#include <stdio.h>
#include <string.h>
#include <math.h>

#define MAXK 9

typedef struct { float d[MAXK*MAXK]; int w, h; } DepthMap;

static int pcf(const DepthMap* m, int x, int y, float frag, float bias, int radius) {
    if (m->w <= 0 || m->h <= 0) return -1;
    if (radius < 0 || radius > 4) return -1;
    int lit = 0, total = 0;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int sx = x + dx, sy = y + dy;
            if (sx < 0) sx = 0;
            if (sy < 0) sy = 0;
            if (sx >= m->w) sx = m->w - 1;
            if (sy >= m->h) sy = m->h - 1;
            float md = m->d[sy * m->w + sx];
            if (frag - bias <= md) lit++;
            total++;
        }
    }
    return (lit * 255) / total;
}

/*
 * Penumbra width in shadow-map texels.
 *
 * Let a be the light-to-blocker distance, D the light-to-receiver distance,
 * and b = D - a the blocker-to-receiver gap. Similar triangles give
 *
 *     P_world = light_size * b / a
 *             = light_size * b / (D - b).
 *
 * The handbook approximation uses / D instead of / (D - k*b) with k = 1.
 * k is an empirical calibration coefficient; ideal geometric optics gives
 * k = 1. Small b / D makes D - k*b ~= D.
 */
static float penumbra_texels_cal(float blocker_depth, float receiver_depth,
                                 float light_size, float texel_world,
                                 float projection_scale, float k) {
    if (texel_world <= 0.0f || projection_scale <= 0.0f) return -1.0f;
    float gap = receiver_depth - blocker_depth;
    if (gap <= 0.0f) return 0.0f;
    float denominator = receiver_depth - k * gap;
    if (denominator <= 0.0f) return -1.0f;
    return light_size * gap / denominator * projection_scale / texel_world;
}

static float penumbra_texels(float blocker_depth, float receiver_depth,
                             float light_size, float texel_world, float projection_scale) {
    return penumbra_texels_cal(blocker_depth, receiver_depth, light_size,
                               texel_world, projection_scale, 1.0f);
}

/* Invert P = light_size * gap / (receiver_depth - k * gap). */
static float penumbra_fit_k(float gap, float receiver_depth,
                            float light_size, float measured_world) {
    if (gap <= 0.0f || light_size <= 0.0f || measured_world <= 0.0f) {
        return NAN;
    }
    return receiver_depth / gap - light_size / measured_world;
}

static int failures = 0;
static void check(const char* name, int ok) {
    printf("%s=%d\n", name, ok ? 1 : 0);
    if (!ok) failures++;
}

int main(void) {
    DepthMap m;
    m.w = 4; m.h = 4;
    for (int i = 0; i < 16; i++) m.d[i] = 0.5f;

    check("all_lit", pcf(&m, 2, 2, 0.4f, 0.0f, 1) == 255);
    check("all_shadow", pcf(&m, 2, 2, 0.6f, 0.0f, 1) == 0);

    /* half the map blocked -> partial coverage */
    DepthMap h;
    h.w = 4; h.h = 4;
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            h.d[y * 4 + x] = (x < 2) ? 0.3f : 0.7f;
    int v = pcf(&h, 2, 2, 0.5f, 0.0f, 1);
    check("partial", v > 30 && v < 220);

    /* bias turns a borderline pixel lit */
    check("bias_flips", pcf(&m, 2, 2, 0.51f, 0.0f, 0) == 0);
    check("bias_helps", pcf(&m, 2, 2, 0.51f, 0.05f, 0) == 255);

    check("penumbra_zero_at_contact", fabsf(penumbra_texels(5.0f, 5.0f, 1.0f, 0.1f, 1.0f)) < 1e-6f);
    check("penumbra_grows", penumbra_texels(1.0f, 10.0f, 1.0f, 0.1f, 1.0f) >
                            penumbra_texels(9.0f, 10.0f, 1.0f, 0.1f, 1.0f));

    /* Similar-triangle values for (blocker, receiver) = (1,2), (.1,10), (9,10). */
    check("penumbra_similar_triangles",
          fabsf(penumbra_texels(1.0f, 2.0f, 1.0f, 0.1f, 1.0f) - 10.0f) < 1e-4f &&
          fabsf(penumbra_texels(0.1f, 10.0f, 1.0f, 0.1f, 1.0f) - 990.0f) < 1e-2f &&
          fabsf(penumbra_texels(9.0f, 10.0f, 1.0f, 0.1f, 1.0f) - 1.111111f) < 1e-4f);

    /* When the supplied middle column is the gap b, blocker = D - b. */
    check("penumbra_gap_interpretation",
          fabsf(penumbra_texels(1.0f, 2.0f, 1.0f, 0.1f, 1.0f) - 10.0f) < 1e-4f &&
          fabsf(penumbra_texels(9.9f, 10.0f, 1.0f, 0.1f, 1.0f) - 0.1010101f) < 1e-5f &&
          fabsf(penumbra_texels(1.0f, 10.0f, 1.0f, 0.1f, 1.0f) - 90.0f) < 1e-3f);

    /* Ideal geometric measurements invert to k = 1 for all three gap cases. */
    check("penumbra_fit_ideal_k",
          fabsf(penumbra_fit_k(1.0f, 2.0f, 1.0f, 1.0f) - 1.0f) < 1e-6f &&
          fabsf(penumbra_fit_k(0.1f, 10.0f, 1.0f, 0.1f / 9.9f) - 1.0f) < 1e-5f &&
          fabsf(penumbra_fit_k(9.0f, 10.0f, 1.0f, 9.0f) - 1.0f) < 1e-6f);

    check("bad_radius", pcf(&m, 0, 0, 0.5f, 0.0f, 9) == -1);
    check("bad_map", pcf(&h, 0, 0, 0.5f, 0.0f, 1) > -1);

    printf("all_ok=%d\n", failures == 0);
    return failures == 0 ? 0 : 1;
}
