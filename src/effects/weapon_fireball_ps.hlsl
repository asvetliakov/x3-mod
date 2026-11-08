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
// The fire material, round 3 (docs/architecture/fire-material.md section 13; weapon_effects_core.h look::fire_*): TEXCOORD5
// = (g, fire, lod, t_g). fire > 0 blends the turbulence law in by wl = saturate(|lod|) x saturate(fire) (under fire_px_0
// and for the plasma globe's half the previous erosion law on the fire ramp, its gain fire_gain_small): a connected hot
// blob whose outline deforms, licked by tongues on its rim. Five octaves of value noise at f_i = fire_turb_f 2^i cells
// per R_s, each dropped under fire_cell_px on screen (the projected R_s; weights saturate(cell / floor - 1)), the fourth
// and fifth also under the budget's octave steps (screen.w 1, 0), the third to fifth when the box is shed (core.z < 0).
// Octaves 4-5 Cartesian, re-seeded at 1 / fire_jit_ms of game time (15 Hz); octaves 2-3 polar (fire_turb_n2 / _n3 cells
// about the disc, periodic: no seam; fire_turb_f 2^i / fire_stretch cells per R_s outward: tongues 1.5 : 1), streaming
// outward at fire_flow_v 1.4^i R_s per game second, taken outside fire_polar_r0 .. r1 (the polar centre is singular),
// each warped along the angle by fire_turb_warp cells of the next finer octave; octave 1 Cartesian under the drift law,
// sliding fire_env_rate cells over the life, warped by octaves 2-3. T = the ridged sum of |2 n_i - 1| fire_turb_a^i,
// normalised by its weights. The envelope: the radius r' = |p| / (1 + fire_env_amp (2 n_1 - 1)) (the blob's silhouette
// bulges and shifts with octave 1); F = 1/2 + fire_turb_k (fire_turb_c - T) - (fire_thr_0 + fire_thr_t x + fire_thr_r r'^2
// (1 + fire_thr_rt x)) - the cluster's thinning. alpha smoothstep(0, fire_soft, F)^fire_gain x column^column_pow (the lobes
// off under the law: the envelope is the silhouette); T' = T(t) (fire_rim_t + (1 - fire_rim_t) m^0.75), m = saturate(F /
// fire_heat_soft): colour from density (the filaments' edges red, the dense interior at T(t)), the luminance x
// lerp(fire_lum_0, 1, m) (1 - fire_lum_depth T_fine), T_fine the ridged sum of octaves 3-5. The fire core inside
// fire_core_r0 falling to 0 at fire_core_r1 of R_s (c25), whitened towards the fire ramp's top stop, never neutral white.
// fire 0: the previous law on the heat ramp (dust, smoke, the eye inside a body); fire < 0: on the fire ramp (the kill
// switch).
#include "weapon_soft_depth.hlsl"
float4 proj : register(c2);    // m00, m11, m20, m21
float4 screen : register(c3);  // 2 / W, 2 / H, near view z, the third octave's weight at most (0: the budget's step 1)
float4 freq : register(c4);    // the octaves' cells per R_s, lobes
float4 weight : register(c5);  // the octaves' weights, rim_t
float4 erode : register(c6);   // erode_0, erode_t, erode_r, erode_soft
float4 shape : register(c7);   // column_pow, lobe_amp, lobe_soft, heat_soft
float4 ramp[5] : register(c8); // the heat ramp's stops at T 0, 0.25, 0.5, 0.75, 1 (rgb)
float4 erode2 : register(c13); // erode_rt (the radial term x (1 + erode_rt x)), unused, the fourth octave's cells, weight
float4 framp[5] : register(c14); // the fire ramp's stops at T 0, 0.25, 0.5, 0.75, 1 (rgb); c15.w hull::fire_cluster_thr
                                 // (fire 1 .. 2: a cluster's burst, its body thinned)
