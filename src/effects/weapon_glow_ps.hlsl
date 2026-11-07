// Weapon effects, the glow primitive's pixel program (docs/architecture/weapon-look.md 2.8): the lane's surface point
// under the pixel (weapon_soft_depth.hlsl; the sky sentinel or an invalid depth draws nothing), rebuilt through the
// frame's jittered projection (D3D9 pixel centres at integer raster coordinates: ndc = 2 r / size - 1, the decal's
// reconstruction), lit by E = colour x saturate(1 - d^2 / R'^2)^2 with d its distance from the light: the falloff of
// engine-light.md, no N.L and no normal, one lane fetch. Whatever surface the lane holds is lit, an occluder too.
// An ember of a burst class (lobe.z >= 0) has the burst's lobed silhouette: R' = R rho(phi), rho = (1 + amp (2 n_L - 1)) /
// (1 + amp), n_L the burst's periodic value noise of `lobes` cells (weapon_fireball_ps.hlsl's, the same seed) at the
// angle of rot(theta) (p - light).xy in the view plane; else R' = R. Alpha 0 under ONE/ONE. Constants:
// renderer/weapon_effects_pass.cpp.
#include "weapon_soft_depth.hlsl"
float4 proj : register(c2);   // m00, m11, m20, m21
float4 screen : register(c3); // 2 / W, 2 / H, lobes (cells about the disc), the ember's lobe amplitude
float hash2(float2 q) { // weapon_fireball_ps.hlsl's
    float3 p = frac(float3(q.x, q.y, q.x) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}
float4 main(float2 vpos : VPOS, float4 light : TEXCOORD0, float4 colour : TEXCOORD1, float4 lobe : TEXCOORD2) : COLOR0 {
    bool occluder;
    float z = lane_view_depth(vpos, occluder);
    float3 c = 0.0;
    [branch] if (occluder && z > 0.0 && z <= 3.402823466e38) {
        float2 ndc = float2(vpos.x * screen.x - 1.0, 1.0 - vpos.y * screen.y);
        float3 p = float3((ndc.x - proj.z) / proj.x * z, (ndc.y - proj.w) / proj.y * z, z);
        float3 d = p - light.xyz;
        float R = light.w;
        [branch] if (lobe.z >= 0.0) {
            float2 v = float2(lobe.x * d.x - lobe.y * d.y, lobe.y * d.x + lobe.x * d.y);
            float2 va = dot(v, v) > 1e-12 ? v : float2(1.0, 0.0); // atan2(0, 0) is undefined; any angle at the centre
            float t = screen.z * (atan2(va.y, va.x) * 0.15915494 + 0.5);
            float i0 = min(floor(t), screen.z - 1.0), fl = saturate(t - i0);
            float i1 = i0 + 1.0 > screen.z - 0.5 ? 0.0 : i0 + 1.0;
            float nl = lerp(hash2(float2(i0, lobe.z)), hash2(float2(i1, lobe.z)), fl * fl * (3.0 - 2.0 * fl));
            R *= (1.0 + screen.w * (2.0 * nl - 1.0)) / (1.0 + screen.w);
        }
        float q = saturate(1.0 - dot(d, d) / max(R * R, 1e-12));
        c = colour.rgb * (q * q);
    }
    return float4(c, 0.0);
}
