// Verification only: the stored-density march with eight texel-exact POINT
// fetches per level sample, the reference for measuring hardware FP16 bilinear.
#define FOG_DENSITY_EXACT_TEXELS
#define FOG_DENSITY_NO_SHAFTS // the fixture disables shafts; keeps the program inside 32 temporaries
#include "../../src/fog/fog_density_field_inc.h"
float4 main(float2 uv:TEXCOORD0):COLOR0 {
    float2 pixel = floor(uv*sizes.zw);
    return march_pixel((pixel*2.0+0.5)/sizes.xy);
}
