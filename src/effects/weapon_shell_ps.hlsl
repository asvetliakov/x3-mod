// Weapon effects, shield hit (docs/architecture/weapon-effects.md 5.1.1): the hex patch's pixel program. Each cell is
// its own hexagon (the vertex program), so the pixel's cell, key and clock come from its vertices (no lookup): the
// offset from the cell centre gives the edge band, the key the hash, the clock (game seconds since lit + 1, negative
// for a hit's centre cell, 0 unlit: a ring passing over a cell not reached yet) the fade; the hex law is
// weapon_hex_patch.hlsl's. Both faces draw (a hit on the far side of its owner is seen through the shield). The soft
// fade against the lane at the pixel (VPOS): whole where the lane lies behind the patch, cut once the hull is a soft
// radius in front of it, so a patch hugs the hull it hit and is cut where it passes inside it. Where a draw of
// another of the owner's regions has cells near (the stage's seam neighbours, up to four), the light is shared by the
// power distance to the two draws' cell bounds: this draw keeps saturate(min_j s_j / cell + 1/2), s_j the pixel's
// owner-frame point's signed distance to the radical plane of its own bound and neighbour j's (positive on its own
// side), so the two shares sum to one over a cell-wide band and two lattices never add over one point; within its own
// bound and outside every neighbour's the share is whole. It depends on where the cells are, not on the rule that
// assigned the hits to regions (travel or radial). Alpha 0 under ONE/ONE. Constants: renderer/weapon_effects_pass.cpp hex_block, defaults
// weapon_effects_core.h.
#include "weapon_soft_depth.hlsl"
#include "weapon_hex_patch.hlsl"
float4 anchor[3] : register(c8);  // the region's axis, its u, its v (owner frame)
float4 shell : register(c19);     // the soft radius (view units), 1 / cell, unused x2
float4 seam[4] : register(c20);   // per seam neighbour the radical plane: s = dot(p, xyz) + w (unused: w = 1e6)
float4 main(float4 cell : TEXCOORD0, float4 key : TEXCOORD1, float3 local : TEXCOORD2, float2 vpos : VPOS) : COLOR0 {
    float2 k = floor(key.xy + 0.5);
    float clock = abs(cell.w);
    float since = clock > 0.5 ? max(clock - 1.0, 0.0) : -1.0; // interpolated: a fresh cell (+-1) may read 1 - 1e-7
    float3 c = hex_shade(cell.xy, k, float2(0.5, 0.8660254) * k * lattice.y, since, cell.w < -0.5, 4, false);
    float s = min(min(dot(local, seam[0].xyz) + seam[0].w, dot(local, seam[1].xyz) + seam[1].w),
                  min(dot(local, seam[2].xyz) + seam[2].w, dot(local, seam[3].xyz) + seam[3].w));
    float share = saturate(s * shell.y + 0.5);
    float vis = soft_visibility(vpos, cell.z - shell.x, shell.x);
    return float4(max(c * (share * vis), 0.0), 0.0);
}
