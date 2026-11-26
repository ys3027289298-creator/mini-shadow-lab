/*
 * bench.c - measurement driver for the two-pipeline comparison.
 *
 *   bench compare <elev> <radius> <mapmode> <stratA:args> <stratB:args> [tag]
 *       pixel-exact equivalence numbers + per-stage timings, PGM dumps
 *   bench funnel  ...same args...
 *       probe the four intermediate layers (bias -> tap -> PCF -> pixel)
 *   bench sweep-elev <radius> <mapmode> <k>
 *       frac(>1 LSB) across light elevations, CSV
 *   bench sweep-k <elev> <radius> <mapmode>
 *       controllability curve: time and frac(>1 LSB) over k=0..3, CSV
 *
 * strategy spec: "ref" | "nat" | "fast:<k>"
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "common.h"
#include "bias.h"
#include "pipeline.h"

#define TIME_ITERS 60

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int cmp_double(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return (x > y) - (x < y);
}

static double median(double* v, int n) {
    qsort(v, n, sizeof(double), cmp_double);
    return v[n / 2];
}

static void parse_strat(const char* spec, BiasStrategy* s) {
    if (!strcmp(spec, "ref")) {
        s->kind = STRAT_REF_D; s->nr_iters = 0;
    } else if (!strcmp(spec, "nat")) {
        s->kind = STRAT_NAT_F; s->nr_iters = 0;
    } else if (!strncmp(spec, "fast:", 5)) {
        s->kind = STRAT_FAST_K; s->nr_iters = atoi(spec + 5);
    } else {
        fprintf(stderr, "bad strategy spec %s\n", spec);
        exit(2);
    }
}

static const char* strat_name(const BiasStrategy* s) {
    static char buf[16];
    if (s->kind == STRAT_REF_D) return "ref_d";
    if (s->kind == STRAT_NAT_F) return "nat_f";
    snprintf(buf, sizeof(buf), "fast_k%d", s->nr_iters);
    return buf;
}

/* median per-stage timings in milliseconds for one full frame */
typedef struct { double s1, s2, s3, s4, total; } StageTime;

static StageTime time_pipeline(const Config* cfg, const BiasStrategy* s) {
    StageTime t = {0};
    double m1[TIME_ITERS], m2[TIME_ITERS], m3[TIME_ITERS], m4[TIME_ITERS];
    Pipeline* p = pipeline_alloc(cfg);
    for (int it = 0; it < TIME_ITERS; it++) {
        double a, b;
        a = now_sec(); stage_mapbuild(p); b = now_sec();
        m1[it] = b - a;
        a = now_sec(); stage_bias(p, s); b = now_sec();
        m2[it] = b - a;
        a = now_sec(); stage_pcf(p); b = now_sec();
        m3[it] = b - a;
        a = now_sec(); stage_shade(p); b = now_sec();
        m4[it] = b - a;
    }
    t.s1 = median(m1, TIME_ITERS) * 1e3;
    t.s2 = median(m2, TIME_ITERS) * 1e3;
    t.s3 = median(m3, TIME_ITERS) * 1e3;
    t.s4 = median(m4, TIME_ITERS) * 1e3;
    t.total = t.s1 + t.s2 + t.s3 + t.s4;
    return t;
}

typedef struct {
    long n;
    long exact;          /* identical 8-bit pixels */
    long gt_one_lsb;     /* |a-b| > 1 quantization level */
    long maxdiff;
    double mean_abs;
} PixelStats;

static PixelStats pixel_compare(const uint8_t* a, const uint8_t* b) {
    PixelStats st = {0};
    st.maxdiff = 0;
    double sum = 0;
    for (long i = 0; i < NFRAG; i++) {
        long d = labs((long)a[i] - (long)b[i]);
        if (d == 0) st.exact++;
        if (d > 1) st.gt_one_lsb++;
        if (d > st.maxdiff) st.maxdiff = d;
        sum += (double)d;
        st.n++;
    }
    st.mean_abs = sum / st.n;
    return st;
}

static void make_config(Config* cfg, float elev, int radius, int mapmode) {
    config_default(cfg, elev);
    cfg->pcf_radius = radius;
    cfg->map_mode = mapmode;
}

