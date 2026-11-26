
/* follow: player-followed shadow tile refresh and buffer sizing.
 *
 * Models the hysteresis rule that re-centres a moving orthographic shadow
 * tile, counts the shadow disappear/reappear flicker for 1..N buffered
 * depth textures, and back-solves tile coverage from the measured period.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ---- scenario configuration (representative open-world numbers) ------ */
#define DT        (1.0f / 60.0f)  /* frame period, s                 */
#define SIM_T     120.0f         /* simulated run length, s          */
#define TILE_W    24.0f          /* shadow tile side length, m       */
#define ALPHA     0.5f           /* trigger ratio of the half-extent */
#define RES       2048           /* tile resolution per axis         */
#define BYTES_PX  4              /* D32F depth texture               */
#define VIEW_D    150.0f         /* character view distance, m       */
#define MAXB      4              /* max buffers simulated            */

/* per-refresh step the player may move before a redraw is triggered */
#define D0        (ALPHA * TILE_W / 2.0f)
#define TEX_MIB   ((long)RES * RES * BYTES_PX / (1024L * 1024L))

/* redraw cost in frames: a normal heavy shadow pass and a stress pass */
#define RD_NORMAL 2
#define RD_STRESS 36

enum { FREE = 0, INFLIGHT, READY };

typedef struct {
    int state;
    int k;      /* grid index this slot renders: tile centre = k * D0 */
    int left;   /* frames until the render finishes */
} Slot;

typedef struct {
    int   refreshes;      /* tile re-centre cycles                    */
    int   flicker_events; /* cycles whose new tile was not ready      */
    int   missing_frames; /* frames the player had no valid shadow    */
    int   starved;        /* triggers with no buffer ready to swap    */
    float period_mean;    /* mean time between refreshes, s           */
    float coverage_back;  /* tile side back-solved from period, m     */
} SimResult;

/* constant-speed run along +x; `buffers` depth textures in the pool */
static void sim(float v, int buffers, int redraw_frames, SimResult* r) {
    memset(r, 0, sizeof(*r));
    int frames = (int)(SIM_T / DT);
    float x = 0.0f;                 /* player position */
    float last_t = -1.0f, per_sum = 0.0f;
    int render_left = 0;            /* B == 1: synchronous redraw */
    int active_k = 0;               /* grid index of the tile in use */
    int max_k = 0;                  /* last refresh boundary reached */
    int next_sched_k = 1;           /* next grid point to render */

    Slot slots[MAXB];
    for (int i = 0; i < MAXB; i++) slots[i].state = FREE;

    if (buffers >= 2) {
        /* pre-warm: these tiles were rendered during the load screen,
         * so they are already READY when the player starts moving */
        for (int b = 0; b < buffers - 1; b++) {
            slots[b].state = READY;
            slots[b].k = next_sched_k++;
            slots[b].left = 0;
        }
    }

    for (int f = 0; f < frames; f++) {
        float t = f * DT;
        x += v * DT;

        if (buffers == 1) {
            /* single buffer: the active texture is invalid while redrawing */
            if (render_left > 0) {
                --render_left;
                r->missing_frames++;
            }
            if (render_left == 0 && x - active_k * D0 >= D0) {
                render_left = redraw_frames;
                active_k++;   /* boundary consumed; content lags redraw */
                r->refreshes++;
                r->flicker_events++;
                if (last_t >= 0.0f) per_sum += t - last_t;
                last_t = t;
            }
        } else {
            /* 1. advance in-flight renders */
            for (int i = 0; i < buffers - 1; i++)
                if (slots[i].state == INFLIGHT && --slots[i].left == 0)
                    slots[i].state = READY;

            int need_k = (int)floorf(x / D0);  /* grid point required now */

            /* 2. refresh boundaries: one event per grid point crossed */
            while (max_k < need_k) {
                max_k++;
                r->refreshes++;
                if (last_t >= 0.0f) per_sum += t - last_t;
                last_t = t;
                int pick = -1;
                for (int i = 0; i < buffers - 1; i++)
                    if (slots[i].state == READY && slots[i].k == max_k) pick = i;
                if (pick >= 0) {                 /* swap exactly on time */
                    active_k = max_k;
                    slots[pick].state = FREE;
                } else {
                    r->flicker_events++;         /* needed tile not done */
                    r->starved++;
                }
            }

            /* 3. late catch-up: a starved tile finished after its boundary */
            for (;;) {
                int pick = -1;
                for (int i = 0; i < buffers - 1; i++) {
                    if (slots[i].state != READY) continue;
                    if (slots[i].k > active_k && slots[i].k <= need_k &&
                        (pick < 0 || slots[i].k > slots[pick].k))
                        pick = i;
                }
                if (pick < 0) break;
                active_k = slots[pick].k;
                slots[pick].state = FREE;
            }

            /* 4. recycle obsolete tiles and keep the pipeline full */
            for (int i = 0; i < buffers - 1; i++) {
                if (slots[i].state == READY && slots[i].k <= active_k)
                    slots[i].state = FREE;
                if (slots[i].state == FREE) {
                    slots[i].state = INFLIGHT;
                    slots[i].k = next_sched_k++;
                    slots[i].left = redraw_frames;
                }
            }
        }

        /* geometric truth: shadow exists only inside the active tile */
        if (fabsf(x - active_k * D0) > TILE_W / 2.0f) r->missing_frames++;
    }

    r->period_mean = r->refreshes > 1 ? per_sum / (r->refreshes - 1) : 0.0f;
    /* D = alpha*W/2 = v*T  =>  W = 2*v*T/alpha */
    r->coverage_back = 2.0f * v * r->period_mean / ALPHA;
}

