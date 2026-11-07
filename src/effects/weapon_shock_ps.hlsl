// Weapon effects, the death explosion's shock shell (docs/architecture/explosion-look.md 2.3; drawn with the fireball
// impostor's vertex program and declaration, weapon_fireball_vs.hlsl: one quad per shell, the sphere's screen rectangle).
// The pixel's ray from the eye through the frame's jittered projection (the fireball's reconstruction) at impact
// parameter b from the centre crosses a thin sphere shell of outer radius r (sphere.w) and width w (look.x, view units)
// on its front and back segments, u in [-s_o, -s_i] and [s_i, s_o] about the ray's closest approach (s_o = sqrt(r^2 -
// b^2), s_i = sqrt(max((r - w)^2 - b^2, 0)); one segment [-s_o, s_o] within w of the limb). The density is the length
// the ray spends in the shell, clipped to the eye's near plane and to the lane (occlusion class 2: a hull in front
// hides, the back half behind a hull is not there, the sky never hides, an invalid lane texel hides), over the limb's
// chord 2 sqrt(2 r w - w^2): 1 at the limb, x a ramp from look.z r (boom::shock_limb) inside the limb to it, so the
// interior is 0 (2026-11-07: the filled shell read as a grey disc). Early out outside the limb. Output tint.rgb x look.y
// (heat x E x alpha) x density, additive, alpha 0. Constants: renderer/weapon_effects_pass.cpp (the fireball's c2, c3).
#include "weapon_soft_depth.hlsl"
float4 proj : register(c2);   // m00, m11, m20, m21
float4 screen : register(c3); // 2 / W, 2 / H, near view z, unused
float4 main(float2 vpos : VPOS, float4 sphere : TEXCOORD0, float4 look : TEXCOORD1, float4 tint : TEXCOORD2) : COLOR0 {
    float2 ndc = float2(vpos.x * screen.x - 1.0, 1.0 - vpos.y * screen.y);
    float3 dir = float3((ndc.x - proj.z) / proj.x, (ndc.y - proj.w) / proj.y, 1.0);
    float r = max(sphere.w, 1e-6), w = clamp(look.x, 1e-6, r);
    float L2 = dot(dir, dir), l = sqrt(L2);
    float bz = dot(dir, sphere.xyz);
    float zm = bz / L2;
    float h2 = dot(sphere.xyz, sphere.xyz) - bz * zm; // b^2
    float so2 = r * r - h2;
    float3 c = 0.0;
    [branch] if (so2 > 0.0) {
        float so = sqrt(so2);
        float ri = r - w;
        float si = sqrt(max(ri * ri - h2, 0.0));
        float u0 = (screen.z - zm) * l, u1 = 3.402823466e38;
        bool occluder;
        float lane = lane_view_depth(vpos, occluder);
        bool valid = !occluder || (lane > 0.0 && lane <= 3.402823466e38);
        if (occluder && valid) u1 = (lane - zm) * l;
        // the front segment [-so, -si] and the back [si, so], each clipped to [u0, u1]
        float front = max(0.0, min(-si, u1) - max(-so, u0));
        float back = max(0.0, min(so, u1) - max(si, u0));
        float limb = 2.0 * sqrt(max(2.0 * r * w - w * w, 1e-12));
        // the limb only (2026-11-07: the filled shell read as a grey disc): x a ramp from look.z r inside the limb, so
        // the interior is 0
        float ramp = saturate((sqrt(h2) - (1.0 - look.z) * r) / max(look.z * r, 1e-6));
        float density = valid ? saturate((front + back) / limb) * ramp : 0.0;
        c = tint.rgb * (look.y * density);
    }
    return float4(max(c, 0.0), 0.0);
}
