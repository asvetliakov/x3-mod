#include "fog_field_inc.h"
sampler2D scene_texture : register(s2);
sampler2D st_texture : register(s3);
float4 main(float2 uv:TEXCOORD0):COLOR0 {
    // Interpolated UV can lie a few ulps either side of an even source centre.
    // Recover the integer pixel before forming the exact 0/.5 half footprint;
    // otherwise a tiny compatible weight can incorrectly bypass full24 repair.
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
            float4 hd = tex2Dlod(depth_texture,float4((q*2.0+0.5)/sizes.xy,0,0));
            float w = (x ? f.x:1.0-f.x)*(y ? f.y:1.0-f.y);
            if (geometry(d) != geometry(hd) || (geometry(hd) && !valid_geometry_depth(hd))) w = 0.0;
            // Preserve the existing relative-depth law within the compatible class.
            else if (geometry(d)) w *= exp(-abs(hd.b-d.b)/max(0.05*min(d.b,hd.b),1e-6)) + 1e-4;
            float4 tap = tex2Dlod(st_texture,float4((q+0.5)/sizes.zw,0,0));
            // Algebraically interpolate T with the same weights, storing its
            // complement so exactly empty inputs stay exactly zero before division.
            sum += w*float4(tap.rgb,1.0-tap.a);
            weight += w;
        }
    }
    float4 st;
    [branch] if (weight > 0.0) {
        if (all(sum == 0.0)) return scene;
        st = float4(sum.rgb/weight,1.0-sum.a/weight);
    }
    else st=march_pixel(uv); // Required full-pixel24 repair, included in timing.
    if (all(st.rgb == 0.0) && st.a == 1.0) return scene;
    return float4(pow(max(pow(max(scene.rgb,0.0),phase_gamma.w)*st.a+st.rgb,0.0),radiance_encode.w),scene.a);
}
