// Diagnostic alternative only: five complete bloom+AgX evaluations then RCAS.
// Not embedded in production. The checker measures its SM3 budget rather than
// assuming repeated color transforms and bloom tents fit portable limits.
#define main bloomComposedTap
#include "../../src/temporal/bloom_agx_ps.hlsl"
#undef main
#include "../../src/temporal/rcas.hlsl"

// The tap takes the pixel's VPOS for the display dither of bloom_agx_ps.hlsl
// (c8.z; the comparison uploads 0, so every tap is undithered).
float4 main(float2 uv : TEXCOORD0, float2 vpos : VPOS) : COLOR0
{
    float2 dx = float2(sharpenConstants.y, 0);
    float2 dy = float2(0, sharpenConstants.z);
    float4 e = bloomComposedTap(uv, vpos);
    return float4(rcas(bloomComposedTap(uv - dy, vpos).rgb,
                       bloomComposedTap(uv - dx, vpos).rgb, e.rgb,
                       bloomComposedTap(uv + dx, vpos).rgb,
                       bloomComposedTap(uv + dy, vpos).rgb), e.a);
}
