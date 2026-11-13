// The bullet re-shade on the lone classic path (docs/reverse-engineering/fl/bullet-draw-batch.md section 7, "Classic
// bodies"; beam-bullet-draw.md section 5): a classic-material (MATERIAL3/5/6) bullet body drawn alone through the material
// submit binds the standard_lighting(2s) DEFAULT pair, whose vertex shader (494fe349b8bc12ec, vs_3_0) passes the UV
// after g_TexMatrix in TEXCOORD0.xy and the node fade (g_AlphaValue x the optional fog factor) in COLOR0.a only; the
// vertex light sum in COLOR0.rgb is not read. ps_3_0: the game's vertex shader is vs_3_0. The batch's law
// (weapon_bullet_reshade.hlsl, c29..c31) with the fade as the vertex alpha, so the lone classic bolt or beam matches the
// batched one (the batch draws these bodies with INSTANCE_BULLETS, COLOR0.a the same fade). The alpha lane is vanilla's
// (lerp(texel.a, light map.a, g_EnableGlow) x COLOR0.a: s2 and c4 the application's), so the game's alpha test and the
// destination alpha see what they saw; the UV is sampled at the centroid as vanilla's (its v1 dcl carries the centroid
// modifier); the frame's colour under the classic ONE / ONE is the re-shade.
#include "weapon_bullet_reshade.hlsl"
sampler2D LightMapTexSampler : register(s2);
float4 g_EnableGlow : register(c4);

float4 main(float4 colour : COLOR0, float2 uv : TEXCOORD0_centroid) : COLOR0 {
    const float4 o = reshade_colour(float4(0.0, 0.0, 0.0, colour.a), uv);
    return float4(o.rgb, lerp(o.a, tex2D(LightMapTexSampler, uv).a, g_EnableGlow.x) * colour.a);
}
