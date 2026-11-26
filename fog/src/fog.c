
/* fog: how fog composites over a shaded (shadowed) pixel.
 *
 * Scene radiance after shadowing:
 *   L_lit  = lit   surface in light
 *   L_shd  = shadow surface (direct term killed by the shadow map)
 * Fog along view distance d with extinction sigma, transmittance:
 *   T = exp(-sigma*d);  fog factor f = 1 - T
 *
 * Two ways to put fog on top (the same T, only the operator differs):
 *   blend (additive in-scatter, the bug):  C = T*L + f*F
 *   mul   (attenuation only):              C = T*L
 */
#include <stdio.h>
#include <math.h>

#define NL 3

/* fixed scene so the three fog densities are the only moving part */
#define L_LIT 0.80f   /* lit-surface radiance */
#define L_SHD 0.20f   /* shadow-surface radiance */
#define F_FOG 0.60f   /* fog colour / in-scatter radiance */
#define DIST  10.0f   /* view distance, world units */
#define R0    (L_LIT / L_SHD)          /* shadow contrast without fog: 4.0 */
#define TOL   1e-4f                    /* invariant-ratio tolerance */
#define KEEP  0.90f                    /* contrast-retention budget */

typedef struct {
    const char* tag;
    float sigma;
    float T;
    float bl_lit, bl_shd, bl_R;        /* additive-blend outputs */
    float ml_lit, ml_shd, ml_R;        /* multiplicative outputs */
} Level;

static Level levels[NL];

/* wrong order: fog in-scatter is mixed onto the shaded pixel */
static float blend_fog(float L, float T, float F) {
    return T * L + (1.0f - T) * F;
}

/* correct order: fog only attenuates the radiance that already carries shadow */
static float mul_fog(float L, float T) {
    return T * L;
}

/* hybrid: (1-alpha) attenuation + alpha additive in-scatter */
static float hybrid(float L, float T, float F, float a) {
    return (1.0f - a) * mul_fog(L, T) + a * blend_fog(L, T, F);
}

static int failures = 0;
static void check(const char* name, int ok) {
    printf("%s=%d\n", name, ok ? 1 : 0);
    if (!ok) failures++;
}

