
/* curvature: shadow-step (staircase) width vs surface curvature.
 *
 * Scene (2D cross-section, extruded): a slope plane and a cylinder pillar.
 * Directional light, 1D shadow map along the light-screen axis u.
 *   - uniform:  N texels evenly spaced in u
 *   - adaptive: texel size follows the half-pixel rule t(u) <= (p/2) cos a / cos b
 *
 * Modes:
 *   ./curvature                      self-test (key=value checks)
 *   ./curvature run <R> <uniform|adaptive> <N> [out.pgm]
 *   ./curvature sweep <R>            CSV: max step width vs total samples
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define IMG 400
#define WORLD 20.0
#define PIX (WORLD / IMG)              /* screen pixel size, world units */
#define LAMBDA (35.0 * M_PI / 180.0)   /* light tilt from vertical */
#define SLOPE_M (-0.35)
#define SLOPE_B (-3.0)
#define CYL_CX 2.0
#define AMAX (85.0 * M_PI / 180.0)     /* clean-zone limit |alpha| <= AMAX */
#define UMIN (-14.0)
#define UMAX 14.0
#define RHO_BG 1.0
#define GRID 28000

static double CL, SL;          /* cos/sin of light tilt */
static double NSX, NSY;        /* slope unit normal */
static double COS_AS, COS_BS;  /* slope: cos(alpha) vs light, cos(beta) vs screen x */
static double US0, US1;        /* slope u-range */

static void scene_init(void) {
    CL = cos(LAMBDA); SL = sin(LAMBDA);
    double nrm = sqrt(1.0 + SLOPE_M * SLOPE_M);
    NSX = -SLOPE_M / nrm; NSY = 1.0 / nrm;
    COS_AS = NSX * (-SL) + NSY * CL;   /* n . light_dir */
    COS_BS = 1.0 / nrm;                /* tangent . screen x */
    double k = CL + SLOPE_M * SL;
    US0 = -10.0 * k + SLOPE_B * SL;
    US1 =  10.0 * k + SLOPE_B * SL;
}

/* cylinder centre rests just above the slope; radius R */
static double cyl_cy(double R) { return R - 3.1; }
static double cyl_cu(double R) { return CYL_CX * CL + cyl_cy(R) * SL; }  /* u of centre */
static double cyl_k (double R) { return -CYL_CX * SL + cyl_cy(R) * CL; } /* depth offset */

/* half-pixel rule: required u-space density rho = (2/p) * cos(beta) / cos(alpha) */
static double rho_cyl(double u, double R) {
    double A = (u - cyl_cu(R)) / R;
    if (A < -1.0 || A > 1.0) return 0.0;
    double alpha = asin(A);            /* lit face: alpha in [-90, 90] deg */
    double ca = cos(alpha);
    double floor_ca = cos(AMAX);
    if (ca < floor_ca) ca = floor_ca;  /* rim beyond AMAX keeps the edge rate */
    double phi = alpha - LAMBDA;       /* normal angle from +y: alpha = phi + lambda */
    return (2.0 / PIX) * fabs(cos(phi)) / ca;
}

static double rho_slope(double u) {
    if (u < US0 || u > US1) return 0.0;
    return (2.0 / PIX) * COS_BS / COS_AS;
}

static double rho_at(double u, double R) {
    double r = RHO_BG, rc = rho_cyl(u, R), rs = rho_slope(u);
    if (rc > r) r = rc;
    if (rs > r) r = rs;
    return r;
}

/* nearest (max) scene depth along the light at u; depth axis l = (-sin, cos) */
static double nearest_depth(double u, double R) {
    double best = -1e9;
    double A = (u - cyl_cu(R)) / R;
    if (fabs(A) <= 1.0) {
        double d = cyl_k(R) + R * sqrt(1.0 - A * A);   /* near cap of the circle */
        if (d > best) best = d;
    }
    double k = CL + SLOPE_M * SL;
    double x = (u - SLOPE_B * SL) / k;
    if (x >= -10.0 && x <= 10.0) {
        double y = SLOPE_M * x + SLOPE_B;
        double d = -x * SL + y * CL;
        if (d > best) best = d;
    }
    return best;
}

typedef struct {
    int n, adaptive;
    double R;
    double *b;      /* n+1 texel boundaries in u */
    double *depth;  /* n texels: max scene depth in texel */
} Map;

