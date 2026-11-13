// The muzzle flash re-shade (X3M_WEAPON_EFFECTS=reshade, FL; docs/architecture/muzzle-flash-look.md,
// docs/reverse-engineering/fl/muzzle-flash-draw.md). Our pixel program bound in place of the game's
// standard_lighting(2s) DEFAULT pixel shader for the draw of a classic muzzle body (node +0x144 in
// muzzle_reshade_core.h's list), with the game's vertex shader (494fe349b8bc12ec, vs_3_0), geometry, texture, sampler
// and ONE / ONE blend. ps_3_0: the game's vertex shader is vs_3_0. Inputs are the vanilla program's: COLOR0 (.a the
// node's fog-scaled alpha), TEXCOORD0 (the centroid UV) and TEXCOORD1 (the camera minus the world position); s0 the
// diffuse atlas, s2 the light map and c4 g_EnableGlow (the application's, read for the alpha lane only).
//
// The law is weapon_muzzle_reshade.hlsl's, d in world space:
//   d = |P - origin| / (vanilla's lit radius x scale x mz_size), P - origin = (camera - origin) - TEXCOORD1   (c192)
// Alpha is vanilla's: lerp(texel.a, light.a, g_EnableGlow) x COLOR0.a.
sampler2D LightMapTexSampler : register(s2);
float4 g_EnableGlow : register(c4);
float4 mz_origin : register(c192); // camera - origin (world), 1 / (lit radius x scale x size)
#define MZ_SHAPE c193 // 1 - smooth, 1 / mean luminance, size, 0
#define MZ_GAIN c194  // gain / N, core / N, gain, core
#define MZ_HUE c195   // hue at luminance 1
#include "weapon_muzzle_reshade.hlsl"

float4 main(float4 colour : COLOR0, float2 uv : TEXCOORD0_centroid, float3 view : TEXCOORD1) : COLOR0 {
    const float4 t = tex2D(DiffuseTexSampler, uv);
    const float3 rgb = muzzle_rgb(t, (mz_origin.xyz - view) * mz_origin.w);
    const float a = lerp(t.a, tex2D(LightMapTexSampler, uv).a, g_EnableGlow.x) * colour.a;
    return float4(rgb, a);
}
