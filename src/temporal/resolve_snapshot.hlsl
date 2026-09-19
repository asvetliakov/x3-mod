// Reactive-mask snapshot program of TemporalPass (split out of resolve.hlsl,
// docs/architecture/taa-flicker-suppression.md step 0). Reads only s5, c4 and
// c7.z with the resolve's register contract, so the pass uploads the same
// constants. Canonicalizes coverage into owned R32F history; never infers it
// from alpha. c7.z = 1 canonical, 2 expanded by the 3x3 union.
sampler2D currentReactive : register(s5);
float4 sizeJitter : register(c4); // 1/W, 1/H, current jitter UV xy
float4 options : register(c7);    // z: 1 canonical, 2 expanded
float4 fetch(sampler2D s, float2 uv) { return tex2Dlod(s, float4(uv, 0, 0)); }
// Exactly zero is safe. Positive, negative and nonfinite mask values reject
// (>= and <= only: NaN-safe on the verified backend, see resolve.hlsl).
bool maskSafe(float v) { return v >= 0 && v <= 0; }
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    if (options.z > 1.5) {
        // Raw supplemental FP16 coverage also affects neighboring color clip
        // statistics. Store its 3x3 union once in current owned R32F history.
        // This branch never samples a previous (already expanded) mask.
        bool safe = true;
        [loop] for (int y = -1; y <= 1; ++y) {
            [loop] for (int x = -1; x <= 1; ++x)
                safe = maskSafe(fetch(currentReactive, uv + float2(x, y) * sizeJitter.xy).r) && safe;
        }
        return float4(safe ? 0 : 1, 0, 0, 1);
    }
    return float4(maskSafe(fetch(currentReactive, uv).r) ? 0 : 1, 0, 0, 1);
}
