// Original D3D9 temporal resolve; see README.md for sampler and coordinate contracts.
sampler2D currentColor : register(s0);
sampler2D currentDepth : register(s1);
sampler2D previousColor : register(s2);
sampler2D previousDepth : register(s3);
sampler2D motionOverride : register(s4);
float4 reprojection0 : register(c0);
float4 reprojection1 : register(c1);
float4 reprojection2 : register(c2);
float4 reprojection3 : register(c3);
float4 sizeJitter : register(c4); // 1/W, 1/H, current jitter UV xy
float4 history : register(c5); // previous jitter UV xy, weight, valid
float4 rejection : register(c6); // absolute device-depth tolerance, relative tolerance, HDR limit, minimum W
float4 options : register(c7); // motion texture enabled; remaining components reserved

bool finiteColor(float3 v) { return all(v == v) && all(abs(v) <= rejection.z); }
bool validDepth(float v) { return v == v && v >= 0 && v <= 1; }
float3 cleanColor(float3 v) { return finiteColor(v) ? v : float3(0, 0, 0); }

// Filtering depth across geometry edges is prohibited. Each bilinear color tap
// carries its own point-sampled depth test; rejected taps contribute no energy.
void historyTap(float2 uv, float weight, float expected, inout float3 sum, inout float total) {
    if (weight > 0 && all(uv >= 0) && all(uv <= 1)) {
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
    float3 raw = tex2D(currentColor, uv).rgb;
    float3 color = cleanColor(raw);
    float depth = tex2D(currentDepth, uv).r;
    if (history.w < 0.5 || history.z <= 0 || !finiteColor(raw) || !validDepth(depth))
        return float4(color, 1);

    // Texture centers use (pixel + .5)/size, but the raw D3D9 viewport maps
    // unadjusted projection NDC zero to raster pixel size/2. Remove the texture
    // half-texel before inverting the camera; restore it after prior projection.
    float2 unjittered = uv - 0.5 * sizeJitter.xy - sizeJitter.zw;
    float4 currentClip = float4(unjittered.x * 2 - 1, 1 - unjittered.y * 2, depth, 1);
    float4 previousClip = float4(dot(reprojection0, currentClip), dot(reprojection1, currentClip),
                                 dot(reprojection2, currentClip), dot(reprojection3, currentClip));
    float2 previousUV;
    float expectedDepth;
    bool valid = all(previousClip == previousClip) && all(abs(previousClip) <= 1e20)
                 && previousClip.w > rejection.w;
    previousUV = float2(previousClip.x, -previousClip.y) / max(previousClip.w, rejection.w) * 0.5 + 0.5;
    previousUV += 0.5 * sizeJitter.xy + history.xy;
    expectedDepth = previousClip.z / max(previousClip.w, rejection.w);
    if (options.x > 0.5) {
        float4 motion = tex2D(motionOverride, uv);
        if (motion.w == 1) {
            previousUV = motion.xy + history.xy;
            expectedDepth = motion.z;
            valid = all(motion == motion) && all(abs(motion) <= 1e20);
        } else if (motion.w != 0) valid = false;
    }
    if (!valid || !validDepth(expectedDepth) || any(previousUV < 0) || any(previousUV > 1))
        return float4(color, 1);

    float2 position = previousUV / sizeJitter.xy - 0.5;
    float2 base = floor(position);
    float2 f = position - base;
    float2 tap = (base + 0.5) * sizeJitter.xy;
    float3 accumulated = 0;
    float total = 0;
    historyTap(tap, (1-f.x)*(1-f.y), expectedDepth, accumulated, total);
    historyTap(tap + float2(sizeJitter.x,0), f.x*(1-f.y), expectedDepth, accumulated, total);
    historyTap(tap + float2(0,sizeJitter.y), (1-f.x)*f.y, expectedDepth, accumulated, total);
    historyTap(tap + sizeJitter.xy, f.x*f.y, expectedDepth, accumulated, total);
    if (total < 0.001) return float4(color, 1);

    float3 low = color, high = color;
    // Invalid neighboring values cannot poison the clipping box.
    [unroll] for (int y=-1; y<=1; ++y) {
        [unroll] for (int x=-1; x<=1; ++x) {
            float3 neighbor = tex2D(currentColor, uv + float2(x,y)*sizeJitter.xy).rgb;
            if (finiteColor(neighbor)) { low = min(low, neighbor); high = max(high, neighbor); }
        }
    }
    float3 old = clamp(accumulated / total, low, high);
    return float4(lerp(color, old, history.z), 1);
}
