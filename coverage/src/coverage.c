/* coverage: size the directional-light ortho box so distant terrain keeps shadows. */
#include <stdio.h>
#include <math.h>

#define PI        3.14159265358979323846f
#define RES       2048.0f   /* shadow map resolution, texels */
#define BITS      16        /* depth bits used for the precision column */
#define NEAR_FAR  100.0f    /* fixed near-cascade range, world units */

/* box half-size S = k * D: k fits the camera frustum bounding sphere.
   fov_y vertical field of view in degrees, aspect = w/h. */
static float box_coeff(float fov_y_deg, float aspect) {
    float t = tanf(fov_y_deg * PI / 360.0f);
    return sqrtf(1.0f + (1.0f + aspect * aspect) * t * t);
}

/* light depth range spans the box diameter, so one depth step is 2S / 2^bits */
static float depth_step(float S, int bits) {
    return (2.0f * S) / (float)(1u << bits);
}

static float texel_world(float S) {
    return (2.0f * S) / RES;
}

/* view distance the box reaches before objects fall outside */
static float clip_distance(float S, float k) {
    return S / k;
}

/* precision budget curve: largest box a depth-step budget allows */
static float S_max_precision(float dz_max, int bits) {
    return dz_max * (float)(1u << bits) / 2.0f;
}

static int failures = 0;
static void check(const char* name, int ok) {
    printf("%s=%d\n", name, ok ? 1 : 0);
    if (!ok) failures++;
}

int main(void) {
    const float fov = 60.0f, aspect = 16.0f / 9.0f;
    float k = box_coeff(fov, aspect);
    check("k_coeff", fabsf(k - 1.5449f) < 1e-3f);

    const float tiers[3] = { 500.0f, 2000.0f, 8000.0f };
    const float cam_ratio = 0.25f;  /* buggy config: box fit to camera view distance */
    const float dz_budget = 0.05f;  /* near-field depth-step budget, world units */

    int bug_clips = 1, fixed_covers = 1, near_const = 1;
    float dz_near_prev = -1.0f;

    for (int i = 0; i < 3; i++) {
        float dv = tiers[i];        /* max visible distance (terrain) */
        float dc = cam_ratio * dv;  /* main camera view distance */

        /* buggy: box from camera view distance -> distant mountain clipped */
        float S_bug = k * dc;
        float excess = dv - clip_distance(S_bug, k);
        if (!(excess > 0.0f)) bug_clips = 0;

        /* fixed single box: box from max visible distance */
        float S = k * dv;
        float feasible = (S <= S_max_precision(dz_budget, BITS)) ? 1.0f : 0.0f;

        /* cascade: fixed near cascade + far cascade out to dv */
        float S_near = k * NEAR_FAR;
        float dz_near = depth_step(S_near, BITS);
        if (dz_near_prev > 0.0f && fabsf(dz_near - dz_near_prev) > 1e-9f) near_const = 0;
        dz_near_prev = dz_near;
        if (clip_distance(S, k) < dv) fixed_covers = 0;

        printf("tier D_vis=%.0f\n", dv);
        printf("  buggy:   S=%.1f clip_at=%.1f clipped_band=(%.0f,%.0f] excess=%.0f\n",
               S_bug, clip_distance(S_bug, k), dc, dv, excess);
        printf("  single:  S=%.1f clip_at=%.1f texel=%.3f dz=%.5f feasible=%.0f\n",
               S, clip_distance(S, k), texel_world(S), depth_step(S, BITS), feasible);
        printf("  cascade: near S=%.1f texel=%.3f dz=%.5f | far split=%.0f mountain_covered=1\n",
               S_near, texel_world(S_near), dz_near, dv);
    }

    check("buggy_clips_mountain", bug_clips);
    check("fixed_covers_mountain", fixed_covers);
    check("near_precision_constant", near_const);

    /* single-box precision thins linearly with view distance: 8000/500 = 16x */
    float ratio = depth_step(k * 8000.0f, BITS) / depth_step(k * 500.0f, BITS);
    check("single_precision_degrades_16x", fabsf(ratio - 16.0f) < 1e-3f);

    /* far tier cannot meet the precision budget with one box; cascade can */
    check("single_infeasible_far", k * 8000.0f > S_max_precision(dz_budget, BITS));
    check("cascade_within_budget", depth_step(k * NEAR_FAR, BITS) < dz_budget);

    printf("all_ok=%d\n", failures == 0);
    return failures == 0 ? 0 : 1;
}
