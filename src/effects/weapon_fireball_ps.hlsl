// Weapon effects, the fireball impostor's pixel program (docs/architecture/weapon-look.md 2.3, 2.7, 2.14, 3.1 the hull
// hit's hot-gas burst; 3.2 the explosions reuse it at full size: the radius, life and octave count are its inputs).
// The pixel's ray from the eye through the frame's jittered projection (D3D9 pixel centres at integer raster
// coordinates, the glow's reconstruction) meets the sphere (look: centre, R) on a chord; the radial density
// rho(r) = 1 - r^2 / R^2 integrated along it in closed form, normalised to 1 through the centre, from the eye's near
// plane to the sphere's far side or the lane's surface, whichever is nearer (occlusion class 2: the half of a burst
// inside a hull is not there, a hull in front hides it, the sky never hides, an invalid lane texel hides). Two octaves
// (screen.w; 1 is the budget's LOD) of value noise at the chord's middle in the sphere's normalised frame, advected
// outward by look.z cells and offset by the seed look.w, modulate it; erosion takes fireball_erode x (1 - T) away as
// it cools; the soft edge is the chord's own fall to 0 at the limb. Heat T' = T (1 - core_t (1 - column)) on the heat
// ramp (c5..c9), radiance ramp(T') x E x T' x density (E = look.y <= body_max). Alpha 0 under ONE/ONE. Constants:
// renderer/weapon_effects_pass.cpp, defaults weapon_effects_core.h look::fireball_*.
#include "weapon_soft_depth.hlsl"
float4 proj : register(c2);    // m00, m11, m20, m21
float4 screen : register(c3);  // 2 / W, 2 / H, near view z, octaves (1 or 2)
float4 shape : register(c4);   // noise cells per radius, noise amplitude, erosion, core_t
float4 ramp[5] : register(c5); // the heat ramp's stops at T 0, 0.25, 0.5, 0.75, 1 (rgb)
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
float3 heat(float t) {
    float x = saturate(t) * 4.0;
    float i = min(floor(x), 3.0);
    float f = x - i;
    float3 lo = i < 0.5 ? ramp[0].rgb : i < 1.5 ? ramp[1].rgb : i < 2.5 ? ramp[2].rgb : ramp[3].rgb;
    float3 hi = i < 0.5 ? ramp[1].rgb : i < 1.5 ? ramp[2].rgb : i < 2.5 ? ramp[3].rgb : ramp[4].rgb;
    return lerp(lo, hi, f);
}
float4 main(float2 vpos : VPOS, float4 sphere : TEXCOORD0, float4 look : TEXCOORD1) : COLOR0 {
    float2 ndc = float2(vpos.x * screen.x - 1.0, 1.0 - vpos.y * screen.y);
    float3 dir = float3((ndc.x - proj.z) / proj.x, (ndc.y - proj.w) / proj.y, 1.0); // the point at view depth z: dir z
    float R = max(sphere.w, 1e-6), R2 = R * R;
    float L2 = dot(dir, dir), l = sqrt(L2);
    float bz = dot(dir, sphere.xyz);
    float zm = bz / L2;                              // the depth of the ray's closest approach to the centre
    float disc = R2 - (dot(sphere.xyz, sphere.xyz) - bz * zm); // R^2 - h^2
    float3 c = 0.0;
    [branch] if (disc > 0.0) {
        float half_chord = sqrt(disc);
        float u0 = max(-half_chord, (screen.z - zm) * l), u1 = half_chord;
        bool occluder;
        float lane = lane_view_depth(vpos, occluder);
        bool valid = !occluder || (lane > 0.0 && lane <= 3.402823466e38);
        if (occluder && valid) u1 = min(u1, (lane - zm) * l);
        [branch] if (valid && u1 > u0) {
            float column = ((u1 - u0) * disc - (u1 * u1 * u1 - u0 * u0 * u0) / 3.0) * 0.75 / (R2 * R);
            float3 q = (dir * (zm + 0.5 * (u0 + u1) / l) - sphere.xyz) / R;
            float rq = sqrt(dot(q, q));
            float3 x = q * shape.x - q / max(rq, 1e-3) * look.z + look.w * float3(1.7, 3.1, 0.7);
            float n = value_noise(x);
            [branch] if (screen.w > 1.5) n = 0.62 * n + 0.38 * value_noise(x * 2.07 + 5.3);
            float T = look.x;
            float density = saturate(column) * saturate(1.0 + shape.y * (2.0 * n - 1.0) - shape.z * (1.0 - T));
            float t = T * (1.0 - shape.w * (1.0 - saturate(column)));
            c = heat(t) * (look.y * t * density);
        }
    }
    return float4(max(c, 0.0), 0.0);
}
