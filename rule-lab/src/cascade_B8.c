/* B8: texel_world returns -1 sentinel; caller uses it as a size unchecked. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#define MAXC 4
typedef struct { float splits[MAXC]; int n; } Cascades;
static int build(Cascades* c, int n, float near_d, float far_d, float lambda) {
    if (n <= 0 || n > MAXC) return -1;
    if (near_d <= 0.0f || far_d <= near_d) return -1;
    if (lambda < 0.0f || lambda > 1.0f) return -1;
    for (int i = 1; i <= n; i++) {
        float r = (float)i / (float)n;
        float log_s = near_d * powf(far_d / near_d, r);
        float uni_s = near_d + (far_d - near_d) * r;
        c->splits[i - 1] = lambda * log_s + (1.0f - lambda) * uni_s;
    }
    c->n = n; return 0;
}
static float texel_world(const Cascades* c, int idx, float res) {
    if (idx < 0 || idx >= c->n || res <= 0.0f) return -1.0f;
    float near_d = (idx == 0) ? 0.0f : c->splits[idx - 1];
    return (c->splits[idx] - near_d) / res;
}
int main(void) {
    Cascades c; memset(&c, 0, sizeof c);
    build(&c, 3, 0.5f, 1000.0f, 0.5f);
    float texel = texel_world(&c, 9, 1024.0f);   /* -1.0f */
    float penum = 1.0f * 2.0f / texel;           /* divides by -1 */
    printf("texel=%.6f penum=%.6f\n", texel, penum);
    return 0;
}