float4 fire1 : register(c19);   // fire_turb_f, fire_turb_a, fire_turb_k, fire_turb_c
float4 fire2 : register(c20);   // fire_turb_n2, fire_turb_n3, 1 / fire_stretch, fire_turb_warp
float4 fire3 : register(c21);   // fire_flow_v / 1000 (R_s per game ms), 1 / fire_jit_ms, fire_thr_0, fire_thr_t
float4 fire4 : register(c22);   // fire_thr_r, 1 / fire_soft, fire_gain, fire_gain_small
float4 fire5 : register(c23);   // 1 / fire_heat_soft, fire_rim_t, fire_env_amp, fire_env_rate
float4 fire6 : register(c24);   // fire_polar_r0, fire_polar_r1, 1 / fire_cell_px, fire_thr_rt
float4 fire7 : register(c25);   // fire_core_r1, 1 / (fire_core_r1 - fire_core_r0), fire_lum_0, fire_lum_depth
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
            // the turbulence law's weight (fire > 0; the plasma globe's half and a body under fire_px_1 blend the
            // previous law in); the fire law's radius the view-plane disc's |p| (the chord's middle q leaves the plane
            // where the lane cuts the chord)
            float fire = core.y, wl = fire > 0.0 ? saturate(abs(core.z)) * saturate(fire) : 0.0;
            float rp2 = dot(p, p), rp = sqrt(rp2);
            // the lobes first (a periodic value noise of freq.w cells about the disc): outside them no noise is taken;
            // off under the turbulence law (its envelope is the silhouette). atan2(0, 0) is undefined: at the centre
            // (inside every lobe) any angle serves
            float2 pa = rp2 > 1e-12 ? p : float2(1.0, 0.0);
            float u = atan2(pa.y, pa.x) * 0.15915494 + 0.5;
            float t = freq.w * u;
            float i0 = min(floor(t), freq.w - 1.0), fl = saturate(t - i0);
            float i1 = i0 + 1.0 > freq.w - 0.5 ? 0.0 : i0 + 1.0;
            float nl = lerp(hash2(float2(i0, disc.z)), hash2(float2(i1, disc.z)), fl * fl * (3.0 - 2.0 * fl));
            float amp = shape.y * (1.0 - wl);
            float rho = (1.0 + amp * (2.0 * nl - 1.0)) / (1.0 + amp);
            float lobe = saturate((rho - rq) / shape.z);
            [branch] if (lobe > 0.0) {
                // the drift law (2026-11-07, no collapse; the constant shift a p / |p| it replaces collapsed every
                // circle |p| = a / f_i onto one noise point, a dot and a ring at the centre from half the life): octave i
                // samples at p (f_i - g_i), g_i = (f_i / 2) / sqrt(1 + (|p| f_i / 2a)^2), the outward shift a y / sqrt(1 +
                // y^2), y = u / 2a, of the radius u = |p| f_i cells: strictly increasing in u (slope 1/2 at the centre, a
                // 2x zoom there, at least 1/2 everywhere), the full shift a far out (0.89 a at u = 4a), no direction and
                // no division at the centre; a = advect -> 0 leaves the pattern
                float k = 0.5 / max(advect, 1e-4);
                float colpow = pow(max(column, 1e-6), shape.x);
                float dens_o = 0.0, T_o = 0.0, dens_f = 0.0, T_f = 0.0;
                // the previous law (smoke, dust, the eye inside a body; a fire body under fire_px_1 or the plasma globe
                // in part): Cartesian octaves 1-2, the third at w3 = saturate(min(disc.w, screen.w)), the fourth past it
                [branch] if (wl < 1.0) {
                    float3 g = 0.5 * freq.xyz * rsqrt(1.0 + dot(p, p) * freq.xyz * freq.xyz * (k * k));
                    float n = weight.x * value_noise(p * (freq.x - g.x) + look.w * float2(1.7, 3.1)) +
                              weight.y * value_noise(p * (freq.y - g.y) + look.w * float2(3.1, 1.7) + 5.3);
                    float wsum = weight.x + weight.y;
                    float o3 = min(disc.w, screen.w), w3 = saturate(o3) * weight.z, w4 = saturate(o3 - 1.0) * erode2.w;
                    [branch] if (w3 > 0.0) {
                        n += w3 * value_noise(p * (freq.z - g.z) + look.w * float2(0.7, 2.3) + 11.1);
                        wsum += w3;
                    }
                    // the fourth octave (2026-11-07: a body past fireball_fine4_px, disc.w 1 .. 2; the frame's octaves 4)
                    [branch] if (w4 > 0.0) {
                        float g4 = 0.5 * erode2.z * rsqrt(1.0 + rp2 * erode2.z * erode2.z * (k * k));
                        n += w4 * value_noise(p * (erode2.z - g4) + look.w * float2(2.9, 0.4) + 17.7);
                        wsum += w4;
                    }
                    n /= wsum;
                    // a fire body's threshold carries the cluster's thinning, its alpha the small gain
                    float thr = erode.x + erode.y * look.z + erode.z * rq2 * (1.0 + erode2.x * look.z) +
                                (fire > 0.0 ? framp[1].w * saturate(fire - 1.0) : 0.0);
                    float d = saturate((n - thr) / erode.w);
                    dens_o = (fire > 0.0 ? pow(max(d, 1e-6), fire4.w) * step(1e-6, d) : d) * colpow * lobe;
                    T_o = look.x * (weight.w + (1.0 - weight.w) * column * saturate((n - thr) / shape.w));
                }
                // the turbulence law (fire-material.md 13): octave i's cell on screen lw / 2^(i - 1) floors, lw the
                // projected R_s over fire_turb_f over fire_cell_px; weights 0 at one floor, 1 at two
                [branch] if (wl > 0.0) {
                    float lw = R * proj.x / (max(sphere.z, 1e-4) * screen.x * fire1.x) * fire6.z;
                    float on = core.z > 0.0 ? 1.0 : 0.0; // a shed box: octaves 3-5 off
                    float wp = smoothstep(fire6.x, fire6.y, rp), a1 = fire1.y, a2 = a1 * a1;
                    // octave 1's ridges at a quarter inside the polar band (its cracks across the dense centre), its
                    // envelope everywhere
                    float w1 = saturate(lw - 1.0) * (0.25 + 0.75 * wp), w2 = saturate(0.5 * lw - 1.0) * wp;
                    float w3 = saturate(0.25 * lw - 1.0) * wp * on;
                    // octaves 4-5 also off in a cluster's burst (fire > 1: N overlapping bursts sum their fine detail
                    // away, and pay it N times)
                    float on4 = on * step(fire, 1.05);
                    float w4 = saturate(0.125 * lw - 1.0) * on4 * saturate(screen.w);
                    float w5 = saturate(0.0625 * lw - 1.0) * on4 * saturate(screen.w - 1.0);
                    // octaves 4-5: Cartesian, re-seeded every fire_jit_ms of game time
                    float2 jo = floor(core.w * fire3.y) * float2(7.31, 3.97) + look.w * float2(2.9, 0.4);
                    float n2 = 0.5, n3 = 0.5, n4 = 0.5, n5 = 0.5;
                    [branch] if (w5 > 0.0) n5 = value_noise(p * (16.0 * fire1.x) + jo + 41.3);
                    [branch] if (w4 > 0.0) n4 = value_noise(p * (8.0 * fire1.x) + jo.yx + 23.9);
                    // octaves 2-3: polar, periodic about the disc, stretched outward, streaming outward at 1.4^i x the
                    // flow, warped along the angle by the next finer octave
                    float tf = core.w * fire3.x;
                    [branch] if (w3 > 0.0) {
                        float a3 = fire2.y * u + fire2.w * (2.0 * n4 - 1.0) + 2.3 * look.w;
                        float s3 = (rp - 1.96 * tf) * (4.0 * fire1.x * fire2.z) + 0.9 * look.w;
                        n3 = vnp(float2(a3, s3), fire2.y);
                    }
                    [branch] if (w2 > 0.0) {
                        float a2p = fire2.x * u + fire2.w * (2.0 * n3 - 1.0) + 1.3 * look.w;
                        float s2 = (rp - 1.4 * tf) * (2.0 * fire1.x * fire2.z) + 0.7 * look.w;
                        n2 = vnp(float2(a2p, s2), fire2.x);
                    }
                    // octave 1: the drift law, sliding fire_env_rate cells over the life, warped by octaves 2-3; always
                    // taken (the envelope)
                    float g1 = 0.5 * fire1.x * rsqrt(1.0 + rp2 * fire1.x * fire1.x * (k * k));
                    float n1 = value_noise(p * (fire1.x - g1) + look.w * float2(1.7, 3.1) + look.z * fire5.w * float2(0.6, 0.8) +
                                           fire2.w * float2(2.0 * n2 - 1.0, 2.0 * n3 - 1.0));
                    float wf = a2 * w3 + a1 * a2 * w4 + a2 * a2 * w5, ws = w1 + a1 * w2 + wf;
                    float tf3 = a2 * w3 * abs(2.0 * n3 - 1.0) + a1 * a2 * w4 * abs(2.0 * n4 - 1.0) + a2 * a2 * w5 * abs(2.0 * n5 - 1.0);
                    float T = (w1 * abs(2.0 * n1 - 1.0) + a1 * w2 * abs(2.0 * n2 - 1.0) + tf3) / max(ws, 1e-4);
                    // the envelope: the radius warped by octave 1 (the outline bulges and shifts), the erosion profile
                    float re = rp / (1.0 + fire5.z * (2.0 * n1 - 1.0));
                    float F = 0.5 + fire1.z * (fire1.w - T) -
                              (fire3.z + fire3.w * look.z + fire4.x * re * re * (1.0 + fire6.w * look.z)) -
                              framp[1].w * saturate(fire - 1.0);
                    float sF = saturate(F * fire4.y);
                    float al = sF * sF * (3.0 - 2.0 * sF);
                    // the luminance follows the density, lerp(fire_lum_0, 1, m), and the fine octaves' ridges, x (1 -
                    // fire_lum_depth T_fine): bright veins over darker billows at the small scales, the dense core kept
                    // (an orange at full alpha x E turns peach under AgX)
                    float mf = saturate(F * fire5.x), tfn = tf3 / max(wf, 1e-4);
                    dens_f = pow(max(al, 1e-6), fire4.z) * step(1e-6, al) * colpow * lobe * lerp(fire7.z, 1.0, mf) *
                             (1.0 - fire7.w * tfn);
                    T_f = look.x * (fire5.y + (1.0 - fire5.y) * pow(max(mf, 1e-6), 0.75));
                }
                float density = lerp(dens_o, dens_f, wl), T = lerp(T_o, T_f, wl);
                if (smoke) {
                    c = tint.rgb * dens_o;
                } else {
                    // the white-hot core (core.x > 0: a death body, a fire class's hull burst): inside fire_core_r0 the
                    // radiance x (1 + g) and the colour whitened towards the top stop by min(1, g), to 0 at fire_core_r1
                    float g = core.x * (fire != 0.0 ? saturate((fire7.x - rp) * fire7.y) : saturate((0.5 - rq) / 0.2));
                    c = lerp(lerp(heat(T, fire != 0.0), tint.rgb, tint.a), framp[4].rgb, saturate(g)) *
                        (look.y * density * (1.0 + g));
                }
            }
        }
    }
    return float4(max(c, 0.0), 0.0);
}
