// Weapon effects, the composed death body (docs/architecture/explosion-look.md 13.2; the table: weapon_effects_boom.h
// boom_compose_table, its constants boom::compose_*, the lab's composeTable): N sub-bursts (and jets) of the fireball
// impostor in one quad, drawn one body a draw through the fireball's vertex program. The CPU's table in c14..c42 (per
// sphere: its view centre and radius, its jet axis and the ray's squeeze 1 - short / long, its erosion clock, its
// noise and lobe seeds and heat, its white-hot core's gain); the quad's disc (rotation, the third octave's weight) and
// look.y (E) as the burst's. Each sphere is the burst program's chord, lobes, drift-law noise, erosion, heat and core
// (weapon_fireball_ps.hlsl, unchanged there); a jet's ray and centre are first squeezed along its axis (affine: the
// chord's parameter holds). The densities compose as the densest sphere's plus a soft share of the others' (c13.y:
// min(1, d_max + soft (sum - d_max)); 2026-11-07, the union 1 - prod(1 - d_i) filled every sub-burst's holes: smooth
// balloons), the colour is the density-weighted mean of the spheres' heat and core: overlaps never add past E (the core
// on top only while a sub-burst is young: its gain, the CPU's, is 0 past compose_core_x of its age). In the shared-noise mode (the eye inside the cluster, the LOD's
// step 1b) the noise is taken once, at the first sphere the ray meets, and the other spheres take only their chord.
// Alpha 0 under ONE / ONE. Constants: renderer/weapon_effects_pass.cpp.
#include "weapon_soft_depth.hlsl"
float4 proj : register(c2);    // m00, m11, m20, m21
float4 screen : register(c3);  // 2 / W, 2 / H, near view z, the third octave's weight at most (0: the budget's step 1)
float4 freq : register(c4);    // the octaves' cells per R_s, lobes
float4 weight : register(c5);  // the octaves' weights, rim_t
float4 erode : register(c6);   // erode_0, erode_t, erode_r, erode_soft
float4 shape : register(c7);   // column_pow, lobe_amp, lobe_soft, heat_soft
float4 ramp[5] : register(c8); // the heat ramp's stops at T 0, 0.25, 0.5, 0.75, 1 (rgb)
float4 erode2 : register(c13); // erode_rt, the soft add of the other spheres' densities, the fourth octave's cells, weight
float4 head : register(c14);   // spheres drawn, the shared-noise mode (1), unused x2
float4 sph[28] : register(c15); // per sphere k: [4k] centre (view), R; [4k+1] jet axis, squeeze; [4k+2] erosion clock x,
                                // noise seed, lobe seed, heat T; [4k+3] core gain, unused x3
