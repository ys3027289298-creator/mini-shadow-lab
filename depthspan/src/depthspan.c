
/* depthspan: light near/far span vs depth precision, culling, contact shadow.
 *
 * Orthographic (directional/ceiling) light. World depth is mapped linearly to
 * [0,1] then encoded into n-bit integer codes, exactly like a hardware UNORM
 * shadow map. Receiver and blocker are compared in the SAME integer code
 * space: when the furniture top and the floor under it round to one code, the
 * bottom is judged fully lit, which is the no-contact-shadow failure.
 *
 * The sweep trades two curves against each other:
 *   precision : dz_min(S) = S / 2^n          (smaller is better)
 *   culling   : N_cull(S)                    (objects clipped by the near plane)
 * A furniture bottom keeps its contact shadow only when its thickness, after
 * bias, still spans at least one depth code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAXOBJ 32

typedef struct {
    char id[8];
    float top;    /* light-space depth of the occluder surface (near side)   */
    float bot;    /* far side of the occluder interval (floor contact == H)  */
} Obj;

typedef struct {
    char name[16];
    float H;                 /* floor depth, light at 0 */
    float naive_far;         /* artist's over-extended far plane (the bug)  */
    int bits;                /* shadow map bit depth                        */
    float bias_world;        /* constant depth bias in world units          */
    Obj o[MAXOBJ];
    int n;
} Scene;

/* ---- core depth math ---------------------------------------------------- */

/* normalized [0,1] depth inside an orthographic frustum spanning [near, far] */
static float norm_depth(float z, float near_d, float far_d) {
    return (z - near_d) / (far_d - near_d);
}

/* n-bit UNORM integer code of a world depth (2^n codes, spacing 1/2^n) */
static int encode_depth(float z, float near_d, float far_d, int bits) {
    float levels = (float)(1 << bits);
    float c = floorf(norm_depth(z, near_d, far_d) * levels + 0.5f);
    if (c < 0.0f) c = 0.0f;
    if (c > levels - 1.0f) c = levels - 1.0f;
    return (int)c;
}

/* smallest world-depth difference the map can resolve at this span */
static float dz_min(float span, int bits) {
    return span / (float)(1 << bits);
}

/* span upper bound that still leaves thickness h visible after bias */
static float span_max_for(float h, float bias, int bits) {
    return (h - bias) * (float)(1 << bits);
}

/* object culled when its depth interval does not intersect [near, far] */
static int is_culled(const Obj* o, float near_d, float far_d) {
    return o->bot < near_d || o->top > far_d;
}

/* shadow test on the floor under an object: 1 = contact shadow survives.
 * Integer-code compare: same code for top and floor means fully lit. */
static int contact_shadow(const Scene* sc, const Obj* o,
                          float near_d, float far_d) {
    if (is_culled(o, near_d, far_d)) return 0;
    float levels = (float)(1 << sc->bits);
    float span = far_d - near_d;
    int c_recv = encode_depth(sc->H, near_d, far_d, sc->bits);
    int c_top  = encode_depth(o->top, near_d, far_d, sc->bits);
    int c_bias = (int)lroundf(sc->bias_world / span * levels);
    if (c_bias < 0) c_bias = 0;
    return c_recv - c_bias > c_top;
}

typedef struct {
    int culled;       /* clipped by the frustum, cast no shadow at all */
    int unresolved;   /* inside frustum but thickness below one code   */
    int resolved;     /* contact shadow visible                        */
    int cull_ids[MAXOBJ];
    int unres_ids[MAXOBJ];
    int res_ids[MAXOBJ];
    float strength;   /* mean bottom shadow strength, 1.0 == full      */
} Result;

static void evaluate(const Scene* sc, float near_d, float far_d, Result* r) {
    memset(r, 0, sizeof(*r));
    for (int i = 0; i < sc->n; i++) {
        if (is_culled(&sc->o[i], near_d, far_d)) {
            r->cull_ids[r->culled++] = i;
        } else if (contact_shadow(sc, &sc->o[i], near_d, far_d)) {
            r->res_ids[r->resolved++] = i;
        } else {
            r->unres_ids[r->unresolved++] = i;
        }
    }
    r->strength = (float)r->resolved / (float)sc->n;
}

