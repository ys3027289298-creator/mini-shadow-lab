#include <stdio.h>
#include <string.h>
#include <math.h>
#define MAXC 4
typedef struct { float splits[MAXC]; int n; } Cascades;
static int build(Cascades* c,int n,float near_d,float far_d,float lambda){
    memset(c,0,sizeof *c);
    if(n<=0||n>MAXC) return -1; if(near_d<=0.0f||far_d<=near_d) return -1;
    if(lambda<0.0f||lambda>1.0f) return -1;
    for(int i=1;i<=n;i++){ float rr=(float)i/(float)n;
        float log_s=near_d*powf(far_d/near_d,rr); float uni_s=near_d+(far_d-near_d)*rr;
        c->splits[i-1]=lambda*log_s+(1.0f-lambda)*uni_s; }
    c->n=n; return 0;
}
static float texel_world(const Cascades* c,int idx,float res){
    if(idx<0||idx>=c->n||res<=0.0f) return -1.0f;
    float near_d=(idx==0)?0.0f:c->splits[idx-1];
    return (c->splits[idx]-near_d)/res;
}
int main(void){ Cascades c; int rc=build(&c,0,1e-06,1000000000.0,0.5);
    if(rc!=0){ printf("a=nan b=nan diff=nan finite=0\n"); return 0; }
    float a=texel_world(&c,0,1024),b=texel_world(&c,0,1024),d=b-a;
    printf("a=%.9g b=%.9g diff=%.9g finite=%d\n",a,b,d,isfinite(d)?1:0); return 0; }
