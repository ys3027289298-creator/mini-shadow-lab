#!/usr/bin/env python3
"""Plot the depth-precision vs cull trade-off from depthspan CSV.

Stdlib only: emits one SVG with two strips per scene tier.
  top    : dz_min(S) precision line (left axis) + N_cull(S) step (right axis),
           Smax precision ceiling marked
  bottom : bottom-shadow strength(S) with the 0.60 threshold and feasible span
Usage: python3 plot_tradeoff.py input.csv output.svg
"""
import csv
import sys
import xml.etree.ElementTree as ET

C_PREC = "#2f6db3"   # precision curve
C_CULL = "#c0562e"   # cull curve
C_STR = "#2e8b57"    # strength curve
C_THR = "#b3332f"    # 0.60 threshold
C_FEAS = "#e6f0e9"   # feasible region fill
C_SMAX = "#7a3fb0"   # precision ceiling marker


def load(path):
    scenes = {}
    order = []
    naive = {}
    with open(path, newline="") as f:
        for row in csv.reader(f):
            name, S, dz, cull, unres, strength = row
            S = float(S)
            rec = (S, float(dz) * 1000.0, int(cull), float(strength))
            if name not in scenes:
                scenes[name] = []
                order.append(name)
            # the over-extended far-plane point is tagged, not part of the
            # fixed-floor working-frustum curve
            if name not in naive or S > naive[name][0]:
                naive[name] = rec
            else:
                scenes[name].append(rec)
    return order, scenes, naive


def el(tag, **attrs):
    return ET.Element(tag, {k: str(v) for k, v in attrs.items()})


def text(parent, x, y, s, size=11, anchor="start", fill="#222", weight="normal"):
    t = el("text", x=x, y=y, fill=fill,
           **{"font-size": size, "text-anchor": anchor, "font-weight": weight,
              "font-family": "sans-serif"})
    t.text = s
    parent.append(t)


def polyline(parent, pts, color, width=1.8, dash=None):
    attrs = {"points": " ".join(f"{x:.2f},{y:.2f}" for x, y in pts),
             "fill": "none", "stroke": color, "stroke-width": width,
             "stroke-linejoin": "round"}
    if dash:
        attrs["stroke-dasharray"] = dash
    parent.append(el("polyline", **attrs))


def step_points(rows, sx, sy):
    pts = []
    prev = None
    for S, _, cull, _ in rows:
        if prev is not None:
            pts.append((sx(S), sy(prev)))
        pts.append((sx(S), sy(cull)))
        prev = cull
    return pts


W, PANEL_H, GAP = 920, 250, 34
ML, MR, MT = 64, 64, 30


