// Weapon effects, shield hit (docs/architecture/weapon-effects.md 5.1.1): the screen-space hex patch's pixel program.
// The pixel's surface is the lane: its view depth (weapon_soft_depth.hlsl; the sky sentinel or an invalid depth draws
// nothing) rebuilds the view point P on the pixel's eye ray (c96, the frame's jittered projection folded on the CPU;
// D3D9 pixel centres at integer raster coordinates: ndc = 2 r / size - 1). The normal n is the cross product of P's
// screen derivatives (ddx, ddy; a 2x2 quad across a silhouette or a crease mixes two surfaces for its edge pixels, and
// a quad touching the sky has none and draws nothing), turned towards the eye. The stand-off (user request after
// Run237 Flight D): the patch floats the tier's stand-off d (c19.x) above the hull along the normal, so the pixel
// shows the hull point H whose offset H + n d lies on its view ray. One parallax step: P's tangent plane offset by d,
// cut by the ray, gives the estimate H1 (exact on a plane); the lane at H1's texel (c97) gives the hull point Q there,
// and Q's plane (normal n) offset and cut the same way gives H. A
// second step does not reduce the screen error (the fixed normal bounds it; the fixture's STANDOFF_STEPS rows). The
// floated surface is clipped at the hull's outline: the pixel draws nothing when its own lane or the lane under H1
// shows the sky, when H1 is at or behind the eye plane or projects off the screen, when Q departs from P's tangent
// plane by more than d (the hull curves or steps away under the floated point, or an occluder covers H1) or when the
// ray meets the offset plane behind the eye. Cost: P's face coordinates outside the hull box grown by 4 d skip the
// normal and the step (H lies d tan(theta) from P on a plane: a view more than 76 degrees off the normal loses the
// patch's far band), and H1 outside the box grown by 2 d skips the fetch. The per-draw face frame in view space
// (c93..c95, .w the owner centre's offset) takes H to the face's coordinates (u, v, a); (u, v) on the face's lattice
// gives the pixel's cell, looked up in the draw's cell map (128 packed keys in ascending order in c23..c54, their
// clocks in c55..c86; unrolled in eight groups of four registers, each behind a branch on its key range in c87..c90,
// so a pixel scans the one group that can hold its key: a rolled loop's keys[k] compiles into a compare per register
// with this d3dx). The box (c91..c92) bounds the cells'
// hull surface (H must lie in it: the rasterized box, grown by d, covers the pixel, not the point) along the face's
// axis (their anchors -+ the extent); within it every cell of the map draws, so a cell's pixels draw or not with the
// cell, not with which hit lights it. The face's share (n in the owner frame) is the triplanar weight
// w_a = |n_a|^s / sum_k |n_k|^s (s = triplanar_sharpness) less the floor f,
// max(w_a - f, 0) renormalised over the three axes, where the outward normal points along the face's axis a, else 0:
// of each axis one face takes the point, the faces of an owner share it and never add over it, and a face tilted past
// about 55 degrees (w_a < f = 0.3 at s = 8 for any split of the rest) takes nothing, so the depth of a cell's anchor
// hit, which moves with the hits along a steep face, never decides its pixels where they would still show; a pixel
// with no share skips the lookup. The cell's look is weapon_hex_patch.hlsl's (the map's clock: game seconds since lit
// + 1, negative for a hit's centre cell, 0 unlit: a ring passing over a cell not reached yet). Alpha 0 under ONE/ONE.
// Constants: renderer/weapon_effects_pass.cpp hex_block, shell_block, map_block and lift_block, defaults
// weapon_effects_core.h.
#include "weapon_soft_depth.hlsl"
#include "weapon_hex_patch.hlsl"
float4 anchor[3] : register(c8);  // the lattice's axis, its u, its v (owner frame)
float4 params : register(c19);    // the stand-off d (view units), the triplanar sharpness, the triplanar floor, unused
float4 axes[3] : register(c20);   // the owner's local axes in view space (the normal's owner-frame components)
float4 keys[32] : register(c23);    // four packed keys per register (< 0 unused; a group led by < 0 is skipped)
float4 clocks[32] : register(c55);  // their game seconds since lit + 1, negative for a hit's centre cell (0 not lit yet)
float4 ranges[4] : register(c87);   // per group of sixteen keys (four registers) its key range: first, last x2
float4 box_lo : register(c91);      // the draw's box in the face frame (u, v, a): the hull point H must lie inside
float4 box_hi : register(c92);
float4 face[3] : register(c93);     // the face frame (u, v, a) in view space, .w its offset: f = dot(float4(v, 1), face)
float4 ray_law : register(c96);     // the eye ray of raster r: (r A.xy + A.zw, 1)
float4 raster_law : register(c97);  // view point v to raster + 1/2: (v.xy / v.z) B.xy + B.zw
float4 near_lo : register(c98);     // the hull box grown by 4 d: P's pre-test (H lies within d tan(theta) of P)
float4 near_hi : register(c99);
float4 step_lo : register(c100);    // the hull box grown by 2 d: H1's test before the parallax fetch
float4 step_hi : register(c101);
// The lane's surface depth at raster r; 1e30 off the hull (sky, an invalid depth).
float surface_z(float2 r) {
    bool occluder;
    float z = lane_view_depth(r, occluder);
    return occluder && z > 0.0 && z <= 3.402823466e38 ? z : 1e30;
}
float3 to_owner(float3 d) {
    return float3(dot(d, axes[0].xyz), dot(d, axes[1].xyz), dot(d, axes[2].xyz));
}
// The face frame (u, v, a) of a view point.
float3 face_of(float3 v) {
    float4 h = float4(v, 1.0);
    return float3(dot(h, face[0]), dot(h, face[1]), dot(h, face[2]));
}
float3 ray_of(float2 r) {
    return float3(r * ray_law.xy + ray_law.zw, 1.0);
}
float4 main(float2 vpos : VPOS) : COLOR0 {
    float z = surface_z(vpos);
    float3 ray = ray_of(vpos);
    float3 vc = ray * z; // P
    float3 fp = face_of(vc);
    // The gradients outside the branches: the normal (view space; a quad touching the sky has none: its cross product
    // is not finite, and it draws nothing) and P's lattice coordinates in cells (the edge band's anti-aliasing: fwidth
    // of the hexagon distance is the active border form's |a . dg|, taken in the branch; H's differ by the parallax's
    // scale, a few per cent).
    float3 nv = cross(ddx(vc), ddy(vc));
    float2 gx = ddx(fp.xy) * lattice.x, gy = ddy(fp.xy) * lattice.x;
    float lift = params.x;
    float3 shade = 0.0;
    [branch] if (z < 1e29 && all(fp >= near_lo.xyz) && all(fp <= near_hi.xyz)) {
    float nn = dot(nv, nv);
    float3 n = nv * rsqrt(clamp(nn, 1e-30, 1e30));
    n = dot(n, ray) > 0.0 ? -n : n; // outward: towards the eye
    float rden = 1.0 / min(dot(n, ray), -1e-6);
    // H1: P's tangent plane offset by d, cut by the ray (at z + d / (n . ray)), less n d (P itself at d = 0); its
    // texel r1 (H1 in front of the eye plane: a hull within d of it has none).
    float t1 = z + lift * rden;
    float3 h1 = ray * t1 - n * lift;
    float3 f1 = face_of(h1);
    float2 r1 = floor(h1.xy / max(h1.z, 1e-6) * raster_law.xy + raster_law.zw);
    [branch] if (nn > 1e-30 && nn < 1e30 && t1 > 0.0 && h1.z > 1e-6 && all(r1 >= 0.0) && all(r1 < lane_sizes.xy) &&
                 all(f1 >= step_lo.xyz) && all(f1 <= step_hi.xyz)) {
    // The parallax step: the hull point Q under H1 (the lane at its texel, on the screen), H on Q's plane (normal n)
    // the same way.
    float z1 = surface_z(r1);
    float3 q = ray_of(r1) * z1;
    float t = (dot(n, q) + lift) * rden;
    float3 f = face_of(ray * t - n * lift); // (u, v, a) of H
    [branch] if (z1 < 1e29 && abs(dot(n, q - vc)) <= lift + 0.05 * lattice.y && t > 0.0 && all(f >= box_lo.xyz) &&
                 all(f <= box_hi.xyz)) {
    float3 no = to_owner(n);
    float3 m = pow(max(abs(no), 1e-6), params.y);
    float3 tw = max(m / (m.x + m.y + m.z) - params.z, 0.0); // the floor: a face past its steep side takes nothing
    float w = dot(n, face[2].xyz) > 0.0 ? dot(tw, abs(anchor[0].xyz)) / max(tw.x + tw.y + tw.z, 1e-6) : 0.0;
    [branch] if (w > 0.0) { // the cell work only where the face takes a share
        float2 k;
        float3 c = hex_cell(f.xy * lattice.x, k);
        // The hexagon distance's border form at the pixel (max of |l.x|, |l.x / 2 +- l.y sqrt 3 / 2|) and its fwidth.
        float2 b0 = float2(1.0, 0.0), b1 = float2(0.5, 0.8660254), b2 = float2(0.5, -0.8660254);
        float d0 = abs(c.x), d1 = abs(dot(c.xy, b1)), d2 = abs(dot(c.xy, b2));
        float2 bf = d0 >= d1 && d0 >= d2 ? b0 : d1 >= d2 ? b1 : b2;
        float fw = max(abs(dot(bf, gx)) + abs(dot(bf, gy)), 1e-5);
        // Keys are integers below 2^24 and unique: 1 - |key - packed| saturates to 1 on the match and 0 elsewhere.
        float packed = (k.x + 2048.0) * 4096.0 + (k.y + 2048.0);
        float4 found = 0.0, clock4 = 0.0;
        [unroll] for (int h = 0; h < 4; ++h) {
            [unroll] for (int half = 0; half < 2; ++half) {
                float2 r = half ? ranges[h].zw : ranges[h].xy;
                [branch] if (packed >= r.x && packed <= r.y) { // the keys ascend: one group holds the pixel's key
                    [unroll] for (int i = 8 * h + 4 * half; i < 8 * h + 4 * half + 4; ++i) {
                        float4 match = saturate(1.0 - abs(keys[i] - packed));
                        found += match;
                        clock4 += match * clocks[i];
                    }
                }
            }
        }
        float clock = dot(clock4, 1.0);
        [branch] if (dot(found, 1.0) > 0.5) {
            float since = abs(clock) > 0.5 ? max(abs(clock) - 1.0, 0.0) : -1.0;
            shade = hex_shade(c.xy, c.z, fw, k, float2(0.5, 0.8660254) * k * lattice.y, since, clock < -0.5, 4, false) * w;
        }
    }
    }
    }
    }
    return float4(max(shade, 0.0), 0.0);
}
