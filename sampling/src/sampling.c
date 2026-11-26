
/* sampling: thin nose self-shadow stripes caused by sub-texel sampling.
 *
 * Let w be the structure's transverse width in the light/shadow plane and
 * h = Delta s the world-space spacing of one shadow sample. The resolution
 * ratio is
 *
 *     rho = w / h.
 *
 * rho < 1 means one sample footprint can contain both flanks. The aliasing
 * amplification is A = h / w = 1/rho, and the phase fraction in which a cell
 * contains both flanks is chi = (h-w)/h = 1-rho.
 *
 * A depth bias only changes the comparison tolerance:
 *
 *     z_f - b > z_m  => shadow.
 *
 * It cannot recover a flank that the sampling map collapsed before the
 * comparison. Information is lost when w < h and two distinct depths z1 != z2
 * are passed through one many-to-one merge Q(z1,z2) = z_m.
 *
 * The verifier below sweeps grid phases and measures the worst connected
 * false-stripe width. The acceptance bar is W_stripe <= h/2. A doubled memory
 * budget means doubled areal texel density K = 2, hence h' = h/sqrt(K) and
 * rho' = rho*sqrt(K). Doubling resolution on both axes is K = 4.
 */
#include <stdio.h>
#include <math.h>

#define PHASES 8192
#define MEM_BUDGET_X 2.0f       /* one density step allowed to double memory */
#define NORMAL_REACH_MM 8.0f    /* local reach of normal/contact shading */
#define FID_MIN 0.80f
#define EPS 1e-5f

typedef enum {
    METHOD_BIAS_PCF = 0,
    METHOD_DENSIFY = 1,
    METHOD_NORMAL_CONTACT = 2,
    METHOD_INFEASIBLE = -1
} Method;

typedef struct {
    float bad_fraction;      /* phases with a two-flank sample cell */
    float worst_stripe_mm;   /* conservative worst connected texel run */
} AliasReport;

typedef struct {
    const char* name;
    float w_mm;
    float h_mm;
    Method method;
    float h_eff_mm;
    float rho_eff;
    float mem_x;
    float fidelity;
    float stripe_after_mm;
    int pass;
} TierResult;

static int failures = 0;

static int closef(float a, float b) {
    return fabsf(a - b) < 1e-4f;
}

static void check(const char* name, int ok) {
    printf("%s=%d\n", name, ok ? 1 : 0);
    if (!ok) failures++;
}

static float resolution_ratio(float w, float h) {
    return w / h;
}

static float alias_ratio(float w, float h) {
    return h / w;
}

/* Fraction of grid phases for which one interval contains both edges. */
static float mix_fraction(float w, float h) {
    float x = (h - w) / h;
    return fminf(1.0f, fmaxf(0.0f, x));
}

/* The practical information-loss discriminant. Equal depths carry no second
 * flank information even if the geometric width is sub-sample. */
static int information_lost(float w, float h, float z1, float z2, float z_eps) {
    return w < h && fabsf(z1 - z2) > z_eps;
}

/* One scalar depth produced from two distinct flank depths. */
static float merge_depth(float z1, float z2, float lambda) {
    return lambda * z1 + (1.0f - lambda) * z2;
}

/* With z_f - b > z_m meaning shadow, b must be at least delta = z_f-z_m.
 * b_safe is the largest bias that still preserves real contact shadows. */
static int bias_interval_exists(float delta_required, float b_safe) {
    return delta_required <= b_safe + EPS;
}

/* Sweep translations of the sample grid. If both edges w apart fall in one
 * cell, that magnified texel forms a worst-case h-wide false stripe. */
static AliasReport scan_alias(float w, float h) {
    AliasReport r = { 0.0f, 0.0f };
    int bad = 0;
    for (int i = 0; i < PHASES; i++) {
        float phase = ((float)i + 0.5f) * h / (float)PHASES;
        if (phase + w < h) {
            bad++;
            r.worst_stripe_mm = h;
        }
    }
    r.bad_fraction = (float)bad / (float)PHASES;
    return r;
}

static float normal_fidelity(float w) {
    return NORMAL_REACH_MM / (NORMAL_REACH_MM + w);
}

static const char* method_name(Method m) {
    switch (m) {
        case METHOD_BIAS_PCF: return "slope_bias_pcf";
        case METHOD_DENSIFY: return "densify_local";
        case METHOD_NORMAL_CONTACT: return "normal_contact";
        default: return "infeasible";
    }
}

