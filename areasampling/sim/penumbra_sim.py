#!/usr/bin/env python3
"""Area-light sample-count vs penumbra-quality simulation.

Model: a disk softbox above a receiver, an infinite straight occluder edge.
Each light sample casts one hard shadow; the pixel averages N binary
visibility tests, exactly the same N-tap averaging as pcf() in pcf/src/pcf.c.

Outputs (written next to this script under ../results):
  - results_fixed.csv     fixed-pattern (staircase) measurements, 3 light sizes
  - results_stochastic.csv MC / stratified noise and monotonicity
  - fig01_tradeoff.png    smoothness and per-pixel lookup cost vs N
  - fig02_profiles.png    quantized penumbra profiles at several N
  - fig03_step_vs_n.png   max adjacent luminance step vs N, 3 light sizes
  - fig04_blotches.png    2D disk-occluder render: blotches at low N
  - fig05_mc_noise.png    RMS error vs N with sqrt(N) reference slopes
Run with: python3 -I penumbra_sim.py
"""
import csv
import math
import os
import site
import sys

# Keep -I isolation (no cwd / PYTHONPATH on the import path) but make the
# user site-packages with numpy/matplotlib available by absolute path.
_sys_sp = os.path.join(site.getuserbase(), "lib",
                       "python%d.%d" % sys.version_info[:2], "site-packages")
if os.path.isdir(_sys_sp) and _sys_sp not in sys.path:
    sys.path.append(_sys_sp)

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

# --- validated categorical palette (dataviz skill, light mode) ---
C_BLUE, C_ORANGE, C_AQUA = "#2a78d6", "#eb6834", "#1baf7a"
C_VIOLET, C_RED, C_INK = "#6250d6", "#e34948", "#0b0b0b"
C_GRID = "#d9d8d3"
plt.rcParams.update({
    "font.size": 10,
    "axes.edgecolor": "#9a9893",
    "axes.labelcolor": C_INK,
    "text.color": C_INK,
    "xtick.color": "#52514e",
    "ytick.color": "#52514e",
    "axes.grid": True,
    "grid.color": C_GRID,
    "grid.linewidth": 0.8,
    "figure.dpi": 130,
})

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "results")
os.makedirs(OUT, exist_ok=True)

rng = np.random.default_rng(20261127)

# ---------------------------------------------------------------- geometry
# Light is a unit disk in its own plane; the occluder edge clips it with a
# line at coordinate t in [-1, 1] (t=-1 fully shadowed, t=+1 fully lit).
def visibility_true(t):
    """Exact lit fraction of a unit disk clipped at u = t."""
    t = np.clip(t, -1.0, 1.0)
    return 0.5 + (t * np.sqrt(1.0 - t * t) + np.arcsin(t)) / math.pi


def concentric(u, v):
    """Shirley-Chiu square [-1,1]^2 -> unit disk mapping."""
    u = np.asarray(u, dtype=float)
    v = np.asarray(v, dtype=float)
    x = np.zeros_like(u)
    y = np.zeros_like(u)
    nz = ~((u == 0.0) & (v == 0.0))
    pi4 = math.pi / 4.0
    ax = np.abs(u) >= np.abs(v)
    m = nz & ax
    r = u[m]
    th = pi4 * (v[m] / u[m])
    x[m], y[m] = r * np.cos(th), r * np.sin(th)
    m = nz & ~ax
    r = v[m]
    th = math.pi / 2.0 - pi4 * (u[m] / v[m])
    x[m], y[m] = r * np.cos(th), r * np.sin(th)
    return x, y


def light_samples(n, mode):
    """N sample points in the unit disk. mode: fixed | stratified | iid."""
    side = int(math.isqrt(n))
    if side * side != n:
        raise ValueError("this simulation uses square N for grid patterns")
    i, j = np.meshgrid(np.arange(side), np.arange(side), indexing="ij")
    if mode == "fixed":
        u = (i.ravel() + 0.5) / side * 2.0 - 1.0
        v = (j.ravel() + 0.5) / side * 2.0 - 1.0
    elif mode == "stratified":
        u = (i.ravel() + rng.random(n)) / side * 2.0 - 1.0
        v = (j.ravel() + rng.random(n)) / side * 2.0 - 1.0
    elif mode == "iid":
        u = rng.random(n) * 2.0 - 1.0
        v = rng.random(n) * 2.0 - 1.0
    else:
        raise ValueError(mode)
    return concentric(u, v)


def profile(sample_u, t_values):
    """V(t) = fraction of samples with u < t (lit side of the edge)."""
    t_values = np.asarray(t_values)
    return np.mean(sample_u[None, :] < t_values[:, None], axis=1)


