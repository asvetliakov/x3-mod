// Display dither of the final 8-bit write of the HDR route (docs/verification/
// hdr-scene-path.md, "Display dither"). The FP16 image carries sub-code
// gradients (a smooth fogged sky moves about 1/22 of an 8-bit code per pixel);
// storing it into the A8R8G8B8 main target quantises them into contour lines
// that the auto exposure then drags across the screen. Adding a uniform
// offset of +-0.5 code before the store turns the contours into a fine,
// unbiased pattern: the mean presented code equals the unquantised value.
//
// The pattern is interleaved gradient noise (Jimenez 2014) of the pixel
// centre, a few ALU ops and no texture. It is STATIC in screen space: the
// write happens after TAA, so a per-frame varying dither would flicker by one
// code at rest; a fixed pattern keeps a still image still. floor(vpos) makes
// the pixel index independent of whether the backend's VPOS carries the
// +0.5 centre offset. The offset is shared by the three channels.
//
// Included textually by agx.hlsl, agx_sharpen_ps.hlsl (through agx.hlsl),
// taa_sharpen_ps.hlsl, bloom_agx_ps.hlsl (through agx.hlsl) and
// hdr_writeback_dither_ps.hlsl. The CPU twin is tools/analysis/
// agx_reference.py dither_noise()/dither_display().
static const float2 ditherIgnWeights = float2(0.06711056, 0.00583715);
static const float ditherIgnScale = 52.9829189;

// Interleaved gradient noise in [0, 1) of the pixel whose VPOS is given.
float displayDitherNoise(float2 vpos)
{
    return frac(ditherIgnScale * frac(dot(floor(vpos) + 0.5, ditherIgnWeights)));
}

// The display value in [0, 1] plus (noise - 0.5) * amplitude, saturated again:
// the dither is added only inside [0, 1] and the store stays saturated.
// amplitude = 1/255 is +-0.5 code of the 8-bit store; at amplitude 0 the
// store equals the former one mathematically (saturate(saturate(c) + 0) =
// saturate(c)); the dither-off fixture cases reproduce the recorded figures.
float3 displayDither(float3 c, float2 vpos, float amplitude)
{
    return saturate(saturate(c) + (displayDitherNoise(vpos) - 0.5) * amplitude);
}
