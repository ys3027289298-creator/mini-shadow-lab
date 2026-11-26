
/* seam_analyze: quantify the CSM brightness seam and back-solve cascade
 * resolution ratios that keep the filter-width jump under a threshold.
 *
 * Model (matches cascade/src/cascade.c and pcf/src/pcf.c):
 *   practical split  : split_i = lambda * log_i + (1-lambda) * uniform_i
 *   cascade depth    : span_i  = split_i - split_{i-1}            (split_-1 = near)
 *   world texel size : t_i     = span_i / R_i
 *   fixed-radius PCF : world kernel width w_i = (2r+1) * t_i
 *   seam jump        : J_i = w_{i+1}/w_i = t_{i+1}/t_i
 *                            = (span_{i+1}/span_i) * (R_i/R_{i+1})
 *
 * Constraint "jump no more than p" (e.g. p = 0.10):
 *   J_i <= 1+p  =>  R_{i+1}/R_i >= span_{i+1} / ((1+p) * span_i).
 *
 * Modes:
 *   ./seam_analyze analyze        human-readable report for 3 view presets
 *   ./seam_analyze sweep <preset> CSV: budget,max-jump for two allocators
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAXC 8
#define JUMP_TOL 0.10          /* seam must not soften by more than +10% */

typedef struct {
    float splits[MAXC];       /* far view-depth bound of each cascade */
    float span[MAXC];         /* depth span each cascade covers */
    int n;
} Cascades;

static int build(Cascades *c, int n, float near_d, float far_d, float lambda) {
    if (n <= 0 || n > MAXC) return -1;
    if (near_d <= 0.0f || far_d <= near_d) return -1;
    if (lambda < 0.0f || lambda > 1.0f) return -1;
    for (int i = 1; i <= n; i++) {
        float r = (float)i / (float)n;
        float log_s = near_d * powf(far_d / near_d, r);
        float uni_s = near_d + (far_d - near_d) * r;
        c->splits[i - 1] = lambda * log_s + (1.0f - lambda) * uni_s;
    }
    float prev = near_d;
    for (int i = 0; i < n; i++) {
        c->span[i] = c->splits[i] - prev;
        prev = c->splits[i];
    }
    c->n = n;
    return 0;
}

static float texel_world(const Cascades *c, int idx, float res) {
    if (idx < 0 || idx >= c->n || res <= 0.0f) return -1.0f;
    return c->span[idx] / res;
}

/* J_i = t_{i+1} / t_i for the boundary between cascade i and i+1. */
static float seam_jump(const Cascades *c, const float *res, int i) {
    if (i < 0 || i >= c->n - 1) return -1.0f;
    return texel_world(c, i + 1, res[i + 1]) /
           texel_world(c, i, res[i]);
}

static float max_jump(const Cascades *c, const float *res) {
    float m = 0.0f;
    for (int i = 0; i < c->n - 1; i++) {
        float j = seam_jump(c, res, i);
        if (j > m) m = j;
    }
    return m;
}

/* Smallest continuous (real-valued) resolutions meeting J_i <= 1+tol on
 * every seam, normalised so cascade 0 = r0. Each seam is made exactly tight,
 * which is the cheapest possible allocation for a given r0. */
static void recommend_continuous(const Cascades *c, float tol, float r0,
                                 float *out) {
    out[0] = r0;
    for (int i = 0; i < c->n - 1; i++) {
        float ratio = c->span[i + 1] / ((1.0f + tol) * c->span[i]);
        out[i + 1] = out[i] * ratio;
    }
}

/* Continuous optimum scaled so the cascade resolutions sum to budget B. */
static void alloc_continuous_budget(const Cascades *c, float tol, float budget,
                                    float *out) {
    float unit[MAXC];
    recommend_continuous(c, tol, 1.0f, unit);   /* shape, R0 = 1 */
    float sum = 0.0f;
    for (int i = 0; i < c->n; i++) sum += unit[i];
    float scale = budget / sum;
    for (int i = 0; i < c->n; i++) out[i] = unit[i] * scale;
}

static int next_pow2_ge(float x) {
    int p = 1;
    while ((float)p < x) p <<= 1;
    return p;
}

/* Power-of-two allocator under a total budget. Enumerate R0 (pow2), derive the
 * minimum pow2 per remaining cascade that keeps every seam <= 1+tol, keep the
 * candidate whose total is largest without exceeding budget. */