def quantize(v):
    return np.clip(np.rint(v * 255.0), 0, 255).astype(int)


def step_metrics(q):
    """Monotonicity violations and max adjacent step in 8-bit code values."""
    d = np.diff(q)  # lit -> shadow scan direction is decreasing; sign handled by caller
    return int(np.sum(d > 0)), int(np.max(np.abs(d)))


# ---------------------------------------------------------------- scan grids
FINE_PTS = 2000                 # resolves every individual tap crossing
t_fine = np.linspace(-1.0, 1.0, FINE_PTS)
# Three softbox sizes: projected penumbra width on the receiver, in pixels.
SIZES = [("small", 32), ("medium", 128), ("large", 256)]
N_GRID = [4, 9, 16, 25, 36, 49, 64, 81, 100, 121, 144, 196, 225, 256,
          400, 625, 900, 1600, 2500, 4096]
N_NOISE = [16, 36, 64, 100, 144, 256, 400, 900, 1600, 2500, 4096]
STEP_LIMIT = 2                 # code values: adjacent-step invisibility budget
RUNS = 40                      # stochastic realizations per measurement


def intrinsic_stair_steps(sample_u):
    """Exact quantized jumps at the tap crossings (no pixel-grid aliasing).

    Samples sharing the same u coordinate cross together; the code jump is
    255 times the size of each crossing group divided by N, rounded.
    """
    su = np.sort(np.asarray(sample_u))
    groups = np.split(su, np.where(np.diff(su) > 1e-12)[0] + 1)
    n = su.size
    return max(int(round(255.0 * g.size / n)) for g in groups)


# ------------------------------------------------- 1. fixed-pattern table
fixed_rows = []
fine_step = {}
for n in N_GRID:
    sx, sy = light_samples(n, "fixed")
    fine_step[n] = intrinsic_stair_steps(sx)
    for name, wpx in SIZES:
        t_px = np.linspace(-1.0, 1.0, wpx)
        q = quantize(profile(np.sort(sx), t_px))
        viol, mx = step_metrics(-q)  # expect non-increasing lit -> shadow
        ok = viol == 0 and mx <= STEP_LIMIT
        fixed_rows.append({
            "light": name, "penumbra_px": wpx, "N": n,
            "monotone_violations": viol, "pixel_grid_max_step": mx,
            "intrinsic_max_step": fine_step[n], "pass": int(ok),
        })

with open(os.path.join(OUT, "results_fixed.csv"), "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(fixed_rows[0].keys()))
    w.writeheader()
    w.writerows(fixed_rows)

# --------------------------------------------- 2. stochastic noise table
stoch_rows = []
for mode in ("iid", "stratified"):
    for n in N_NOISE:
        rms_runs, step_runs, viol_runs = [], [], []
        for _ in range(RUNS):
            sx, _ = light_samples(n, mode)
            v = profile(sx, t_fine)
            err = (v - visibility_true(t_fine)) * 255.0
            rms_runs.append(np.sqrt(np.mean(err ** 2)))
            q = quantize(v)
            viol, mx = step_metrics(-q)
            step_runs.append(mx)
            viol_runs.append(viol)
        stoch_rows.append({
            "sampler": mode, "N": n,
            "rms_codes_median": float(np.median(rms_runs)),
            "max_step_codes_median": float(np.median(step_runs)),
            "monotone_violations_mean": float(np.mean(viol_runs)),
        })

with open(os.path.join(OUT, "results_stochastic.csv"), "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(stoch_rows[0].keys()))
    w.writeheader()
    w.writerows(stoch_rows)

# --------------------------------------------- 3. analytic recommendation
N_STAIR = math.ceil(255.0 / STEP_LIMIT)          # 128
N_MC = math.ceil((127.5 / STEP_LIMIT) ** 2)      # 4064, 1-sigma criterion
W_MIN = math.ceil(255.0 / STEP_LIMIT)            # 128 px penumbra

# ------------------------------------------------------------ fig01 tradeoff
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.2))
ns = np.logspace(0.4, 3.7, 400)
ax1.loglog(ns, 255.0 / ns, color=C_BLUE, lw=2,
           label="fixed-pattern band $\\Delta_L=255/N$ (code)")
ax1.loglog(ns, 127.5 / np.sqrt(ns), color=C_ORANGE, lw=2,
           label="MC noise $\\sigma_L=127.5/\\sqrt{N}$ (code)")
