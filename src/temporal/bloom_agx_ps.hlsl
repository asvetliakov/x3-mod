// Complete display transform for a pre-original bloom candidate. The retained
// resolved scene is engine-space FP16, never the original-bloomed main image.
//
// s0: scene, POINT/CLAMP, LOD 0; s1: reconstructed U0 bloom, LINEAR/CLAMP, LOD 0.
// All sRGB texture/write conversion is disabled. c8..c21 are existing AgX;
// c24 describes U0; c25 describes the full-size output; c26.w is strength;
// c27.z is the bloom scratch bound. Use the normal writeback's latched exposure
// in c8; candidate preparation must neither meter nor advance TAA history.
//
// No sharpen: write directly into the complete A8R8G8B8 candidate.
// Sharpen: write display RGB once into A16B16G16R16F staging, then run existing
// taa_sharpen_ps.hlsl into the complete A8R8G8B8 candidate. The stage is AFTER
// AgX, never a FP16 store of the exposed scene+bloom sum. This introduces
// measured display quantization, not mathematical equivalence to fused RCAS.
// The later main copy writes RGB only, preserving the original compositor's
// actual destination alpha. This shader carries the retained scene alpha.
// Fullscreen quad uses the existing -0.5 pixel positions and ordinary UVs.
#define AGX_NO_MAIN
#include "agx.hlsl"
#include "bloom_common.hlsl"
sampler2D reconstructedBloom : register(s1);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float4 scene = tex2Dlod(sceneColor, float4(uv, 0, 0));
    // Preserve the base AgX arithmetic, including identity-decode negatives
    // and exposed channels above 65504. bloomExposed() is ONLY for extraction.
    float3 v = decodeEngine(scene.rgb);
    v = min(v, exposure.y);
    v *= exposure.x;
    v = bloomComposite(v, bloomTent(reconstructedBloom, uv));
    return agxTonemapExposed(v, scene.a);
}
