/* transmit: transmittance shadow map for transparent occluders. */
#ifndef TRANSMIT_H
#define TRANSMIT_H

#define XMT_MAX_LAYERS 8
#define XMT_QUANT      4096.0f  /* transmittance is stored in 1/4096 units */
#define XMT_MAX_OD     8.0f     /* exp(-8) = 3.4e-4, opaque enough to clamp */
#define XMT_DEFAULT_SIGMA 21.9f /* m^-1, recovered from the calibration fixture */

/* A slab stack: an occluder split into slices along the light path.
 * od[i] is sigma * thickness_i, so optical depth is additive.
 * edge[i] is the cumulative thickness at the far side of slice i. */
typedef struct {
    float od[XMT_MAX_LAYERS];
    float edge[XMT_MAX_LAYERS];
    float od_total;
    float thickness_total;
    int n;
} xmt_slab;

/* Stored map: what survives through the first (i+1) slices, per level.
 * edge[i] repeats the slab boundary, so a lookup can pick the level by the
 * receiver's path length. levels == 0 means "no occluder" (T = 1). */
typedef struct {
    unsigned short t[XMT_MAX_LAYERS];
    float edge[XMT_MAX_LAYERS];
    int levels;
} xmt_map;

/* Beer-Lambert closed form, clamped to the representable optical depth:
 *   T = exp(-sigma * thickness) */
float xmt_transmittance(float sigma, float thickness);

/* Solve sigma in T_i = exp(-sigma * thickness_i) from measured transmittances.
 * Least squares in the log domain through the origin:
 *   sigma = -sum(d_i ln T_i) / sum(d_i^2)
 * Returns 0, or -1 for bad input (n out of range, T outside (0,1], d <= 0). */
int xmt_fit_sigma(const float *thickness, const float *measured, int n,
                  float *out_sigma);

/* Single-sample inversion sigma_i = -ln(T_i) / d_i. Kept separate because
 * measurement noise makes one sample less stable than the joint fit. */
int xmt_fit_sigma_one(float thickness, float measured, float *out_sigma);

/* Split an occluder of the given thicknesses into a slab stack. */
int xmt_slab_init(xmt_slab *s, float sigma, const float *thickness, int n);

/* Exact transmittance through the first layers_used slices.
 * layers_used <= 0 means the whole stack. */
float xmt_slab_transmittance(const xmt_slab *s, int layers_used);

/* Transmittance shadow map: accumulate optical depth over the first `layers`
 * slices and store exp(-od), quantized. Stores how much light gets through,
 * not the nearest depth. Returns 0, or -1 if layers exceeds the stack. */
int xmt_store(xmt_map *m, const xmt_slab *s, int layers);

/* Decode level `level` of the map; returns -1 if the level is not stored. */
float xmt_lookup(const xmt_map *m, int level);

/* Fraction of light reaching a fragment whose light-path through the
 * occluder is `path` long. Picks the last stored slice boundary at or before
 * the receiver, so a receiver in front of the glass reads 1.0 and one behind
 * reads the full accumulated transmittance. */
float xmt_lit_fraction(const xmt_map *m, float path);

/* What a depth-style map answers for the same glass: 1 if lit, 0 if
 * occluded. Used only to show the difference between storing min-depth and
 * storing transmittance. */
float xmt_depth_style_lit(float nearest_depth, float frag_depth, float bias);

#endif
