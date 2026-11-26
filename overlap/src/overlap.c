/* overlap: compose shadow attenuation from overlapping casters.
 *
 * v_i in [0,1] is the light a layer leaves at a pixel (1 = lit, 0 = black);
 * a PCF lookup already returns exactly this fraction.
 *
 * Applying every layer multiplies the pixel's light:
 *     v_prod = prod_i v_i
 * Two opaque blockers on the same light ray cannot attenuate it twice, so a
 * pixel may be attenuated only once, by the darkest layer:
 *     v_min  = min_i v_i
 *
 * Sorted ascending v(1) <= ... <= v(n):
 *     v_prod = v_min * prod_{k>=2} v(k)
 *     (v_min - v_prod) / v_min = 1 - prod_{k>=2} v(k)
 * For n = 2 the relative over-darkening is 1 - v(2), exactly the brighter
 * layer's attenuation.
 *
 * Accepting "overlap darkness == non-overlap darkness" gives prod_{k>=2}
 * v(k) = 1, which only min_i v_i satisfies for every pixel; product only
 * satisfies it when every extra layer is fully lit. The program runs three
 * overlap tiers under both compositions, and prints, for any tier that
 * breaks the bar, the layer count, both brightnesses and the method.
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <math.h>
#include <time.h>

#define MAXL 8
#define TOL 0.01f /* 1% relative over-darkening is the acceptance bar */

typedef struct { int n; float v[MAXL]; } Layers;

/* bug: every layer attenuates the pixel */
static float compose_product(const Layers* L) {
    float v = 1.0f;
    for (int i = 0; i < L->n; i++) v *= L->v[i];
    return v;
}

/* fix: pixel attenuated once, by the darkest layer */
static float compose_min(const Layers* L) {
    float v = 1.0f;
    for (int i = 0; i < L->n; i++)
        if (L->v[i] < v) v = L->v[i];
    return v;
}

/* non-overlap reference: the darkest layer applied alone */
static float compose_once(const Layers* L) {
    int k = 0;
    for (int i = 1; i < L->n; i++)
        if (L->v[i] < L->v[k]) k = i;
    return L->v[k];
}

/* fraction of the non-overlap brightness lost at the overlap */
static float overdark(float ref, float got) {
    if (ref <= 1e-6f) return 0.0f; /* both black: the band has no contrast */
    float d = (ref - got) / ref;
    return d > 0.0f ? d : 0.0f;
}

/* 1 - prod of every layer except the darkest, i.e. the derived excess */
static float predicted_overdark(const Layers* L) {
    int k = 0;
    for (int i = 1; i < L->n; i++)
        if (L->v[i] < L->v[k]) k = i;
    float p = 1.0f;
    for (int i = 0; i < L->n; i++)
        if (i != k) p *= L->v[i];
    return 1.0f - p;
}

static int failures = 0;
static void check(const char* name, int ok) {
    printf("%s=%d\n", name, ok ? 1 : 0);
    if (!ok) failures++;
}
static int closef(float a, float b) { return fabsf(a - b) < 1e-6f; }

/* ---- cost: one shadow fetch per layer, plus the combine ops ---- */
#define BN (1u << 20)
static float bench_pix[BN][MAXL];
static float bench_depth[BN];
static volatile float sink;

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static void fill_bench(void) {
    unsigned x = 2463534242u;
    for (unsigned p = 0; p < BN; p++) {
        for (int i = 0; i < MAXL; i++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            bench_pix[p][i] = 0.4f + 0.6f * (float)(x & 0xffffu) / 65535.0f;
        }
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        bench_depth[p] = 0.4f + 0.6f * (float)(x & 0xffffu) / 65535.0f;
    }
}

/* one shadow-map fetch: random-access depth compare, as in a real lookup */
static float fetch_depth(unsigned p, int i) {
    int idx = (int)(bench_pix[p][i] * 65535.0f) & (int)(BN - 1);
    return bench_depth[idx] < bench_pix[p][i] ? 0.0f : 1.0f;
}

static double bench_ns(int n, int use_min) {
    double best = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_ns();
        float acc = 0.0f;
        if (use_min) {
            for (unsigned p = 0; p < BN; p++) {
                float v = 1.0f;
                for (int i = 0; i < n; i++) {
                    float s = fetch_depth(p, i);
                    if (s < v) v = s;
                }
                acc += v;
            }
        } else {
            for (unsigned p = 0; p < BN; p++) {
                float v = 1.0f;
                for (int i = 0; i < n; i++) v *= fetch_depth(p, i);
                acc += v;
            }
        }
        double dt = now_ns() - t0;
        sink += acc;
        if (dt < best) best = dt;
    }
    return best / (double)BN;
}

/* Combine cost with an L1-resident table: same loads for both variants, so
 * the delta is purely the extra compare (min) vs the extra multiply. */
