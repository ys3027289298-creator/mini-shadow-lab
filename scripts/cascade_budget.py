#!/usr/bin/env python3
"""cascade budget: solve the three simultaneous constraints.

C1 no stair-stepping near : texel spacing of cascade 0 <= p_stair
C2 no shadow loss far     : last split (view distance)        >= D_req
C3 shadow map memory      : sum of map bytes                  <= M_budget

Split scheme mirrors cascade/src/cascade.c (practical log/uniform blend).
Run: python3 -I cascade_budget.py
"""
import math

NEAR = 0.5          # camera near plane, world units (cascade.c self-test)
LAMBDA = 0.5        # practical blend (cascade.c self-test)
P_STAIR = 0.05      # world-unit texel ceiling: ~5cm, sub-pixel on a 1080p
                    # screen at typical near-field world-per-pixel (~6-10cm)
D_REQ = 1000.0      # required shadow coverage = far plane in cascade.c
M_BUDGET = 128 * 1024 * 1024
BYTES_PER_TEXEL = 4 + 4   # D32 depth + D32 color (VSM/variance): 8 B/texel


def practical_splits(n, near_d, far_d, lam):
    out = []
    for i in range(1, n + 1):
        r = i / n
        log_s = near_d * (far_d / near_d) ** r
        uni_s = near_d + (far_d - near_d) * r
        out.append(lam * log_s + (1.0 - lam) * uni_s)
    return out


def spans(n, far_d, lam=0.5):
    sp = practical_splits(n, NEAR, far_d, lam)
    bounds = [0.0] + sp          # cascade 0 covers from 0 like texel_world()
    return [bounds[i + 1] - bounds[i] for i in range(n)], sp


def mem_mb(res_list):
    return sum(r * r for r in res_list) * BYTES_PER_TEXEL / (1024 * 1024)


def report(tag, n, far_d, res_list, lam=0.5):
    sn, splits = spans(n, far_d, lam)
    t0 = sn[0] / res_list[0]
    print(f"--- {tag}: n={n} far={far_d:g} lambda={lam} res={res_list}")
    for i, (s, r) in enumerate(zip(sn, res_list)):
        print(f"  cascade {i}: span={s:9.2f}  res={r:5d}  texel={s / r:.4f} m")
    print(f"  C1 near texel = {t0:.4f} m   (need <= {P_STAIR})  "
          f"{'PASS' if t0 <= P_STAIR else 'FAIL'}")
    print(f"  C2 last split = {splits[-1]:.2f} m (need >= {D_REQ}) "
          f"{'PASS' if splits[-1] >= D_REQ - 1e-6 else 'FAIL'}")
    mm = mem_mb(res_list)
    print(f"  C3 memory     = {mm:7.2f} MB (budget {M_BUDGET / 1024 / 1024:.0f}) "
          f"{'PASS' if mm <= 128.0 else 'FAIL'}")
    return t0, splits[-1], mm


print("=== reference: repo self-test geometry n=3, far=1000, lambda=0.5 ===")
sn, sp = spans(3, 1000.0)
print("splits:", [round(x, 3) for x in sp], " spans:", [round(x, 2) for x in sn])

print("\n=== derived design boundaries ===")
# C1-driven resolution for cascade 0 under each (n, far):  r1 >= s1 / p
for n in (2, 3, 4):
    s, _ = spans(n, D_REQ)
    r1_need = s[0] / P_STAIR
    print(f"n={n}: first span={s[0]:7.2f} m -> r1 >= {r1_need:8.0f} "
          f"(next power of two {2 ** math.ceil(math.log2(r1_need))})")

# far a single 4096 cascade can cover while keeping 5 cm texels
r_max = 4096
far_cap_1 = P_STAIR * r_max
print(f"single 4096 map, 5cm texel -> far coverage ceiling = {far_cap_1:g} m "
      f"(requirement {D_REQ:g} m, short by factor {D_REQ / far_cap_1:g})")

