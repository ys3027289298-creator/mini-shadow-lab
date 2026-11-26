#include <stdio.h>
#include <string.h>
#include <math.h>
#define MAXC 4
typedef struct { float splits[MAXC]; int n; } Cascades;
static int build(Cascades* c,int n,float near_d,float far_d,float lambda){
  memset(c,0,sizeof *c);
  if(n<=0||n>MAXC) return -1; if(near_d<=0.0f||far_d<=near_d) return -1;
  if(lambda<0.0f||lambda>1.0f) return -1;
  for(int i=1;i<=n;i++){ float r=(float)i/(float)n;
    float log_s=near_d*powf(far_d/near_d,r); float uni_s=near_d+(far_d-near_d)*r;
    c->splits[i-1]=lambda*log_s+(1.0f-lambda)*uni_s; }
  c->n=n; return 0;
}
int main(void){ Cascades c; build(&c,0,0.5f,100.0f,0.5f);
  printf("n_after_fail=%d\n", c.n); return 0; }
