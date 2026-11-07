// Scratch (not production): the ray-sheet alternative of explosion-look.md section 14 for its slot count. One quad per
// death about the projected centre; per pixel the angle's slot among N, the two nearest slots' hashed rays (angle
// jitter, length, width, brightness, flicker), the same profile as the ray kind (taper, fade-in, (1 - u)^1.5, box filter).
#include "weapon_soft_depth.hlsl"
float4 sheet : register(c5);   // N, turn, ray start px, time (game ms)
float4 sheet2 : register(c6);  // R_f px, body px, envelope, seed
float hash1(float x) { return frac(sin(x * 12.9898) * 43758.5453); }
float ray(float slot, float ang, float r, float t) {
    float n = sheet.x, s = fmod(slot + n, n);
    float h0 = hash1(s + sheet2.w), h1 = hash1(s * 1.7 + sheet2.w + 3.1), h2 = hash1(s * 2.3 + sheet2.w + 5.7);
    float h3 = hash1(s * 3.1 + sheet2.w + 7.3);
    float th = (slot + 0.1 + 0.8 * h0) / n + sheet.y;
    float d = frac(ang - th + 0.5) - 0.5;
    float across = r * sin(6.2831853 * d);
    float len = max((1.2 + 0.8 * h1) * sheet2.x, 1.6 * sheet2.y);
    float u = saturate((r - sheet.z) / len);
    float hw = 0.5 * (1.0 + 2.0 * h2) * (1.0 - 0.6 * u);
    float x = saturate(hw + 0.5 - abs(across));
    float period = 30.0 + 30.0 * h3, q = t / period, qf = floor(q), fr = q - qf;
    float a0 = hash1(qf + s * 9.1), a1 = hash1(qf + 1.0 + s * 9.1);
    float flick = 1.0 - 0.55 * lerp(a0, a1, fr * fr * (3.0 - 2.0 * fr));
    float f = 1.0 - u;
    return (0.45 + 0.55 * h1) * flick * f * sqrt(f) * saturate(8.0 * u) * x * (r > sheet.z && r < sheet.z + len ? 1.0 : 0.0);
}
float4 main(float2 vpos : VPOS, float4 shade : TEXCOORD0, float4 colour : TEXCOORD1, float4 depth : TEXCOORD2) : COLOR0 {
    bool occluder;
    float lane = lane_view_depth(vpos, occluder);
    bool valid = !occluder || (lane > 0.0 && lane <= 3.402823466e38);
    float2 p = shade.xy;
    float r = length(p);
    float ang = atan2(p.y, p.x) * 0.15915494 + 0.5;
    float slot = floor((ang - sheet.y) * sheet.x);
    float e = ray(slot, ang, r, sheet.w) + ray(slot + 1.0, ang, r, sheet.w) + ray(slot - 1.0, ang, r, sheet.w);
    float vis = occluder ? saturate((lane - depth.x) / max(depth.z, 1e-4)) : 1.0;
    float3 c = colour.rgb * (shade.z * sheet2.z * e * vis);
    return float4(valid ? max(c, 0.0) : 0.0, 0.0);
}
