// Weapon effects, the death body's halo (docs/architecture/explosion-look.md "Built numbers (step 3)", decision 3 of
// 2026-11-07: a body in open space read as a cut-out blob on black): the glow primitive's light in air about the body,
// drawn with the glow's vertex program and declaration (weapon_glow_vs.hlsl: the sphere's screen rectangle, the light's
// view-space centre and radius R, its radiance colour). The pixel's ray through the frame's jittered projection passes
// the centre at impact parameter b; the halo is colour x (1 - b^2 / R^2)^2, the glow's falloff on the view plane (its 10 %
// radius 0.83 R). Occlusion class 1: the lane hides it where a surface lies in front of the ray's closest approach to
// the centre (a soft cut over light.w x halo_soft, c4.x), the sky never hides, an invalid lane texel hides. Alpha 0
// under ONE/ONE. Constants: renderer/weapon_effects_pass.cpp (the glow's c2, c3; c4.x the soft share).
#include "weapon_soft_depth.hlsl"
float4 proj : register(c2);   // m00, m11, m20, m21
float4 screen : register(c3); // 2 / W, 2 / H, unused x2
float4 halo : register(c4);   // the soft cut's share of R, unused x3
float4 main(float2 vpos : VPOS, float4 light : TEXCOORD0, float4 colour : TEXCOORD1) : COLOR0 {
    float2 ndc = float2(vpos.x * screen.x - 1.0, 1.0 - vpos.y * screen.y);
    float3 dir = float3((ndc.x - proj.z) / proj.x, (ndc.y - proj.w) / proj.y, 1.0);
    float R = max(light.w, 1e-6);
    float L2 = dot(dir, dir);
    float bz = dot(dir, light.xyz);
    float zc = bz / L2;                                  // the depth of the ray's closest approach to the centre
    float b2 = dot(light.xyz, light.xyz) - bz * zc;
    float q = saturate(1.0 - b2 / (R * R));
    float3 c = 0.0;
    [branch] if (q > 0.0) {
        bool occluder;
        float lane = lane_view_depth(vpos, occluder);
        bool valid = !occluder || (lane > 0.0 && lane <= 3.402823466e38);
        float vis = occluder ? saturate((lane - zc) / max(halo.x * R, 1e-4)) : 1.0;
        c = valid ? colour.rgb * (q * q * vis) : 0.0;
    }
    return float4(max(c, 0.0), 0.0);
}
