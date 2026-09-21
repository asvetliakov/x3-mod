#include "fog_density_field_inc.h"
sampler2D scene_texture : register(s2);
sampler2D st_texture : register(s3);
// The qualified integer-pixel 2x2 footprint. Pixels with no class-compatible
// half sample keep the scene here; fog_density_repair_ps marches them.
float4 main(float2 uv:TEXCOORD0):COLOR0 {
    float2 pixel = floor(uv*sizes.xy);
    uv = (pixel+0.5)/sizes.xy;
    float4 scene = tex2Dlod(scene_texture,float4(uv,0,0));
    float4 d = tex2Dlod(depth_texture,float4(uv,0,0));
    if (geometry(d) && !valid_geometry_depth(d)) return scene;
    // Half samples are at full even pixels, rather than 2x2 cell centres.
    float2 hp = pixel*0.5;
    float2 base = floor(hp), f = frac(hp);
    float4 sum = 0.0; float weight = 0.0;
    [unroll] for (int y=0; y<2; ++y) {
        [unroll] for (int x=0; x<2; ++x) {
            float2 q = clamp(base+float2(x,y),0.0,sizes.zw-1.0);
            float w = footprint_weight(d,q,(x ? f.x:1.0-f.x)*(y ? f.y:1.0-f.y));
            float4 tap = tex2Dlod(st_texture,float4((q+0.5)/sizes.zw,0,0));
            // Interpolate the complement of T so exactly empty inputs stay exactly zero.
            sum += w*float4(tap.rgb,1.0-tap.a);
            weight += w;
        }
    }
    if (weight <= 0.0 || all(sum == 0.0)) return scene;
    return fog_apply(scene,float4(sum.rgb/weight,1.0-sum.a/weight));
}
