// Weapon effects, the death fire wave (docs/architecture/explosion-look.md 13.1, D26-D29, D38-D40 and D52 on; the law and
// its constants: weapon_effects_core.h boom::wave_*, which the lab's fireWave draws too): a thick planar fire front of a
// capital's or a station's death, drawn through the fireball's vertex program over its annulus's eight strips (or the
// slab's box at birth). The pixel's ray (the frame's jittered projection, view depth z as its parameter) meets the slab
// |n . (p - C)| <= h / 2 (n the plane's normal, c13.xyz; C the centre, h / 2 look.y) on [z0, z1], cut by the near plane
// and the lane's surface (occlusion class 2: a hull in front hides it, the sky never). Along it the stretched in-plane
// distance d' (the long axis a = tint.xyz divided by the stretch, 1 / stretch in tint.w) is a quadratic in z; the
// representative point is the segment's point whose d' is nearest the front's radius r (sphere.w): the nearer crossing of
// d' = r, else the closest approach or the far end (face-on the mid-plane's point); a strip's quad (disc.w its sector
// k, the plane's angles (k - 1/2 .. k + 1/2) pi / 4; -1 the box) shades only points of its own sector. The front's radius
// there is shifted by the flame tongues (c7.xy: the circumference's two 1-D noises mixed uncorrelated with its break).
// v7 (2026-11-07, D52): the wave is an annulus of the fireball's material. s = (r_t - d') / band (band units: 0 the
// leading edge, 1 the band's end, the sheet beyond to s_end as the eroded outer shell), rq = s / s_end the body's radial
// coordinate; three octaves of a value noise periodic in the angle (c8 cells about the circumference) over s - advect x
// (c9 cells a band; x = t / L_w, c13.w), the body's erosion thr = e_0 + e_t x + e_r rq^2 (1 + e_rt x), its lobes from the
// break's mix, the density never under the band's floor (dark red, no transparent hole) and the heat falling to the
// floor's T where the noise is under the threshold; the leading edge a thin hot rim (e^(-(z / ahead)^2) ahead, over the
// lead behind) broken by the circumference's break m. T: the edge's over the lead, the band's start -> end, -> the
// sheet's end (c21), the pixel's T the intensity-weighted mean of rim and material, its excess over the floor's T x
// sqrt(the intensity) (dim is cool), x the age's cooling (c14.x); the radial cells x sqrt(r_ref R_f / r) (c14.z). The
// intensity min(1, rim + density): the slab never brighter than face-on. The slab's density (1 - (2 y / h)^2) integrated
// over the front's window along the ray, normalised by the perpendicular chord (4 / 3 h / 2) and saturated as the body's
// column: edge-on a bar h thick, never a line. A lane surface point inside the slab and the front's window adds the rim
// and band at light x heat (the swept hulls; never the sky). Radiance x E (look.w: wave_e x the fade x the eye's fade
// near the plane). One draw a wave. Alpha 0 under ONE / ONE. Constants: renderer/weapon_effects_pass.cpp.
#include "weapon_soft_depth.hlsl"
float4 proj : register(c2);    // m00, m11, m20, m21
float4 screen : register(c3);  // 2 / W, 2 / H, near view z, unused
float4 prof : register(c4);    // 1 / the band (w_e), the sheet's end (band units), the swept hulls' light, the break's bias
float4 win : register(c5);     // window in, window out (w_e), m floor, m contrast
float4 noise : register(c6);   // the break's cells 1, 2, weights 1, 2
float4 shape : register(c7);   // the tongues' amplitude (w_e), their contrast, the lobes' amplitude, their softness (rq)
float4 cells : register(c8);   // the octaves' cells about the circumference (1, 2, 3), the advection (band units over L_w)
float4 rad : register(c9);     // the octaves' cells a band (1, 2, 3), the rim's lead (band units)
float4 wts : register(c10);    // the octaves' weights (1, 2, 3), the material's share under a broken rim
float4 erode : register(c11);  // e_0, e_t, e_r, the erosion's softness
float4 erode2 : register(c12); // e_rt, the heat's softness, the band's floor density, the floor's T
float4 plane : register(c13);  // the plane's normal (view, unit), the age fraction x = t / L_w
float4 heat_c : register(c14); // the age's cooling factor (x every T), the sheet's third octave's weight (0: shed), the
                               // radial cells' scale sqrt(r_ref R_f / r), unused
