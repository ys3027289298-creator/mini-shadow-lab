# -*- coding: utf-8 -*-
"""Figures 3-5: stair convergence, scanline profiles, and the ghosting trade-off."""
import json
import math
import os

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager, patches

HERE = os.path.dirname(os.path.abspath(__file__))
FIG = os.path.join(HERE, "figures")

FONT = "/home/ys/.fonts/NotoSansCJKsc-Regular.otf"
font_manager.fontManager.addfont(FONT)
plt.rcParams["font.family"] = font_manager.FontProperties(fname=FONT).get_name()
plt.rcParams["axes.unicode_minus"] = False
plt.rcParams.update({
    "font.size": 11, "axes.edgecolor": "#c3c2b7", "axes.linewidth": 0.9,
    "axes.labelcolor": "#0b0b0b", "text.color": "#0b0b0b",
    "xtick.color": "#52514e", "ytick.color": "#52514e",
    "axes.grid": True, "grid.color": "#e1e0d9", "grid.linewidth": 0.8,
    "axes.axisbelow": True, "figure.facecolor": "white",
    "axes.facecolor": "#fcfcfb", "legend.frameon": False,
})
BLUE, ORANGE, AQUA = "#2a78d6", "#eb6834", "#1baf7a"
VIOLET, RED, GRAY = "#6250d6", "#e34948", "#898781"

d = json.load(open(os.path.join(HERE, "data", "sim_results.json"), encoding="utf-8"))
conv = d["convergence"]
TIERS = ["0.5", "2.0", "8.0"]
tcolor = {"0.5": BLUE, "2.0": ORANGE, "8.0": AQUA}
tlabel = {"0.5": "月亮 0.5°", "2.0": "中等 2°", "8.0": "大光源 8°"}

# ============================================================== fig 3: stair vs N
fig, axes = plt.subplots(1, 3, figsize=(13.5, 4.8), sharey=True)
for ax, alpha in zip(axes, TIERS):
    modes = conv[alpha]["modes"]
    n = list(range(1, len(modes["temporal_only"]["halton"]["amp"]) + 1))
    # temporal only: three sequences + no-jitter control
    ax.plot(n, modes["temporal_only"]["halton"]["amp"], color=tcolor[alpha],
            lw=2.2, marker="o", ms=4.5, label="纯时间抖动 (Halton, K=1)")
    ax.plot(n, modes["temporal_only"]["white"]["amp"], color=ORANGE,
            lw=1.5, ls="--", label="纯时间抖动 (白噪声)")
    ax.plot(n, modes["temporal_only"]["none"]["amp"], color=GRAY,
            lw=1.5, ls=":", label="无抖动对照")
    # hybrid options
    for tag, st in ((k, s) for k, s in modes.items() if k.startswith("hybrid")):
        K = st["kernel_texels"]
        ax.plot(n, st["halton"]["amp"],
                color=VIOLET if K == 3 else AQUA,
                lw=2.0, marker="s", ms=5, mfc="white", mew=1.6,
                label=f"空间 PCF K={K} + 时间累积（地板 1/K={1/K:.2f}）")
    ax.axhline(0.30, color=RED, lw=1.8, ls="--")
    ax.annotate("30% 门限", xy=(2, 0.40), color=RED, fontsize=10,
                bbox=dict(boxstyle="round,pad=0.2", fc="#fcfcfb", ec="none", alpha=0.9))
    rho = conv[alpha]["rho"]
    ax.set_title(f"{tlabel[alpha]}   ρ={rho:.2f}", pad=10)
    ax.set_xlabel("累积帧数 N")
    ax.set_ylim(-0.03, 1.08)
axes[0].set_ylabel("残留台阶幅度（最大逐纹素跳变）")
axes[0].legend(fontsize=8.2, loc="center right")
fig.suptitle("纯时间抖动有量化地板（K=1 → 0.75），门限以下只能靠空间核把地板压到 1/K",
             fontsize=12.5, y=1.02)
fig.tight_layout()
fig.savefig(os.path.join(FIG, "fig3_stair_convergence.png"), dpi=160,
            bbox_inches="tight")
plt.close(fig)

# ============================================================== fig 4: scanline profiles
import sim_taa as S

x = np.arange(-6, 7, dtype=float)
edge = 0.37
rho = conv["0.5"]["rho"]
truth = S.truth_coverage(x, edge, rho)
u = S.halton_seq(8)