ax1.axhline(STEP_LIMIT, color=C_RED, ls="--", lw=1.6, label="invisibility budget = 2 codes")
ax1.axvline(N_STAIR, color=C_BLUE, ls=":", lw=1.4)
ax1.axvline(N_MC, color=C_ORANGE, ls=":", lw=1.4)
ax1.annotate(f"$N\\geq{N_STAIR}$", (N_STAIR, 80), ha="center", color=C_BLUE)
ax1.annotate(f"$N\\geq{N_MC}$", (N_MC, 80), ha="center", color=C_ORANGE)
ax1.set(xlabel="light samples N", ylabel="luminance error (8-bit code)",
        title="Smoothness: error vs sample count")
ax1.legend(frameon=False, fontsize=8.5)

ax2.plot(ns, ns, color=C_AQUA, lw=2, label="shadow-map lookups per pixel = N")
ax2.axvspan(N_STAIR, ns[-1], color=C_BLUE, alpha=0.08,
            label=f"feasible for fixed/stratified ($N\\geq{N_STAIR}$)")
ax2.axvline(N_MC, color=C_ORANGE, ls=":", lw=1.4,
            label=f"pure-MC bound $N\\geq{N_MC}$")
ax2.set(xlabel="light samples N", ylabel="lookups per pixel",
        title="Cost: table lookups vs sample count")
ax2.legend(frameon=False, fontsize=8.5)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig01_tradeoff.png"), bbox_inches="tight")
plt.close(fig)

# ------------------------------------------------------------ fig02 profiles
fig, ax = plt.subplots(figsize=(9, 4.6))
tt = np.linspace(-1, 1, 600)
ax.plot(tt, visibility_true(tt) * 255, color=C_INK, lw=1.8,
        label="analytic area-light profile")
for n, c in ((9, C_RED), (16, C_ORANGE), (64, C_VIOLET), (256, C_BLUE)):
    sx, _ = light_samples(n, "fixed")
    q = quantize(profile(np.sort(sx), tt))
    ax.step(tt, q, where="post", lw=1.3, color=c, label=f"N={n} quantized")
ax.set(xlabel="penumbra position (normalized)", ylabel="luminance (0-255)",
       title="Fixed-pattern penumbra: staircase steps shrink as 255/N")
ax.legend(frameon=False, fontsize=9)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig02_profiles.png"), bbox_inches="tight")
plt.close(fig)

# ------------------------------------------------------- fig03 step vs N
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.2), sharex=True)
colors = {"small": C_RED, "medium": C_ORANGE, "large": C_BLUE}
ax1.plot(N_GRID, [fine_step[n] for n in N_GRID], "-o", color=C_AQUA, ms=4,
         label="exact tap staircase $\\lceil 255/N\\rceil$")
ax1.axhline(STEP_LIMIT, color=C_RED, ls="--", lw=1.6, label="2-code limit")
ax1.set(xlabel="N", ylabel="max adjacent step (code)",
        title="Sampler bound: converges like 255/N (all light sizes)")
ax1.legend(frameon=False, fontsize=9)
for name, wpx in SIZES:
    ys = [next(r["pixel_grid_max_step"] for r in fixed_rows
               if r["light"] == name and r["N"] == n) for n in N_GRID]
    floor = 255.0 * 4.0 / (math.pi * wpx)
    ax2.plot(N_GRID, ys, "-o", ms=4, color=colors[name],
             label=f"{name}, penumbra {wpx}px")
    ax2.axhline(floor, color=colors[name], ls=":", lw=1.2, alpha=0.8)
ax2.axhline(STEP_LIMIT, color=C_RED, ls="--", lw=1.6, label="2-code limit")
ax2.set(xlabel="N", ylabel="max adjacent step (code)",
        title="Pixel-grid bound: dotted floors $1020/(\\pi W)$ cannot be fixed by N")
ax2.legend(frameon=False, fontsize=9)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig03_step_vs_n.png"), bbox_inches="tight")
plt.close(fig)

# ------------------------------------------------------- fig04 2D blotches
def render_disk_shadow(n, light_radius, grid=240, H=1.0, h_o=0.5, r_o=0.55,
                       mode="fixed"):
    """Disk occluder, disk softbox; per-pixel N light samples (vectorized)."""
    span = 2.6
    p = np.linspace(-span, span, grid) / H
    xx, yy = np.meshgrid(p, p)
    alpha = h_o / H
    # blocked disk in light-plane coords: center c, radius rb
    c = -((1.0 - alpha) / alpha) * np.stack([xx, yy], axis=-1)
    rb = r_o / alpha
    side = int(math.isqrt(n))
    if mode == "fixed":
        sx, sy = light_samples(n, "fixed")
    else:
        i, j = np.meshgrid(np.arange(side), np.arange(side), indexing="ij")
        u = (i.ravel() + rng.random(n)) / side * 2 - 1
        v = (j.ravel() + rng.random(n)) / side * 2 - 1
        sx, sy = concentric(u, v)
    sx = sx * light_radius
    sy = sy * light_radius
    dx = sx[None, None, :] - c[..., 0, None]
    dy = sy[None, None, :] - c[..., 1, None]
    blocked = dx * dx + dy * dy <= rb * rb
    img = 255.0 * (1.0 - np.mean(blocked, axis=2))
    return np.clip(np.rint(img), 0, 255).astype(np.uint8)


