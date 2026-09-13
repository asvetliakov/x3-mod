// Shared original SM3 bloom numerics. All samplers: clamp, LOD 0, sRGB off.
// c24..c28 are BloomConstants (bloom.h). Full precision: never mark these _pp.
#ifndef X3_BLOOM_COMMON
#define X3_BLOOM_COMMON
float4 bloomSource : register(c24);
float4 bloomDestination : register(c25);
float4 bloomFilter : register(c26);
float4 bloomRadiance : register(c27);
float4 bloomDecode : register(c28);

// FP16 scene storage bounds finite inputs. Comparisons explicitly reject NaN
// and negatives; positive infinity clamps. Do not multiply an invalid value
// by zero as a sanitizer. pow intermediates remain below float32 overflow.
float3 bloomSafeEngine(float3 e)
{
    return float3(e.r >= 0 ? min(e.r, 65504.0) : 0,
                  e.g >= 0 ? min(e.g, 65504.0) : 0,
                  e.b >= 0 ? min(e.b, 65504.0) : 0);
}
float3 bloomExposed(float3 engine)
{
    float3 e = bloomSafeEngine(engine);
    float3 f = max(e, 1e-10);
    // The same extended gamma2.2 / piecewise sRGB / identity definitions as
    // agx.hlsl, with exact zero restored before the black-preserving filter.
    // Extraction wrappers specialize decode at compile time: six variants
    // (three modes x generic/even geometry), no unused powers or dynamic
    // branches. Runtime branches exceeded SM3's portable 512-slot budget in
    // generic extraction. Unused helper code disappears from down/up shaders.
#if BLOOM_DECODE_MODE == 2
    float3 decoded = e;
#elif BLOOM_DECODE_MODE == 1
    float3 decoded = lerp(f / 12.92, pow((f + 0.055) / 1.055, 2.4), step(0.04045, f));
#else
    float3 decoded = pow(f, bloomDecode.x);
#endif
    decoded = float3(e.r > 0 ? decoded.r : 0, e.g > 0 ? decoded.g : 0, e.b > 0 ? decoded.b : 0);
    // Match decoded-space firefly clamp; then bound the exposed value before
    // any FP16 store. Max product 65504^2 is safe in float32 registers.
    return min(min(decoded, bloomRadiance.y) * bloomRadiance.x, bloomRadiance.z);
}
float3 bloomPrefilter(float3 engine)
{
    float3 e = bloomExposed(engine);
    float y = dot(e, float3(0.2126, 0.7152, 0.0722));
    float t = bloomFilter.x;
    float k = t * bloomFilter.y;
    // q^2/(4k), written q*(q/(4k)): no large square, and k=0 is defined.
    float q = clamp(y - t + k, 0, 2 * k);
    float soft = q * (q / max(4 * k, 1e-10));
    float contribution = max(y - t, soft);
    // At threshold zero y/y gives full contribution; black remains zero.
    return e * saturate(contribution / max(y, 1e-10));
}
// Exact four-fetch form of the nine-bilinear-sample separable [1 2 1]/4
// tent. At texel phase f, each axis has four discrete weights
// [1-f, 2-f, 1+f, f]/4. Merge adjacent pairs into two linear samples with
// pair weights (3-2f)/4 and (1+2f)/4. Both denominators are >=1. This works
// at arbitrary UVs/odd sizes, including clamp edges; fixed +/-0.5 offsets
// would only equal the original tent at selected phases.
float3 bloomTent(sampler2D image, float2 uv)
{
    float2 position = uv * bloomSource.xy - 0.5;
    float2 base = floor(position);
    float2 f = position - base;
    float2 leftDenom = 3 - 2 * f;
    float2 rightDenom = 1 + 2 * f;
    float2 left = (base - 0.5 + (2 - f) / leftDenom) * bloomSource.zw;
    float2 right = (base + 1.5 + f / rightDenom) * bloomSource.zw;
    float2 lw = leftDenom * 0.25;
    float2 rw = rightDenom * 0.25;
    float3 v = tex2Dlod(image, float4(left, 0, 0)).rgb * (lw.x * lw.y);
    v += tex2Dlod(image, float4(right.x, left.y, 0, 0)).rgb * (rw.x * lw.y);
    v += tex2Dlod(image, float4(left.x, right.y, 0, 0)).rgb * (lw.x * rw.y);
    v += tex2Dlod(image, float4(right, 0, 0)).rgb * (rw.x * rw.y);
    return min(v, bloomRadiance.z);
}
// Final composition is float32 immediately before AgX's inset, with NO FP16
// store, decode, exposure or firefly clamp after this addition. Alpha must be
// taken separately from the original point-sampled scene. exposedScene is the
// existing AgX decode/clamp/exposure result, NOT bloomExposed(engine): its
// float32 highlights and identity-decode negatives must remain unchanged,
// including strength zero. The caller owns its existing input contract.
float3 bloomComposite(float3 exposedScene, float3 reconstructedBloom)
{
    return exposedScene + bloomFilter.w * reconstructedBloom;
}
#endif