static void print_ids(const Scene* sc, const int* ids, int k) {
    for (int i = 0; i < k; i++)
        printf("%s%s", i ? "," : "", sc->o[ids[i]].id);
}

static void report(const char* tag, const Scene* sc, float near_d, float far_d) {
    Result r;
    float span = far_d - near_d;
    evaluate(sc, near_d, far_d, &r);
    printf("  [%s] near=%.2f far=%.2f span=%.3fm  dz_min=%.6fm (%.4fmm)\n",
           tag, near_d, far_d, span, dz_min(span, sc->bits),
           dz_min(span, sc->bits) * 1000.0f);
    printf("        bottom_shadow_strength=%.3f (threshold 0.60) pass=%d\n",
           r.strength, r.strength >= 0.60f);
    printf("        culled(%d): ", r.culled);
    print_ids(sc, r.cull_ids, r.culled);
    printf(" | unresolved(%d): ", r.unresolved);
    print_ids(sc, r.unres_ids, r.unresolved);
    printf(" | resolved(%d): ", r.resolved);
    print_ids(sc, r.res_ids, r.resolved);
    printf("\n");
    if (r.strength < 0.60f) {
        printf("        >>> FAIL REPORT: span=%.3fm dz_min=%.6fm (%.4fmm)\n",
               span, dz_min(span, sc->bits), dz_min(span, sc->bits) * 1000.0f);
        printf("            culled_ids=");
        print_ids(sc, r.cull_ids, r.culled);
        printf(" ; unresolved_ids=");
        print_ids(sc, r.unres_ids, r.unresolved);
        printf("\n");
    }
}

/* sweep span S with far fixed at the floor and near pulled up: S = far-near */
static void run_scene(const Scene* sc) {
    printf("== %s  H=%.1fm  %d-bit  bias=%.1fmm  objects=%d ==\n",
           sc->name, sc->H, sc->bits, sc->bias_world * 1000.0f, sc->n);

    /* the bug: far plane over-extended, near pinned to the light */
    report("naive ", sc, 0.0f, sc->naive_far);

    /* thinnest furniture sets the precision-driven span ceiling */
    float hmin = sc->o[0].bot - sc->o[0].top;
    for (int i = 1; i < sc->n; i++) {
        float h = sc->o[i].bot - sc->o[i].top;
        if (h < hmin) hmin = h;
    }
    float smax = span_max_for(hmin, sc->bias_world, sc->bits);
    printf("  thinnest h=%.4fm -> precision span ceiling Smax=(h-b)*2^n=%.3fm\n",
           hmin, smax);

    /* two-curve trade-off: largest S <= Smax keeps culling minimal */
    const float step = 0.2f;
    float best = -1.0f;
    for (float S = step; S <= sc->H + 1e-6f; S += step) {
        if (S > smax + 1e-6f) break;
        best = S;
    }
    float S_star = (best > 0.0f) ? best : smax;
    if (S_star > sc->H) S_star = sc->H;
    float near_star = sc->H - S_star;
    report("chosen", sc, near_star, sc->H);

    /* two-curve operating point sweep, wide -> narrow.
     * tight_S  = smallest span still clearing 60% (precision side limit)
     * loose_S  = largest passing span (culling side limit, min cull design) */
    float tight_S = -1.0f, loose_S = -1.0f, best_strength = 0.0f, best_S = sc->H;
    for (float S = sc->H; S >= step - 1e-6f; S -= step) {
        float near_d = sc->H - S;
        Result r;
        evaluate(sc, near_d, near_d + S, &r);
        if (r.strength > best_strength) {
            best_strength = r.strength;
            best_S = S;
        }
        if (r.strength >= 0.60f) {
            if (loose_S < 0.0f) loose_S = S;   /* first hit while wide -> largest */
            tight_S = S;                        /* keep overwriting -> smallest    */
        }
    }
    if (tight_S > 0.0f) {
        printf("  feasible region for >=0.60: tightest span=%.2fm .. widest span=%.2fm\n",
               tight_S, loose_S);
        report("op_tight", sc, sc->H - tight_S, sc->H);
        report("op_loose", sc, sc->H - loose_S, sc->H);
    } else {
        printf("  >>> NO span reaches 0.60; best=%.3f at span=%.2fm "
               "(dz_min=%.4fmm)\n", best_strength, best_S,
               dz_min(best_S, sc->bits) * 1000.0f);
    }

    if (getenv("DS_CSV")) {
        FILE* f = fopen(getenv("DS_CSV"), "a");
        if (f) {
            /* pathological over-extended far plane: near pinned to the light,
             * far pushed past the floor (the original no-shadow bug) */
            Result r0;
            evaluate(sc, 0.0f, sc->naive_far, &r0);
            fprintf(f, "%s,%.2f,%.6f,%d,%d,%.3f\n",
                    sc->name, sc->naive_far,
                    dz_min(sc->naive_far, sc->bits),
                    r0.culled, r0.unresolved, r0.strength);
            /* working frusta: far stays on the floor, near is pulled up */
            for (float S = step; S <= sc->H; S += step) {
                float near_d = sc->H - S;
                Result r;
                evaluate(sc, near_d, near_d + S, &r);
                fprintf(f, "%s,%.2f,%.6f,%d,%d,%.3f\n",
                        sc->name, S, dz_min(S, sc->bits),
                        r.culled, r.unresolved, r.strength);
            }
            fclose(f);
        }
    }
    printf("\n");
}

