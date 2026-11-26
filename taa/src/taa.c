
/* taa: temporal accumulation (EMA) of a moving, PCF-softened shadow edge.
 *
 * The shadow of a moving object is rendered per frame with sub-pixel jitter,
 * then blended against last frame's result.  A history weight that is too
 * large leaves a ghost trail behind the moving edge; one that is too small
 * stops smoothing the jitter and the edge flickers.  This file measures both
 * quantities on a 1-D scanline so the trade-off can be quantified.
 */
#include <stdio.h>
#include <math.h>
#include <string.h>

#define W        1024
#define SHADOW_L 40.0f                  /* shadow length, px                     */
#define BUDGET   (SHADOW_L / 10.0f)     /* allowed trail: 1/10 of shadow = 4 px  */
#define JIT      0.15f                  /* sub-pixel shadow-map jitter, px       */
#define PCF_K    3                      /* box-PCF kernel width, texels          */
#define EPS      0.10f                  /* visibility threshold for a ghost      */
#define FLICK    0.05f                  /* frame delta above which a pixel flickers */
#define WARM     300                    /* stationary warm-up frames             */
#define MOVE     120                    /* frames in motion                      */
#define SETTLE   150                    /* stationary frames before measuring    */
#define MEAS     300                    /* frames counted for flicker            */
#define NGRID    50                     /* beta = 0.00, 0.02 .. 0.98             */
#define NSPEED   3

static unsigned long long rng;

static float urand(void) {              /* [0,1), LCG */
    rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return (float)((rng >> 33) & 0x7fffffffu) / (float)0x80000000u;
}

/* PCF coverage of pixel x: fraction of the kernel covered by shadow [E, E+L). */
static float coverage(float E, int x) {
    float s = 0.0f;
    for (int k = 0; k < PCF_K; k++) {
        float t = (float)(x - 1 + k);
        if (t >= E && t < E + SHADOW_L) s += 1.0f;
    }
    return s / (float)PCF_K;
}

/* Equivalent frame count: age at which the residual history beta^n falls to
 * EPS, i.e. n = ln(EPS)/ln(beta).  No history -> 0. */
static float neff(float beta) {
    if (beta <= 0.0f) return 0.0f;
    return logf(EPS) / logf(beta);
}

/* Largest history weight whose residual trail v*Neff(beta) stays in BUDGET. */
static float beta_max(float v) {
    float n = BUDGET / v;               /* Neff budget at this speed */
    return expf(logf(EPS) / n);
}

typedef struct { float trail; int flick; int rest_trail; } Run;

/* One full protocol: warm up at rest, move at v with beta_move, freeze and
 * measure flicker with beta_rest.  Same RNG seed for every run so weight
 * sweeps share one jitter sequence. */
static Run simulate(float v, float beta_move, float beta_rest) {
    float h[W], prev[W];
    memset(h, 0, sizeof h);
    rng = 0x243f6a88ULL;
    float E = 40.0f;

    for (int f = 0; f < WARM; f++) {
        float Ej = E + (urand() * 2.0f - 1.0f) * JIT;
        for (int x = 0; x < W; x++) {
            float c = coverage(Ej, x);
            h[x] = beta_rest * h[x] + (1.0f - beta_rest) * c;
        }
    }

    double trail_sum = 0.0;
    int trail_n = 0;
    for (int f = 0; f < MOVE; f++) {
        E += v;
        float Ej = E + (urand() * 2.0f - 1.0f) * JIT;
        for (int x = 0; x < W; x++) {
            float c = coverage(Ej, x);
            h[x] = beta_move * h[x] + (1.0f - beta_move) * c;
        }
        if (f >= 30) {                   /* skip motion transients */
            int xL = (int)floorf(Ej);
            int n = 0;
            for (int x = xL - 1; x >= 0 && h[x] >= EPS; x--) n++;
            trail_sum += n;
            trail_n++;
        }
    }

    float Estop = E;
    for (int f = 0; f < SETTLE; f++) {
        float Ej = Estop + (urand() * 2.0f - 1.0f) * JIT;
        for (int x = 0; x < W; x++) {
            float c = coverage(Ej, x);
            h[x] = beta_rest * h[x] + (1.0f - beta_rest) * c;
        }
    }

    int xL = (int)floorf(Estop);
    int rest_trail = 0;
    for (int x = xL - 1; x >= 0 && h[x] >= EPS; x--) rest_trail++;

    memcpy(prev, h, sizeof h);
    long events = 0;
    for (int f = 0; f < MEAS; f++) {
        float Ej = Estop + (urand() * 2.0f - 1.0f) * JIT;
        for (int x = 0; x < W; x++) {
            float c = coverage(Ej, x);
            h[x] = beta_rest * h[x] + (1.0f - beta_rest) * c;
        }
        for (int x = xL - 2; x < xL; x++)
            if (fabsf(h[x] - prev[x]) > FLICK) events++;
        memcpy(prev, h, sizeof h);
    }

    Run r = { (float)(trail_sum / trail_n), (int)events, rest_trail };
    return r;
}

