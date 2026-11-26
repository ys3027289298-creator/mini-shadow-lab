# -*- coding: utf-8 -*-
"""
Temporal sub-pixel jitter for shadow edges — core simulation.

1D scanline across a shadow edge, units = shadow-map texels.

Physics
  A light of angular *diameter* alpha, blocker-to-receiver gap g casts a
  penumbra of full width  w_pen = 2 g tan(alpha/2)  (world).
  In texels: rho = w_pen / texel_world  — the penumbra / sampling-limit ratio.
  Ground-truth coverage P(x) is a linear ramp of width rho centred on the edge
  (uniform disk light -> linear 1D profile).

Per-frame hard shadow-map sample (jitter s_f in texels, spatial PCF taps t_k)
  B_f(x_i) = (1/K) sum_k 1[ x_i + t_k - s_f >= edge ]
  Temporal accumulation: C_N(x_i) = (1/N) sum_f B_f(x_i).

  K = 1 (r = 0) is temporal-only: it can only ever build a 1-texel-wide ramp,
  no matter how wide the real penumbra is. r >= 1 is the spatial-temporal
  hybrid used once rho exceeds one texel.

Stair amplitude = max per-texel jump |C_N(i+1) - C_N(i)|; the unjittered
single-frame hard edge has amplitude 1. The task gate is amplitude < 0.30.
"""
import json
import math
import os

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
os.makedirs(DATA, exist_ok=True)

RNG = np.random.default_rng(20261127)

# ---------------------------------------------------------------- reference scene
LIGHT_SPAN_M = 40.0          # directional cascade ground span
SHADOW_RES = 2048
TEXEL_M = LIGHT_SPAN_M / SHADOW_RES          # 0.01953 m (~1.95 cm/texel)
GAP_M = 3.0                                  # canopy -> ground
TIERS_DEG = [0.5, 2.0, 8.0]                  # moon / medium / large
N_MAX = 64
N_GRID = np.arange(1, N_MAX + 1)
TARGET = 0.30
TRIALS = 200


# ---------------------------------------------------------------- jitter sequences
def halton_1d(index: int, base: int) -> float:
    f, r = 1.0, 0.0
    i = index
    while i > 0:
        f /= base
        r += f * (i % base)
        i //= base
    return r


def halton_seq(n: int, base: int = 2) -> np.ndarray:
    return np.array([halton_1d(11 + k, base) for k in range(n)])


def golden_r2_seq(n: int) -> np.ndarray:
    g = 0.5 * (math.sqrt(5.0) + 1.0)
    return (np.arange(1, n + 1) / g) % 1.0


# ---------------------------------------------------------------- physics
def penumbra_world_m(alpha_deg: float, gap_m: float) -> float:
    return 2.0 * gap_m * math.tan(math.radians(alpha_deg / 2.0))


def rho_rows():
    rows = []
    for alpha in TIERS_DEG:
        for gap in (0.5, 3.0, 10.0):
            for res in (1024, 2048, 4096, 8192):
                texel = LIGHT_SPAN_M / res
                w = penumbra_world_m(alpha, gap)
                rows.append({"alpha_deg": alpha, "gap_m": gap, "res": res,
                             "texel_m": texel, "penumbra_m": w, "rho": w / texel})
    return rows


def truth_coverage(x: np.ndarray, edge: float, rho: float) -> np.ndarray:
    """Linear ramp of width rho centred on the edge (uniform light disk)."""
    if rho <= 1e-12:
        return (x >= edge).astype(float)
    return np.clip((x - edge) / rho + 0.5, 0.0, 1.0)


# ---------------------------------------------------------------- edge simulation
def taps(radius: int) -> np.ndarray:
    return np.arange(-radius, radius + 1, dtype=float)


def frame_values(x: np.ndarray, edge: float, s: float, t: np.ndarray) -> np.ndarray:
    """Hard shadow-map tests, jittered sample position, averaged over PCF taps."""
    pos = x[:, None] + t[None, :] - s              # (texels, taps)
    return (pos >= edge).mean(axis=1)


def accumulate(x, edge, rho, jitter_u, radius):
    """Running temporal average C_N for every N = 1..len(jitter_u)."""
    t = taps(radius)
    n = len(jitter_u)
    frames = np.empty((n, len(x)))
    for f in range(n):
        frames[f] = frame_values(x, edge, jitter_u[f], t)
    csum = np.cumsum(frames, axis=0)
    return csum / np.arange(1, n + 1)[:, None]


