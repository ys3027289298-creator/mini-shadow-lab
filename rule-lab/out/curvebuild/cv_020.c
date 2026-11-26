#include <stdio.h>
#include <string.h>
#include <math.h>
typedef struct { float m[16]; } Mat4;
static void O(Mat4* m,float l,float r,float b,float t,float n,float f){
    memset(m->m,0,sizeof(m->m)); for(int i=0;i<4;i++)m->m[i*5]=1.0f;
    m->m[0]=2.0f/(r-l); m->m[5]=2.0f/(t-b); m->m[10]=-1.0f/(f-n);
    m->m[12]=-(r+l)/(r-l); m->m[14]=-n/(f-n); }
int main(void){ Mat4 m; O(&m,-10,10,-10,10,1.0,1.000100016593933);
    float z=m.m[10]; int black=(!isfinite(z));
    printf("black=%d z10=%g\n",black,z); return 0; }
