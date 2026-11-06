// Weapon effects, the filament glow (docs/architecture/hull-impact-classes.md 3.6, the ion class's arcs): the glow
// primitive with a noise mask. The lane's surface point under the pixel (weapon_soft_depth.hlsl; the sky sentinel or an
// invalid depth draws nothing), rebuilt as the glow's, taken into the owner's frame (its axes in view: TEXCOORD3..5,
// relative to the hit) and scaled by shape.x (filament_scale / r_hit per view unit), offset by the seed shape.y (the
// stage re-hashes it every rehash_ms: the arcs crawl, fixed to the hull between slots); two octaves of signed value
// noise n, lit where |n| crosses zero: mask = saturate(1 - |n| / shape.z); E = colour x mask x saturate(1 - d^2 /
// shape.w^2). Alpha 0 under ONE/ONE. Constants: renderer/weapon_effects_pass.cpp.
#include "weapon_soft_depth.hlsl"
float4 proj : register(c2);   // m00, m11, m20, m21
float4 screen : register(c3); // 2 / W, 2 / H, unused x2
float hash3(float3 p) {
    p = frac(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return frac((p.x + p.y) * p.z);
}
float value_noise(float3 x) {
    float3 i = floor(x), f = x - i;
    float3 u = f * f * (3.0 - 2.0 * f);
    float a = hash3(i), b = hash3(i + float3(1, 0, 0)), c = hash3(i + float3(0, 1, 0)), d = hash3(i + float3(1, 1, 0));
    float e = hash3(i + float3(0, 0, 1)), g = hash3(i + float3(1, 0, 1)), h = hash3(i + float3(0, 1, 1));
    float k = hash3(i + float3(1, 1, 1));
    return lerp(lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y), lerp(lerp(e, g, u.x), lerp(h, k, u.x), u.y), u.z);
}
float4 main(float2 vpos : VPOS, float4 light : TEXCOORD0, float4 colour : TEXCOORD1, float4 shape : TEXCOORD2,
            float4 ax : TEXCOORD3, float4 ay : TEXCOORD4, float4 az : TEXCOORD5) : COLOR0 {
    bool occluder;
    float z = lane_view_depth(vpos, occluder);
    float3 c = 0.0;
    [branch] if (occluder && z > 0.0 && z <= 3.402823466e38) {
        float2 ndc = float2(vpos.x * screen.x - 1.0, 1.0 - vpos.y * screen.y);
        float3 p = float3((ndc.x - proj.z) / proj.x * z, (ndc.y - proj.w) / proj.y * z, z);
        float3 d = p - light.xyz;
        float fall = saturate(1.0 - dot(d, d) / max(shape.w * shape.w, 1e-12));
        [branch] if (fall > 0.0) {
            float3 q = float3(dot(d, ax.xyz), dot(d, ay.xyz), dot(d, az.xyz)) * shape.x + shape.y * float3(1.7, 3.1, 0.7);
            float n = 0.65 * (2.0 * value_noise(q) - 1.0) + 0.35 * (2.0 * value_noise(q * 2.03 + 5.3) - 1.0);
            float mask = saturate(1.0 - abs(n) / max(shape.z, 1e-4));
            c = colour.rgb * (mask * fall);
        }
    }
    return float4(c, 0.0);
}
