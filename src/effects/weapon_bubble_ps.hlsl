// Weapon effects, shield hit (docs/architecture/weapon-effects.md 5.1.1 "Placement"): the ship bubble's pixel program.
// The patch's cells lie on the owner's bubble, an ellipsoid in the owner frame (centre c, half-extents e; the stage's
// hull bounds x bubble_scale), not on the lane: per pixel the view ray (view z = t: the ray's z component is 1) meets
// the bubble analytically in the bubble's normalised space (q = (x - c) / e: the unit sphere; d = the ray's direction
// there, linear in the raster, so c19..c21 give it as vpos.x X + vpos.y Y + D0; c22 the eye there). The chord's middle
// tm = -o.d / d.d and its point h = o + tm d give 1 - |h|^2 (> 0: the ray meets the bubble) and the half chord
// sqrt((1 - |h|^2) / d.d), robust for a far eye; the near and far points q = h -+ half d at t = tm -+ half. Each point
// is drawn only where it lies in front of the eye (t > near z) and nearer than the lane (the hull and anything else in
// front hides it; the sky is far): the near side faces the eye, the far side shows around the hull's outline, at
// bubble_far_gain (c19.w; c20.w the near side's gain); where both draw they add. At each point the outward normal
// n = normalize(q / e) gives the face's triplanar share as weapon_shell_ps.hlsl does (|n_k|^s normalised, less the
// floor, renormalised; only where n points along the face's axis, so of each axis one sheet takes the point), the
// point's cell on the face's lattice (u, v = c.u + q.(e u), c.v + q.(e v): c24, c25) and its clock from the draw's cell
// map (c26..c93, the shell program's layout and lookup), where the point lies in the draw's box (c94, c95, in q: the
// sheet over the cells within their anchors' depth -+ the extent, stage::bubble_box). The lane is fetched once, for the occlusion; no normal is
// rebuilt and nothing is placed from it. The eye inside the bubble (the cockpit, a close chase) has the near point
// behind it: only the far side draws. Alpha 0 under ONE/ONE. Constants: renderer/weapon_effects_pass.cpp hex_block,
// bubble_block and map_block.
#include "weapon_soft_depth.hlsl"
#include "weapon_hex_patch.hlsl"
float4 anchor[3] : register(c8);  // the face's axis, its u, its v (owner frame)
float4 ray_x : register(c19);     // d per raster x (normalised owner space), far side's gain
float4 ray_y : register(c20);     // d per raster y, near side's gain
float4 ray_0 : register(c21);     // d at raster (0, 0), the triplanar sharpness
float4 eye_q : register(c22);     // the eye in normalised owner space, the near plane's view z
float4 inv_e : register(c23);     // 1 / e (owner axes), the triplanar floor
float4 face_u : register(c24);    // e * u (owner frame), c . u
float4 face_v : register(c25);    // e * v, c . v
float4 keys[32] : register(c26);   // four packed keys per register (< 0 unused)
float4 clocks[32] : register(c58); // game seconds since lit + 1, negative for a hit's centre cell (0 not lit yet)
float4 ranges[4] : register(c90);  // per group of sixteen keys its key range: first, last x2
float4 box_lo : register(c94);     // the draw's box in normalised owner coordinates: a point must lie inside
float4 box_hi : register(c95);
// The lane's depth at raster r as an occluder: 1e30 on sky; a lane texel without a valid depth occludes everything.
float occluder_z(float2 r) {
    bool occluder;
    float z = lane_view_depth(r, occluder);
    return !occluder ? 1e30 : (z > 0.0 && z <= 3.402823466e38 ? z : 0.0);
}
// The hexagon distance's border forms (|l.x|, |l.x / 2 +- l.y sqrt 3 / 2|): their fwidth for the plane point's
// gradients gx, gy (in cells).
float3 border_fw(float2 gx, float2 gy) {
    float2 b1 = float2(0.5, 0.8660254), b2 = float2(0.5, -0.8660254);
    return float3(abs(gx.x) + abs(gy.x), abs(dot(b1, gx)) + abs(dot(b1, gy)), abs(dot(b2, gx)) + abs(dot(b2, gy)));
}
// One point's cell (f: its plane point, view units; fw3: border_fw of its gradients) at weight w.
float3 cell_shade(float2 f, float3 fw3, float w) {
    float2 k;
    float3 c = hex_cell(f * lattice.x, k);
    float2 b1 = float2(0.5, 0.8660254), b2 = float2(0.5, -0.8660254);
    float d0 = abs(c.x), d1 = abs(dot(c.xy, b1)), d2 = abs(dot(c.xy, b2));
    float fw = max(d0 >= d1 && d0 >= d2 ? fw3.x : d1 >= d2 ? fw3.y : fw3.z, 1e-5);
    float packed = (k.x + 2048.0) * 4096.0 + (k.y + 2048.0);
    float4 found = 0.0, clock4 = 0.0;
    [unroll] for (int h = 0; h < 4; ++h) {
        [unroll] for (int half = 0; half < 2; ++half) {
            float2 r = half ? ranges[h].zw : ranges[h].xy;
            [branch] if (packed >= r.x && packed <= r.y) {
                [unroll] for (int i = 8 * h + 4 * half; i < 8 * h + 4 * half + 4; ++i) {
                    float4 match = saturate(1.0 - abs(keys[i] - packed));
                    found += match;
                    clock4 += match * clocks[i];
                }
            }
        }
    }
    float clock = dot(clock4, 1.0);
    float3 shade = 0.0;
    [branch] if (dot(found, 1.0) > 0.5) {
        float since = abs(clock) > 0.5 ? max(abs(clock) - 1.0, 0.0) : -1.0;
        shade = hex_shade(c.xy, c.z, fw, k, float2(0.5, 0.8660254) * k * lattice.y, since, clock < -0.5, 4, false) * w;
    }
    return shade;
}
float4 main(float2 vpos : VPOS) : COLOR0 {
    float zl = occluder_z(vpos);
    float3 d = vpos.x * ray_x.xyz + vpos.y * ray_y.xyz + ray_0.xyz;
    float a = dot(d, d);
    float tm = -dot(eye_q.xyz, d) / a;
    float3 h = eye_q.xyz + tm * d;
    float disc = 1.0 - dot(h, h);
    float half_t = sqrt(max(disc, 0.0) / a);
    float3 qn = h - half_t * d, qf = h + half_t * d;
    // The plane points and their gradients outside the branches (the edge band's anti-aliasing; the branch below tests
    // their border widths, so the compiler keeps the gradients ahead of every branch).
    float2 fn = float2(dot(qn, face_u.xyz) + face_u.w, dot(qn, face_v.xyz) + face_v.w);
    float2 ff = float2(dot(qf, face_u.xyz) + face_u.w, dot(qf, face_v.xyz) + face_v.w);
    float3 fwn = border_fw(ddx(fn) * lattice.x, ddy(fn) * lattice.x), fwf = border_fw(ddx(ff) * lattice.x, ddy(ff) * lattice.x);
    float3 shade = 0.0;
    [branch] if (disc > 0.0 && dot(fwn + fwf, 1.0) < 1e30) { // false only for a non-finite gradient
        [unroll] for (int side = 0; side < 2; ++side) { // the near point, then the far one
            bool far_side = side > 0;
            float t = far_side ? tm + half_t : tm - half_t, gain = far_side ? ray_x.w : ray_y.w;
            float3 q = far_side ? qf : qn;
            [branch] if (t > eye_q.w && t < zl && gain > 0.0 && all(q >= box_lo.xyz) && all(q <= box_hi.xyz)) {
                float3 n = normalize(q * inv_e.xyz);
                float3 m = pow(max(abs(n), 1e-6), ray_0.w);
                float3 s = max(m / (m.x + m.y + m.z) - inv_e.w, 0.0);
                float w = dot(n, anchor[0].xyz) > 0.0 ? dot(s, abs(anchor[0].xyz)) / max(s.x + s.y + s.z, 1e-6) : 0.0;
                [branch] if (w > 0.0)
                    shade += cell_shade(far_side ? ff : fn, far_side ? fwf : fwn, w * gain);
            }
        }
    }
    return float4(max(shade, 0.0), 0.0);
}
