#!/usr/bin/env python3
"""烘焙/实时阴影合成的两条决策曲线 + 三档交界处亮度。

测量点来自 ./bake_realtime 的 CSV 输出; 连续曲线用 C 侧同一组闭合表达式
(ED*min/mul + EA*GAO), 常量必须与 src/bake_realtime.c 保持一致。
"""
import csv
import io
import subprocess
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

ROOT = Path(__file__).resolve().parent.parent

# ---- 与 bake_realtime.c 相同的场景常量 ----
ED, EA, SV, GAO = 0.80, 0.25, 0.90, 0.90
QUANT = 1.0 / 255.0
# 每像素合成开销 (相对 ALU 单位): 相乘 2 个 mul, 取最小值 1 个 cmp+1 个 select
COST_MUL, COST_MIN = 2.0, 3.0

# ---- dataviz 参考调色板 (已过 validate_palette) ----
BLUE, ORANGE, AQUA = "#2a78d6", "#eb6834", "#1baf7a"
SURFACE, INK, INK2, MUTED = "#fcfcfb", "#0b0b0b", "#52514e", "#898781"
GRID, BASELINE = "#e1e0d9", "#c3c2b7"
CRITICAL = "#d03b3b"

plt.rcParams.update({
    "font.family": ["Noto Sans CJK SC", "sans-serif"],
    "font.size": 10.5,
    "text.color": INK,
    "axes.edgecolor": BASELINE,
    "axes.labelcolor": INK2,
    "axes.titlecolor": INK,
    "xtick.color": MUTED,
    "ytick.color": MUTED,
    "axes.grid": True,
    "grid.color": GRID,
    "grid.linewidth": 0.8,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "figure.facecolor": SURFACE,
    "axes.facecolor": SURFACE,
    "savefig.facecolor": SURFACE,
})


def read_tiers():
    out = subprocess.run(
        [str(ROOT / "bake_realtime")], cwd=ROOT, check=True,
        stdout=subprocess.PIPE, text=True,
    ).stdout
    block = out.splitlines()
    start = next(i for i, ln in enumerate(block) if ln.startswith("tier,"))
    names = ("tier,", "near,", "middle,", "far,")
    csv_lines = [ln for ln in block[start:] if ln.startswith(names)]
    rows = list(csv.DictReader(io.StringIO("\n".join(csv_lines))))
    for r in rows:
        for k, v in list(r.items()):
            if k != "tier" and k != "verdict":
                r[k] = float(v)
    return rows


def shade_min(rv):
    return ED * min(SV, rv) + EA * GAO


def shade_mul(rv):
    return ED * SV * rv + EA * GAO * rv


def seam_mul(rv):
    return shade_min(rv) - shade_mul(rv)


