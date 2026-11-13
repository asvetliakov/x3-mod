// The muzzle flash re-shade on the effects DEFAULT pair (X3M_WEAPON_EFFECTS=reshade; docs/architecture/muzzle-flash-look.md
// section "The effects-pair body"): body 12009 (scene 08001, PRG) is an effects.fx body, drawn by the game with the
// effects DEFAULT pair (VS d5e1c75351ed3f04, PS 8360f422de08b5bd), whose vertex shader passes only the UV and the fade
// (oT0.xy, oD0.x), no position. So the re-shade binds our vertex shader beside this program
// (weapon_muzzle_reshade_effects_vs.hlsl: the game VS's oPos, oT0 and oD0 instruction for instruction, plus the model
// position in TEXCOORD1), and this ps_2_0 program in place of the game's, with the game's geometry, texture, sampler,
// blend (ONE / INVSRCCOLOR for 12009) and Z state.
//
// The law is weapon_muzzle_reshade.hlsl's, d in model units (the node's world scale is uniform, so the model distance
// over the lit radius equals the classic route's world distance over the lit radius x the scale):
//   d = |TEXCOORD1| x c29.w, c29.w = 1 / (lit radius x mz_size)
//   rgb = saturate(law) x f, f = COLOR0.x (g_AlphaValue x the fog clip, the game VS's)
// The saturate keeps the screen blend's factor 1 - src in [0, 1] on a float target as a fixed-point target clamps it.
// Alpha is the texel's, as vanilla's. The game program's colour matrix (c0..c2) is not applied.
#define MZ_SHAPE c29 // 1 - smooth, 1 / mean luminance, 0, 1 / (lit radius x size)
#define MZ_GAIN c30  // gain / N, core / N, gain, core
#define MZ_HUE c31   // hue at luminance 1
#include "weapon_muzzle_reshade.hlsl"

float4 main(float4 colour : COLOR0, float2 uv : TEXCOORD0, float3 model : TEXCOORD1) : COLOR0 {
    const float4 t = tex2D(DiffuseTexSampler, uv);
    return float4(saturate(muzzle_rgb(t, model * mz_shape.w)) * saturate(colour.x), t.a);
}
