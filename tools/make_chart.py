#!/usr/bin/env python3
"""Build a self-contained HTML report of the CSM seam trade-off curves.

Reads artifacts/curve_<preset>.csv (produced by tools/seam_analyze.c) and
recomputes the allocation table with the same formulas. Run with:
    python3 -I tools/make_chart.py
"""
import csv
import json
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, "artifacts")
NEAR, LAMBDA, N, R0 = 0.5, 0.5, 3, 2048
PRESETS = [("near-field", 100.0), ("mid", 500.0), ("far", 1000.0)]
TOL = 0.10


def splits(far):
    out = []
    for i in range(1, N + 1):
        r = i / N
        log_s = NEAR * (far / NEAR) ** r
        uni_s = NEAR + (far - NEAR) * r
        out.append(LAMBDA * log_s + (1 - LAMBDA) * uni_s)
    bounds = [NEAR] + out
    return [bounds[i + 1] - bounds[i] for i in range(N)], out


def read_curve(preset):
    cont, pow2 = [], []
    section = None
    with open(os.path.join(ART, f"curve_{preset}.csv"), newline="") as f:
        for row in csv.reader(f):
            if not row:
                continue
            if row[0].startswith("cont_"):
                section = "cont"
                continue
            if row[0].startswith("pow2_"):
                section = "pow2"
                continue
            if section == "cont":
                cont.append((float(row[0]), float(row[1])))
            else:
                pow2.append((float(row[0]), float(row[1]), row[2]))
    return cont, pow2


def table_rows(span):
    """Allocation numbers for the report table."""
    # equal resolution
    t_eq = [s / R0 for s in span]
    eq_jump = max(t_eq[i + 1] / t_eq[i] for i in range(N - 1))
    # continuous frontier anchored at R0, exact tolerance p
    def cont_alloc(p):
        rs = [R0 * span[i] / (span[0] * (1 + p) ** i) for i in range(N)]
        ts = [span[i] / rs[i] for i in range(N)]
        j = max(ts[i + 1] / ts[i] for i in range(N - 1))
        return rs, ts, j, sum(rs)
    rs10, t10, j10, tot10 = cont_alloc(TOL)
    rs05, t05, j05, tot05 = cont_alloc(0.05)
    # fixed budget 6144: keep same shape, scale down
    unit = [span[i] / (span[0] * (1 + TOL) ** i) for i in range(N)]
    scale = (R0 * N) / (R0 * sum(unit))
    rs_b = [R0 * u * scale for u in unit]
    t_b = [span[i] / rs_b[i] for i in range(N)]
    required = [span[i + 1] / ((1 + TOL) * span[i]) for i in range(N - 1)]
    return {
        "texel_eq": t_eq, "eq_jump": eq_jump,
        "rs10": rs10, "t10": t10, "tot10": tot10,
        "rs05": rs05, "t05": t05, "tot05": tot05,
        "rs_budget": rs_b, "t_budget": t_b,
        "required": required,
    }


# ---- SVG geometry -------------------------------------------------------
W, H = 580, 380
ML, MR, MT, MB = 70, 28, 34, 52
PW, PH = W - ML - MR, H - MT - MB
X0, X1 = 1.0, 4.2           # jump factor domain (equal-res point reaches ~4.09)
Y0, Y1 = 0.0, 15000.0       # total texels


def sx(factor):
    return ML + (factor - X0) / (X1 - X0) * PW


def sy(total):
    return MT + (1 - (total - Y0) / (Y1 - Y0)) * PH


