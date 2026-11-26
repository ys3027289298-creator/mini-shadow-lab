#!/usr/bin/env python3
"""Runtime layer, measured for real.

For every corpus instance we build three binaries from the SAME emitted .c:
  sanitizer : -fsanitize=address,undefined only
  asserts   : sanitizer + fixed semantic assertions inserted at the point
              of use (same assertion text for all instances of a family)
  blackside : asserts + the 'failure must be black' predicate that the
              incident rule actually demands
We then run each and record whether it aborts. FLAGS=true means the binary
observed a violation and aborted (exit != 0 / sanitizer report).

The point: sanitizers catch UB only. Float div-by-zero, uninitialised-int
reads that are in-bounds, and 'cleared-to-0 means fully occluded' are NOT
UB, so sanitizers stay silent.  Semantic assertions are what catch them.
"""
import json, os, re, subprocess, collections

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(ROOT, "out"); BUILD = os.path.join(OUT, "build"); RB = os.path.join(OUT, "rt")
os.makedirs(RB, exist_ok=True)

rows = json.load(open(os.path.join(OUT, "corpus2.json")))

def f32(x):
    import struct; return struct.unpack("f", struct.pack("f", x))[0]
def oracle(r):
    f,p=r["family"],r["params"]
    if f=="cascade":
        n,near,far,lam=p["n"],p["near"],p["far"],p["lam"]
        if not(1<=n<=4 and near>0 and far>near and 0.0<=lam<=1.0): return True,"uninit"
        spl=[]
        for i in range(1,n+1):
            rr=f32(i/n); log_s=f32(f32(near)*f32(pow(f32(far)/f32(near),rr)))
            uni_s=f32(f32(near)+f32(f32(far)-f32(near))*rr)
            spl.append(f32(f32(lam)*log_s+f32(1.0-f32(lam))*uni_s))
        sp=[spl[0]]+[f32(spl[i]-spl[i-1]) for i in range(1,n)]
        return (any(s<=0.0 for s in sp)),("collapse" if any(s<=0.0 for s in sp) else None)
    if f=="pcf":
        if max(p["w"],p["h"])>9: return True,"capacity"
        if p["rd"]==0.0: return True,"div"
        return False,None
    if f=="shadowmap":
        if p["f"]==p["n"] or p["l"]==p["r"]: return True,"div"
        if p["clear"]==0.0: return True,"clear"
        return False,None
    return False,None

# --- instrument: insert assertions right before the dangerous use ----
def instrument(code, family):
    if family=="pcf":
        code=code.replace("int lit = 0, total = 0;",
            "assert(m->w <= MAXK && m->h <= MAXK);\n    int lit = 0, total = 0;")
        if "/ receiver_depth" in code:
            code=code.replace("float d = receiver_depth - blocker_depth;",
                "assert(receiver_depth != 0.0f);\n    float d = receiver_depth - blocker_depth;")
    if family=="cascade":
        code=code.replace("Cascades c;",
            "Cascades c; memset(&c,0,sizeof c);")
        code=code.replace("build(&c",
            "int _rc=build(&c")
        code=re.sub(r"int _rc=build\((&c[^;]*);", r"int _rc=build(\1; assert(_rc==0);", code)
        code=code.replace("float a = texel_world(&c, 0",
            "float a = texel_world(&c, 0")
        code=code.replace("printf(\"a=",
            "assert(isfinite(a) && a>0.0f);\n    printf(\"a=")
    if family=="shadowmap":
        code=code.replace("mat_identity(m);",
            "assert(!(f==n) && !(l==r) && !(t==b));\n    mat_identity(m);")
        code=code.replace("printf(\"ndc_z=",
            "assert(isfinite(lp.z) && isfinite(lp.w));\n    printf(\"ndc_z=")
    if "#include <assert.h>" not in code:
        code=code.replace("#include <stdio.h>","#include <stdio.h>\n#include <assert.h>")
    return code