float4 ramp[5] : register(c16); // the body's heat ramp's stops at T 0, 0.25, 0.5, 0.75, 1 (rgb)
float4 temp : register(c21);   // T at the edge, at the band's start, at its end, at the sheet's end
float4 edge_c : register(c22); // the rim's fall-off ahead (w_e), its share modulated by the material's noise, unused
float hash2(float2 q) {
    float3 p = frac(float3(q.x, q.y, q.x) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}
// A periodic 1-D value noise of `cells` cells over u in [0, 1).
float vn1(float u, float cells, float seed) {
    float t = u * cells;
    float i0 = min(floor(t), cells - 1.0), f = saturate(t - i0);
    float i1 = i0 + 1.0 > cells - 0.5 ? 0.0 : i0 + 1.0;
    return lerp(hash2(float2(i0, seed)), hash2(float2(i1, seed)), f * f * (3.0 - 2.0 * f));
}
// A value noise periodic in x over `cells` cells (the angle's wrap: no seam).
float vn2p(float2 x, float cells) {
    float2 i = floor(x), f = x - i;
    i.x = i.x - cells * floor(i.x / cells);
    float i1 = i.x + 1.0 > cells - 0.5 ? 0.0 : i.x + 1.0;
    float2 w = f * f * (3.0 - 2.0 * f);
    float a = hash2(i), b = hash2(float2(i1, i.y)), c = hash2(i + float2(0, 1)), d = hash2(float2(i1, i.y + 1.0));
    return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
}
float3 heat(float t) {
    float x = saturate(t) * 4.0;
    float i = min(floor(x), 3.0);
    float f = x - i;
    float3 lo = i < 0.5 ? ramp[0].rgb : i < 1.5 ? ramp[1].rgb : i < 2.5 ? ramp[2].rgb : ramp[3].rgb;
    float3 hi = i < 0.5 ? ramp[1].rgb : i < 1.5 ? ramp[2].rgb : i < 2.5 ? ramp[3].rgb : ramp[4].rgb;
    return lerp(lo, hi, f);
}
// The material's temperature profile s band units behind the front (before the noise and the age): the band's start ->
// end over the band, -> the sheet's end over the sheet (the rim carries the edge's T over the lead).
float t_profile(float s) {
    float tb = lerp(temp.y, temp.z, saturate(s));
    float ts = lerp(temp.z, temp.w, saturate((s - 1.0) / max(prof.y - 1.0, 1e-3)));
    return s < 1.0 ? tb : ts;
}
// The rim's intensity at zz (w_e, > 0 ahead) and s: e^(-(zz / ahead)^2) ahead, 1 - smoothstep(0, lead, s) behind.
float rim_profile(float zz, float s) {
    return zz > 0.0 ? exp2(-1.442695 * (zz * zz) / (edge_c.x * edge_c.x)) : 1.0 - smoothstep(0.0, rad.w, s);
}
float4 main(float2 vpos : VPOS, float4 sphere : TEXCOORD0, float4 look : TEXCOORD1, float4 tint : TEXCOORD2,
            float4 disc : TEXCOORD3, float advect : TEXCOORD4) : COLOR0 {
    float2 ndc = float2(vpos.x * screen.x - 1.0, 1.0 - vpos.y * screen.y);
    float3 dir = float3((ndc.x - proj.z) / proj.x, (ndc.y - proj.w) / proj.y, 1.0);
    float3 n = plane.xyz, a = tint.xyz, b = cross(n, a), C = sphere.xyz;
    float r = sphere.w, we = max(look.x, 1e-6), hh = max(look.y, 1e-6), s = tint.w;
    bool occluder;
    float lane = lane_view_depth(vpos, occluder);
    bool lane_ok = !occluder || (lane > 0.0 && lane <= 3.402823466e38);
    float3 c = 0.0;
    [branch] if (lane_ok) {
        // the slab chord in z
        float nd = dot(n, dir), nc = dot(n, C);
        float z0 = screen.z, z1 = sqrt(dot(C, C)) + (r + (win.y + shape.x) * we) * max(1.0 / s, 1.0) + hh;
        if (abs(nd) > 1e-7) {
            float za = (nc - hh) / nd, zb = (nc + hh) / nd;
            z0 = max(z0, min(za, zb));
            z1 = min(z1, max(za, zb));
        } else if (abs(nc) > hh) {
            z1 = z0 - 1.0;
        }
        if (occluder) z1 = min(z1, lane);
        float da = dot(a, dir) * s, ca = dot(a, C) * s, db = dot(b, dir), cb = dot(b, C);
        float A = da * da + db * db, B = da * ca + db * cb, Cq = ca * ca + cb * cb;
        // early out: the mid-plane's point farther from the front than the window and the chord's in-plane span
        float zmid = 0.5 * (z0 + z1), qmid = max(A * zmid * zmid - 2.0 * B * zmid + Cq, 0.0);
        float span = abs(A * zmid - B) * rsqrt(max(qmid, 1e-12)) * max(z1 - z0, 0.0);
        [branch] if (z1 > z0 && A > 1e-12 && abs(sqrt(qmid) - r) < (win.x + win.y + 2.0 * shape.x) * we + span) {
            float zc = clamp(B / A, z0, z1);
            float q0 = max(A * zc * zc - 2.0 * B * zc + Cq, 0.0);
            float zr = zc;
            if (q0 < r * r) {
                float disc2 = B * B - A * (Cq - r * r), sq = sqrt(max(disc2, 0.0));
                float zm = (B - sq) / A, zp = (B + sq) / A;
                float q1 = max(A * z0 * z0 - 2.0 * B * z0 + Cq, 0.0), q2 = max(A * z1 * z1 - 2.0 * B * z1 + Cq, 0.0);
                zr = zm >= z0 && zm <= z1 ? zm : zp >= z0 && zp <= z1 ? zp : (q1 > q2 ? z0 : z1);
            }
            // a short in-plane span (face-on): the mid-plane's point, the front's own profile; a long one (edge-on): the
            // nearest crossing, the bar
            zr = lerp(zmid, zr, saturate((span - 2.0 * we) / (4.0 * we)));
            float xa = zr * da - ca, xb = zr * db - cb;
            float dr = sqrt(xa * xa + xb * xb);
            float u = atan2(xb, xa) * 0.15915494 + 0.5;
            // the circumference's two noises: the break's mix, and the tongues' (w2 n1 - w1 n2 + w1: uncorrelated with the
            // break's, mostly the finer noise) shifting the front's radius
            float n1 = vn1(u, noise.x, disc.z), n2 = vn1(u, noise.y, advect);
            float nn = noise.z * n1 + noise.w * n2, nt = noise.w * n1 - noise.z * n2 + noise.z;
            float zz = (dr - r) / we - shape.x * (2.0 * saturate(0.5 + shape.y * (nt - 0.5)) - 1.0);
            // a strip shades only its own sector of the plane (disc.w its index, -1 the box): the strips overlap on
            // screen and the blend adds
            float sector = floor(frac(u - 0.4375) * 8.0);
            [branch] if ((disc.w < -0.5 || abs(sector - disc.w) < 0.5) && zz > -win.x && zz < win.y) {
                // the front's window along the ray: the edge's width over the in-plane rate, within the chord
                float l = sqrt(dot(dir, dir));
                float rate = abs(A * zr - B) / max(dr, 1e-6);
                float half_z = min(0.5 * 1.7724539 * we / max(rate, 1e-6), 0.5 * (z1 - z0));
                float zlo = max(zr - half_z, z0), zhi = min(zr + half_z, z1);
                if (zhi - zlo < 2.0 * half_z) { // the window against an end: shift it inside the chord
                    zlo = max(z0, zhi - 2.0 * half_z);
                    zhi = min(z1, zlo + 2.0 * half_z);
                }
                float ya = zlo * nd - nc, yb = zhi * nd - nc;
                float integral = abs(nd) > 1e-7 ? (zhi - zlo) - (yb * yb * yb - ya * ya * ya) / (3.0 * hh * hh * nd)
                                                : (zhi - zlo) * (1.0 - ya * ya / (hh * hh));
                float column = saturate(integral * l / (1.3333333 * hh));
                float m = win.z + (1.0 - win.z) * saturate(0.5 + win.w * (nn - prof.w));
                // v7: the fireball's material on (the angle, s): s band units behind the shifted front, rq = s / s_end
                float sb = -zz * prof.x, sp = max(sb, 0.0), rq = sp / prof.y, x = plane.w;
                float y = (sp - cells.w * x) * heat_c.z; // the radial cells grow with the front as sqrt(r)
                float nz = wts.x * vn2p(float2(u * cells.x + 1.7 * disc.z, y * rad.x + 3.1 * advect), cells.x) +
                           wts.y * vn2p(float2(u * cells.y + 3.1 * disc.z + 5.3, y * rad.y + 1.7 * advect + 5.3), cells.y);
                float wsum = wts.x + wts.y;
                // the third octave; past the band it is the sheet's, the budget's first shed (heat_c.y 0)
                float w3 = wts.z * (sb > 1.0 ? heat_c.y : 1.0);
                [branch] if (w3 > 0.0) {
                    nz += w3 * vn2p(float2(u * cells.z + 0.7 * disc.z + 10.6, y * rad.z + 2.3 * advect + 10.6), cells.z);
                    wsum += w3;
                }
                nz /= wsum;
                float thr = erode.x + erode.y * x + erode.z * rq * rq * (1.0 + erode2.x * x);
                float d = saturate((nz - thr) / erode.w), mh = saturate((nz - thr) / erode2.y);
                // the lobes from the break's mix (the strong arcs' material reaches farther behind)
                float rho = (1.0 + shape.z * (2.0 * nn - 1.0)) / (1.0 + shape.z);
                float lobe = saturate((rho - rq) / shape.w);
                // the band never under its floor (dark red, no transparent hole); fading over the sheet
                float flo = erode2.z * (1.0 - smoothstep(1.0, prof.y, sb));
                // ahead of the shifted front the material falls off as the rim does (where the rim is broken its edge is
                // the material's: never a cut)
                float ahead = zz > 0.0 ? exp2(-1.442695 * (zz * zz) / (edge_c.x * edge_c.x)) : 1.0;
                float dens = max(flo, d * lobe) * (1.0 - smoothstep(0.7, prof.y, sb)) * ahead * (wts.w + (1.0 - wts.w) * m);
                float rim = rim_profile(zz, sb) * m * (1.0 - edge_c.y + edge_c.y * d);
                float tm = erode2.w + (t_profile(sp) - erode2.w) * mh;
                float tot = rim + dens, inten = min(tot, 1.0);
                // the intensity-weighted T of rim and material, cooled towards the floor's where the wave is dim (a dim
                // yellow-white reads grey or olive, never fire)
                float T = erode2.w + ((temp.x * rim + tm * dens) / max(tot, 1e-4) - erode2.w) * sqrt(inten);
                c = heat(T * heat_c.x) * (inten * column);
            }
        }
        // the swept hulls: the lane's surface point inside the slab and the front's window
        [branch] if (occluder) {
            float3 pl = dir * lane - C;
            float y = dot(n, pl) / hh;
            float xa = dot(a, pl) * s, xb = dot(b, pl);
            float zz = (sqrt(xa * xa + xb * xb) - r) / we;
            float sector = floor(frac(atan2(xb, xa) * 0.15915494 + 0.0625) * 8.0);
            [branch] if ((disc.w < -0.5 || abs(sector - disc.w) < 0.5) && abs(y) < 1.0 && zz > -win.x && zz < win.y) {
                float sb = -zz * prof.x;
                float ri = rim_profile(zz, sb), fl = erode2.z * (1.0 - smoothstep(0.7, prof.y, sb)) * step(0.0, sb);
                float T = (temp.x * ri + t_profile(max(sb, 0.0)) * fl) / max(ri + fl, 1e-4);
                c += prof.z * (1.0 - y * y) * min(ri + fl, 1.0) * heat(T * heat_c.x);
            }
        }
    }
    return float4(max(c * look.w, 0.0), 0.0);
}