def build_svg(preset, far, cont, pow2, info):
    span, split_vals = splits(far)
    eq_jump = info["eq_jump"]
    parts = []
    parts.append(
        f'<svg class="chart" viewBox="0 0 {W} {H}" role="img" '
        f'aria-label="Seam trade-off for {preset} preset" data-preset="{preset}">')

    # horizontal gridlines + y ticks
    for yv in range(0, 15001, 3000):
        y = sy(yv)
        parts.append(f'<line class="grid" x1="{ML}" y1="{y:.1f}" x2="{W-MR}" y2="{y:.1f}"/>')
        label = "0" if yv == 0 else f"{yv//1000}K"
        parts.append(f'<text class="tick" x="{ML-10}" y="{y+4:.1f}" text-anchor="end">{label}</text>')

    # x ticks
    for fv in (1.0, 1.1, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0):
        x = sx(fv)
        parts.append(f'<line class="grid vert" x1="{x:.1f}" y1="{MT}" x2="{x:.1f}" y2="{H-MB}"/>')
        parts.append(f'<text class="tick" x="{x:.1f}" y="{H-MB+20}" text-anchor="middle">'
                     f'1.0×{"" if fv == 1.0 else f" (+{round((fv-1)*100)}%)"}</text>')

    # axis titles
    parts.append(f'<text class="axis-title" x="{ML+PW/2}" y="{H-8}" text-anchor="middle">'
                 f'Allowed softening jump across a seam (factor of texel / PCF width)</text>')
    parts.append(f'<text class="axis-title" transform="translate(18 {MT+PH/2}) rotate(-90)" '
                 f'text-anchor="middle">Total shadow-map resolution (texels, all 3 cascades)</text>')

    # target line at 1.10
    xt = sx(1.10)
    parts.append(f'<line class="target" x1="{xt:.1f}" y1="{MT}" x2="{xt:.1f}" y2="{H-MB}"/>')
    parts.append(f'<text class="target-label" x="{xt+6:.1f}" y="{MT+14}">+10% target</text>')

    # continuous frontier, clipped to x domain
    pts = [(1 + p, tot) for p, tot in cont if X0 <= 1 + p <= X1]
    d = "M " + " L ".join(f"{sx(f):.1f} {sy(v):.1f}" for f, v in pts)
    area = d + f" L {sx(pts[-1][0]):.1f} {sy(0):.1f} L {sx(pts[0][0]):.1f} {sy(0):.1f} Z"
    parts.append(f'<path class="area" d="{area}"/>')
    parts.append(f'<path class="frontier" d="{d}"/>')
    parts.append(f'<text class="direct-label" x="{sx(pts[0][0])+6:.1f}" y="{sy(pts[0][1])-8:.1f}">'
                 f'continuous frontier (R0={R0} anchored)</text>')

    # pow2 reachable points (orange), de-duplicated
    seen = set()
    for jump, total, pat in pow2:
        key = (round(jump, 3), round(total))
        if key in seen or not (X0 <= 1 + jump <= X1):
            continue
        seen.add(key)
        x, y = sx(1 + jump), sy(total)
        parts.append(f'<circle class="marker pow2" cx="{x:.1f}" cy="{y:.1f}" r="5" '
                     f'data-jump="{jump:.3f}" data-total="{total:.0f}" data-pat="{pat}"/>')

    # equal-resolution current point (aqua)
    xe, ye = sx(1 + eq_jump), sy(R0 * N)
    parts.append(f'<circle class="marker equal" cx="{xe:.1f}" cy="{ye:.1f}" r="6" '
                 f'data-jump="{eq_jump:.3f}" data-total="{R0*N}" data-pat="2048-2048-2048"/>')
    parts.append(f'<text class="direct-label" x="{xe-8:.1f}" y="{ye-12:.1f}" text-anchor="end">'
                 f'equal-res (current): +{eq_jump*100:.0f}%</text>')

    # crosshair + hover overlay
    parts.append(f'<line class="crosshair" x1="0" y1="{MT}" x2="0" y2="{H-MB}" visibility="hidden"/>')
    parts.append(f'<rect class="hover-zone" x="{ML}" y="{MT}" width="{PW}" height="{PH}" '
                 f'tabindex="0" data-preset="{preset}"/>')
    parts.append('</svg>')
    return "\n".join(parts), pts, sorted((j, t, p) for j, t, p in pow2 if (round(j, 3), round(t)) in seen)


def fmt_k(v):
    return f"{v:,.0f}"