// The burst program's noise and ramp (weapon_fireball_ps.hlsl, verbatim).
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
            float4 disc : TEXCOORD3, float advect : TEXCOORD4) : COLOR0 {
    float2 ndc = float2(vpos.x * screen.x - 1.0, 1.0 - vpos.y * screen.y);
    float3 dir = float3((ndc.x - proj.z) / proj.x, (ndc.y - proj.w) / proj.y, 1.0);
    bool occluder;
    float lane = lane_view_depth(vpos, occluder);
    float3 c = 0.0;
    [branch] if (!occluder || (lane > 0.0 && lane <= 3.402823466e38)) {
        float dmax = 0.0, ws = 0.0, Ts = 0.0, gs = 0.0, n_shared = -1.0;
        float o3 = min(disc.w, screen.w), w3 = saturate(o3) * weight.z, w4 = saturate(o3 - 1.0) * erode2.w;
        [loop] for (int k = 0; k < 7; ++k) {
            [branch] if (k < head.x) {
                float4 s0 = sph[4 * k], s1 = sph[4 * k + 1], s2 = sph[4 * k + 2];
                float3 dj = dir - (s1.w * dot(dir, s1.xyz)) * s1.xyz, cj = s0.xyz - (s1.w * dot(s0.xyz, s1.xyz)) * s1.xyz;
                float R = max(s0.w, 1e-6), R2 = R * R;
                float L2 = dot(dj, dj), l = sqrt(L2);
                float bz = dot(dj, cj);
                float zm = bz / L2;
                float disc2 = R2 - (dot(cj, cj) - bz * zm);
                [branch] if (disc2 > 0.0) {
                    float half_chord = sqrt(disc2);
                    float u0 = max(-half_chord, (screen.z - zm) * l), u1 = half_chord;
                    if (occluder) u1 = min(u1, (lane - zm) * l);
                    [branch] if (u1 > u0) {
                        float column = saturate(((u1 - u0) * disc2 - (u1 * u1 * u1 - u0 * u0 * u0) / 3.0) * 0.75 / (R2 * R));
                        float3 q = (dj * (zm + 0.5 * (u0 + u1) / l) - cj) / R;
                        float rq2 = dot(q, q), rq = sqrt(rq2);
                        float2 p = float2(disc.x * q.x - disc.y * q.y, disc.y * q.x + disc.x * q.y);
                        float lobe = 1.0;
                        [branch] if (head.y < 0.5 || n_shared < 0.0) { // the shared mode: the chord alone past the first
                            float2 pa = dot(p, p) > 1e-12 ? p : float2(1.0, 0.0);
                            float t = freq.w * (atan2(pa.y, pa.x) * 0.15915494 + 0.5);
                            float i0 = min(floor(t), freq.w - 1.0), fl = saturate(t - i0);
                            float i1 = i0 + 1.0 > freq.w - 0.5 ? 0.0 : i0 + 1.0;
                            float nl = lerp(hash2(float2(i0, s2.z)), hash2(float2(i1, s2.z)), fl * fl * (3.0 - 2.0 * fl));
                            float rho = (1.0 + shape.y * (2.0 * nl - 1.0)) / (1.0 + shape.y);
                            lobe = saturate((rho - rq) / shape.z);
                        }
                        [branch] if (lobe > 0.0) {
                            float x = s2.x, nn = n_shared;
                            [branch] if (head.y < 0.5 || nn < 0.0) {
                                float kk = 0.5 / max(advect * x, 1e-4);
                                float3 g = 0.5 * freq.xyz * rsqrt(1.0 + dot(p, p) * freq.xyz * freq.xyz * (kk * kk));
                                nn = weight.x * value_noise(p * (freq.x - g.x) + s2.y * float2(1.7, 3.1)) +
                                     weight.y * value_noise(p * (freq.y - g.y) + s2.y * float2(3.1, 1.7) + 5.3);
                                float wsum = weight.x + weight.y;
                                [branch] if (w3 > 0.0 && head.y < 0.5) {
                                    nn += w3 * value_noise(p * (freq.z - g.z) + s2.y * float2(0.7, 2.3) + 11.1);
                                    wsum += w3;
                                    [branch] if (w4 > 0.0) { // the fourth octave (the fireball program's)
                                        float g4 = 0.5 * erode2.z * rsqrt(1.0 + dot(p, p) * erode2.z * erode2.z * (kk * kk));
                                        nn += w4 * value_noise(p * (erode2.z - g4) + s2.y * float2(2.9, 0.4) + 17.7);
                                        wsum += w4;
                                    }
                                }
                                nn /= wsum;
                                n_shared = nn;
                            }
                            float thr = erode.x + erode.y * x + erode.z * rq2 * (1.0 + erode2.x * x);
                            // column^column_pow at the burst's 0.25 (renderer/weapon_effects_pass.cpp asserts it)
                            float density = saturate((nn - thr) / erode.w) * sqrt(sqrt(max(column, 1e-6))) * lobe;
                            float m = column * saturate((nn - thr) / shape.w);
                            float g = sph[4 * k + 3].x * saturate((0.5 - rq) / 0.2);
                            dmax = max(dmax, saturate(density));
                            ws += density;
                            Ts += density * s2.w * (weight.w + (1.0 - weight.w) * m);
                            gs += density * g;
                        }
                    }
                }
            }
        }
        float Tm = ws > 0.0 ? Ts / ws : 0.0, gm = ws > 0.0 ? gs / ws : 0.0;
        float u = saturate(dmax + erode2.y * (ws - dmax));
        c = lerp(lerp(heat(Tm), tint.rgb, tint.a), float3(1.0, 1.0, 1.0), saturate(gm)) * (look.y * u * (1.0 + gm));
    }
    return float4(max(c, 0.0), 0.0);
}
