// AgX tonemap for the FP16 HDR scene path (docs/architecture/hdr-scene-path.md §3).
// Original ps_3_0 full-screen fragment: the stage-2 port of
// tools/analysis/agx_reference.py, compiled by tools/shaders/
// generate_rigid_motion_pixel.py --shader hdr_tonemap into
// src/renderer/hdr_tonemap_program_inc.h and bound by hdr_pass.cpp as the
// write-back program when X3M_HDR_TONEMAP=agx. agx.h names its constant
// registers so the host uploads the very numbers the reference uses.
//
// Contract
//   s0   scene colour: the resolved FP16 image (TAA output) in ENGINE space,
//        sampled point/clamp at LOD 0
//        through the -0.5 pixel quad of resolve.hlsl. The alpha carries through.
//   out  the game's A8R8G8B8 main surface, SRGBWRITEENABLE=FALSE, blending off.
//        oC0.rgb is ALREADY display encoded by the outset matrix; there is no
//        pow(2.2) here (the standard AgX integration bug, §3).
//
// Documented caveat (§2): the game blended in gamma space, so decoding at this
// input is not physically exact; X3M_HDR_DECODE=none exists for the A/B.
//
// Constant layout: c8..c21, chosen away from the resolve's c0..c7 so both
// programs can share one device constant file without clobbering each other.
//   c8   exposure    x exp2(EV_adapted) of frame n-1, y clamp max on the decoded
//                    input (X3M_HDR_CLAMP; the host writes 65504 when off), zw 0
//   c9   decodeMode  x gamma exponent (2.2), y sRGB-piecewise flag, z none flag
//                    (exactly one of {x>0, y, z} in use; the host validates)
//   c10  inset0      rows of M_in  (w unused)
//   c11  inset1
//   c12  inset2
//   c13  outset0     rows of M_out (w unused)
//   c14  outset1
//   c15  outset2
//   c16  logRange    x min_ev, y 1/(max_ev-min_ev), z max_ev, w 0
//   c17  contrastHi  x^6, x^5, x^4, x^3 coefficients of the sigmoid fit
//   c18  contrastLo  x^2, x^1, x^0 coefficients, w 0
//   c19  lookSlope   xyz slope, w saturation           (none/golden/punchy, §3)
//   c20  lookOffset  xyz offset, w 0
//   c21  lookPower   xyz power, w 0
sampler2D sceneColor : register(s0);
float4 exposure   : register(c8);
float4 decodeMode : register(c9);
float4 inset0     : register(c10);
float4 inset1     : register(c11);
float4 inset2     : register(c12);
float4 outset0    : register(c13);
float4 outset1    : register(c14);
float4 outset2    : register(c15);
float4 logRange   : register(c16);
float4 contrastHi : register(c17);
float4 contrastLo : register(c18);
float4 lookSlope  : register(c19);
float4 lookOffset : register(c20);
float4 lookPower  : register(c21);

// Rec.709 luma, the look's saturation pivot. Fixed in the reference too.
static const float3 lumaWeights = float3(0.2126, 0.7152, 0.0722);
// Floor under log2 so black stays finite; the clamp to min_ev follows anyway.
static const float logFloor = 1e-10;
// Floor under the decode's pow (see decodeEngine).
static const float decodeFloor = 1e-10;

// Engine-space code value -> scene-linear (§2). Every branch is evaluated and
// selected by the mode constant: no dynamic flow control for a per-pixel select.
float3 decodeEngine(float3 e)
{
    // The floor keeps pow() (log/exp on SM3) off an exact zero; 1e-10^2.2 is
    // 1e-22, indistinguishable from the reference's max(e, 0)^2.2 = 0. The
    // identity mode returns the raw value, negatives included, exactly as
    // agx_reference.decode('none') does (review 23).
    float3 f = max(e, decodeFloor);
    float3 gamma = pow(f, decodeMode.x);
    float3 srgb = lerp(f / 12.92, pow((f + 0.055) / 1.055, 2.4), step(0.04045, f));
    float3 v = lerp(gamma, srgb, decodeMode.y);
    return lerp(v, e, decodeMode.z);
}

// 3x3 row-major matrix as three dot products (no SM3 matrix registers needed).
float3 mul3(float4 r0, float4 r1, float4 r2, float3 v)
{
    return float3(dot(r0.xyz, v), dot(r1.xyz, v), dot(r2.xyz, v));
}

// Sixth-order sigmoid fit on the 0..1 log encoding (agx_reference.contrast).
float3 contrast(float3 x)
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return contrastHi.x * x4 * x2 + contrastHi.y * x4 * x + contrastHi.z * x4
         + contrastHi.w * x2 * x + contrastLo.x * x2 + contrastLo.y * x + contrastLo.z;
}

// ASC CDL slope/offset/power plus saturation about luma (agx_reference.look).
// The max() keeps pow() off negative bases: contrast(0) is -0.00232.
float3 look(float3 v)
{
    float y = dot(v, lumaWeights);
    float3 graded = pow(max(v * lookSlope.xyz + lookOffset.xyz, 0), lookPower.xyz);
    return y + lookSlope.w * (graded - y);
}

// Tail shared by the ordinary transform and bloom composition. Input has
// already been decoded, firefly-clamped and exposed. Bloom is added in float32
// immediately before this function; never store that sum into FP16 or decode
// it again. Keep expression order identical to the original tonemap.
float4 agxTonemapExposed(float3 v, float alpha)
{
    v = mul3(inset0, inset1, inset2, v); // inset
    v = clamp(log2(max(v, logFloor)), logRange.x, logRange.z);
    v = (v - logRange.x) * logRange.y;   // 0..1 log encoding
    v = contrast(v);
    v = look(v);
    v = mul3(outset0, outset1, outset2, v); // outset: display encoded
    return float4(saturate(v), alpha);
}

// The whole transform of one engine-space sample: display-encoded RGB with
// the alpha carried. main() applies it to the centre sample; agx_sharpen_ps.hlsl
// (AGX_NO_MAIN defined before including this file) applies it to five taps.
float4 agxTonemap(float4 scene)
{
    float3 v = decodeEngine(scene.rgb);
    v = min(v, exposure.y);              // X3M_HDR_CLAMP firefly guard (65504 = off)
    v *= exposure.x;                     // exp2(EV_adapted)
    return agxTonemapExposed(v, scene.a);
}

#ifndef AGX_NO_MAIN
float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    return agxTonemap(tex2Dlod(sceneColor, float4(uv, 0, 0)));
}
#endif