static int failures = 0;
static void check(const char* name, int ok) {
    printf("%s=%d\n", name, ok ? 1 : 0);
    if (!ok) failures++;
}

static void print_row(const char* tag, float v, int b, const SimResult* r) {
    printf("  %-8s v=%4.1f B=%d  refresh=%3d  T=%6.3fs  Wback=%5.2fm "
           "flicker=%3d  miss_fr=%4d  mem=%2ldMiB\n",
           tag, v, b, r->refreshes, r->period_mean, r->coverage_back,
           r->flicker_events, r->missing_frames, (long)b * TEX_MIB);
}

int main(void) {
    const float speeds[3] = { 2.0f, 6.0f, 12.0f };   /* walk / run / sprint */
    const char* names[3] = { "walk", "run", "sprint" };

    printf("config: tile W=%.0fm alpha=%.2f step D=alpha*W/2=%.1fm  "
           "res=%d D32F=%ldMiB/tex  view=%.0fm (W/view=%.1f%%)\n",
           TILE_W, ALPHA, D0, RES, TEX_MIB, VIEW_D, 100.0 * TILE_W / VIEW_D);
    printf("theory: T = D/v = alpha*W/(2v)\n\n");

    int min_b_measured[3] = {0, 0, 0};

    for (int s = 0; s < 3; s++) {
        float v = speeds[s];
        float t_theory = D0 / v;
        int b_formula = 1 + (int)ceilf((RD_NORMAL * DT) / t_theory);
        printf("[%s v=%.0f m/s] theory T=%.3fs  formula B_min=1+ceil(td/T)=%d\n",
               names[s], v, t_theory, b_formula);
        for (int b = 1; b <= MAXB; b++) {
            SimResult r;
            sim(v, b, RD_NORMAL, &r);
            print_row("normal", v, b, &r);
            if (min_b_measured[s] == 0 && r.flicker_events == 0)
                min_b_measured[s] = b;
            if (b == 2) {
                check("period_matches_theory",
                      fabsf(r.period_mean - t_theory) < 2.0f * DT);
                check("coverage_back_solve",
                      fabsf(r.coverage_back - TILE_W) < 0.5f);
                check("double_buffer_no_flicker", r.flicker_events == 0);
                check("double_buffer_no_gap_frames", r.missing_frames == 0);
            }
            if (b == 1)
                check("single_buffer_always_flickers",
                      r.flicker_events == r.refreshes && r.refreshes > 0);
        }
        check("formula_predicts_min_buffers",
              min_b_measured[s] == b_formula && b_formula == 2);
        printf("  -> pick B=%d (extra textures=%d, memory x%d)\n\n",
               min_b_measured[s], min_b_measured[s] - 1, min_b_measured[s]);
    }

    /* stress: redraw longer than the sprint refresh interval */
    printf("[stress sprint v=12 m/s, redraw td=%.2fs = %d frames]\n",
           RD_STRESS * DT, RD_STRESS);
    float t_sprint = D0 / 12.0f;
    int b_stress = 1 + (int)ceilf((RD_STRESS * DT) / t_sprint);
    int stress_min = 0;
    for (int b = 1; b <= 3; b++) {
        SimResult r;
        sim(12.0f, b, RD_STRESS, &r);
        print_row("stress", 12.0f, b, &r);
        if (stress_min == 0 && r.flicker_events == 0) stress_min = b;
    }
    check("stress_formula_bmin3", b_stress == 3);
    SimResult r2;
    sim(12.0f, 2, RD_STRESS, &r2);
    check("stress_double_buffer_flickers", r2.flicker_events > 0);
    SimResult r3;
    sim(12.0f, 3, RD_STRESS, &r3);
    check("stress_triple_buffer_clean", r3.flicker_events == 0);
    check("stress_measured_matches_formula", stress_min == b_stress);

    printf("\nall_ok=%d\n", failures == 0);
    return failures == 0 ? 0 : 1;
}