static void map_build(Map *m, double R, int adaptive, int N) {
    m->n = N; m->adaptive = adaptive; m->R = R;
    m->b = malloc((size_t)(N + 1) * sizeof(double));
    m->depth = malloc((size_t)N * sizeof(double));
    if (!adaptive) {
        for (int i = 0; i <= N; i++) m->b[i] = UMIN + (UMAX - UMIN) * i / N;
    } else {
        static double mass[GRID];
        double du = (UMAX - UMIN) / GRID, total = 0.0;
        for (int j = 0; j < GRID; j++) {
            mass[j] = rho_at(UMIN + (j + 0.5) * du, R) * du;
            total += mass[j];
        }
        double target = total / N, acc = 0.0;
        int j = 0;
        m->b[0] = UMIN;
        for (int i = 1; i < N && j < GRID; i++) {
            double need = target;
            while (need > 0.0 && j < GRID) {
                double avail = mass[j] - acc;
                if (avail >= need) { acc += need; need = 0.0; }
                else { need -= avail; j++; acc = 0.0; }
            }
            double frac = (j < GRID && mass[j] > 0.0) ? acc / mass[j] : 0.0;
            m->b[i] = UMIN + (j + frac) * du;
        }
        for (int i = 1; i <= N; i++) if (m->b[i] == 0.0) m->b[i] = m->b[i - 1];
        m->b[N] = UMAX;
    }
    for (int i = 0; i < N; i++) {
        double t = m->b[i + 1] - m->b[i], best = -1e9;
        for (int k = 0; k < 33; k++) {
            double d = nearest_depth(m->b[i] + t * (k + 0.5) / 33.0, R);
            if (d > best) best = d;
        }
        m->depth[i] = best;
    }
}

static void map_free(Map *m) { free(m->b); free(m->depth); }

static int map_find(const Map *m, double u) {
    if (u <= m->b[0]) return 0;
    if (u >= m->b[m->n]) return m->n - 1;
    int lo = 0, hi = m->n - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (m->b[mid] <= u) lo = mid; else hi = mid - 1;
    }
    return lo;
}

typedef struct {
    double cyl_w_px;     /* widest counted step on the cylinder, screen px */
    double cyl_dtheta;   /* normal turn across that texel, rad */
    double cyl_alpha;    /* tilt at that texel centre, rad */
    double cyl_phi;      /* normal angle from +y at that texel centre, rad */
    double cyl_t;        /* that texel's width in u */
    double cyl_wx, cyl_wy;       /* world coords of widest step */
    double cyl_pxx, cyl_pyy;     /* screen pixel coords of widest step */
    double slope_w_px;   /* widest step on the slope, screen px */
    double max_w_px;
    /* reference texel: the one containing alpha = AREF on the cylinder */
    double ref_w_px, ref_dtheta, ref_alpha;
} Meas;

#define AREF (80.0 * M_PI / 180.0)

static double clamp1(double v) { return v < -1.0 ? -1.0 : (v > 1.0 ? 1.0 : v); }

/* exact geometric measurement: project each texel's surface footprint to screen */
static void measure(const Map *m, double R, Meas *out) {
    double cu = cyl_cu(R);
    memset(out, 0, sizeof *out);
    for (int i = 0; i < m->n; i++) {
        double u0 = m->b[i], u1 = m->b[i + 1], t = u1 - u0;
        double ua = u0 > cu - R ? u0 : cu - R;
        double ub = u1 < cu + R ? u1 : cu + R;
        if (ua < ub) {
            double a1 = asin(clamp1((ua - cu) / R));
            double a2 = asin(clamp1((ub - cu) / R));
            double ac = 0.5 * (a1 + a2);
            if (fabs(ac) <= AMAX) {              /* only the clean zone counts */
                double p1 = a1 - LAMBDA, p2 = a2 - LAMBDA;
                double wpx = R * fabs(sin(p2) - sin(p1)) / PIX;
                if (wpx > out->cyl_w_px) {
                    double pm = 0.5 * (p1 + p2);
                    out->cyl_w_px = wpx;
                    out->cyl_dtheta = a2 - a1;
                    out->cyl_alpha = ac;
                    out->cyl_phi = pm;
                    out->cyl_t = t;
                    out->cyl_wx = CYL_CX + R * sin(pm);
                    out->cyl_wy = cyl_cy(R) + R * cos(pm);
                    out->cyl_pxx = (out->cyl_wx + 10.0) / PIX;
                    out->cyl_pyy = (10.0 - out->cyl_wy) / PIX;
                }
            }
        }
        double sa = u0 > US0 ? u0 : US0, sb = u1 < US1 ? u1 : US1;
        if (sa < sb) {
            double wpx = (sb - sa) * COS_BS / COS_AS / PIX;  /* overlap, not whole texel */
            if (wpx > out->slope_w_px) out->slope_w_px = wpx;
        }
    }
    out->max_w_px = out->cyl_w_px > out->slope_w_px ? out->cyl_w_px : out->slope_w_px;

    /* reference texel at fixed tilt alpha = AREF (R-independent comparison point) */
    double uref = cu + R * sin(AREF);
    int ir = map_find(m, uref);
    double rua = m->b[ir] > cu - R ? m->b[ir] : cu - R;
    double rub = m->b[ir + 1] < cu + R ? m->b[ir + 1] : cu + R;
    double ra = asin(clamp1((rua - cu) / R));
    double rb = asin(clamp1((rub - cu) / R));
    double rp1 = ra - LAMBDA, rp2 = rb - LAMBDA;
    out->ref_w_px = R * fabs(sin(rp2) - sin(rp1)) / PIX;
    out->ref_dtheta = rb - ra;
    out->ref_alpha = 0.5 * (ra + rb);
}

