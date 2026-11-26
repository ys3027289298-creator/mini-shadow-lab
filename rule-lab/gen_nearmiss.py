#!/usr/bin/env python3
"""Near-miss instances: the validity guard PASSES but float32 output still
degenerates. These are what a narrow 'exact equality' rule misses and a
wider 'within epsilon' rule catches. Emitted as real .c files in out/build.
"""
import json, os, struct
ROOT=os.path.dirname(os.path.abspath(__file__))
OUT=os.path.join(ROOT,"out"); BUILD=os.path.join(OUT,"build"); os.makedirs(BUILD,exist_ok=True)
def f32(x): return struct.unpack("f",struct.pack("f",x))[0]

CAS=r"""#include <stdio.h>
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
    build(&c,%(N)d,%(NEAR)r,%(FAR)r,%(LAM)r);
    float a=texel_world(&c,0,1024),b=texel_world(&c,%(B)d,1024),d=b-a;
    printf("a=%%.9g b=%%.9g diff=%%.9g span0=%%.9g finite=%%d\n",a,b,d,texel_world(&c,0,1.0f),isfinite(d)?1:0);
    return 0; }
"""
PCFr=r"""#include <stdio.h>
#include <string.h>
#include <math.h>
#define MAXK 9
typedef struct { float d[MAXK*MAXK]; int w,h; } DepthMap;
static float penumbra_texels(float bd,float rd,float ls,float tw,float ps){
    if(tw<=0.0f||ps<=0.0f) return -1.0f;
    float d=rd-bd; if(d<0.0f)d=0.0f;
    return (ls*d/rd)*ps/tw;
}
int main(void){ float pn=penumbra_texels(%(BD)r,%(RD)r,1.0f,0.1f,1.0f);
    printf("penum=%%.6g pfinite=%%d\n",pn,isfinite(pn)?1:0); return 0; }
"""
SMr=r"""#include <stdio.h>
#include <string.h>
#include <math.h>
typedef struct { float x,y,z,w; } V4;
typedef struct { float m[16]; } Mat4;
static void I(Mat4* m){ memset(m->m,0,sizeof(m->m)); for(int i=0;i<4;i++)m->m[i*5]=1.0f; }
static void O(Mat4* m,float l,float r,float b,float t,float n,float f){
    I(m); m->m[0]=2.0f/(r-l); m->m[5]=2.0f/(t-b); m->m[10]=-1.0f/(f-n);
    m->m[12]=-(r+l)/(r-l); m->m[13]=-(t+b)/(t-b); m->m[14]=-n/(f-n); }
static V4 A(const Mat4* m,V4 v){ V4 o; o.x=o.y=o.z=o.w=0;
    o.z=m->m[2]*v.x+m->m[6]*v.y+m->m[10]*v.z+m->m[14]*v.w;
    o.w=m->m[3]*v.x+m->m[7]*v.y+m->m[11]*v.z+m->m[15]*v.w; return o; }
static int in_shadow(float fd,float md,float b){ return fd-b>md?1:0; }
int main(void){ Mat4 m; O(&m,%(L)r,%(R)r,-10,10,%(N)r,%(F)r);
    V4 p={0,0,-%(N)r,1}; V4 lp=A(&m,p); float cv=%(CLEAR)r; int sh=0;
    for(int i=0;i<1000;i++) sh+=in_shadow(0.5f,cv,0.0f);
    printf("ndc_z=%%.6g zfinite=%%d shadowed=%%d\n",lp.z,isfinite(lp.z)?1:0,sh); return 0; }
"""

rows=[]
def emit(name,src):
    open(os.path.join(BUILD,name+".c"),"w").write(src)

# cascade near-miss: far = near*(1+rel), rel tiny -> spans collapse while guard passes
i=0
for rel in [1e-9,1e-8,1e-7,3e-7,1e-6,1e-5,1e-4]:
    for n,lam in [(3,0.5),(4,0.0),(4,1.0),(3,1.0)]:
        near=1.0; far=f32(near*(1+rel))
        name=f"nm_cascade_{i:03d}"; i+=1
        emit(name,CAS%dict(N=n,NEAR=near,FAR=far,LAM=lam,B=min(n,3)))
        rows.append(dict(name=name,family="cascade",
                         params=dict(n=n,near=near,far=far,lam=lam,rel=rel)))
# pcf near-miss: receiver_depth tiny but nonzero -> penumbra enormous/inf
i=0
for rd in [1e-30,1e-20,1e-12,1e-8,1e-6,1e-4]:
    name=f"nm_pcf_{i:03d}"; i+=1
    emit(name,PCFr%dict(BD=0.0,RD=rd))
    rows.append(dict(name=name,family="pcf",params=dict(bd=0.0,rd=rd,w=4,h=4)))
# shadowmap near-miss: f-n tiny nonzero -> 1/(f-n) huge -> depth precision collapse
i=0
for d in [1e-30,1e-20,1e-12,1e-8,1e-6,1e-4]:
    name=f"nm_sm_{i:03d}"; i+=1
    nn=1.0; ff=f32(nn+d)
    emit(name,SMr%dict(L=-10.0,R=10.0,N=nn,F=ff,CLEAR=1.0))
    rows.append(dict(name=name,family="shadowmap",params=dict(l=-10.0,r=10.0,n=nn,f=ff,clear=1.0,d=d)))
# shadowmap near-miss: clear value small nonzero -> still fully occluded
i=0
for cv in [1e-8,1e-6,1e-4,1e-3,1e-2]:
    name=f"nm_smc_{i:03d}"; i+=1
    emit(name,SMr%dict(L=-10.0,R=10.0,N=1.0,F=100.0,CLEAR=cv))
    rows.append(dict(name=name,family="shadowmap",params=dict(l=-10.0,r=10.0,n=1.0,f=100.0,clear=cv)))

json.dump(rows,open(os.path.join(OUT,"nearmiss.json"),"w"),indent=2)
print(f"emitted {len(rows)} near-miss instances")
