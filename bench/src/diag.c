/* diag.c - locate and explain the largest pixel differences between two
 * strategies. Prints the top-N worst pixels with their full local state. */
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
    if (argc < 6) { fprintf(stderr, "usage: diag elev radius mapmode stratA stratB\n"); return 2; }
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

    /* gather worst pixels */
    long idx[10]; long dv[10]; int n = 0;
    for (long i = 0; i < NFRAG; i++) {
        long d = labs((long)pa->pixel[i] - (long)pb->pixel[i]);
        int pos = 0;
        while (pos < n && dv[pos] >= d) pos++;
        if (pos < 10) {
            for (int j = n > 9 ? 9 : n; j > pos; j--) { dv[j] = dv[j-1]; idx[j] = idx[j-1]; }
            dv[pos] = d; idx[pos] = i;
            if (n < 10) n++;
        }
    }

    for (int t = 0; t < n; t++) {
        long i = idx[t];
        int px = i % IMG_W, py = i / IMG_W;
        float w[3]; fragment_pos(px, py, w);
        float u, v; light_uv(&pa->lf, w, &u, &v);
        int cx = (int)(u * SMAP_W), cy = (int)(v * SMAP_H);
        printf("rank %d: pixel(%d,%d) world(%.2f,%.2f) |dpix|=%ld covA=%d covB=%d\n",
               t, px, py, w[0], w[1], dv[t], pa->coverage[i], pb->coverage[i]);
        printf("  ndotl=%.6f biasA=%.8e biasB=%.8e frag=%.8f mapcenter=%.8f\n",
               pa->ndotl[i], pa->bias[i], pb->bias[i], pa->frag_d[i],
               pa->map_d[cy * SMAP_W + cx]);
        /* per-tap comparison around this fragment */
        int flips = 0;
        for (int dy = -radius; dy <= radius; dy++)
            for (int dx = -radius; dx <= radius; dx++) {
                int sx = cx + dx, sy = cy + dy;
                if (sx < 0) sx = 0;
                if (sy < 0) sy = 0;
                if (sx >= SMAP_W) sx = SMAP_W - 1;
                if (sy >= SMAP_H) sy = SMAP_H - 1;
                float md = pa->map_d[sy * SMAP_W + sx];
                int ta = (pa->frag_d[i] - pa->bias[i] <= md);
                int tb = (pb->frag_d[i] - pb->bias[i] <= md);
                if (ta != tb) {
                    flips++;
                    printf("  TAP FLIP d(%d,%d) map=%.8f marginA=%+.2e marginB=%+.2e\n",
                           dx, dy, md,
                           pa->frag_d[i] - pa->bias[i] - md,
                           pb->frag_d[i] - pb->bias[i] - md);
                }
            }
        printf("  tap flips for this pixel: %d / %d\n", flips, (2*radius+1)*(2*radius+1));
    }

    /* histogram of nonzero diff sizes */
    long hist[256] = {0};
    for (long i = 0; i < NFRAG; i++)
        hist[labs((long)pa->pixel[i] - (long)pb->pixel[i])]++;
    printf("\ndiff-size histogram (level: count), nonzero only:\n");
    for (int d = 1; d < 256; d++) if (hist[d]) printf("  %3d: %ld\n", d, hist[d]);
    return 0;
}
