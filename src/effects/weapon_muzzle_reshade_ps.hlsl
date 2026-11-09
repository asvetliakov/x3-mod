// The muzzle flash re-shade (X3M_WEAPON_EFFECTS=reshade, FL; docs/architecture/muzzle-flash-look.md,
// docs/reverse-engineering/fl/muzzle-flash-draw.md). Our pixel program bound in place of the game's
// standard_lighting(2s) DEFAULT pixel shader for the draw of a muzzle body (node +0x144 in muzzle_reshade_core.h's
// list), with the game's vertex shader (494fe349b8bc12ec, vs_3_0), geometry, texture, sampler and ONE / ONE blend.
// ps_3_0: the game's vertex shader is vs_3_0. Inputs are the vanilla program's: COLOR0 (.a the node's fog-scaled
// alpha), TEXCOORD0 (the centroid UV) and TEXCOORD1 (the camera minus the world position); s0 the diffuse atlas, s2 the
// light map and c4 g_EnableGlow (the application's, read for the alpha lane only).
//
// One glow around the gun in world space, the fan quads and the texture only modulating it (FL 64 judgement: a per-quad
// falloff in the cell's UV drew one lobe per quad):
//   d      = |P - origin| / (vanilla's lit radius x scale x mz_size), P - origin = (camera - origin) - TEXCOORD1  (c192)
//   g      = (1 - d^2)^2 for d < 1, else 0
//   t      = lerp(1, texel luminance / the cell's centre luminance, 1 - mz_smooth)                              (c193)
//   rgb    = hue x mz_gain x g x t + lerp(hue, white, 0.5) x mz_core x g^4                                       (c194, c195)
// The hue is at luminance 1, so mz_gain and mz_core are luminances; the core reaches scene white where the quads
// overlap at the gun. Alpha is vanilla's: lerp(texel.a, light.a, g_EnableGlow) x COLOR0.a.
sampler2D DiffuseTexSampler : register(s0);
sampler2D LightMapTexSampler : register(s2);
float4 g_EnableGlow : register(c4);
float4 mz_origin : register(c192); // camera - origin (world), 1 / (lit radius x scale x size)
float4 mz_shape : register(c193);  // 1 - smooth, 1 / mean luminance, size, 0
float4 mz_gain : register(c194);   // gain, core, 0, 0
float4 mz_hue : register(c195);    // hue at luminance 1

float4 main(float4 colour : COLOR0, float2 uv : TEXCOORD0_centroid, float3 view : TEXCOORD1) : COLOR0 {
    const float4 t = tex2D(DiffuseTexSampler, uv);
    const float3 d = (mz_origin.xyz - view) * mz_origin.w;
    float g = 1.0 - saturate(dot(d, d));
    g *= g;
    const float g2 = g * g;
    const float lum = dot(t.rgb, float3(0.2126, 0.7152, 0.0722));
    const float mod = 1.0 + (lum * mz_shape.y - 1.0) * mz_shape.x;
    const float3 rgb = mz_hue.rgb * (mz_gain.x * g * mod) + (0.5 * mz_hue.rgb + 0.5) * (mz_gain.y * g2 * g2);
    const float a = lerp(t.a, tex2D(LightMapTexSampler, uv).a, g_EnableGlow.x) * colour.a;
    return float4(rgb, a);
}
