#include <stdio.h>
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
    int rc=build(&c,4,1.0,1.0099999904632568,0.5f);
    if(rc!=0){ printf("black=0\n"); return 0; }
    float s0=texel_world(&c,0,1.0f),s1=texel_world(&c,1,1.0f),s2=texel_world(&c,2,1.0f),s3=texel_world(&c,3,1.0f);
    int black=(s0<=0||s1<=0||s2<=0||s3<=0);
    printf("black=%d s=%g,%g,%g,%g\n",black,s0,s1,s2,s3); return 0; }