#define CN 4096
static float ctab[CN];
static double bench_combine(int n, int use_min) {
    double best = 1e30;
    for (int rep = 0; rep < 7; rep++) {
        double t0 = now_ns();
        float acc = 0.0f;
        for (unsigned p = 0; p < BN; p++) {
            unsigned b = p & (CN - 1);
            float v = ctab[b];
            if (use_min) {
                for (int i = 1; i < n; i++) {
                    float s = ctab[(b + (unsigned)i * 977u) & (CN - 1)];
                    if (s < v) v = s;
                }
            } else {
                for (int i = 1; i < n; i++)
                    v *= ctab[(b + (unsigned)i * 977u) & (CN - 1)];
            }
            acc += v;
        }
        double dt = now_ns() - t0;
        sink += acc;
        if (dt < best) best = dt;
    }
    return best / (double)BN;
}

int main(void) {
    /* --- algebra checks --- */
    Layers one = { 1, { 0.55f } };
    check("one_layer_product_is_min",
          closef(compose_product(&one), compose_min(&one)));

    Layers two = { 2, { 0.55f, 0.85f } };
    float ref = compose_once(&two);
    float prod = compose_product(&two);
    float mn = compose_min(&two);
    check("two_layer_min_matches_single",
          closef(mn, compose_once(&two)) && closef(mn, ref));
    check("two_layer_product_is_darker", prod < mn);
    check("two_layer_excess_is_brighter_attenuation",
          closef(overdark(ref, prod), 1.0f - 0.85f));
    check("excess_matches_formula",
          closef(overdark(ref, prod), predicted_overdark(&two)));

    /* all extra layers fully lit => multiplying cannot over-darken */
    Layers flat = { 3, { 0.55f, 1.0f, 1.0f } };
    check("no_partial_overlap_no_excess",
          closef(compose_product(&flat), compose_min(&flat)));

    /* --- three overlap tiers, both compositions --- */
    const float dark = 0.55f, extra = 0.85f;
    const int tier_n[3] = { 2, 3, 4 };
    int chosen_min[3] = { 0, 0, 0 };

    for (int t = 0; t < 3; t++) {
        Layers L;
        L.n = tier_n[t];
        L.v[0] = dark;
        for (int i = 1; i < L.n; i++) L.v[i] = extra;

        float r = compose_once(&L);
        float p = compose_product(&L);
        float m = compose_min(&L);
        float pe = overdark(r, p);
        float me = overdark(r, m);

        printf("tier=%d layers=%d method=product overlap=%.6f nonoverlap=%.6f overdark=%.6f pass=%d\n",
               t + 1, L.n, p, r, pe, pe <= TOL ? 1 : 0);
        printf("tier=%d layers=%d method=min overlap=%.6f nonoverlap=%.6f overdark=%.6f pass=%d\n",
               t + 1, L.n, m, r, me, me <= TOL ? 1 : 0);

        if (pe > TOL)
            printf("EXCEED layers=%d nonoverlap=%.6f overlap=%.6f method=product overdark=%.6f\n",
                   L.n, r, p, pe);
        if (me > TOL)
            printf("EXCEED layers=%d nonoverlap=%.6f overlap=%.6f method=min overdark=%.6f\n",
                   L.n, r, m, me);

        /* cheapest composition that meets the bar */
        chosen_min[t] = !(pe <= TOL);
        float cm = chosen_min[t] ? m : p;
        float ce = chosen_min[t] ? me : pe;
        printf("chosen tier=%d layers=%d method=%s overlap=%.6f nonoverlap=%.6f overdark=%.6f ok=%d\n",
               t + 1, L.n, chosen_min[t] ? "min" : "product", cm, r, ce, ce <= TOL ? 1 : 0);

        char name[64];
        snprintf(name, sizeof(name), "tier%d_chosen_consistent", t + 1);
        check(name, ce <= TOL && closef(cm, r) == (chosen_min[t] != 0));
    }

    /* --- decision curves --- */
    printf("curve:\n");
    for (int n = 2; n <= MAXL; n++) {
        Layers L;
        L.n = n;
        L.v[0] = dark;
        for (int i = 1; i < n; i++) L.v[i] = extra;
        float od = predicted_overdark(&L);
        printf("curve n=%d shadow_fetches=%d product_mul=%d min_extra_cmp=%d product_overdark=%.6f\n",
               n, n, n - 1, n - 1, od);
    }

    fill_bench();
    bench_ns(MAXL, 0); /* warm caches and clocks */
    bench_ns(MAXL, 1);
    printf("bench (combine after per-layer fetch, 1M px):\n");
    const int bn[4] = { 2, 3, 4, 8 };
    for (int i = 0; i < 4; i++) {
        int n = bn[i];
        double tp = bench_ns(n, 0);
        double tm = bench_ns(n, 1);
        printf("bench n=%d product_ns_px=%.3f min_ns_px=%.3f delta_ns_px=%.3f\n",
               n, tp, tm, tm - tp);
    }
    printf("combine (L1-resident table, per-layer fetch excluded):\n");
    for (int i = 0; i < CN; i++) ctab[i] = 0.4f + 0.6f * (float)(i % 251) / 250.0f;
    const int cb[4] = { 2, 3, 4, 8 };
    for (int i = 0; i < 4; i++) {
        int n = cb[i];
        double tp = bench_combine(n, 0);
        double tm = bench_combine(n, 1);
        printf("combine n=%d product_ns_px=%.4f min_ns_px=%.4f delta_ns_px=%.4f\n",
               n, tp, tm, tm - tp);
    }

    printf("all_ok=%d\n", failures == 0);
    return failures == 0 ? 0 : 1;
}