static void render(const Map *m, double R, const Meas *ms, const char *path) {
    static unsigned char img[IMG * IMG];
    double cy = cyl_cy(R);
    for (int py = 0; py < IMG; py++) {
        double y = 10.0 - (py + 0.5) * PIX;
        for (int px = 0; px < IMG; px++) {
            double x = -10.0 + (px + 0.5) * PIX;
            double yvis = SLOPE_M * x + SLOPE_B;
            int on_cyl = 0;
            double dx = x - CYL_CX;
            if (fabs(dx) <= R) {
                double yc = cy + sqrt(R * R - dx * dx);
                if (yc > yvis) { yvis = yc; on_cyl = 1; }
            }
            unsigned char v;
            if (y > yvis) { v = 235; }
            else {
                double nx, ny;
                if (on_cyl) { nx = dx / R; ny = sqrt(1.0 - nx * nx); }
                else { nx = NSX; ny = NSY; }
                double ndotl = nx * (-SL) + ny * CL;
                if (ndotl <= 1e-3) { v = 30; }
                else {
                    double u = x * CL + yvis * SL;
                    double d = -x * SL + yvis * CL;
                    double ta = sqrt(1.0 - ndotl * ndotl) / ndotl;
                    double bias = 0.005 + 0.02 * ta;   /* slope-scaled, as shadowmap lab */
                    int lit = (m->depth[map_find(m, u)] - d) <= bias;
                    if (lit) { int s = 80 + (int)(140 * ndotl); v = s > 220 ? 220 : (unsigned char)s; }
                    else v = 40;
                }
            }
            img[py * IMG + px] = v;
        }
    }
    int mx = (int)(ms->cyl_pxx + 0.5), my = (int)(ms->cyl_pyy + 0.5);
    for (int a = -2; a <= 2; a++)
        for (int b = -2; b <= 2; b++) {
            int xx = mx + a, yy = my + b;
            if (xx >= 0 && xx < IMG && yy >= 0 && yy < IMG) img[yy * IMG + xx] = 255;
        }
    FILE *f = fopen(path, "wb");
    if (!f) { perror("pgm"); return; }
    fprintf(f, "P5\n%d %d\n255\n", IMG, IMG);
    fwrite(img, 1, IMG * IMG, f);
    fclose(f);
}

static void print_meas(const Map *m, double R, const Meas *s) {
    double limit = 0.5;
    printf("R=%.4g\n", R);
    printf("mode=%s\n", m->adaptive ? "adaptive" : "uniform");
    printf("N=%d\n", m->n);
    printf("kappa=%.6f\n", 1.0 / R);
    printf("cyl_max_step_px=%.4f\n", s->cyl_w_px);
    printf("cyl_max_step_alpha_deg=%.3f\n", s->cyl_alpha * 180.0 / M_PI);
    printf("cyl_max_step_dtheta_deg=%.4f\n", s->cyl_dtheta * 180.0 / M_PI);
    printf("cyl_max_step_dtheta_formula_deg=%.4f\n",
           (1.0 / R) * s->cyl_t / cos(s->cyl_alpha) * 180.0 / M_PI);
    printf("cyl_max_step_texel_u=%.6f\n", s->cyl_t);
    printf("cyl_max_step_world_x=%.4f\n", s->cyl_wx);
    printf("cyl_max_step_world_y=%.4f\n", s->cyl_wy);
    printf("cyl_max_step_pixel_x=%.2f\n", s->cyl_pxx);
    printf("cyl_max_step_pixel_y=%.2f\n", s->cyl_pyy);
    printf("slope_max_step_px=%.4f\n", s->slope_w_px);
    printf("ref_alpha_deg=%.3f\n", s->ref_alpha * 180.0 / M_PI);
    printf("ref_step_px=%.4f\n", s->ref_w_px);
    printf("ref_dtheta_deg=%.4f\n", s->ref_dtheta * 180.0 / M_PI);
    printf("max_step_px=%.4f\n", s->max_w_px);
    printf("limit_px=%.4f\n", limit);
    printf("pass=%d\n", s->max_w_px <= limit);
}

