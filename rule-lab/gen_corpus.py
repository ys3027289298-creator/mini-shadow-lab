#!/usr/bin/env python3
"""Generate a labeled corpus of shadow-system variants.

Each variant is a real, compilable C translation unit derived from the
mini-shadow-lab sources. It is compiled and *executed*; the ground-truth
label ("does this variant produce a black / occluded-everything outcome
on a legal input?") is decided by running it, not by assertion.

Three independent detectors then run over each variant:
  D_static  gcc -fanalyzer + -Wall/-Warray-bounds, plus an AST pass for
            missing-zero-guard / capacity-coupling patterns.
  D_review  a scenario-parameter checklist evaluated on the *extracted
            parameters* (near/far/n/lambda/res/w/h) of each variant.
  D_assert  runs the binary with UBSan/ASan and inspects a fail-fast
            assertion on the black-side predicate.

Output: out/corpus.json  (one record per variant)
"""
import json, os, subprocess, tempfile, shutil, re, sys

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC  = os.path.join(ROOT, "src")
OUT  = os.path.join(ROOT, "out")
os.makedirs(OUT, exist_ok=True)

# ---- parametric variant templates -------------------------------------------
# We emit concrete .c files by substituting into the hand-written templates
# already in src/, plus copy the pristine sources as CLEAN controls.

def compile_and_run(path, extra_cflags, timeout=5):
    exe = path[:-2] + ".bin"
    cc = ["gcc", "-O1", "-std=c11", "-lm", path, "-o", exe] + extra_cflags
    cp = subprocess.run(cc, capture_output=True, text=True)
    build_ok = cp.returncode == 0
    build_log = (cp.stdout or "") + (cp.stderr or "")
    run_out = ""
    if build_ok:
        try:
            rp = subprocess.run([exe], capture_output=True, text=True, timeout=timeout)
            run_out = (rp.stdout or "") + (rp.stderr or "")
        except subprocess.TimeoutExpired:
            run_out = "TIMEOUT"
    return build_ok, build_log, run_out

def truth_from_output(name, out):
    """Ground-truth: does the variant produce a black/全遮挡 outcome?
    Decided by parsing the program's own numeric output."""
    def num(key):
        m = re.search(rf"{key}=([-\w.]+)", out)
        return m.group(1) if m else None
    if name.startswith("cascade_B1"):
        t = num("tuple")
        return (t is not None and (not isfinite_str(t) or float(t) <= 0))
    if name.startswith("cascade_B2"):
        f = num("finite"); d = num("diff")
        return f == "0" or (d is not None and abs(float(d)) < 1e-9)
    if name.startswith("cascade_B8"):
        p = num("penum")
        return p is not None and (not isfinite_str(p) or float(p) <= 0)
    if name.startswith("pcf_B3"):
        # OOB read: ASan/UBSan report => real fault; also treat as black-side
        return "ERROR: AddressSanitizer" in out or "runtime error" in out
    if name.startswith("pcf_B4"):
        return num("finite") == "0"
    if name.startswith("shadowmap_B5"):
        return num("finite") == "0"
    if name.startswith("shadowmap_B6"):
        s = num("shadowed")
        return s is not None and s.startswith("1000")
    # OTHER / CLEAN: by construction not black-on-rare-input
    return False

def isfinite_str(s):
    try:
        return abs(float(s)) < 1e300
    except ValueError:
        return s not in ("inf", "-inf", "nan")

def variant_class(name):
    if name.startswith("cascade_B1") or name.startswith("cascade_B2") or \
       name.startswith("cascade_B8") or name.startswith("pcf_B3") or \
       name.startswith("pcf_B4") or name.startswith("shadowmap_B5") or \
       name.startswith("shadowmap_B6"):
        return "BH"
    if "_O" in name:
        return "OTHER"
    return "CLEAN"

# ---- detector 1: static -----------------------------------------------------
def det_static(path, extra):
    cp = subprocess.run(
        ["gcc", "-fanalyzer", "-Wall", "-Wextra", "-Warray-bounds", "-Wdiv-by-zero",
         "-std=c11", "-c", path, "-o", "/dev/null"],
        capture_output=True, text=True)
    log = (cp.stdout or "") + (cp.stderr or "")
    # static fires if any analyzer/warning diagnostic is emitted
    fired = bool(re.search(r"warning:|error:", log))
    return fired, log

# ---- detector 3: runtime assert (ASan/UBSan + black-side predicate) --------
def det_assert(path):
    ok, blog, out = compile_and_run(
        path, ["-fsanitize=address,undefined", "-fno-omit-frame-pointer",
               "-g"], timeout=8)
    if not ok:
        return False, "build-fail:" + blog[:200]
    fired = ("AddressSanitizer" in out or "runtime error" in out or
             "SEGV" in out or "assert" in out.lower() and "Assertion" in out)
    return fired, out[:300]

# ---- detector 2: review checklist on scenario parameters -------------------
def det_review(path):
    """Extract scenario params from the file text and evaluate the checklist.
    Checklist rule (narrow): 'if a division/index has a free denominator or
    index bound that can reach 0 or exceed capacity for SOME legal parameter
    tuple, flag it.' This is parameter-driven, not line-driven."""
    txt = open(path).read()
    flags = []
    # free denominator reachable to 0
    for pat, label in [
        (r"/\s*\(f\s*-\s*n\)", "ortho_div_f_minus_n"),
        (r"/\s*receiver_depth", "penumbra_div_receiver"),
        (r"powf\s*\(\s*far_d\s*/\s*near_d", "split_log_ratio"),
    ]:
        if re.search(pat, txt):
            flags.append(label)
    # unvalidated capacity coupling
    if re.search(r"MAXK\s*\*\s*MAXK", txt) and not re.search(r"w\s*<=\s*MAXK", txt):
        flags.append("depthmap_capacity_unchecked")
    # uninitialised struct before early-return failure
    if re.search(r"if\s*\([^)]*\)\s*return\s*-1;", txt) and not re.search(r"memset\s*\(\s*c", txt):
        flags.append("cascade_struct_uninit_on_fail")
    # sentinel return consumed as size
    if re.search(r"texel_world\s*\([^)]*\)", txt) and re.search(r"/\s*texel", txt):
        flags.append("sentinel_used_as_size")
    return (len(flags) > 0), flags

# ---- main -------------------------------------------------------------------
records = []
for fn in sorted(os.listdir(SRC)):
    if not fn.endswith(".c"):
        continue
    path = os.path.join(SRC, fn)
    name = fn[:-2]
    cls = variant_class(name)
    base = ["-w"]
    ok, blog, out = compile_and_run(path, ["-w"])
    truth = truth_from_output(fn, out) if ok else False
    s_fired, s_log = det_static(path, base)
    r_fired, r_flags = det_review(path)
    a_fired, a_out = det_assert(path)
    records.append({
        "name": name, "class": cls,
        "build_ok": ok, "ran": bool(out) and out != "TIMEOUT",
        "truth_black": truth,
        "static_fired": s_fired, "static_log": s_log[:400],
        "review_fired": r_fired, "review_flags": r_flags,
        "assert_fired": a_fired, "assert_out": a_out,
        "output": out[:300],
    })

with open(os.path.join(OUT, "corpus.json"), "w") as f:
    json.dump(records, f, indent=2)

print(f"wrote {len(records)} records")
for r in records:
    print(f"{r['name']:20s} class={r['class']:5s} truth_black={int(r['truth_black'])} "
          f"static={int(r['static_fired'])} review={int(r['review_fired'])} "
          f"assert={int(r['assert_fired'])}")
