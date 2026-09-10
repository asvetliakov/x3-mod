// RGBA32F ABI consumed by resolve.hlsl. Perspective interpolation of previous
// homogeneous clip followed by division yields correspondence at current raster
// samples, even when current clip W varies across the primitive.
float4 coordinates : register(c0); // 1/width, 1/height, prior jitter UV x/y
float4 mode : register(c1); // x=0 initializes invalid sentinel, x=1 writes motion
float4 main(float4 previous : TEXCOORD0) : COLOR0 {
    if (mode.x < 0.5) return float4(0,0,0,-1);
    bool valid = all(previous == previous) && all(abs(previous) <= 1e20)
                 && previous.w > 0.000001;
    float3 ndc = previous.xyz / max(previous.w, 0.000001);
    if (!valid || !(ndc.z >= 0 && ndc.z <= 1)) return float4(0,0,0,-1);
    // Raw D3D9 raster centers are integer; texture centers carry +0.5 texel.
    // Exclude previous jitter in the output, then resolve adds it exactly once.
    float2 uv = float2(ndc.x,-ndc.y)*0.5 + 0.5 + 0.5*coordinates.xy - coordinates.zw;
    return float4(uv, ndc.z, 1);
}
