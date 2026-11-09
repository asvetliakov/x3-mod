// Weapon effects, the muzzle flash's hull pulse (docs/architecture/muzzle-flash-look.md section 3): a point light on
// whatever surface the lane holds within the pulse's radius of the muzzle, drawn on the decal vertex program's
// camera-facing octagon (weapon_decal_vs.hlsl; renderer/weapon_effects_pass.cpp run_projectiles, before the bolts). The
// vertex carries the muzzle (view space) and the octagon's half size in its position, the corner, the radius and the
// pulse's gain (its fade) in its shade. Per pixel: the half size from the corner's screen derivative (the corner moves
// 1 / half_px per pixel, half_px = half m00 W / (2 z_0)), the view ray v / z of the raster through the jittered
// projection, the lane's surface point at depth z_s on that ray and the octagon's own point at the muzzle's depth z_0
// (the corner x the half size from the muzzle, on the same ray), hence the surface point's offset from the muzzle,
// (ray (z_s - z_0) + corner x half, z_s - z_0); r its length over the radius: light = colour x gain x (1 - r)^2 inside,
// discarded outside, under 1/256 of scene white (the projectile programs' cut) and where the lane holds no occluder
// (the sky is never written). No normal (the lane carries depth
// only): a glow on the hull around the mount for one or two frames. Alpha 0 under ONE/ONE. Compiled with
// tools/shaders/generate_weapon_effects_programs.py (ps_3_0).
#include "weapon_soft_depth.hlsl"
float4 projection : register(c2); // m00, m11, m20, m21 (the jittered projection the lane was drawn with)
float4 screen : register(c3);     // 2 / W, 2 / H, unused x2
float4 colour : register(c4);     // the run's radiance at the muzzle at full gain (linear rgb), its largest channel
float4 main(float4 raster : TEXCOORD0, float4 shade : TEXCOORD1) : COLOR0 {
    // ddx(raster.x) is 1 (a pixel): the half size in view units from the corner's derivative alone
    float half_size = raster.z * screen.x / (projection.x * ddx(shade.x));
    bool occluder;
    float z = lane_view_depth(raster.xy, occluder);
    clip(occluder && z > 0.0 && z <= 3.402823466e38 ? 1.0 : -1.0);
    float2 ndc = float2(raster.x * screen.x - 1.0, 1.0 - raster.y * screen.y);
    float2 ray = (ndc - projection.zw) / projection.xy;
    float dz = z - raster.z;
    float3 d = float3(ray * dz + shade.xy * half_size, dz);
    float r = length(d) / raster.w;
    float fall = saturate(1.0 - r);
    fall *= fall * shade.w;
    clip(fall * colour.w - 1.0 / 256.0); // outside the radius, or under the projectile's radiance cut: no blend
    return float4(colour.rgb * fall, 0.0);
}
