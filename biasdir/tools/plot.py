#!/usr/bin/env python3
"""Plot the bias-direction sweep produced by ../biasdir.

Run from the biasdir directory:  python3 tools/plot.py
Reads results/sweep.csv + results/gate.csv, writes PNGs into results/.
One axis per chart; the two measures (black area, detachment) get separate
files and one paired small-multiple, never a dual axis.
"""
import csv
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "results")

BLUE = "#2a78d6"     # light-axis offset (validated categorical slot 1)
ORANGE = "#eb6834"   # normal offset   (validated categorical slot 2)
SURFACE = "#fcfcfb"
GRID = "#e1e0d9"
INK = "#0b0b0b"
MUTED = "#898781"
MODE_STYLE = {
    "light_const": (BLUE, "-", "light-axis push d"),
    "normal_const": (ORANGE, "-", "normal push d"),
}
TIERS = ["0", "45", "80"]

plt.rcParams.update({
    "font.family": "sans-serif",
    "font.sans-serif": ["DejaVu Sans"],
    "font.size": 10,
    "text.color": INK,
    "axes.edgecolor": "#c3c2b7",
    "axes.labelcolor": INK,
    "xtick.color": "#52514e",
    "ytick.color": "#52514e",
    "axes.grid": True,
    "grid.color": GRID,
    "grid.linewidth": 0.8,
    "axes.axisbelow": True,
    "figure.facecolor": SURFACE,
    "axes.facecolor": SURFACE,
    "savefig.facecolor": SURFACE,
})


def load_sweep():
    rows = []
    with open(os.path.join(RES, "sweep.csv"), newline="") as f:
        for r in csv.DictReader(f):
            if r["mode"] not in MODE_STYLE:
                continue
            rows.append({
                "tier": r["angle_deg"],
                "d": float(r["d_texel"]),
                "mode": r["mode"],
                "black": float(r["black_frac_pct"]),
                "gap": float(r["gap_texel"]),
                "bias": float(r["depth_bias_texel"]),
            })
    return rows


def load_gate():
    with open(os.path.join(RES, "gate.csv"), newline="") as f:
        return list(csv.DictReader(f))


def series(rows, tier, mode, key):
    pts = sorted((r["d"], r[key]) for r in rows if r["tier"] == tier and r["mode"] == mode)
    return [p[0] for p in pts], [p[1] for p in pts]


def style_ax(ax):
    for spine in ("top", "right"):
        ax.spines[spine].set_visible(False)


def fig_black(rows):
    fig, axes = plt.subplots(1, 3, figsize=(12, 4), sharey=True)
    for ax, tier in zip(axes, TIERS):
        for mode, (color, ls, label) in MODE_STYLE.items():
            d, y = series(rows, tier, mode, "black")
            ax.plot(d, y, color=color, ls=ls, lw=2, label=label)
        ax.set_title(f"surface {tier}°", color=INK)
        ax.set_xlabel("world offset d (shadow-map texels)")
        ax.axhline(0, color="#c3c2b7", lw=1)
        style_ax(ax)
    axes[0].set_ylabel("black-stripe area (% of patch)")
    axes[-1].legend(frameon=False, loc="upper right")
    fig.suptitle("Shadow acne vs offset direction — black area must reach zero",
                 fontsize=12, x=0.02, ha="left")
    fig.tight_layout(rect=(0, 0, 1, 0.94))
    fig.savefig(os.path.join(RES, "black_area.png"), dpi=150)
    plt.close(fig)


def fig_gap(rows):
    fig, axes = plt.subplots(1, 3, figsize=(12, 4), sharey=True)
    for ax, tier in zip(axes, TIERS):
        for mode, (color, ls, label) in MODE_STYLE.items():
            d, y = series(rows, tier, mode, "gap")
            ax.plot(d, y, color=color, ls=ls, lw=2, label=label)
        ax.axhline(1.0, color=MUTED, ls="--", lw=1.2)
        ax.text(39.5, 1.6, "1-texel budget", color=MUTED, fontsize=8,
                ha="right")
        ax.set_ylim(bottom=-1, top=42)
        ax.set_title(f"surface {tier}°", color=INK)
        ax.set_xlabel("world offset d (shadow-map texels)")
        style_ax(ax)
    axes[0].set_ylabel("shadow detachment (texels)")
    axes[-1].legend(frameon=False, loc="upper left")
    fig.suptitle("Cost of the offset — normal push slides the sample sideways",
                 fontsize=12, x=0.02, ha="left")
    fig.tight_layout(rect=(0, 0, 1, 0.94))
    fig.savefig(os.path.join(RES, "detachment.png"), dpi=150)
    plt.close(fig)