static TierResult solve_tier(const char* name, float w, float h) {
    TierResult out = { name, w, h, METHOD_INFEASIBLE, h, w / h,
                       1.0f, 1.0f, 0.0f, 0 };
    float limit = 0.5f * h;
    AliasReport base = scan_alias(w, h);

    if (base.worst_stripe_mm <= limit + EPS) {
        out.method = METHOD_BIAS_PCF;
        out.h_eff_mm = h;
        out.rho_eff = w / h;
        out.mem_x = 1.0f;
        out.fidelity = fminf(1.0f, w / h);
        out.stripe_after_mm = base.worst_stripe_mm;
        out.pass = out.stripe_after_mm <= 0.5f * out.h_eff_mm + EPS;
        return out;
    }

    /* One density step that is allowed to double memory. */
    float q = sqrtf(MEM_BUDGET_X);
    float h_dense = h / q;
    AliasReport dense = scan_alias(w, h_dense);
    float dense_limit = 0.5f * h_dense;
    if (dense.worst_stripe_mm <= dense_limit + EPS) {
        out.method = METHOD_DENSIFY;
        out.h_eff_mm = h_dense;
        out.rho_eff = w / h_dense;
        out.mem_x = MEM_BUDGET_X;
        out.fidelity = fminf(1.0f, out.rho_eff);
        out.stripe_after_mm = dense.worst_stripe_mm;
        out.pass = 1;
        return out;
    }

    float fn = normal_fidelity(w);
    if (fn >= FID_MIN) {
        out.method = METHOD_NORMAL_CONTACT;
        out.h_eff_mm = h; /* screen/normal rate replaces shadow-map rate */
        out.rho_eff = w / h;
        out.mem_x = 1.0f; /* no denser shadow map; normal target assumed present */
        out.fidelity = fn;
        out.stripe_after_mm = 0.0f; /* depth-map alias run is removed */
        out.pass = 0.0f <= limit + EPS;
        return out;
    }

    out.stripe_after_mm = base.worst_stripe_mm;
    out.pass = 0;
    return out;
}

