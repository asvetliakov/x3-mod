// Original D3D9 temporal resolve; see README.md for sampler and coordinate contracts.
sampler2D currentColor : register(s0);
sampler2D currentDepth : register(s1);
sampler2D previousColor : register(s2);
sampler2D previousDepth : register(s3);
sampler2D motionOverride : register(s4);
sampler2D currentReactive : register(s5);
sampler2D previousReactive : register(s6);
float4 reprojection0 : register(c0);
float4 reprojection1 : register(c1);
float4 reprojection2 : register(c2);
float4 reprojection3 : register(c3);
float4 sizeJitter : register(c4); // 1/W, 1/H, current jitter UV xy
// c5.xy carries the previous raster jitter (UV) for ABI compatibility only; the
// resolve never reads it. History is the accumulated output on the UNJITTERED
// pixel grid and output pixel p represents unjittered position p. The jittered
// raster sample at p shows content at p - current jitter (the sub-pixel offset
// is the supersampling); its previous unjittered position q is what the motion
// producer/camera path yields, and history is sampled at q + current jitter
// ("pixel center minus velocity"). For a static scene that is p exactly (f = 0),
// so the output is stable across phases. Adding the previous jitter instead
// moved the taps by the jitter difference every frame: oscillation plus blur.
float4 history : register(c5); // (previous jitter UV xy, unused), weight, valid
float4 rejection : register(c6); // absolute device-depth tolerance, relative tolerance, HDR limit, minimum W
float4 options : register(c7); // motion enabled, reactive enabled, mask snapshot mode, depth-sentinel reactive

bool finiteColor(float3 v) { return all(v == v) && all(abs(v) <= rejection.z); }
bool validDepth(float v) { return v == v && v >= 0 && v <= 1; }
float3 cleanColor(float3 v) { return finiteColor(v) ? v : float3(0, 0, 0); }
// Exactly zero is safe. Positive, negative and nonfinite mask values reject.
// Bounds comparisons avoid depending on a NaN self-comparison surviving compile.
bool maskSafe(float v) { return v >= 0 && v <= 0; }