static long alloc_pow2_budget(const Cascades *c, float tol, long budget,
                              int *out) {
    float unit[MAXC];
    recommend_continuous(c, tol, 1.0f, unit);   /* required shape vs R0 */
    int best[MAXC] = {0};
    long best_total = 0;
    for (int r0 = 1; r0 <= budget; r0 <<= 1) {
        int cand[MAXC];
        long total = 0;
        for (int i = 0; i < c->n; i++) {
            cand[i] = next_pow2_ge(r0 * unit[i]);
            total += cand[i];
        }
        if (total <= budget && total > best_total) {
            best_total = total;
            for (int i = 0; i < c->n; i++) best[i] = cand[i];
        }
    }
    for (int i = 0; i < c->n; i++) out[i] = best[i];
    return best_total;
}

/* Equal-resolution baseline at the same total budget. */
static void alloc_equal(const Cascades *c, long budget, float *out) {
    float r = (float)budget / c->n;
    for (int i = 0; i < c->n; i++) out[i] = r;
}

static void analyze(const char *tag, float near_d, float far_d,
                    float lambda, int n, int equal_res) {
    Cascades c;
    if (build(&c, n, near_d, far_d, lambda) != 0) {
        printf("bad preset %s\n", tag);
        return;
    }

    printf("================================================================\n");
    printf("preset=%s  near=%.2f far=%.0f lambda=%.2f cascades=%d\n",
           tag, near_d, far_d, lambda, n);
    printf("----------------------------------------------------------------\n");
    printf("%-4s %12s %12s %14s\n", "casc", "split(far)", "depth_span",
           "texel@equal_res");
    float equal[MAXC];
    for (int i = 0; i < n; i++) {
        equal[i] = (float)equal_res;
        printf("%-4d %12.3f %12.3f %14.6f\n", i, c.splits[i], c.span[i],
               texel_world(&c, i, equal[i]));
    }

    printf("\n[a] equal resolution R=%d (the current seam source):\n",
           equal_res);
    int fails = 0;
    for (int i = 0; i < n - 1; i++) {
        float ti = texel_world(&c, i, equal[i]);
        float tj = texel_world(&c, i + 1, equal[i + 1]);
        float j = seam_jump(&c, equal, i);
        int bad = j > 1.0f + JUMP_TOL;
        fails += bad;
        printf("    seam %d->%d : t%d=%.6f t%d=%.6f  jump=%.3f (%+.1f%%)  %s\n",
               i, i + 1, i, ti, i + 1, tj, j, (j - 1.0f) * 100.0f,
               bad ? "EXCEEDS +10%" : "ok");
    }
    if (!fails) printf("    all seams within tolerance\n");

    printf("\n[b] required per-seam resolution ratio R_{i+1}/R_i (jump<=10%%):\n");
    for (int i = 0; i < n - 1; i++) {
        float rr = c.span[i + 1] / ((1.0f + JUMP_TOL) * c.span[i]);
        printf("    R%d/R%d >= %.3f\n", i + 1, i, rr);
    }

    long equal_budget = (long)equal_res * n;
    printf("\n[c] continuous optimum at same budget B=%ld (every seam tight):\n",
           equal_budget);
    float cont[MAXC];
    alloc_continuous_budget(&c, JUMP_TOL, (float)equal_budget, cont);
    long cont_total = 0;
    for (int i = 0; i < n; i++) {
        printf("    R%d = %9.1f   texel=%.6f\n", i, cont[i],
               texel_world(&c, i, cont[i]));
        cont_total += lroundf(cont[i]);
    }
    printf("    total=%ld  max jump=%.3f (%+.1f%%)\n", cont_total,
           max_jump(&c, cont), (max_jump(&c, cont) - 1.0f) * 100.0f);

    printf("\n[d] power-of-two allocation inside the same budget B=%ld:\n",
           equal_budget);
    int p2[MAXC];
    long p2_total = alloc_pow2_budget(&c, JUMP_TOL, equal_budget, p2);
    float p2f[MAXC];
    for (int i = 0; i < n; i++) p2f[i] = (float)p2[i];
    for (int i = 0; i < n; i++)
        printf("    R%d = %6d   texel=%.6f\n", i, p2[i],
               texel_world(&c, i, p2f[i]));
    for (int i = 0; i < n - 1; i++)
        printf("    seam %d->%d jump=%.3f (%+.1f%%) %s\n", i, i + 1,
               seam_jump(&c, p2f, i),
               (seam_jump(&c, p2f, i) - 1.0f) * 100.0f,
               seam_jump(&c, p2f, i) > 1.0f + JUMP_TOL ? "EXCEEDS" : "ok");
    printf("    total=%ld (budget headroom %ld)  max jump=%.3f\n",
           p2_total, equal_budget - p2_total, max_jump(&c, p2f));
    printf("\n");
}

