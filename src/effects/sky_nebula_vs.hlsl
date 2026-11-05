// Sky nebula deband (docs/architecture/sector-background-modern.md, Decision C1): the vertex program that replaces the
// engine's SM1.1 background program 7b6393fe2d3e1d85 (adeffects.fb) on the four nebula part draws. Its math is that
// program's, read from its local bytecode (docs/reverse-engineering/background-temporal-coverage.md, "The background
// pair's bytecode"): the position with w forced to 1 through the four rows g_mWorldViewProjection (c0-c3, oPos.i =
// dot(p, c_i)); the UV (u, v, 1) through the two rows of g_TexMatrix (c4, c5, the .xyz of each). The engine's constants
// are read as set. Added: the frame's TAA jitter, oPos.xy += c255.xy x oPos.w (c255 = (2 jx / W, -2 jy / H, 0, 0) when
// the jitter is active, else 0: the term then adds an exact 0). Compiled with
// tools/shaders/generate_sky_nebula_programs.py (vs_3_0).
float4 wvp[4] : register(c0);
float4 tex_matrix[2] : register(c4);
float4 jitter : register(c255);
struct Input {
    float4 position : POSITION;
    float2 uv : TEXCOORD0;
};
struct Output {
    float4 position : POSITION;
    float2 uv : TEXCOORD0;
};
Output main(Input i) {
    Output o;
    float4 p = float4(i.position.xyz, 1);
    o.position = float4(dot(p, wvp[0]), dot(p, wvp[1]), dot(p, wvp[2]), dot(p, wvp[3]));
    o.position.xy += jitter.xy * o.position.w;
    float3 t = float3(i.uv, 1);
    o.uv = float2(dot(t, tex_matrix[0].xyz), dot(t, tex_matrix[1].xyz));
    return o;
}
