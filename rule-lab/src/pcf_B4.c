/* B4: penumbra_texels divides by receiver_depth with no zero guard. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#define MAXK 9
typedef struct { float d[MAXK*MAXK]; int w, h; } DepthMap;
static float penumbra_texels(float blocker_depth, float receiver_depth,
                             float light_size, float texel_world, float projection_scale) {
    if (texel_world <= 0.0f || projection_scale <= 0.0f) return -1.0f;
    float d = receiver_depth - blocker_depth;
    if (d < 0.0f) d = 0.0f;
    return (light_size * d / receiver_depth) * projection_scale / texel_world;
}
int main(void) {
    float p = penumbra_texels(0.0f, 0.0f, 1.0f, 0.1f, 1.0f);  /* receiver at light plane */
    printf("penum=%.6f finite=%d\n", p, isfinite(p) ? 1 : 0);
    return 0;
}
