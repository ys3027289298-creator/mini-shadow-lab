#!/usr/bin/env python3
"""Generate the report figures from the measured CSVs.

Design rules (dataviz skill): one axis per panel (dual measures become two
panels), categorical colors from the validated palette, direct labels,
log scale only where the data spans decades, never a rainbow ramp.
"""
import csv
import pathlib

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

# validated categorical palette (light mode), blue/orange/aqua/magenta
BLUE, ORANGE, AQUA, MAGENTA = "#2a78d6", "#eb6834", "#1baf7a", "#e87ba4"
INK, MUTED, GRID = "#0b0b0b", "#52514e", "#d9d8d4"
SURFACE = "#fcfcfb"

plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE,
    "axes.edgecolor": MUTED, "axes.labelcolor": INK,
    "text.color": INK, "xtick.color": INK, "ytick.color": INK,
    "font.size": 10, "axes.grid": True, "grid.color": GRID,
    "grid.linewidth": 0.7, "axes.spines.top": False,
    "axes.spines.right": False,
})

OUT = pathlib.Path("artifacts/figs")
OUT.mkdir(parents=True, exist_ok=True)
CSV = pathlib.Path("artifacts/csv")


def save(fig, name):
    fig.tight_layout()
    fig.savefig(OUT / name, dpi=140, bbox_inches="tight")
    plt.close(fig)
    print("wrote", OUT / name)


# ---- Fig 1: per-stage timing, grouped bars --------------------------------
def fig_stage_timing():
    rows = list(csv.reader(open(CSV / "final_A_r2.txt")))
    row = next(r for r in rows if r and r[0] == "CSV")
    # 0:CSV 1:A 2:B 3:exact 4:gt1 5:max | 6..10 A s1..total | 11..15 B s1..total
    ta = list(map(float, row[6:11]))
    tb = list(map(float, row[11:16]))
    stages = ["S1\nmap build", "S2\nbias", "S3\nPCF", "S4\nshade", "total"]
    x = np.arange(len(stages))
    w = 0.38
    fig, ax = plt.subplots(figsize=(7.2, 4.0))
    b1 = ax.bar(x - w/2, ta, w, label="ref_d (slow)", color=BLUE)
    b2 = ax.bar(x + w/2, tb, w, label="nat_f (fast)", color=ORANGE)
    ax.set_ylabel("median time / frame (ms)")
    ax.set_xticks(x)
    ax.set_xticklabels(stages)
    for bars in (b1, b2):
        for r in bars:
            ax.annotate(f"{r.get_height():.2f}",
                        (r.get_x() + r.get_width()/2, r.get_height()),
                        ha="center", va="bottom", fontsize=8, color=MUTED)
    # annotate the bias-stage gap, the whole story
    gap = ta[1] - tb[1]
    pct = 100.0 * gap / ta[1]
    ax.annotate(f"bias stage\n−{gap:.2f} ms (−{pct:.1f}%)",
                xy=(1 + w/2, tb[1]), xytext=(1.9, 13.2),
                arrowprops=dict(arrowstyle="->", color=MAGENTA, lw=1.3),
                color=MAGENTA, fontsize=9, fontweight="bold", ha="left")
    ax.set_title("Per-stage cost: the gap is entirely inside the bias stage",
                 loc="left", fontsize=11)
    ax.legend(frameon=False, loc="upper left")
    save(fig, "fig1_stage_timing.png")


