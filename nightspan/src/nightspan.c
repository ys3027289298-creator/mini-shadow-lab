/* nightspan: night-time shadow banding from depth quantization.
 *
 * Model
 * -----
 * Depth map: BITS levels spread over light-frustum depth span S,
 *   quantization step  d = S / 2^BITS            (world units)
 * Night condition: moonlight dim, receiver depths cluster in [n, n+u], u << S,
 * span S unchanged -> only N = u/d levels are ever used:
 *   B_eff = log2(N) = BITS - log2(S/u)           (effective vs actual bits)
 * Compressing span S -> S' recovers  R = log2(S/S')  bits; at S' = u, B_eff = BITS.
 *
 * Banding visibility: shading variation across the cluster (~L_dir*NdotL) is
 * stair-stepped into N bands, adjacent-band luminance jump
 *   dL = L_dir*NdotL / N = L_dir*NdotL*S / (u * 2^BITS)
 * The eye's quantization level at adaptation L_a is one Weber step w*L_a
 * (w ~ 0.02), L_a = L_amb + 0.5*L_dir*NdotL.  Bands invisible iff
 *   dL <= w*L_a   <=>   S <= S_max = w*L_a*u*2^BITS / (L_dir*NdotL)
 * At night L_a/L_dir collapses (ambient gone), so S_max shrinks and a fixed
 * day-tuned span fails; compressing the span restores it.
 */
#include <stdio.h>
#include <math.h>

#define BITS       16.0    /* actual depth bits in the shadow map */
#define SPAN_FIXED 1000.0  /* fixed light-frustum depth span, world units */
#define WEBER      0.02    /* one perceptual quantization level (1 JND) */
#define NDOTL      0.7     /* representative surface orientation */
#define MARGIN     1.5     /* corrected span = cluster * MARGIN */

typedef struct { const char* name; double ldir, lamb, u; } Tier;

static double two_pow_b(void) { return ldexp(1.0, (int)BITS); }

/* levels of the map that fall inside the receiver cluster */
static double levels(double u, double span) { return u * two_pow_b() / span; }

/* effective bits: B_eff = BITS - log2(S/u) */
static double b_eff(double u, double span) { return BITS - log2(span / u); }

/* bits recovered by compressing span s_old -> s_new */
static double recovered_bits(double s_old, double s_new) { return log2(s_old / s_new); }

static double adapt_lum(const Tier* t) { return t->lamb + 0.5 * t->ldir * NDOTL; }

/* adjacent-band luminance jump dL */
static double band_step(const Tier* t, double span) {
    return t->ldir * NDOTL / levels(t->u, span);
}

/* Weber contrast of one band step; visible when > WEBER */
static double contrast(const Tier* t, double span) {
    return band_step(t, span) / adapt_lum(t);
}

/* largest span keeping bands invisible: S_max from dL = w*L_a */
static double span_max(const Tier* t) {
    return WEBER * adapt_lum(t) * t->u * two_pow_b() / (t->ldir * NDOTL);
}

static int failures = 0;
static void check(const char* name, int ok) {
    printf("%s=%d\n", name, ok ? 1 : 0);
    if (!ok) failures++;
}

int main(void) {
    /* three light tiers: day sun, dusk, night moon */
    Tier tiers[3] = {
        { "day",   1.0,   0.10,   1000.0 },  /* receivers use full range */
        { "dusk",  0.02,  5.0e-4,   60.0 },
        { "night", 0.003, 2.0e-5,    1.2 },  /* depths cluster near */
    };

    printf("-- relations --\n");
    printf("quant_step      d = S/2^BITS = %.4f world units (S=%.0f, BITS=%.0f)\n",
           SPAN_FIXED / two_pow_b(), SPAN_FIXED, BITS);
    printf("effective_bits  B_eff = BITS - log2(S/u)\n");
    printf("recovered_bits  R = log2(S/S_new);  S_new=u restores B_eff=BITS\n");
    printf("invisibility    dL <= w*L_a  <=>  S <= w*L_a*u*2^BITS/(L_dir*NdotL)\n\n");

    printf("-- three tiers, fixed span S=%.0f --\n", SPAN_FIXED);
    for (int i = 0; i < 3; i++) {
        Tier* t = &tiers[i];
        double dl = band_step(t, SPAN_FIXED);
        double q = WEBER * adapt_lum(t);
        double c = contrast(t, SPAN_FIXED);
        printf("%-5s u=%7.1f  N=%9.1f  B_eff=%5.2f  dL=%.3e  q=w*L_a=%.3e  C=%.5f  %s\n",
               t->name, t->u, levels(t->u, SPAN_FIXED), b_eff(t->u, SPAN_FIXED),
               dl, q, c, c <= WEBER ? "PASS(invisible)" : "FAIL(banding)");
        if (c > WEBER) {
            double s_corr = t->u * MARGIN;
            printf("      -> span=%.0f  B_eff=%.2f  S_max=%.1f  correct to span=%.1f"
                   "  (B_eff=%.2f, R=%.2f bits, C=%.5f)\n",
                   SPAN_FIXED, b_eff(t->u, SPAN_FIXED), span_max(t), s_corr,
                   b_eff(t->u, s_corr), recovered_bits(SPAN_FIXED, s_corr),
                   contrast(t, s_corr));
        }
    }

    printf("\n-- strategy: visibility curve vs recompute cost (night tier) --\n");
    Tier* nt = &tiers[2];
    for (double s = SPAN_FIXED; s >= 2.0; s /= 4.0)
        printf("span=%8.1f  C=%.5f %s\n", s, contrast(nt, s),
               contrast(nt, s) <= WEBER ? "ok" : "BANDING");
    /* cost: one projection refit + shadow-map re-render = 1 unit;
     * 1000-frame night sequence with 3 tier transitions */
    printf("cost: dynamic(per-frame)=1000  tiered(3 transitions)=3  fixed=0"
           "  -> tiered presets + hysteresis\n\n");

    /* self-checks */
    check("beff_full_range", fabs(b_eff(1000.0, 1000.0) - BITS) < 1e-9);
    check("waste_is_log_ratio",
          fabs((BITS - b_eff(1.2, SPAN_FIXED)) - log2(SPAN_FIXED / 1.2)) < 1e-9);
    check("recovered_bits_value",
          fabs(recovered_bits(SPAN_FIXED, 1.2 * MARGIN) - 9.12) < 0.01);
    check("compress_to_cluster_restores",
          fabs(b_eff(1.2, 1.2) - BITS) < 1e-9);
    check("span_max_solves_threshold",
          fabs(contrast(nt, span_max(nt)) - WEBER) < 1e-9);

    check("day_pass", contrast(&tiers[0], SPAN_FIXED) < WEBER);
    check("dusk_pass", contrast(&tiers[1], SPAN_FIXED) < WEBER);
    check("night_fails_fixed", contrast(nt, SPAN_FIXED) > WEBER);

    double s_corr = nt->u * MARGIN;
    check("corrected_covers_cluster", s_corr >= nt->u);
    check("corrected_within_limit", s_corr <= span_max(nt));
    check("corrected_passes", contrast(nt, s_corr) < WEBER);
    check("jump_below_one_level",
          band_step(nt, s_corr) <= WEBER * adapt_lum(nt));
    check("contrast_grows_with_span",
          contrast(nt, SPAN_FIXED) > contrast(nt, SPAN_FIXED / 4.0));

    printf("all_ok=%d\n", failures == 0);
    return failures == 0 ? 0 : 1;
}
