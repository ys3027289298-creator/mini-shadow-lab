/* B3: pcf does not validate w,h <= MAXK; index sy*m->w+sx runs off d[]. */
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
int main(void) {
    DepthMap m; memset(&m, 0, sizeof m);
    m.w = 12; m.h = 12;   /* > MAXK, but within its own w*h contract */
    for (int i = 0; i < 12*12 && i < MAXK*MAXK; i++) m.d[i] = 0.5f;
    int v = pcf(&m, 8, 8, 0.5f, 0.0f, 1);
    printf("v=%d\n", v);
    return 0;
}