# ---- Fig 2: funnel across intermediate layers ------------------------------
def fig_funnel():
    labels = ["L1\nbias value", "L2\nraw tap", "L3\nbiased tap",
              "L4\nPCF coverage", "L5\nfinal pixel"]
    pairA = [16.95882, 0.0, 0.0, 0.0, 0.0]
    pairB = [100.0, 0.0, 0.01318, 0.11523, 0.11523]
    x = np.arange(len(labels))
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.6, 3.9))

    ax = ax1
    ax.plot(x, pairA, "-o", color=BLUE, lw=2, ms=7, label="Pair A: ref vs nat")
    ax.plot(x, pairB, "-s", color=ORANGE, lw=2, ms=7, label="Pair B: ref vs fast_k0")
    ax.set_yscale("symlog", linthresh=1e-3)
    ax.set_xticks(x)
    ax.set_xticklabels(labels)
    ax.set_ylabel("fraction of elements differing (%)  — log scale")
    ax.set_title("Difference funnel, log view", loc="left", fontsize=11)
    ax.set_ylim(-3e-4, 400)
    # direct labels on the L1 points, no text near the crowded tail
    ax.annotate("16.96%", (0, 16.95882), textcoords="offset points",
                xytext=(12, -2), ha="left", fontsize=8.5, color=BLUE)
    ax.annotate("100%", (0, 100.0), textcoords="offset points",
                xytext=(12, 2), ha="left", fontsize=8.5, color=ORANGE)
    ax.annotate("Pair A: 0 at every layer past L1", (2, 3e-4),
                textcoords="offset points", xytext=(10, 6),
                fontsize=8.5, color=BLUE)
    ax.legend(frameon=False, loc="upper right")

    # linear zoom on the tail layers where Pair B survives
    ax = ax2
    tail = labels[2:]
    ax.plot(range(3), pairB[2:], "-s", color=ORANGE, lw=2, ms=8,
            label="Pair B (ref vs fast_k0)")
    ax.plot(range(3), pairA[2:], "-o", color=BLUE, lw=2, ms=8,
            label="Pair A (ref vs nat)")
    ax.set_xticks(range(3))
    ax.set_xticklabels(tail)
    ax.set_ylabel("fraction differing (%)  — linear zoom")
    ax.set_ylim(-0.01, 0.14)
    ax.set_title("Tail layers, linear view", loc="left", fontsize=11)
    for xi, yi in zip(range(3), pairB[2:]):
        ax.annotate(f"{yi:g}%", (xi, yi), textcoords="offset points",
                    xytext=(0, 9), ha="center", fontsize=8, color=ORANGE)
    ax.legend(frameon=False, loc="center right")
    save(fig, "fig2_funnel.png")


# ---- Fig 3: error-amplification causal experiment --------------------------
def fig_amplify():
    def parse(path):
        G, flips = [], []
        for line in open(CSV / path):
            p = line.strip().split(",")
            if p and p[0].replace(".", "").isdigit():
                G.append(float(p[0])); flips.append(float(p[3]))
        return np.array(G), np.array(flips)
    gA, fA = parse("margin_pairA.txt")
    gB, fB = parse("margin_pairB.txt")
    fig, ax = plt.subplots(figsize=(7.8, 4.4))
    ax.plot(gA, fA, "-o", color=BLUE, lw=2, ms=7,
            label="Pair A ref vs nat  (real eps = 2.3e-10)")
    ax.plot(gB, fB, "-s", color=ORANGE, lw=2, ms=7,
            label="Pair B ref vs fast_k0 (real eps = 2.4e-5)")
    ax.set_xscale("symlog", linthresh=1)
    ax.set_yscale("symlog", linthresh=1e-3)
    ax.set_xlabel("amplification gain G applied to the bias difference")
    ax.set_ylabel("PCF taps flipped (%)")
    ax.set_ylim(-2e-4, 30)
    ax.set_title("Flips stay zero until the amplified error crosses the "
                 "margin band", loc="left", fontsize=11)
    ax.axvspan(0.3, 300, color=AQUA, alpha=0.12)
    ax.annotate("Pair A: 0 flips for G ≤ 100\n(error still below every tap margin)",
                xy=(100, 1e-3), xytext=(0.12, 0.35),
                arrowprops=dict(arrowstyle="->", color=BLUE, lw=1.2),
                fontsize=9, color=BLUE)
    ax.legend(frameon=False, loc="upper left", fontsize=9)
    save(fig, "fig3_amplification.png")


# ---- Fig 4: controllability curve over Newton steps ------------------------
def fig_k_sweep():
    rows = list(csv.DictReader(open(CSV / "sweep_k_30.csv")))
    names = [r["variant"] for r in rows]
    t = [float(r["time_ms"]) for r in rows]
    gt1 = [float(r["frac_gt1lsb_pct"]) for r in rows]
    rel = [float(r["bias_rel_err"]) for r in rows]
    x = np.arange(len(names))

    # three panels, one measure each: no dual-axis chart
    fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(12.4, 3.9))
    colors = [BLUE, AQUA, ORANGE, ORANGE, ORANGE, ORANGE]
    ax1.bar(x, t, color=colors, width=0.62)
    ax1.axhline(t[0], color=MUTED, ls="--", lw=1)
    ax1.annotate("ref baseline", (5.4, t[0]), va="bottom",
                 ha="right", fontsize=8, color=MUTED)
    ax1.set_xticks(x); ax1.set_xticklabels(names, rotation=35, ha="right")
    ax1.set_ylabel("total frame time (ms)")
    ax1.set_title("Cost", loc="left", fontsize=11)
    for xi, yi in zip(x, t):
        ax1.annotate(f"{yi:.1f}", (xi, yi), textcoords="offset points",
                     xytext=(0, 4), ha="center", fontsize=8, color=MUTED)

    ax2.plot(x, np.maximum(rel, 1e-8), "-o", color=MAGENTA, lw=2, ms=7)
    ax2.set_yscale("log")
    ax2.set_xticks(x); ax2.set_xticklabels(names, rotation=35, ha="right")
    ax2.set_ylabel("max relative bias error (log)")
    ax2.set_title("Intermediate precision", loc="left", fontsize=11)
    ax2.annotate("k≥2 reaches float-correct", xy=(4, 3e-6),
                 xytext=(0.6, 4e-4), arrowprops=dict(arrowstyle="->",
                 color=MAGENTA, lw=1.2), fontsize=8.5, color=MAGENTA)

    ax3.bar(x, gt1, color=colors, width=0.62)
    ax3.set_xticks(x); ax3.set_xticklabels(names, rotation=35, ha="right")
    ax3.set_ylabel("pixels differing >1 LSB (%)")
    ax3.set_title("Visual cost", loc="left", fontsize=11)
    ax3.set_ylim(0, 0.14)
    for xi, yi in zip(x, gt1):
        if yi > 0:
            ax3.annotate(f"{yi:g}", (xi, yi), textcoords="offset points",
                         xytext=(0, 4), ha="center", fontsize=8, color=MUTED)
    fig.suptitle("Controllability curve: one precision knob (Newton steps k) "
                 "moves all three measures", x=0.02, ha="left", fontsize=12)
    save(fig, "fig4_k_sweep.png")


