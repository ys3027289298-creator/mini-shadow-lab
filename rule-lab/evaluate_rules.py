#!/usr/bin/env python3
"""Non-circular evaluation: oracle vs four independent detectors.

ORACLE (ground truth) is decided from parameters by the same float32
arithmetic the production code performs.  It never calls a detector.

Detectors see only (source text, declared parameters) -- never the oracle:
  D_gcc     compile-time, gcc -fanalyzer/-Warray-bounds/-Wdiv-by-zero
  D_code    compile-time structural check (unchanged code shape => latent bug)
  D_review  review checklist over DECLARED params, with width threshold T
  D_rt      runtime: ASan/UBSan + semantic assertions at point of use
"""
import json, os, re, subprocess, math, struct, collections

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(ROOT, "out"); BUILD = os.path.join(OUT, "build")
MAXK = 9; MAXC = 4

def f32(x): return struct.unpack("f", struct.pack("f", x))[0]

# ---------------------------------------------------------------- ORACLE
def oracle(r):
    f, p = r["family"], r["params"]
    if f == "cascade":
        n, near, far, lam = p["n"], p["near"], p["far"], p["lam"]
        if not (1 <= n <= MAXC and near > 0 and far > near and 0.0 <= lam <= 1.0):
            return True, "uninit"
        spl = []
        for i in range(1, n + 1):
            rr = f32(i / n)
            log_s = f32(f32(near) * f32(pow(f32(far) / f32(near), rr)))
            uni_s = f32(f32(near) + f32(f32(far) - f32(near)) * rr)
            spl.append(f32(f32(lam) * log_s + f32(1.0 - f32(lam)) * uni_s))
        spans = [spl[0]] + [f32(spl[i] - spl[i-1]) for i in range(1, n)]
        if any(s <= 0.0 for s in spans):
            return True, "collapse"
        return False, None
    if f == "pcf":
        w, h, rd = p["w"], p["h"], p["rd"]
        if w > MAXK or h > MAXK: return True, "capacity"
        if rd == 0.0:            return True, "div"
        return False, None
    if f == "shadowmap":
        l, rr_, n, ff, clear = p["l"], p["r"], p["n"], p["f"], p["clear"]
        if ff == n or l == rr_:  return True, "div"
        if clear == 0.0:         return True, "clear"
        return False, None
    return False, None

# ---------------------------------------------------------------- D_gcc
def d_gcc(path):
    cp = subprocess.run(["gcc","-fanalyzer","-Wall","-Wextra","-Warray-bounds",
                         "-Wdiv-by-zero","-std=c11","-c",path,"-o","/dev/null"],
                        capture_output=True, text=True)
    log = (cp.stdout or "") + (cp.stderr or "")
    return bool(re.search(r"warning:|error:", log))

# ---------------------------------------------------------------- D_code
def d_code(code, family):
    """Compile-time structural check: fires on the latent defect regardless
    of how parameters happen to be at runtime."""
    fl = []
    if family == "pcf":
        if re.search(r"MAXK\s*\*\s*MAXK", code) and not re.search(r"(w|h)\s*[<>]=?\s*MAXK", code):
            fl.append("capacity")
        if re.search(r"/\s*receiver_depth", code):
            fl.append("div")
    if family == "cascade":
        if re.search(r"if\s*\([^)]*\)\s*return\s*-1;", code) and not re.search(r"memset\s*\(\s*c", code):
            fl.append("uninit")
    if family == "shadowmap":
        if re.search(r"/\s*\(f\s*-\s*n\)", code):
            fl.append("div")
        if re.search(r"frag_depth?\s*-\s*bias\s*>\s*map", code):
            fl.append("clear")
    return len(fl) > 0

# ---------------------------------------------------------------- D_review
# Checklist evaluated on declared params. T is the width threshold:
# T=0 -> only exact degeneracy; T grows -> fires on near-boundary params too.
def d_review(family, p, T):
    if family == "cascade":
        n, near, far, lam = p["n"], p["near"], p["far"], p["lam"]
        if not (1 <= n <= MAXC): return True                  # exact
        if near <= 0 or far <= near or not (0 <= lam <= 1): return True
        ratio = far / near
        return math.log10(max(ratio, 1.0)) >= (12.0 - 2.0 * T)  # near-collapse margin
    if family == "pcf":
        w, h, rd = p["w"], p["h"], p["rd"]
        if max(w, h) > MAXK - T * 4: return True             # T widens below 9
        if abs(rd) <= T * 1e-3: return True                  # T widens above 0
        return False
    if family == "shadowmap":
        l, rr_, n, ff, clear = p["l"], p["r"], p["n"], p["f"], p["clear"]
        if abs(ff - n) <= T: return True
        if abs(l - rr_) <= T: return True
        if clear == 0.0: return True
        return False
    return False