static int failures = 0;
static void check(const char* name, int ok) {
    printf("%s=%d\n", name, ok ? 1 : 0);
    if (!ok) failures++;
}

int main(void) {
    const float speeds[NSPEED] = { 0.25f, 2.0f, 6.0f };
    const char* tag[NSPEED] = { "slow", "medium", "fast" };
    float grid[NGRID];
    for (int i = 0; i < NGRID; i++) grid[i] = i == 0 ? 0.0f : 0.02f * (float)i;

    FILE* csv = fopen("results.csv", "w");
    if (!csv) { perror("results.csv"); return 2; }
    fprintf(csv, "speed,beta,trail_meas,trail_pred,flicker,mode\n");

    /* fixed-weight sweep over all three speeds */
    Run tab[NSPEED][NGRID];
    for (int s = 0; s < NSPEED; s++) {
        for (int i = 0; i < NGRID; i++) {
            float b = grid[i];
            tab[s][i] = simulate(speeds[s], b, b);
            fprintf(csv, "%.2f,%.2f,%.3f,%.3f,%d,fixed\n",
                    speeds[s], b, tab[s][i].trail,
                    speeds[s] * neff(b), tab[s][i].flick);
        }
    }

    printf("%-7s %8s %8s %9s %9s %8s %10s\n",
           "speed", "beta_max", "beta*", "trail_p", "trail_m", "flick", "conflict");
    for (int s = 0; s < NSPEED; s++) {
        float v = speeds[s];
        float bmax = beta_max(v);

        int istar = 0;                   /* largest grid beta <= bmax */
        for (int i = 0; i < NGRID; i++)
            if (grid[i] <= bmax + 1e-6f) istar = i;
        float bstar = grid[istar];

        int iflick = -1;                /* smallest grid beta with zero flicker */
        for (int i = 0; i < NGRID; i++)
            if (tab[s][i].flick == 0) { iflick = i; break; }

        Run r = tab[s][istar];
        int conflict = (r.flick > 0);
        printf("%-7s %8.3f %8.2f %9.3f %9.3f %8d %10d\n",
               tag[s], bmax, bstar, v * neff(bstar), r.trail, r.flick, conflict);
        printf("  flicker-first alternative: beta=%.2f trail=%.3fpx budget=%.1fpx pass=%d\n",
               iflick >= 0 ? grid[iflick] : -1.0f,
               iflick >= 0 ? tab[s][iflick].trail : -1.0f,
               BUDGET, iflick >= 0 && tab[s][iflick].trail <= BUDGET);

        check("trail_within_budget", r.trail <= BUDGET + 0.5f);
    }

    /* motion-adaptive policy: clamp history while moving, full weight at rest */
    printf("\nadaptive policy (beta clamped in motion, 0.98 at rest):\n");
    for (int s = 0; s < NSPEED; s++) {
        float v = speeds[s];
        float bmove = beta_max(v);
        if (bmove > 0.98f) bmove = 0.98f;
        if (bmove < 0.0f) bmove = 0.0f;
        Run r = simulate(v, bmove, 0.98f);
        fprintf(csv, "%.2f,%.3f,%.3f,%.3f,%d,adaptive\n",
                v, bmove, r.trail, v * neff(bmove), r.flick);
        printf("%-7s beta_move=%.3f trail=%.3fpx flicker=%d rest_trail=%d\n",
               tag[s], bmove, r.trail, r.flick, r.rest_trail);
        check("adaptive_trail", r.trail <= BUDGET + 0.5f);
        check("adaptive_no_flicker", r.flick == 0);
        /* at rest only the 1/3-coverage PCF half-texel remains below EPS:
         * the history ghost itself has fully decayed. */
        check("stop_clears_trail", r.rest_trail <= 1);
    }
    fclose(csv);

    /* analytic-vs-simulation checks */
    Run q;
    q = simulate(2.0f, 0.5f, 0.5f);
    check("analytic_match_050", fabsf(q.trail - 2.0f * neff(0.5f)) <= 0.25f * 2.0f * neff(0.5f) + 0.5f);
    q = simulate(2.0f, 0.8f, 0.8f);
    check("analytic_match_080", fabsf(q.trail - 2.0f * neff(0.8f)) <= 0.25f * 2.0f * neff(0.8f) + 0.5f);

    check("no_history_no_trail", tab[1][0].trail < 0.5f);
    check("trail_grows_with_weight", tab[1][40].trail > tab[1][10].trail);
    check("flicker_falls_with_weight", tab[1][10].flick > tab[1][45].flick);

    float h = 0.0f;
    for (int f = 0; f < 500; f++) h = 0.9f * h + 0.1f * 0.5f;
    check("ema_converges", fabsf(h - 0.5f) < 0.01f);

    printf("all_ok=%d\n", failures == 0);
    return failures == 0 ? 0 : 1;
}
