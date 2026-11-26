#include <stdio.h>
#include <math.h>
static float depth_bias(float ndotl, float base, float slope) {
    if (ndotl < 0.0f) ndotl = 0.0f;
    if (ndotl > 1.0f) ndotl = 1.0f;
    float tan_acos = sqrtf(1.0f - ndotl*ndotl) / (ndotl > 1e-4f ? ndotl : 1e-4f);
    return base + slope * tan_acos;
}
int main(void){ printf("bias_grazing=%.6g\n", depth_bias(1e-5f, 0.001f, 0.01f)); return 0; }
