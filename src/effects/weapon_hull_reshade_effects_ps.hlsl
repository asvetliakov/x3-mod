// The hull hit re-shade on the effects / effects2s DEFAULT pair (docs/architecture/hull-hit-look.md section 3): an impact
// body with an effects.fx material (the v/12000 flipbooks, v/10659, v/12007) binds the vertex shader d5e1c75351ed3f04
// (vs_2_0: the UV after g_TexMatrix in TEXCOORD0.xy, the fade g_AlphaValue x the fog clip in COLOR0.x), so this
// program is ps_2_0, as the lone bullet program. The law of weapon_hull_reshade.hlsl in c30..c31 (inside the lone bullet
// block c29..c31); alpha is the texel's, as vanilla's. The game program's colour matrix (c0..c2) is not applied.
#define HH_LOOK c30
#define HH_SHAPE c31
#include "weapon_hull_reshade.hlsl"

float4 main(float4 colour : COLOR0, float2 uv : TEXCOORD0) : COLOR0 {
    const float4 t = tex2D(DiffuseTexSampler, uv);
    return float4(hull_rgb(t, uv, saturate(colour.x)), t.a);
}
