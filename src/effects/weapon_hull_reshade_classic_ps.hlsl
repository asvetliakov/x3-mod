// The hull hit re-shade on the standard_lighting(2s) DEFAULT pair (docs/architecture/hull-hit-look.md section 3): an
// impact body with a standard_lighting.fx or classic MATERIAL3 material (the v/10663 sparks, the v/10658 and 10661 glows,
// the v/00587 and 00564 meshes, ...) binds the vertex shader 494fe349b8bc12ec (vs_3_0: the UV after g_TexMatrix in
// TEXCOORD0.xy, sampled at the centroid as vanilla's; the fade g_AlphaValue x the fog factor in COLOR0.a only), so this
// program is ps_3_0, as the muzzle and lone classic programs. The law of weapon_hull_reshade.hlsl in c192..c193 (inside
// the muzzle block c192..c195); the vertex light sum in COLOR0.rgb is not drawn (the LIGHT elements still light the
// hull). The alpha lane is vanilla's (lerp(texel.a, light map.a, g_EnableGlow) x COLOR0.a: s2 and c4 the application's).
#define HH_LOOK c192
#define HH_SHAPE c193
#include "weapon_hull_reshade.hlsl"
sampler2D LightMapTexSampler : register(s2);
float4 g_EnableGlow : register(c4);

float4 main(float4 colour : COLOR0, float2 uv : TEXCOORD0_centroid) : COLOR0 {
    const float4 t = tex2D(DiffuseTexSampler, uv);
    return float4(hull_rgb(t, uv, saturate(colour.a)),
                  lerp(t.a, tex2D(LightMapTexSampler, uv).a, g_EnableGlow.x) * colour.a);
}
