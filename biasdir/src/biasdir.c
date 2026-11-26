
/* biasdir: receiver offset along the surface normal vs along the light axis.
 *
 * Shadow acne is a depth-axis failure: a fragment is mis-shadowed when its
 * biased depth still exceeds the rasterized receiver depth stored in its
 * shadow-map texel. An offset only helps through its projection onto the
 * light (depth) axis. This lab measures that projection and its cost.
 *
 * Orthographic light looking down +z, so light-space depth equals world z and
 * one shadow-map texel is normalized to one world unit (h = 1). A planar
 * receiver tilted by angle theta rises as z(x) = tan(theta) * x. Its normal
 * satisfies n.L = cos(theta). The shadow pass samples depth at each texel
 * origin and quantizes by floor (finite depth-buffer resolution); the
 * sub-texel slope error against the continuously interpolated receiver is
 * what draws the parallel black stripes.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#define TEXELS 64           /* texels across the test patch              */
#define SUB 32              /* receiver sub-samples per texel column     */
#define SAMPLES (TEXELS * SUB)
#define NT 3                /* orientation tiers                         */
#define STEPS 161           /* d = 0 .. 40 texel, 0.25 texel per step    */
#define BASE_BIAS 0.25f     /* constant depth floor, in texels           */
#define SLOPE_K 1.25f       /* slope factor; smallest passing k, see gate */

static const float angles[NT] = { 0.0f, 45.0f, 80.0f };

enum { LIGHT_CONST = 0, NORMAL_CONST, LIGHT_SLOPE, NORMAL_SLOPE, NMODES };
static const char* mode_name[NMODES] = {
    "light_const", "normal_const", "light_slope", "normal_slope"
};

typedef struct {
    float zmap[TEXELS];   /* rasterized depth per texel */
} ShadowMap;

static float deg2rad(float a) { return a * 3.14159265358979f / 180.0f; }

/* receiver plane: z grows with x; normal = (-sin, 0, cos), n.L = cos */
static float plane_z(float x, float tan_t) { return tan_t * x; }

/* Shadow pass: depth is sampled at the texel origin (nearest-sample
 * convention) and quantized by floor, matching a finite-resolution depth
 * buffer. The main pass then interpolates receiver depth continuously, so
 * the sub-texel slope error alternates sign across columns and draws the
 * stripe pattern; a slope of exactly one quantum per texel (45 deg) yields
 * regular per-texel bands, steeper slopes irregular stripes.
 */
static void rasterize(ShadowMap* m, float tan_t) {
    for (int i = 0; i < TEXELS; i++) {
        float xs = (float)i;                  /* texel origin sample point */
        m->zmap[i] = floorf(plane_z(xs, tan_t));
    }
}

/* One bias rule evaluated at a receiver sample.
 * Returns the depth subtraction b (texels) and the lateral sample slide dx
 * (world units) caused by a normal-directed push. Light-axis pushes have
 * dx = 0 by construction.
 */
typedef struct { float b; float dx; } Offset;

static Offset evaluate(int mode, float d, float cos_t, float tan_t) {
    Offset o;
    switch (mode) {
    case LIGHT_CONST:                       /* push d along the light axis */
        o.b = d;
        o.dx = 0.0f;
        break;
    case NORMAL_CONST: {                    /* push d along the normal */
        float sin_t = sqrtf(1.0f - cos_t * cos_t);
        o.b = d * cos_t;                    /* depth projection  */
        o.dx = d * sin_t;                   /* lateral projection */
        break;
    }
    case LIGHT_SLOPE: {                     /* GL slope-scaled depth bias */
        o.b = BASE_BIAS + SLOPE_K * tan_t;
        o.dx = 0.0f;
        (void)d;
        break;
    }
    case NORMAL_SLOPE: {                    /* same depth budget via normal */
        float b = BASE_BIAS + SLOPE_K * tan_t;
        float sin_t = sqrtf(1.0f - cos_t * cos_t);
        o.b = b;
        o.dx = (b / cos_t) * sin_t;         /* dN = b/cos, lateral = dN sin */
        break;
    }
    default: o.b = 0.0f; o.dx = 0.0f; break;
    }
    return o;
}