def stair_amplitude(cn: np.ndarray) -> np.ndarray:
    return np.abs(np.diff(cn, axis=1)).max(axis=1)


def transition_width(cn: np.ndarray) -> np.ndarray:
    return ((cn > 0.02) & (cn < 0.98)).sum(axis=1).astype(float)


def linf_error(cn: np.ndarray, truth: np.ndarray) -> np.ndarray:
    return np.max(np.abs(cn - truth[None, :]), axis=1)


def run(seq: str, rho: float, radius: int, n_trials: int):
    """Average metrics over random sub-texel edge phases."""
    x = np.arange(-16, 17, dtype=float)
    amp = np.zeros(N_MAX)
    width = np.zeros(N_MAX)
    err = np.zeros(N_MAX)
    flicker = np.zeros(N_MAX)
    for trial in range(n_trials):
        edge = RNG.uniform(-4.0, 4.0)
        truth = truth_coverage(x, edge, rho)
        if seq == "halton":
            u = halton_seq(N_MAX)
        elif seq == "golden":
            u = golden_r2_seq(N_MAX)
        elif seq == "white":
            u = RNG.uniform(size=N_MAX)
        elif seq == "none":
            u = np.zeros(N_MAX)
        else:
            raise ValueError(seq)
        cn = accumulate(x, edge, rho, u, radius)
        amp += stair_amplitude(cn)
        width += transition_width(cn)
        err += linf_error(cn, truth)
        # perceived flicker: max inter-frame jump of the displayed image,
        # |C_N - C_{N-1}| (first frame counts as a full hard-edge change)
        t = taps(radius)
        frames = np.array([frame_values(x, edge, u[f], t) for f in range(N_MAX)])
        inter = np.empty(N_MAX)
        inter[0] = 1.0
        for k in range(1, N_MAX):
            inter[k] = np.abs(cn[k] - cn[k - 1]).max()
        flicker += inter
    return {"amp": (amp / n_trials).tolist(),
            "width": (width / n_trials).tolist(),
            "linf": (err / n_trials).tolist(),
            "flicker": (flicker / n_trials).tolist()}


# ---------------------------------------------------------------- sequence quality
def star_discrepancy_1d(points: np.ndarray) -> float:
    q = np.sort(points)
    n = len(q)
    d = 0.0
    for i, p in enumerate(q, start=1):
        d = max(d, abs(i / n - p), abs((i - 1) / n - p))
    return max(d, abs(1.0 - q[-1]))


def autocorr(points: np.ndarray, lag: int) -> float:
    u = points - points.mean()
    denom = np.dot(u, u)
    if denom < 1e-15:
        return 1.0 if lag == 0 else 0.0
    return float(np.dot(u[:-lag], u[lag:]) / denom)


def sequence_quality():
    out = {}
    rng = np.random.default_rng(7)
    for name, fn in (("halton", lambda n: halton_seq(n, 2)),
                     ("golden", golden_r2_seq),
                     ("white", lambda n: rng.uniform(size=n))):
        disc, ac1, ac2 = [], [], []
        for n in N_GRID:
            p = fn(int(n))
            disc.append(star_discrepancy_1d(p))
            ac1.append(autocorr(p, 1))
            ac2.append(autocorr(p, 2))
        out[name] = {"n": N_GRID.tolist(), "discrepancy": disc,
                     "ac_lag1": ac1, "ac_lag2": ac2}
    return out


def first_n_below(amp: np.ndarray, target: float):
    below = np.nonzero(amp < target)[0]
    return int(below[0] + 1) if len(below) else None


