
/* shadowmap: project world point to light space and test occlusion. */
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef struct { float x, y, z, w; } V4;
typedef struct { float m[16]; } Mat4;

static void mat_identity(Mat4* m) {
    memset(m->m, 0, sizeof(m->m));
    for (int i = 0; i < 4; i++) m->m[i * 5] = 1.0f;
}

/* orthographic projection: maps [-1,1]^3 to depth in [0,1] */
static void mat_ortho(Mat4* m, float l, float r, float b, float t, float n, float f) {
    mat_identity(m);
    m->m[0] = 2.0f / (r - l);
    m->m[5] = 2.0f / (t - b);
    m->m[10] = -1.0f / (f - n);
    m->m[12] = -(r + l) / (r - l);
    m->m[13] = -(t + b) / (t - b);
    m->m[14] = -n / (f - n);
}

static V4 apply(const Mat4* m, V4 v) {
    V4 o;
    o.x = m->m[0]*v.x + m->m[4]*v.y + m->m[8]*v.z + m->m[12]*v.w;
    o.y = m->m[1]*v.x + m->m[5]*v.y + m->m[9]*v.z + m->m[13]*v.w;
    o.z = m->m[2]*v.x + m->m[6]*v.y + m->m[10]*v.z + m->m[14]*v.w;
    o.w = m->m[3]*v.x + m->m[7]*v.y + m->m[11]*v.z + m->m[15]*v.w;
    return o;
}

/* depth bias grows with slope to fight acne */
static float depth_bias(float ndotl, float base, float slope) {
    if (ndotl < 0.0f) ndotl = 0.0f;
    if (ndotl > 1.0f) ndotl = 1.0f;
    float tan_acos = sqrtf(1.0f - ndotl * ndotl) / (ndotl > 1e-4f ? ndotl : 1e-4f);
    return base + slope * tan_acos;
}

static int in_shadow(float frag_depth, float map_depth, float bias) {
    return frag_depth - bias > map_depth ? 1 : 0;
}

static int failures = 0;
static void check(const char* name, int ok) {
    printf("%s=%d\n", name, ok ? 1 : 0);
    if (!ok) failures++;
}

int main(void) {
    Mat4 m;
    mat_ortho(&m, -10, 10, -10, 10, 1, 100);
    V4 p = { 0, 0, -50, 1 };
    V4 lp = apply(&m, p);
    check("centre_maps_to_zero", fabsf(lp.x) < 1e-5f && fabsf(lp.y) < 1e-5f);
    check("depth_in_range", lp.z >= 0.0f && lp.z <= 1.0f);

    V4 q = { 10, 0, -50, 1 };
    V4 lq = apply(&m, q);
    check("right_edge_one", fabsf(lq.x - 1.0f) < 1e-5f);

    V4 near_p = { 0, 0, -1, 1 };
    V4 far_p = { 0, 0, -100, 1 };
    V4 ln = apply(&m, near_p), lf = apply(&m, far_p);
    check("near_depth_smaller", ln.z < lf.z);
    check("near_is_zero", fabsf(ln.z) < 1e-5f);
    check("far_is_one", fabsf(lf.z - 1.0f) < 1e-5f);

    check("bias_grows_with_slope",
          depth_bias(1.0f, 0.001f, 0.01f) < depth_bias(0.1f, 0.001f, 0.01f));
    check("bias_base_only_facing", fabsf(depth_bias(1.0f, 0.001f, 0.01f) - 0.001f) < 1e-6f);

    check("occluded", in_shadow(0.50f, 0.40f, 0.0f) == 1);
    check("lit", in_shadow(0.40f, 0.50f, 0.0f) == 0);
    check("bias_rescues", in_shadow(0.405f, 0.40f, 0.01f) == 0);

    printf("all_ok=%d\n", failures == 0);
    return failures == 0 ? 0 : 1;
}
