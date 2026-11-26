/* transmit: transmittance shadow map for transparent occluders. */
#include "transmit.h"
#include <math.h>

float xmt_transmittance(float sigma, float thickness) {
    if (!(sigma >= 0.0f) || !(thickness >= 0.0f)) return 0.0f;
    float od = sigma * thickness;
    if (od > XMT_MAX_OD) od = XMT_MAX_OD;
    return expf(-od);
}

static int fit_common(int n) { return n > 0 && n <= XMT_MAX_LAYERS; }

int xmt_fit_sigma(const float *thickness, const float *measured, int n,
                  float *out_sigma) {
    if (!thickness || !measured || !out_sigma) return -1;
    if (!fit_common(n)) return -1;
    float num = 0.0f, den = 0.0f;
    for (int i = 0; i < n; i++) {
        float d = thickness[i], t = measured[i];
        if (!(d > 0.0f)) return -1;
        if (!(t > 0.0f) || t > 1.0f) return -1;
        num -= d * logf(t);
        den += d * d;
    }
    if (!(den > 0.0f)) return -1;
    *out_sigma = num / den;
    return 0;
}

int xmt_fit_sigma_one(float thickness, float measured, float *out_sigma) {
    return xmt_fit_sigma(&thickness, &measured, 1, out_sigma);
}

int xmt_slab_init(xmt_slab *s, float sigma, const float *thickness, int n) {
    if (!s || !thickness) return -1;
    if (n <= 0 || n > XMT_MAX_LAYERS) return -1;
    if (!(sigma >= 0.0f)) return -1;
    float total = 0.0f, thick = 0.0f;
    for (int i = 0; i < n; i++) {
        if (!(thickness[i] >= 0.0f)) return -1;
        float od = sigma * thickness[i];
        if (od > XMT_MAX_OD) od = XMT_MAX_OD;
        s->od[i] = od;
        thick += thickness[i];
        s->edge[i] = thick;
        total += od;
    }
    s->od_total = total;
    s->thickness_total = thick;
    s->n = n;
    return 0;
}

float xmt_slab_transmittance(const xmt_slab *s, int layers_used) {
    if (!s || s->n <= 0) return 0.0f;
    if (layers_used <= 0 || layers_used > s->n) layers_used = s->n;
    float od = 0.0f;
    for (int i = 0; i < layers_used; i++) od += s->od[i];
    if (od > XMT_MAX_OD) od = XMT_MAX_OD;
    return expf(-od);
}

/* Store cumulative transmittance, not the nearest depth:
 *   level i <-> prod_{j<=i} exp(-od_j) = exp(-sum_{j<=i} od_j)
 * The depth map would store min(z) and answer with a threshold test; this
 * stores how much light got through and answers with a multiply. */
int xmt_store(xmt_map *m, const xmt_slab *s, int layers) {
    if (!m || !s) return -1;
    if (layers <= 0 || layers > s->n) return -1;
    float od = 0.0f;
    for (int i = 0; i < layers; i++) {
        od += s->od[i];
        float od_c = od > XMT_MAX_OD ? XMT_MAX_OD : od;
        float t = expf(-od_c);
        int q = (int)(t * XMT_QUANT + 0.5f);
        if (q < 0) q = 0;
        if (q > (int)XMT_QUANT) q = (int)XMT_QUANT;
        m->t[i] = (unsigned short)q;
        m->edge[i] = s->edge[i];
    }
    m->levels = layers;
    return 0;
}

float xmt_lookup(const xmt_map *m, int level) {
    if (!m || level < 0 || level >= m->levels) return -1.0f;
    return (float)m->t[level] / XMT_QUANT;
}

float xmt_lit_fraction(const xmt_map *m, float path) {
    if (!m) return -1.0f;
    if (m->levels <= 0) return 1.0f;          /* nothing stored: unoccluded */
    if (!(path > 0.0f)) return 1.0f;          /* receiver in front of glass */
    int level = m->levels - 1;
    for (int i = 0; i < m->levels; i++) {
        if (path <= m->edge[i] + 1e-6f) { level = i; break; }
    }
    return xmt_lookup(m, level);
}

float xmt_depth_style_lit(float nearest_depth, float frag_depth, float bias) {
    return frag_depth - bias > nearest_depth ? 0.0f : 1.0f;
}
