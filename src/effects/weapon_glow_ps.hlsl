// Weapon effects, the glow primitive's pixel program (docs/architecture/weapon-look.md 2.8): the lane's surface point
// under the pixel (weapon_soft_depth.hlsl; the sky sentinel or an invalid depth draws nothing), rebuilt through the
// frame's jittered projection (D3D9 pixel centres at integer raster coordinates: ndc = 2 r / size - 1, the decal's
// reconstruction), lit by E = colour x saturate(1 - d^2 / R^2)^2 with d its distance from the light: the falloff of
// engine-light.md, no N.L and no normal, one lane fetch. Whatever surface the lane holds is lit, an occluder too.
// Alpha 0 under ONE/ONE. Constants: renderer/weapon_effects_pass.cpp.
#include "weapon_soft_depth.hlsl"
float4 proj : register(c2);   // m00, m11, m20, m21
float4 screen : register(c3); // 2 / W, 2 / H, unused x2
float4 main(float2 vpos : VPOS, float4 light : TEXCOORD0, float4 colour : TEXCOORD1) : COLOR0 {
    bool occluder;
    float z = lane_view_depth(vpos, occluder);
    float3 c = 0.0;
    [branch] if (occluder && z > 0.0 && z <= 3.402823466e38) {
        float2 ndc = float2(vpos.x * screen.x - 1.0, 1.0 - vpos.y * screen.y);
        float3 p = float3((ndc.x - proj.z) / proj.x * z, (ndc.y - proj.w) / proj.y * z, z);
        float3 d = p - light.xyz;
        float q = saturate(1.0 - dot(d, d) / max(light.w * light.w, 1e-12));
        c = colour.rgb * (q * q);
    }
    return float4(c, 0.0);
}