# ---- Fig 5: divergence vs light elevation ----------------------------------
def fig_elev_sweep():
    def load(path):
        e, gt = [], []
        for r in csv.DictReader(open(path)):
            e.append(float(r["elev_deg"]))
            gt.append(float(r["frac_gt1lsb_pct"]))
        return np.array(e), np.array(gt)
    e1, g1 = load(CSV / "sweep_elev_nat.csv")
    e2, g2 = load(CSV / "sweep_elev_fast0.csv")
    fig, ax = plt.subplots(figsize=(7.8, 4.0))
    ax.plot(e1, g1, "-o", color=BLUE, lw=2, ms=5,
            label="Pair A ref vs nat: 0.00000% at every elevation 2°–45°")
    ax.plot(e2, g2, "-s", color=ORANGE, lw=2, ms=5,
            label="Pair B ref vs fast_k0")
    ax.set_xlabel("light elevation (degrees)")
    ax.set_ylabel("pixels differing >1 quantization level (%)")
    ax.set_ylim(-0.015, 0.34)
    ax.set_title("Equivalence holds down to grazing light for Pair A",
                 loc="left", fontsize=11)
    ax.legend(frameon=False, loc="upper right", fontsize=9)
    ax.annotate("nat stays exactly 0 even at 2°", xy=(2, 0.0),
                xytext=(8, 0.055), arrowprops=dict(arrowstyle="->",
                color=BLUE, lw=1.2), fontsize=9, color=BLUE)
    save(fig, "fig5_elev_sweep.png")


# ---- Fig 6: actual images and amplified difference map ---------------------
def fig_images():
    def load(tag, strat):
        return np.array(Image.open(f"artifacts/img_{tag}_{strat}.pgm"))
    pairs = [("A_r2", "ref_d", "nat_f", BLUE, ORANGE),
             ("B_r2", "ref_d", "fast_k0", BLUE, ORANGE)]
    fig, axes = plt.subplots(2, 3, figsize=(11.2, 5.4),
                             gridspec_kw={"width_ratios": [1, 1, 0.55]})
    for row, (tag, s1, s2, c1, c2) in enumerate(pairs):
        a, b = load(tag, s1), load(tag, s2)
        d = np.abs(a.astype(int) - b.astype(int))
        for col, (img, title, colr) in enumerate([
                (a, f"{tag}: {s1}", c1), (b, f"{tag}: {s2}", c2)]):
            ax = axes[row, col]
            ax.imshow(img, cmap="gray", vmin=0, vmax=255)
            ax.set_title(title, fontsize=10, color=colr)
            ax.set_xticks([]); ax.set_yticks([])
            ax.grid(False)
        ax = axes[row, 2]
        # amplify 20x so sub-percent differences become visible
        ax.imshow(np.clip(d * 20, 0, 255), cmap="magma")
        ax.set_title(f"|diff|×20  (max={d.max()}, "
                     f">{(d>1).mean()*100:.4f}% px)", fontsize=9)
        ax.set_xticks([]); ax.set_yticks([]); ax.grid(False)
    fig.suptitle("Rendered output and the amplified difference map",
                 x=0.02, ha="left", fontsize=12)
    save(fig, "fig6_images.png")


if __name__ == "__main__":
    fig_stage_timing()
    fig_funnel()
    fig_amplify()
    fig_k_sweep()
    fig_elev_sweep()
    fig_images()
