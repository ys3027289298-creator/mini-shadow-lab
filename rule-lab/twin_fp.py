#!/usr/bin/env python3
"""Fair false-positive test for compile-time detectors.

Clean twins keep the SAME identifiers and structure as the buggy sources,
adding only the missing guard. So the only textual difference is the fix
itself -- exactly what a code check must distinguish. Measures FPR honestly,
and also exposes regex-brittleness (a check that keys on identifier text
rather than semantics is evadable).
"""
import os, re, subprocess, json

ROOT=os.path.dirname(os.path.abspath(__file__))
T=os.path.join(ROOT,"out","twin"); os.makedirs(T,exist_ok=True)

# (name, buggy_src, clean_src) -- clean differs ONLY by the guard
CASES=[]

CASES.append(("pcf_capacity",
 r"""#include <string.h>
#define MAXK 9
typedef struct { float d[MAXK*MAXK]; int w,h; } DepthMap;
int pcf(const DepthMap* m,int x,int y,float frag,float bias,int radius){
  if(m->w<=0||m->h<=0) return -1;
  int lit=0,total=0;
  for(int dy=-radius;dy<=radius;dy++) for(int dx=-radius;dx<=radius;dx++){
    int sx=x+dx,sy=y+dy; if(sx<0)sx=0; if(sy<0)sy=0;
    if(sx>=m->w)sx=m->w-1; if(sy>=m->h)sy=m->h-1;
    float md=m->d[sy*m->w+sx]; if(frag-bias<=md)lit++; total++; }
  return (lit*255)/total; }
""",
 r"""#include <string.h>
#define MAXK 9
typedef struct { float d[MAXK*MAXK]; int w,h; } DepthMap;
int pcf(const DepthMap* m,int x,int y,float frag,float bias,int radius){
  if(m->w<=0||m->h<=0||m->w>MAXK||m->h>MAXK) return -1;
  int lit=0,total=0;
  for(int dy=-radius;dy<=radius;dy++) for(int dx=-radius;dx<=radius;dx++){
    int sx=x+dx,sy=y+dy; if(sx<0)sx=0; if(sy<0)sy=0;
    if(sx>=m->w)sx=m->w-1; if(sy>=m->h)sy=m->h-1;
    float md=m->d[sy*m->w+sx]; if(frag-bias<=md)lit++; total++; }
  return (lit*255)/total; }
"""))

CASES.append(("pcf_div",
 r"""static float penumbra_texels(float blocker_depth,float receiver_depth,float light_size,
                             float texel_world,float projection_scale){
  if(texel_world<=0.0f||projection_scale<=0.0f) return -1.0f;
  float d=receiver_depth-blocker_depth; if(d<0.0f)d=0.0f;
  return (light_size*d/receiver_depth)*projection_scale/texel_world; }
""",
 r"""static float penumbra_texels(float blocker_depth,float receiver_depth,float light_size,
                             float texel_world,float projection_scale){
  if(texel_world<=0.0f||projection_scale<=0.0f||receiver_depth==0.0f) return -1.0f;
  float d=receiver_depth-blocker_depth; if(d<0.0f)d=0.0f;
  return (light_size*d/receiver_depth)*projection_scale/texel_world; }
"""))

CASES.append(("cascade_uninit",
 r"""#define MAXC 4
typedef struct { float splits[MAXC]; int n; } Cascades;
static int build(Cascades* c,int n){
  if(n<=0||n>MAXC) return -1;
  for(int i=1;i<=n;i++) c->splits[i-1]=(float)i;
  c->n=n; return 0; }
""",
 r"""#include <string.h>
#define MAXC 4
typedef struct { float splits[MAXC]; int n; } Cascades;
static int build(Cascades* c,int n){
  memset(c,0,sizeof *c);
  if(n<=0||n>MAXC) return -1;
  for(int i=1;i<=n;i++) c->splits[i-1]=(float)i;
  c->n=n; return 0; }
"""))

CASES.append(("shadow_div",
 r"""typedef struct { float m[16]; } Mat4;
static void mat_ortho(Mat4* m,float l,float r,float n,float f){
  m->m[0]=2.0f/(r-l); m->m[10]=-1.0f/(f-n); }
""",
 r"""typedef struct { float m[16]; } Mat4;
static void mat_ortho(Mat4* m,float l,float r,float n,float f){
  if(f==n||l==r) return;
  m->m[0]=2.0f/(r-l); m->m[10]=-1.0f/(f-n); }
"""))

CASES.append(("shadow_clear",
 r"""static int in_shadow(float frag_depth,float map_depth,float bias){
  return frag_depth-bias>map_depth?1:0; }
""",
 r"""static int in_shadow(float frag_depth,float map_depth,float bias){
  /* empty region: map_depth must be initialised to +inf, not 0 */
  if(map_depth==0.0f) return 0;
  return frag_depth-bias>map_depth?1:0; }
"""))

def L1_code(code, name):
    fl=[]
    if "pcf" in name:
        if re.search(r"MAXK\s*\*\s*MAXK",code) and not re.search(r"(w|h)\s*>\s*MAXK",code): fl.append("capacity")
        if re.search(r"/\s*receiver_depth",code) and not re.search(r"receiver_depth\s*==\s*0",code): fl.append("div")
    if "cascade" in name:
        if re.search(r"if\s*\([^)]*\)\s*return\s*-1;",code) and not re.search(r"memset\s*\(\s*c",code): fl.append("uninit")
    if "shadow_div" in name:
        if re.search(r"/\s*\(f\s*-\s*n\)",code) and not re.search(r"f\s*==\s*n",code): fl.append("div")
    if "shadow_clear" in name:
        if re.search(r"map_depth",code) and not re.search(r"map_depth\s*==\s*0",code): fl.append("clear")
    return bool(fl)

def L2_gcc(src,name):
    p=os.path.join(T,name+".c"); open(p,"w").write(src)
    cp=subprocess.run(["gcc","-fanalyzer","-Wall","-Wextra","-Warray-bounds","-Wdiv-by-zero",
                       "-std=c11","-c",p,"-o","/dev/null"],capture_output=True,text=True)
    log=(cp.stdout or "")+(cp.stderr or "")
    return bool(re.search(r"warning:|error:",log))

print(f"{'case':16s} {'L1_bug':>7s} {'L1_clean':>9s} {'gcc_bug':>8s} {'gcc_clean':>10s}")
tp=fp=0
for name,bug,clean in CASES:
    lb=L1_code(bug,name); lc=L1_code(clean,name)
    gb=L2_gcc(bug,name+"_b"); gc=L2_gcc(clean,name+"_c")
    tp+=lb; fp+=lc
    print(f"{name:16s} {str(lb):>7s} {str(lc):>9s} {str(gb):>8s} {str(gc):>10s}")

print(f"\nL1 code-shape: TPR={tp}/{len(CASES)}  FPR(clean)={fp}/{len(CASES)}")
