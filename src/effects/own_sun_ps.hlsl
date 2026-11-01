// Own sun (docs/architecture/own-sun-pass.md sections 2-4): the pixel program of the stage's one quad, ONE/ONE into the
// FP16 scene after the fog pass and before the TAA resolve.
//   radiance = lerp(corona x h(r), disc, D(r)) x open
//   D(r)  = 1 - smoothstep(0.9, 1, r): the disc with a soft edge of 0.1 disc radius (r in disc radii)
//   h(r)  = max(saturate(exp(-3 (r - 1))) - exp(-6), 0): the corona, 1 inside the disc, 0 at the quad's edge (r = 3)
//   open  = the nine taps (the fragment and the eight knight moves (+-2, +-1) / (+-1, +-2) pixels) of the scene depth lane
//           (RT2 at s0, point sampled on texel centres) whose .r < 0 (the route's sentinel: no routed surface in front of
//           the far plane), / 9: the per-pixel clip with its soft edge (sun-partial-occlusion.md, "Jitter contract").
// c3 / c4 carry the colours with the fog's far transmittance already applied (own_sun_core.h radiance()). Alpha 0: the
// scene's alpha is left as it is under ONE/ONE. Compiled with tools/shaders/generate_own_sun_programs.py (ps_3_0).
sampler2D lane_sampler : register(s0);
float4 texel : register(c0);      // 1/W, 1/H of the lane (the target's size), unused x2
float4 knight_d : register(c1);   // 2/W, 1/H, 1/W, 2/H: the knight offsets (+2, +1) and (+1, +2) as lane uv
float4 knight_w : register(c2);   // -2/W, 1/H, -1/W, 2/H: the knight offsets (-2, +1) and (-1, +2)
float4 disc_rgb : register(c3);   // K lerp(c, 1, 0.6) T, unused
float4 corona_rgb : register(c4); // K_h c T, unused
struct Input {
    float2 local : TEXCOORD0; // offset from the sun's centre in disc radii
    float2 pixel : VPOS;
};
float4 main(Input i) : COLOR {
    const float2 base = (i.pixel + 0.5) * texel.xy;
    float4 a, b;
    a.x = tex2Dlod(lane_sampler, float4(base + knight_d.xy, 0, 0)).r;
    a.y = tex2Dlod(lane_sampler, float4(base - knight_d.xy, 0, 0)).r;
    a.z = tex2Dlod(lane_sampler, float4(base + knight_w.xy, 0, 0)).r;
    a.w = tex2Dlod(lane_sampler, float4(base - knight_w.xy, 0, 0)).r;
    b.x = tex2Dlod(lane_sampler, float4(base + knight_d.zw, 0, 0)).r;
    b.y = tex2Dlod(lane_sampler, float4(base - knight_d.zw, 0, 0)).r;
    b.z = tex2Dlod(lane_sampler, float4(base + knight_w.zw, 0, 0)).r;
    b.w = tex2Dlod(lane_sampler, float4(base - knight_w.zw, 0, 0)).r;
    const float centre = tex2Dlod(lane_sampler, float4(base, 0, 0)).r;
    const float4 ninth = 1.0 / 9.0;
    const float open = dot((a < 0) ? ninth : 0, 1) + dot((b < 0) ? ninth : 0, 1) + ((centre < 0) ? 1.0 / 9.0 : 0);
    const float r = length(i.local);
    const float d = 1 - smoothstep(0.9, 1.0, r);
    const float h = max(saturate(exp2(-4.32808512 * (r - 1))) - 0.00247875218, 0);
    const float3 rgb = lerp(corona_rgb.rgb * h, disc_rgb.rgb, d) * open;
    return float4(rgb, 0);
}