int main(void) {
    const float sigmas[NL] = { 0.02f, 0.10f, 0.30f };
    const char* tags[NL]  = { "light", "medium", "dense" };

    printf("# levels: sigma*d sets T=exp(-sigma*d), d=%.1f, lit=%.2f shd=%.2f fog=%.2f R0=%.2f\n",
           DIST, L_LIT, L_SHD, F_FOG, R0);

    for (int i = 0; i < NL; i++) {
        Level* v = &levels[i];
        v->tag = tags[i];
        v->sigma = sigmas[i];
        v->T = expf(-v->sigma * DIST);

        v->bl_lit = blend_fog(L_LIT, v->T, F_FOG);
        v->bl_shd = blend_fog(L_SHD, v->T, F_FOG);
        v->bl_R   = v->bl_lit / v->bl_shd;

        v->ml_lit = mul_fog(L_LIT, v->T);
        v->ml_shd = mul_fog(L_SHD, v->T);
        v->ml_R   = v->ml_lit / v->ml_shd;

        printf("[%s] sigma=%.2f T=%.4f\n", v->tag, v->sigma, v->T);
        printf("  blend: lit=%.4f shd=%.4f ratio=%.4f\n",
               v->bl_lit, v->bl_shd, v->bl_R);
        printf("  mul  : lit=%.4f shd=%.4f ratio=%.4f\n",
               v->ml_lit, v->ml_shd, v->ml_R);
    }

    /* ---- compression ratio of the additive blend ---------------------- */
    /* contrast as a ratio: R_blend/R0. Algebra gives (T*Llit+fF)/(T*Lshd+fF)
     * divided by Llit/Lshd. Numerically it must match that expression. */
    Level* dens = &levels[NL - 1];
    float f = 1.0f - dens->T;
    float ratio_form = (dens->bl_R / R0);
    float ratio_algebra =
        ((dens->T * L_LIT + f * F_FOG) / (dens->T * L_SHD + f * F_FOG)) / R0;
    check("dense_blend_compression_ratio_matches_formula",
          fabsf(ratio_form - ratio_algebra) < 1e-5f);
    printf("dense_blend_compression_ratio=%.4f (R/R0, 1=no loss)\n", ratio_form);

    /* contrast as a difference (Michelson-style): additive blend shrinks
     * the lit-shadow gap by exactly T:  (bl_lit-bl_shd)/(Llit-Lshd) = T */
    float diff_compression = (dens->bl_lit - dens->bl_shd) / (L_LIT - L_SHD);
    check("dense_blend_gap_compressed_by_T",
          fabsf(diff_compression - dens->T) < 1e-5f);
    printf("dense_blend_gap_compression=%.4f == T=%.4f\n",
           diff_compression, dens->T);

    /* ---- three densities: multiplicative must keep contrast ------------ */
    int all_mul_ok = 1, any_blend_ok = 0;
    for (int i = 0; i < NL; i++) {
        char name[96];
        snprintf(name, sizeof(name), "%s_mul_contrast_invariant", levels[i].tag);
        int ok = fabsf(levels[i].ml_R - R0) < TOL;
        check(name, ok);
        all_mul_ok = all_mul_ok && ok;

        snprintf(name, sizeof(name), "%s_blend_contrast_lost", levels[i].tag);
        int lost = levels[i].bl_R < R0 - TOL;
        check(name, lost);
        any_blend_ok = any_blend_ok || !lost;
    }
    check("mul_invariant_at_all_three_densities", all_mul_ok);
    check("blend_fails_at_all_three_densities", !any_blend_ok);

    /* fog thickens: additive ratio keeps dropping, multiplication flat */
    check("blend_ratio_monotone_decreasing",
          levels[0].bl_R > levels[1].bl_R && levels[1].bl_R > levels[2].bl_R);
    check("mul_ratio_flat",
          fabsf(levels[0].ml_R - levels[1].ml_R) < TOL &&
          fabsf(levels[1].ml_R - levels[2].ml_R) < TOL);

    /* inverse solve: demand R(T) == R0 for every T.  Any additive term
     * g(T)*F > 0 breaks it, so the only solution is g(T) == 0: pure multiply */
    float g_dense = f * F_FOG;                 /* additive radiance on shadow */
    check("inverse_solve_forces_zero_additive", g_dense > 0.0f); /* present in blend... */
    check("inverse_solve_mul_has_no_additive",
          fabsf(mul_fog(L_SHD, dens->T) - dens->T * L_SHD) < 1e-6f);

    /* ---- trade-off curves at dense fog: contrast vs overall brightness --- */
    printf("# dense-fog trade-off (alpha = fraction of additive in-scatter)\n");
    float alpha_max = -1.0f;
    for (int k = 0; k <= 1000; k++) {
        float a = k / 1000.0f;
        float hl = hybrid(L_LIT, dens->T, F_FOG, a);
        float hs = hybrid(L_SHD, dens->T, F_FOG, a);
        float hR = hl / hs;
        float mean = 0.5f * (hl + hs);
        if (k % 100 == 0)
            printf("  alpha=%.2f ratio=%.4f mean=%.4f\n", a, hR, mean);
        if (hR >= KEEP * R0) alpha_max = a;   /* largest alpha inside budget */
    }
    check("alpha0_is_pure_mul",
          fabsf(hybrid(L_LIT, dens->T, F_FOG, 0.0f) - dens->ml_lit) < 1e-6f);
    check("alpha1_is_pure_blend",
          fabsf(hybrid(L_LIT, dens->T, F_FOG, 1.0f) - dens->bl_lit) < 1e-6f);
    check("hybrid_alpha_budget_found", alpha_max >= 0.0f && alpha_max < 0.05f);
    printf("dense_hybrid_alpha_budget=%.3f (max additive fraction keeping ratio>=%.1f*R0)\n",
           alpha_max, KEEP);

    /* mul preserves contrast but goes dark; blend stays bright but washes out */
    check("mul_darker_than_blend_at_dense",
          dens->ml_lit < dens->bl_lit && dens->ml_shd < dens->bl_shd);
    check("mul_dense_over_black_below_quarter", 0.5f * (dens->ml_lit + dens->ml_shd) < 0.25f);

    /* ---- over-limit report: every density the additive blend fails ------- */
    printf("# out_of_tolerance (contrast must stay R0=%.2f, budget ratio>=%.2f)\n",
           R0, KEEP * R0);
    int over = 0;
    for (int i = 0; i < NL; i++) {
        Level* v = &levels[i];
        if (v->bl_R < KEEP * R0) {
            printf("  OVER blend sigma=%.2f T=%.4f shd=%.4f lit=%.4f ratio=%.4f\n",
                   v->sigma, v->T, v->bl_shd, v->bl_lit, v->bl_R);
            over++;
        }
    }
    check("blend_over_limit_count_three", over == NL);

    printf("all_ok=%d\n", failures == 0);
    return failures == 0 ? 0 : 1;
}
