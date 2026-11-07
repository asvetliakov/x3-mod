// Weapon effects, the fireball impostor's pixel program (docs/architecture/hull-burst-look.md section 2, the hull hit's
// burst body; section 5 its smoke; weapon-look.md 2.14, 3.2 the explosions reuse it at full size).
// The pixel's ray from the eye through the frame's jittered projection (D3D9 pixel centres at integer raster
// coordinates, the glow's reconstruction) meets the sphere (centre, R_s) on a chord; the radial density
// rho(r) = 1 - r^2 / R^2 integrated along it in closed form (the column, 1 through the centre), from the eye's near
// plane to the sphere's far side or the lane's surface, whichever is nearer (occlusion class 2: the half of a burst
// inside a hull is not there, a hull in front hides it, the sky never hides, an invalid lane texel hides). At the
// chord's middle q (normalised by R_s) the view-plane disc p = rot(theta) q.xy carries two octaves of 2-D value noise and
// a third at w3 = saturate(min(disc.w, screen.w)) and a fourth at saturate(min(disc.w, screen.w) - 1) (disc.w 0 .. 2:
// its size's weights; screen.w the frame's octaves - 2, the budget's steps; weights renormalised over
// w1 + w2 + w3 w_3, mean 1/2: continuous in w3), advected outward by `advect` cells under the drift law (each
// octave's radius u = |p| f cells sampled at u (1 - 1 / (2 sqrt(1 + (u / 2 advect)^2))): injective, no collapse) and
// offset by the seed look.w; the alpha erosion d = saturate((n - thr) / soft), thr = erode_0 + erode_t x +
// erode_r |q|^2 (1 + erode_rt x) (x = look.z, the age fraction); the lobed silhouette saturate((rho(phi) - |q|) / lobe_soft),
// rho(phi) = (1 + lobe_amp (2 n_L - 1)) / (1 + lobe_amp), n_L a periodic value noise of `lobes` cells about the disc
// (seed disc.z); density = d x column^column_pow x lobe. Heat T' = T (rim_t + (1 - rim_t) m), m = column x
// saturate((n - thr) / heat_soft), on the heat ramp; radiance lerp(ramp(T'), tint, share) x E x density (the plasma
// globe's tint, the asteroid's dust at share 1). Smoke (tint.a < 0): over the lane's surface only (never the sky), the
// output tint.rgb x density under the scorch pass's ZERO / INVSRCCOLOR (the scene x (1 - it)). Alpha 0. Constants:
// renderer/weapon_effects_pass.cpp, defaults weapon_effects_core.h look::fireball_*.
// Step 3: a death body's white-hot core (TEXCOORD5 g > 0: radiance x (1 + g), whitened, inside 0.3
// of the radius falling to 0 at 0.5; boom::core_*).
#include "weapon_soft_depth.hlsl"
float4 proj : register(c2);    // m00, m11, m20, m21
float4 screen : register(c3);  // 2 / W, 2 / H, near view z, the third octave's weight at most (0: the budget's step 1)
float4 freq : register(c4);    // the octaves' cells per R_s, lobes
float4 weight : register(c5);  // the octaves' weights, rim_t
float4 erode : register(c6);   // erode_0, erode_t, erode_r, erode_soft
float4 shape : register(c7);   // column_pow, lobe_amp, lobe_soft, heat_soft
float4 ramp[5] : register(c8); // the heat ramp's stops at T 0, 0.25, 0.5, 0.75, 1 (rgb)
float4 erode2 : register(c13); // erode_rt (the radial term x (1 + erode_rt x)), unused, the fourth octave's cells, weight
float hash2(float2 q) {
    float3 p = frac(float3(q.x, q.y, q.x) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}
float value_noise(float2 x) {
    float2 i = floor(x), f = x - i;
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = hash2(i), b = hash2(i + float2(1, 0)), c = hash2(i + float2(0, 1)), d = hash2(i + float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
float3 heat(float t) {
    float x = saturate(t) * 4.0;
    float i = min(floor(x), 3.0);
    float f = x - i;
    float3 lo = i < 0.5 ? ramp[0].rgb : i < 1.5 ? ramp[1].rgb : i < 2.5 ? ramp[2].rgb : ramp[3].rgb;
    float3 hi = i < 0.5 ? ramp[1].rgb : i < 1.5 ? ramp[2].rgb : i < 2.5 ? ramp[3].rgb : ramp[4].rgb;
    return lerp(lo, hi, f);
}
float4 main(float2 vpos : VPOS, float4 sphere : TEXCOORD0, float4 look : TEXCOORD1, float4 tint : TEXCOORD2,
            float4 disc : TEXCOORD3, float advect : TEXCOORD4, float core : TEXCOORD5) : COLOR0 {
    float2 ndc = float2(vpos.x * screen.x - 1.0, 1.0 - vpos.y * screen.y);
    float3 dir = float3((ndc.x - proj.z) / proj.x, (ndc.y - proj.w) / proj.y, 1.0); // the point at view depth z: dir z
    float R = max(sphere.w, 1e-6), R2 = R * R;
    float L2 = dot(dir, dir), l = sqrt(L2);
    float bz = dot(dir, sphere.xyz);
    float zm = bz / L2;                              // the depth of the ray's closest approach to the centre
    float disc2 = R2 - (dot(sphere.xyz, sphere.xyz) - bz * zm); // R^2 - h^2
    bool smoke = tint.a < 0.0;
    float3 c = 0.0;
    [branch] if (disc2 > 0.0) {
        float half_chord = sqrt(disc2);
        float u0 = max(-half_chord, (screen.z - zm) * l), u1 = half_chord;
        bool occluder;
        float lane = lane_view_depth(vpos, occluder);
        bool valid = !occluder || (lane > 0.0 && lane <= 3.402823466e38);
        if (occluder && valid) u1 = min(u1, (lane - zm) * l);
        [branch] if (valid && u1 > u0 && (occluder || !smoke)) {
            float column = saturate(((u1 - u0) * disc2 - (u1 * u1 * u1 - u0 * u0 * u0) / 3.0) * 0.75 / (R2 * R));
            float3 q = (dir * (zm + 0.5 * (u0 + u1) / l) - sphere.xyz) / R;
            float rq2 = dot(q, q), rq = sqrt(rq2);
            float2 p = float2(disc.x * q.x - disc.y * q.y, disc.y * q.x + disc.x * q.y);
            // the lobes first (a periodic value noise of freq.w cells about the disc): outside them no noise is taken
            // atan2(0, 0) is undefined: at the centre (inside every lobe) any angle serves
            float2 pa = dot(p, p) > 1e-12 ? p : float2(1.0, 0.0);
            float t = freq.w * (atan2(pa.y, pa.x) * 0.15915494 + 0.5);
            float i0 = min(floor(t), freq.w - 1.0), fl = saturate(t - i0);
            float i1 = i0 + 1.0 > freq.w - 0.5 ? 0.0 : i0 + 1.0;
            float nl = lerp(hash2(float2(i0, disc.z)), hash2(float2(i1, disc.z)), fl * fl * (3.0 - 2.0 * fl));
            float rho = (1.0 + shape.y * (2.0 * nl - 1.0)) / (1.0 + shape.y);
            float lobe = saturate((rho - rq) / shape.z);
            [branch] if (lobe > 0.0) {
                // the drift law (2026-11-07, no collapse; the constant shift a p / |p| it replaces collapsed every
                // circle |p| = a / f_i onto one noise point, a dot and a ring at the centre from half the life): octave i
                // samples at p (f_i - g_i), g_i = (f_i / 2) / sqrt(1 + (|p| f_i / 2a)^2), the outward shift a y / sqrt(1 +
                // y^2), y = u / 2a, of the radius u = |p| f_i cells: strictly increasing in u (slope 1/2 at the centre, a
                // 2x zoom there, at least 1/2 everywhere), the full shift a far out (0.89 a at u = 4a), no direction and
                // no division at the centre; a = advect -> 0 leaves the pattern
                float k = 0.5 / max(advect, 1e-4);
                float3 g = 0.5 * freq.xyz * rsqrt(1.0 + dot(p, p) * freq.xyz * freq.xyz * (k * k));
                float n = weight.x * value_noise(p * (freq.x - g.x) + look.w * float2(1.7, 3.1)) +
                          weight.y * value_noise(p * (freq.y - g.y) + look.w * float2(3.1, 1.7) + 5.3);
                float wsum = weight.x + weight.y;
                float o3 = min(disc.w, screen.w), w3 = saturate(o3) * weight.z, w4 = saturate(o3 - 1.0) * erode2.w;
                [branch] if (w3 > 0.0) {
                    n += w3 * value_noise(p * (freq.z - g.z) + look.w * float2(0.7, 2.3) + 11.1);
                    wsum += w3;
                    // the fourth octave (2026-11-07: a body past fireball_fine4_px, disc.w 1 .. 2; the frame's octaves 4)
                    [branch] if (w4 > 0.0) {
                        float g4 = 0.5 * erode2.z * rsqrt(1.0 + dot(p, p) * erode2.z * erode2.z * (k * k));
                        n += w4 * value_noise(p * (erode2.z - g4) + look.w * float2(2.9, 0.4) + 17.7);
                        wsum += w4;
                    }
                }
                n /= wsum;
                float thr = erode.x + erode.y * look.z + erode.z * rq2 * (1.0 + erode2.x * look.z);
                float d = saturate((n - thr) / erode.w);
                float density = d * pow(max(column, 1e-6), shape.x) * lobe;
                if (smoke) {
                    c = tint.rgb * density;
                } else {
                    float m = column * saturate((n - thr) / shape.w);
                    float T = look.x * (weight.w + (1.0 - weight.w) * m);
                    // step 3: the death body's white-hot core (core > 0 only for a death body): inside 0.3 of the radius
                    // the radiance x (1 + g) and the colour whitened by min(1, g), to 0 at 0.5
                    float g = core * saturate((0.5 - rq) / 0.2);
                    c = lerp(lerp(heat(T), tint.rgb, tint.a), float3(1.0, 1.0, 1.0), saturate(g)) * (look.y * density * (1.0 + g));
                }
            }
        }
    }
    return float4(max(c, 0.0), 0.0);
}
