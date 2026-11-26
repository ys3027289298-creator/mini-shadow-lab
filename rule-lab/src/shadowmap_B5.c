/* B5: mat_ortho divides by (f-n) and (r-l) with no degeneracy guard. */
#include <stdio.h>
#include <string.h>
#include <math.h>
typedef struct { float x, y, z, w; } V4;
typedef struct { float m[16]; } Mat4;
static void mat_identity(Mat4* m) { memset(m->m, 0, sizeof(m->m)); for (int i=0;i<4;i++) m->m[i*5]=1.0f; }
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
    V4 o; o.x=o.y=o.z=o.w=0.0f;
    o.z = m->m[2]*v.x + m->m[6]*v.y + m->m[10]*v.z + m->m[14]*v.w;
    o.w = m->m[3]*v.x + m->m[7]*v.y + m->m[11]*v.z + m->m[15]*v.w;
    return o;
}
int main(void) {
    Mat4 m; mat_ortho(&m, -10,10, -10,10, 5.0f, 5.0f);   /* f==n degenerate frustum */
    V4 p = {0,0,-5,1};
    V4 lp = apply(&m, p);
    printf("ndc_z=%.6f finite=%d\n", lp.z, isfinite(lp.z) ? 1 : 0);
    return 0;
}
