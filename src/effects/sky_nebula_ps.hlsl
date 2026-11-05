// Sky nebula deband (docs/architecture/sector-background-modern.md, Decision C1): the pixel program that replaces the
// engine's SM1.1 program 6109cf64c03529dd (gui2d.fb) on the four nebula part draws. The engine's output is the sampled
// RGB and the texture alpha times g_AlphaValue (c0.x; an SM1 constant is clamped to [-1, 1], kept here); the draws blend
// SRCALPHA/ONE into the FP16 target. The RGB is debanded in texture space over the note's tap set: two rings of four
// bilinear taps, ring 1 at radius R / 2 texels along d and its perpendicular, ring 2 at radius R along d rotated by 45
// degrees (an eight-point star, symmetric under a quarter turn). d's angle is c200.w (sky_nebula_deband_hash, default
// 0: a fixed star, d = (1, 0)) times a quarter turn times a texel-indexed hash, interpolated bilinearly between the four
// nearest texel centres: static in texture space (no per-frame term, no screen position) and continuous in uv; it
// rotates the star per texel, which trades the fixed star's residual for texel-scale grain (fixture: ripple 1.98/255
// with it, 0.78 without). Gradient-aware weight: diff = the largest channel difference between the midpoint of
// an opposite tap pair and the centre sample c, over the four pairs (0 on any linear gradient, about half the height
// across an edge); w = saturate(2 (on - diff / threshold)): full weight up to half the threshold, none at it. Output
// lerp(c, m, w), m the alpha-weighted mean of the centre and the eight taps (the centre when every alpha is 0). on = 0
// (c200.x) is the identity: w = 0 exactly, the
// centre sample unchanged. Constants: c200 = (on, 1 / threshold, R, hash) with threshold in value units
// (sky_nebula_deband_threshold / 255), c201 = (width, height, 1 / width, 1 / height) of the bound part's level 0
// (src/proxy/sky_nebula_core.h pixel_constants). The stage-0 sampler state is the engine's. 9 taps. Compiled with
// tools/shaders/generate_sky_nebula_programs.py (ps_3_0).
sampler2D part : register(s0);
float4 alpha : register(c0);
float4 deband : register(c200);
float4 size : register(c201);
// Texel-indexed hash in [0, 1) (Hoskins' hash12, multiplies and frac only: fp32-exact enough below 2^12 texels).
float hash(float2 p) {
    float3 p3 = frac(p.xyx * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float4 centre = tex2D(part, uv);
    float3 c = centre.rgb;
    float2 p = uv * size.xy - 0.5;
    float2 t = floor(p);
    float2 f = p - t;
    float h = lerp(lerp(hash(t), hash(t + float2(1, 0)), f.x), lerp(hash(t + float2(0, 1)), hash(t + 1), f.x), f.y);
    float2 d;
    sincos(1.57079633 * h * deband.w, d.y, d.x);
    float2 d1 = d * (deband.z * 0.5) * size.zw, p1 = float2(-d.y, d.x) * (deband.z * 0.5) * size.zw;
    float2 e = float2(d.x - d.y, d.x + d.y) * 0.70710678; // d rotated by 45 degrees
    float2 d2 = e * deband.z * size.zw, p2 = float2(-e.y, e.x) * deband.z * size.zw;
    float4 a0 = tex2D(part, uv + d1), a1 = tex2D(part, uv - d1), a2 = tex2D(part, uv + p1), a3 = tex2D(part, uv - p1);
    float4 b0 = tex2D(part, uv + d2), b1 = tex2D(part, uv - d2), b2 = tex2D(part, uv + p2), b3 = tex2D(part, uv - p2);
    float3 diff = max(max(abs((a0.rgb + a1.rgb) * 0.5 - c), abs((a2.rgb + a3.rgb) * 0.5 - c)),
                      max(abs((b0.rgb + b1.rgb) * 0.5 - c), abs((b2.rgb + b3.rgb) * 0.5 - c)));
    float w = saturate(2 * (deband.x - max(diff.r, max(diff.g, diff.b)) * deband.y));
    // The mean weighted by each sample's alpha (AP's DXT5 and uncompressed parts: a transparent texel's colour never
    // bleeds into the average); the centre when every weight is 0. DXT1 alpha is 1: the plain mean of nine samples.
    float sa = centre.a + a0.a + a1.a + a2.a + a3.a + b0.a + b1.a + b2.a + b3.a;
    float3 sc = c * centre.a + a0.rgb * a0.a + a1.rgb * a1.a + a2.rgb * a2.a + a3.rgb * a3.a + b0.rgb * b0.a +
                b1.rgb * b1.a + b2.rgb * b2.a + b3.rgb * b3.a;
    float3 m = sa > 0 ? sc / sa : c;
    return float4(lerp(c, m, w), centre.a * clamp(alpha.x, -1, 1));
}