// Filtering depth across geometry edges is prohibited. Each bilinear color tap
// carries its own point-sampled depth test; rejected taps contribute no energy.
void historyTap(float2 uv, float weight, float expected, inout float3 sum, inout float total,
                inout bool reactive) {
    if (weight > 0 && all(uv >= 0) && all(uv <= 1)) {
        // A contaminated contributor invalidates the entire footprint; do not
        // renormalize around it and blend in a neighboring particle history.
        if (options.y > 0.5 && !maskSafe(tex2D(previousReactive, uv).r)) reactive = true;
        float depth = tex2D(previousDepth, uv).r;
        float3 color = tex2D(previousColor, uv).rgb;
        float tolerance = rejection.x + rejection.y * abs(expected);
        if (validDepth(depth) && abs(depth - expected) <= tolerance && finiteColor(color)) {
            sum += color * weight;
            total += weight;
        }
    }
}
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    // Explicit GPU snapshot mode, used by TemporalPass only after validating s5.
    // Canonicalize coverage into owned R32F history; never infer it from alpha.
    if (options.z > 0.5)
        return float4(maskSafe(tex2D(currentReactive, uv).r) ? 0 : 1, 0, 0, 1);
    float4 current = tex2D(currentColor, uv);
    float3 raw = current.rgb;
    float3 color = cleanColor(raw);
    // The output alpha is the current alpha (the 8-bit main target keeps
    // whatever the game wrote there); history alpha is never blended.
    float alpha = current.a == current.a ? current.a : 1;
    float depth = tex2D(currentDepth, uv).r;
    // Depth-sentinel reactive mode (options.w): a negative current depth marks a
    // pixel no routed opaque draw wrote (background, particles, unknown
    // programs); it is current-only. Sentinel history taps are rejected one by
    // one inside historyTap by validDepth, so a silhouette footprint keeps its
    // surviving opaque taps. No mask texture or snapshot draw is involved.
    if (options.w > 0.5 && depth < 0) return float4(color, alpha);
    if (history.w < 0.5 || history.z <= 0 || !finiteColor(raw) || !validDepth(depth))
        return float4(color, alpha);
    if (options.y > 0.5 && !maskSafe(tex2D(currentReactive, uv).r))
        return float4(color, alpha);

    // Texture centers use (pixel + .5)/size, but the raw D3D9 viewport maps
    // unadjusted projection NDC zero to raster pixel size/2. Remove the texture
    // half-texel before inverting the camera; restore it after prior projection.
    // The current color/depth/motion are the jittered rasterization read at
    // this pixel. The camera reconstruction removes the current jitter to get
    // the content's unjittered position, reprojects it, and restores the same
    // jitter below so a static camera lands on this pixel's own texel center.
    float2 unjittered = uv - 0.5 * sizeJitter.xy - sizeJitter.zw;
    float4 currentClip = float4(unjittered.x * 2 - 1, 1 - unjittered.y * 2, depth, 1);
    float4 previousClip = float4(dot(reprojection0, currentClip), dot(reprojection1, currentClip),
                                 dot(reprojection2, currentClip), dot(reprojection3, currentClip));
    float2 previousUV;
    float expectedDepth;
    bool valid = all(previousClip == previousClip) && all(abs(previousClip) <= 1e20)
                 && previousClip.w > rejection.w;
    previousUV = float2(previousClip.x, -previousClip.y) / max(previousClip.w, rejection.w) * 0.5 + 0.5;
    // Previous unjittered texture-center UV of the content plus the CURRENT
    // jitter (never the previous one): history lives on the unjittered grid.
    previousUV += 0.5 * sizeJitter.xy + sizeJitter.zw;
    expectedDepth = previousClip.z / max(previousClip.w, rejection.w);
    if (options.x > 0.5) {
        float4 motion = tex2D(motionOverride, uv);
        if (motion.w == 1) {
            // RG is the producer's previous unjittered texture-center UV of the
            // content at this jittered sample; add the current jitter only.
            previousUV = motion.xy + sizeJitter.zw;
            expectedDepth = motion.z;
            valid = all(motion == motion) && all(abs(motion) <= 1e20);
        } else if (motion.w != 0) valid = false;
    }
    if (!valid || !validDepth(expectedDepth) || any(previousUV < 0) || any(previousUV > 1))
        return float4(color, alpha);

    float2 position = previousUV / sizeJitter.xy - 0.5;
    float2 base = floor(position);
    float2 f = position - base;
    float2 tap = (base + 0.5) * sizeJitter.xy;
    float3 accumulated = 0;
    float total = 0;
    bool reactive = false;
    historyTap(tap, (1-f.x)*(1-f.y), expectedDepth, accumulated, total, reactive);
    historyTap(tap + float2(sizeJitter.x,0), f.x*(1-f.y), expectedDepth, accumulated, total, reactive);
    historyTap(tap + float2(0,sizeJitter.y), (1-f.x)*f.y, expectedDepth, accumulated, total, reactive);
    historyTap(tap + sizeJitter.xy, f.x*f.y, expectedDepth, accumulated, total, reactive);
    if (reactive || total < 0.001) return float4(color, alpha);

    float3 low = color, high = color;
    // Invalid neighboring values cannot poison the clipping box.
    [unroll] for (int y=-1; y<=1; ++y) {
        [unroll] for (int x=-1; x<=1; ++x) {
            float3 neighbor = tex2D(currentColor, uv + float2(x,y)*sizeJitter.xy).rgb;
            if (finiteColor(neighbor)) { low = min(low, neighbor); high = max(high, neighbor); }
        }
    }
    float3 old = clamp(accumulated / total, low, high);
    return float4(lerp(color, old, history.z), alpha);
}
