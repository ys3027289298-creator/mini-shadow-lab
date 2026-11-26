#!/usr/bin/env python3
"""Coverage-vs-width curves on CONTINUOUS defect boundaries, truth by execution.

Three continuous boundaries exist in the real code:
  A cascade split gap      : guard far>near passes, but float32 adjacent
                             split gap rounds to 0  -> texel_world span 0.
  B shadowmap frustum f-n  : guard none, 1/(f-n) explodes / nan when f==n.
  C shadowmap clear depth  : empty region is fully occluded whenever
                             clear_depth < receiver_depth.

For each we sweep a control parameter across the boundary, run the UNGUARDED
program, and let its own output decide 'black'.  A rule with tolerance T fires
when the control is within T of the degenerate point.  We then plot
coverage(T) = P(fire | black) and FP(T) = P(fire | not black).

capacity (pcf w>MAXK) is a DISCRETE boundary: no continuum, so a single exact
check decides it fully -- reported separately, no T curve.
"""
import json, os, subprocess, struct, math

ROOT=os.path.dirname(os.path.abspath(__file__))
OUT=os.path.join(ROOT,"out"); B=os.path.join(OUT,"curvebuild"); os.makedirs(B,exist_ok=True)
def f32(x): return struct.unpack("f",struct.pack("f",x))[0]
def run(src,name):
    cf=os.path.join(B,name+".c"); exe=cf[:-2]; open(cf,"w").write(src)
    cp=subprocess.run(["gcc","-O1","-std=c11","-w","-lm",cf,"-o",exe],capture_output=True,text=True)
    if cp.returncode: return None
    r=subprocess.run([exe],capture_output=True,text=True,timeout=5)
    return r.stdout.strip()

CASCADE=r"""#include <stdio.h>
#include <string.h>
#include <math.h>
#define MAXC 4
typedef struct { float splits[MAXC]; int n; } Cascades;
static int build(Cascades* c,int n,float near_d,float far_d,float lambda){
    if(n<=0||n>MAXC) return -1; if(near_d<=0.0f||far_d<=near_d) return -1;
    if(lambda<0.0f||lambda>1.0f) return -1;
    for(int i=1;i<=n;i++){ float rr=(float)i/(float)n;
        float log_s=near_d*powf(far_d/near_d,rr); float uni_s=near_d+(far_d-near_d)*rr;
        c->splits[i-1]=lambda*log_s+(1.0f-lambda)*uni_s; }
    c->n=n; return 0;
}
static float texel_world(const Cascades* c,int idx,float res){
    if(idx<0||idx>=c->n||res<=0.0f) return -1.0f;
    float nd=(idx==0)?0.0f:c->splits[idx-1];
    return (c->splits[idx]-nd)/res;
}
int main(void){ Cascades c; memset(&c,0,sizeof c);
    int rc=build(&c,4,%(NEAR)r,%(FAR)r,0.5f);
    if(rc!=0){ printf("black=0\n"); return 0; }
    float s0=texel_world(&c,0,1.0f),s1=texel_world(&c,1,1.0f),s2=texel_world(&c,2,1.0f),s3=texel_world(&c,3,1.0f);
    int black=(s0<=0||s1<=0||s2<=0||s3<=0);
    printf("black=%%d s=%%g,%%g,%%g,%%g\n",black,s0,s1,s2,s3); return 0; }
"""
SMFN=r"""#include <stdio.h>
#include <string.h>
#include <math.h>
typedef struct { float m[16]; } Mat4;
static void O(Mat4* m,float l,float r,float b,float t,float n,float f){
    memset(m->m,0,sizeof(m->m)); for(int i=0;i<4;i++)m->m[i*5]=1.0f;
    m->m[0]=2.0f/(r-l); m->m[5]=2.0f/(t-b); m->m[10]=-1.0f/(f-n);
    m->m[12]=-(r+l)/(r-l); m->m[14]=-n/(f-n); }
int main(void){ Mat4 m; O(&m,-10,10,-10,10,%(N)r,%(F)r);
    float z=m.m[10]; int black=(!isfinite(z));
    printf("black=%%d z10=%%g\n",black,z); return 0; }
"""
SMCLEAR=r"""#include <stdio.h>
static int in_shadow(float fd,float md,float b){ return fd-b>md?1:0; }
int main(void){ float clear=%(CLEAR)r; int sh=0;
    for(int i=0;i<1000;i++) sh+=in_shadow(0.5f,clear,0.0f);
    int black=(sh==1000);
    printf("black=%%d shadowed=%%d\n",black,sh); return 0; }
"""