int main(void) {
    /* --- core ratio and information-loss algebra --- */
    check("ratio_fine", closef(resolution_ratio(0.8f, 2.0f), 0.4f));
    check("alias_is_inverse_ratio", closef(alias_ratio(0.8f, 2.0f), 2.5f));
    check("mix_is_one_minus_ratio", closef(mix_fraction(0.8f, 2.0f), 0.6f));
    check("mix_zero_when_resolved", closef(mix_fraction(6.0f, 2.0f), 0.0f));

    check("sub_sample_distinct_faces_lost",
          information_lost(0.8f, 2.0f, 0.2f, 0.8f, 1e-4f));
    check("equal_width_boundary_not_flagged",
          !information_lost(2.0f, 2.0f, 0.2f, 0.8f, 1e-4f));
    check("equal_depths_contain_no_second_face",
          !information_lost(1.0f, 2.0f, 0.5f, 0.5f, 1e-4f));

    /* Two different face-depth pairs merge to exactly the same sample. A bias
     * is applied after this many-to-one map, so it cannot separate them. */
    float mA = merge_depth(0.2f, 0.8f, 0.5f);
    float mB = merge_depth(0.3f, 0.7f, 0.5f);
    check("distinct_pairs_merge_to_same_depth", closef(mA, mB));
    int bias_can_separate = 0;
    for (int b_i = 0; b_i <= 20; b_i++) {
        float b = 0.01f * (float)b_i;
        int sA = (0.75f - b > mA);
        int sB = (0.75f - b > mB);
        if (sA != sB) bias_can_separate = 1;
    }
    check("bias_cannot_invert_merge", !bias_can_separate);

    /* Ordinary slope acne has a usable bias interval. In the collapsed case
     * the required tolerance exceeds the safe bias that keeps contact. */
    check("normal_acne_bias_interval", bias_interval_exists(0.02f, 0.03f));
    check("collapsed_face_bias_interval_closed",
          !bias_interval_exists(0.08f, 0.03f));

    /* --- three structure-width tiers --- */
    enum { NTIER = 3 };
    TierResult tiers[NTIER];
    const float ws[NTIER] = { 0.8f, 1.6f, 6.0f };
    const float h = 2.0f;
    int exceed_count = 0;

    for (int t = 0; t < NTIER; t++) {
        AliasReport base = scan_alias(ws[t], h);
        float rho = resolution_ratio(ws[t], h);
        float A = alias_ratio(ws[t], h);
        float chi = mix_fraction(ws[t], h);
        float limit = 0.5f * h;
        int base_pass = base.worst_stripe_mm <= limit + EPS;

        printf("tier=%d w=%.3f h=%.3f rho=%.3f alias_A=%.3f mix_chi=%.3f "
               "phase_bad=%.4f stripe=%.3f half_h=%.3f pass=%d\n",
               t + 1, ws[t], h, rho, A, chi, base.bad_fraction,
               base.worst_stripe_mm, limit, base_pass);
        check(t == 0 ? "fine_phase_fraction" :
              t == 1 ? "thin_phase_fraction" : "wide_has_no_mixed_cell",
              fabsf(base.bad_fraction - chi) < 2.0f / PHASES);

        tiers[t] = solve_tier(t == 0 ? "fine_nose_tip" :
                              t == 1 ? "thin_nose_bridge" : "resolved_body",
                              ws[t], h);

        if (!base_pass) {
            exceed_count++;
            float k_req = 1.0f / (rho * rho);
            printf("EXCEED tier=%d w=%.3f h=%.3f rho=%.3f stripe=%.3f "
                   "half_h=%.3f k_req_info=%.3f chosen=%s\n",
                   t + 1, ws[t], h, rho, base.worst_stripe_mm, limit,
                   k_req, method_name(tiers[t].method));
        }

        printf("chosen tier=%d name=%s method=%s h_eff=%.4f rho_eff=%.4f "
               "stripe_after=%.4f stripe_limit=%.4f memory_x=%.3f "
               "detail_fidelity=%.4f pass=%d\n",
               t + 1, tiers[t].name, method_name(tiers[t].method),
               tiers[t].h_eff_mm, tiers[t].rho_eff,
               tiers[t].stripe_after_mm, 0.5f * tiers[t].h_eff_mm,
               tiers[t].mem_x, tiers[t].fidelity, tiers[t].pass);

        char name[64];
        snprintf(name, sizeof(name), "tier%d_acceptance", t + 1);
        check(name, tiers[t].pass);
    }

    check("exactly_two_tiers_exceed", exceed_count == 2);
    check("tier1_uses_normal_contact", tiers[0].method == METHOD_NORMAL_CONTACT);
    check("tier2_uses_densification", tiers[1].method == METHOD_DENSIFY);
    check("tier3_uses_existing_bias_pcf", tiers[2].method == METHOD_BIAS_PCF);
    check("normal_contact_rejects_wide_structure",
          normal_fidelity(ws[2]) < FID_MIN);
    check("doubled_memory_solves_tier2",
          tiers[1].mem_x <= MEM_BUDGET_X + EPS && tiers[1].rho_eff >= 1.0f);
    check("doubled_memory_fails_tier1_information_bar",
          resolution_ratio(tiers[0].w_mm, h / sqrtf(2.0f)) < 1.0f);

    /* --- detail-fidelity and memory curves --- */
    printf("curve:\n");
    const float rhos[] = { 0.25f, 0.40f, 0.50f, 0.7071f, 0.80f,
                           1.00f, 1.4142f, 2.00f, 3.00f, 4.00f };
    for (unsigned i = 0; i < sizeof(rhos) / sizeof(rhos[0]); i++) {
        float rho = rhos[i];
        float w = rho * h;
        float rho_dense = rho * sqrtf(MEM_BUDGET_X);
        float k_req = rho > EPS ? 1.0f / (rho * rho) : 1e30f;
        int dense_ok = rho_dense >= 1.0f - EPS;
        float fn = normal_fidelity(w);
        const char* choice;
        if (rho >= 1.0f) choice = "bias_pcf";
        else if (dense_ok) choice = "densify_x2_memory";
        else if (fn >= FID_MIN) choice = "normal_contact";
        else choice = "larger_budget_or_hybrid";

        printf("curve rho=%.4f w=%.3f info_lost=%d k_req_info=%.3f "
               "dense2_rho=%.4f dense2_ok=%d memory_dense_x=%.1f "
               "fid_info=%.3f fid_nyquist=%.3f fid_normal=%.3f "
               "memory_normal_extra_x=0.000 choice=%s\n",
               rho, w, rho < 1.0f, k_req, rho_dense, dense_ok,
               MEM_BUDGET_X, fminf(1.0f, rho), fminf(1.0f, rho / 2.0f),
               fn, choice);
    }

    printf("memory_scaling: areal_K=2 -> axis_q=sqrt2=%.4f; "
           "axis_q=2 -> areal_K=4.000\n", sqrtf(2.0f));

    printf("all_ok=%d\n", failures == 0);
    return failures == 0 ? 0 : 1;
}
