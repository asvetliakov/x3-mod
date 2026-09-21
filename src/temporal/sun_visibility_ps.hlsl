// Partial sun occlusion, step 1 (docs/architecture/sun-partial-occlusion.md, "Visibility pass").
// Original ps_3_0 fragment drawn into a 1x1 A16B16G16R16F target once per frame: the fraction of
// 32 disc taps of the route's scene depth (RT2 .r: device depth, -1 where no routed surface wrote,
// which is where a sun beyond every surface shows) that are open, smoothed against the previous
// 1x1 result (ping-pong; no blending on FP16, no readback).
//
//   s0  RT2 of the frame (R32F / G32R32F / A32B32G32R32F), point / clamp, LOD 0
//   s1  the previous 1x1 result, point / clamp
//   c0  xy the disc centre in back-buffer uv, zw its radius in u and in v
//   c1  x the smoothing weight 1 - exp(-dt / tau), y 1 to seed (no history), z the use exponent
//
//   .r  the smoothed fraction (the history)        .g  the fraction the lens draws multiply by:
//   .b  this frame's raw fraction                       pow(saturate((r - 0.03) / 0.94), c1.z)
//   .a  valid taps / 32
// Taps outside the viewport leave numerator and denominator; with none valid the history stands
// (1 when seeding). The dead band gives an exact 0 behind a full cover and an exact 1 in the open
// although the FP16 lerp stalls one step short, and absorbs one tap of jitter flicker.
sampler2D sceneDepth : register(s0);
sampler2D history : register(s1);
float4 disc : register(c0);
float4 control : register(c1);

#define X3M_SUN_TAP(x, y) float2(x, y),
static const float2 taps[32] = {
#include "../renderer/sun_visibility_taps_inc.h"
};

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float open = 0;
    float valid = 0;
    [unroll] for (int i = 0; i < 32; ++i)
    {
        float2 p = disc.xy + taps[i] * disc.zw;
        float inside = (p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y <= 1) ? 1.0 : 0.0;
        float depth = tex2Dlod(sceneDepth, float4(p, 0, 0)).r;
        valid += inside;
        open += inside * (depth < -0.5 ? 1.0 : 0.0);
    }
    float previous = tex2Dlod(history, float4(0.5, 0.5, 0, 0)).r;
    // Selects, not arithmetic, wherever the history may be unset: a seed never reads it.
    bool seed = control.y > 0.5;
    float raw = valid > 0.5 ? open / max(valid, 1.0) : (seed ? 1.0 : previous);
    float smoothed = saturate(seed ? raw : lerp(previous, raw, control.x));
    float band = saturate((smoothed - 0.03) / 0.94);
    float shaped = band <= 0 ? 0 : pow(band, control.z);
    return float4(smoothed, shaped, raw, valid * (1.0 / 32.0));
}
