#include <stdio.h>
#include <math.h>
#include <assert.h>
static int in_shadow(float frag_depth,float map_depth,float bias){
  assert(isfinite(frag_depth)&&isfinite(map_depth)&&isfinite(bias));
  return frag_depth-bias>map_depth?1:0;
}
int main(void){ printf("v=%d\n", in_shadow(0.5f,0.4f,0.0f)); return 0; }
