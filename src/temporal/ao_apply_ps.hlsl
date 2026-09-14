// Ambient occlusion chain, pass 5: bilateral upsample of the half-resolution
// occlusion term (1 - visibility; 4 term + 4 half-depth taps around the full-resolution pixel, whose
// own device depth is linearized here as the fifth tap) and the multiply
// application factor pow(1 - s (1 - ao), 1 / 2.2), drawn as a ZERO/SRCCOLOR
// blend into the owning scene target (docs/architecture/ambient-occlusion.md,
// section 4). Half texel (i, j) sits at full pixel (2i, 2j), so the full
// pixel's half coordinate is pixel / 2 and the bilinear fractions are 0 or
// 1/2. A sentinel full-resolution pixel and a pixel whose four half taps all
// fail the depth test get factor 1 (no darkening); alpha is 1 so the blend
// leaves the target's alpha unchanged. Compiled into
// src/renderer/ambient_occlusion_apply_program_inc.h.
sampler aoTex : register(s0);
sampler halfDepthTex : register(s1);
sampler fullDepthTex : register(s2);
float4 size : register(c0);     // xy = 1 / half width/height, zw = half width/height
float4 fullSize : register(c1); // xy = full width/height, zw = 1 / full width/height
float4 terms : register(c2);    // x = m22, y = m32, z = tau, w = strength s

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float2 pixel = floor(uv * fullSize.xy);
    float d = tex2D(fullDepthTex, (pixel + 0.5) * fullSize.zw).r;
    if (d < 0.0) return float4(1.0, 1.0, 1.0, 1.0);
    float zf = terms.y / (d - terms.x);
    float2 halfCoord = pixel * 0.5;
    float2 base = floor(halfCoord);
    float2 f = halfCoord - base;
    float sum = 0.0, weightSum = 0.0;
    [unroll] for (int b = 0; b < 2; ++b) {
        [unroll] for (int a = 0; a < 2; ++a) {
            float2 texel = min(base + float2(a, b), size.zw - 1.0);
            float2 tapUV = (texel + 0.5) * size.xy;
            float bilinear = (a ? f.x : 1.0 - f.x) * (b ? f.y : 1.0 - f.y);
            float z = tex2D(halfDepthTex, tapUV).r;
            float w = (z < 0.0) ? 0.0 : bilinear * saturate(1.0 - abs(z - zf) / (terms.z * zf));
            sum += w * tex2D(aoTex, tapUV).r;
            weightSum += w;
        }
    }
    float occlusion = (weightSum > 1e-4) ? sum / weightSum : 0.0;
    float factor = pow(1.0 - terms.w * occlusion, 1.0 / 2.2);
    return float4(factor, factor, factor, 1.0);
}
