// Weapon effects, shield hit (docs/architecture/weapon-effects.md 5.1.1 "Placement", 2026-11-12): the lathe bubble's
// pixel program. The patch's cells lie on the owner's profile lathe (weapon_lathe_vs.hlsl rasterizes its band): one
// chart per owner, u the meridian length and v the perimeter fraction x the chart's period (view units), so the cells
// keep their world size along a long hull; the cell lattice, the cell map and its lookup are weapon_bubble_ps.hlsl's
// (keys c26..c57, clocks c58..c89, group ranges c90..c93), the draw's box the cells' chart bounds (c94, c95). Both sides
// of the lathe rasterize (CULLMODE NONE): VFACE x the frame's sign > 0 is the side facing the eye, drawn at the near
// gain, the other the far side seen around the hull's outline at the far gain; a point draws only where it is nearer the
// eye than the lane (the hull and anything in front hide it; the sky is far), the near plane clips. The cell hash takes
// the key's row modulo the chart's period rows (c19.w), so a cell repeated across the seam (the stage duplicates a hit
// near it by one period) keeps its stagger. Alpha 0 under ONE/ONE. Constants: renderer/weapon_effects_pass.cpp
// hex_block, lathe_block and map_block.
#include "weapon_soft_depth.hlsl"
#include "weapon_hex_patch.hlsl"
float4 side : register(c19);       // near gain, far gain, the front sign (+-1), the period's key rows (0: no wrap)
float4 keys[32] : register(c26);   // four packed keys per register (< 0 unused)
float4 clocks[32] : register(c58); // game seconds since lit + 1, negative for a hit's centre cell (0 not lit yet)
float4 ranges[4] : register(c90);  // per group of sixteen keys its key range: first, last x2
float4 box_lo : register(c94);     // the draw's cells' chart bounds: u, v (view units)
float4 box_hi : register(c95);
// The lane's depth at raster r as an occluder: 1e30 on sky; a lane texel without a valid depth occludes everything.
float occluder_z(float2 r) {
    bool occluder;
    float z = lane_view_depth(r, occluder);
    return !occluder ? 1e30 : (z > 0.0 && z <= 3.402823466e38 ? z : 0.0);
}
// The hexagon distance's border forms (|l.x|, |l.x / 2 +- l.y sqrt 3 / 2|): their fwidth for the chart point's
// gradients gx, gy (in cells).
float3 border_fw(float2 gx, float2 gy) {
    float2 b1 = float2(0.5, 0.8660254), b2 = float2(0.5, -0.8660254);
    return float3(abs(gx.x) + abs(gy.x), abs(dot(b1, gx)) + abs(dot(b1, gy)), abs(dot(b2, gx)) + abs(dot(b2, gy)));
}
// One point's cell (f: its chart point, view units; fw3: border_fw of its gradients) at weight w.
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
        // (k.y + 0.5): the quotient of an integer row is never within rounding of an integer, so a seam row (an exact
        // multiple of the period's rows) wraps to 0 like the stage's integer division (FL 70 A review)
        float2 kh = side.w > 0.5 ? float2(k.x, k.y - side.w * floor((k.y + 0.5) / side.w)) : k;
        shade = hex_shade(c.xy, c.z, fw, kh, float2(0.5, 0.8660254) * k * lattice.y, since, clock < -0.5, 4, false) * w;
    }
    return shade;
}
float4 main(float2 vpos : VPOS, float face : VFACE, float4 t : TEXCOORD0) : COLOR0 {
    float2 f = t.xy;
    // The gradients outside the branch (the edge band's anti-aliasing).
    float3 fw = border_fw(ddx(f) * lattice.x, ddy(f) * lattice.x);
    float zl = occluder_z(vpos);
    float gain = face * side.z > 0.0 ? side.x : side.y;
    float3 shade = 0.0;
    [branch] if (t.z < zl && gain > 0.0 && all(f >= box_lo.xy) && all(f <= box_hi.xy) && dot(fw, 1.0) < 1e30)
        shade = cell_shade(f, fw, gain);
    return float4(max(shade, 0.0), 0.0);
}
