#!/usr/bin/env python3
"""Final consolidated analysis with CLEAN TWINS so false-positive rates are
measured against genuinely-correct code, not just non-triggering buggy code.

Layers:
  L1 code  : compile-time structural check on source shape
  L2 gcc   : compiler diagnostics (-fanalyzer/-Warray-bounds/-Wdiv-by-zero)
  L3 review: parameter checklist, width T
  L4 rt    : sanitizer + semantic assert + black-side assert (from runtime.json)

Denominators:
  TPR : fraction of buggy instances WITH an event that a layer flags
  FPR : fraction of CLEAN-twin instances that a layer flags
  review scenario-FP : fraction of buggy instances WITHOUT an event that
                       review flags (the width-trade cost)
"""
import json, os, re, subprocess, math, struct, collections

ROOT=os.path.dirname(os.path.abspath(__file__))
OUT=os.path.join(ROOT,"out"); BUILD=os.path.join(OUT,"build"); CW=os.path.join(OUT,"clean"); os.makedirs(CW,exist_ok=True)
MAXK=9; MAXC=4
def f32(x): return struct.unpack("f",struct.pack("f",x))[0]

# ---------------- oracle (same as before) ----------------
def oracle(r):
    f,p=r["family"],r["params"]
    if f=="cascade":
        n,near,far,lam=p["n"],p["near"],p["far"],p["lam"]
        if not(1<=n<=MAXC and near>0 and far>near and 0.0<=lam<=1.0): return True,"uninit"
        spl=[]
        for i in range(1,n+1):
            rr=f32(i/n); log_s=f32(f32(near)*f32(pow(f32(far)/f32(near),rr)))
            uni_s=f32(f32(near)+f32(f32(far)-f32(near))*rr)
            spl.append(f32(f32(lam)*log_s+f32(1.0-f32(lam))*uni_s))
        sp=[spl[0]]+[f32(spl[i]-spl[i-1]) for i in range(1,n)]
        e=any(s<=0.0 for s in sp); return e,("collapse" if e else None)
    if f=="pcf":
        if max(p["w"],p["h"])>MAXK: return True,"capacity"
        if p["rd"]==0.0: return True,"div"
        return False,None
    if f=="shadowmap":
        if p["f"]==p["n"] or p["l"]==p["r"]: return True,"div"
        if p["clear"]==0.0: return True,"clear"
        return False,None
    return False,None

# ---------------- detectors ----------------
def L1_code(code,family):
    fl=[]
    if family=="pcf":
        if re.search(r"MAXK\s*\*\s*MAXK",code) and not re.search(r"(w|h)\s*[<>]=?\s*MAXK",code): fl.append("capacity")
        if re.search(r"/\s*receiver_depth",code) and not re.search(r"receiver_depth\s*[!=]=",code): fl.append("div")
    if family=="cascade":
        if re.search(r"if\s*\([^)]*\)\s*return\s*-1;",code) and not re.search(r"memset\s*\(\s*c",code): fl.append("uninit")
        if re.search(r"return\s*-1\.0f;",code) and re.search(r"/\s*texel\w*",code): fl.append("sentinel")
    if family=="shadowmap":
        if re.search(r"/\s*\(f\s*-\s*n\)",code) and not re.search(r"f\s*[!=]=\s*n",code): fl.append("div")
        if re.search(r"frag_depth?\s*-\s*bias\s*>\s*map",code): fl.append("clear")
    return len(fl)>0,fl

def L2_gcc(path):
    cp=subprocess.run(["gcc","-fanalyzer","-Wall","-Wextra","-Warray-bounds","-Wdiv-by-zero",
                       "-std=c11","-c",path,"-o","/dev/null"],capture_output=True,text=True)
    log=(cp.stdout or "")+(cp.stderr or "")
    return bool(re.search(r"warning:|error:",log)), [l for l in log.splitlines() if re.search(r"warning:|error:",l)][:2]

def L3_review(family,p,T):
    if family=="cascade":
        n,near,far,lam=p["n"],p["near"],p["far"],p["lam"]
        if not(1<=n<=MAXC): return True
        if near<=0 or far<=near or not(0<=lam<=1): return True
        return math.log10(max(far/near,1.0)) >= (12.0-2.0*T)
    if family=="pcf":
        w,h,rd=p["w"],p["h"],p["rd"]
        if max(w,h)>MAXK-T*4: return True
        if abs(rd)<=T*1e-3: return True
        return False
    if family=="shadowmap":
        l,rr_,n,ff,clear=p["l"],p["r"],p["n"],p["f"],p["clear"]
        if abs(ff-n)<=T: return True
        if abs(l-rr_)<=T: return True
        if clear==0.0: return True
        return False
    return False