# memory-driven max equal resolution per cascade count
for n in (1, 2, 3, 4):
    r_eq = math.sqrt(M_BUDGET / BYTES_PER_TEXEL / n)
    print(f"n={n}: memory allows equal res <= {r_eq:8.0f} "
          f"(power of two {2 ** math.floor(math.log2(r_eq))})")

print("\n=== strict-requirement feasibility at power-of-two resolutions ===")
# minimal power-of-two r1 satisfying C1, minimal memory overall
for n in (2, 3, 4):
    s, spl = spans(n, D_REQ)
    r1 = 2 ** math.ceil(math.log2(s[0] / P_STAIR))
    # give every cascade at least r1 (smallest legal uniform choice)
    res = [max(r1, 256)] * n
    mm = mem_mb(res)
    print(f"n={n}: r1 needs {r1}, uniform {res} -> {mm:7.1f} MB "
          f"({'over' if mm > 128 else 'within'})")

print("\n=== candidate solutions (8 B/texel: depth + color) ===")
# A: uniform 2048 x3, keep strict 1000 m view; near texel relaxed to ~8.3 cm
report("A uniform 2048x3", 3, 1000.0, [2048, 2048, 2048])
# C: 4-cascade reference layout, lands exactly on the budget
report("C 2048 x4", 4, 1000.0, [2048] * 4)
# E: cheapest practical layout that satisfies C1 and C2 (far cascades >=1024)
report("E 4096/1024/1024 n=3", 3, 1000.0, [4096, 1024, 1024])
# D: strict C1 with a quality far cascade
s4, spl4 = spans(4, 1000.0)
r1_strict = 2 ** math.ceil(math.log2(s4[0] / P_STAIR))
report("D 4096/2048/1024/1024 n=4", 4, 1000.0,
       [r1_strict, 2048, 1024, 1024])
# B': relax view distance instead; C1 + C3 hold, C2 fails by 2x
report("B' 2048/2048/1024 @500m", 3, 500.0, [2048, 2048, 1024])

print("\n=== same layouts at 4 B/texel (depth-only D32, no color target) ===")
for tag, n, far, res in [
    ("F 4096/1024/1024 n=3", 3, 1000.0, [4096, 1024, 1024]),
    ("G 4096/2048/2048 n=3", 3, 1000.0, [4096, 2048, 2048]),
]:
    sn, splits = spans(n, far)
    t0 = sn[0] / res[0]
    mb = sum(r * r for r in res) * 4 / 1024 / 1024
    ok = t0 <= P_STAIR and splits[-1] >= D_REQ - 1e-6 and mb <= 128
    print(f"{tag}: near texel={t0:.4f} m, far={splits[-1]:g} m, "
          f"mem={mb:.0f} MB -> ALL THREE {'PASS' if ok else 'FAIL'}")

print("\n=== gap summary ===")
s1n3, _ = spans(3, 1000.0)
tA = s1n3[0] / 2048
print(f"config A: near texel {tA:.4f} m vs ceiling {P_STAIR}: overshoot x{tA / P_STAIR:.2f}")
s1n4, _ = spans(4, 1000.0)
tC = s1n4[0] / 2048
print(f"config C: near texel {tC:.4f} m vs ceiling {P_STAIR}: overshoot x{tC / P_STAIR:.2f}")
print(f"config E: 144 MB vs 128 MB budget: overshoot x{144 / 128:.3f} "
      f"(absolute gap {144 - 128} MB)")
print(f"config D: 176 MB vs 128 MB budget: overshoot x{176 / 128:.3f}")
print(f"config B': far 500 vs required 1000: shortfall x{1000 / 500:.1f}")
need_mb = (4096 ** 2) * BYTES_PER_TEXEL / 1024 / 1024
print(f"single 4096 map already {need_mb:.0f} MB and only reaches "
      f"{far_cap_1:g} m; 1000 m at 5 cm on ONE map needs {D_REQ / P_STAIR:g} "
      f"texels/side -> {(D_REQ / P_STAIR) ** 2 * BYTES_PER_TEXEL / 1024 / 1024:,.0f} MB")
print(f"sensitivity: stair threshold at which A passes = {tA:.3f} m, "
      f"C passes = {tC:.3f} m (vs assumed {P_STAIR} m)")
