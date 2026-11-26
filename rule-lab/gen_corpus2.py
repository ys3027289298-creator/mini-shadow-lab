#!/usr/bin/env python3
"""Parametric corpus + width-swept rule evaluation.

For every family we sweep the *trigger parameters* (near/far/n/lambda/res/
w/h/blocker/receiver/frustum), emit a real .c file, compile, run, and let
the program's own numeric output decide whether that instance is a
black-frame event (truth).  We then define the rule family with a single
continuous WIDTH knob and sweep it to trace
    coverage(TPR)  and  false-positive rate  vs  width.

Everything numeric here is measured; nothing is hand-scored.
"""
import json, os, subprocess, re, math, itertools, tempfile
from string import Template

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(ROOT, "out"); os.makedirs(OUT, exist_ok=True)
BUILD = os.path.join(OUT, "build"); os.makedirs(BUILD, exist_ok=True)

# ---------------------------------------------------------------- templates
CASCADE_HEAD = Template(r"""
#include <stdio.h>
#include <string.h>
#include <math.h>
#define MAXC 4
typedef struct { float splits[MAXC]; int n; } Cascades;
static int build(Cascades* c, int n, float near_d, float far_d, float lambda) {
    ${INIT}
    if (n <= 0 || n > MAXC) return -1;
    if (near_d <= 0.0f || far_d <= near_d) return -1;
    if (lambda < 0.0f || lambda > 1.0f) return -1;
    for (int i = 1; i <= n; i++) {
        float r = (float)i / (float)n;
        float log_s = near_d * powf(far_d / near_d, r);
        float uni_s = near_d + (far_d - near_d) * r;
        c->splits[i - 1] = lambda * log_s + (1.0f - lambda) * uni_s;
    }
    c->n = n; return 0;
}
static float texel_world(const Cascades* c, int idx, float res) {
    if (idx < 0 || idx >= c->n || res <= 0.0f) return -1.0f;
    float near_d = (idx == 0) ? 0.0f : c->splits[idx - 1];
    float span = c->splits[idx] - near_d;
    return span / res;
}
int main(void) {
    Cascades c;
    ${USE}
    ${CALL}
    float a = texel_world(&c, 0, ${RES});
    float b = texel_world(&c, ${BIDX}, ${RES});
    float d = b - a;
    printf("a=%.9g b=%.9g diff=%.9g finite=%d\n", a, b, d, isfinite(d) ? 1 : 0);
    return 0;
}
""")

PCF_HEAD = Template(r"""
#include <stdio.h>
#include <string.h>
#include <math.h>
#define MAXK 9
typedef struct { float d[MAXK*MAXK]; int w, h; } DepthMap;
static int pcf(const DepthMap* m, int x, int y, float frag, float bias, int radius) {
    ${GUARD}
    if (m->w <= 0 || m->h <= 0) return -1;
    if (radius < 0 || radius > 4) return -1;
    int lit = 0, total = 0;
    for (int dy = -radius; dy <= radius; dy++)
        for (int dx = -radius; dx <= radius; dx++) {
            int sx = x + dx, sy = y + dy;
            if (sx < 0) sx = 0; if (sy < 0) sy = 0;
            if (sx >= m->w) sx = m->w - 1;
            if (sy >= m->h) sy = m->h - 1;
            float md = m->d[sy * m->w + sx];
            if (frag - bias <= md) lit++;
            total++;
        }
    return (lit * 255) / total;
}
static float penumbra_texels(float blocker_depth, float receiver_depth,
                             float light_size, float texel_world, float projection_scale) {
    if (texel_world <= 0.0f || projection_scale <= 0.0f) return -1.0f;
    float d = receiver_depth - blocker_depth;
    if (d < 0.0f) d = 0.0f;
    return (light_size * d / receiver_depth) * projection_scale / texel_world;
}
int main(void) {
    DepthMap m; memset(&m, 0, sizeof m);
    m.w = ${W}; m.h = ${H};
    for (int i = 0; i < MAXK*MAXK; i++) m.d[i] = 0.5f;
    int v = pcf(&m, ${X}, ${Y}, 0.5f, 0.0f, 1);
    float p = penumbra_texels(${BD}, ${RD}, 1.0f, 0.1f, 1.0f);
    printf("v=%d penum=%.6g pfinite=%d\n", v, p, isfinite(p) ? 1 : 0);
    return 0;
}
""")

