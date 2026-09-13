// s0 is the fine downsample level (POINT), s1 the coarser reconstruction
// (LINEAR); both clamp/LOD0/sRGB off. c24 describes s1; c25 describes output.
// Coarsest reconstruction aliases its downsample texture (no draw).
#include "bloom_common.hlsl"
sampler2D bloomFine : register(s0);
sampler2D bloomCoarse : register(s1);
float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float3 fine = tex2Dlod(bloomFine, float4(uv, 0, 0)).rgb;
    float3 coarse = bloomTent(bloomCoarse, uv);
    return float4(min(lerp(fine, coarse, bloomFilter.z), bloomRadiance.z), 0);
}
