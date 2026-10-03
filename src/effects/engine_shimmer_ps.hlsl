// Engine heat shimmer (docs/architecture/engine-exhaust-gap-analysis.md gap 9; docs/architecture/effects-modernisation-
// opus.md section 3.8): the refraction draw after the temporal resolve and before the write-back and bloom. The pass
// copied the resolved image over the union of the rects into a scratch texture (s0 linear, s1 point: the same texture)
// and draws the rects' quads (the shared pass-through vertex program, src/temporal/quad_vs.hlsl) into the resolved
// image itself. Every pixel sums the displacement of all rects (so overlapping quads write the same value), each one
// mask x the direction of the gradient of animated value noise (the plume's hash family, engine_plume_ps.hlsl vnoise:
// the cell grid in nozzle widths, scrolled along the axis by the nozzle's own flow phase as its plume, boiling in the third axis with the
// stage's clock), clamps the sum to unit length and samples the copy amplitude pixels away. A pixel whose sum is zero
// (outside every mask, occluded) is the copy point-sampled at its own centre: bit for bit the input.
// Mask of a rect (engine_shimmer_core.h mask_at): s along the screen axis from the nozzle, t across;
// smoothstep(-back, 0, s) (1 - smoothstep(0, length, s)) (1 - smoothstep(0.35 half-width, half-width, |t|)), 0 on the
// border. Occlusion: the lane's device depth d (s2, point) in [0, depth) is nearer than the plume: the shimmer fades out
// over a small view-depth band in front of it, x saturate(1 - (depth - d) x fade) (the sky's -1 never occludes).
// Compiled with tools/shaders/generate_engine_shimmer_program.py.
sampler2D scene_linear : register(s0);
sampler2D scene_point : register(s1);
sampler2D lane : register(s2);
float4 target : register(c0);    // 1/W, 1/H, W, H
float4 frame : register(c1);     // amplitude (px), rect count, 0, the clock x boil rate
float4 grid : register(c2);      // 1 / cell (cells per nozzle width), 0, 0, 0
float4 rect_a[16] : register(c4);  // origin (px), unit axis
float4 rect_b[16] : register(c20); // length (px ahead), half-width (px), back (px behind), 1 / nozzle width (px)
float4 rect_c[16] : register(c36); // seed (x 61.7), occlusion depth (device; 0 none), the nozzle's flow phase / cell, the
                                   // occlusion fade (1 / the band's device-depth span)
// The plume's value noise and its gradient in x and y: (d/dx, d/dy) of the smoothstep-trilinear interpolation.
float2 vnoise_gradient(float3 p) {
    const float3 f = frac(p);
    const float3 i = p - f;
    const float3 base = i * 0.3183099 + float3(0.1, 0.2, 0.3);
    const float3 a = frac(base), b = frac(base + 0.3183099);
    const float3 u = f * f * (3.0 - 2.0 * f);
    const float2 du = 6.0 * f.xy * (1.0 - f.xy);
    const float4 X = float4(a.x, b.x, a.x, b.x);
    const float4 Y = float4(a.y, a.y, b.y, b.y);
    const float4 xy = X * Y * 83521.0, sxy = X + Y;
    const float4 h0 = frac(xy * a.z * (sxy + a.z));
    const float4 h1 = frac(xy * b.z * (sxy + b.z));
    const float4 h = lerp(h0, h1, u.z);          // corners (0,0) (1,0) (0,1) (1,1)
    const float2 hx = lerp(h.xz, h.yw, u.x);     // rows y = 0, y = 1
    const float2 dx = h.yw - h.xz;               // the x differences of both rows
    return float2(du.x * lerp(dx.x, dx.y, u.y), du.y * (hx.y - hx.x));
}
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    const float2 p = uv * target.zw;
    const float depth = tex2Dlod(lane, float4(uv, 0.0, 0.0)).r;
    float2 total = float2(0.0, 0.0);
    // Unrolled: the native compiler selects a dynamically indexed constant with a compare chain (48 cmp per pass of a
    // rep loop, measured on d3dx9_37); sixteen static copies of the one-octave body cost about 1,500 slots and a rect
    // that misses the pixel about ten instructions.
    [unroll] for (int i = 0; i < 16; ++i) {
        const float4 a = rect_a[i], b = rect_b[i], c = rect_c[i];
        const float2 rel = p - a.xy;
        const float s = dot(rel, a.zw);
        const float t = dot(rel, float2(-a.w, a.z));
        const float at = abs(t);
        const float visible = depth >= 0.0 ? saturate(1.0 - (c.y - depth) * c.w) : 1.0;
        [branch] if (i < frame.y && s > -b.z && s < b.x && at < b.y && visible > 0.0) {
            const float mask = smoothstep(-b.z, 0.0, s) * (1.0 - smoothstep(0.0, b.x, s)) *
                               (1.0 - smoothstep(0.35 * b.y, b.y, at)) * visible;
            // Nozzle widths -> cells; the field moves away from the nozzle with the flow.
            const float2 q = float2(s, t) * (b.w * grid.x);
            const float3 cell = float3(q.x - c.z, q.y, c.x + frame.w);
            const float2 g = vnoise_gradient(cell);
            // Gradient (in the rect's frame) -> screen direction, at most unit length per rect: the slope reaches 1.5
            // per axis but is about 0.4 typically (measured: 0.47 of the amplitude at most with 1 / 1.5), so 1 / 0.75
            // makes the amplitude the displacement's ceiling. One octave: sixteen unrolled copies of two stay over the
            // tool's 32 KB program bound.
            float2 v = (a.zw * g.x + float2(-a.w, a.z) * g.y) * (1.0 / 0.75);
            const float l2 = dot(v, v);
            v *= l2 > 1.0 ? rsqrt(l2) : 1.0;
            total += mask * v;
        }
    }
    const float m2 = dot(total, total);
    [branch] if (m2 > 0.0) {
        total *= m2 > 1.0 ? rsqrt(m2) : 1.0;
        return tex2Dlod(scene_linear, float4(uv + total * frame.x * target.xy, 0.0, 0.0));
    }
    return tex2Dlod(scene_point, float4(uv, 0.0, 0.0));
}