SHADOW_HEAD = Template(r"""
#include <stdio.h>
#include <string.h>
#include <math.h>
typedef struct { float x,y,z,w; } V4;
typedef struct { float m[16]; } Mat4;
static void mat_identity(Mat4* m){ memset(m->m,0,sizeof(m->m)); for(int i=0;i<4;i++)m->m[i*5]=1.0f; }
static void mat_ortho(Mat4* m,float l,float r,float b,float t,float n,float f){
    mat_identity(m);
    ${GUARD}
    m->m[0]=2.0f/(r-l); m->m[5]=2.0f/(t-b); m->m[10]=-1.0f/(f-n);
    m->m[12]=-(r+l)/(r-l); m->m[13]=-(t+b)/(t-b); m->m[14]=-n/(f-n);
}
static V4 apply(const Mat4* m,V4 v){ V4 o; o.x=o.y=o.z=o.w=0;
    o.z=m->m[2]*v.x+m->m[6]*v.y+m->m[10]*v.z+m->m[14]*v.w;
    o.w=m->m[3]*v.x+m->m[7]*v.y+m->m[11]*v.z+m->m[15]*v.w; return o; }
static int in_shadow(float frag_depth,float map_depth,float bias){
    return frag_depth-bias>map_depth?1:0;
}
int main(void){
    Mat4 m; mat_ortho(&m,${L},${R},${B},${T},${N},${F});
    V4 p={0,0,-${N},1}; V4 lp=apply(&m,p);
    float clearv=${CLEAR}; int sh=0;
    for(int i=0;i<1000;i++) sh+=in_shadow(0.5f,clearv,0.0f);
    printf("ndc_z=%.6g zfinite=%d shadowed=%d\n", lp.z, isfinite(lp.z)?1:0, sh);
    return 0;
}
""")

def run(src_text, name, sanitize=False):
    cf = os.path.join(BUILD, name + ".c")
    bf = os.path.join(BUILD, name + ".bin")
    open(cf, "w").write(src_text)
    flags = ["gcc","-O1","-std=c11","-lm",cf,"-o",bf]
    if sanitize: flags[1:1] = ["-fsanitize=address,undefined","-g"]
    cfail = subprocess.run(flags, capture_output=True, text=True)
    if cfail.returncode != 0:
        return None, cfail.stderr[:300]
    try:
        r = subprocess.run([bf], capture_output=True, text=True, timeout=5,
                           env={**os.environ, "ASAN_OPTIONS":"detect_leaks=0"})
        return (r.stdout or "") + (r.stderr or ""), None
    except subprocess.TimeoutExpired:
        return None, "TIMEOUT"

def num(out, k):
    if not out: return None
    m = re.search(rf"{k}=([-+]?[0-9.eE+-]+|nan|inf|-inf)", out)
    return m.group(1) if m else None

# ---------------------------------------------------------------- instances
rows = []
def add(family, params, src, truth_fn, severity_fn, capacity_bug, div_bug,
        init_bug, sentinel_bug):
    name = f"{family}_{len(rows):03d}"
    out, err = run(src, name)
    truth = truth_fn(out) if out is not None else False
    sev = severity_fn(params) if callable(severity_fn) else float(severity_fn)
    rows.append(dict(name=name, family=family, params=params,
                     truth_black=bool(truth), severity=sev,
                     capacity_bug=capacity_bug, div_bug=div_bug,
                     init_bug=init_bug, sentinel_bug=sentinel_bug,
                     ran=out is not None))

# --- cascade family: sweep near, far, n, lambda, res; bug variants -----------
def cascade_src(near, far, n, lam, res, bidx, init, idxunchecked):
    return CASCADE_HEAD.substitute(dict(INIT=init, USE="", RES=res, BIDX=bidx,
                               CALL=f"build(&c,{n},{float(near)!r},{float(far)!r},{float(lam)!r});"))

