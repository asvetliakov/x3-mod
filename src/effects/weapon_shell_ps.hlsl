// Weapon effects, shield hit (docs/architecture/weapon-effects.md 5.1.1): the hex patch's pixel program. The lattice
// plane is the stereographic projection of the unit sphere from the anchor's antipode: q = 2 S (p.u, p.v) / (1 + p.a)
// (a the anchor region's direction, u, v its tangent frame, S the ellipsoid's mean axis in view units), conformal (the
// hexagons keep their shape) and nowhere shrinking (scale 2 / (1 + cos theta) >= 1), so a planar distance below the
// radius R is a surface distance below R and the lit area is bounded by R by construction. The hex law
// (weapon_hex_patch.hlsl) gives the radiance; front faces only (the pass's winding cull and the interpolated normal
// facing the eye); the soft fade against the lane where the patch cuts the hull. Alpha 0 under ONE/ONE. Constants:
// renderer/weapon_effects_pass.cpp hex_block, defaults weapon_effects_core.h.
#include "weapon_soft_depth.hlsl"
#include "weapon_hex_patch.hlsl"
float4 anchor[3] : register(c8);  // the anchor direction, its u, its v (owner frame)
float4 main(float4 raster : TEXCOORD0, float3 normal : TEXCOORD1, float3 local : TEXCOORD2, float3 view : TEXCOORD3) : COLOR0 {
    float3 p = normalize(local);
    float facing = dot(normalize(normal), normalize(-view));
    float2 q = lattice.w * float2(dot(p, anchor[1].xyz), dot(p, anchor[2].xyz)) / max(1.0 + dot(p, anchor[0].xyz), 1e-3);
    float3 c = hex_patch(q, 4);
    float vis = soft_visibility(raster.xy, raster.z, raster.w);
    return float4(max(c * (vis * step(0.0, facing)), 0.0), 0.0);
}
