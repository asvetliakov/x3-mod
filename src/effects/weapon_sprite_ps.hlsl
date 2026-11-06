// Weapon effects, the sprite batch's pixel program (docs/architecture/weapon-look.md 2.3, 2.7, 3.1). Two kinds by the
// colour's alpha, both volumetric against the lane (weapon_soft_depth.hlsl): vis = saturate((z_lane - z) / (SOFT r)),
// the sky never hides, an invalid lane texel hides.
// - A streak (alpha 0, a spark): across its width a (1 - v^2)^2 profile, its ends feathered over a quarter of the
//   quad's half-length, at the interpolated view depth, SOFT = soft_core x its width (a hard test in practice: the
//   lane hides the half of the sparks that flies into the hull).
// - A flash (alpha > 0): a white disc core of alpha x the quad's radius (anti-aliased over a quarter of it) and a halo
//   of the vertex colour at halo_max of the core, e-folded within halo_fold core radii and windowed to the quad
//   ((1 - d^2)^2); its depth the sphere's front z - r sqrt(1 - d^2) about the eye-biased centre, SOFT soft_core x the
//   core's radius for the core and soft_halo x r for the halo.
// - A ring (depth.y 2, the heavy shock ring): a band of shade.w of the radius inside the plane square's unit circle,
//   brightest at its leading (outer) edge (anti-aliased over a quarter band), (1 - x)^2 across it, at the plane's
//   interpolated view depth, SOFT = depth.z (view units).
// Radiance x the vertex's shade.z. Alpha 0 under ONE/ONE. Constants: renderer/weapon_effects_pass.cpp, defaults
// weapon_effects_core.h look::.
#include "weapon_soft_depth.hlsl"
float4 sprite_look : register(c2); // soft_core, soft_halo, halo_max, halo_fold
float4 main(float2 vpos : VPOS, float4 shade : TEXCOORD0, float4 colour : TEXCOORD1, float4 depth : TEXCOORD2) : COLOR0 {
    bool occluder;
    float lane = lane_view_depth(vpos, occluder);
    bool valid = !occluder || (lane > 0.0 && lane <= 3.402823466e38);
    float z = depth.x;
    float3 c = 0.0;
    [branch] if (depth.y > 1.5) {
        float d = sqrt(dot(shade.xy, shade.xy));
        float band = max(shade.w, 1e-3);
        float x = (1.0 - d) / band;
        float edge = saturate((1.0 - d) * 4.0 / band);
        float inner = saturate(1.0 - x);
        float vis = occluder ? saturate((lane - z) / max(depth.z, 1e-4)) : 1.0;
        c = colour.rgb * (shade.z * edge * inner * inner * vis);
    } else if (colour.a < 0.5 / 255.0) {
        float across = saturate(1.0 - shade.y * shade.y);
        float along = saturate((1.0 - abs(shade.x)) * 4.0);
        float vis = occluder ? saturate((lane - z) / max(sprite_look.x * shade.w, 1e-4)) : 1.0;
        c = colour.rgb * (shade.z * across * across * along * vis);
    } else {
        float d2 = dot(shade.xy, shade.xy);
        float d = sqrt(d2);
        float core = colour.a;
        float zf = z - shade.w * sqrt(saturate(1.0 - d2));
        float vis_core = occluder ? saturate((lane - zf) / max(sprite_look.x * core * shade.w, 1e-4)) : 1.0;
        float vis_halo = occluder ? saturate((lane - zf) / max(sprite_look.y * shade.w, 1e-4)) : 1.0;
        float disc = saturate((core - d) / max(0.25 * core, 1e-3));
        float w = saturate(1.0 - d2);
        float halo = sprite_look.z * exp(-max(d - core, 0.0) / max(sprite_look.w * core, 1e-3)) * w * w;
        c = (disc * vis_core + colour.rgb * (halo * vis_halo)) * shade.z;
    }
    return float4(valid ? max(c, 0.0) : 0.0, 0.0);
}
