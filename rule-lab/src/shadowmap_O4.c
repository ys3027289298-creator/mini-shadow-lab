#include <stdio.h>
#include <math.h>
static int in_shadow(float frag_depth, float map_depth, float bias) {
    return frag_depth - bias > map_depth ? 1 : 0;
}
int main(void){ printf("shadow_of_nan=%d\n", in_shadow(NAN, 0.5f, 0.0f)); return 0; }
