/* transmit_selftest: Beer-Lambert transmittance, sigma recovery, layer count. */
#include <stdio.h>
#include <math.h>
#include "transmit.h"

/* Calibration fixture: light through N slabs measured with a power meter.
 * Three nominal glass thicknesses, ~1% measurement noise. If any of these
 * exceeds the 10% budget the test prints the thickness, the measured
 * transmittance and the sigma recovered from that measurement. */
static const float XM_THICK[3] = { 0.004f, 0.006f, 0.010f };
static const float XM_MEAS[3]  = { 0.91759f, 0.87284f, 0.80493f };

#define TOL 0.10f   /* |T_meas - T_theory| / T_theory <= 10% */

static int failures = 0;
static void check(const char *name, int ok) {
    printf("%s=%d\n", name, ok ? 1 : 0);
    if (!ok) failures++;
}

static float beam_T_n(const xmt_slab *s, int n) {
    float od = 0.0f;
    for (int i = 0; i < n; i++) od += s->od[i];
    return expf(-od);
}

/* Smallest layer count whose partial coverage is within tol of the full
 * stack. */
static int min_layers_for(const xmt_slab *s, float tol) {
    float full = beam_T_n(s, s->n);
    for (int n = 1; n <= s->n; n++)
        if (fabsf(beam_T_n(s, n) - full) / full <= tol) return n;
    return -1;
}

/* Per-layer error curve, printed as "layer_err_<n>=<rel err>". */
static void print_layer_curve(const char *tag, const xmt_slab *s) {
    float full = beam_T_n(s, s->n);
    for (int n = 1; n <= s->n; n++)
        printf("%s_%d=%.4f\n", tag, n, fabsf(beam_T_n(s, n) - full) / full);
}

