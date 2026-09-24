// Robust contrast-adaptive sharpening (RCAS) of the post-resolve display image
// (docs/architecture/temporal-integration.md, "Post-resolve sharpen"). Our
// HLSL ps_3_0 reimplementation of the algorithm AMD published with
// FidelityFX Super Resolution 1.0 (ffx_fsr1.h, MIT licence); no line of that
// file is copied. The published construction: a five-tap cross, a luma noise
// detector that halves the lobe on pure noise, a per-channel peak-range
// limiter derived from the ring's min/max, one negative lobe applied to the
// ring and normalised. Ours on top: the divisions are guarded (a black or
// white ring gives a zero lobe instead of 0 * inf) and the result is clamped
// to the five taps' own min/max, so the output can never ring past its
// neighbourhood (the fixtures check the 3x3 bound, which contains the cross).
//
// Included textually (`#include "rcas.hlsl"`) by taa_sharpen_ps.hlsl and
// agx_sharpen_ps.hlsl. D3DXCompileShader is given no include handler, so
// tools/shaders/generate_rigid_motion_pixel.py and the temporal fixture
// expand the directive themselves before compiling (the provenance records
// the include's hash).
//
// Space: every tap is a DISPLAY-REFERRED value in [0, 1] - the game's own
// gamma-encoded colour on the 8-bit route and the HDR route's identity
// write-back, the AgX output (display encoded by its outset matrix) on the
// tonemapped route - so the sharpening acts in the space the display shows.
// Every tap is saturated first: a non-finite input becomes the backend's
// saturate() of it (0 for NaN on the verified backend) and can neither
// propagate through the lobe nor widen the limiter.
//
// c23 (x3::temporal::kSharpenRegister; the resolve owns c0..c7 and c22, the
// AgX block c8..c21): x = sharpness gain exp2(-stops) in (0, 1] (1 = the
// strongest published setting, 0 stops), y = 1 / width, z = 1 / height, w = the
// display dither amplitude taa_sharpen_ps.hlsl applies (0 = none; RCAS itself
// never reads it).
float4 sharpenConstants : register(c23);

// Luma times two, the published weights (R/2 + G + B/2).
float rcasLuma(float3 c) { return 0.5 * c.r + c.g + 0.5 * c.b; }

// b, d, e, f, h: the taps above, left of, at, right of and below the pixel.
float3 rcas(float3 b, float3 d, float3 e, float3 f, float3 h)
{
    b = saturate(b); d = saturate(d); e = saturate(e); f = saturate(f); h = saturate(h);
    float bL = rcasLuma(b), dL = rcasLuma(d), eL = rcasLuma(e), fL = rcasLuma(f), hL = rcasLuma(h);
    // Noise detection: the centre's departure from the ring mean in units of
    // the local luma range; a full-range departure halves the lobe.
    float lumaMax = max(max(max(bL, dL), max(eL, fL)), hL);
    float lumaMin = min(min(min(bL, dL), min(eL, fL)), hL);
    float nz = saturate(abs(0.25 * (bL + dL + fL + hL) - eL) / max(lumaMax - lumaMin, 1.0 / 256.0));
    nz = 1.0 - 0.5 * nz;
    // Ring extremes per channel.
    float3 mn4 = min(min(b, d), min(f, h));
    float3 mx4 = max(max(b, d), max(f, h));
    // Peak-range limiter: the most negative lobe at which a centre anywhere in
    // [mn4, mx4] still lands inside [0, 1] (the published derivation, so a
    // ring touching 0 or 1 in a channel gets no sharpening in that channel
    // and the least-permissive channel rules). Guarded denominators.
    float3 hitMin = mn4 / max(4.0 * mx4, 1.0 / 4096.0);
    float3 hitMax = (1.0 - mx4) / min(4.0 * mn4 - 4.0, -1.0 / 4096.0);
    float3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-0.1875, min(max(max(lobeRGB.r, lobeRGB.g), lobeRGB.b), 0.0)) * sharpenConstants.x * nz;
    // Single-lobe resolve: the centre pushed away from the ring mean.
    float3 pix = ((b + d + f + h) * lobe + e) / (4.0 * lobe + 1.0);
    // Ours: never past the taps' own range.
    return clamp(pix, min(mn4, e), max(mx4, e));
}
