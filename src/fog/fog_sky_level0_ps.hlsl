// Volumetric sun fog, sky hue reduction, level 0 (docs/architecture/volumetric-fog.md,
// "Stage 1 implementation"): an 8x8 target; every texel takes a 12x12 grid of
// point samples over its cell of the scene copy, keeps the sentinel (sky)
// samples (RT2.r < 0), decodes them to linear (clamped at 4) and writes
// rgb = sum / 144, a = sky sample count / 144. 9,216 samples of the frame; the
// 1x1 level smooths the sparse estimate over time. Compiled by
// tools/shaders/generate_rigid_motion_pixel.py into
// src/renderer/fog_sky_level0_program_inc.h.
sampler sceneTex : register(s0);
sampler depthShareTex : register(s1);
float4 terms : register(c0); // x = decode exponent

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float2 cell = floor(uv * 8.0);
    float4 sum = 0.0;
    [loop] for (int b = 0; b < 12; ++b) {
        [loop] for (int a = 0; a < 12; ++a) {
            float4 tapUV = float4((cell * 12.0 + float2(a, b) + 0.5) / 96.0, 0.0, 0.0);
            float mask = tex2Dlod(depthShareTex, tapUV).r < 0.0 ? 1.0 : 0.0;
            float3 engine = clamp(tex2Dlod(sceneTex, tapUV).rgb, 0.0, 65504.0);
            float3 lin = float3(engine.r > 0.0 ? pow(engine.r, terms.x) : 0.0,
                                engine.g > 0.0 ? pow(engine.g, terms.x) : 0.0,
                                engine.b > 0.0 ? pow(engine.b, terms.x) : 0.0);
            sum += float4(min(lin, 4.0), 1.0) * mask;
        }
    }
    return sum / 144.0;
}