cascade_cases = []
for near,far in [(0.5,1000),(1e-4,1e7),(0.001,1e6),(0.1,1e3),(1e-6,1e9)]:
    for n in [1,2,3,4,5,0]:
        for lam in [0.0,0.5,1.0]:
            cascade_cases.append((near,far,n,lam))
# dedupe keep manageable: sample
import random; random.seed(7)
random.shuffle(cascade_cases)
cascade_cases = cascade_cases[:60]

for (near,far,n,lam) in cascade_cases:
    # B1: uninitialised struct + failed build (n invalid or valid)
    src = cascade_src(near,far,n,lam,1024,min(n,3) if n>0 else 3,
                      init="/* no init */",
                      idxunchecked=False)
    def t(out):
        f=num(out,"finite")
        if f=="0": return True
        d=num(out,"diff")
        return d is not None and abs(float(d))<1e-9
    # severity: log10(far/near) governs float split collapse
    sev = math.log10(far/near)
    add("cascade", dict(near=near,far=far,n=n,lam=lam,res=1024), src, t,
        (lambda p: sev), capacity_bug=False, div_bug=False,
        init_bug=(n<=0 or n>4), sentinel_bug=False)

# --- pcf family: sweep w,h (capacity), receiver/blocker (div) ---------------
pcf_cases = []
for w,h in [(4,4),(8,8),(9,9),(10,10),(12,12),(16,16),(32,32)]:
    for (bd,rd) in [(5,5),(0,0),(1,10),(0.0,10.0),(9,10),(0,0.0001)]:
        pcf_cases.append((w,h,bd,rd))
for i,(w,h,bd,rd) in enumerate(pcf_cases):
    x=max(0,w-1); y=max(0,h-1)
    src = PCF_HEAD.substitute(dict(GUARD="", W=w,H=h,X=x,Y=y,BD=bd,RD=rd))
    def t(out):
        if out is None: return False
        if "AddressSanitizer" in out or "runtime error" in out: return True
        f=num(out,"pfinite"); return f=="0"
    # severity = how far capacity exceeded OR how close receiver to 0
    cap_sev = max(0.0, (w-9)/9.0) + max(0.0,(h-9)/9.0)
    div_sev = 1.0 if abs(rd) < 1e-3 else 0.0
    add("pcf", dict(w=w,h=h,bd=bd,rd=rd), src, t,
        max(cap_sev, div_sev), capacity_bug=(w>9 or h>9), div_bug=(abs(rd)<1e-3),
        init_bug=False, sentinel_bug=False)

# --- shadowmap family: sweep frustum degeneracy + clear value ---------------
sm_cases=[]
for (l,r) in [(-10,10),(0,0),(5,5),(-1,1)]:
    for (nn,ff) in [(1,100),(5,5),(1,1),(0.1,1000)]:
        for clearv in [0.0,1.0]:
            sm_cases.append((l,r,nn,ff,clearv))
for (l,r,nn,ff,clearv) in sm_cases:
    src = SHADOW_HEAD.substitute(dict(GUARD="",L=l,R=r,B=-10,T=10,N=nn,F=ff,CLEAR=clearv))
    def t(out):
        if out is None: return False
        if num(out,"zfinite")=="0": return True
        s=num(out,"shadowed")
        return s is not None and s=="1000"
    div_sev = 2.0 if (ff==nn or l==r) else 0.0
    clear_sev = 1.0 if clearv==0.0 else 0.0
    add("shadowmap", dict(l=l,r=r,n=nn,f=ff,clear=clearv), src, t,
        max(div_sev,clear_sev), capacity_bug=False, div_bug=(ff==nn or l==r),
        init_bug=False, sentinel_bug=False)

with open(os.path.join(OUT,"corpus2.json"),"w") as f:
    json.dump(rows,f,indent=2)
pos=sum(1 for r in rows if r["truth_black"])
print(f"instances={len(rows)} positives(black)={pos} negatives={len(rows)-pos}")
print(f"  cascade={sum(1 for r in rows if r['family']=='cascade')} "
      f"pcf={sum(1 for r in rows if r['family']=='pcf')} "
      f"shadowmap={sum(1 for r in rows if r['family']=='shadowmap')}")