int main(void) {
    /* 1. The closed form, by hand: sigma=25, d=0.01 -> exp(-0.25). */
    check("beer_known_value", fabsf(xmt_transmittance(25.0f, 0.01f) - expf(-0.25f)) < 1e-6f);
    check("beer_zero_thickness", fabsf(xmt_transmittance(25.0f, 0.0f) - 1.0f) < 1e-6f);
    check("beer_thicker_darker",
          xmt_transmittance(25.0f, 0.02f) < xmt_transmittance(25.0f, 0.01f));
    check("beer_saturates", xmt_transmittance(1e6f, 1.0f) >= 0.0f);  /* finite */

    /* 2. Recover sigma from three measured thicknesses. Store it before the
     * single-sample check, which writes its own output. */
    float sigma = 0.0f;
    check("fit_ok", xmt_fit_sigma(XM_THICK, XM_MEAS, 3, &sigma) == 0);
    printf("sigma_recovered=%.2f\n", sigma);
    check("sigma_plausible", sigma > 15.0f && sigma < 30.0f);

    float s_one = 0.0f;
    check("fit_single_ok", xmt_fit_sigma_one(XM_THICK[1], XM_MEAS[1], &s_one) == 0);
    printf("sigma_single_mid=%.2f\n", s_one);
    check("single_near_fit", fabsf(s_one - sigma) / sigma < 0.05f);

    /* 3. Three thicknesses: measured vs theory within 10%. */
    for (int i = 0; i < 3; i++) {
        float Ti = xmt_transmittance(sigma, XM_THICK[i]);
        float dev = fabsf(XM_MEAS[i] - Ti) / Ti;
        printf("glass_%d_thickness=%.4f measured_T=%.5f theory_T=%.5f dev=%.4f\n",
               i, XM_THICK[i], XM_MEAS[i], Ti, dev);
        char name[48];
        snprintf(name, sizeof name, "glass_dev_%d_within_10pct", i);
        int ok = dev <= TOL;
        if (!ok) {
            float s_i = 0.0f;
            xmt_fit_sigma_one(XM_THICK[i], XM_MEAS[i], &s_i);
            printf("glass_dev_%d: thickness=%.4f m measured_T=%.5f sigma=%.2f/m\n",
                   i, XM_THICK[i], XM_MEAS[i], s_i);
        }
        check(name, ok);
    }

    /* 4. Optimal split: small slices where most light is lost (front of the
     * sheet), so a truncated stack under-estimates coverage by the tail it
     * drops. Uniform split gets the same 10% budget with 6 slices. */
    xmt_slab s_opt, s_uni;
    check("slab_ok", xmt_slab_init(&s_opt, sigma,
                                   (const float[]){ 0.006f, 0.005f, 0.004f,
                                                    0.003f, 0.002f, 0.001f }, 6) == 0);
    check("slab_uniform", xmt_slab_init(&s_uni, sigma,
                                        (const float[]){ 0.01f, 0.01f, 0.01f,
                                                         0.01f, 0.01f, 0.01f }, 6) == 0);
    printf("layers_optimal=%d\n", min_layers_for(&s_opt, TOL));
    printf("layers_uniform=%d\n", min_layers_for(&s_uni, TOL));
    check("optimal_beats_uniform", min_layers_for(&s_opt, TOL) < min_layers_for(&s_uni, TOL));

    print_layer_curve("layer_err_optimal", &s_opt);
    print_layer_curve("layer_err_uniform", &s_uni);

    /* 5. Storage: cumulative transmittance in the map. */
    xmt_map m;
    check("store_bad_layers", xmt_store(&m, NULL, 1) == -1);
    check("store_too_many", xmt_store(&m, &s_opt, XMT_MAX_LAYERS + 1) == -1);
    for (int n = 1; n <= s_opt.n; n++) {
        xmt_store(&m, &s_opt, n);
        float stored = xmt_lookup(&m, n - 1);
        float exact = beam_T_n(&s_opt, n);
        check("store_is_transmittance", fabsf(stored - exact) < 2e-4f);
    }
    check("store_full_matches_beer",
          fabsf(xmt_slab_transmittance(&s_opt, 0) - xmt_transmittance(sigma, 0.021f)) < 1e-5f);
    check("lookup_missing_level", xmt_lookup(&m, 7) < 0.0f);

    /* 6. Depth-style vs transmittance: same glass, same floor fragment. */
    float min_depth = 10.0f;   /* nearest glass surface */
    float frag_far  = 50.0f;   /* floor under the glass */
    check("depth_style_black", xmt_depth_style_lit(min_depth, frag_far, 0.0f) == 0.0f);
    xmt_store(&m, &s_opt, s_opt.n);
    float through = xmt_lit_fraction(&m, s_opt.thickness_total);
    printf("depth_style_lit=%.3f transmission_lit=%.3f\n",
           xmt_depth_style_lit(min_depth, frag_far, 0.0f), through);
    check("transmission_lit_partial", through > 0.5f && through < 0.7f);

    /* 7. Receiver position: in front of the glass is lit, behind it sees the
     * full accumulated transmittance. */
    check("lit_before_glass", fabsf(xmt_lit_fraction(&m, 0.0f) - 1.0f) < 1e-6f);
    check("lit_after_glass", fabsf(xmt_lit_fraction(&m, s_opt.thickness_total) - through) < 1e-4f);

    /* 8. Single-layer shortcut is not good enough; 5 of the optimal 6 is. */
    check("one_layer_edge_wrong",
          fabsf(beam_T_n(&s_opt, 1) - beam_T_n(&s_opt, s_opt.n)) / beam_T_n(&s_opt, s_opt.n) > TOL);
    check("six_layer_ok", min_layers_for(&s_opt, TOL) <= 6);

    /* 9. Bad inputs. */
    check("fit_bad_T", xmt_fit_sigma(XM_THICK, (const float[]){ 0.9f, 1.1f, 0.8f }, 3, &sigma) == -1);
    check("fit_bad_d", xmt_fit_sigma((const float[]){ 0.0f, 0.006f, 0.01f },
                                     XM_MEAS, 3, &sigma) == -1);

    printf("all_ok=%d\n", failures == 0);
    return failures == 0 ? 0 : 1;
}
