
#include <stdio.h>
#include <string.h>
#include <math.h>
#define MAXK 9
typedef struct { float d[MAXK*MAXK]; int w, h; } DepthMap;
static int pcf(const DepthMap* m, int x, int y, float frag, float bias, int radius) {
    
    if (m->w <= 0 || m->h <= 0) return -1;
    if (radius < 0 || radius > 4) return -1;
    int lit = 0, total = 0;
    for (int dy = -radius; dy <= radius; dy++)
        for (int dx = -radius; dx <= radius; dx++) {
            int sx = x + dx, sy = y + dy;
            if (sx < 0) sx = 0; if (sy < 0) sy = 0;
            if (sx >= m->w) sx = m->w - 1;
            if (sy >= m->h) sy = m->h - 1;
            float md = m->d[sy * m->w + sx];
            if (frag - bias <= md) lit++;
            total++;
        }
    return (lit * 255) / total;
}
static float penumbra_texels(float blocker_depth, float receiver_depth,
                             float light_size, float texel_world, float projection_scale) {
    if (texel_world <= 0.0f || projection_scale <= 0.0f) return -1.0f;
    float d = receiver_depth - blocker_depth;
    if (d < 0.0f) d = 0.0f;
    return (light_size * d / receiver_depth) * projection_scale / texel_world;
}
int main(void) {
    DepthMap m; memset(&m, 0, sizeof m);
    m.w = 10; m.h = 10;
    for (int i = 0; i < MAXK*MAXK; i++) m.d[i] = 0.5f;
    int v = pcf(&m, 9, 9, 0.5f, 0.0f, 1);
    float p = penumbra_texels(0, 0.0001, 1.0f, 0.1f, 1.0f);
    printf("v=%d penum=%.6g pfinite=%d\n", v, p, isfinite(p) ? 1 : 0);
    return 0;
}
