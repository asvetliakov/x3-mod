// Weapon effects, flat hex patch (docs/architecture/weapon-effects.md 5.1.1): the hex law of the patch
// (weapon_hex_patch.hlsl) on a camera-facing quad, the plane coordinate q = corner x the quad's half size (c15.z: the
// footprint plus a cell's circumradius) with the one hit at the centre, its cells evaluated here (no cell map),
// and the two-sided fade against the lane so the decal sits on the hull it hit (surface_affinity). Ages are game
// seconds. Alpha 0 under ONE/ONE.
#include "weapon_soft_depth.hlsl"
#include "weapon_hex_patch.hlsl"
float4 main(float4 raster : TEXCOORD0, float4 shade : TEXCOORD1) : COLOR0 {
    float3 c = hex_patch(shade.xy * points[0].z, 1, false);
    float affinity = surface_affinity(raster.xy, raster.z, raster.w);
    return float4(max(c * (affinity * shade.w), 0.0), 0.0);
}
