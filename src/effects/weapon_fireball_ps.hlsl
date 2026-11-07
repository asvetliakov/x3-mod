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
// The fire material (docs/architecture/fire-material.md section 2; weapon_effects_core.h look::fire_*): TEXCOORD5 =
// (g, fire, lod, t_g). fire > 0: in the band rq > fire_blend_0 a polar tongue noise (fire_cells about the disc,
// periodic: no seam; stretched outward, warped, sheared, streaming outward at 1 cell / fire_flow_ms of game time)
// replaces the third Cartesian octave and blends over octaves 1-2 by w_t = smoothstep(blend_0, blend_1, rq) x fire x
// w_px; a hard threshold thr = thr_0 + thr_t x + thr_r rq^2 (1 + erode_rt x) over fire_soft with the convex gain
// lerp(gain_small, gain, w_px); a per-tongue flicker at 1 cell / fire_flick_ms; the colour fire_ramp (c14..c18) of T (rim_t
// + (1 - rim_t) m^0.75), m = saturate((n - thr) / heat_soft). fire < 0: the previous law on the fire ramp (the kill
// switch); fire 0: the previous law on the heat ramp (dust, smoke, the eye inside a body). w_px = saturate(lod), the big
// body's second polar octave weight fire_oct2_w + fire_oct2_big saturate(lod - 1). The fire law's radius (the blend, the
// tongues' s, the threshold, the core) is the view-plane disc's |p|, never the chord middle's |q| (a lane-cut chord). The
// tips cool to red, T x (1 - fire_rim_cool smoothstep(fire_rim_r0, fire_rim_r1, |p|)) (c24); the fire core inside
// fire_core_r0 falling to 0 at fire_core_r1 of R_s (c25).
// Round 2 (2026-11-07, fire-material.md section 12): a fine octave n_f (under the tongues the polar noise at fire_fine_k x
// their cells and s, streaming outward with them; elsewhere the Cartesian third octave, taken for a fire body of any
// size) modulates the luminance by 1 - fire_fine_depth saturate(fire_fine_bias - 2 n_f) and jitters the threshold by
// fire_fine_jitter (n_f - 1/2): ragged edges, structure inside every tongue; the threshold blends to the fire law by
// w_px saturate(2 fire - 1) (the plasma globe, fire 0.5, keeps the previous threshold and its late-life floor); the
// core (every body's, the eye-inside one too) whitens towards the fire ramp's top stop (yellow-white), never neutral white.
#include "weapon_soft_depth.hlsl"
float4 proj : register(c2);    // m00, m11, m20, m21
float4 screen : register(c3);  // 2 / W, 2 / H, near view z, the third octave's weight at most (0: the budget's step 1)
float4 freq : register(c4);    // the octaves' cells per R_s, lobes
float4 weight : register(c5);  // the octaves' weights, rim_t
float4 erode : register(c6);   // erode_0, erode_t, erode_r, erode_soft
float4 shape : register(c7);   // column_pow, lobe_amp, lobe_soft, heat_soft
float4 ramp[5] : register(c8); // the heat ramp's stops at T 0, 0.25, 0.5, 0.75, 1 (rgb)
float4 erode2 : register(c13); // erode_rt (the radial term x (1 + erode_rt x)), unused, the fourth octave's cells, weight
float4 framp[5] : register(c14); // the fire ramp's stops at T 0, 0.25, 0.5, 0.75, 1 (rgb); c14.w fire_fine_bias,
                                 // c15.w hull::fire_cluster_thr (fire 1 .. 2: a cluster's burst, its tongues thinned)
