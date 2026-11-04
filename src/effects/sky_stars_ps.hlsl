// Sky stars (docs/architecture/sector-background-modern.md, Implementation A): the pixel program of the star batch.
// The pixel's share of the star: the separable Beta(3,3) kernel over [-a, a] integrated over the pixel's own square,
// (C(t(u + 0.5)) - C(t(u - 0.5))) per axis with C the quintic smootherstep and t(x) = x / (2a) + 0.5, so the shares of
// the covered pixels sum to exactly 1 at any sub-pixel position (src/proxy/sky_stars_core.h). RGB only; the batch is
// drawn ONE/ONE with COLORWRITEENABLE 7. Compiled with tools/shaders/generate_sky_stars_programs.py (ps_3_0).
struct Input {
    float4 kernel : TEXCOORD0; // offset px (x, y), 1/(2a)
    float4 colour : TEXCOORD1; // energy per channel
};
float4 main(Input i) : COLOR0 {
    float4 t = saturate(float4(i.kernel.x + 0.5, i.kernel.x - 0.5, i.kernel.y + 0.5, i.kernel.y - 0.5) * i.kernel.z + 0.5);
    float4 c = t * t * t * (t * (t * 6 - 15) + 10);
    return float4(i.colour.rgb * ((c.x - c.y) * (c.z - c.w)), 0);
}
