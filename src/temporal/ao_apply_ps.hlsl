// Ambient occlusion chain, pass 4: bilateral upsample of the half-resolution
// occlusion term (1 - visibility; 4 term + 4 half-depth taps around the
// full-resolution pixel, whose own device depth is linearized here as the
// fifth tap) and the multiply application factor pow(1 - s (1 - ao), 1 / 2.2),
// drawn as a ZERO/SRCCOLOR blend into the owning scene target
// (docs/architecture/ambient-occlusion.md, section 4). Half texel (i, j) sits
// at full pixel (2i, 2j), so the full pixel's half coordinate is pixel / 2 and
// the bilinear fractions are 0 or 1/2. The pixel's own depth is linearized
// here to the same scale-free zs = 1 / (m22 - d) the half-resolution target
// holds (ao_linearize_ps.hlsl), so the relative depth test matches. A sentinel
// full-resolution pixel and a pixel whose four half taps all fail the depth
// test get factor 1 (no darkening); alpha is 1 so the blend leaves the
// target's alpha unchanged. Compiled into
// src/renderer/ambient_occlusion_apply_program_inc.h.
sampler aoTex : register(s0);
sampler halfDepthTex : register(s1); // half-resolution R32F linear depth
sampler fullDepthTex : register(s2); // full-resolution R32F device depth
float4 size : register(c0);     // xy = 1 / half width/height, zw = half width/height
float4 terms : register(c5);    // x = m22, y = tau, z = strength s
float4 fullSize : register(c6); // xy = full width/height, zw = 1 / full width/height
float4 halfUV : register(c7);   // xy = 0.5 / half width/height, zw = half width/height - 1

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float2 pixel = floor(uv * fullSize.xy);
    float d = tex2D(fullDepthTex, (pixel + 0.5) * fullSize.zw).r;
    if (d < 0.0) return float4(1.0, 1.0, 1.0, 1.0);
    float zf = 1.0 / (terms.x - d);
    float2 halfCoord = pixel * 0.5;
    float2 base = floor(halfCoord);
    float2 f = halfCoord - base;
    float sum = 0.0, weightSum = 0.0;
    [unroll] for (int b = 0; b < 2; ++b) {
        [unroll] for (int a = 0; a < 2; ++a) {
            float2 tapUV = min(base + float2(a, b), halfUV.zw) * size.xy + halfUV.xy;
            float bilinear = (a ? f.x : 1.0 - f.x) * (b ? f.y : 1.0 - f.y);
            float z = tex2D(halfDepthTex, tapUV).r;
            float w = (z < 0.0) ? 0.0 : bilinear * saturate(1.0 - abs(z - zf) / (terms.y * zf));
            sum += w * tex2D(aoTex, tapUV).r;
            weightSum += w;
        }
    }
    float occlusion = (weightSum > 1e-4) ? sum / weightSum : 0.0;
    float factor = pow(1.0 - terms.z * occlusion, 1.0 / 2.2);
    return float4(factor, factor, factor, 1.0);
}
