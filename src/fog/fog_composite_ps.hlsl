// Volumetric sun fog, quad 2 (docs/architecture/volumetric-fog.md, section 3 and
// "Stage 1 implementation"): the full-resolution composite, exact in linear
// light. The scene target is engine-space (decode exponent 2.2, or 1 when the
// HDR path decodes nothing), so the quad reads a copy of it, decodes, applies
//   L <- L Tb + hue E_sun p_HG(cos) F (1 - Tb),  Tb = exp(-tau_max (1 - exp(-d / R)))
// and encodes; alpha is carried. d is the pixel's distance along its view ray
// (RT2 as fog_march_ps.hlsl; a sentinel pixel has Tb = exp(-tau_max)). F is the
// half-resolution lit fraction, upsampled here by four taps weighted bilinear x
// (exp(-|dz| / (0.05 min z)) + 1e-4) against the view depth of the even full
// pixel each half texel was marched for (read from RT2 directly; no
// half-resolution depth target exists). hue = the smoothed mean sky colour
// (1x1, linear) divided by its luma and clamped to [0, 4]; white while no sky
// has been seen. E_sun arrives as linear RGB from the tracked sun light.
// Bound: the added radiance is at most hue E_sun p_HG(1) (1 - exp(-tau_max)).
// tools/analysis/fog_offline_mock.py (upsample(), the tau loop) is the law.
// Compiled by tools/shaders/generate_rigid_motion_pixel.py into
// src/renderer/fog_composite_program_inc.h.
sampler sceneTex : register(s0);      // copy of the owning FP16 scene target
sampler litTex : register(s1);        // half-resolution lit fraction
sampler depthShareTex : register(s2); // the route's RT2
sampler skyTex : register(s3);        // 1x1 smoothed mean sky colour, linear
float4 view : register(c0);        // m00, m11, m20, m21
float4 depthTerms : register(c1);  // m22, m32, 1 when .b carries the view depth, decode exponent
float4 medium : register(c2);      // tau_max, R, g, 1 / decode exponent
float4 halfSize : register(c3);    // half width, half height, 1 / half width, 1 / half height
float4 fullSize : register(c4);    // W, H, 1 / W, 1 / H
float4 sunView : register(c5);     // unit view-space direction toward the sun
float4 sunRadiance : register(c6); // linear E_sun RGB

static const float3 LUMA = float3(0.2126, 0.7152, 0.0722);

float viewDepth(float4 ds) {
    bool geometry = ds.r >= 0.0 && ds.r <= 1.0;
    float z = (depthTerms.z > 0.0 && ds.b > 0.0) ? ds.b : depthTerms.y / (ds.r - depthTerms.x);
    return geometry ? z : 1e9;
}

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float2 pixel = floor(uv * fullSize.xy);
    float2 puv = (pixel + 0.5) * fullSize.zw;
    float4 scene = tex2D(sceneTex, puv);
    float zf = viewDepth(tex2D(depthShareTex, puv));
    float2 ndc = float2(puv.x * 2.0 - 1.0, 1.0 - puv.y * 2.0);
    float3 ray = float3((ndc.x - view.z) / view.x, (ndc.y - view.w) / view.y, 1.0);
    float rayLength = length(ray);
    float falloff = zf < 1e8 ? exp(-zf * rayLength / medium.y) : 0.0;
    float transmittance = exp(-medium.x * (1.0 - falloff));
    // Depth-aware upsample of F.
    float2 halfCoord = (pixel - 0.5) * 0.5;
    float2 base = floor(halfCoord);
    float2 f = halfCoord - base;
    float sum = 0.0, weightSum = 0.0;
    [unroll] for (int b = 0; b < 2; ++b) {
        [unroll] for (int a = 0; a < 2; ++a) {
            float2 tapPixel = clamp(base + float2(a, b), 0.0, halfSize.xy - 1.0);
            float bilinear = (a ? f.x : 1.0 - f.x) * (b ? f.y : 1.0 - f.y);
            float zh = viewDepth(tex2D(depthShareTex, (tapPixel * 2.0 + 0.5) * fullSize.zw));
            float w = bilinear * (exp(-abs(zh - zf) / (0.05 * min(zf, zh))) + 1e-4);
            sum += w * tex2D(litTex, (tapPixel + 0.5) * halfSize.zw).r;
            weightSum += w;
        }
    }
    float lit = sum / weightSum;
    float g = medium.z;
    float cosine = dot(ray / rayLength, sunView.xyz);
    float phase = (1.0 - g * g) / (12.5663706 * pow(max(1.0 + g * g - 2.0 * g * cosine, 1e-6), 1.5));
    float3 sky = tex2D(skyTex, float2(0.5, 0.5)).rgb;
    float skyLuma = dot(sky, LUMA);
    float3 hue = skyLuma > 1e-6 ? clamp(sky / skyLuma, 0.0, 4.0) : float3(1.0, 1.0, 1.0);
    float3 inscatter = hue * sunRadiance.rgb * (phase * lit * (1.0 - transmittance));
    float3 engine = clamp(scene.rgb, 0.0, 65504.0);
    float3 linearScene = float3(engine.r > 0.0 ? pow(engine.r, depthTerms.w) : 0.0,
                                engine.g > 0.0 ? pow(engine.g, depthTerms.w) : 0.0,
                                engine.b > 0.0 ? pow(engine.b, depthTerms.w) : 0.0);
    float3 fogged = linearScene * transmittance + inscatter;
    float3 encoded = float3(fogged.r > 0.0 ? pow(fogged.r, medium.w) : 0.0,
                            fogged.g > 0.0 ? pow(fogged.g, medium.w) : 0.0,
                            fogged.b > 0.0 ? pow(fogged.b, medium.w) : 0.0);
    return float4(encoded, scene.a);
}