float4 fire1 : register(c19);   // fire_cells, fire_s_cells, fire_warp_f, fire_warp_amp
float4 fire2 : register(c20);   // fire_shear, fire_blend_0, fire_blend_1, 1 / fire_flow_ms
float4 fire3 : register(c21);   // 1 / fire_flick_ms, fire_flick_amp, fire_thr_0, fire_thr_t
float4 fire4 : register(c22);   // fire_thr_r, 1 / fire_soft, fire_gain, fire_gain_small
float4 fire5 : register(c23);   // 1 / fire_heat_soft, fire_rim_t, fire_oct2_w, fire_oct2_big
float4 fire6 : register(c24);   // fire_rim_cool, fire_rim_r0, fire_rim_r1, fire_fine_depth
float4 fire7 : register(c25);   // fire_core_r1, 1 / (fire_core_r1 - fire_core_r0), fire_fine_jitter, fire_fine_k
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
// A value noise periodic in x.x over `cells` cells (the tongues' angle: no seam at the wrap).
float vnp(float2 x, float cells) {
    float2 i = floor(x), f = x - i;
    i.x = i.x - cells * floor(i.x / cells);
    float i1 = i.x + 1.0 > cells - 0.5 ? 0.0 : i.x + 1.0;
    float2 w = f * f * (3.0 - 2.0 * f);
    float a = hash2(i), b = hash2(float2(i1, i.y)), c = hash2(i + float2(0, 1)), d = hash2(float2(i1, i.y + 1.0));
    return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
}
// A 1-D value noise periodic over `cells` cells (the flicker), seeded.
float vn1p(float t, float cells, float seed) {
    float i0 = floor(t), f = t - i0;
    i0 = i0 - cells * floor(i0 / cells);
    float i1 = i0 + 1.0 > cells - 0.5 ? 0.0 : i0 + 1.0;
    return lerp(hash2(float2(i0, seed)), hash2(float2(i1, seed)), f * f * (3.0 - 2.0 * f));
}
// The heat ramp (fire false) or the fire ramp (true), T in [0, 1].
float3 heat(float t, bool fire) {
    float x = saturate(t) * 4.0;
    float i = min(floor(x), 3.0);
    float f = x - i;
    float3 r0 = fire ? framp[0].rgb : ramp[0].rgb, r1 = fire ? framp[1].rgb : ramp[1].rgb, r2 = fire ? framp[2].rgb : ramp[2].rgb;
    float3 r3 = fire ? framp[3].rgb : ramp[3].rgb, r4 = fire ? framp[4].rgb : ramp[4].rgb;
    float3 lo = i < 0.5 ? r0 : i < 1.5 ? r1 : i < 2.5 ? r2 : r3;
    float3 hi = i < 0.5 ? r1 : i < 1.5 ? r2 : i < 2.5 ? r3 : r4;
    return lerp(lo, hi, f);
}
float4 main(float2 vpos : VPOS, float4 sphere : TEXCOORD0, float4 look : TEXCOORD1, float4 tint : TEXCOORD2,
            float4 disc : TEXCOORD3, float advect : TEXCOORD4, float4 core : TEXCOORD5) : COLOR0 {
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
                // the fire material's weights: w_px the size LOD, the tongues' blend w_t (0 inside fire_blend_0)
                // the fire law's radius: the view-plane disc's |p| (the chord's middle q leaves the plane where the lane
                // cuts the chord: a hull hit's tongues would meet at the screen centre with |q|)
                // core.z < 0: the budget shed the tongues (step 0c, the fence), the law's size LOD -core.z kept
                float fire = core.y, wpx = saturate(core.z), rp2 = dot(p, p), rp = sqrt(rp2);
                float wt = max(fire, 0.0) * wpx * smoothstep(fire2.y, fire2.z, rp);
                float n = weight.x * value_noise(p * (freq.x - g.x) + look.w * float2(1.7, 3.1)) +
                          weight.y * value_noise(p * (freq.y - g.y) + look.w * float2(3.1, 1.7) + 5.3);
                float wsum = weight.x + weight.y;
                // the third octave, replaced by the tongues where they are (its fine grain never shows under them)
                float o3 = min(disc.w, screen.w), w3 = saturate(o3) * weight.z * (1.0 - wt), w4 = saturate(o3 - 1.0) * erode2.w;
                // the third octave; on a fire body also the fine octave's Cartesian part (wt < 1) at any size
                float n3 = 0.5;
                [branch] if (max(w3, fire * (1.0 - wt)) > 0.0) {
                    n3 = value_noise(p * (freq.z - g.z) + look.w * float2(0.7, 2.3) + 11.1);
                    n += w3 * n3;
                    wsum += w3;
                }
                // the fourth octave (2026-11-07: a body past fireball_fine4_px, disc.w 1 .. 2; the frame's octaves 4), on
                // the Cartesian term only
                [branch] if (w4 > 0.0) {
                    float g4 = 0.5 * erode2.z * rsqrt(1.0 + dot(p, p) * erode2.z * erode2.z * (k * k));
                    n += w4 * value_noise(p * (erode2.z - g4) + look.w * float2(2.9, 0.4) + 17.7);
                    wsum += w4;
                }
                n /= wsum;
                float flick = 1.0, nf = n3;
                [branch] if (wt > 0.0) {
                    // the tongues: two warp taps, the polar domain (a about the disc, s outward, streaming with t_g), two
                    // periodic octaves; the per-tongue flicker
                    float2 wp = p * fire1.z;
                    float wa = fire1.w * (2.0 * value_noise(wp + look.w * float2(0.9, 2.7) + 21.3) - 1.0);
                    float wb = fire1.w * (2.0 * value_noise(wp + look.w * float2(2.1, 0.5) + 31.7) - 1.0);
                    float u = atan2(pa.y, pa.x) * 0.15915494 + 0.5;
                    float a = fire1.x * u + fire2.x * rp * wb + 1.3 * look.w;
                    float s = rp * fire1.y - core.w * fire2.w + wa + 0.7 * look.w;
                    float wo = fire5.z + fire5.w * saturate(core.z - 1.0);
                    float nt = (1.0 - wo) * vnp(float2(a, s), fire1.x) + wo * vnp(float2(2.0 * a + 0.5, 2.0 * s + 3.7), 2.0 * fire1.x);
                    n = lerp(n, nt, wt);
                    // the fine octave under the tongues: the polar noise at fire_fine_k x their cells, streaming with s
                    float kf = fire7.w;
                    nf = lerp(n3, vnp(float2(kf * a + 0.3, kf * s + 1.9), kf * fire1.x), wt);
                    flick = lerp(1.0, 1.0 - fire3.y + fire3.y * vn1p(fire1.x * u + core.w * fire3.x, fire1.x, look.w), wt);
                }
                float colpow = pow(max(column, 1e-6), shape.x);
                if (smoke) {
                    float thr = erode.x + erode.y * look.z + erode.z * rq2 * (1.0 + erode2.x * look.z);
                    c = tint.rgb * (saturate((n - thr) / erode.w) * colpow * lobe);
                } else {
                    float density, T;
                    [branch] if (fire > 0.0) {
                        // the fire law blended in by the size LOD w_px: under fire_px_0 the previous erosion and heat law
                        // (today's clumps and holes, section 2.5) on the fire ramp with the small gain; wl the law's blend
                        // (the size LOD x the fire weight: the plasma globe half), wth the threshold's (the plasma globe:
                        // the previous one), both in this branch only (the eye-inside body's fence, 300 us)
                        float wlaw = saturate(abs(core.z));
                        float wl = wlaw * saturate(fire), wth = wlaw * saturate(2.0 * fire - 1.0);
                        float thr_o = erode.x + erode.y * look.z + erode.z * rq2 * (1.0 + erode2.x * look.z);
                        float thr_f = fire3.z + fire3.w * look.z + fire4.x * rp2 * (1.0 + erode2.x * look.z);
                        float thr = lerp(thr_o, thr_f, wth) + fire7.z * (nf - 0.5) + framp[1].w * saturate(fire - 1.0);
                        float d = saturate((n - thr) / lerp(erode.w, 1.0 / fire4.y, wl));
                        density = pow(max(d, 1e-6), lerp(fire4.w, fire4.z, wl)) * step(1e-6, d) * flick * colpow * lobe *
                                  (1.0 - fire6.w * saturate(framp[0].w - 2.0 * nf)); // the fine octave's luminance
                        float m = saturate((n - thr) * fire5.x);
                        float T_f = look.x * (fire5.y + (1.0 - fire5.y) * pow(max(m, 1e-6), 0.75)) *
                                    (1.0 - fire6.x * smoothstep(fire6.y, fire6.z, rp)); // the tongues' tips red
                        float T_o = look.x * (weight.w + (1.0 - weight.w) * column * saturate((n - thr) / shape.w));
                        T = lerp(T_o, T_f, wl);
                    } else {
                        float thr = erode.x + erode.y * look.z + erode.z * rq2 * (1.0 + erode2.x * look.z);
                        density = saturate((n - thr) / erode.w) * colpow * lobe;
                        float m = column * saturate((n - thr) / shape.w);
                        T = look.x * (weight.w + (1.0 - weight.w) * m);
                    }
                    // the white-hot core (core.x > 0: a death body, a fire class's hull burst): inside 0.3 of the radius
                    // the radiance x (1 + g) and the colour whitened by min(1, g), to 0 at 0.5
                    float g = core.x * (fire != 0.0 ? saturate((fire7.x - rp) * fire7.y) : saturate((0.5 - rq) / 0.2));
                    c = lerp(lerp(heat(T, fire != 0.0), tint.rgb, tint.a), framp[4].rgb, saturate(g)) *
                        (look.y * density * (1.0 + g));
                }
            }
        }
    }
    return float4(max(c, 0.0), 0.0);
}