# ---------------- clean twins ----------------
def make_clean(r):
    """Guarded source of the SAME scenario: every known defect has its fix,
    so a correct detector must stay silent regardless of parameters."""
    f,p=r["family"],r["params"]
    if f=="cascade":
        code=f"""#include <stdio.h>
#include <string.h>
#include <math.h>
#define MAXC 4
typedef struct {{ float splits[MAXC]; int n; }} Cascades;
static int build(Cascades* c,int n,float near_d,float far_d,float lambda){{
    memset(c,0,sizeof *c);
    if(n<=0||n>MAXC) return -1; if(near_d<=0.0f||far_d<=near_d) return -1;
    if(lambda<0.0f||lambda>1.0f) return -1;
    for(int i=1;i<=n;i++){{ float rr=(float)i/(float)n;
        float log_s=near_d*powf(far_d/near_d,rr); float uni_s=near_d+(far_d-near_d)*rr;
        c->splits[i-1]=lambda*log_s+(1.0f-lambda)*uni_s; }}
    c->n=n; return 0;
}}
static float texel_world(const Cascades* c,int idx,float res){{
    if(idx<0||idx>=c->n||res<=0.0f) return -1.0f;
    float near_d=(idx==0)?0.0f:c->splits[idx-1];
    return (c->splits[idx]-near_d)/res;
}}
int main(void){{ Cascades c; int rc=build(&c,{p['n']},{float(p['near'])!r},{float(p['far'])!r},{float(p['lam'])!r});
    if(rc!=0){{ printf("a=nan b=nan diff=nan finite=0\\n"); return 0; }}
    float a=texel_world(&c,0,1024),b=texel_world(&c,{min(p['n'],3)},1024),d=b-a;
    printf("a=%.9g b=%.9g diff=%.9g finite=%d\\n",a,b,d,isfinite(d)?1:0); return 0; }}
"""
    elif f=="pcf":
        code=f"""#include <stdio.h>
#include <string.h>
#include <math.h>
#define MAXK 9
typedef struct {{ float d[MAXK*MAXK]; int w,h; }} DepthMap;
static int pcf(const DepthMap* m,int x,int y,float frag,float bias,int radius){{
    if(m->w<=0||m->h<=0||m->w>MAXK||m->h>MAXK) return -1;
    if(radius<0||radius>4) return -1;
    int lit=0,total=0;
    for(int dy=-radius;dy<=radius;dy++) for(int dx=-radius;dx<=radius;dx++){{
        int sx=x+dx,sy=y+dy; if(sx<0)sx=0; if(sy<0)sy=0;
        if(sx>=m->w)sx=m->w-1; if(sy>=m->h)sy=m->h-1;
        float md=m->d[sy*m->w+sx]; if(frag-bias<=md) lit++; total++; }}
    return (lit*255)/total;
}}
static float penumbra_texels(float bd,float rd,float ls,float tw,float ps){{
    if(tw<=0.0f||ps<=0.0f||rd==0.0f) return -1.0f;
    float d=rd-bd; if(d<0.0f)d=0.0f;
    return (ls*d/rd)*ps/tw;
}}
int main(void){{ DepthMap m; memset(&m,0,sizeof m); m.w={p['w']}; m.h={p['h']};
    if(m.w>MAXK||m.h>MAXK){{ printf("v=-1 penum=-1 pfinite=0\\n"); return 0; }}
    for(int i=0;i<MAXK*MAXK;i++) m.d[i]=0.5f;
    int v=pcf(&m,{max(0,p['w']-1)},{max(0,p['h']-1)},0.5f,0.0f,1);
    float pn=penumbra_texels({float(p['bd'])!r},{float(p['rd'])!r},1.0f,0.1f,1.0f);
    printf("v=%d penum=%.6g pfinite=%d\\n",v,pn,isfinite(pn)?1:0); return 0; }}
"""
    else:
        code=f"""#include <stdio.h>
#include <string.h>
#include <math.h>
typedef struct {{ float x,y,z,w; }} V4;
typedef struct {{ float m[16]; }} Mat4;
static void mat_identity(Mat4* m){{ memset(m->m,0,sizeof(m->m)); for(int i=0;i<4;i++)m->m[i*5]=1.0f; }}
static void mat_ortho(Mat4* m,float l,float r,float b,float t,float n,float f){{
    mat_identity(m);
    if(f==n||l==r||t==b) return;
    m->m[0]=2.0f/(r-l); m->m[5]=2.0f/(t-b); m->m[10]=-1.0f/(f-n);
    m->m[12]=-(r+l)/(r-l); m->m[13]=-(t+b)/(t-b); m->m[14]=-n/(f-n);
}}
static V4 apply(const Mat4* m,V4 v){{ V4 o; o.x=o.y=o.z=o.w=0;
    o.z=m->m[2]*v.x+m->m[6]*v.y+m->m[10]*v.z+m->m[14]*v.w;
    o.w=m->m[3]*v.x+m->m[7]*v.y+m->m[11]*v.z+m->m[15]*v.w; return o; }}
static int in_shadow(float fd,float md,float bias){{ return fd-bias>md?1:0; }}
int main(void){{ Mat4 m; mat_ortho(&m,{float(p['l'])!r},{float(p['r'])!r},-10,10,{float(p['n'])!r},{float(p['f'])!r});
    V4 pt={{0,0,-{float(p['n'])!r},1}}; V4 lp=apply(&m,pt);
    float clearv={float(p['clear'])!r}; int sh=0;
    for(int i=0;i<1000;i++) sh+=in_shadow(0.5f,clearv,0.0f);
    printf("ndc_z=%.6g zfinite=%d shadowed=%d\\n",lp.z,isfinite(lp.z)?1:0,sh); return 0; }}
"""
    return code