def main():
    tier_rho = {}
    for alpha in TIERS_DEG:
        w = penumbra_world_m(alpha, GAP_M)
        tier_rho[str(alpha)] = {
            "penumbra_m": w,
            "rho": w / TEXEL_M,
            "rho_4x": w / (LIGHT_SPAN_M / (SHADOW_RES * 4)),
        }

    # spatial kernel: radius r -> K = 2r+1 taps, covers K texels ~ penumbra.
    # moon gets two options: tight 3-tap (matches penumbra) and 5-tap (passes gate).
    radii_for = {0.5: [1, 2], 2.0: [2], 8.0: [10]}

    conv = {}
    for alpha in TIERS_DEG:
        rho = tier_rho[str(alpha)]["rho"]
        conv[str(alpha)] = {"rho": rho, "modes": {}}
        conv[str(alpha)]["modes"]["temporal_only"] = {
            "radius": 0, "kernel_texels": 1,
            "halton": run("halton", rho, 0, 16),
            "golden": run("golden", rho, 0, 16),
            "white": run("white", rho, 0, TRIALS),
            "none": run("none", rho, 0, 4),
        }
        for r in radii_for[alpha]:
            tag = f"hybrid_r{r}"
            conv[str(alpha)]["modes"][tag] = {
                "radius": r, "kernel_texels": 2 * r + 1,
                "halton": run("halton", rho, r, 16),
                "golden": run("golden", rho, r, 16),
                "white": run("white", rho, r, TRIALS),
            }

    # frame-count decision, two motion speeds, trail budget 2 texels.
    # stair gate: max per-texel jump < 0.30.  flicker gate: running per-texel
    # temporal std < 0.10 (the residual noise between frames once converged).
    motions = [0.25, 0.5]
    trail_budget = 2.0
    flicker_target = 0.10
    frames = {}
    for alpha in TIERS_DEG:
        frames[str(alpha)] = {}
        for tag, entry in conv[str(alpha)]["modes"].items():
            amp = np.array(entry["halton"]["amp"])
            width = np.array(entry["halton"]["width"])
            flicker = np.array(entry["halton"]["flicker"])
            n_stair = first_n_below(amp, TARGET)
            n_flicker = first_n_below(flicker, flicker_target)
            # stair is the hard task gate; flicker only adds frames on top.
            # If the stair gate is unreachable (quantisation floor), report None.
            n_need = (max(n_stair, n_flicker)
                      if (n_stair is not None and n_flicker is not None)
                      else (n_stair if n_stair is not None else None))
            picks = {"radius": entry["radius"],
                     "kernel_texels": entry["kernel_texels"],
                     "frames_needed_by_stair": n_stair,
                     "frames_needed_by_flicker": n_flicker,
                     "frames_needed": n_need,
                     "amp_at_need": (float(amp[n_need - 1]) if n_need else None),
                     "flicker_at_need": (float(flicker[n_need - 1]) if n_need else None),
                     "width_at_need": (float(width[n_need - 1]) if n_need else None),
                     "by_motion": {}}
            for v in motions:
                n_trail = int(math.floor(trail_budget / v)) + 1
                chosen = n_need if (n_need is not None and n_need <= n_trail) else None
                picks["by_motion"][f"v={v}"] = {
                    "motion_texels_per_frame": v,
                    "frames_allowed_by_trail": n_trail,
                    "frames_chosen": chosen,
                    "residual_amp": float(amp[(chosen or min(n_trail, N_MAX)) - 1]),
                    "residual_flicker": float(flicker[(chosen or min(n_trail, N_MAX)) - 1]),
                    "residual_width_texels": float(width[(chosen or min(n_trail, N_MAX)) - 1]),
                    "meets_target": chosen is not None,
                }
            frames[str(alpha)][tag] = picks

    summary = {
        "scene": {"light_span_m": LIGHT_SPAN_M, "shadow_res": SHADOW_RES,
                  "texel_m": TEXEL_M, "gap_m": GAP_M, "n_max": N_MAX,
                  "trials": TRIALS, "target": TARGET,
                  "flicker_target": flicker_target,
                  "trail_budget_texels": trail_budget},
        "rho_rows": rho_rows(),
        "tier_rho": tier_rho,
        "convergence": conv,
        "frames": frames,
        "quality": sequence_quality(),
    }
    with open(os.path.join(DATA, "sim_results.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)

    print(f"texel={TEXEL_M * 100:.2f} cm  gap={GAP_M} m")
    for alpha in TIERS_DEG:
        t = tier_rho[str(alpha)]
        print(f"alpha={alpha:>3}  penumbra={t['penumbra_m']*100:6.2f} cm  "
              f"rho={t['rho']:6.3f}  rho@4x={t['rho_4x']:6.3f}")
    print()
    for alpha in TIERS_DEG:
        for tag, picks in frames[str(alpha)].items():
            print(f"alpha={alpha:>3} {tag:>15} K={picks['kernel_texels']:>2}: "
                  f"need(stair={picks['frames_needed_by_stair']},flick={picks['frames_needed_by_flicker']}) "
                  f"-> N={picks['frames_needed']}")
            for k, v in picks["by_motion"].items():
                print(f"    {k:>7} cap={v['frames_allowed_by_trail']} chosen={v['frames_chosen']} "
                      f"amp={v['residual_amp']:.3f} flick={v['residual_flicker']:.3f} "
                      f"width={v['residual_width_texels']:.1f} ok={v['meets_target']}")
        print()


if __name__ == "__main__":
    main()
