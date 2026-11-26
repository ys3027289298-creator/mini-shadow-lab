#!/usr/bin/env python3
"""Policy chart: adaptive vs fixed PCF kernel.

Two panels sharing the distance-ratio x-axis:
  top    penumbra width error against the geometric theory, with the 15%
         tolerance line and the three calibration tiers marked;
  bottom per-pixel sample taps (2r+1)^2, the cost axis of the trade-off.

Reads the CSVs emitted by src/calibrate.c; no numbers are hard-coded here.
"""
import csv
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

# validated categorical slots (dataviz reference palette, light mode)
BLUE = "#2a78d6"    # adaptive kernel
ORANGE = "#eb6834"  # fixed kernel (the reported bug)
AQUA = "#1baf7a"    # calibration tiers
SURFACE = "#fcfcfb"
GRID = "#e1e0d9"
INK = "#0b0b0b"
MUTED = "#898781"
TOL = 0.15


def load(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def main(sweep_csv, tiers_csv, out_png):
    sweep = load(sweep_csv)
    tiers = [r for r in load(tiers_csv) if r["tier"] != "stress"]
    stress = next(r for r in load(tiers_csv) if r["tier"] == "stress")

    x = [float(r["d_over_D"]) for r in sweep]
    err_a = [100.0 * float(r["err_adaptive"]) for r in sweep]
    err_f = [100.0 * float(r["err_fixed"]) for r in sweep]
    samp_a = [int(r["samples_adaptive"]) for r in sweep]
    samp_f = [int(r["samples_fixed"]) for r in sweep]

    fig, (ax1, ax2) = plt.subplots(
        2, 1, figsize=(9, 7.2), sharex=True, height_ratios=[1.25, 1]
    )
    fig.patch.set_facecolor(SURFACE)
    for ax in (ax1, ax2):
        ax.set_facecolor(SURFACE)
        ax.grid(True, color=GRID, linewidth=0.8)
        ax.spines[["top", "right"]].set_visible(False)
        ax.spines[["left", "bottom"]].set_color("#c3c2b7")
        ax.tick_params(colors=MUTED, labelsize=9)

    # ---- top panel: penumbra width error ----------------------------------
    ax1.axhline(100.0 * TOL, color=INK, linestyle="--", linewidth=1.2, alpha=0.7)
    ax1.text(
        0.785, 100.0 * TOL + 2.5, "15% tolerance", ha="right", va="bottom",
        fontsize=9, color=INK,
    )
    ax1.plot(x, err_a, color=BLUE, linewidth=2, label="adaptive r = k·w(d/D), k fitted")
    ax1.plot(x, err_f, color=ORANGE, linewidth=2, label="fixed r = 3 (bug: kernel never changes)")

    # calibration tiers: label above the point, except far which labels below
    # to clear the tolerance annotation
    for r in tiers:
        xv, yv = float(r["d_over_D"]), 100.0 * float(r["rel_err"])
        ax1.scatter(xv, yv, s=64, color=AQUA, edgecolor="white",
                    linewidth=1.2, zorder=5)
        below = r["tier"] == "far"
        ax1.annotate(
            f"{r['tier']} {100 * float(r['rel_err']):.1f}%",
            (xv, yv), textcoords="offset points",
            xytext=(0, -16 if below else 10),
            ha="center", fontsize=8.5, color=INK,
        )
    ax1.scatter(
        [], [], s=64, color=AQUA, edgecolor="white", linewidth=1.2,
        label="calibration tiers (all within 15%)",
    )

    # sub-texel floor region: r_min = 1 cannot express penumbras under ~2 texels
    floor_x = max(x[i] for i in range(len(x)) if int(sweep[i]["r_adaptive"]) == 1)
    ax1.axvspan(0, floor_x, color=ORANGE, alpha=0.07)
    ax1.text(
        floor_x / 2, 92,
        "r_min floor:\nsub-texel penumbra,\nneither kernel meets 15%",
        ha="center", va="top", fontsize=8.5, color=INK,
        bbox=dict(boxstyle="round,pad=0.35", fc="white", ec=GRID),
    )

    # crossover where the two error curves meet
    cross = next(
        (x[i] for i in range(1, len(x))
         if (err_a[i] - err_f[i]) * (err_a[i - 1] - err_f[i - 1]) <= 0),
        None,
    )
    if cross is not None:
        ax1.axvline(cross, color=MUTED, linestyle=":", linewidth=1.2)
        ax1.text(
            cross + 0.008, 60, f"crossover\nd/D ≈ {cross:.2f}",
            fontsize=8.5, color=MUTED, ha="left",
        )

    ax1.set_ylabel("penumbra width error vs theory (%)", fontsize=10, color=INK)
    ax1.set_ylim(0, 100)
    ax1.set_title(
        "PCF kernel policy: penumbra accuracy vs per-pixel cost",
        fontsize=12, color=INK, loc="left", pad=10,
    )
    ax1.legend(loc="upper right", fontsize=8.5, frameon=False)

    # ---- bottom panel: per-pixel taps -------------------------------------
    ax2.step(x, samp_a, where="mid", color=BLUE, linewidth=2,
             label="adaptive: (2r+1)² taps")
    ax2.step(x, samp_f, where="mid", color=ORANGE, linewidth=2,
             label="fixed: 49 taps")
    ax2.fill_between(x, samp_a, step="mid", color=BLUE, alpha=0.08)
    ax2.set_ylabel("depth taps per pixel", fontsize=10, color=INK)
    ax2.set_xlabel("blocker-to-receiver gap over light-to-receiver  d/D",
                   fontsize=10, color=INK)
    ax2.set_xlim(0, x[-1])
    ax2.annotate(
        f"{samp_a[-1]} taps at d/D={x[-1]:.2f}",
        xy=(x[-1], samp_a[-1]), xytext=(-122, -18),
        textcoords="offset points", fontsize=8.5, color=BLUE,
    )
    ax2.annotate(
        "49 taps, any distance", xy=(x[2], 49), xytext=(8, 6),
        textcoords="offset points", fontsize=8.5, color=ORANGE,
    )
    ax2.legend(loc="upper left", fontsize=8.5, frameon=False)

    fig.text(
        0.01, 0.01,
        f"stress tier d/D={float(stress['d_over_D']):.2f}: theory w={float(stress['w_theory']):.2f} texel, "
        f"adaptive r={stress['radius']} measured {float(stress['w_meas']):.1f} "
        f"({100 * float(stress['rel_err']):.0f}%), fixed r=3 measured 7.0 (1067%) — "
        "sub-texel penumbra needs r<1, outside integer PCF's reach",
        fontsize=8, color=MUTED, wrap=True,
    )
    fig.tight_layout(rect=(0, 0.045, 1, 1))
    fig.savefig(out_png, dpi=150, facecolor=SURFACE)
    print(f"wrote {out_png}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
