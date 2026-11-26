#include <string.h>
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
