// Effects stage, projectiles (docs/architecture/effects-modernisation-opus.md 3.1): the capsule pixel program. The
// signed distance to the capsule segment [-L_back, L_front] x radius r gives a white-hot core with a one-pixel analytic
// edge and a halo exp(-d / sigma) tinted by the bullet atlas at the instance's UV centroid (tex2Dlod at mip 4 so mod
// bolt colours carry over), both above 1.0 in engine units and faded softly where the bolt ends in a hull (the lane at
// s0). Alpha 0: the scene's alpha is untouched under ONE/ONE.
#include "effects_soft_depth.hlsl"
sampler2D atlas_sampler : register(s1);
float4 intensity : register(c2); // I_core, I_halo, halo sigma (fraction of R - r), tint floor
float4 main(float4 raster : TEXCOORD0, float4 capsule : TEXCOORD1, float4 shade : TEXCOORD2, float2 uv : TEXCOORD3) : COLOR0 {
    float u = clamp(raster.z, -capsule.x, capsule.y);
    float d = length(float2(raster.z - u, raster.w));
    float core = saturate(capsule.z + 0.5 - d);
    float halo = exp(-max(d - capsule.z, 0.0) / max((capsule.w - capsule.z) * intensity.z, 0.5));
    float3 tint = tex2Dlod(atlas_sampler, float4(uv, 0, 4)).rgb;
    tint = tint / max(max(tint.r, tint.g), max(tint.b, intensity.w));
    float vis = soft_visibility(raster.xy, shade.x, shade.z);
    float3 colour = (intensity.x * core + intensity.y * halo * tint) * shade.y * vis;
    return float4(max(colour, 0.0), 0.0);
}