def render(order, scenes, naive, smax_map, out):
    n = len(order)
    H = MT + n * PANEL_H + (n - 1) * GAP + 30
    svg = el("svg", xmlns="http://www.w3.org/2000/svg", width=W, height=H,
             viewBox=f"0 0 {W} {H}")
    svg.append(el("rect", x=0, y=0, width=W, height=H, fill="white"))
    text(svg, W / 2, 20, "light near/far span: depth precision vs culling",
         size=15, anchor="middle", weight="bold")

    for idx, name in enumerate(order):
        rows = scenes[name]
        y0 = MT + idx * (PANEL_H + GAP)
        strip_h = 104
        x0, x1 = ML, W - MR
        xmax = max(naive.get(name, rows[-1])[0], float(max(r[0] for r in rows)))
        sx = lambda S: x0 + (S / xmax) * (x1 - x0)

        # ---- top strip: precision + cull ----
        top = y0
        dz_max = max(r[1] for r in rows)
        dz_dy = lambda v: top + strip_h - (v / dz_max) * (strip_h - 16)
        cull_max = max(r[2] for r in rows) or 1
        cull_dy = lambda v: top + strip_h - (v / cull_max) * (strip_h - 16)

        svg.append(el("rect", x=x0, y=top, width=x1 - x0, height=strip_h,
                      fill="#fafafa", stroke="#ccc"))
        for g in range(5):
            gx = x0 + g * (x1 - x0) / 4
            svg.append(el("line", x1=gx, y1=top, x2=gx, y2=top + strip_h,
                          stroke="#e3e3e3"))
        polyline(svg, [(sx(S), dz_dy(dz)) for S, dz, _, _ in rows], C_PREC, 2.0)
        polyline(svg, step_points(rows, sx, cull_dy), C_CULL, 2.0)

        smax = smax_map.get(name)
        if smax is not None and smax <= xmax:
            mx = sx(smax)
            svg.append(el("line", x1=mx, y1=top, x2=mx, y2=top + strip_h,
                          stroke=C_SMAX, **{"stroke-width": 1.4,
                                            "stroke-dasharray": "5,4"}))
            text(svg, mx + 3, top + 12, f"Smax={smax:.1f}m", size=10,
                 fill=C_SMAX, weight="bold")

        # pathological over-extended far plane, shown as a red marker
        if name in naive:
            nS, ndz, ncull, nstr = naive[name]
            svg.append(el("circle", cx=sx(nS), cy=dz_dy(ndz), r=4.5,
                          fill=C_THR))
            svg.append(el("circle", cx=sx(nS), cy=cull_dy(ncull), r=4.5,
                          fill=C_THR))
            text(svg, sx(nS) - 4, top + strip_h - 4,
                 f"naive far={nS:.0f}m", size=9, anchor="end", fill=C_THR)

        text(svg, x0, top - 8, name, size=13, weight="bold")
        text(svg, x1, top - 8, f"dz_min {C_PREC} (mm, left) / culled {C_CULL} (n, right)",
             size=10, anchor="end")
        text(svg, x0 - 8, top + 10, f"{dz_max:.2f}", size=9, anchor="end", fill=C_PREC)
        text(svg, x0 - 8, top + strip_h, "0", size=9, anchor="end", fill=C_PREC)
        text(svg, x1 + 8, top + 10, str(cull_max), size=9, fill=C_CULL)
        text(svg, x1 + 8, top + strip_h, "0", size=9, fill=C_CULL)

        # ---- bottom strip: strength ----
        bot = top + strip_h + 26
        bh = 76
        sy = lambda v: bot + bh - v * (bh - 10)
        svg.append(el("rect", x=x0, y=bot, width=x1 - x0, height=bh,
                      fill="#fafafa", stroke="#ccc"))

        feasible = [S for S, _, _, st in rows if st >= 0.60]
        if feasible:
            svg.append(el("rect", x=sx(min(feasible)), y=bot,
                          width=sx(max(feasible)) - sx(min(feasible)),
                          height=bh, fill=C_FEAS))
        ty = sy(0.60)
        svg.append(el("line", x1=x0, y1=ty, x2=x1, y2=ty,
                      stroke=C_THR, **{"stroke-width": 1.3,
                                       "stroke-dasharray": "6,4"}))
        text(svg, x1 - 4, ty - 3, "0.60 threshold", size=9, anchor="end",
             fill=C_THR)
        polyline(svg, [(sx(S), sy(st)) for S, _, _, st in rows], C_STR, 2.0)
        if name in naive:
            nS, _, _, nstr = naive[name]
            svg.append(el("circle", cx=sx(nS), cy=sy(nstr), r=4.5, fill=C_THR))
        text(svg, x0, bot - 7, "bottom shadow strength (resolved / total)",
             size=10, fill=C_STR)
        text(svg, x0 - 8, sy(1.0) + 3, "1.0", size=9, anchor="end", fill=C_STR)
        text(svg, x0 - 8, sy(0.0) + 3, "0.0", size=9, anchor="end", fill=C_STR)

        for g in range(5):
            gx = x0 + g * (x1 - x0) / 4
            val = g * xmax / 4
            svg.append(el("line", x1=gx, y1=bot + bh, x2=gx, y2=bot + bh + 4,
                          stroke="#888"))
            text(svg, gx, bot + bh + 16, f"{val:.0f}m", size=9, anchor="middle")
        text(svg, (x0 + x1) / 2, bot + bh + 30, "span S = far - near (m)",
             size=10, anchor="middle")

    ET.ElementTree(svg).write(out, encoding="unicode", xml_declaration=False)


if __name__ == "__main__":
    order, scenes, naive = load(sys.argv[1])
    # precision ceilings from the C run: (thinnest h - bias) * 2^bits
    smax_map = {
        "small_room": (0.002 - 0.0005) * 65536,
        "mid_hall": (0.003 - 0.0005) * 65536,
        "large_atrium": (0.001 - 0.0005) * 65536,
    }
    render(order, scenes, naive, smax_map, sys.argv[2])
    print("wrote", sys.argv[2])
