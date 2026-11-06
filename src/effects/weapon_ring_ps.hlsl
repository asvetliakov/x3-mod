// Weapon effects, the surface ring (docs/architecture/hull-impact-classes.md 3.5): the glow primitive with a shell
// profile. The lane's surface point under the pixel (weapon_soft_depth.hlsl; the sky sentinel or an invalid depth draws
// nothing), rebuilt through the frame's jittered projection as the glow's, at distance d from the hit: E = colour x
// exp(-((d - r) / w)^2) with r the ring's radius now (shape.x) and w its width (shape.y): the expanding sphere shell cut
// by whatever surface the lane holds (a circle on a flat plate, a contour on a curved hull, cut at a hull's edge, never
// on sky); no normal. The colour carries the heat, the radiance and the (1 - t / L)^2 fade. Alpha 0 under ONE/ONE.
// Constants: renderer/weapon_effects_pass.cpp.
#include "weapon_soft_depth.hlsl"
float4 proj : register(c2);   // m00, m11, m20, m21
float4 screen : register(c3); // 2 / W, 2 / H, unused x2
float4 main(float2 vpos : VPOS, float4 light : TEXCOORD0, float4 colour : TEXCOORD1, float4 shape : TEXCOORD2) : COLOR0 {
    bool occluder;
    float z = lane_view_depth(vpos, occluder);
    float3 c = 0.0;
    [branch] if (occluder && z > 0.0 && z <= 3.402823466e38) {
        float2 ndc = float2(vpos.x * screen.x - 1.0, 1.0 - vpos.y * screen.y);
        float3 p = float3((ndc.x - proj.z) / proj.x * z, (ndc.y - proj.w) / proj.y * z, z);
        float3 d = p - light.xyz;
        float x = (sqrt(dot(d, d)) - shape.x) / max(shape.y, 1e-6);
        c = colour.rgb * exp2(-1.442695 * x * x);
    }
    return float4(c, 0.0);
}
