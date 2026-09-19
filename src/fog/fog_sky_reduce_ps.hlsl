// Volumetric sun fog, sky hue reduction, level 1: the 8x8 level to the 1x1
// history under SRCALPHA / INVSRCALPHA blending. rgb = the mean linear sky
// colour (sum rgb / sum coverage), a = the blend weight the pass uploads (1 on
// the first update after creation or Reset), or 0 when under 2 % of the
// samples were sky (the history is kept). Compiled by
// tools/shaders/generate_rigid_motion_pixel.py into
// src/renderer/fog_sky_reduce_program_inc.h.
sampler levelTex : register(s0);
float4 terms : register(c0); // x = blend weight

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float4 sum = 0.0;
    [loop] for (int b = 0; b < 8; ++b) {
        [loop] for (int a = 0; a < 8; ++a)
            sum += tex2Dlod(levelTex, float4((float2(a, b) + 0.5) / 8.0, 0.0, 0.0));
    }
    float covered = sum.a / 64.0;
    float3 mean = sum.a > 0.0 ? sum.rgb / sum.a : float3(0.0, 0.0, 0.0);
    return float4(mean, covered > 0.02 ? terms.x : 0.0);
}
