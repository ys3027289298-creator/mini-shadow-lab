#ifndef SHADOW_BENCH_PIPELINE_H
#define SHADOW_BENCH_PIPELINE_H

#include "common.h"
#include "bias.h"

typedef struct {
    const Config* cfg;
    LightFrame lf;
    float map_d[SMAP_N];          /* depth as stored in the map (after mode) */
    float ndotl[NFRAG];
    float frag_d[NFRAG];
    float bias[NFRAG];
    int   lit_raw[NFRAG];         /* unbiased single-tap shadow test */
    int   lit_biased[NFRAG];      /* biased single-tap shadow test */
    uint8_t coverage[NFRAG];      /* PCF output, 0..255 */
    uint8_t pixel[NFRAG];         /* final image */
} Pipeline;

Pipeline* pipeline_run(const Config* cfg, const BiasStrategy* s);
Pipeline* pipeline_alloc(const Config* cfg);
void stage_mapbuild(Pipeline* p);
void stage_bias(Pipeline* p, const BiasStrategy* s);
void stage_pcf(Pipeline* p);
void stage_shade(Pipeline* p);
void write_pgm(const char* path, const uint8_t* px);

#endif
