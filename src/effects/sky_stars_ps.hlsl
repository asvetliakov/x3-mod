// Sky stars (docs/architecture/sector-background-modern.md, Implementation A): the pixel program of the star batch.
// The pixel's share of the star: the separable Beta(3,3) kernel over [-a, a] integrated over the pixel's own square,
// (C(t(u + 0.5)) - C(t(u - 0.5))) per axis with C the quintic smootherstep and t(x) = x / (2a) + 0.5, so the shares of
// the covered pixels sum to exactly 1 at any sub-pixel position (src/proxy/sky_stars_core.h). RGB only; the batch is
// drawn ONE/ONE with COLORWRITEENABLE 7. Compiled with tools/shaders/generate_sky_stars_programs.py (ps_3_0).
// SKY_STARS_DIM (src/effects/sky_stars_dim_ps.hlsl, Decision C2): the deferred batch's twin multiplies the share by
// 1 - k saturate(L / L0), L the pre-decode luma (0.299, 0.587, 0.114) of the mean of four bilinear taps of the 1/8-size
// copy of the HDR target (s0) one dim texel apart around the star's centre. Without the define the program is the
// in-place batch's, unchanged.
#ifdef SKY_STARS_DIM
sampler2D nebula : register(s0);
#endif
struct Input {
    float4 kernel : TEXCOORD0; // offset px (x, y), 1/(2a)
    float4 colour : TEXCOORD1; // energy per channel
#ifdef SKY_STARS_DIM
    float4 dim_uv : TEXCOORD2;
    float2 dim_law : TEXCOORD3;
#endif
};
float4 main(Input i) : COLOR0 {
    float4 t = saturate(float4(i.kernel.x + 0.5, i.kernel.x - 0.5, i.kernel.y + 0.5, i.kernel.y - 0.5) * i.kernel.z + 0.5);
    float4 c = t * t * t * (t * (t * 6 - 15) + 10);
#ifdef SKY_STARS_DIM
    float3 n = (tex2D(nebula, i.dim_uv.xy).rgb + tex2D(nebula, i.dim_uv.zy).rgb + tex2D(nebula, i.dim_uv.xw).rgb +
                tex2D(nebula, i.dim_uv.zw).rgb) * 0.25;
    float factor = 1 - i.dim_law.x * saturate(dot(n, float3(0.299, 0.587, 0.114)) * i.dim_law.y);
    return float4(i.colour.rgb * ((c.x - c.y) * (c.z - c.w) * factor), 0);
#else
    return float4(i.colour.rgb * ((c.x - c.y) * (c.z - c.w)), 0);
#endif
}