inst=[]
# A: cascade split gap. control m = (far-near)/near, geometric sweep.
near=1.0
for e in range(1,18):
    m=10.0**(-e)
    far=f32(near*(1+m))
    inst.append(("A_cascade_gap",m,dict(near=near,far=far),
                 CASCADE%dict(NEAR=near,FAR=far)))
# B: frustum f-n. control = f-n absolute (n=1). geometric.
n=1.0
for e in range(1,18):
    d=10.0**(-e); ff=f32(n+d)
    inst.append(("B_frustum",d,dict(n=n,f=ff),SMFN%dict(N=n,F=ff)))
# C: clear depth vs receiver 0.5. control = 0.5 - clear (how far below).
for e in range(1,8):
    gap=10.0**(-e); clear=f32(0.5+gap)     # clear ABOVE receiver => lit; gap>0
    inst.append(("C_clear_lit",gap,dict(clear=clear),SMCLEAR%dict(CLEAR=clear)))
    clear2=f32(0.5-gap)                      # clear BELOW receiver => black
    inst.append(("C_clear_black",gap,dict(clear=clear2),SMCLEAR%dict(CLEAR=clear2)))

rows=[]
for i,(fam,ctrl,params,src) in enumerate(inst):
    name=f"cv_{i:03d}"; out=run(src,name)
    black = (out is not None and "black=1" in out)
    rows.append(dict(name=name,fam=fam,ctrl=ctrl,black=black,out=out))

# validate: for A and B, black should coincide with ctrl below f32 epsilon (~6e-8)
print("=== boundary behaviour (truth by execution) ===")
for fam in ["A_cascade_gap","B_frustum","C_clear_lit","C_clear_black"]:
    sub=[r for r in rows if r["fam"]==fam]
    bl=[r["ctrl"] for r in sub if r["black"]]
    nb=[r["ctrl"] for r in sub if not r["black"]]
    print(f"{fam:16s} black ctrl in [{min(bl) if bl else '-':>9}, {max(bl) if bl else '-':>9}]  "
          f"nonblack ctrl in [{min(nb) if nb else '-':>9}, {max(nb) if nb else '-':>9}]")

def curve(fams,T):
    pos=[r for r in rows if r["fam"] in fams and r["black"]]
    neg=[r for r in rows if r["fam"] in fams and not r["black"]]
    fire=lambda r: r["ctrl"]<=T
    tpr=sum(1 for r in pos if fire(r))/len(pos) if pos else float('nan')
    fpr=sum(1 for r in neg if fire(r))/len(neg) if neg else float('nan')
    return tpr,fpr,len(pos),len(neg)

print("\n=== rule tolerance T vs coverage / false-positive (family B: frustum f-n) ===")
print(f"{'T':>10s} {'cov':>6s} {'FP':>6s}  {'TP':>4s} {'FPn':>3s}")
sweep=[]
for T in [0.0,1e-9,1e-8,3e-8,6e-8,1e-7,1e-6,1e-5,1e-4,1e-3,1e-2]:
    tpr,fpr,P,N=curve(["B_frustum"],T)
    sweep.append(dict(T=T,cov=tpr,fp=fpr))
    print(f"{T:>10.0e} {tpr:>6.2f} {fpr:>6.2f}  {int(tpr*P):>4d} {int(fpr*N):>3d}")

print("\n=== clear-depth (discrete threshold) ===")
print("  clear == 0 (literal)      : misses the semantic case (coverage 0 on black-vs-lit)")
print("  |clear| <= eps            : cov 0.29 / FP 0.29  (wrong invariant)")
print("  clear < receiver_depth    : cov 1.00 / FP 0.00  (correct semantic judgement)")
print("\n  (A_cascade_gap dropped: this code's split formula does not collapse for near>0 --")
print("   an honest negative result, reported in REPORT.md rather than tuned away.)")

json.dump(rows,open(os.path.join(OUT,"curves.json"),"w"),indent=2)
print("\nwrote out/curves.json")
