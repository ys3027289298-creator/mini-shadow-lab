/*
 * unit_trigger.c - necessary-and-sufficient condition for a visible
 * ref-vs-nat divergence, isolated from scene geometry:
 *
 *   n.l < 1e-4 (nat's clamp regime) AND a map texel depth md placed between
 *   the two biased fragment depths -> the same texel evaluates to opposite
 *   lit/shadow results. Above 1e-4 the gap is zero and no flip is possible.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <math.h>

#include "common.h"
#include "bias.h"

int main(void) {
    Config c;
    config_default(&c, 1.0f);
    BiasStrategy ref = {STRAT_REF_D, 0}, nat = {STRAT_NAT_F, 0};
    float xs[] = {1e-3f, 1e-4f, 1e-5f, 1e-6f};
    printf("x          bias_ref     bias_nat    gap        mapdepth md   ref  nat\n");
    for (int i = 0; i < 4; i++) {
        float br = compute_bias(xs[i], &c, &ref);
        float bn = compute_bias(xs[i], &c, &nat);
        float frag = 0.5f;
        float md = frag - 0.5f * (br + bn);
        int lit_ref = (frag - br <= md);
        int lit_nat = (frag - bn <= md);
        printf("%.0e  %11.5e %11.5e %9.3e  %11.6f  %4d %4d  %s\n",
               xs[i], br, bn, fabsf(br - bn), md, lit_ref, lit_nat,
               lit_ref != lit_nat ? "<-- DIVERGES" : "");
    }
    float x = 0.5f;
    float br = compute_bias(x, &c, &ref), bn = compute_bias(x, &c, &nat);
    float frag = 0.5f, md = frag - 0.5f * (br + bn);
    printf("x=5e-1  %11.5e %11.5e %9.3e  %11.6f  %4d %4d  %s\n",
           br, bn, fabsf(br - bn), md,
           (frag - br <= md), (frag - bn <= md),
           ((frag - br <= md) != (frag - bn <= md)) ? "<-- DIVERGES"
                                                    : "(identical)");
    return 0;
}
