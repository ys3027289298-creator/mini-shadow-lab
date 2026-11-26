/*
 * pipeline.c - the full per-frame shadow pipeline, shared by both variants.
 *
 * Stages (timed independently in bench.c):
 *   S1 mapbuild : ray-march the shadow map for the directional light
 *   S2 bias     : per-fragment slope-scaled bias  <- ONLY stage that differs
 *   S3 pcf      : clamped-window percentage-closer filter (same kernel as
 *                 pcf/src/pcf.c), producing a 0..255 coverage
 *   S4 shade    : quantize coverage + light dot product into an 8-bit pixel
 *
 * Intermediate buffers are exposed so the funnel experiment can probe where
 * the two variants diverge before later stages swallow the difference.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "common.h"
#include "bias.h"
#include "pipeline.h"

static float f32_to_stored(float d, int mode) {
    switch (mode) {
    case MAP_F16:
        return (float)(_Float16)d;
    case MAP_U16: {
        uint16_t q = (uint16_t)lroundf(d * 65535.0f);
        return (float)q / 65535.0f;
    }
    default:
        return d;
    }
}

/* ---- S1: shadow map ---------------------------------------------------- */
static void build_map(Pipeline* p) {
    const Config* c = p->cfg;
    const LightFrame* lf = &p->lf;
    const float* az = lf->az;
    for (int ty = 0; ty < SMAP_H; ty++) {
        for (int tx = 0; tx < SMAP_W; tx++) {
            float u = ((float)tx + 0.5f) / SMAP_W;
            float v = ((float)ty + 0.5f) / SMAP_H;
            /* texel center in world space, on the center-depth plane */
            float sx = (u - 0.5f) * 2.0f * lf->extent;
            float sy = (v - 0.5f) * 2.0f * lf->extent;
            float o[3];
            for (int i = 0; i < 3; i++)
                o[i] = c->center[i] + lf->ax[i] * sx + lf->ay[i] * sy
                       + az[i] * ((lf->t1 + lf->t0) * 0.5f - v3_dot(c->center, az));
            float best = INFINITY;
            for (int b = 0; b < c->nboxes; b++) {
                float t = ray_box(o, az, &c->boxes[b]);
                if (t < best) best = t;
            }
            float d;
            if (isinf(best)) {
                d = 1.0f;
            } else {
                float hp[3];
                for (int i = 0; i < 3; i++) hp[i] = o[i] + az[i] * best;
                d = light_depth(lf, hp);
                if (d < 0.0f) d = 0.0f;
                if (d > 1.0f) d = 1.0f;
            }
            p->map_d[ty * SMAP_W + tx] = f32_to_stored(d, c->map_mode);
        }
    }
}

/* ---- S2: per-fragment bias -------------------------------------------- */
static void fragment_bias(Pipeline* p, const BiasStrategy* s) {
    const Config* c = p->cfg;
    for (int i = 0; i < NFRAG; i++) {
        int px = i % IMG_W, py = i / IMG_W;
        float w[3], n[3];
        fragment_pos(px, py, w);
        ground_normal(px, py, n);
        float nl = v3_dot(n, c->L);
        p->ndotl[i] = nl;
        float fd = light_depth(&p->lf, w);
        if (fd < 0.0f) fd = 0.0f;
        if (fd > 1.0f) fd = 1.0f;   /* outside the far window: clamp, do not
                                       let frag exceed the background sentinel */
        p->frag_d[i] = fd;
        p->bias[i] = compute_bias(nl, c, s);
    }
}

/* ---- S3: PCF (clamped window, same semantics as pcf/src/pcf.c) -------- */
static void pcf_filter(Pipeline* p) {
    const Config* c = p->cfg;
    int r = c->pcf_radius;
    for (int i = 0; i < NFRAG; i++) {
        int px = i % IMG_W, py = i / IMG_W;
        float w[3];
        fragment_pos(px, py, w);
        float u, v;
        light_uv(&p->lf, w, &u, &v);
        int cx = (int)(u * SMAP_W);
        int cy = (int)(v * SMAP_H);
        float frag = p->frag_d[i], bias = p->bias[i];

        int lit = 0, total = 0;
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                int sx = cx + dx, sy = cy + dy;
                if (sx < 0) sx = 0;
                if (sy < 0) sy = 0;
                if (sx >= SMAP_W) sx = SMAP_W - 1;
                if (sy >= SMAP_H) sy = SMAP_H - 1;
                float md = p->map_d[sy * SMAP_W + sx];
                if (frag - bias <= md) lit++;
                total++;
            }
        }
        p->coverage[i] = (uint8_t)((lit * 255) / total);
        if (r == 0) {
            /* also record the single-tap flags for the funnel experiment */
            float md = p->map_d[cy * SMAP_W + cx];
            p->lit_raw[i]    = (frag > md) ? 1 : 0;
            p->lit_biased[i] = (frag - bias > md) ? 1 : 0;
        }
    }
}

/* ---- S4: shade into 8-bit --------------------------------------------- */
static void shade(Pipeline* p) {
    for (int i = 0; i < NFRAG; i++) {
        float nl = p->ndotl[i];
        if (nl < 0.0f) nl = 0.0f;
        float lit_frac = p->coverage[i] / 255.0f;
        float ambient = 0.15f;
        float val = ambient + (1.0f - ambient) * nl * lit_frac;
        if (val > 1.0f) val = 1.0f;
        p->pixel[i] = (uint8_t)lroundf(val * 255.0f);
    }
}

/* Fresh pipeline with its own buffers, so two runs can be compared directly. */
Pipeline* pipeline_run(const Config* cfg, const BiasStrategy* s) {
    Pipeline* p = calloc(1, sizeof(Pipeline));
    if (!p) { perror("calloc"); exit(1); }
    p->cfg = cfg;
    light_frame_build(&p->lf, cfg);
    build_map(p);
    fragment_bias(p, s);
    pcf_filter(p);
    shade(p);
    return p;
}

/* stage entry points reused by the timing driver */
void stage_mapbuild(Pipeline* p) { build_map(p); }
void stage_bias(Pipeline* p, const BiasStrategy* s) { fragment_bias(p, s); }
void stage_pcf(Pipeline* p) { pcf_filter(p); }
void stage_shade(Pipeline* p) { shade(p); }

/* Reused single pipeline for the timing loop (no second copy needed there). */
Pipeline* pipeline_alloc(const Config* cfg) {
    static Pipeline* g_pipe = NULL;
    if (!g_pipe) g_pipe = calloc(1, sizeof(Pipeline));
    memset(g_pipe, 0, sizeof(Pipeline));
    g_pipe->cfg = cfg;
    light_frame_build(&g_pipe->lf, cfg);
    return g_pipe;
}

void write_pgm(const char* path, const uint8_t* px) {
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P5\n%d %d\n255\n", IMG_W, IMG_H);
    fwrite(px, 1, NFRAG, f);
    fclose(f);
}