/* CSV: budget, max-jump for equal / continuous / pow2 allocators. */
static void sweep(const Cascades *c) {
    printf("budget,equal_jump,continuous_jump,pow2_jump,pow2_total\n");
    for (long b = 1024; b <= 12288; b += 512) {
        float eq[MAXC], ct[MAXC];
        int p2[MAXC];
        alloc_equal(c, b, eq);
        alloc_continuous_budget(c, JUMP_TOL, (float)b, ct);
        long p2t = alloc_pow2_budget(c, JUMP_TOL, b, p2);
        float p2f[MAXC];
        for (int i = 0; i < c->n; i++) p2f[i] = (float)(p2[i] ? p2[i] : 1);
        printf("%ld,%.4f,%.4f,%.4f,%ld\n", b, max_jump(c, eq),
               max_jump(c, ct), p2t ? max_jump(c, p2f) : 0.0f, p2t);
    }
}

/* recursive enumeration of non-decreasing pow2 patterns anchored at r0:
 * each later level is either equal to or double the previous one. */
static void emit_pow2(const Cascades *c, int *pat, int level, int r0) {
    (void)r0;
    if (level == c->n) {
        float pf[MAXC], total = 0.0f;
        for (int i = 0; i < c->n; i++) {
            pf[i] = (float)pat[i];
            total += (float)pat[i];
        }
        printf("%.3f,%.0f,", max_jump(c, pf), total);
        for (int i = 0; i < c->n; i++) printf("%s%d", i ? "-" : "", pat[i]);
        printf("\n");
        return;
    }
    int prev = pat[level - 1];
    pat[level] = prev;       emit_pow2(c, pat, level + 1, r0);
    pat[level] = prev << 1;  emit_pow2(c, pat, level + 1, r0);
}

/* Trade-off curve with the near cascade anchored at r0 (its sharpness is held
 * fixed). For each allowed jump p, the cheapest continuous allocation that
 * meets every seam has R_i = r0 * span_i / (span_0 * (1+p)^i).
 * CSV block 1 ("cont"): allowed_jump -> total texels on that tight frontier.
 * CSV block 2 ("pow2"): the discrete (jump,total) points that strict
 * power-of-two cascades (R0 fixed, later levels equal or double) can reach. */
static void curve(const Cascades *c, int r0) {
    printf("cont_allowed_jump,cont_total_texels\n");
    for (int k = 1; k <= 160; k++) {
        float p = 0.01f + 0.02f * k;          /* 1.03 .. 4.21 factor */
        float factor = 1.0f + p;
        float total = 0.0f;
        for (int i = 0; i < c->n; i++) {
            float ri = (float)r0 * c->span[i] /
                       (c->span[0] * powf(factor, (float)i));
            total += ri;
        }
        printf("%.3f,%.0f\n", p, total);
    }
    printf("pow2_actual_jump,pow2_total_texels,pattern\n");
    int pat[MAXC];
    pat[0] = r0;
    emit_pow2(c, pat, 1, r0);
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "analyze";
    if (strcmp(mode, "sweep") == 0 || strcmp(mode, "curve") == 0) {
        const char *preset = argc > 2 ? argv[2] : "far";
        struct { const char *tag; float near_d, far_d, lambda; int n; } ps[] = {
            {"near-field", 0.5f, 100.0f, 0.5f, 3},
            {"mid",        0.5f, 500.0f, 0.5f, 3},
            {"far",        0.5f, 1000.0f, 0.5f, 3},
        };
        for (int k = 0; k < 3; k++) {
            if (strcmp(ps[k].tag, preset) == 0) {
                Cascades c;
                build(&c, ps[k].n, ps[k].near_d, ps[k].far_d, ps[k].lambda);
                if (strcmp(mode, "sweep") == 0) sweep(&c);
                else curve(&c, 2048);
                return 0;
            }
        }
        fprintf(stderr, "unknown preset %s\n", preset);
        return 1;
    }
    analyze("near-field", 0.5f, 100.0f, 0.5f, 3, 2048);
    analyze("mid",        0.5f, 500.0f, 0.5f, 3, 2048);
    analyze("far",        0.5f, 1000.0f, 0.5f, 3, 2048);
    return 0;
}
