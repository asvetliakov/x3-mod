// Weapon effects, shield hit (docs/architecture/weapon-effects.md 5.1.1): the screen-space hex patch's pixel program.
// The pixel's surface is the lane: its view depth (weapon_soft_depth.hlsl; the sky sentinel or an invalid depth draws
// nothing) rebuilds the view point through the frame's jittered projection (D3D9 pixel centres at integer raster
// coordinates: ndc = 2 r / size - 1), the owner's rows take it to the owner frame (p), and its projection onto this
// draw's face lattice gives the pixel's cell, looked up in the draw's cell map (128 packed keys in ascending order in
// c26..c57, their clocks in c58..c89; unrolled in eight groups of four registers, each behind a branch on its key range
// in c90..c93, so a pixel scans the one group that can hold its key: a rolled loop's keys[k] compiles into a compare
// per register with this d3dx). The box (c94..c95) bounds the cells' surface (the pixel's point must lie in it: the
// rasterized box covers the pixel, not the point) along the face's axis (their anchors -+ the extent); within it every
// cell of the map draws, so a cell's pixels draw or not with the cell, not with which hit lights it. The normal is the cross product of the reconstructed view point's
// screen derivatives (ddx, ddy: no extra lane fetch; a 2x2 quad across a silhouette or a crease mixes two surfaces for
// its edge pixels), in the owner frame, turned towards the eye (the eye's direction where it degenerates). The face's
// share is the triplanar weight w_a = |n_a|^s / sum_k |n_k|^s (s = triplanar_sharpness) less the floor f,
// max(w_a - f, 0) renormalised over the three axes, where the outward normal points along the face's axis a, else 0:
// of each axis one face takes the point, the faces of an owner share it and never add over it, and a face tilted past
// about 55 degrees (w_a < f = 0.3 at s = 8 for any split of the rest) takes nothing, so the depth of a cell's anchor
// hit, which moves with the hits along a steep face, never decides its pixels where they would still show; a pixel
// with no share skips the lookup. The cell's look is weapon_hex_patch.hlsl's (the map's clock: game seconds since lit
// + 1, negative for a hit's centre cell, 0 unlit: a ring passing over a cell not reached yet). Alpha 0 under ONE/ONE.
// Constants: renderer/weapon_effects_pass.cpp hex_block, shell_block and map_block, defaults weapon_effects_core.h.
#include "weapon_soft_depth.hlsl"
#include "weapon_hex_patch.hlsl"
float4 anchor[3] : register(c8);  // the lattice's axis, its u, its v (owner frame)
float4 proj : register(c19);      // m00, m11, m20, m21 of the jittered projection
float4 screen : register(c20);    // 2 / W, 2 / H, unused, the triplanar sharpness
float4 axes[3] : register(c21);   // the owner's local axes in view space
float4 owner_centre : register(c24); // the owner's centre in view space
float4 eye : register(c25);       // the eye in the owner's frame, the triplanar floor
float3 view_point(float2 r, float z) {
    float2 ndc = float2(r.x * screen.x - 1.0, 1.0 - r.y * screen.y);
    return float3((ndc.x - proj.z) / proj.x * z, (ndc.y - proj.w) / proj.y * z, z);
}
float4 keys[32] : register(c26);    // four packed keys per register (< 0 unused; a group led by < 0 is skipped)
float4 clocks[32] : register(c58);  // their game seconds since lit + 1, negative for a hit's centre cell (0 not lit yet)
float4 ranges[4] : register(c90);   // per group of sixteen keys (four registers) its key range: first, last x2
float4 box_lo : register(c94);      // the draw's box in the face frame (u, v, a): the pixel's point must lie inside
float4 box_hi : register(c95);
// The lane's surface depth at raster r; 1e30 off the hull (sky, an invalid depth).
float surface_z(float2 r) {
    bool occluder;
    float z = lane_view_depth(r, occluder);
    return occluder && z > 0.0 && z <= 3.402823466e38 ? z : 1e30;
}
float3 to_owner(float3 d) {
    return float3(dot(d, axes[0].xyz), dot(d, axes[1].xyz), dot(d, axes[2].xyz));
}
float4 main(float2 vpos : VPOS) : COLOR0 {
    float z = surface_z(vpos);
    float3 vc = view_point(vpos, z);
    float3 p = to_owner(vc - owner_centre.xyz);
    float3 f = float3(dot(p, anchor[1].xyz), dot(p, anchor[2].xyz), dot(p, anchor[0].xyz)); // (u, v, a)
    // The gradients outside the branches: the view point's (the normal) and the plane point's in cells (the edge band's
    // anti-aliasing: fwidth of the hexagon distance is the active border form's |a . dg|, taken in the branch).
    float3 nv = cross(ddx(vc), ddy(vc));
    float2 gx = ddx(f.xy) * lattice.x, gy = ddy(f.xy) * lattice.x;
    float3 shade = 0.0;
    [branch] if (z < 1e29 && all(f >= box_lo.xyz) && all(f <= box_hi.xyz)) {
    float3 n = to_owner(nv);
    n = dot(n, n) > 1e-30 && dot(n, n) < 1e30 ? n : eye.xyz - p;
    n = (dot(n, eye.xyz - p) < 0.0 ? -n : n) * rsqrt(max(dot(n, n), 1e-30)); // outward: towards the eye
    float3 m = pow(max(abs(n), 1e-6), screen.w);
    float3 t = max(m / (m.x + m.y + m.z) - eye.w, 0.0); // the floor: a face past its steep side takes nothing
    float w = dot(n, anchor[0].xyz) > 0.0 ? dot(t, abs(anchor[0].xyz)) / max(t.x + t.y + t.z, 1e-6) : 0.0;
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
    return float4(max(shade, 0.0), 0.0);
}