def main():
    charts, embedded, tables = [], {}, []
    for preset, far in PRESETS:
        span, split_vals = splits(far)
        cont, pow2 = read_curve(preset)
        info = table_rows(span)
        svg, fpts, ppts = build_svg(preset, far, cont, pow2, info)
        embedded[preset] = {
            "frontier": [{"p": p, "total": tot} for p, tot in cont],
            "pow2": [{"jump": j, "total": t, "pat": p} for j, t, p in pow2],
            "equal": {"jump": info["eq_jump"], "total": R0 * N},
        }
        charts.append(f'<section class="card"><h2>{preset} '
                      f'<span class="sub">view far = {far:.0f} world units, '
                      f'splits {split_vals[0]:.1f} / {split_vals[1]:.1f} / {split_vals[2]:.1f}</span></h2>'
                      f'<div class="chart-wrap">{svg}<div class="tooltip" role="status"></div></div></section>')

        # table twin
        def alloc_cells(rs, ts):
            return " ".join(
                f"<td>{fmt_k(rs[i])}</td><td>{ts[i]:.4f}</td>" for i in range(N))
        rows = []
        rows.append("<tr><td>Equal resolution (current)</td>"
                    + alloc_cells([R0]*N, info["texel_eq"])
                    + f'<td>+{info["eq_jump"]*100:.1f}%</td><td>{R0*N}</td></tr>')
        rows.append("<tr><td>Continuous, jump ≤ 5%</td>"
                    + alloc_cells(info["rs05"], info["t05"])
                    + f'<td>+5.0%</td><td>{fmt_k(info["tot05"])}</td></tr>')
        rows.append("<tr><td>Continuous, jump ≤ 10%</td>"
                    + alloc_cells(info["rs10"], info["t10"])
                    + f'<td>+10.0%</td><td>{fmt_k(info["tot10"])}</td></tr>')
        rows.append("<tr><td>Same 6144 budget, jump ≤ 10% (near gets softer)</td>"
                    + alloc_cells(info["rs_budget"], info["t_budget"])
                    + '<td>+10.0%</td><td>6,144</td></tr>')
        req = " · ".join(f"R{i+1}/R{i} ≥ {r:.2f}" for i, r in enumerate(info["required"]))
        tables.append(
            f'<section class="card"><h2>{preset} — allocation table '
            f'<span class="sub">columns per cascade 0→2</span></h2>'
            f'<p class="req">Back-solved per-seam ratios: {req}</p>'
            '<table><thead><tr><th>Strategy</th>'
            '<th>R0</th><th>texel t0</th><th>R1</th><th>texel t1</th>'
            '<th>R2</th><th>texel t2</th><th>worst jump</th><th>total texels</th>'
            '</tr></thead><tbody>' + "".join(rows) + "</tbody></table></section>")

    legend = (
        '<div class="legend">'
        '<span class="key"><span class="swatch line s1"></span>continuous optimum (any texture size)</span>'
        '<span class="key"><span class="swatch dot s2"></span>strict power-of-two cascade sizes</span>'
        '<span class="key"><span class="swatch dot s3"></span>equal resolution (current setup)</span>'
        '</div>')

    html = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CSM seam: resolution-ratio trade-off</title>
<style>
:root {{
  color-scheme: light;
  --surface:#fcfcfb; --page:#f9f9f7; --ink:#0b0b0b; --ink2:#52514e; --muted:#898781;
  --grid:#e1e0d9; --axis:#c3c4b7; --border:rgba(11,11,11,.10);
  --s1:#2a78d6; --s2:#eb6834; --s3:#1baf7a; --target:#898781;
}}
@media (prefers-color-scheme: dark) {{ :root:not([data-theme="light"]) {{
  color-scheme: dark;
  --surface:#1a1a19; --page:#0d0d0d; --ink:#f0efec; --ink2:#c3c2b7; --muted:#898781;
  --grid:#2c2c2a; --axis:#383835; --border:rgba(255,255,255,.10);
  --s1:#3987e5; --s2:#d95926; --s3:#199e70; --target:#898781;
}}}}
:root[data-theme="dark"] {{
  color-scheme: dark;
  --surface:#1a1a19; --page:#0d0d0d; --ink:#f0efec; --ink2:#c3c2b7; --muted:#898781;
  --grid:#2c2c2a; --axis:#383835; --border:rgba(255,255,255,.10);
  --s1:#3987e5; --s2:#d95926; --s3:#199e70; --target:#898781;
}}
* {{ box-sizing: border-box; }}
body {{ margin:0; background:var(--page); color:var(--ink);
  font:14px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif; }}
main {{ max-width:720px; margin:0 auto; padding:32px 20px 64px; }}
h1 {{ font-size:24px; margin:0 0 6px; }}
h2 {{ font-size:16px; margin:0 0 14px; }}
p, td, th, li {{ color:var(--ink2); }}
.lead {{ margin:0 0 8px; }}
.findings {{ background:var(--surface); border:1px solid var(--border);
  border-radius:10px; padding:14px 18px; margin:18px 0 8px; }}
.findings li {{ margin:6px 0; }}
button.theme {{ float:right; border:1px solid var(--border); background:var(--surface);
  color:var(--ink); border-radius:8px; padding:6px 12px; font:inherit; cursor:pointer; }}