static int cmd_compare(int argc, char** argv) {
    /* elev radius mapmode stratA stratB [tag] */
    float elev = strtof(argv[2], NULL);
    int radius = atoi(argv[3]), mapmode = atoi(argv[4]);
    BiasStrategy sa, sb;
    parse_strat(argv[5], &sa);
    parse_strat(argv[6], &sb);
    const char* tag = argc > 7 ? argv[7] : "out";

    Config cfg;
    make_config(&cfg, elev, radius, mapmode);

    Pipeline* pa = pipeline_run(&cfg, &sa);
    Pipeline* pb = pipeline_run(&cfg, &sb);

    PixelStats st = pixel_compare(pa->pixel, pb->pixel);
    StageTime ta = time_pipeline(&cfg, &sa);
    StageTime tb = time_pipeline(&cfg, &sb);

    char path[256];
    snprintf(path, sizeof(path), "artifacts/img_%s_%s.pgm", tag, strat_name(&sa));
    write_pgm(path, pa->pixel);
    snprintf(path, sizeof(path), "artifacts/img_%s_%s.pgm", tag, strat_name(&sb));
    write_pgm(path, pb->pixel);

    printf("scene: elev=%.1f deg radius=%d mapmode=%d  A=%s B=%s\n",
           elev, radius, mapmode, strat_name(&sa), strat_name(&sb));
    printf("pixels=%ld exact=%ld (%.4f%%)  gt1LSB=%ld (%.5f%%)  maxdiff=%ld meanabs=%.5f\n",
           st.n, st.exact, 100.0 * st.exact / st.n,
           st.gt_one_lsb, 100.0 * st.gt_one_lsb / st.n,
           st.maxdiff, st.mean_abs);
    printf("time ms:                S1map    S2bias    S3pcf   S4shade    total\n");
    printf("  %-8s        %8.4f %9.4f %9.4f %9.4f %9.4f\n",
           strat_name(&sa), ta.s1, ta.s2, ta.s3, ta.s4, ta.total);
    printf("  %-8s        %8.4f %9.4f %9.4f %9.4f %9.4f\n",
           strat_name(&sb), tb.s1, tb.s2, tb.s3, tb.s4, tb.total);
    printf("delta B-A ms            %8.4f %9.4f %9.4f %9.4f %9.4f\n",
           tb.s1 - ta.s1, tb.s2 - ta.s2, tb.s3 - ta.s3,
           tb.s4 - ta.s4, tb.total - ta.total);
    printf("relative total delta: %+.2f%%  (bias stage delta: %+.2f%%)\n",
           100.0 * (tb.total - ta.total) / ta.total,
           100.0 * (tb.s2 - ta.s2) / ta.s2);
    /* machine-readable tail for the plotting step */
    printf("CSV,%s,%s,%.4f,%.5f,%ld,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
           strat_name(&sa), strat_name(&sb),
           100.0 * st.exact / st.n, 100.0 * st.gt_one_lsb / st.n, st.maxdiff,
           ta.s1, ta.s2, ta.s3, ta.s4, ta.total,
           tb.s1, tb.s2, tb.s3, tb.s4, tb.total);
    return 0;
}

static int cmd_funnel(int argc, char** argv) {
    (void)argc;
    float elev = strtof(argv[2], NULL);
    int radius = atoi(argv[3]), mapmode = atoi(argv[4]);
    BiasStrategy sa, sb;
    parse_strat(argv[5], &sa);
    parse_strat(argv[6], &sb);

    Config cfg;
    make_config(&cfg, elev, 0, mapmode);   /* r=0 exposes raw tap decisions */

    Pipeline* pa = pipeline_run(&cfg, &sa);
    Pipeline* pb = pipeline_run(&cfg, &sb);

    long bias_diff = 0, tap_raw_diff = 0, tap_biased_diff = 0;
    double bias_maxrel = 0, bias_maxabs = 0;
    for (long i = 0; i < NFRAG; i++) {
        double db = fabs((double)pa->bias[i] - (double)pb->bias[i]);
        double rel = db / (double)pa->bias[i];
        if (db > 0) bias_diff++;
        if (db > bias_maxabs) bias_maxabs = db;
        if (rel > bias_maxrel) bias_maxrel = rel;
        if (pa->lit_raw[i] != pb->lit_raw[i]) tap_raw_diff++;
        if (pa->lit_biased[i] != pb->lit_biased[i]) tap_biased_diff++;
    }

    /* rerun at the real radius for the PCF/pixel layer counts */
    Config cfg2 = cfg;
    cfg2.pcf_radius = radius;
    Pipeline* qa = pipeline_run(&cfg2, &sa);
    Pipeline* qb = pipeline_run(&cfg2, &sb);
    long cov_diff = 0, cov_gt1 = 0, pix_diff = 0, pix_gt1 = 0;
    for (long i = 0; i < NFRAG; i++) {
        long dc = labs((long)qa->coverage[i] - (long)qb->coverage[i]);
        long dp = labs((long)qa->pixel[i] - (long)qb->pixel[i]);
        if (dc) cov_diff++;
        if (dc > 1) cov_gt1++;
        if (dp) pix_diff++;
        if (dp > 1) pix_gt1++;
    }

    printf("funnel %s vs %s (elev=%.1f r=%d mode=%d), N=%ld\n",
           strat_name(&sa), strat_name(&sb), elev, radius, mapmode, (long)NFRAG);
    printf("L1 bias values differ : %ld (%.5f%%)  maxabs=%.3e maxrel=%.3e\n",
           bias_diff, 100.0 * bias_diff / NFRAG, bias_maxabs, bias_maxrel);
    printf("L2 raw tap flips      : %ld (%.5f%%)  [no bias applied]\n",
           tap_raw_diff, 100.0 * tap_raw_diff / NFRAG);
    printf("L3 biased tap flips   : %ld (%.5f%%)\n",
           tap_biased_diff, 100.0 * tap_biased_diff / NFRAG);
    printf("L4 PCF coverage diffs : %ld (%.5f%%), >1 LSB: %ld\n",
           cov_diff, 100.0 * cov_diff / NFRAG, cov_gt1);
    printf("L5 final pixels diff  : %ld (%.5f%%), >1 LSB: %ld (%.5f%%)\n",
           pix_diff, 100.0 * pix_diff / NFRAG,
           pix_gt1, 100.0 * pix_gt1 / NFRAG);
    return 0;
}