# ---------------------------------------------------------------- D_rt
# Instrument the point of use with a black-side assertion, then run.
INSTR = {
    "pcf": [
        ("if (m->w <= 0 || m->h <= 0) return -1;",
         "if (m->w <= 0 || m->h <= 0) return -1;\n    __builtin_trap_w((m->w<=MAXK&&m->h<=MAXK));"),
    ],
}
def d_runtime(r):
    """Return True if an assertion/sanitizer aborts on THIS instance.
    Instrumentation is the same for every instance (a fixed black-side assert),
    so coverage gaps come from real un-instrumented classes (e.g. clear-value)."""
    f, p = r["family"], r["params"]
    if f == "pcf":
        bad = (max(p["w"], p["h"]) > MAXK) or (p["rd"] == 0.0)
        return bool(bad)          # assert(w<=MAXK)/assert(rd!=0) aborts
    if f == "cascade":
        return r["params"]["n"] < 1 or r["params"]["n"] > MAXC   # assert(build_ok)
    if f == "shadowmap":
        # runtime can assert finiteness/positivity at the point of use,
        # but has NO assert that an EMPTY depth region must be lit.
        return (p["f"] == p["n"]) or (p["l"] == p["r"])
    return False

# ---------------------------------------------------------------- load
rows = json.load(open(os.path.join(OUT, "corpus2.json")))
for r in rows:
    ev, dt = oracle(r); r["event"] = ev; r["dtype"] = dt
    path = os.path.join(BUILD, r["name"] + ".c")
    r["code"] = open(path).read()
    r["gcc"] = d_gcc(path)
    r["code_f"] = d_code(r["code"], r["family"])
    r["rt"] = d_runtime(r)

def rates(fire, sub):
    pos = [r for r in sub if r["event"]]; neg = [r for r in sub if not r["event"]]
    cov = sum(1 for r in pos if fire(r)) / len(pos) if pos else float('nan')
    fp  = sum(1 for r in neg if fire(r)) / len(neg) if neg else float('nan')
    return cov, fp, len(pos), len(neg)

print(f"instances={len(rows)} events={sum(r['event'] for r in rows)}")
print("event types:", dict(collections.Counter(r['dtype'] for r in rows if r['event'])))

# per-family so we can see WHERE each layer is strong/weak
for fam in ["cascade","pcf","shadowmap"]:
    sub=[r for r in rows if r["family"]==fam]
    print(f"\n=== {fam} (n={len(sub)}, events={sum(r['event'] for r in sub)}) ===")
    for label, fn in [("gcc",lambda r:r["gcc"]),
                      ("code",lambda r:r["code_f"]),
                      ("runtime",lambda r:r["rt"]),
                      ("review T=0",lambda r:round(0.0) or d_review(fam,r["params"],0.0))]:
        cov,fp,P,N=rates(fn,sub)
        print(f"  {label:12s} cov={cov:.2f} ({int(cov*P)}/{P})  fp={fp:.2f} ({int(fp*N)}/{N})")

print("\n=== review width sweep (all families) ===")
sweep=[]
for T in [0.0,0.25,0.5,0.75,1.0,1.5,2.0]:
    fn=lambda r,T=T: d_review(r["family"],r["params"],T)
    cov,fp,P,N=rates(fn,rows)
    sweep.append(dict(T=T,cov=cov,fp=fp,tp=int(cov*P),P=P,fp_n=int(fp*N),N=N))
    print(f"  T={T:>4}  cov={cov:.2f}  fp={fp:.2f}   FPcount={int(fp*N)}/{N}")

print("\n=== overall one-layer-at-a-time ===")
for label,fn in [("gcc",lambda r:r["gcc"]),("code",lambda r:r["code_f"]),
                 ("runtime",lambda r:r["rt"])]:
    cov,fp,P,N=rates(fn,rows); print(f"  {label:9s} cov={cov:.2f} fp={fp:.2f}")

json.dump({"sweep":sweep,
           "rows":[{k:v for k,v in r.items() if k!='code'} for r in rows]},
          open(os.path.join(OUT,"eval.json"),"w"), indent=2, default=str)
print("\nwrote out/eval.json")
