// Ambient occlusion chain, pass 3 (step 1b): one 2D depth-aware blur of the
// half-resolution occlusion term (1 - visibility), replacing the two separable
// 5-tap passes of step 1 (one render pass instead of two for the same 18
// fetches). The kernel is a sparse 5x5 quincunx, centre weight 4, the four
// diagonals (+-1, +-1) weight 2 and the four axial taps (+-2, 0), (0, +-2)
// weight 1: support +-2 in both axes as the separable 1 4 6 4 1 pair had, so
// the 4x4 Bayer noise of the horizon search still averages out, at 9 texels
// instead of 25.
// Depth is the half-resolution linear target of ao_linearize_ps.hlsl
// (sampler 1, -1 sentinel); each tap's weight is scaled by
// saturate(1 - |dz| / (tau * z)), which is invariant under that target's
// |m32| scale. Sentinel centre -> 0 (no occlusion); sentinel taps have weight
// 0; off-image taps clamp to the border texel. Compiled into
// src/renderer/ambient_occlusion_blur_program_inc.h.
sampler aoTex : register(s0);
sampler depthTex : register(s1); // half-resolution R32F linear depth
float4 size : register(c0);      // xy = 1 / half width/height, zw = half width/height
float4 terms : register(c5);     // y = tau (relative depth tolerance)
float4 halfUV : register(c7);    // xy = 0.5 / half width/height, zw = half width/height - 1

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float2 pixel = floor(uv * size.zw);
    float2 centreUV = pixel * size.xy + halfUV.xy;
    float zc = tex2D(depthTex, centreUV).r;
    if (zc < 0.0) return float4(0.0, 0.0, 0.0, 0.0);
    float sum = 4.0 * tex2D(aoTex, centreUV).r, weightSum = 4.0;
    static const float2 taps[8] = {float2(-1.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0), float2(1.0, 1.0),
                                   float2(-2.0, 0.0), float2(2.0, 0.0), float2(0.0, -2.0), float2(0.0, 2.0)};
    static const float weights[8] = {2.0, 2.0, 2.0, 2.0, 1.0, 1.0, 1.0, 1.0};
    [unroll] for (int i = 0; i < 8; ++i) {
        float2 tapUV = clamp(pixel + taps[i], 0.0, halfUV.zw) * size.xy + halfUV.xy;
        float z = tex2D(depthTex, tapUV).r;
        float w = (z < 0.0) ? 0.0 : weights[i] * saturate(1.0 - abs(z - zc) / (terms.y * zc));
        sum += w * tex2D(aoTex, tapUV).r;
        weightSum += w;
    }
    float occlusion = sum / weightSum;
    return occlusion.xxxx;
}
