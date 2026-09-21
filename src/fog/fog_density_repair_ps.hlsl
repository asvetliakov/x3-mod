#include "fog_density_field_inc.h"
sampler2D scene_texture : register(s2); // pristine scene copy; the target holds the composite
// Full-resolution edge repair: only pixels whose 2x2 half footprint has no
// class-compatible sample (composite weight 0) are marched and written.
float4 main(float2 uv:TEXCOORD0):COLOR0 {
    float2 pixel = floor(uv*sizes.xy);
    uv = (pixel+0.5)/sizes.xy;
    float4 d = tex2Dlod(depth_texture,float4(uv,0,0));
    float keep = needs_repair(d,pixel) > 0.0 ? 1.0:-1.0;
    float4 result = 0.0;
    [branch] if (keep > 0.0) {
        float4 st = march_depth(uv,d);
        if (all(st.rgb == 0.0) && st.a == 1.0) keep = -1.0;
        result = fog_apply(tex2Dlod(scene_texture,float4(uv,0,0)),st);
    }
    clip(keep);
    return result;
}
