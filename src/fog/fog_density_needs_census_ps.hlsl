// Diagnostic only (--gpu-sync-timing; docs/architecture/fog-gpu-cost.md, step C): keeps exactly the full pixels the repair
// would march at this program's spacing (needs_repair: a valid depth class with no class-compatible march sample), for an
// occlusion query around one full-screen quad drawn with colour writes off. Unlike the repair it also keeps a pixel whose
// march comes out empty, so its count is the marched set, not the written one.
#include "fog_density_field_inc.h"
float4 main(float2 uv:TEXCOORD0):COLOR0 {
    float2 pixel = floor(uv*sizes.xy);
    float4 d = tex2Dlod(depth_texture,float4((pixel+0.5)/sizes.xy,0,0));
    clip(needs_repair(d,pixel)-0.5);
    return 0.0;
}
