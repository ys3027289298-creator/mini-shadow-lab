/*
 * margin.c - validate the "the difference is swallowed by the comparison"
 * explanation with two experiments.
 *
 * Experiment 1 (margin band): for every single tap, compute the signed
 *   margin m = frag - bias - mapdepth. A tap can only flip when the two
 *   strategies' bias difference exceeds |m|. Report the |m| distribution and
 *   check that every actual flip indeed sits inside the predicted band.
 *
 * Experiment 2 (error-amplification sweep): inject biasB(G) = biasA +
 *   G*(biasB-biasA) for G = 1..100000. If the funnel explanation is right,
 *   flips must stay ~0 while G*eps is smaller than the typical margin, then
 *   rise monotonically once the amplified error crosses the margin band.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "common.h"
#include "bias.h"
#include "pipeline.h"

static void parse_strat(const char* spec, BiasStrategy* s) {
    if (!strcmp(spec, "ref")) { s->kind = STRAT_REF_D; s->nr_iters = 0; }
    else if (!strcmp(spec, "nat")) { s->kind = STRAT_NAT_F; s->nr_iters = 0; }
    else if (!strncmp(spec, "fast:", 5)) { s->kind = STRAT_FAST_K; s->nr_iters = atoi(spec + 5); }
    else { fprintf(stderr, "bad spec\n"); exit(2); }
}

int main(int argc, char** argv) {
    if (argc < 6) { fprintf(stderr, "usage: margin elev radius mapmode stratA stratB\n"); return 2; }
    float elev = strtof(argv[1], NULL);
    int radius = atoi(argv[2]), mapmode = atoi(argv[3]);
    BiasStrategy sa, sb;
    parse_strat(argv[4], &sa);
    parse_strat(argv[5], &sb);

    Config cfg;
    config_default(&cfg, elev);
    cfg.pcf_radius = radius;
    cfg.map_mode = mapmode;

    Pipeline* pa = pipeline_run(&cfg, &sa);
    Pipeline* pb = pipeline_run(&cfg, &sb);

    /* ---- Experiment 1: margin distribution over every PCF tap ---------- */
    double max_bias_delta = 0;
    long nbins = 9;
    long margin_hist[9] = {0};           /* |m| in decades 1e-9..1e-1, else */
    long flips_total = 0, taps_total = 0;
    long flips_inside_band = 0;

    for (long i = 0; i < NFRAG; i++) {
        double dbias = fabs((double)pa->bias[i] - (double)pb->bias[i]);
        if (dbias > max_bias_delta) max_bias_delta = dbias;

        int px = i % IMG_W, py = i / IMG_W;
        float w[3]; fragment_pos(px, py, w);
        float u, v; light_uv(&pa->lf, w, &u, &v);
        int cx = (int)(u * SMAP_W), cy = (int)(v * SMAP_H);
        for (int dy = -radius; dy <= radius; dy++)
            for (int dx = -radius; dx <= radius; dx++) {
                int sx = cx + dx, sy = cy + dy;
                if (sx < 0) sx = 0;
                if (sy < 0) sy = 0;
                if (sx >= SMAP_W) sx = SMAP_W - 1;
                if (sy >= SMAP_H) sy = SMAP_H - 1;
                float md = pa->map_d[sy * SMAP_W + sx];
                float mA = pa->frag_d[i] - pa->bias[i] - md;
                float mB = pb->frag_d[i] - pb->bias[i] - md;
                double am = fabs((double)mA);
                int b = 0;
                double thresh = 1e-9;
                while (b < nbins - 1 && am > thresh) { b++; thresh *= 10.0; }
                margin_hist[b]++;
                taps_total++;
                int litA = (mA <= 0.0f), litB = (mB <= 0.0f);
                if (litA != litB) {
                    flips_total++;
                    if (am <= max_bias_delta + 1e-12) flips_inside_band++;
                }
            }
    }

    printf("Experiment 1: margin band (elev=%.0f r=%d), taps=%ld\n", elev, radius, taps_total);
    printf("max |biasA-biasB| = %.3e\n", max_bias_delta);
    const char* labels[9] = {"<=1e-9","1e-8","1e-7","1e-6","1e-5","1e-4","1e-3","1e-2",">1e-2"};
    for (int b = 0; b < nbins; b++)
        printf("  |margin| %-7s : %10ld (%7.4f%%)\n", labels[b],
               margin_hist[b], 100.0 * margin_hist[b] / taps_total);
    printf("taps flipped: %ld (%.5f%%); flips with |margin| <= bias-delta band: %ld/%ld\n",
           flips_total, 100.0 * flips_total / taps_total,
           flips_inside_band, flips_total);

    /* ---- Experiment 2: amplify the bias difference by gain G ------------ */
    printf("\nExperiment 2: injected bias = biasA + G*(biasB-biasA)\n");
    printf("G,amplified_eps,flips,flip_pct,gt1lsb_pixels_pct\n");
    /* G=0 reproduces strategy A exactly; sanity anchor, must show 0 flips */
    double gains[] = {0, 1, 10, 100, 1000, 10000, 100000};
    for (int g = 0; g < 7; g++) {
        double G = gains[g];
        long flips = 0;
        long pix_gt1 = 0;
        for (long i = 0; i < NFRAG; i++) {
            float bias_g = pa->bias[i]
                + (float)G * (pb->bias[i] - pa->bias[i]);
            int px = i % IMG_W, py = i / IMG_W;
            float w[3]; fragment_pos(px, py, w);
            float u, v; light_uv(&pa->lf, w, &u, &v);
            int cx = (int)(u * SMAP_W), cy = (int)(v * SMAP_H);
            int lit = 0, total = 0;
            for (int dy = -radius; dy <= radius; dy++)
                for (int dx = -radius; dx <= radius; dx++) {
                    int sx = cx + dx, sy = cy + dy;
                    if (sx < 0) sx = 0;
                    if (sy < 0) sy = 0;
                    if (sx >= SMAP_W) sx = SMAP_W - 1;
                    if (sy >= SMAP_H) sy = SMAP_H - 1;
                    float md = pa->map_d[sy * SMAP_W + sx];
                    float mA = pa->frag_d[i] - pa->bias[i] - md;
                    int litA = (mA <= 0.0f);
                    int litG = (pa->frag_d[i] - bias_g <= md);
                    if (litA != litG) flips++;
                    if (litG) lit++;
                    total++;
                }
            int cov_g = (lit * 255) / total;
            if (abs(cov_g - (int)pa->coverage[i]) > 1) pix_gt1++;
        }
        printf("%.0f,%.3e,%ld,%.5f,%.5f\n", G, G * max_bias_delta, flips,
               100.0 * flips / taps_total, 100.0 * pix_gt1 / NFRAG);
    }
    /* direct strategy-vs-strategy tap count on the same tap set */
    long direct_flips = 0;
    for (long i = 0; i < NFRAG; i++) {
        int px = i % IMG_W, py = i / IMG_W;
        float w[3]; fragment_pos(px, py, w);
        float u, v; light_uv(&pa->lf, w, &u, &v);
        int cx = (int)(u * SMAP_W), cy = (int)(v * SMAP_H);
        for (int dy = -radius; dy <= radius; dy++)
            for (int dx = -radius; dx <= radius; dx++) {
                int sx = cx + dx, sy = cy + dy;
                if (sx < 0) sx = 0;
                if (sy < 0) sy = 0;
                if (sx >= SMAP_W) sx = SMAP_W - 1;
                if (sy >= SMAP_H) sy = SMAP_H - 1;
                float md = pa->map_d[sy * SMAP_W + sx];
                if ((pa->frag_d[i] - pa->bias[i] <= md) !=
                    (pb->frag_d[i] - pb->bias[i] <= md))
                    direct_flips++;
            }
    }
    printf("direct A-vs-B tap flips: %ld (%.5f%% of taps)\n",
           direct_flips, 100.0 * direct_flips / taps_total);
    return 0;
}
