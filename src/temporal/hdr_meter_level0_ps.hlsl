// Exposure meter, level 0 + first reduction, of the FP16 HDR scene path
// (docs/architecture/hdr-scene-path.md §3, "Stage 2 implementation").
// Original ps_3_0 fragment. One output texel of the first two-channel chain
// level (G32R32F, or A32B32G32R32F where that is not a render target) holds
// the mean and the maximum of the log2 luminance of the 4x4 block of scene
// pixels beneath it:
//
//   L     = dot(decode(scene.rgb), (0.2126, 0.7152, 0.0722))
//   v     = log2(clamp(L, meterFloor, meterClip))
//   out.r = mean over the 16 taps of v,  out.g = max over the 16 taps of v
//
// which is exposure_reference.meter_level0 followed by one step of
// reduce_chain (factor 4, taps clamped to the source through the CLAMP
// sampler state, so odd sizes weight the edge texels exactly as the
// reference does). The full-resolution level-0 image is never stored:
// folding it into the first reduction saves a write and read per pixel. The
// chain stops at the tile image (no axis above 128 texels), which the host
// reads back and reduces to the space-aware statistic (exposure.h).
//
// Contract
//   s0   the FP16 scene target, point/clamp, LOD 0; sampled at texel centres
//        of the SOURCE level (uv = (texel + 0.5) / sourceSize), so the
//        interpolated TEXCOORD0 only selects the output texel.
//   out  .r = the block mean of the log2 luminance, .g = its block maximum.
//
// Constants c0..c3 (the tonemap owns c8..c21; both are saved and restored
// around the write-back):
//   c0  sourceSize  x width, y height, z 1/width, w 1/height of the SOURCE
//   c1  decodeMode  x gamma exponent, y sRGB flag, z none flag (agx.hlsl c9)
//   c2  meter       x meterFloor (1e-4), y meterClip (64), zw 0
//   c3  outputSize  x width, y height of THIS level, zw 0
sampler2D sceneColor : register(s0);
float4 sourceSize : register(c0);
float4 decodeMode : register(c1);
float4 meter      : register(c2);
float4 outputSize : register(c3);

static const float3 lumaWeights = float3(0.2126, 0.7152, 0.0722);
static const float decodeFloor = 1e-10;

// Engine-space code value -> scene-linear, identical to agx.hlsl decodeEngine.
float3 decodeEngine(float3 e)
{
    float3 f = max(e, decodeFloor);
    float3 gamma = pow(f, decodeMode.x);
    float3 srgb = lerp(f / 12.92, pow((f + 0.055) / 1.055, 2.4), step(0.04045, f));
    float3 v = lerp(gamma, srgb, decodeMode.y);
    return lerp(v, e, decodeMode.z); // the identity mode keeps the raw value (negatives included)
}

float tap(float2 texel)
{
    float4 s = tex2Dlod(sceneColor, float4((texel + 0.5) * sourceSize.zw, 0, 0));
    float L = dot(decodeEngine(s.rgb), lumaWeights);
    return log2(clamp(L, meter.x, meter.y));
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    // Output texel (x, y): uv * outputSize lands on x + 0.5, y + 0.5.
    float2 base = floor(uv * outputSize.xy) * 4.0;
    float acc = 0;
    float peak = log2(meter.x);   // every tap is at least the floor
    [unroll] for (int ty = 0; ty < 4; ++ty)
        [unroll] for (int tx = 0; tx < 4; ++tx)
        {
            float v = tap(base + float2(tx, ty));
            acc += v;
            peak = max(peak, v);
        }
    return float4(acc * (1.0 / 16.0), peak, 0, 1);
}
