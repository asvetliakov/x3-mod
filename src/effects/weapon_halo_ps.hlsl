// Weapon effects, the death body's halo (docs/architecture/explosion-look.md section 14 item 1, 2026-11-08: the own
// sun's corona, a glow brightest at the body's rim falling off outward): drawn with the glow primitive's vertex program
// and declaration (weapon_glow_vs.hlsl: the quad sphere's screen rectangle, its view-space centre and radius light.w =
// halo_r_x R(t), the radiance colour; the lobe slot's TEXCOORD2.z carries the rim radius R = R(t)). The pixel's ray
// through the frame's jittered projection passes the centre at impact parameter b; with r = b / R, d = max(r - 1, 0):
// colour x (e^(-(d / sigma)^2) + tail w(d) / (1 + d^2)) / (1 + tail) x smoothstep(halo_in, 1, r), w = 1 -
// smoothstep(win0, win1, d) (0 at the quad's radius): 0 inside halo_in R (no fill), the peak at the rim. Occlusion
// class 1: the lane hides it where a surface lies in front of the ray's closest approach to the centre (a soft cut over
// light.w x halo_soft, c4.x), the sky never hides, an invalid lane texel hides. Alpha 0 under ONE/ONE. Constants:
// renderer/weapon_effects_pass.cpp (the glow's c2, c3; c4 halo_soft, 1 / sigma, tail, halo_in; c5 win0, win1,
// 1 / (1 + tail)).
#include "weapon_soft_depth.hlsl"
float4 proj : register(c2);   // m00, m11, m20, m21
float4 screen : register(c3); // 2 / W, 2 / H, unused x2
float4 halo : register(c4);   // the soft cut's share of light.w, 1 / sigma, the tail's weight, halo_in
float4 shape : register(c5);  // the tail window's start and end in d, 1 / (1 + tail), unused
float4 main(float2 vpos : VPOS, float4 light : TEXCOORD0, float4 colour : TEXCOORD1, float4 rim : TEXCOORD2) : COLOR0 {
    float2 ndc = float2(vpos.x * screen.x - 1.0, 1.0 - vpos.y * screen.y);
    float3 dir = float3((ndc.x - proj.z) / proj.x, (ndc.y - proj.w) / proj.y, 1.0);
    float Rq = max(light.w, 1e-6);
    float L2 = dot(dir, dir);
    float bz = dot(dir, light.xyz);
    float zc = bz / L2;                                  // the depth of the ray's closest approach to the centre
    float b2 = max(dot(light.xyz, light.xyz) - bz * zc, 0.0);
    float r = sqrt(b2) / max(rim.z, 1e-6);
    float3 c = 0.0;
    [branch] if (b2 < Rq * Rq && r > halo.w) {
        float d = max(r - 1.0, 0.0);
        float x = d * halo.y;
        float t = saturate((d - shape.x) / (shape.y - shape.x));
        float p = (exp2(-1.442695 * x * x) + halo.z * (1.0 - t * t * (3.0 - 2.0 * t)) / (1.0 + d * d)) * shape.z;
        float s = saturate((r - halo.w) / (1.0 - halo.w));
        bool occluder;
        float lane = lane_view_depth(vpos, occluder);
        bool valid = !occluder || (lane > 0.0 && lane <= 3.402823466e38);
        float vis = occluder ? saturate((lane - zc) / max(halo.x * Rq, 1e-4)) : 1.0;
        c = valid ? colour.rgb * (p * s * s * (3.0 - 2.0 * s) * vis) : 0.0;
    }
    return float4(max(c, 0.0), 0.0);
}
