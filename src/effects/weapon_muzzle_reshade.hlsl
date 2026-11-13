// The muzzle flash re-shade's law (included by weapon_muzzle_reshade_ps.hlsl, ps_3_0, beside the game's
// standard_lighting DEFAULT vs_3_0, and weapon_muzzle_reshade_effects_ps.hlsl, ps_2_0, beside our
// weapon_muzzle_reshade_effects_vs.hlsl; X3M_WEAPON_EFFECTS=reshade; docs/architecture/muzzle-flash-look.md). One glow
// around the gun, the fan quads and the texture only modulating it (FL 64 judgement: a per-quad falloff in the cell's
// UV drew one lobe per quad):
//   d      = |P - origin| / (vanilla's lit radius x mz_size), in model units (the wrapper forms d)
//   g      = (1 - d^2)^2 for d < 1, else 0
//   t      = lerp(1, texel luminance / mean, 1 - mz_smooth)                                              (MZ_SHAPE.xy)
//            mean: the body's texel reference (muzzle_reshade_core.h bodies[]: 445 its cell's centre luminance, the
//            others their glow-weighted texel luminance); each body's overlap N divides gain and core (MZ_GAIN.xy)
//   rgb    = hue x mz_gain x g x t + lerp(hue, white, 0.5) x mz_core x g^4                               (MZ_GAIN, MZ_HUE)
// The hue is at luminance 1, so mz_gain and mz_core are luminances; the core reaches scene white where the quads
// overlap at the gun. MZ_SHAPE = (1 - smooth, 1 / mean, ., .), MZ_GAIN = (gain / N, core / N, ., .), MZ_HUE = (hue, .);
// the wrapper defines the three registers (c193..c195 on ps_3_0, c29..c31 on ps_2_0), written per substituted draw and
// put back after it.
sampler2D DiffuseTexSampler : register(s0);
float4 mz_shape : register(MZ_SHAPE);
float4 mz_gain : register(MZ_GAIN);
float4 mz_hue : register(MZ_HUE);

float3 muzzle_rgb(float4 t, float3 d) {
    float g = 1.0 - saturate(dot(d, d));
    g *= g;
    const float g2 = g * g;
    const float lum = dot(t.rgb, float3(0.2126, 0.7152, 0.0722));
    const float mod = 1.0 + (lum * mz_shape.y - 1.0) * mz_shape.x;
    return mz_hue.rgb * (mz_gain.x * g * mod) + (0.5 * mz_hue.rgb + 0.5) * (mz_gain.y * g2 * g2);
}