static int cmd_sweep_elev(int argc, char** argv) {
    (void)argc;
    int radius = atoi(argv[2]), mapmode = atoi(argv[3]);
    BiasStrategy sa = {STRAT_REF_D, 0}, sb;
    parse_strat(argv[4], &sb);
    /* optional: sweep-elev radius mapmode strat [hi lo step] */
    float hi = argc > 5 ? strtof(argv[5], NULL) : 45.0f;
    float lo = argc > 6 ? strtof(argv[6], NULL) : 1.5f;
    float step = argc > 7 ? strtof(argv[7], NULL) : 1.0f;
    printf("elev_deg,frac_exact_pct,frac_gt1lsb_pct,maxdiff,meanabs\n");
    for (float e = hi; e >= lo - step * 0.5f; e -= step) {
        Config cfg;
        make_config(&cfg, e, radius, mapmode);
        Pipeline* pa = pipeline_run(&cfg, &sa);
        Pipeline* pb = pipeline_run(&cfg, &sb);
        PixelStats st = pixel_compare(pa->pixel, pb->pixel);
        printf("%.2f,%.4f,%.5f,%ld,%.5f\n", e,
               100.0 * st.exact / st.n,
               100.0 * st.gt_one_lsb / st.n, st.maxdiff, st.mean_abs);
    }
    return 0;
}

static int cmd_sweep_k(int argc, char** argv) {
    (void)argc;
    float elev = strtof(argv[2], NULL);
    int radius = atoi(argv[3]), mapmode = atoi(argv[4]);
    Config cfg;
    make_config(&cfg, elev, radius, mapmode);
    BiasStrategy ref = {STRAT_REF_D, 0};
    Pipeline* pr = pipeline_run(&cfg, &ref);

    printf("variant,nr_iters,time_ms,frac_exact_pct,frac_gt1lsb_pct,maxdiff,bias_rel_err\n");
    /* reference row */
    StageTime tr = time_pipeline(&cfg, &ref);
    printf("ref_d,-1,%.4f,100.0000,0.00000,0,0\n", tr.total);
    BiasStrategy nat = {STRAT_NAT_F, 0};
    StageTime tn = time_pipeline(&cfg, &nat);
    Pipeline* pn = pipeline_run(&cfg, &nat);
    PixelStats sn = pixel_compare(pr->pixel, pn->pixel);
    printf("nat_f,-1,%.4f,%.4f,%.5f,%ld,%.3e\n", tn.total,
           100.0 * sn.exact / sn.n, 100.0 * sn.gt_one_lsb / sn.n,
           sn.maxdiff, 0.0);
    for (int k = 0; k <= 3; k++) {
        BiasStrategy fk = {STRAT_FAST_K, k};
        StageTime tf = time_pipeline(&cfg, &fk);
        Pipeline* pf = pipeline_run(&cfg, &fk);
        PixelStats st = pixel_compare(pr->pixel, pf->pixel);
        double maxrel = 0;
        for (long i = 0; i < NFRAG; i++) {
            double rel = fabs((double)pr->bias[i] - (double)pf->bias[i])
                         / (double)pr->bias[i];
            if (rel > maxrel) maxrel = rel;
        }
        printf("fast_k%d,%d,%.4f,%.4f,%.5f,%ld,%.3e\n", k, k, tf.total,
               100.0 * st.exact / st.n, 100.0 * st.gt_one_lsb / st.n,
               st.maxdiff, maxrel);
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: bench compare|funnel|sweep-elev|sweep-k ...\n");
        return 2;
    }
    if (!strcmp(argv[1], "compare"))  return cmd_compare(argc, argv);
    if (!strcmp(argv[1], "funnel"))   return cmd_funnel(argc, argv);
    if (!strcmp(argv[1], "sweep-elev")) return cmd_sweep_elev(argc, argv);
    if (!strcmp(argv[1], "sweep-k"))  return cmd_sweep_k(argc, argv);
    fprintf(stderr, "unknown command %s\n", argv[1]);
    return 2;
}