def fig_tradeoff(rows):
    """Paired small multiples, same d axis: black area over detachment."""
    fig, axes = plt.subplots(2, 3, figsize=(12, 6.5), sharex="col")
    for col, tier in enumerate(TIERS):
        for mode, (color, _, label) in MODE_STYLE.items():
            d, black = series(rows, tier, mode, "black")
            _, gap = series(rows, tier, mode, "gap")
            axes[0, col].plot(d, black, color=color, lw=2, label=label)
            axes[1, col].plot(d, gap, color=color, lw=2, label=label)
        axes[0, col].axhline(0, color="#c3c2b7", lw=1)
        axes[1, col].axhline(1.0, color=MUTED, ls="--", lw=1.2)
        axes[0, col].set_title(f"surface {tier}°", color=INK)
        axes[1, col].set_xlabel("world offset d (texels)")
        style_ax(axes[0, col]); style_ax(axes[1, col])
    axes[0, 0].set_ylabel("black area (%)")
    axes[1, 0].set_ylabel("detachment (texels)")
    axes[0, 0].set_ylim(bottom=-2, top=105)   # flat 0 deg tier reads as zero
    axes[0, 1].set_ylim(bottom=-2, top=105)
    axes[0, 2].set_ylim(bottom=-2, top=105)
    axes[0, -1].legend(frameon=False, loc="upper right")
    fig.suptitle("The trade-off: normal push detaches long before the grazing tier clears",
                 fontsize=12, x=0.02, ha="left")
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    fig.savefig(os.path.join(RES, "tradeoff.png"), dpi=150)
    plt.close(fig)


def fig_projection(rows):
    """Depth-axis projection of the offset — the quantity that actually matters."""
    fig, axes = plt.subplots(1, 3, figsize=(12, 4), sharey=True)
    for ax, tier in zip(axes, TIERS):
        for mode, (color, _, label) in MODE_STYLE.items():
            d, b = series(rows, tier, mode, "bias")
            ax.plot(d, b, color=color, lw=2, label=label)
        ax.plot([0, 40], [0, 40], color=MUTED, ls=":", lw=1.2, label="b = d reference")
        ax.set_title(f"surface {tier}°", color=INK)
        ax.set_xlabel("world offset d (texels)")
        style_ax(ax)
    axes[0].set_ylabel("effective depth bias b (texels)")
    axes[-1].legend(frameon=False, loc="upper left")
    fig.suptitle("Projection on the light axis: b = d vs b = d·cos(θ)",
                 fontsize=12, x=0.02, ha="left")
    fig.tight_layout(rect=(0, 0, 1, 0.94))
    fig.savefig(os.path.join(RES, "projection.png"), dpi=150)
    plt.close(fig)


def fig_gate(gate):
    """Bar chart of the smallest world offset that clears each tier."""
    rules = ["light_const", "normal_const", "light_slope", "normal_slope"]
    labels = {
        "light_const": "light, const",
        "normal_const": "normal, const",
        "light_slope": "light, slope",
        "normal_slope": "normal, slope",
    }
    colors = {"light_const": BLUE, "normal_const": ORANGE,
              "light_slope": BLUE, "normal_slope": ORANGE}
    fig, ax = plt.subplots(figsize=(10, 4.5))
    width = 0.18
    x = range(len(TIERS))
    for k, rule in enumerate(rules):
        vals, fails = [], []
        for tier in TIERS:
            row = next(r for r in gate if r["rule"] == rule and r["angle_deg"] == tier)
            vals.append(float(row["d_world_texel"]))
            fails.append(row["gate_pass"] == "0")
        offs = [xi + (k - 1.5) * width for xi in x]
        bars = ax.bar(offs, vals, width, color=colors[rule],
                      label=labels[rule], edgecolor=SURFACE, linewidth=2)
        for bar, fail in zip(bars, fails):
            if fail:
                ax.text(bar.get_x() + bar.get_width() / 2,
                        bar.get_height() + 0.6, "✕", ha="center",
                        color="#d03b3b", fontsize=13, fontweight="bold")
    ax.axhline(1.0, color=MUTED, ls="--", lw=1.2)
    ax.text(2.62, 1.15, "1-texel detachment budget", color=MUTED, fontsize=8)
    ax.set_xticks(list(x))
    ax.set_xticklabels([f"{t}° surface" for t in TIERS])
    ax.set_ylabel("world offset to zero black area (texels)")
    ax.legend(frameon=False, ncol=4, loc="upper left")
    style_ax(ax)
    fig.suptitle("Smallest offset that clears each tier (✕ = fails the 1-texel gate)",
                 fontsize=12, x=0.02, ha="left")
    fig.tight_layout(rect=(0, 0, 1, 0.94))
    fig.savefig(os.path.join(RES, "gate_bars.png"), dpi=150)
    plt.close(fig)


def main():
    rows = load_sweep()
    gate = load_gate()
    fig_black(rows)
    fig_gap(rows)
    fig_tradeoff(rows)
    fig_projection(rows)
    fig_gate(gate)
    for name in ("black_area", "detachment", "tradeoff", "projection", "gate_bars"):
        print("wrote results/%s.png" % name)


if __name__ == "__main__":
    main()