static int run_mode(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: curvature run <R> <uniform|adaptive> <N> [out.pgm]\n"); return 2; }
    double R = atof(argv[2]);
    int adaptive = strcmp(argv[3], "adaptive") == 0;
    int N = atoi(argv[4]);
    Map m; Meas s;
    map_build(&m, R, adaptive, N);
    measure(&m, R, &s);
    print_meas(&m, R, &s);
    if (argc > 5 && strcmp(argv[5], "-") != 0) render(&m, R, &s, argv[5]);
    map_free(&m);
    return 0;
}

static int sweep_mode(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: curvature sweep <R>\n"); return 2; }
    double R = atof(argv[2]);
    static const int NS[] = { 256, 512, 1024, 2048, 4096, 8192, 16384 };
    printf("R,mode,N,cyl_max_step_px,slope_max_step_px,max_step_px,pass\n");
    for (int ad = 0; ad <= 1; ad++)
        for (size_t q = 0; q < sizeof NS / sizeof NS[0]; q++) {
            Map m; Meas s;
            map_build(&m, R, ad, NS[q]);
            measure(&m, R, &s);
            printf("%.4g,%s,%d,%.4f,%.4f,%.4f,%d\n",
                   R, ad ? "adaptive" : "uniform", NS[q],
                   s.cyl_w_px, s.slope_w_px, s.max_w_px, s.max_w_px <= 0.5);
            map_free(&m);
        }
    return 0;
}

static int failures = 0;
static void check(const char *name, int ok) {
    printf("%s=%d\n", name, ok ? 1 : 0);
    if (!ok) failures++;
}

static int selftest(void) {
    /* |dn/ds| = kappa = 1/R on a circle */
    double R = 3.0, s0 = 1.0, e = 1e-4;
    double nx0 = sin((s0 - e) / R), ny0 = cos((s0 - e) / R);
    double nx1 = sin((s0 + e) / R), ny1 = cos((s0 + e) / R);
    double rate = hypot(nx1 - nx0, ny1 - ny0) / (2 * e);
    check("normal_rate_is_curvature", fabs(rate - 1.0 / R) < 1e-3 / R);

    Map m; Meas s;
    map_build(&m, 3.0, 0, 2048);
    measure(&m, 3.0, &s);
    /* dtheta = kappa * t * sec(alpha) at the widest step */
    double formula = (1.0 / 3.0) * s.cyl_t / cos(s.cyl_alpha);
    check("dtheta_matches_kappa_t_secalpha", fabs(s.cyl_dtheta - formula) / formula < 0.05);
    /* w = t * sec(alpha) * cos(beta) at the widest step */
    double w_formula = s.cyl_t * fabs(cos(s.cyl_phi)) / cos(s.cyl_alpha);
    check("width_matches_t_secalpha_cosbeta",
          fabs(s.cyl_w_px * PIX - w_formula) / w_formula < 0.10);
    check("uniform_fails_half_pixel", s.max_w_px > 0.5);
    check("slope_ok_while_cylinder_fails", s.slope_w_px <= 0.5 && s.cyl_w_px > 0.5);
    map_free(&m);

    /* at fixed tilt alpha: uniform step width is curvature-independent (kappa
     * cancels in w), while the normal turn dtheta = kappa*t*sec(alpha) scales
     * with kappa -- compare the reference texel at alpha = 80 deg */
    Map m1, m6; Meas s1, s6;
    map_build(&m1, 1.5, 0, 2048); measure(&m1, 1.5, &s1);
    map_build(&m6, 6.0, 0, 2048); measure(&m6, 6.0, &s6);
    check("uniform_width_r_independent", fabs(s1.ref_w_px - s6.ref_w_px) / s6.ref_w_px < 0.05);
    check("dtheta_scales_with_kappa",
          fabs(s1.ref_dtheta / s6.ref_dtheta - 4.0) / 4.0 < 0.05);
    map_free(&m1); map_free(&m6);

    /* adaptive: half-pixel rule met at the same budget */
    Map ma; Meas sa;
    map_build(&ma, 3.0, 1, 2048);
    measure(&ma, 3.0, &sa);
    check("adaptive_meets_half_pixel", sa.max_w_px <= 0.5);
    int mono = 1;
    for (int i = 0; i < ma.n; i++) if (ma.b[i + 1] <= ma.b[i]) mono = 0;
    check("adaptive_boundaries_monotone", mono);
    map_free(&ma);

    printf("all_ok=%d\n", failures == 0);
    return failures == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    scene_init();
    if (argc >= 2 && strcmp(argv[1], "run") == 0) return run_mode(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "sweep") == 0) return sweep_mode(argc, argv);
    return selftest();
}
