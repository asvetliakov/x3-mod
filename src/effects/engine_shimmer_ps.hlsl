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
// A weapon hit's shock ring (docs/architecture/hull-impact-classes.md 3.5, 2026-11-07; engine_shimmer_core.h weapon_ring):
// with c1.z rings, the lane's surface point under the pixel (its device depth through the unjittered projection, c74 /
// c75; the sky never) whose stretched distance d' from the ring's centre lies within the band b of r - b (u = (r - d') /
// b - 1, |u| < 2.5) adds strength e^(-u^2) along the pixel's direction from the centre's pixel: the band rides the hull
// just inside the front like the ring's own shell. No ring (every plume frame): the branch is skipped, the plumes'
// image as before bit for bit. A ring slot in the plane mode (c72+k.w 1; a death fire wave, docs/architecture/
// explosion-look.md 13.1, engine_shimmer_core.h wave_strips): the pixel's ray meets the plane (normal c76+k) through the
// centre at view depth t; a point whose stretched distance d' lies in [r, r + b] (u = (d' - r) / b) moves outward from
// the centre's pixel by strength x 4 u (1 - u) pixels, sky included, not where the lane's surface is nearer than the
// plane point; this displacement adds to the amplitude's field after its clamp.
// Compiled with tools/shaders/generate_engine_shimmer_program.py.
sampler2D scene_linear : register(s0);
sampler2D scene_point : register(s1);
sampler2D lane : register(s2);
float4 target : register(c0);    // 1/W, 1/H, W, H
float4 frame : register(c1);     // amplitude (px), rect count, ring count, the clock x boil rate
float4 grid : register(c2);      // 1 / cell (cells per nozzle width), 0, 0, 0
float4 rect_a[16] : register(c4);  // origin (px), unit axis
float4 rect_b[16] : register(c20); // length (px ahead), half-width (px), back (px behind), 1 / nozzle width (px)
float4 rect_c[16] : register(c36); // seed (x 61.7), occlusion depth (device; 0 none), the nozzle's flow phase / cell, the
                                   // occlusion fade (1 / the band's device-depth span)
float4 rect_d[16] : register(c52); // strength (an engine rect 1: x 1 is exact; a weapon hit's 1 -> 0), unused x3
float4 ring_a[2] : register(c68);  // the ring's centre (view), its radius (view units)
float4 ring_b[2] : register(c70);  // the heading x sqrt(1 - 1 / stretch^2) (view; 0: round), the band (view units)
float4 ring_c[2] : register(c72);  // the centre's pixel, the strength, 0
float4 lane_proj : register(c74);  // m00, m11, m20, m21 of the unjittered projection
float4 lane_law : register(c75);   // m22, m32 (view z = m32 / (d - m22)), 0, 0
float4 ring_n[2] : register(c76);  // the plane mode's normal (view, unit), unused
// The plume's value noise and its gradient in x and y: (d/dx, d/dy) of the smoothstep-trilinear interpolation.
// The lattice index is exact, as in engine_plume_ps.hlsl vnoise: p - frac(p) is off an integer in its last bit when
// fxc folds an integer offset out of the frc but keeps it in a mad that the driver fuses (NVIDIA, 2026-11-05);
// floor(x + 0.5) of a value within half a unit of an integer is that integer whatever the contraction.
float2 vnoise_gradient(float3 p) {
    const float3 i = floor(floor(p) + 0.5);
    const float3 f = p - i;
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
            total += (mask * rect_d[i].x) * v;
        }
    }
    float2 wave = float2(0.0, 0.0); // the plane mode's displacement (pixels)
    [branch] if (frame.z > 0.0) { // uniform: a plume-only frame skips the rings whole
        const float2 ndc = float2(2.0 * uv.x - 1.0 - target.x, 1.0 - 2.0 * uv.y + target.y);
        const float3 ray = float3((ndc.x - lane_proj.z) / lane_proj.x, (ndc.y - lane_proj.w) / lane_proj.y, 1.0);
        const float zv = depth >= 0.0 ? lane_law.y / (depth - lane_law.x) : 3.402823466e38;
        [unroll] for (int k = 0; k < 2; ++k) {
            [branch] if (k < frame.z) {
                [branch] if (ring_c[k].w > 0.5) {
                    const float nd = dot(ring_n[k].xyz, ray);
                    const float t = abs(nd) > 1e-7 ? dot(ring_n[k].xyz, ring_a[k].xyz) / nd : -1.0;
                    [branch] if (t > 0.0 && t < zv) {
                        const float3 d = ray * t - ring_a[k].xyz;
                        const float al = dot(d, ring_b[k].xyz);
                        const float u = (sqrt(max(dot(d, d) - al * al, 0.0)) - ring_a[k].w) / ring_b[k].w;
                        [branch] if (u > 0.0 && u < 1.0) {
                            const float2 rel = p - ring_c[k].xy;
                            wave += (ring_c[k].z * 4.0 * u * (1.0 - u) * rsqrt(max(dot(rel, rel), 1e-4))) * rel;
                        }
                    }
                } else {
                    [branch] if (depth >= 0.0) {
                        const float3 pv = ray * zv;
                        const float3 d = pv - ring_a[k].xyz;
                        const float al = dot(d, ring_b[k].xyz);
                        const float u = (ring_a[k].w - sqrt(max(dot(d, d) - al * al, 0.0))) / ring_b[k].w - 1.0;
                        [branch] if (abs(u) < 2.5) {
                            const float2 rel = p - ring_c[k].xy;
                            total += (ring_c[k].z * exp2(-1.442695 * u * u) * rsqrt(max(dot(rel, rel), 1e-4))) * rel;
                        }
                    }
                }
            }
        }
    }
    const float m2 = dot(total, total);
    [branch] if (m2 > 0.0 || dot(wave, wave) > 0.0) {
        total *= m2 > 1.0 ? rsqrt(m2) : 1.0;
        return tex2Dlod(scene_linear, float4(uv + (total * frame.x + wave) * target.xy, 0.0, 0.0));
    }
    return tex2Dlod(scene_point, float4(uv, 0.0, 0.0));
}