static int failures = 0;
static void check(const char* name, int ok) {
    printf("%s=%d\n", name, ok ? 1 : 0);
    if (!ok) failures++;
}

int main(void) {
    /* ---- self tests ---------------------------------------------------- */
    /* linear depth: near -> 0, far -> 1, midpoint -> 0.5 */
    check("linear_near0", fabsf(norm_depth(2.0f, 2.0f, 10.0f)) < 1e-6f);
    check("linear_far1", fabsf(norm_depth(10.0f, 2.0f, 10.0f) - 1.0f) < 1e-6f);
    check("linear_mid", fabsf(norm_depth(6.0f, 2.0f, 10.0f) - 0.5f) < 1e-6f);

    /* one code of an 8-bit map over span 8m is 8/256 m */
    check("dz_formula", fabsf(dz_min(8.0f, 8) - 8.0f / 256.0f) < 1e-7f);
    check("encode_grid", encode_depth(0.5f, 0.0f, 1.0f, 8) == 128);

    /* span ceiling: h=1mm, b=0.5mm, 16-bit -> 0.5mm*65536 = 32.768m */
    check("span_ceiling", fabsf(span_max_for(0.001f, 0.0005f, 16) - 32.768f) < 1e-3f);

    /* toy room, floor at 2m: 50mm slab and 1mm sheet sitting on the floor */
    Obj thick = { "T", 1.95f, 2.0f };
    Obj thin  = { "t", 1.999f, 2.0f };
    Scene toy = { "toy", 2.0f, 2.0f, 16, 0.0005f, { thick, thin }, 2 };
    Obj above = { "A", 0.5f, 0.8f };   /* whole interval above near plane */
    check("cull_detect", is_culled(&above, 1.2f, 2.0f) == 1);
    check("not_cull", is_culled(&thick, 0.5f, 2.0f) == 0);
    check("thick_resolves", contact_shadow(&toy, &thick, 0.0f, 2.0f) == 1);
    /* tight 2m span: code = 0.0305mm, the 1mm sheet occupies ~33 codes */
    check("thin_resolves_tight", contact_shadow(&toy, &thin, 0.0f, 2.0f) == 1);
    /* wide 120m span: code = 1.83mm, top and floor share one code -> lit */
    check("thin_lost_wide", contact_shadow(&toy, &thin, 0.0f, 120.0f) == 0);
    /* widening past the analytic boundary (h-b)*2^n = 49.5mm*65536 ~ 3244m:
     * at 4000m the slab still clears its bias codes, at 8000m it is swallowed */
    check("thick_survives_4000", contact_shadow(&toy, &thick, 0.0f, 4000.0f) == 1);
    check("thick_lost_huge", contact_shadow(&toy, &thick, 0.0f, 8000.0f) == 0);

    /* ---- three scene size tiers ---------------------------------------- */
    Scene room = {
        "small_room", 2.8f, 20.0f, 16, 0.0005f,
        {
            { "s1", 2.20f, 2.80f },   /* 600mm cabinet  */
            { "s2", 2.45f, 2.80f },   /* 350mm table    */
            { "s3", 2.30f, 2.80f },   /* 500mm sofa     */
            { "s4", 2.70f, 2.80f },   /* 100mm shelf    */
            { "s5", 2.77f, 2.80f },   /* 30mm panel     */
            { "s6", 2.792f, 2.80f },  /* 8mm sheet      */
            { "s7", 2.796f, 2.80f },  /* 4mm sheet      */
            { "s8", 2.798f, 2.80f },  /* 2mm sheet      */
            { "s9", 2.55f, 2.80f },   /* 250mm bed      */
            { "s10", 2.76f, 2.80f },  /* 40mm drawer    */
        }, 10
    };

    Scene hall = {
        "mid_hall", 8.0f, 60.0f, 16, 0.0005f,
        {
            { "m1", 7.00f, 8.00f },    /* 1000mm counter */
            { "m2", 7.40f, 8.00f },    /* 600mm bench    */
            { "m3", 6.20f, 8.00f },    /* 1800mm shelf   */
            { "m4", 7.80f, 8.00f },    /* 200mm panel    */
            { "m5", 7.95f, 8.00f },    /* 50mm board     */
            { "m6", 7.988f, 8.00f },   /* 12mm sheet     */
            { "m7", 7.994f, 8.00f },   /* 6mm sheet      */
            { "m8", 7.997f, 8.00f },   /* 3mm sheet      */
            { "m9", 7.50f, 8.00f },    /* 500mm cabinet  */
            { "m10", 7.90f, 8.00f },   /* 100mm desk top */
        }, 10
    };

    Scene atrium = {
        "large_atrium", 48.0f, 120.0f, 16, 0.0005f,
        {
            /* floor furniture: thick pieces + thin industrial sheet goods */
            { "f1", 47.00f, 48.00f },  /* 1000mm kiosk   */
            { "f2", 47.40f, 48.00f },  /* 600mm bench    */
            { "f3", 47.70f, 48.00f },  /* 300mm cabinet  */
            { "f4", 47.90f, 48.00f },  /* 100mm panel    */
            { "f5", 47.95f, 48.00f },  /* 50mm board     */
            { "f6", 47.992f, 48.00f }, /* 8mm sheet      */
            { "f7", 47.995f, 48.00f }, /* 5mm sheet      */
            { "f8", 47.997f, 48.00f }, /* 3mm sheet      */
            { "f9", 47.9985f, 48.00f },/* 1.5mm sheet    */
            { "f10", 47.9990f, 48.00f },/* 1mm sheet     */
            /* ceiling-hung services: culled once the near plane passes them */
            { "h1", 6.0f, 6.5f },      /* banner         */
            { "h2", 7.0f, 7.4f },      /* duct           */
            { "h3", 8.0f, 8.3f },      /* pipe           */
            { "h4", 9.0f, 9.2f },      /* conduit        */
            { "h5", 10.0f, 10.15f },   /* tray           */
            { "h6", 11.0f, 11.1f },    /* cable tray     */
            { "h7", 12.0f, 12.08f },   /* housing        */
            { "h8", 13.0f, 13.05f },   /* light bar      */
            { "h9", 14.0f, 14.03f },   /* strip          */
            { "h10", 16.0f, 16.1f },   /* rail           */
            { "h11", 18.0f, 18.08f },  /* housing        */
            { "h12", 20.0f, 20.05f },  /* light          */
            { "h13", 24.0f, 24.03f },  /* strip          */
            { "h14", 28.0f, 28.02f },  /* rail           */
        }, 24
    };

    printf("\n");
    run_scene(&room);
    run_scene(&hall);
    run_scene(&atrium);

    printf("selftest_all_ok=%d\n", failures == 0);
    return failures == 0 ? 0 : 1;
}
