// Weapon effects, the ion class's lightning (docs/architecture/hull-impact-classes.md 4.6): one segment of a channel or
// branch per quad (renderer/weapon_effects_pass.h WeaponLightningVertex through weapon_lightning_vs.hlsl; the stage's
// stage::ion_segment_quad). The lane's surface point under the pixel (weapon_soft_depth.hlsl; the sky sentinel or an
// invalid depth draws nothing): the pixel's distance d from the segment a..b in pixels (ends: a, b in the same pixel
// space as VPOS) and the lane's view depth within the window tol (shape.w) of the segment's depth at the nearest point
// (full within tol / 2, none past tol), so the channel lies on whatever hull the lane holds under it. The core
// saturate(hw + 1/2 - d) (a 2 hw wide line with a one-pixel edge, hw = shape.x) in the core colour, under it the corona
// saturate(1 - (d / hc)^2), hc = hw x corona_x, in the corona's (it carries ion_corona_share): E = e x (core x c_core +
// (1 - core) x corona x c_corona). Alpha 0 under ONE/ONE. Constants: renderer/weapon_effects_pass.cpp (weapon_effects_
// core.h hull::ion_*).
#include "weapon_soft_depth.hlsl"
float4 core_colour : register(c4);   // white mixed ion_core_white towards ion_colour
float4 corona_colour : register(c5); // ion_colour x ion_corona_share
float4 corona : register(c6);        // ion_corona_x, unused x3
float4 main(float2 vpos : VPOS, float4 ends : TEXCOORD0, float4 shape : TEXCOORD1, float4 e : TEXCOORD2) : COLOR0 {
    bool occluder;
    float z = lane_view_depth(vpos, occluder);
    float3 c = 0.0;
    [branch] if (occluder && z > 0.0 && z <= 3.402823466e38) {
        float2 ab = ends.zw - ends.xy, pa = vpos - ends.xy;
        float t = saturate(dot(pa, ab) / max(dot(ab, ab), 1e-6));
        float2 r = pa - ab * t;
        float d = sqrt(dot(r, r));
        float zs = lerp(shape.y, shape.z, t);
        float win = saturate(2.0 - 2.0 * abs(z - zs) / max(shape.w, 1e-6));
        float hc = shape.x * corona.x;
        float k = saturate(shape.x + 0.5 - d);
        float q = saturate(1.0 - d * d / max(hc * hc, 1e-6));
        c = (core_colour.rgb * k + corona_colour.rgb * (q * (1.0 - k))) * (e.x * win);
    }
    return float4(c, 0.0);
}
