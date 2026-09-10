// D3D9/WineD3D fallback for native D24X8 RESZ snapshots that expose only shadow
// comparison sampling. s0 must be a point-filtered, clamp-addressed depth texture;
// no mipmaps or sRGB. Decode at source dimensions into R32F, with pixel-center UVs.
// This uses 24 dependent comparisons plus two endpoint checks per output pixel.
// Exact 0/1 sentinels are preserved without classifying nearby geometry as sky.
sampler2D DepthSnapshot : register(s0);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float lower = 0.0;
    float upper = 1.0;
    [unroll]
    for (int bit = 0; bit < 24; ++bit)
    {
        float reference = (lower + upper) * 0.5;
        // tex2Dproj preserves the comparison-reference coordinate. A float2
        // tex2D lookup supplies no reference and cannot recover raw D24X8 here.
        float above = tex2Dproj(DepthSnapshot, float4(uv, reference, 1.0)).r;
        lower = lerp(lower, reference, above);
        upper = lerp(reference, upper, above);
    }
    float atFarEndpoint = tex2Dproj(DepthSnapshot, float4(uv, 1.0, 1.0)).r;
    float nonzero = tex2Dproj(DepthSnapshot, float4(uv, 0.5 / 16777215.0, 1.0)).r;
    // IEEE float32 addition can round an interior midpoint up to 1. Keep the
    // search result strictly interior; only the explicit comparison may emit 1.
    float interior = clamp((lower + upper) * 0.5,
                           0.5 / 16777216.0, 1.0 - 1.0 / 16777216.0);
    float decoded = lerp(interior, 1.0, atFarEndpoint);
    return float4(decoded * nonzero, 0.0, 0.0, 1.0);
}
