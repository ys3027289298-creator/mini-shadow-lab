/*
 * common.h - shared scene, light-space math and config for the two-pipeline
 * comparison. Both pipelines include this; the only deliberate difference
 * between them lives in bias.h (slope-factor strategy).
 *
 * Scene: a ground receiver plane z=0 and a handful of AABB occluders, lit by
 * a directional light. The shadow map is an orthographic depth image along
 * the light axis. Image pixels are ground-plane fragments.
 */
#ifndef SHADOW_BENCH_COMMON_H
#define SHADOW_BENCH_COMMON_H

#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdint.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define IMG_W 960
#define IMG_H 640
#define NFRAG (IMG_W * IMG_H)

#define SMAP_W 512
#define SMAP_H 512
#define SMAP_N (SMAP_W * SMAP_H)

#define MAX_BOXES 8
#define MAXC 4

typedef struct { float lo[3], hi[3]; } Box;

typedef struct {
    /* light direction: unit vector pointing from surface toward the light */
    float L[3];
    float center[3];   /* ortho frustum center in world space */
    float extent;      /* half width/height of the ortho frustum, world */
    float t_near, t_far; /* depth window along light axis, world distance */
    Box boxes[MAX_BOXES];
    int nboxes;
    /* bias controls (light-depth units) */
    float base_bias;
    float slope_scale;
    int pcf_radius;
    int map_mode;      /* 0 = f32, 1 = f16, 2 = u16 normalized */
} Config;

enum { MAP_F32 = 0, MAP_F16 = 1, MAP_U16 = 2 };

/* light-space axes, derived once per config */
typedef struct {
    float ax[3], ay[3], az[3]; /* unit axes; az points along the view ray */
    float t0, t1;              /* depth window along az */
    float extent;
} LightFrame;

static inline void v3_sub(float* o, const float* a, const float* b) {
    for (int i = 0; i < 3; i++) o[i] = a[i] - b[i];
}
static inline float v3_dot(const float* a, const float* b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
static inline void v3_cross(float* o, const float* a, const float* b) {
    o[0] = a[1]*b[2] - a[2]*b[1];
    o[1] = a[2]*b[0] - a[0]*b[2];
    o[2] = a[0]*b[1] - a[1]*b[0];
}
static inline float v3_norm(const float* a) { return sqrtf(v3_dot(a, a)); }
static inline void v3_normalize(float* o, const float* a) {
    float n = v3_norm(a);
    for (int i = 0; i < 3; i++) o[i] = a[i] / n;
}

/* Default scene. elev_deg sets light elevation; lower = more grazing. */
static inline void config_default(Config* c, float elev_deg) {
    memset(c, 0, sizeof(*c));
    float el = elev_deg * (float)M_PI / 180.0f;
    float az = 35.0f * (float)M_PI / 180.0f;
    c->L[0] = cosf(el) * cosf(az);
    c->L[1] = cosf(el) * sinf(az);
    c->L[2] = sinf(el);
    c->center[0] = 0; c->center[1] = 0; c->center[2] = 2.0f;
    c->extent = 24.0f;
    c->t_near = 1.0f;
    c->t_far = 60.0f;

    /* fixed occluders: boxes of varying height sitting on the ground */
    static const float b[MAX_BOXES][6] = {
        {-6, -4, 0, -3, -1, 4.0f},
        { 1, -5, 0,  4, -2, 2.5f},
        { 5,  3, 0,  8,  6, 5.5f},
        {-2,  2, 0,  1,  5, 1.6f},
        {-9,  5, 0, -6,  8, 3.2f},
        { 2,  7, 0,  4,  9, 0.9f},
    };
    c->nboxes = 6;
    for (int i = 0; i < c->nboxes; i++)
        for (int k = 0; k < 3; k++) {
            c->boxes[i].lo[k] = b[i][k];
            c->boxes[i].hi[k] = b[i][k + 3];
        }

    c->base_bias   = 0.00020f;  /* normalized light-depth units */
    c->slope_scale = 0.00050f;
    c->pcf_radius  = 2;
    c->map_mode    = MAP_F32;
}

static inline void light_frame_build(LightFrame* lf, const Config* c) {
    /* view axis points opposite the light */
    for (int i = 0; i < 3; i++) lf->az[i] = -c->L[i];
    float up[3] = {0, 0, 1};
    float ax[3];
    v3_cross(ax, up, lf->az);
    v3_normalize(lf->ax, ax);
    v3_cross(lf->ay, lf->az, lf->ax);
    /* t = dot(p, az); window centered on c->center */
    float tc = v3_dot(c->center, lf->az);
    lf->t0 = tc - c->t_far;
    lf->t1 = tc + c->t_near;
    lf->extent = c->extent;
}

static inline float light_depth(const LightFrame* lf, const float* p) {
    float t = v3_dot(p, lf->az);
    return (t - lf->t0) / (lf->t1 - lf->t0);
}

static inline void light_uv(const LightFrame* lf, const float* p,
                            float* u, float* v) {
    float d[3];
    v3_sub(d, p, (const float[]){0,0,0});
    float xu = v3_dot(p, lf->ax) / lf->extent * 0.5f + 0.5f;
    float yv = v3_dot(p, lf->ay) / lf->extent * 0.5f + 0.5f;
    (void)d;
    *u = xu;
    *v = yv;
}

/* ground-plane world position for image pixel (px, py) */
static inline void fragment_pos(int px, int py, float* p) {
    float fx = (float)px / (IMG_W - 1);
    float fy = (float)py / (IMG_H - 1);
    p[0] = -16.0f + 32.0f * fx;
    p[1] = -12.0f + 24.0f * fy;
    p[2] = 0.0f;
}

/*
 * Perturbed ground normal (deterministic, no state): stands in for a normal
 * map / micro-surface, so the per-pixel n.l varies instead of being one
 * constant the compiler can hoist out of the bias loop. Tilt stays under
 * ~4 degrees; the surface still reads as flat ground visually.
 */
static inline void ground_normal(int px, int py, float* n) {
    float fx = (float)px / (IMG_W - 1);
    float fy = (float)py / (IMG_H - 1);
    float a = 0.06f * sinf(2.0f * (float)M_PI * (3.0f * fx + 1.7f * fy));
    float b = 0.06f * sinf(2.0f * (float)M_PI * (2.3f * fx - 4.1f * fy) + 1.3f);
    n[0] = a;
    n[1] = b;
    n[2] = 1.0f;
    float inv = 1.0f / sqrtf(a * a + b * b + 1.0f);
    for (int i = 0; i < 3; i++) n[i] *= inv;
}

/* slab intersection of ray (o, dir) with AABB; nearest t >= 0 or +inf */
static inline float ray_box(const float* o, const float* dir, const Box* b) {
    float tmin = 0.0f, tmax = INFINITY;
    for (int i = 0; i < 3; i++) {
        if (fabsf(dir[i]) < 1e-9f) {
            if (o[i] < b->lo[i] || o[i] > b->hi[i]) return INFINITY;
        } else {
            float inv = 1.0f / dir[i];
            float t1 = (b->lo[i] - o[i]) * inv;
            float t2 = (b->hi[i] - o[i]) * inv;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return INFINITY;
        }
    }
    return tmin >= 0.0f ? tmin : INFINITY;
}

#endif
