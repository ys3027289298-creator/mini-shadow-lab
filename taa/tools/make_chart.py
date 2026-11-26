#!/usr/bin/env python3
"""Build taa/chart.html from taa/results.csv (self-contained, no network)."""
import csv
import json
import pathlib

HERE = pathlib.Path(__file__).resolve().parent.parent
rows = []
with open(HERE / "results.csv", newline="") as f:
    for r in csv.DictReader(f):
        rows.append({
            "speed": float(r["speed"]),
            "beta": float(r["beta"]),
            "trail": float(r["trail_meas"]),
            "pred": float(r["trail_pred"]),
            "flicker": int(r["flicker"]),
            "mode": r["mode"],
        })

data = json.dumps(rows, ensure_ascii=False)
html = """<!doctype html>
<html lang="zh">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TAA 时域累积：拖尾–闪烁权重权衡</title>
<style>
.viz-root {
  color-scheme: light;
  --surface-1: #fcfcfb; --surface-2: #f2f1ee;
  --text-primary: #0b0b0b; --text-secondary: #52514e; --text-muted: #8a8881;
  --grid: #e4e2dc; --axis: #c9c6bd;
  --series-1: #2a78d6; --series-2: #eb6834; --series-3: #1baf7a; --limit: #c4443f;
  --pick: #6250d6;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) .viz-root {
    color-scheme: dark;
    --surface-1: #1a1a19; --surface-2: #232322;
    --text-primary: #f0efec; --text-secondary: #c3c2b7; --text-muted: #8f8d85;
    --grid: #2e2e2c; --axis: #45443f;
    --series-1: #3987e5; --series-2: #d95926; --series-3: #199e70; --limit: #e06a66;
    --pick: #9085e9;
  }
}
:root[data-theme="dark"] .viz-root {
  color-scheme: dark;
  --surface-1: #1a1a19; --surface-2: #232322;
  --text-primary: #f0efec; --text-secondary: #c3c2b7; --text-muted: #8f8d85;
  --grid: #2e2e2c; --axis: #45443f;
  --series-1: #3987e5; --series-2: #d95926; --series-3: #199e70; --limit: #e06a66;
  --pick: #9085e9;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--surface-2); color: var(--text-primary);
       font: 14px/1.5 -apple-system, "Segoe UI", "Noto Sans CJK SC", sans-serif; }
.viz-root { background: var(--surface-1); max-width: 1180px; margin: 24px auto;
            padding: 28px 32px 36px; border-radius: 12px; }
h1 { font-size: 20px; margin: 0 0 4px; }
.sub { color: var(--text-secondary); margin: 0 0 18px; font-size: 13px; }
.legend { display: flex; flex-wrap: wrap; gap: 18px; margin: 10px 0 6px;
          color: var(--text-secondary); font-size: 13px; }
.legend span { display: inline-flex; align-items: center; gap: 7px; }
.sw { width: 22px; height: 0; border-top: 2px solid var(--series-1); }
.sw.dash { border-top: 2px dashed var(--series-2); }
.sw.aqua { border-top: 2px solid var(--series-3); }
.sw.limit { border-top: 2px dashed var(--limit); }
.sw.pick { border-top: 2px solid var(--pick); }
.grid3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: 14px; }
.panel { background: var(--surface-1); border: 1px solid var(--grid);
         border-radius: 10px; padding: 10px 10px 6px; position: relative; }
.panel h3 { margin: 2px 4px 6px; font-size: 13px; font-weight: 600;
            color: var(--text-primary); display: flex; justify-content: space-between; }
.badge { font-size: 11px; font-weight: 600; padding: 1px 8px; border-radius: 999px; }
.badge.ok { color: var(--series-3); border: 1px solid var(--series-3); }
.badge.bad { color: var(--limit); border: 1px solid var(--limit); }
svg { width: 100%; height: 210px; display: block; }
.axis-label { fill: var(--text-muted); font-size: 11px; }
.gridline { stroke: var(--grid); stroke-width: 1; }
.axisline { stroke: var(--axis); stroke-width: 1; }
.series { fill: none; stroke-width: 2; }
.s1 { stroke: var(--series-1); } .s2 { stroke: var(--series-2); stroke-dasharray: 5 4; }
.s3 { stroke: var(--series-3); }
.limitline { stroke: var(--limit); stroke-width: 1.5; stroke-dasharray: 4 4; }
.pickline { stroke: var(--pick); stroke-width: 1.5; }
.dot { stroke: var(--surface-1); stroke-width: 2; }
.tip { position: fixed; pointer-events: none; background: var(--surface-2);
       border: 1px solid var(--axis); border-radius: 8px; padding: 8px 10px;
       font-size: 12px; color: var(--text-primary); display: none; z-index: 10;
       min-width: 130px; box-shadow: 0 4px 14px rgba(0,0,0,.18); }
.tip b { display: block; margin-bottom: 3px; }
.tip i { font-style: normal; color: var(--text-muted); }
table { border-collapse: collapse; width: 100%; margin-top: 22px; font-size: 13px; }
th, td { text-align: right; padding: 7px 10px; border-bottom: 1px solid var(--grid); }
th:first-child, td:first-child { text-align: left; }
th { color: var(--text-secondary); font-weight: 600; }
td.note { color: var(--limit); font-weight: 600; }
h2 { font-size: 15px; margin: 26px 0 8px; }
.eq { background: var(--surface-2); border-radius: 8px; padding: 12px 16px;
      font-size: 13px; color: var(--text-secondary); overflow-x: auto; }
.eq b { color: var(--text-primary); }
button.theme { float: right; border: 1px solid var(--axis); background: var(--surface-2);
       color: var(--text-secondary); border-radius: 8px; padding: 5px 12px;
       font-size: 12px; cursor: pointer; }
@media (max-width: 900px) { .grid3 { grid-template-columns: 1fr; } }
</style>
</head>
<body>
<div class="viz-root" id="root">
  <button class="theme" id="themeBtn" type="button">切换主题</button>
  <h1>TAA 时域累积：历史权重 β 的拖尾–闪烁权衡</h1>
  <p class="sub">一维扫描线模拟（影子长 40px，PCF k=3，亚像素抖动 ±0.15px）；上排：拖尾长度，下排：300 帧内边缘闪烁事件数；三列对应三档移动速度。</p>
  <div class="legend">
    <span><i class="sw"></i>实测拖尾（px）</span>
    <span><i class="sw dash"></i>解析预测 v·N_eff(β)</span>
    <span><i class="sw aqua"></i>闪烁事件数</span>
    <span><i class="sw limit"></i>拖尾预算 4px</span>
    <span><i class="sw pick"></i>β*（预算内最大权重）</span>
  </div>
  <div class="grid3" id="panels"></div>

  <h2>定权策略与运动自适应策略实测值</h2>
  <table id="tbl">
    <thead><tr><th>速度 (px/帧)</th><th>策略</th><th>β（运动中）</th>
      <th>β 上限（解析）</th><th>拖尾实测 (px)</th><th>预算 (px)</th>
      <th>闪烁事件</th><th>结论</th></tr></thead>
    <tbody></tbody>
  </table>

  <h2>推导链</h2>
  <div class="eq">
    <b>① 混合权重表达式（EMA 时域累积）：</b><br>
    C_t = β·H_{t−1} + (1−β)·I_t，&nbsp; H_t = C_t，&nbsp; β∈[0,1) 为历史帧权重，(1−β) 为当前帧权重<br><br>
    <b>② 拖尾长度：</b>边缘以 v px/帧移动后，n 帧前的旧结果在当前像素残留 βⁿ；定义残留阈值 ε=0.10，
    等效帧数 N_eff = ln ε / ln β，拖尾像素长度 <b>L_trail = v·N_eff</b><br><br>
    <b>③ 权重上限（L_trail ≤ L_shadow/10 = 4px 反解）：</b><br>
    β_max = ε^(v/L_budget) = exp(ln ε · v / L_budget)，&nbsp;
    v=0.25→0.866，v=2→0.316，v=6→0.032<br><br>
    <b>④ 冲突处理：</b>定权 β* 满足拖尾预算但中/高速下闪烁不为零（让步给拖尾）；
    运动自适应 β=min(β_max,0.98) 运动中限权、静止恢复 0.98，两项同时达标。
  </div>
</div>
<div class="tip" id="tip"></div>
<script>
const ROWS = __DATA__;
const SPEEDS = [0.25, 2, 6], LABELS = ["慢速 0.25 px/帧", "中速 2 px/帧", "快速 6 px/帧"];
const BUDGET = 4, BMAX = {0.25: 0.866, 2: 0.316, 6: 0.032}, BSTAR = {0.25: 0.86, 2: 0.30, 6: 0.02};
const W = 340, H = 210, M = {l: 44, r: 14, t: 12, b: 30};
const PW = W - M.l - M.r, PH = H - M.t - M.b;
const css = n => getComputedStyle(document.getElementById('root')).getPropertyValue(n).trim();
const X = b => M.l + b * PW;
const YT = v => M.t + PH - Math.min(v, 60) / 60 * PH;
const YF = v => M.t + PH - Math.min(v, 250) / 250 * PH;

function fixed(speed) { return ROWS.filter(r => r.speed === speed && r.mode === "fixed"); }
function path(data, key, yf) {
  return data.map((r, i) => (i ? "L" : "M") + X(r.beta).toFixed(1) + " " + yf(r[key]).toFixed(1)).join(" ");
}
function grid(g, yf, ticks) {
  let s = "";
  for (const t of ticks) {
    const y = yf(t);
    s += `<line class="gridline" x1="${M.l}" x2="${W-M.r}" y1="${y}" y2="${y}"/>`;
    s += `<text class="axis-label" x="${M.l-6}" y="${y+3.5}" text-anchor="end">${t}</text>`;
  }
  for (let b = 0; b <= 1.0001; b += 0.25)
    s += `<text class="axis-label" x="${X(b)}" y="${H-8}" text-anchor="middle">${b.toFixed(2)}</text>`;
  s += `<line class="axisline" x1="${M.l}" x2="${W-M.r}" y1="${M.t+PH}" y2="${M.t+PH}"/>`;
  s += `<line class="axisline" x1="${M.l}" x2="${M.l}" y1="${M.t}" y2="${M.t+PH}"/>`;
  g.innerHTML += s;
}
function panel(speed, label, row) {
  const d = fixed(speed), star = BSTAR[speed], bmax = BMAX[speed];
  const conflict = speed > 0.25;
  const el = document.createElement("div");
  el.className = "panel";
  el.innerHTML = `<h3><span>${label} · ${row === 0 ? "拖尾长度" : "闪烁事件"}</span>
      <span class="badge ${conflict && row === 0 ? "bad" : "ok"}">${conflict && row === 0 ? "定权冲突" : "可达标"}</span></h3>
    <svg viewBox="0 0 ${W} ${H}" data-speed="${speed}" data-row="${row}"></svg>`;
  const svg = el.querySelector("svg");
  if (row === 0) {
    grid(svg, YT, [0, 15, 30, 45, 60]);
    svg.innerHTML += `<line class="limitline" x1="${M.l}" x2="${W-M.r}" y1="${YT(BUDGET)}" y2="${YT(BUDGET)}"/>
      <text class="axis-label" x="${W-M.r-4}" y="${YT(BUDGET)-4}" text-anchor="end" fill="${css('--limit')}">预算 4px</text>
      <line class="pickline" x1="${X(star)}" x2="${X(star)}" y1="${M.t}" y2="${M.t+PH}"/>
      <path class="series s1" d="${path(d, "trail", YT)}"/>
      <path class="series s2" d="${path(d, "pred", YT)}"/>
      <circle class="dot" r="4.5" fill="${css('--pick')}" cx="${X(star)}" cy="${YT(d.find(r=>r.beta===star).trail)}"/>`;
  } else {
    grid(svg, YF, [0, 60, 120, 180, 240]);
    svg.innerHTML += `<line class="pickline" x1="${X(star)}" x2="${X(star)}" y1="${M.t}" y2="${M.t+PH}"/>
      <path class="series s3" d="${path(d, "flicker", YF)}"/>
      <circle class="dot" r="4.5" fill="${css('--pick')}" cx="${X(star)}" cy="${YF(d.find(r=>r.beta===star).flicker)}"/>`;
  }
  svg.innerHTML += `<text class="axis-label" x="${M.l+PW/2}" y="${H-0}" text-anchor="middle">历史权重 β</text>`;
  if (row === 0) svg.innerHTML += `<text class="axis-label" transform="rotate(-90 12 ${M.t+PH/2})" x="12" y="${M.t+PH/2}" text-anchor="middle">px</text>`;
  return el;
}
const wrap = document.getElementById("panels");
for (let row = 0; row < 2; row++)
  SPEEDS.forEach((v, i) => wrap.appendChild(panel(v, LABELS[i], row)));

const tip = document.getElementById("tip");
document.querySelectorAll("svg").forEach(svg => {
  const speed = +svg.dataset.speed, row = +svg.dataset.row, d = fixed(speed);
  svg.addEventListener("mousemove", e => {
    const rect = svg.getBoundingClientRect();
    const b = Math.max(0, Math.min(0.98, ((e.clientX - rect.left) / rect.width * W - M.l) / PW));
    const r = d.reduce((a, c) => Math.abs(c.beta - b) < Math.abs(a.beta - b) ? c : a);
    tip.style.display = "block";
    tip.style.left = (e.clientX + 14) + "px";
    tip.style.top = (e.clientY + 14) + "px";
    tip.innerHTML = `<b>v=${speed} px/帧 · β=${r.beta.toFixed(2)}</b>
      <i>拖尾实测</i> ${r.trail.toFixed(2)} px<br>
      <i>解析预测</i> ${r.pred.toFixed(2)} px<br>
      <i>闪烁事件</i> ${r.flicker} 次 / 300 帧`;
  });
  svg.addEventListener("mouseleave", () => tip.style.display = "none");
});

const adapt = ROWS.filter(r => r.mode === "adaptive");
const tb = document.querySelector("#tbl tbody");
for (const v of SPEEDS) {
  const fx = fixed(v).find(r => r.beta === BSTAR[v]);
  const ad = adapt.find(r => r.speed === v);
  tb.insertAdjacentHTML("beforeend", `<tr>
    <td>${v}</td><td>定权 β*=${BSTAR[v]}</td><td>${BSTAR[v].toFixed(2)}</td>
    <td>${BMAX[v].toFixed(3)}</td><td>${fx.trail.toFixed(2)}</td><td>4.0</td>
    <td>${fx.flicker}</td>
    <td class="${fx.flicker ? "note" : ""}">${fx.flicker ? "拖尾达标，闪烁让步（实测 " + fx.flicker + " 次）" : "两项达标"}</td></tr>`);
  tb.insertAdjacentHTML("beforeend", `<tr>
    <td>${v}</td><td>运动自适应</td><td>${ad.beta.toFixed(3)}</td>
    <td>${BMAX[v].toFixed(3)}</td><td>${ad.trail.toFixed(2)}</td><td>4.0</td>
    <td>${ad.flicker}</td><td>两项达标（静止 β=0.98）</td></tr>`);
}
document.getElementById("themeBtn").onclick = () => {
  const root = document.documentElement;
  root.dataset.theme = (root.dataset.theme ||
    (matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light")) === "dark" ? "light" : "dark";
};
</script>
</body>
</html>
"""
out = HERE / "chart.html"
out.write_text(html.replace("__DATA__", data), encoding="utf-8")
print("wrote", out, len(out.read_bytes()), "bytes")
