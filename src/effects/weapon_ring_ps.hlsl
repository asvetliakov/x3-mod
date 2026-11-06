// Weapon effects, the surface ring as a shock front (docs/architecture/hull-impact-classes.md 3.5; the law and its
// constants: weapon_effects_core.h hull::ring_*, which the lab's shockFront draws too): the glow primitive with a front
// profile. The lane's surface point under the pixel (weapon_soft_depth.hlsl; the sky sentinel or an invalid depth draws
// nothing), rebuilt through the frame's jittered projection as the glow's; its offset d from the hit taken into the
// owner's frame with the shot's stretch folded in (TEXCOORD3..5: the rows of M, M d = R d - (1 - 1 / ring_stretch) (R a)
// (a . d), R the owner's axes in view, a the shot's heading; the view's axes without the owner's frame, no stretch
// without a heading), d' = |M d|, z = (d' - r) / w with r the ring's radius now (shape.x) and w its width (1 / shape.y):
// E = colour x (m (exp(-z^2) + 4 b (1 - b) trail) + wash), b = saturate(-z / span): the white-hot leading edge at r and the
// redder trailing band from r - span w to r (trail: the band's colour over the edge's, c4; 1 / span), cut by whatever surface the
// lane holds, never on sky; m the noise's modulation of the front, one octave of value noise of the owner-frame
// direction M d / d' x freq + the record's seed shape.z x c6.xyz (a negative seed: none) where z < c6.w; wash = shape.w
// saturate((r - d') / w) 2^(-max(r - d', 0) light.w) inside the front only (shape.w 0: none; light.w = log2(e) /
// (ring_wash_fall r), the exponent clamped so a newborn ring's tiny r never makes 0 x inf). The colour carries the edge's
// heat, the radiance and the (1 - t / L)^2 fade; only the wash outside -span < z < c7.x. Alpha 0 under ONE/ONE.
// Constants: renderer/weapon_effects_pass.cpp.
#include "weapon_soft_depth.hlsl"
float4 proj : register(c2);   // m00, m11, m20, m21
float4 screen : register(c3); // 2 / W, 2 / H, unused x2
float4 trail : register(c4);  // the trailing band's colour over the leading edge's (x ring_trail_gain), 1 / its span (w)
float4 front : register(c5);  // noise frequency, contrast, depth, the band's span (w)
float4 seeds : register(c6);  // the seed's axis (hull::ring_seed_axis), the noise's window (ring_noise_window, w)
float4 windows : register(c7); // the front's window (ring_front_window, w), unused x3
// The filament's value noise (weapon_filament_ps.hlsl; the lab's valueNoise).
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
        float3 q = float3(dot(d, ax.xyz), dot(d, ay.xyz), dot(d, az.xyz));
        float dl = sqrt(dot(q, q));
        float x = (dl - shape.x) * shape.y;
        float behind = shape.x - dl;
        float3 e = shape.w * saturate(behind * shape.y) * exp2(-max(behind, 0.0) * light.w); // the wash (0 outside)
        // The front where it shows (-span < z < 3: inside, the band is 0 and the edge under e^-9; outside, the edge's
        // tail under e^-9): the edge, the band and the noise's modulation of both.
        [branch] if (x > -front.w && x < windows.x) {
            float b = saturate(-x * trail.w);
            float m = 1.0;
            [branch] if (shape.z >= 0.0 && x < seeds.w) {
                float n = value_noise(q * (front.x / max(dl, 1e-6)) + shape.z * seeds.xyz);
                m = 1.0 - front.z + front.z * saturate(0.5 + front.y * (n - 0.5));
            }
            e += m * (exp2(-1.442695 * x * x) + (4.0 * b * (1.0 - b)) * trail.rgb);
        }
        c = colour.rgb * e;
    }
    return float4(c, 0.0);
}
