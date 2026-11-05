// Weapon effects, shield hit (docs/architecture/weapon-effects.md 5.1.1): the hex patch's pixel program. The lattice
// plane is the stereographic projection of the unit sphere from the anchor's antipode: q = 2 S (p.u, p.v) / (1 + p.a)
// (a the anchor region's direction, u, v its tangent frame, S the ellipsoid's mean axis in view units), conformal (the
// hexagons keep their shape) and nowhere shrinking (scale 2 / (1 + cos theta) >= 1), so a planar distance is an upper
// bound of the surface distance and the stage's cap bounds every lit cell. The hex law (weapon_hex_patch.hlsl, with
// the draw's cell map) gives the radiance; front faces only (the pass's winding cull and the interpolated normal
// facing the eye); the soft fade against the lane where the patch cuts the hull. At a seam with another of the owner's
// regions drawing there (the stage's seams), the light is shared: this draw keeps saturate((s_own - s_other) k + 1/2)
// of it, s the pixel's score dot(p, axis) of each region and s_other the best of the seam regions, k = S / (sqrt 2 cell)
// (one cell wide), so two lattices across a seam never add over one point (their shares sum to one; up to 3/2 within
// a cell of a cube corner where three regions meet). Alpha 0 under ONE/ONE. Constants: renderer/weapon_effects_pass.cpp
// hex_block, defaults weapon_effects_core.h.
#include "weapon_soft_depth.hlsl"
#include "weapon_hex_patch.hlsl"
float4 anchor[3] : register(c8);  // the anchor direction, its u, its v (owner frame)
float4 projection : register(c19); // m00, m11, m20, m21 of the projection the vertex program placed the cap with
float4 inverse[3] : register(c20); // the rows of A^-1: view -> the owner's unit sphere
float4 origin : register(c23);     // A^-1 (eye - centre): the eye in the owner's unit-sphere frame
// The seam regions' scores (owner frame): max(p M+ + B+, p M- + B-) is p_i (+axis i a seam), -p_i (-axis i) or -2 (no
// seam); seam[0].w = k.
float4 seam[4] : register(c24);
// The pixel's point on the owner's unit sphere, exactly: the eye ray through the pixel's window position (the vertex
// program's own projection, inverted) intersected with the ellipsoid in its unit-sphere frame (the near root, through
// the ray's closest approach so a far owner keeps its precision). The interpolated cap point would follow the facets
// (window-exact positions interpolate affinely), so a cell border pixel could change cells when the cap changes; the
// window position is snapped to the pixel's integer position (as soft_visibility reads the lane), so the triangle that
// interpolates it does not move a pixel lying on a cell border across it.
float3 sphere_point(float2 raster) {
    float2 at = floor(raster + 0.5);
    float2 ndc = float2(at.x * 2.0 * lane_sizes.z - 1.0, 1.0 - at.y * 2.0 * lane_sizes.w);
    float3 ray = float3((ndc.x - projection.z) / projection.x, (ndc.y - projection.w) / projection.y, 1.0);
    float3 d = normalize(float3(dot(inverse[0].xyz, ray), dot(inverse[1].xyz, ray), dot(inverse[2].xyz, ray)));
    float3 perp = origin.xyz - dot(origin.xyz, d) * d;
    return normalize(perp - sqrt(saturate(1.0 - dot(perp, perp))) * d);
}
float4 main(float4 raster : TEXCOORD0, float3 normal : TEXCOORD1, float3 view : TEXCOORD3) : COLOR0 {
    float3 p = sphere_point(raster.xy);
    float facing = dot(normalize(normal), normalize(-view));
    float2 q = lattice.w * float2(dot(p, anchor[1].xyz), dot(p, anchor[2].xyz)) / max(1.0 + dot(p, anchor[0].xyz), 1e-3);
    float3 o = max(p * seam[0].xyz + seam[1].xyz, p * seam[2].xyz + seam[3].xyz);
    float share = saturate((dot(p, anchor[0].xyz) - max(max(o.x, o.y), o.z)) * seam[0].w + 0.5);
    float3 c = hex_patch(q, 4, true) * share;
    float vis = soft_visibility(raster.xy, raster.z, raster.w);
    return float4(max(c * (vis * step(0.0, facing)), 0.0), 0.0);
}
