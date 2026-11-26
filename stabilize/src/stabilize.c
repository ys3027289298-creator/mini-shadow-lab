
/* stabilize: shadow projection matrix update criteria and texel-phase stability.
 *
 * The shadow map is an orthographic projection mounted in light space. A world
 * point p maps to a shadow texel as
 *   texel(p) = (p - o) / w            (scalar 1D model along one light axis)
 * where o is the frustum origin in light space and w the world size of a texel.
 * The fractional part frac(texel(p)) is the texel phase: if it moves frame to
 * frame, the shadow edge re-quantizes on different texels and the shadow
 * shimmers even though geometry and light did not change.
 *
 * Numerics use double on purpose: float32 accumulation (~1e-5 texel over one
 * second) is large enough to mask the exact zero the theory predicts.
 */
#include <stdio.h>
#include <math.h>

#define FPS      60.0
#define FRAMES   60                    /* 1 second of motion */
#define W        0.1                   /* texel size in world units */
#define PROBE_X  50.0                  /* fixed world point whose phase we track */
#define FRUS_MIN (-50.0)               /* light-anchored frustum extent, must    */
#define FRUS_MAX (100.0)               /* contain every camera pos and the probe */

typedef enum { CAM_RAW = 0, LIGHT_SNAP = 1, LIGHT_FIXED = 2 } Strategy;

static const char* sname(Strategy s) {
    switch (s) {
        case CAM_RAW:    return "cam_raw";    /* frustum follows camera, no snapping */
        case LIGHT_SNAP: return "light_snap"; /* light-space origin snapped to grid */
        case LIGHT_FIXED:return "light_fixed";/* frustum anchored once, never moves  */
        default:         return "?";
    }
}

static double wrap01(double v) { return v - floor(v); }

/* frustum origin (light space) produced by the given strategy at a frame */
static double origin_of(Strategy s, double cam) {
    switch (s) {
        case CAM_RAW:     return cam;                     /* camera-anchored */
        case LIGHT_SNAP:  return floor(cam / W) * W;      /* anchored in light space */
        case LIGHT_FIXED: return FRUS_MIN;                /* anchored once */
    }
    return 0.0;
}

/* run one strategy at one speed (world units / second, 0 = standing still).
 * rebuilds counts frames whose matrix differs from the previous frame;
 * *jitter is the largest frame-to-frame texel-phase jump of the fixed probe. */
static void run(Strategy s, double speed, int* rebuilds, double* jitter) {
    double prev_origin = origin_of(s, 0.0);
    double prev_phase  = wrap01((PROBE_X - prev_origin) / W);
    int reb = 0;
    double jmax = 0.0;
    for (int f = 1; f <= FRAMES; f++) {
        double cam = speed * (double)f / FPS;
        double o = origin_of(s, cam);

        if (fabs(o - prev_origin) > 1e-12) reb++;         /* matrix update */

        double phase = wrap01((PROBE_X - o) / W);
        double d = wrap01(phase - prev_phase);            /* circular distance */
        if (d > 0.5) d = 1.0 - d;
        if (d > jmax) jmax = d;

        prev_origin = o;
        prev_phase = phase;
    }
    *rebuilds = reb;
    *jitter = jmax;
}

static int failures = 0;
static void check(const char* name, int ok) {
    printf("%s=%d\n", name, ok ? 1 : 0);
    if (!ok) failures++;
}

int main(void) {
    const double speeds[3] = { 0.0, 2.0, 7.0 };          /* stand / walk / run */
    const char* tag[3]      = { "stand", "walk", "run" };

    /* all camera positions and the probe must stay inside the fixed frustum */
    int contained = 1;
    for (int f = 0; f <= FRAMES && contained; f++) {
        double cam = speeds[2] * (double)f / FPS;
        contained = cam >= FRUS_MIN && cam <= FRUS_MAX &&
                    PROBE_X >= FRUS_MIN && PROBE_X <= FRUS_MAX;
    }
    check("fixed_frustum_contains_scene", contained);

    /* update criterion: with light and scene bounds fixed, a correctly mounted
     * matrix must not change. LIGHT_FIXED rebuilds zero times at every speed. */
    for (int i = 0; i < 3; i++) {
        int reb; double j;
        run(LIGHT_FIXED, speeds[i], &reb, &j);
        printf("light_fixed_%s_rebuilds=%d\n", tag[i], reb);
        check("light_fixed_matrix_invariant", reb == 0);
    }

    /* three speeds x three strategies: the requested table */
    for (int i = 0; i < 3; i++) {
        for (int st = 0; st < 3; st++) {
            int reb; double j;
            run((Strategy)st, speeds[i], &reb, &j);
            printf("%s_%s_rebuilds=%d\n", sname((Strategy)st), tag[i], reb);
            printf("%s_%s_phase_jitter_texels=%.6f\n",
                   sname((Strategy)st), tag[i], j);
        }
    }

    /* acceptance: phase jump must be zero for every speed when the matrix is
     * anchored in light space (snapped or fully fixed) */
    for (int i = 0; i < 3; i++) {
        int r1, r2; double j1, j2;
        run(LIGHT_SNAP, speeds[i], &r1, &j1);
        run(LIGHT_FIXED, speeds[i], &r2, &j2);
        check("light_snap_zero_jitter", j1 < 1e-9);
        check("light_fixed_zero_jitter", j2 < 1e-9);
    }

    /* the failing case is reproduced exactly: camera-anchored, moving, jitter
     * equals frac(dx/w) and the matrix is rebuilt every single frame */
    int rw, rr, rs; double jw, jr, js;
    run(CAM_RAW, speeds[1], &rw, &jw);
    run(CAM_RAW, speeds[2], &rr, &jr);
    double expect_w = wrap01((speeds[1] / FPS) / W);     /* 2/60 / 0.1 */
    double expect_r = wrap01((speeds[2] / FPS) / W);     /* 7/60 / 0.1 */
    check("cam_raw_rebuilds_every_frame", rw == FRAMES && rr == FRAMES);
    check("cam_raw_walk_phase_matches_formula", fabs(jw - expect_w) < 1e-12);
    check("cam_raw_run_phase_matches_formula", fabs(jr - expect_r) < 1e-12);
    run(CAM_RAW, 0.0, &rs, &js);
    check("cam_raw_stand_is_stable", js < 1e-12);

    /* snapping rebuilds only on frames where the camera crosses a texel-grid
     * boundary. Cells crossed per second is v/w; at most one rebuild can be
     * counted per frame, so rebuilds = min(fps, v/w): walk -> 20, run -> 60
     * (run crosses 70 cells but two cells are consumed inside single frames). */
    int rwalk, rrun; double jtmp;
    run(LIGHT_SNAP, speeds[1], &rwalk, &jtmp);
    run(LIGHT_SNAP, speeds[2], &rrun, &jtmp);
    double cells_walk = speeds[1] / W;
    double cells_run  = speeds[2] / W;
    printf("light_snap_walk_cells_crossed=%d\n", (int)cells_walk);
    printf("light_snap_run_cells_crossed=%d\n", (int)cells_run);
    check("snap_walk_rebuilds_min_fps_cells",
          rwalk == (int)fmin(FPS, cells_walk));
    check("snap_run_rebuilds_min_fps_cells",
          rrun == (int)fmin(FPS, cells_run));

    printf("all_ok=%d\n", failures == 0);
    return failures == 0 ? 0 : 1;
}
