#include <stdio.h>
#include <string.h>
#include <math.h>
#define MAXK 9
typedef struct { float d[MAXK*MAXK]; int w,h; } DepthMap;
static float penumbra_texels(float bd,float rd,float ls,float tw,float ps){
    if(tw<=0.0f||ps<=0.0f) return -1.0f;
    float d=rd-bd; if(d<0.0f)d=0.0f;
    return (ls*d/rd)*ps/tw;
}
int main(void){ float pn=penumbra_texels(0.0,0.0001,1.0f,0.1f,1.0f);
    printf("penum=%.6g pfinite=%d\n",pn,isfinite(pn)?1:0); return 0; }
