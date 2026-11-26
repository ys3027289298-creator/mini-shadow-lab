
/* pcf: percentage-closer filtering self-test. Implementation in pcf_core.h. */
#include "pcf_core.h"

static int failures = 0;
static void check(const char* name, int ok) {
    printf("%s=%d\n", name, ok ? 1 : 0);
    if (!ok) failures++;
}

int main(void) {
    DepthMap m;
    m.w = 4; m.h = 4;
    for (int i = 0; i < 16; i++) m.d[i] = 0.5f;

    check("all_lit", pcf(&m, 2, 2, 0.4f, 0.0f, 1) == 255);
    check("all_shadow", pcf(&m, 2, 2, 0.6f, 0.0f, 1) == 0);

    /* half the map blocked -> partial coverage */
    DepthMap h;
    h.w = 4; h.h = 4;
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            h.d[y * 4 + x] = (x < 2) ? 0.3f : 0.7f;
    int v = pcf(&h, 2, 2, 0.5f, 0.0f, 1);
    check("partial", v > 30 && v < 220);

    /* bias turns a borderline pixel lit */
    check("bias_flips", pcf(&m, 2, 2, 0.51f, 0.0f, 0) == 0);
    check("bias_helps", pcf(&m, 2, 2, 0.51f, 0.05f, 0) == 255);

    check("penumbra_zero_at_contact", fabsf(penumbra_texels(5.0f, 5.0f, 1.0f, 0.1f, 1.0f)) < 1e-6f);
    check("penumbra_grows", penumbra_texels(1.0f, 10.0f, 1.0f, 0.1f, 1.0f) >
                            penumbra_texels(9.0f, 10.0f, 1.0f, 0.1f, 1.0f));

    /* radius tracks the distance ratio: linear in w, coefficient k */
    float w1 = penumbra_texels(9.0f, 10.0f, 1.0f, 0.1f, 1.0f);  /* d/D = 0.1 -> 1 texel  */
    float w5 = penumbra_texels(5.0f, 10.0f, 1.0f, 0.1f, 1.0f);  /* d/D = 0.5 -> 5 texels */
    check("radius_linear_in_penumbra",
          fabsf(kernel_radius_texels(w5, 0.5f) - 5.0f * kernel_radius_texels(w1, 0.5f)) < 1e-5f);
    check("radius_half_at_k_half", fabsf(kernel_radius_texels(8.0f, 0.5f) - 4.0f) < 1e-6f);
    check("quantize_rounds_and_clamps",
          quantize_radius(3.4f, 1, 8) == 3 &&
          quantize_radius(0.2f, 1, 8) == 1 &&
          quantize_radius(99.0f, 1, 8) == 8);
    check("radius_bad_input", kernel_radius_texels(-1.0f, 0.5f) < 0.0f &&
                              quantize_radius(1.0f, 5, 2) < 0);

    check("bad_radius", pcf(&m, 0, 0, 0.5f, 0.0f, 9) == -1);
    check("bad_map", pcf(&h, 0, 0, 0.5f, 0.0f, 1) > -1);

    printf("all_ok=%d\n", failures == 0);
    return failures == 0 ? 0 : 1;
}