static int shadowed(const ShadowMap* m, float x, float zf, const Offset* o) {
    float xs = x + o->dx;
    int j = (int)floorf(xs);
    if (j < 0) j = 0;
    if (j >= TEXELS) j = TEXELS - 1;
    return (zf - o->b > m->zmap[j]) ? 1 : 0;
}

static int failures = 0;
static void check(const char* name, int ok) {
    printf("%s=%d\n", name, ok ? 1 : 0);
    if (!ok) failures++;
}

int main(void) {
    FILE* fsweep = fopen("results/sweep.csv", "w");
    FILE* fgate = fopen("results/gate.csv", "w");
    FILE* fcount = fopen("results/counterexample.csv", "w");
    if (!fsweep || !fgate || !fcount) {
        printf("cannot open results csv\n");
        return 2;
    }
    fprintf(fsweep, "angle_deg,ndotl,d_texel,mode,black_pixels,black_frac_pct,gap_texel,depth_bias_texel\n");
    fprintf(fgate, "rule,angle_deg,ndotl,tan_theta,d_world_texel,depth_bias_texel,black_pixels,black_frac_pct,gap_texel,gate_pass\n");
    fprintf(fcount, "angle_deg,ndotl,mode,d_texel,depth_bias_texel,proj_formula,black_pixels,black_frac_pct\n");

    /* geometry invariants used by the self-tests below */
    float cos0 = cosf(deg2rad(0.0f));
    Offset n0 = evaluate(NORMAL_CONST, 7.0f, cos0, 0.0f);
    Offset l0 = evaluate(LIGHT_CONST, 7.0f, cos0, 0.0f);
    check("front_face_projections_equal", fabsf(n0.b - l0.b) < 1e-5f);

    float cos45 = cosf(deg2rad(45.0f));
    Offset n45 = evaluate(NORMAL_CONST, 8.0f, cos45, 1.0f);
    Offset l45 = evaluate(LIGHT_CONST, 8.0f, cos45, 1.0f);
    check("light_projection_invariant", fabsf(l45.b - 8.0f) < 1e-5f);
    check("normal_projection_is_d_cos_theta", fabsf(n45.b - 8.0f * cos45) < 1e-5f);

    /* inverse solve: to land a depth bias b on a tilted plane the normal
     * push must be dN = b / cos(theta); the light push stays dL = b. */
    float want = 2.0f;
    float dN = want / cos45;
    Offset n_inv = evaluate(NORMAL_CONST, dN, cos45, 1.0f);
    check("inverse_normal_requires_b_over_cos", fabsf(n_inv.b - want) < 1e-4f);

    int lslope_clean_all = 1;
    float nslope_gap_max = 0.0f;

    for (int t = 0; t < NT; t++) {
        float theta = deg2rad(angles[t]);
        float cos_t = cosf(theta);
        float tan_t = tanf(theta);
        ShadowMap m;
        rasterize(&m, tan_t);

        /* first d at which each constant rule drives black area to zero */
        float first_clean[NMODES];
        for (int k = 0; k < NMODES; k++) first_clean[k] = -1.0f;

        for (int si = 0; si < STEPS; si++) {
            float d = 0.25f * (float)si;
            for (int mode = LIGHT_CONST; mode <= NORMAL_CONST; mode++) {
                Offset o = evaluate(mode, d, cos_t, tan_t);
                int black = 0;
                for (int p = 0; p < SAMPLES; p++) {
                    int i = p / SUB, k = p % SUB;
                    float x = (float)i + ((float)k + 0.5f) / (float)SUB;
                    float zf = plane_z(x, tan_t);
                    black += shadowed(&m, x, zf, &o);
                }
                float pct = 100.0f * (float)black / (float)SAMPLES;
                float gap = fabsf(o.dx);   /* light-axis rule: 0 */
                fprintf(fsweep, "%.0f,%.4f,%.2f,%s,%d,%.3f,%.3f,%.3f\n",
                        angles[t], cos_t, d, mode_name[mode], black, pct, gap, o.b);
                if (black == 0 && first_clean[mode] < 0.0f) first_clean[mode] = d;
            }
        }

        /* fixed slope-scaled rules: one evaluation per tier */
        for (int mode = LIGHT_SLOPE; mode <= NORMAL_SLOPE; mode++) {
            Offset o = evaluate(mode, 0.0f, cos_t, tan_t);
            int black = 0;
            for (int p = 0; p < SAMPLES; p++) {
                int i = p / SUB, k = p % SUB;
                float x = (float)i + ((float)k + 0.5f) / (float)SUB;
                float zf = plane_z(x, tan_t);
                black += shadowed(&m, x, zf, &o);
            }
            float pct = 100.0f * (float)black / (float)SAMPLES;
            float gap = fabsf(o.dx);
            float dworld = (mode == LIGHT_SLOPE) ? o.b : o.b / cos_t;
            int pass = (black == 0 && gap <= 1.0f);
            fprintf(fgate, "%s,%.0f,%.4f,%.4f,%.3f,%.3f,%d,%.3f,%.3f,%d\n",
                    mode_name[mode], angles[t], cos_t, tan_t, dworld, o.b,
                    black, pct, gap, pass);
            if (mode == LIGHT_SLOPE && black != 0) lslope_clean_all = 0;
            if (mode == NORMAL_SLOPE && gap > nslope_gap_max) nslope_gap_max = gap;
        }

        /* gate rows for the constant rules at their smallest clean d */
        for (int mode = LIGHT_CONST; mode <= NORMAL_CONST; mode++) {
            float d = first_clean[mode];
            if (d < 0.0f) {
                /* never cleaned inside the sweep: report the sweep endpoint */
                d = 0.25f * (float)(STEPS - 1);
            }
            Offset o = evaluate(mode, d, cos_t, tan_t);
            int black = 0;
            for (int p = 0; p < SAMPLES; p++) {
                int i = p / SUB, k = p % SUB;
                float x = (float)i + ((float)k + 0.5f) / (float)SUB;
                float zf = plane_z(x, tan_t);
                black += shadowed(&m, x, zf, &o);
            }
            float pct = 100.0f * (float)black / (float)SAMPLES;
            float gap = fabsf(o.dx);
            int pass = (black == 0 && gap <= 1.0f);
            fprintf(fgate, "%s,%.0f,%.4f,%.4f,%.3f,%.3f,%d,%.3f,%.3f,%d\n",
                    mode_name[mode], angles[t], cos_t, tan_t, d, o.b,
                    black, pct, gap, pass);
        }

        /* counterexample: push ten texels, both directions */
        for (int mode = LIGHT_CONST; mode <= NORMAL_CONST; mode++) {
            float d = 10.0f;
            Offset o = evaluate(mode, d, cos_t, tan_t);
            int black = 0;
            for (int p = 0; p < SAMPLES; p++) {
                int i = p / SUB, k = p % SUB;
                float x = (float)i + ((float)k + 0.5f) / (float)SUB;
                float zf = plane_z(x, tan_t);
                black += shadowed(&m, x, zf, &o);
            }
            float pct = 100.0f * (float)black / (float)SAMPLES;
            const char* formula = (mode == LIGHT_CONST)
                ? "b=d" : "b=d*cos(theta)";
            fprintf(fcount, "%.0f,%.4f,%s,%.2f,%.3f,%s,%d,%.3f\n",
                    angles[t], cos_t, mode_name[mode], d, o.b, formula,
                    black, pct);
        }
    }

    check("light_slope_zero_black_all_tiers", lslope_clean_all);
    check("normal_slope_breaks_texel_budget", nslope_gap_max > 1.0f);

    /* counterexample expectations: 10 texels along light cleans every tier;
     * 10 texels along normal leaves the grazing tier visibly black. */
    fclose(fsweep); fclose(fgate); fclose(fcount);
    FILE* fc = fopen("results/counterexample.csv", "r");
    int l10_black = 0, n10_black80 = 0;
    char line[256];
    while (fgets(line, sizeof(line), fc)) {
        int angle, black; char mode[32];
        if (sscanf(line, "%d,%*f,%31[^,],%*f,%*f,%*[^,],%d", &angle, mode, &black) == 3) {
            if (angle == 80 && strcmp(mode, "light_const") == 0) l10_black = black;
            if (angle == 80 && strcmp(mode, "normal_const") == 0) n10_black80 = black;
        }
    }
    fclose(fc);
    check("ten_texel_light_clears_grazing", l10_black == 0);
    check("ten_texel_normal_still_black", n10_black80 > 0);

    printf("all_ok=%d\n", failures == 0);
    return failures == 0 ? 0 : 1;
}
