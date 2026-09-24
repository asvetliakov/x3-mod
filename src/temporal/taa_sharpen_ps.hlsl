// Post-resolve sharpen of the 8-bit route and of the HDR route's identity
// write-back (docs/architecture/temporal-integration.md, "Post-resolve
// sharpen"). Original ps_3_0 full-screen fragment compiled by
// tools/shaders/generate_rigid_motion_pixel.py --shader taa_sharpen into
// src/renderer/taa_sharpen_program_inc.h.
//
// Contract
//   s0   the resolved FP16 image: display-referred values in [0, 1] (the 8-bit
//        route's gamma-encoded colour as the resolve stored it, or the HDR
//        identity write-back's scene copy), point/clamp sampled at LOD 0
//        through the -0.5 pixel quad; the clamp addressing repeats the edge
//        texel for the cross taps outside the image.
//   c23  sharpenConstants (rcas.hlsl); w is the display dither amplitude
//        (display_dither.hlsl): 1/255 on the HDR identity write-back and the
//        bloom candidate's sharpen with X3M_HDR_DITHER, 0 on the 8-bit route,
//        where the store equals the undithered RCAS mathematically.
//   out  the game's 8-bit main target: RCAS of the five-tap cross, the centre
//        alpha carried unchanged (the copy-back it replaces kept the game's
//        alpha channel intact too).
// The history is never written here: the temporal pass draws this into the
// game's target AFTER the resolve wrote its FP16 history, and the HDR pass
// samples the published history without writing it.
#include "rcas.hlsl"
#include "display_dither.hlsl"
sampler2D resolved : register(s0);

float4 main(float2 uv : TEXCOORD0, float2 vpos : VPOS) : COLOR0
{
    float2 dx = float2(sharpenConstants.y, 0);
    float2 dy = float2(0, sharpenConstants.z);
    float4 e = tex2Dlod(resolved, float4(uv, 0, 0));
    float3 b = tex2Dlod(resolved, float4(uv - dy, 0, 0)).rgb;
    float3 d = tex2Dlod(resolved, float4(uv - dx, 0, 0)).rgb;
    float3 f = tex2Dlod(resolved, float4(uv + dx, 0, 0)).rgb;
    float3 h = tex2Dlod(resolved, float4(uv + dy, 0, 0)).rgb;
    return float4(displayDither(rcas(b, d, e.rgb, f, h), vpos, sharpenConstants.w), e.a);
}
