// Ambient occlusion chain, passes 3 and 4: one separable 5-tap depth-aware
// blur of the half-resolution occlusion term (1 - visibility; weights
// 1 4 6 4 1, each tap's weight scaled by saturate(1 - |dz| / (tau * z))), run
// horizontally then vertically (direction in c1). Sentinel centre -> 0
// (no occlusion); sentinel taps have weight 0;
// off-image taps clamp to the border texel. Compiled into
// src/renderer/ambient_occlusion_blur_program_inc.h.
sampler aoTex : register(s0);
sampler depthTex : register(s1);
float4 size : register(c0);      // xy = 1 / half width/height, zw = half width/height
float4 direction : register(c1); // xy = texel step, z = tau (relative depth tolerance)

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float2 pixel = floor(uv * size.zw);
    float2 centreUV = (pixel + 0.5) * size.xy;
    float zc = tex2D(depthTex, centreUV).r;
    if (zc < 0.0) return float4(0.0, 0.0, 0.0, 0.0);
    float sum = 6.0 * tex2D(aoTex, centreUV).r, weightSum = 6.0;
    static const float taps[4] = {-2.0, -1.0, 1.0, 2.0};
    static const float weights[4] = {1.0, 4.0, 4.0, 1.0};
    [unroll] for (int i = 0; i < 4; ++i) {
        float2 tapUV = (clamp(pixel + direction.xy * taps[i], 0.0, size.zw - 1.0) + 0.5) * size.xy;
        float z = tex2D(depthTex, tapUV).r;
        float w = (z < 0.0) ? 0.0 : weights[i] * saturate(1.0 - abs(z - zc) / (direction.z * zc));
        sum += w * tex2D(aoTex, tapUV).r;
        weightSum += w;
    }
    float occlusion = sum / weightSum;
    return occlusion.xxxx;
}
