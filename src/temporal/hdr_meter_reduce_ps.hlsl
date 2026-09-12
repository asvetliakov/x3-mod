// Exposure meter, levels 2..n, of the FP16 HDR scene path
// (docs/architecture/hdr-scene-path.md §3, "Stage 2 implementation").
// Original ps_3_0 fragment: one output texel is the mean of the 4x4 block
// of the previous R32F level beneath it (exposure_reference.reduce_chain,
// factor 4, taps clamped to the source by the CLAMP sampler state). The
// last level is 1x1 and holds avg_log_l.
//
//   s0  the previous chain level (R32F), point/clamp, LOD 0
//   c0  sourceSize  x width, y height, z 1/width, w 1/height of the SOURCE
//   c3  outputSize  x width, y height of THIS level
sampler2D level : register(s0);
float4 sourceSize : register(c0);
float4 outputSize : register(c3);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 base = floor(uv * outputSize.xy) * 4.0;
    float acc = 0;
    [unroll] for (int ty = 0; ty < 4; ++ty)
        [unroll] for (int tx = 0; tx < 4; ++tx)
            acc += tex2Dlod(level, float4((base + float2(tx, ty) + 0.5) * sourceSize.zw, 0, 0)).r;
    return float4(acc * (1.0 / 16.0), 0, 0, 1);
}