# --- blackside: adds the predicate 'an unwritten/cleared region must be lit' ---
def blackside(code, family):
    if family=="shadowmap":
        # the incident rule: cleared depth region must NOT be fully occluded
        code=code.replace("printf(\"ndc_z=",
            "assert(sh < 1000);  /* cleared region must not be 100% occluded */\n    printf(\"ndc_z=")
    return code

def build_run(src, name, tag, extra):
    cf=os.path.join(RB,f"{name}_{tag}.c"); exe=cf[:-2]
    open(cf,"w").write(src)
    cp=subprocess.run(["gcc","-O1","-g","-std=c11","-fsanitize=address,undefined",
                       "-fno-omit-frame-pointer","-lm",cf,"-o",exe]+extra,
                      capture_output=True,text=True)
    if cp.returncode!=0: return "BUILDFAIL", cp.stderr[:150]
    try:
        r=subprocess.run([exe],capture_output=True,text=True,timeout=8,
                         env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0"})
        out=(r.stdout or "")+(r.stderr or "")
        fired = (r.returncode!=0) or ("AddressSanitizer" in out) or \
                ("runtime error" in out) or ("Assertion" in out)
        return fired, out[:150]
    except subprocess.TimeoutExpired:
        return True, "timeout"

recs=[]
for r in rows:
    path=os.path.join(BUILD,r["name"]+".c")
    code=open(path).read()
    ev,dt=oracle(r)
    b_san,o_san=build_run(code,r["name"],"san",["-w"])
    b_asm,o_asm=build_run(instrument(code,r["family"]),r["name"],"asm",["-w"])
    b_bs ,o_bs =build_run(blackside(instrument(code,r["family"]),r["family"]),r["name"],"bs",["-w"])
    recs.append(dict(name=r["name"],family=r["family"],event=ev,dtype=dt,
                     san=b_san,asm=b_asm,bs=b_bs,
                     o_san=str(o_san),o_asm=str(o_asm),o_bs=str(o_bs)))

def rates(key,sub):
    pos=[x for x in sub if x["event"]]; neg=[x for x in sub if not x["event"]]
    cov=sum(1 for x in pos if x[key])/len(pos) if pos else float('nan')
    fp =sum(1 for x in neg if x[key])/len(neg) if neg else float('nan')
    return cov,fp,len(pos),len(neg)

print(f"instances={len(recs)} events={sum(x['event'] for x in recs)}")
print("\n=== runtime layer, measured per family ===")
for fam in ["cascade","pcf","shadowmap"]:
    sub=[x for x in recs if x["family"]==fam]
    print(f"\n{fam} (n={len(sub)}, events={sum(x['event'] for x in sub)})")
    for key,label in [("san","sanitizer only"),("asm","+semantic assert"),("bs","+black-side assert")]:
        cov,fp,P,N=rates(key,sub)
        print(f"  {label:20s} cov={cov:.2f} ({int(cov*P)}/{P})  fp={fp:.2f} ({int(fp*N)}/{N})")

print("\n=== runtime layer, overall ===")
for key,label in [("san","sanitizer only"),("asm","+semantic assert"),("bs","+black-side assert")]:
    cov,fp,P,N=rates(key,recs)
    print(f"  {label:20s} cov={cov:.2f} ({int(cov*P)}/{P})  fp={fp:.2f} ({int(fp*N)}/{N})")

print("\n=== which classes each runtime variant catches ===")
for key in ["san","asm","bs"]:
    byc=collections.defaultdict(lambda:[0,0])
    for x in recs:
        if x["event"]:
            byc[x["dtype"]][0]+=1
            if x[key]: byc[x["dtype"]][1]+=1
    print(f"  {key}: "+", ".join(f"{k}={v[1]}/{v[0]}" for k,v in sorted(byc.items())))

json.dump(recs,open(os.path.join(OUT,"runtime.json"),"w"),indent=2)
print("\nwrote out/runtime.json")
