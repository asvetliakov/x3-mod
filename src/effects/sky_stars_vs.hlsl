// Sky stars (docs/architecture/sector-background-modern.md, Implementation A): the vertex program of the star batch.
// One quad per star from the static buffer (src/proxy/sky_stars_core.h build()): the unit direction goes through the
// frame's view rotation and far-plane projection with the TAA jitter folded in (c252-c254, core vertex_constants), the
// corner offset in pixels is added in clip space (c255.xy = 2/W, 2/H), so every quad is screen-aligned and sized in
// pixels whatever the field of view. All four corners share w, so a star behind the camera is clipped whole. The
// pixel program receives the offset from the star centre in pixels, 1/(2a) for its support half-width a, and the
// star's summed target value per channel. Compiled with tools/shaders/generate_sky_stars_programs.py (vs_3_0).
float4 rows[4] : register(c252);
struct Input {
    float3 direction : POSITION; // unit, world orientation
    float2 corner : TEXCOORD0;   // +-(a + margin) px
    float energy : TEXCOORD1;    // summed target value of the brightest channel
    float4 tint : COLOR0;        // brightest channel 1
};
struct Output {
    float4 position : POSITION;
    float4 kernel : TEXCOORD0; // offset px (x, y), 1/(2a), 0
    float4 colour : TEXCOORD1; // energy per channel
};
Output main(Input i) {
    Output o;
    float w = dot(i.direction, rows[2].xyz);
    o.position.xy = float2(dot(i.direction, rows[0].xyz), dot(i.direction, rows[1].xyz)) + i.corner * rows[3].xy * w;
    o.position.z = w * rows[3].z;
    o.position.w = w;
    float a = abs(i.corner.x) - rows[3].w;
    o.kernel = float4(i.corner, 0.5 / a, 0);
    o.colour = float4(i.tint.rgb * i.energy, 0);
    return o;
}
