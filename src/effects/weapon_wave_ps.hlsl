// Weapon effects, the death fire wave (docs/architecture/explosion-look.md 13.1, D26-D29 and D38-D40; the law and its
// constants: weapon_effects_core.h boom::wave_*, which the lab's fireWave draws too): a thick planar fire front of a
// capital's or a station's death, drawn through the fireball's vertex program over its annulus's eight strips (or the
// slab's box at birth). The pixel's ray (the frame's jittered projection, view depth z as its parameter) meets the slab
// |n . (p - C)| <= h / 2 (n the plane's normal, c13.xyz; C the centre, h / 2 look.y) on [z0, z1], cut by the near plane
// and the lane's surface (occlusion class 2: a hull in front hides it, the sky never). Along it the stretched in-plane
// distance d' (the long axis a = tint.xyz divided by the stretch, 1 / stretch in tint.w) is a quadratic in z; the
// representative point is the segment's point whose d' is nearest the front's radius r (sphere.w): the nearer crossing of
// d' = r, else the closest approach or the far end (face-on the mid-plane's point); a strip's quad (disc.w its sector
// k, the plane's angles (k - 1/2 .. k + 1/2) pi / 4; -1 the box) shades only points of its own sector. The front's radius
// there is shifted by the flame tongues (c7.xy: the circumference's two 1-D noises mixed uncorrelated with its break).
// In z = (d' - r_t) / w_e (w_e look.x), early out outside -window_in < z < window_out: the intensity e^(-z^2) (the
// edge) + the band 4 b (1 - b) at its gain, b = saturate(-z / span), + the sheet (1 - s)^2 at its gain, s = saturate(-z
// / sheet span), band and sheet x the holes the sheet's noise carves behind the front (two octaves of a value noise of
// the in-plane point at c13.w a view unit, spreading with the front; the second's weight c14.y, 0 when the budget sheds
// it), + the wash, capped at 1: the slab never brighter than face-on; x the circumference's break m = floor + (1 -
// floor) saturate(0.5 + contrast (n - bias)) (seeds disc.z and the advect slot: the vertex's disc.y and disc.z through
// the fireball's vertex program); the colour the body's heat ramp (c16..c20) at T falling from the edge's
// over the band to the band's end's and over the sheet to its end's (c9), x the age's cooling (c14.x). The slab's
// density (1 - (2 y / h)^2) integrated over the front's window along the ray, normalised by the perpendicular chord (4 /
// 3 h / 2) and saturated as the body's column: edge-on a bar h thick, never a line. A lane surface point inside the slab
// and the front's window adds the edge and band at light x heat (the swept hulls; never the sky). Radiance x E (look.w:
// wave_e x (1 - t / L_w)^2 x the eye's fade near the plane). One draw a wave. Alpha 0 under ONE / ONE. Constants:
// renderer/weapon_effects_pass.cpp.
#include "weapon_soft_depth.hlsl"
float4 proj : register(c2);    // m00, m11, m20, m21
float4 screen : register(c3);  // 2 / W, 2 / H, near view z, unused
float4 prof : register(c4);    // 1 / trail span (w_e), wash, the swept hulls' light, the break's bias
float4 win : register(c5);     // window in, window out (w_e), m floor, m contrast
float4 noise : register(c6);   // cells 1, cells 2, weight 1, weight 2
float4 sheet : register(c7);   // the tongues' amplitude (w_e), their contrast, 1 / sheet span (w_e), sheet gain
float4 sheet2 : register(c8);  // band gain, the holes' threshold, the second octave's frequency ratio, the holes' softness
float4 temp : register(c9);    // T at the edge, at the band's end, at the sheet's end; the wash's span (w_e)
float4 plane : register(c13);  // the plane's normal (view, unit), the sheet noise's frequency (per view unit)
float4 heat_c : register(c14); // the age's cooling factor (x every T), the sheet's second octave's weight (0: shed),
                               // the holes' cells about the circumference, their radial rate (1 / the stretch)
float4 ramp[5] : register(c16); // the body's heat ramp's stops at T 0, 0.25, 0.5, 0.75, 1 (rgb)
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
float value_noise(float2 x) {
    float2 i = floor(x), f = x - i;
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = hash2(i), b = hash2(i + float2(1, 0)), c = hash2(i + float2(0, 1)), d = hash2(i + float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
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
// The temperature d w_e behind the front: the edge's down to the band's end, then to the sheet's end (2026-11-07: the
// whole wave on the body's ramp), x the age's cooling.
float wave_t(float d) {
    float tb = lerp(temp.x, temp.y, saturate(d * prof.x));
    float bspan = 1.0 / prof.x, sspan = 1.0 / sheet.z;
    float ts = lerp(temp.y, temp.z, saturate((d - bspan) / max(sspan - bspan, 1e-3)));
    return heat_c.x * (d * prof.x < 1.0 ? tb : ts);
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
        float z0 = screen.z, z1 = sqrt(dot(C, C)) + (r + win.y * we) * max(1.0 / s, 1.0) + hh;
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
        [branch] if (z1 > z0 && A > 1e-12 && abs(sqrt(qmid) - r) < (win.x + win.y) * we + span) {
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
            float zz = (dr - r) / we - sheet.x * (2.0 * saturate(0.5 + sheet.y * (nt - 0.5)) - 1.0);
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
                float bb = saturate(-zz * prof.x), ss = saturate(-zz * sheet.z);
                // the sheet's noise behind the front carves holes (0) in band and sheet, never darker material: two
                // octaves of the in-plane point (spreading with the front)
                float fire = 1.0;
                [branch] if (zz < 0.0) {
                    // flame gaps (2026-11-07 v4): periodic in the angle (heat_c.z cells about), the radius at plane.w x
                    // heat_c.w (a 3:1 radial stretch), the second octave at twice the cells and sheet2.z the radial rate
                    float2 pn = float2(u * heat_c.z, dr * plane.w * heat_c.w + disc.z);
                    float ns = vn2p(pn, heat_c.z);
                    [branch] if (heat_c.y > 0.0)
                        ns = (1.0 - heat_c.y) * ns + heat_c.y * vn2p(float2(pn.x * 2.0, pn.y * sheet2.z + advect), 2.0 * heat_c.z);
                    fire = smoothstep(sheet2.y - sheet2.w, sheet2.y + sheet2.w, ns);
                }
                float wash = prof.y * saturate(-zz) * saturate(temp.w + zz);
                // the intensity capped at 1 (the slab never past the face-on brightness, E_w at most), the colour the
                // ramp's at the distance behind the front
                float inten = exp2(-1.442695 * zz * zz) +
                              fire * (sheet2.x * 4.0 * bb * (1.0 - bb) + sheet.w * (1.0 - ss) * (1.0 - ss)) + wash;
                c = heat(wave_t(max(-zz, 0.0))) * (m * min(inten, 1.0) * column);
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
                float bb = saturate(-zz * prof.x);
                float inten = exp2(-1.442695 * zz * zz) + sheet2.x * 4.0 * bb * (1.0 - bb);
                c += prof.z * (1.0 - y * y) * min(inten, 1.0) * heat(wave_t(max(-zz, 0.0)));
            }
        }
    }
    return float4(max(c * look.w, 0.0), 0.0);
}