fig, axes = plt.subplots(1, 3, figsize=(14, 4.8))
panels = (
    (axes[0], 9, "fixed", "N=9 fixed: overlapping hard discs -> blotches"),
    (axes[1], 144, "fixed", "N=144 fixed: continuous penumbra, static banding"),
    (axes[2], 64, "stratified", "N=64 stratified: jitter breaks the bands"),
)
for ax, n, mode, title in panels:
    img = render_disk_shadow(n, light_radius=0.9, mode=mode)
    ax.imshow(img, cmap="gray", vmin=0, vmax=255)
    ax.set_title(title, fontsize=10)
    ax.set_xticks([]); ax.set_yticks([])
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig04_blotches.png"), bbox_inches="tight")
plt.close(fig)

# ------------------------------------------------------- fig05 MC noise
fig, ax = plt.subplots(figsize=(8, 4.6))
for mode, c, marker in (("iid", C_ORANGE, "o"), ("stratified", C_BLUE, "s")):
    rows = [r for r in stoch_rows if r["sampler"] == mode]
    nn = [r["N"] for r in rows]
    rr = [r["rms_codes_median"] for r in rows]
    ax.loglog(nn, rr, marker=marker, ms=5, lw=1.6, color=c,
              label=f"{mode} measured RMS")
ref_n = np.array(N_NOISE, dtype=float)
ax.loglog(ref_n, 127.5 / np.sqrt(ref_n), "--", color=C_ORANGE, lw=1.2,
          alpha=0.7, label=r"$127.5/\sqrt{N}$ reference, slope $-1/2$")
ax.loglog(ref_n, 127.5 / ref_n ** 0.75, "--", color=C_BLUE, lw=1.2,
          alpha=0.7, label=r"stratified edge term $\propto N^{-3/4}$")
ax.axhline(STEP_LIMIT, color=C_RED, ls="--", lw=1.6, label="2-code budget")
ax.set(xlabel="N", ylabel="RMS luminance error (code)",
       title="Stochastic sampling: measured penumbra noise vs N")
ax.legend(frameon=False, fontsize=9)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "fig05_mc_noise.png"), bbox_inches="tight")
plt.close(fig)

# ------------------------------------------------------------ console report
print("== analytic bounds ==")
print(f"staircase bound  N >= 255/2 = {N_STAIR}")
print(f"MC 1-sigma bound N >= (127.5/2)^2 = {N_MC}")
print(f"geometric bound  penumbra width >= {W_MIN} px")
print("\n== fixed-pattern failures (max step > 2 or non-monotone) ==")
for r in fixed_rows:
    if not r["pass"]:
        print(f"{r['light']:7s} W={r['penumbra_px']:4d}px N={r['N']:4d} "
              f"pixel_step={r['pixel_grid_max_step']:3d} viol={r['monotone_violations']} "
              f"intrinsic_step={r['intrinsic_max_step']}")
print("\n== smallest passing N per light size (pixel-grid criterion) ==")
for name, wpx in SIZES:
    passes = [r["N"] for r in fixed_rows
              if r["light"] == name and r["pass"]]
    print(f"{name:7s} W={wpx:4d}px -> "
          + (f"N_min={min(passes)}" if passes
             else f"no N passes (geometric floor {255*4/(math.pi*wpx):.2f} codes/px)"))
print("\n== intrinsic sampler bound (exact tap staircase) ==")
intrinsic_pass = [n for n in N_GRID if fine_step[n] <= STEP_LIMIT]
print(f"first N with intrinsic step <= {STEP_LIMIT}: N={min(intrinsic_pass)} "
      f"(bound says N >= {N_STAIR})")
print("\n== stochastic RMS at key N ==")
for r in stoch_rows:
    if r["N"] in (144, 400, 1600, 4096):
        print(f"{r['sampler']:11s} N={r['N']:5d} rms={r['rms_codes_median']:6.2f} "
              f"codes, median max step={r['max_step_codes_median']:.1f}, "
              f"mean monotone violations={r['monotone_violations_mean']:.1f}")
print(f"\noutputs written to {os.path.abspath(OUT)}")
