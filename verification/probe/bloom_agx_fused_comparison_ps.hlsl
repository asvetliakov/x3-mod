// Diagnostic alternative only: five complete bloom+AgX evaluations then RCAS.
// Not embedded in production. The checker measures its SM3 budget rather than
// assuming repeated color transforms and bloom tents fit portable limits.
#define main bloomComposedTap
#include "../../src/temporal/bloom_agx_ps.hlsl"
#undef main
#include "../../src/temporal/rcas.hlsl"

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 dx = float2(sharpenConstants.y, 0);
    float2 dy = float2(0, sharpenConstants.z);
    float4 e = bloomComposedTap(uv);
    return float4(rcas(bloomComposedTap(uv - dy).rgb,
                       bloomComposedTap(uv - dx).rgb, e.rgb,
                       bloomComposedTap(uv + dx).rgb,
                       bloomComposedTap(uv + dy).rgb), e.a);
}
