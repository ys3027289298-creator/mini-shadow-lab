/* B6: depth compare assumes cleared=0 means "far"; clear=0 makes empty area fully occluded. */
#include <stdio.h>
#include <string.h>
#include <math.h>
static int in_shadow(float frag_depth, float map_depth, float bias) {
    return frag_depth - bias > map_depth ? 1 : 0;
}
int main(void) {
    float cleared = 0.0f;          /* streaming clear value used by Core */
    int shadows = 0;
    for (int i = 0; i < 1000; i++) {
        float frag = 0.5f;         /* receiver depth */
        shadows += in_shadow(frag, cleared, 0.0f);
    }
    printf("shadowed=%d/1000\n", shadows);
    return 0;
}
