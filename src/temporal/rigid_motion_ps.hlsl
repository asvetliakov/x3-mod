// RGBA32F ABI consumed by resolve.hlsl. Perspective interpolation of previous
// homogeneous clip followed by division yields correspondence at current raster
// samples, even when current clip W varies across the primitive.
// c0.zw: the jitter (UV) the interpolated PREVIOUS rows carried, subtracted so
// the output is the previous UNJITTERED UV the resolve samples verbatim (it adds
// no jitter term). The live route interpolates unjittered shadow rows and
// uploads zero here (c216.zw); pass the actual jitter only for jittered rows.
float4 coordinates : register(c0); // 1/width, 1/height, previous-row jitter UV x/y
float4 mode : register(c1); // x=0 initializes invalid sentinel, x=1 writes motion
float4 main(float4 previous : TEXCOORD0) : COLOR0 {
    if (mode.x < 0.5) return float4(0,0,0,-1);
    bool valid = all(previous == previous) && all(abs(previous) <= 1e20)
                 && previous.w > 0.000001;
    float3 ndc = previous.xyz / max(previous.w, 0.000001);
    if (!valid || !(ndc.z >= 0 && ndc.z <= 1)) return float4(0,0,0,-1);
    // Raw D3D9 raster centers are integer; texture centers carry +0.5 texel.
    // Exclude the rows' jitter from the output: the resolve adds none back.
    float2 uv = float2(ndc.x,-ndc.y)*0.5 + 0.5 + 0.5*coordinates.xy - coordinates.zw;
    return float4(uv, ndc.z, 1);
}
