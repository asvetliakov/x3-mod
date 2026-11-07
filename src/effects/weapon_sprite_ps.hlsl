// Weapon effects, the sprite batch's pixel program (docs/architecture/weapon-look.md 2.3, 2.7, 3.1). Two kinds by the
// colour's alpha, both volumetric against the lane (weapon_soft_depth.hlsl): vis = saturate((z_lane - z) / (SOFT r)),
// the sky never hides, an invalid lane texel hides.
// - A streak (alpha 0, a spark): across its width a (1 - v^2)^2 profile, its ends feathered over a quarter of the
//   quad's half-length, at the interpolated view depth, SOFT = soft_core x its width (a hard test in practice: the
//   lane hides the half of the sparks that flies into the hull).
// - A flash (alpha > 0): a disc core of alpha x the quad's radius (anti-aliased over a quarter of it; white, or
//   lerp(white, the vertex colour, depth.w): the beam spot's weapon-coloured core) and a halo
//   of the vertex colour at halo_max of the core, e-folded within halo_fold core radii and windowed to the quad
//   ((1 - d^2)^2); its depth the sphere's front z - r sqrt(1 - d^2) about the eye-biased centre, SOFT soft_core x the
//   core's radius for the core and soft_halo x r for the halo.
// - A ring (depth.y 2, the heavy shock ring): a band of shade.w of the radius inside the plane square's unit circle,
//   brightest at its leading (outer) edge (anti-aliased over a quarter band), (1 - x)^2 across it, at the plane's
//   interpolated view depth, SOFT = depth.z (view units).
// - A star (depth.y 3, the hull hit's flash since 2026-11-07, hull-burst-look.md section 4): the stage rotated the
//   quad into the star's frame, so (u, v) are along its arms (+u, +v, -u, -v at star_len x A, A = depth.z of the
//   quad's half): each arm (1 - s)^2 along it, across (1 - (d / w)^2)^2 within the half width w = star_width A (1 - s) / 2
//   + depth.w (the pixel floor's half), in the vertex colour; a white disc core of alpha x the quad's half (anti-aliased
//   over a quarter of it); no halo; at the quad's view depth, SOFT = soft_core x its half (view units, shade.w).
// Radiance x the vertex's shade.z. Alpha 0 under ONE/ONE. Constants: renderer/weapon_effects_pass.cpp, defaults
// weapon_effects_core.h look::.
#include "weapon_soft_depth.hlsl"
float4 sprite_look : register(c2); // soft_core, soft_halo, halo_max, halo_fold
float4 star_len : register(c3);    // the star's arm lengths over A: +u, +v, -u, -v
float4 star_shape : register(c4);  // star_width, unused x3
float arm(float along, float across, float len, float A, float floor_w) {
    float s = along / len;
    float w = 0.5 * star_shape.x * A * saturate(1.0 - s) + floor_w;
    float x = saturate(1.0 - (across * across) / (w * w));
    float f = saturate(1.0 - s);
    return f * f * x * x;
}
float4 main(float2 vpos : VPOS, float4 shade : TEXCOORD0, float4 colour : TEXCOORD1, float4 depth : TEXCOORD2) : COLOR0 {
    bool occluder;
    float lane = lane_view_depth(vpos, occluder);
    bool valid = !occluder || (lane > 0.0 && lane <= 3.402823466e38);
    float z = depth.x;
    float3 c = 0.0;
    [branch] if (depth.y > 2.5) {
        float2 q = shade.xy;
        float A = depth.z;
        float lu = (q.x >= 0.0 ? star_len.x : star_len.z) * A, lv = (q.y >= 0.0 ? star_len.y : star_len.w) * A;
        float arms = max(arm(abs(q.x), abs(q.y), lu, A, depth.w), arm(abs(q.y), abs(q.x), lv, A, depth.w));
        float core = colour.a;
        float disc = saturate((core - sqrt(dot(q, q))) / max(0.25 * core, 1e-3));
        float vis = occluder ? saturate((lane - z) / max(sprite_look.x * shade.w, 1e-4)) : 1.0;
        c = (disc + colour.rgb * arms) * (shade.z * vis);
    } else if (depth.y > 1.5) {
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
        float3 core_c = lerp(float3(1.0, 1.0, 1.0), colour.rgb, depth.w);
        c = (core_c * (disc * vis_core) + colour.rgb * (halo * vis_halo)) * shade.z;
    }
    return float4(valid ? max(c, 0.0) : 0.0, 0.0);
}
