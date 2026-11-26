
/* cascade: split view depth range into cascades and pick resolutions. */
#include <stdio.h>
#include <string.h>
#include <math.h>

#define MAXC 4

typedef struct {
    float splits[MAXC];   /* far bound of each cascade, world units */
    int n;
} Cascades;

/* practical split scheme: blend of logarithmic and uniform, lambda in [0,1] */
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
    c->n = n;
    return 0;
}

/* texel size in world units for a cascade covering [0, split] with res texels */
static float texel_world(const Cascades* c, int idx, float res) {
    if (idx < 0 || idx >= c->n || res <= 0.0f) return -1.0f;
    float near_d = (idx == 0) ? 0.0f : c->splits[idx - 1];
    float span = c->splits[idx] - near_d;
    return span / res;
}

static int failures = 0;
static void check(const char* name, int ok) {
    printf("%s=%d\n", name, ok ? 1 : 0);
    if (!ok) failures++;
}

int main(void) {
    Cascades c;
    check("build_ok", build(&c, 3, 0.5f, 1000.0f, 0.5f) == 0);
    check("splits_increasing", c.splits[0] < c.splits[1] && c.splits[1] < c.splits[2]);
    check("last_is_far", fabsf(c.splits[2] - 1000.0f) < 1e-2f);
    check("first_above_near", c.splits[0] > 0.5f);

    /* lambda = 1 gives logarithmic, which is very small near */
    Cascades lg;
    build(&lg, 3, 0.5f, 1000.0f, 1.0f);
    check("lambda1_smaller_first", lg.splits[0] < c.splits[0]);

    /* lambda = 0 gives uniform, equal spans */
    Cascades un;
    build(&un, 3, 0.5f, 1000.0f, 0.0f);
    float s1 = un.splits[0] - 0.5f;
    float s2 = un.splits[1] - un.splits[0];
    float s3 = un.splits[2] - un.splits[1];
    check("uniform_equal", fabsf(s2 - s1) < 1e-2f && fabsf(s3 - s1) < 1e-2f);

    /* texel size grows for farther cascades */
    check("texel_grows", texel_world(&c, 0, 1024.0f) < texel_world(&c, 2, 1024.0f));

    check("bad_n", build(&c, 0, 0.5f, 100.0f, 0.5f) == -1);
    check("bad_range", build(&c, 3, 10.0f, 1.0f, 0.5f) == -1);
    check("bad_lambda", build(&c, 3, 0.5f, 100.0f, 1.5f) == -1);
    check("bad_texel_idx", texel_world(&c, 9, 1024.0f) < 0.0f);

    printf("all_ok=%d\n", failures == 0);
    return failures == 0 ? 0 : 1;
}
