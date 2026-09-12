// AgX tonemap followed by the post-resolve sharpen: the HDR route's
// tonemapped write-back with X3M_TAA_SHARPEN > 0 (docs/architecture/
// temporal-integration.md, "Post-resolve sharpen"). Compiled by
// tools/shaders/generate_rigid_motion_pixel.py --shader hdr_tonemap_sharpen
// into src/renderer/hdr_tonemap_sharpen_program_inc.h.
//
// The sharpen acts on DISPLAY-REFERRED values: each of the five cross taps of
// the resolved FP16 image (s0, engine space) goes through the full AgX
// transform of agx.hlsl (c8..c21, unchanged) first, and RCAS (rcas.hlsl, c23)
// combines the five tonemapped colours. Sharpening before the tonemap would
// be cheaper (one AgX evaluation) but would sharpen scene radiance, which
// the sigmoid then compresses unevenly; this order is the one the fixture
// verifies against RCAS(AgX(resolved)) per pixel. Alpha is the centre's.
// The history the resolve published is only sampled here, never written.
#define AGX_NO_MAIN
#include "agx.hlsl"
#include "rcas.hlsl"

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 dx = float2(sharpenConstants.y, 0);
    float2 dy = float2(0, sharpenConstants.z);
    float4 e = agxTonemap(tex2Dlod(sceneColor, float4(uv, 0, 0)));
    float3 b = agxTonemap(tex2Dlod(sceneColor, float4(uv - dy, 0, 0))).rgb;
    float3 d = agxTonemap(tex2Dlod(sceneColor, float4(uv - dx, 0, 0))).rgb;
    float3 f = agxTonemap(tex2Dlod(sceneColor, float4(uv + dx, 0, 0))).rgb;
    float3 h = agxTonemap(tex2Dlod(sceneColor, float4(uv + dy, 0, 0))).rgb;
    return float4(rcas(b, d, e.rgb, f, h), e.a);
}