.card {{ background:var(--surface); border:1px solid var(--border);
  border-radius:12px; padding:18px; margin:18px 0; }}
.sub {{ font-weight:400; color:var(--muted); font-size:13px; }}
.legend {{ display:flex; flex-wrap:wrap; gap:16px; margin:10px 0 4px; font-size:13px; }}
.key {{ display:inline-flex; align-items:center; gap:7px; color:var(--ink2); }}
.swatch.line {{ width:22px; height:2px; border-radius:1px; background:var(--s1); }}
.swatch.dot {{ width:10px; height:10px; border-radius:50%; border:2px solid var(--surface); }}
.s2 {{ background:var(--s2); }} .s3 {{ background:var(--s3); }}
.chart-wrap {{ position:relative; }}
svg.chart {{ width:100%; height:auto; display:block; }}
.grid {{ stroke:var(--grid); stroke-width:1; }}
.grid.vert {{ stroke:var(--grid); }}
.tick {{ fill:var(--muted); font-size:11px; font-variant-numeric:tabular-nums; }}
.axis-title {{ fill:var(--ink2); font-size:12px; }}
.target {{ stroke:var(--target); stroke-width:1.5; }}
.target-label {{ fill:var(--muted); font-size:11px; }}
.area {{ fill:var(--s1); opacity:.10; }}
.frontier {{ fill:none; stroke:var(--s1); stroke-width:2; stroke-linejoin:round; stroke-linecap:round; }}
.direct-label {{ fill:var(--ink2); font-size:11.5px; }}
.marker {{ stroke:var(--surface); stroke-width:2; }}
.marker.pow2 {{ fill:var(--s2); }} .marker.equal {{ fill:var(--s3); }}
.crosshair {{ stroke:var(--axis); stroke-width:1; pointer-events:none; }}
.hover-zone {{ fill:transparent; cursor:crosshair; outline:none; }}
.hover-zone:focus-visible {{ stroke:var(--s1); stroke-width:2; }}
.tooltip {{ position:absolute; pointer-events:none; background:var(--surface);
  border:1px solid var(--border); border-radius:8px; padding:8px 11px;
  font-size:12.5px; min-width:170px; box-shadow:0 4px 14px rgba(0,0,0,.12);
  opacity:0; transition:opacity .12s; z-index:2; }}
.tooltip.show {{ opacity:1; }}
.tooltip .tv {{ color:var(--ink); font-weight:600; font-variant-numeric:tabular-nums; }}
.tooltip .row {{ display:flex; align-items:center; gap:7px; margin:3px 0; }}
.tooltip .lk {{ width:14px; height:2px; border-radius:1px; flex:none; }}
.tooltip .ld {{ width:9px; height:9px; border-radius:50%; flex:none; border:2px solid var(--surface); }}
table {{ border-collapse:collapse; width:100%; font-size:12.5px; }}
th, td {{ padding:7px 8px; text-align:right; border-bottom:1px solid var(--grid);
  font-variant-numeric:tabular-nums; }}
