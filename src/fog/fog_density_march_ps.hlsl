#include "fog_density_field_inc.h"
float4 main(float2 uv:TEXCOORD0):COLOR0 {
    float2 pixel = floor(uv*sizes.zw);
    return march_pixel((pixel*FOG_MARCH_STEP+0.5)/sizes.xy);
}