fig, axes = plt.subplots(1, 3, figsize=(13.5, 4.4), sharey=True)
profiles = [
    (0, 1, "单帧硬边（无累积）", BLUE),
    (0, 8, "纯时间抖动，N=8（仍只有 1 纹素宽的台阶）", ORANGE),
    (2, 8, "混合：PCF K=5 + N=8（台阶拆成 5 级）", AQUA),
]
for ax, (r, N, title, c) in zip(axes, profiles):
    cn = S.accumulate(x, edge, rho, u[:N], r)[-1]
    ax.step(x, cn, where="mid", color=c, lw=2.2, label="累积结果 C(x)")
    ax.plot(x, truth, color=GRAY, lw=1.8, ls="--", label="真实半影 P(x)")
    amp = np.abs(np.diff(cn)).max()
    w = ((cn > 0.02) & (cn < 0.98)).sum()
    ax.annotate(f"最大台阶={amp:.2f}\n过渡带宽={w} 纹素",
                xy=(0.05, 0.62), xycoords="axes fraction", fontsize=10,
                bbox=dict(boxstyle="round,pad=0.35", fc="white",
                          ec="#c3c2b7", lw=0.8))
    ax.set_title(title, pad=10, fontsize=10.5)
    ax.set_xlabel("采样点位置（纹素）")
    ax.set_ylim(-0.05, 1.08)
axes[0].set_ylabel("阴影覆盖率")
axes[0].legend(fontsize=9, loc="lower right")
fig.suptitle("月亮档（ρ=1.34）：时间累积只填充亚像素，真正削台阶需要空间核",
             fontsize=12.5, y=1.02)
fig.tight_layout()
fig.savefig(os.path.join(FIG, "fig4_scanline.png"), dpi=160, bbox_inches="tight")
plt.close(fig)

# ============================================================== fig 5: stair vs ghost trail
# Two stacked panels sharing the frame-count axis (one measure per axis, no twin).
fig, (axa, axb) = plt.subplots(2, 1, figsize=(9.4, 7.2), sharex=True,
                               gridspec_kw={"hspace": 0.12})
N = np.arange(1, 17)
amp5 = np.array(conv["0.5"]["modes"]["hybrid_r2"]["halton"]["amp"][:16])

# upper panel: residual stair
axa.plot(N, amp5, color=BLUE, lw=2.4, marker="o", ms=6,
         label="残留台阶幅度（K=5 混合）")
axa.axhline(0.30, color=RED, lw=1.8, ls="--")
axa.annotate("30% 台阶门限", xy=(11.2, 0.315), color=RED, fontsize=10)
axa.set_ylabel("残留台阶幅度")
axa.set_ylim(0, 1.0)
axa.legend(loc="upper right", fontsize=10)

# lower panel: ghost trail at two edge speeds
for v, c, mk in ((0.25, ORANGE, "s"), (0.5, AQUA, "^")):
    axb.plot(N, (N - 1) * v, color=c, lw=2.2, marker=mk, ms=6,
             label=f"拖影长度（v={v} 纹素/帧）")
axb.axhline(2.0, color=GRAY, lw=1.8, ls=":")
axb.annotate("拖影预算 2 纹素", xy=(1.1, 2.12), color=GRAY, fontsize=10)

# feasible windows, drawn on both panels so the intersection is visible
for v, c in ((0.25, ORANGE), (0.5, AQUA)):
    cap = int(math.floor(2.0 / v)) + 1
    for ax in (axa, axb):
        ax.axvspan(1, cap, color=c, alpha=0.08)
    axb.annotate(f"v={v}：N≤{cap}", xy=(cap, 4.6), ha="center",
                 fontsize=9.5, color=c)
axb.set_ylabel("拖影长度（纹素）")
axb.set_xlabel("累积帧数 N")
axb.set_xticks(N)
axb.set_ylim(0, 8.2)
axb.legend(loc="upper left", fontsize=10)
fig.suptitle("帧数取舍：台阶要 N≥2 才过门限，拖影给 N 设上限，可行区是两条曲线的交集",
             fontsize=12.5, y=0.97)
fig.tight_layout(rect=(0, 0, 1, 0.95))
fig.savefig(os.path.join(FIG, "fig5_tradeoff.png"), dpi=160)
plt.close(fig)
print("fig3, fig4, fig5 written")