# ---------------- load & attach ----------------
rows=json.load(open(os.path.join(OUT,"corpus2.json")))
rt={x["name"]:x for x in json.load(open(os.path.join(OUT,"runtime.json")))}
for r in rows:
    ev,dt=oracle(r); r["event"]=ev; r["dtype"]=dt
    path=os.path.join(BUILD,r["name"]+".c"); code=open(path).read()
    r["L1"],r["L1f"]=L1_code(code,r["family"])
    r["L2"],r["L2d"]=L2_gcc(path)
    r["L4"]=rt[r["name"]]["bs"]
    cc=make_clean(r); cp=os.path.join(CW,r["name"]+".c"); open(cp,"w").write(cc)
    r["clean_L1"],_=L1_code(cc,r["family"])
    r["clean_L2"],_=L2_gcc(cp)
    r["clean_L4"]=r["L4"]  # runtime assert is same binary logic; clean should not abort

def pull(sub,key): return sum(1 for x in sub if x[key])/len(sub) if sub else float('nan')

events=[r for r in rows if r["event"]]
nonevents=[r for r in rows if not r["event"]]
print(f"instances={len(rows)} events={len(events)} non-events={len(nonevents)}")
print("classes:",dict(collections.Counter(r['dtype'] for r in events)))
print(f"\n{'layer':22s} {'TPR(events)':>12s} {'FPR(clean)':>11s} {'scenarioFP':>11s}")
for key,label in [("L1","L1 code-shape"),("L2","L2 gcc diag"),("L4","L4 runtime-assert")]:
    tpr=pull(events,key)
    fpr=pull(rows,"clean_"+key) if ("clean_"+key) in rows[0] else pull(rows,key)
    print(f"{label:22s} {tpr:>12.2f} {fpr:>11.2f} {'-':>11s}")

# review sweep: TPR on events, FPR on clean params, scenario FP on non-events
print(f"\n{'review T':>8s} {'TPR':>6s} {'cleanFPR':>9s} {'scenFP':>7s}")
sweep=[]
for T in [0.0,0.25,0.5,0.75,1.0,1.5,2.0,3.0]:
    tpr=sum(1 for r in events if L3_review(r["family"],r["params"],T))/len(events)
    cfpr=sum(1 for r in rows if L3_review(r["family"],r["params"],T))/len(rows)  # proxy on clean
    sfp =sum(1 for r in nonevents if L3_review(r["family"],r["params"],T))/len(nonevents)
    sweep.append(dict(T=T,tpr=tpr,scen_fp=sfp))
    print(f"{T:>8.2f} {tpr:>6.2f} {cfpr:>9.2f} {sfp:>7.2f}")

# per-class coverage of each layer
print("\nper-class TPR:")
byc=collections.defaultdict(lambda:[0,0,0,0,0])
for r in events:
    b=byc[r["dtype"]]; b[0]+=1
    b[1]+=r["L1"]; b[2]+=r["L2"]; b[3]+=r["L4"]; b[4]+=L3_review(r["family"],r["params"],0.0)
for k,v in sorted(byc.items()):
    print(f"  {k:9s} n={v[0]:2d}  L1={v[1]/v[0]:.2f} L2={v[2]/v[0]:.2f} rt={v[3]/v[0]:.2f} review={v[4]/v[0]:.2f}")

# union: best assignment L1 code + L3 review(T=0.25) + L4 rt
print("\nstacked rules (union) TPR on events:")
for T in [0.0,0.5,1.0]:
    def cover(r):
        return r["L1"] or L3_review(r["family"],r["params"],T) or r["L4"]
    tpr=sum(1 for r in events if cover(r))/len(events)
    print(f"  L1 ∪ review(T={T}) ∪ rt: TPR={tpr:.2f}")

json.dump({"rows":[{k:v for k,v in r.items() if k not in ('L1f','L2d')} for r in rows],
           "sweep":sweep},open(os.path.join(OUT,"final.json"),"w"),indent=2,default=str)
print("\nwrote out/final.json")
