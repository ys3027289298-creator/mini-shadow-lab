# -*- coding: utf-8 -*-
"""Figures 1-2: why resolution cannot fix it, and why LDS beats white noise."""
import json
import math
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager, ticker

HERE = os.path.dirname(os.path.abspath(__file__))
FIG = os.path.join(HERE, "figures")
os.makedirs(FIG, exist_ok=True)

# ---------------------------------------------------------------- style
FONT = "/home/ys/.fonts/NotoSansCJKsc-Regular.otf"
font_manager.fontManager.addfont(FONT)
plt.rcParams["font.family"] = font_manager.FontProperties(fname=FONT).get_name()
plt.rcParams["axes.unicode_minus"] = False
plt.rcParams.update({
    "font.size": 11,
    "axes.edgecolor": "#c3c2b7", "axes.linewidth": 0.9,
    "axes.labelcolor": "#0b0b0b", "text.color": "#0b0b0b",
    "xtick.color": "#52514e", "ytick.color": "#52514e",
    "axes.grid": True, "grid.color": "#e1e0d9", "grid.linewidth": 0.8,
    "axes.axisbelow": True, "figure.facecolor": "white",
    "axes.facecolor": "#fcfcfb", "legend.frameon": False,
})
BLUE, ORANGE, AQUA = "#2a78d6", "#eb6834", "#1baf7a"
VIOLET, RED, GRAY = "#6250d6", "#e34948", "#898781"

d = json.load(open(os.path.join(HERE, "data", "sim_results.json"), encoding="utf-8"))

# ============================================================== fig 1: rho
fig, ax = plt.subplots(figsize=(8.2, 5.0))
res_grid = [1024, 2048, 4096, 8192]
colors = {0.5: BLUE, 2.0: ORANGE, 8.0: AQUA}
labels = {0.5: "月亮 0.5°", 2.0: "中等光源 2°", 8.0: "大光源 8°"}
for alpha in (0.5, 2.0, 8.0):
    ys_ = []
    for res in res_grid:
        row = next(r for r in d["rho_rows"]
                   if r["alpha_deg"] == alpha and r["res"] == res
                   and r["gap_m"] == 3.0)
        ys_.append(row["rho"])
    ax.plot(res_grid, ys_, marker="o", lw=2, ms=8, color=colors[alpha],
            label=labels[alpha])
    for x, y in zip(res_grid, ys_):
        ax.annotate(f"{y:.2f}", (x, y), textcoords="offset points",
                    xytext=(0, 9), ha="center", fontsize=9, color="#52514e")
ax.axhline(1.0, color=RED, lw=1.8, ls="--")
ax.annotate("采样极限 ρ = 1：半影仅覆盖 1 个采样点",
            xy=(1180, 1.06), color=RED, fontsize=10)
ax.set_xscale("log", base=2)
ax.set_yscale("log")
ax.xaxis.set_major_formatter(ticker.StrMethodFormatter("{x:.0f}"))
ax.set_xlabel("阴影贴图分辨率（每轴纹素数）")
ax.set_ylabel(r"半影 / 采样极限比值  ρ = $w_{pen}$ / $w_{texel}$")
ax.set_title("分辨率提高 4 倍，ρ 只线性变大；月亮档始终贴着采样极限", pad=12)
ax.legend(loc="upper left")
fig.tight_layout()
fig.savefig(os.path.join(FIG, "fig1_rho_vs_resolution.png"), dpi=160)
plt.close(fig)

# ============================================================== fig 2: sequence quality
q = d["quality"]
n = q["halton"]["n"]
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.5, 4.6))

styles = {"halton": (BLUE, "o", "Halton 低差异 (base-2)"),
          "golden": (AQUA, "s", "黄金比 R2 低差异"),
          "white": (ORANGE, "^", "白噪声随机抖动")}
for name, (c, mk, lab) in styles.items():
    ax1.plot(n, q[name]["discrepancy"], marker=mk, ms=6, lw=2,
             color=c, label=lab, markevery=4)
# reference slopes
ref = [v / math.sqrt(nn) for v, nn in zip(q["white"]["discrepancy"], n)]
k = ref[3] * math.sqrt(n[3])
ax1.plot(n, [k / math.sqrt(nn) for nn in n], ls=":", color=GRAY, lw=1.6,
         label=r"参考斜率 $N^{-1/2}$")
k2 = q["halton"]["discrepancy"][7] * n[7] / math.log(n[7])
ax1.plot(n, [k2 * math.log(nn) / nn for nn in n], ls="--", color=VIOLET, lw=1.6,
         label=r"参考斜率 $\log N / N$")
ax1.set_yscale("log")
ax1.set_xlabel("累积帧数 N")
ax1.set_ylabel("星差 D*（最大未覆盖比例）")
ax1.set_title("(a) 点集均匀度：低差异序列的误差衰减更快", pad=10)
ax1.legend(fontsize=9)

for name, (c, mk, lab) in styles.items():
    ax2.plot(n, q[name]["ac_lag1"], marker=mk, ms=6, lw=2,
             color=c, label=lab, markevery=4)
ax2.axhline(0.0, color=GRAY, lw=1.2)
ax2.set_ylim(-0.85, 0.35)
ax2.set_xlabel("累积帧数 N")
ax2.set_ylabel("滞后 1 阶自相关 R(1)")
ax2.set_title("(b) 自相关：低差异序列稳定为负（相邻点互相避开）", pad=10)
ax2.legend(fontsize=9)
fig.tight_layout()
fig.savefig(os.path.join(FIG, "fig2_sequence_quality.png"), dpi=160)
plt.close(fig)
print("fig1, fig2 written")
