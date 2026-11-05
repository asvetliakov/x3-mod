// Sky stars (docs/architecture/sector-background-modern.md, Implementation A): the vertex program of the star batch.
// One quad per star from the static buffer (src/proxy/sky_stars_core.h build()): the unit direction goes through the
// frame's view rotation and far-plane projection with the TAA jitter folded in (c252-c254, core vertex_constants), the
// corner offset in pixels is added in clip space (c255.xy = 2/W, 2/H), so every quad is screen-aligned and sized in
// pixels whatever the field of view. All four corners share w, so a star behind the camera is clipped whole. The
// pixel program receives the offset from the star centre in pixels, 1/(2a) for its support half-width a, and the
// star's summed target value per channel. Twinkle (c251 = wall seconds modulo 128, amplitude; core twinkle_constants):
// a star whose twinkle lanes are on scales its energy by 1 + amplitude sin(2 pi (rate t + phase)), rate = R x 255/128
// Hz (an integer n / 128 Hz), phase = G (shifted by half a cycle so the sine argument stays in [-pi, pi]); steady stars
// carry 0 lanes. Energy only, the footprint is unchanged. Compiled
// with tools/shaders/generate_sky_stars_programs.py (vs_3_0).
// SKY_STARS_DIM (src/effects/sky_stars_dim_vs.hlsl, Decision C2): the deferred batch's twin also passes the star centre's
// UV in the 1/8-size copy of the HDR target, four tap positions half a dim texel either side (c250.zw), and the law
// (k, 1 / L0) = c250.xy to the pixel program. Without the define the program is the in-place batch's, unchanged.
#ifdef SKY_STARS_DIM
float4 dim : register(c250);
#endif
float4 twinkle : register(c251);
float4 rows[4] : register(c252);
struct Input {
    float3 direction : POSITION; // unit, world orientation
    float2 corner : TEXCOORD0;   // +-(a + margin) px
    float energy : TEXCOORD1;    // summed target value of the brightest channel
    float4 tint : COLOR0;        // brightest channel 1
    float4 lanes : COLOR1;       // rate n / 255 (r), phase (g), on (b)
};
struct Output {
    float4 position : POSITION;
    float4 kernel : TEXCOORD0; // offset px (x, y), 1/(2a), 0
    float4 colour : TEXCOORD1; // energy per channel
#ifdef SKY_STARS_DIM
    float4 dim_uv : TEXCOORD2;  // the centre's dim-copy UV minus and plus half a dim texel (x, y, x, y)
    float2 dim_law : TEXCOORD3; // k, 1 / L0
#endif
};
Output main(Input i) {
    Output o;
    float w = dot(i.direction, rows[2].xyz);
    o.position.xy = float2(dot(i.direction, rows[0].xyz), dot(i.direction, rows[1].xyz)) + i.corner * rows[3].xy * w;
    o.position.z = w * rows[3].z;
    o.position.w = w;
    float a = abs(i.corner.x) - rows[3].w;
    o.kernel = float4(i.corner, 0.5 / a, 0);
    float cycles = frac(i.lanes.r * (255.0 / 128.0) * twinkle.x + i.lanes.g);
    float gain = 1 + twinkle.y * i.lanes.b * sin(6.28318531 * cycles - 3.14159265);
    o.colour = float4(i.tint.rgb * (i.energy * gain), 0);
#ifdef SKY_STARS_DIM
    float2 centre = float2(dot(i.direction, rows[0].xyz), dot(i.direction, rows[1].xyz)) / w;
    float2 uv = centre * float2(0.5, -0.5) + 0.5;
    o.dim_uv = float4(uv - dim.zw, uv + dim.zw);
    o.dim_law = dim.xy;
#endif
    return o;
}
