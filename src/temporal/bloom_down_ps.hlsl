// Exact area reduction into ceil(source/2). s0 POINT/clamp; no hardware decode.
// The caller supplies the standard -0.5 pixel fullscreen quad, no extra UV
// correction. Exact ceil-half integer geometry avoids deriving large pixel
// boundaries with source*rounded reciprocal then floor (wrong near integers).
#include "bloom_common.hlsl"
sampler2D bloomInput : register(s0);
float3 bloomRead(float2 cell)
{
    float3 v = tex2Dlod(bloomInput, float4((cell + 0.5) * bloomSource.zw, 0, 0)).rgb;
#ifdef BLOOM_EXTRACT
    return bloomPrefilter(v); // nonlinear operations BEFORE spatial averaging
#else
    return v; // already exposed-linear, bounded FP16
#endif
}
float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 cell = min(floor(uv * bloomDestination.xy), bloomDestination.xy - 1);
#ifdef BLOOM_EVEN
    // Valid ONLY when BOTH source dimensions are even. A separate compiled
    // extraction shader avoids five zero-weight texture/decode/threshold taps
    // on the normal even-sized first level; renderer selection is per-frame.
    float2 base = 2 * cell;
    float3 v = bloomRead(base) * 0.25;
    v += bloomRead(base + float2(1, 0)) * 0.25;
    v += bloomRead(base + float2(0, 1)) * 0.25;
    v += bloomRead(base + float2(1, 1)) * 0.25;
#else
    // D=ceil(S/2); parity=2D-S is exactly 0 or 1. At j, take cells
    // [2j-1,2j,2j+1]. Even weights are [0,1/2,1/2]; odd weights are
    // [j,D,D-1-j]/S. Integers <=16384 are exact float32; round only weights.
    // For j=0 the negative-index tap has zero weight and safely clamps.
    float2 base = 2 * cell - 1;
    float2 parity = 2 * bloomDestination.xy - bloomSource.xy;
    float3 ix = base.x + float3(0, 1, 2);
    float3 iy = base.y + float3(0, 1, 2);
    float3 wx = float3(parity.x * cell.x, bloomDestination.x,
        bloomDestination.x - parity.x * (cell.x + 1)) * bloomSource.z;
    float3 wy = float3(parity.y * cell.y, bloomDestination.y,
        bloomDestination.y - parity.y * (cell.y + 1)) * bloomSource.w;
    wx /= dot(wx, float3(1, 1, 1));
    wy /= dot(wy, float3(1, 1, 1));
    float3 v = bloomRead(float2(ix.x, iy.x)) * (wx.x * wy.x);
    v += bloomRead(float2(ix.y, iy.x)) * (wx.y * wy.x);
    v += bloomRead(float2(ix.z, iy.x)) * (wx.z * wy.x);
    v += bloomRead(float2(ix.x, iy.y)) * (wx.x * wy.y);
    v += bloomRead(float2(ix.y, iy.y)) * (wx.y * wy.y);
    v += bloomRead(float2(ix.z, iy.y)) * (wx.z * wy.y);
    v += bloomRead(float2(ix.x, iy.z)) * (wx.x * wy.z);
    v += bloomRead(float2(ix.y, iy.z)) * (wx.y * wy.z);
    v += bloomRead(float2(ix.z, iy.z)) * (wx.z * wy.z);
#endif
    return float4(min(v, bloomRadiance.z), 0); // bloom alpha has no scene meaning
}
