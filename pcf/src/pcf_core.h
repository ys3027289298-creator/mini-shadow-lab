
/* pcf_core.h: pure PCF / penumbra functions shared by the self-test and the
   calibration harness. Static-inline so each translation unit stays standalone. */
#ifndef PCF_CORE_H
#define PCF_CORE_H

#include <stdio.h>
#include <math.h>

#define MAXK 9

typedef struct { float d[MAXK*MAXK]; int w, h; } DepthMap;

/* percentage-closer filter: returns lit fraction scaled to [0,255] */
static inline int pcf(const DepthMap* m, int x, int y, float frag, float bias, int radius) {
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
 * Geometric penumbra width.
 *
 * Exact similar triangles for an area light of diameter s, blocker at distance
 * b from the light, receiver at D (b < D):
 *
 *     w = s * (D - b) / b  = s * d / (D - d),        d = D - b
 *
 * With the light far from the blocker relative to the gap, d << D, this
 * linearizes to the ratio form used by the lab:
 *
 *     w ~= s * d / D.
 *
 * The return value is the width in shadow-map texels, after the light-frustum
 * projection scale and the texel world size are applied.
 */
static inline float penumbra_texels(float blocker_depth, float receiver_depth,
                                    float light_size, float texel_world,
                                    float projection_scale) {
    if (texel_world <= 0.0f || projection_scale <= 0.0f) return -1.0f;
    float d = receiver_depth - blocker_depth;
    if (d < 0.0f) d = 0.0f;
    return (light_size * d / receiver_depth) * projection_scale / texel_world;
}

/*
 * Adaptive kernel radius (continuous, texels) from the penumbra width.
 *
 * A box PCF kernel of radius r turns a hard step edge into a transition band of
 * full width 2r texels between the fully-shadowed and fully-lit plateaus, so
 * aligning the kernel with the geometric penumbra gives
 *
 *     r = k * w,  with geometrically k = 1/2,
 *
 * i.e. the radius is a function of the distance ratio d/D alone:
 *
 *     r(d/D) = k * (s * projection_scale / texel_world) * (d/D).
 *
 * k is fit from measured transition bands (see src/calibrate.c) instead of
 * being asserted; a fixed kernel drops r(d/D) and keeps r constant, which
 * under-samples the growing penumbra as a light approaches the receiver.
 */
static inline float kernel_radius_texels(float penumbra_w_texels, float k) {
    if (penumbra_w_texels < 0.0f || k <= 0.0f) return -1.0f;
    return k * penumbra_w_texels;
}

/* integer texel radius actually issued to pcf(): quantized, then clamped */
static inline int quantize_radius(float r_float, int r_min, int r_max) {
    if (r_float < 0.0f || r_min < 0 || r_max < r_min) return -1;
    int r = (int)lroundf(r_float);
    if (r < r_min) r = r_min;
    if (r > r_max) r = r_max;
    return r;
}

#endif /* PCF_CORE_H */