th:first-child, td:first-child {{ text-align:left; }}
th {{ color:var(--muted); font-weight:600; }}
.req {{ font-size:13px; }}
</style></head>
<body><main>
<button class="theme" id="themeBtn" type="button">Toggle theme</button>
<h1>Cascade shadow-map seam — resolution-ratio trade-off</h1>
<p class="lead">Three view-distance presets, 3 cascades, practical split (λ=0.5, near=0.5).
The near cascade is anchored at R0={R0} so foreground sharpness is held fixed; the curves show
what total resolution each softening-jump tolerance costs.</p>
{legend}
<div class="findings"><ul>
<li><b>The seam jump is the texel-size ratio</b> J = t<sub>i+1</sub>/t<sub>i</sub>
= (span<sub>i+1</sub>/span<sub>i</sub>)·(R<sub>i</sub>/R<sub>i+1</sub>). With equal 2048 maps it is
fixed by geometry alone: +133%/+33% … up to <b>+209% (far)</b>, independent of budget.</li>
<li>Back-solving J ≤ 1.10 needs per-seam ratios R<sub>i+1</sub>/R<sub>i</sub> ≥
span<sub>i+1</sub>/(1.1·span<sub>i</sub>); with R0 anchored, the continuous optimum costs
roughly <b>10.2K–10.7K texels</b> vs 6144 today.</li>
<li><b>Strict power-of-two sizes cannot reach +10%</b> — adjacent levels can only stay equal or
double (2×), while the second seam needs 2.23×–2.81×. Best pow2 result is +22.6% (near), +45.7%
(mid), +54.3% (far). Every orange marker lies right of the target line.</li>
</ul></div>
{''.join(charts)}
{''.join(tables)}
</main>
<script>
const DATA = {json.dumps(embedded)};
const ML={ML}, MT={MT}, PW={PW}, PH={PH}, X0={X0}, X1={X1}, Y1={Y1};
const sx = f => ML + (f - X0)/(X1 - X0)*PW;
const sy = v => MT + (1 - v/{Y1})*PH;
document.getElementById('themeBtn').addEventListener('click', () => {{
  const r = document.documentElement;
  r.setAttribute('data-theme', r.getAttribute('data-theme') === 'dark' ? 'light' : 'dark');
}});
function nearestIdx(arr, x) {{
  let bi=0, bd=Infinity;
  arr.forEach((d,i)=>{{ const c=Math.abs((1+d.p)-x); if(c<bd){{bd=c;bi=i;}} }});
  return bi;
}}
document.querySelectorAll('.hover-zone').forEach(zone => {{
  const svg = zone.closest('svg');
  const tip = svg.parentElement.querySelector('.tooltip');
  const cross = svg.querySelector('.crosshair');
  const d = DATA[zone.dataset.preset];
  function row(kind, colorVar, label, value) {{
    const r = document.createElement('div');
    r.className = 'row';
    const key = document.createElement('span');
    key.className = kind === 'line' ? 'lk' : 'ld';
    key.style.background = colorVar;
    const lab = document.createElement('span');
    lab.textContent = label;
    const val = document.createElement('span');
    val.className = 'tv';
    val.style.marginLeft = 'auto';
    val.textContent = value;
    r.append(key, lab, val);
    return r;
  }}
  function render(clientX) {{
    const rect = svg.getBoundingClientRect();
    const fx = (clientX - rect.left) / rect.width * {W};
    const factor = X0 + (fx - ML)/PW*(X1-X0);
    if (factor < X0 || factor > X1) {{ hide(); return; }}
    const i = nearestIdx(d.frontier, factor);
    const fp = d.frontier[i];
    const x = sx(1+fp.p);
    cross.setAttribute('x1', x); cross.setAttribute('x2', x);
    cross.setAttribute('visibility', 'visible');
    tip.replaceChildren();
    tip.appendChild(row('line', 'var(--s1)',
      `frontier @ +${{(fp.p*100).toFixed(0)}}% jump`,
      `${{fp.total.toLocaleString()}} texels`));
    d.pow2.filter(q => Math.abs((1+q.jump)-(1+fp.p)) < 0.12).forEach(q => {{
      tip.appendChild(row('dot', 'var(--s2)',
        `pow2 ${{q.pat}} (+${{(q.jump*100).toFixed(1)}}%)`,
        q.total.toLocaleString()));
    }});
    if (Math.abs(d.equal.jump - fp.p) < 0.12) {{
      tip.appendChild(row('dot', 'var(--s3)',
        `equal-res (+${{(d.equal.jump*100).toFixed(1)}}%)`, '6,144'));
    }}
    tip.classList.add('show');
    const wrapRect = svg.parentElement.getBoundingClientRect();
    let left = (x / {W}) * rect.width + 14;
    if (left + 200 > wrapRect.width) left -= 210;
    tip.style.left = left + 'px';
    tip.style.top = Math.max(0, sy(fp.total) - 10) + 'px';
  }}
  function hide() {{ cross.setAttribute('visibility','hidden'); tip.classList.remove('show'); }}
  zone.addEventListener('pointermove', e => render(e.clientX));
  zone.addEventListener('pointerleave', hide);
  let ki = 20;
  zone.addEventListener('keydown', e => {{
    if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {{
      ki = Math.max(0, Math.min(d.frontier.length-1, ki + (e.key==='ArrowRight'?1:-1)));
      const rect = svg.getBoundingClientRect();
      render(rect.left + sx(1+d.frontier[ki].p)/{W}*rect.width);
      e.preventDefault();
    }}
  }});
  zone.addEventListener('blur', hide);
}});
</script></body></html>"""
    out = os.path.join(ART, "seam_tradeoff.html")
    with open(out, "w") as f:
        f.write(html)
    print("wrote", out)


if __name__ == "__main__":
    main()