def tradeoff_figure(tiers, path):
    rv = [i / 400.0 for i in range(1, 401)]
    fig, (ax1, ax2) = plt.subplots(
        2, 1, figsize=(8.2, 7.4), sharex=True, height_ratios=[1.15, 1.0])
    fig.subplots_adjust(hspace=0.12, left=0.10, right=0.95, top=0.92, bottom=0.09)

    # 上: 接缝过暗程度 (量化级)
    y_min = [0.0 for _ in rv]
    y_mul = [seam_mul(v) / QUANT for v in rv]
    ax1.plot(rv, y_mul, color=ORANGE, lw=2, label="相乘：接缝过暗 Δ(rV)/q")
    ax1.plot(rv, y_min, color=BLUE, lw=2, label="取最小值：接缝恒为 0")
    ax1.axhline(1.0, color=CRITICAL, lw=1.4)
    ax1.annotate("验收阈值 1 个量化级", xy=(0.02, 1.0), xytext=(0.02, 3.2),
                 color=CRITICAL, fontsize=9.5)
    for r in tiers:
        ax1.plot(r["rV"], r["mul_seam_levels"], "o", ms=8,
                 color=ORANGE, mec=SURFACE, mew=2, zorder=5)
        ax1.plot(r["rV"], 0.0, "o", ms=8,
                 color=BLUE, mec=SURFACE, mew=2, zorder=5)
    ax1.annotate("相乘 %.1f 级" % tiers[0]["mul_seam_levels"],
                 xy=(tiers[0]["rV"], tiers[0]["mul_seam_levels"]),
                 xytext=(tiers[0]["rV"] + 0.06, tiers[0]["mul_seam_levels"] + 1.5),
                 color=INK2, fontsize=9.5)
    ax1.set_ylabel("接缝亮度差（8bit 量化级）")
    ax1.set_ylim(-2.5, 60)
    ax1.set_title("决策曲线一：过暗程度随实时可见性 rV 的变化", loc="left")
    ax1.legend(frameon=False, loc="upper right")

    # 下: 合成开销随实时阴影屏占比 f 的累积
    f = [i / 100.0 for i in range(101)]
    cost_mul = [COST_MUL * v / COST_MUL for v in f]      # 归一: 相乘全帧=1.0
    cost_min = [COST_MIN * v / COST_MUL for v in f]
    ax2.plot(f, cost_min, color=BLUE, lw=2,
             label="取最小值：%.0f ALU/像素 (cmp+select)" % COST_MIN)
    ax2.plot(f, cost_mul, color=ORANGE, lw=2,
             label="相乘：%.0f ALU/像素 (2×mul)" % COST_MUL)
    ax2.annotate("全帧 1.50×", xy=(1.0, 1.5), xytext=(0.80, 1.56),
                 color=INK2, fontsize=9.5)
    ax2.annotate("全帧 1.00×（基线）", xy=(1.0, 1.0), xytext=(0.72, 0.86),
                 color=INK2, fontsize=9.5)
    ax2.set_xlabel("实时阴影覆盖的屏占比 f")
    ax2.set_ylabel("帧内合成总 ALU（相对）")
    ax2.set_title("决策曲线二：合成开销随覆盖面积线性累积", loc="left")
    ax2.legend(frameon=False, loc="upper left")
    ax2.set_xlim(0, 1)

    fig.savefig(path, dpi=160)
    plt.close(fig)


def junction_figure(tiers, path):
    labels = ["%s\n%.1fm" % (r["tier"], r["dist_m"]) for r in tiers]
    x = range(len(tiers))
    w = 0.24
    base = [r["nonjunction"] for r in tiers]
    truth = [r["truth_junction"] for r in tiers]
    mul = [r["mul_junction"] for r in tiers]

    fig, ax = plt.subplots(figsize=(8.2, 5.2))
    fig.subplots_adjust(left=0.09, right=0.97, top=0.80, bottom=0.13)
    b1 = ax.bar([i - w for i in x], base, width=w, color=AQUA,
                label="非交界处（无角色遮挡）")
    b2 = ax.bar(list(x), truth, width=w, color=BLUE,
                label="交界处真值 = 取最小值")
    b3 = ax.bar([i + w for i in x], mul, width=w, color=ORANGE,
                label="交界处：相乘")
    for bars in (b1, b2, b3):
        for b in bars:
            ax.annotate("%.3f" % b.get_height(),
                        xy=(b.get_x() + b.get_width() / 2, b.get_height()),
                        xytext=(0, 3), textcoords="offset points",
                        ha="center", fontsize=9, color=INK2)
    # 1 个量化级的高度示意 (真值柱顶上方一个 q)
    for i, r in zip(x, tiers):
        ax.plot([i - w / 2, i + w / 2],
                [r["truth_junction"] + QUANT] * 2,
                color=CRITICAL, lw=1.6)
    ax.annotate("红线段 = 1 个量化级（%s）；相乘柱顶低出约 40 级" % "1/255",
                xy=(0.02, 0.97), xycoords="axes fraction",
                color=CRITICAL, fontsize=9.5, va="top")
    ax.set_xticks(list(x))
    ax.set_xticklabels(labels)
    ax.set_ylabel("墙面像素亮度")
    ax.set_ylim(0, 1.15)
    ax.set_title("三档角色距离：交界处亮度对比（阈值 < 1 量化级）", loc="left",
                 pad=42)
    ax.legend(frameon=False, loc="lower left",
              bbox_to_anchor=(0.0, 1.0), ncol=3, columnspacing=1.4,
              handlelength=1.6)
    fig.savefig(path, dpi=160)
    plt.close(fig)


def main():
    tiers = read_tiers()
    out_dir = ROOT / "artifacts"
    out_dir.mkdir(exist_ok=True)
    tradeoff_figure(tiers, out_dir / "tradeoff_curves.png")
    junction_figure(tiers, out_dir / "junction_brightness.png")
    for p in sorted(out_dir.glob("*.png")):
        print(p)


if __name__ == "__main__":
    sys.exit(main())
