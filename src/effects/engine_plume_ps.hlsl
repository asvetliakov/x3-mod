// Engine plumes (docs/architecture/engine-effects-modern.md sections 3, 4 and "Plume look redesign"): the pixel
// program of the stage's one indexed draw, ONE/ONE on the FP16 scene (alpha 0: the scene's alpha is untouched).
// The law is the Engine Exhaust Lab's plume() (tools/effects/engine_exhaust_lab.html, modern branch), analytic, no
// texture but the lane. Lengths are in nozzle widths n (local.w, world units: engine_plumes_core.h Look::nozzle_width
// x value): x along the axis from the nozzle, y across, r = |y|; u = x / L with L the pulsed length (the CPU pulses it
// per nozzle and frame). The constants come from engine_plumes_core.h Look through c3..c7 (one block, uploaded per
// frame).
// Body: width w(u) (the mouth bulge, a near-cylinder tapering towards the tail, the tail narrowing), radial = r / w,
// 3-octave value noise in ((x - phase) 1.6, y 3, seed + 0.7 t), phase the flow in nozzle widths accumulated on the CPU
// at a constant speed (c0.z; engine_plumes_core.h FlowPhase), so the field moves the same whatever the pulsed L: n eats
// the edge (erosion), n2 modulates the radiance (turbulence); shock diamonds along the core fading along the plume; a
// white-hot core cooling into the tint; the tail fade. Halo: exp(-d / sigma) x exp(-2.2 u), d the distance to the segment
// nozzle..tip, sigma the nozzle's (sigma0, the preset's) along the whole plume as in the mock-up, tapered to 0 over the
// last 0.5 sigma before the quad's reach (`look.z` sigma), so inside it the halo is the mock-up's.
// Nozzle ring: a thin bright ring at the mouth, its Gaussian widened to the pixel footprint (energy kept).
// Disc (kind 1, the camera-facing nozzle disc; the CPU weighted it by |axis . to_camera|): the same law end-on, the
// body at u = Look::disc_u over the disc's radius, the ring at the mouth, the halo radially about the nozzle.
// Soft occlusion against the completed lane (RT2 at s0, point sampled at the pixel): .b the view depth on the
// four-channel lane, z/w in .r inverted with m32 / (d - m22) on the R32F lane; .r outside [0, 1] (the sentinel) = no
// occluder; a non-finite or non-positive depth occludes. The plume's depth is that of the nearest axis point (the
// nozzle's view z plus the axis's view z component x u clamped to [0, L]; the disc's is the nozzle's), pulled towards the
// camera by the bias when the exhaust faces it: vis = saturate((lane - (z_axis - bias)) / SOFT), SOFT 0.15 value for the
// body and the ring and 1.0 value for the halo, so a plume behind a hull shows only past its silhouette.
// Compiled with tools/shaders/generate_rigid_motion_pixel.py.
sampler2D lane_sampler : register(s0);
float4 lane_sizes : register(c0); // 1/W, 1/H of the target, the flow phase (nozzle widths), unused
float4 lane_form : register(c1);  // four_channel (1: .b is the view depth), m22, m32 (R32F: z = m32 / (d - m22)), unused
float4 look : register(c2);       // SOFT body, SOFT halo (x value), halo reach (x sigma), seconds (the stage's clock)
float4 shape_k : register(c3);    // 0.5 bulge, (0.04 - 0.5 bulge) taper, 0.5 bulge taper, tail narrowing x taper
float4 fire_k : register(c4);     // unused, erosion (erode x 1.6 x 0.6), turbulence (turb x 2.2), tail fade start
float4 cell_k : register(c5);     // shock, 2 pi / period, 5 cfade, 1.2 tail
float4 core_k : register(c6);     // heat, 1 / (1.4 core), ring radius, ring sigma^2
float4 halo_k : register(c7);     // unused, disc u, the ring's axial falloff, 2 (params.z holds I_ring / I_core / 2)
struct Input {
    float4 local : TEXCOORD0;     // x, y (world), L (pulsed, world), n (nozzle width, world)
    float4 shape : TEXCOORD1;     // halo sigma0 (nozzle widths), value, occlusion bias, kind (0 axial, 1 disc)
    float4 view : TEXCOORD2;      // the nozzle's view z, I_core, I_halo, the axis's view z component
    float3 tint : TEXCOORD3;
    float4 params : TEXCOORD4;    // throttle s, seed (0..1), I_ring / I_core / 2, unused
    float3 fog : TEXCOORD5;       // the fog transmittance per channel: the white-hot core's and the ring's (the tint has it)
    float2 pixel : VPOS;
};
// Value noise of the mock-up (hash p = frac(p 0.3183099 + (.1, .2, .3)) 17, frac(x y z (x + y + z)); smoothstep
// trilinear), the eight corners in two float4 batches; the hash's 17^4 is folded into one product.
float vnoise(float3 p) {
    float3 f = frac(p);
    const float3 i = p - f;
    const float3 base = i * 0.3183099 + float3(0.1, 0.2, 0.3);
    const float3 a = frac(base), b = frac(base + 0.3183099);
    f = f * f * (3.0 - 2.0 * f);
    const float4 X = float4(a.x, b.x, a.x, b.x);
    const float4 Y = float4(a.y, a.y, b.y, b.y);
    const float4 xy = X * Y * 83521.0, sxy = X + Y;
    const float4 h0 = frac(xy * a.z * (sxy + a.z));
    const float4 h1 = frac(xy * b.z * (sxy + b.z));
    const float4 h = lerp(h0, h1, f.z);
    const float2 hx = lerp(h.xz, h.yw, f.x);
    return lerp(hx.x, hx.y, f.y);
}
float fbm(float3 p) {
    return 0.5 * vnoise(p) + 0.25 * vnoise(p * 2.03 + 1.7) + 0.125 * vnoise(p * 4.1 + 3.1);
}
float4 main(Input i) : COLOR0 {
    const float n = max(i.local.w, 1e-6);
    const float L = max(i.local.z, 1e-6) / n; // nozzle widths
    const bool disc = i.shape.w > 0.5;
    const float2 q = i.local.xy / n;
    const float rho = length(q);
    const float s = i.params.x;
    const float t = look.w;
    // The body's coordinates: the axial quad's own, the disc's end-on at u = disc_u.
    const float x = disc ? halo_k.y * L : q.x;
    const float r = disc ? rho : abs(q.y);
    const float u = x / L;
    const float uc = saturate(u);
    // Width: the bulge at the mouth, the line between cylinder and cone, the tail narrowing.
    const float b = shape_k.x * (1.0 - 0.55 * exp(-9.0 * uc)) * (1.0 + 0.35 * smoothstep(0.0, 0.25, uc) * exp(-4.0 * uc));
    const float w = min(shape_k.x + shape_k.y * uc, b + shape_k.z) * max(1.0 - shape_k.w * smoothstep(0.6, 1.0, uc), 0.05);
    const float radial = r / w;
    // Noise translated along the axis by the flow phase (the disc: its own plane).
    const float3 p = float3(((disc ? q.x : x) - lane_sizes.z) * 1.6, q.y * 3.0, i.params.y * 1861.5 + t * 0.7);
    const float n1 = fbm(p);
    const float n2 = fbm(p * 2.2 + float3(5.0, 2.0, 1.0));
    const float edge = 1.0 - smoothstep(0.55, 1.0, radial + fire_k.y * (n1 - 0.5));
    const float tail = (1.0 - smoothstep(fire_k.w, 1.0, u)) * exp(-u * cell_k.w);
    const float inside = (u >= 0.0 && u <= 1.0) ? 1.0 : 0.0;
    const float cells = 1.0 + cell_k.x * cos(cell_k.y * u) * exp(-u * cell_k.z) * (1.0 - smoothstep(0.0, 0.8, radial)) * s;
    const float turbulence = 1.0 + fire_k.z * (n2 - 0.5);
    const float core_mask = 1.0 - smoothstep(0.0, 1.0, radial * core_k.y); // 1 - smoothstep(0, 1.4 core, radial)
    const float heat = core_k.x * core_mask * (1.0 - smoothstep(0.0, 0.55, u));
    const float3 colour = lerp(i.tint, i.fog * float3(1.0, 0.97, 0.9), heat);
    const float body = i.view.y * edge * tail * inside * cells * turbulence * (1.0 + 0.6 * core_mask);
    // Halo about the segment nozzle..tip (the disc: about the nozzle), the nozzle's sigma; the window
    // saturate((reach - d) / (0.5 sigma)) only tapers its outer part.
    const float du = disc ? 0.0 : q.x - clamp(q.x, 0.0, L);
    const float d = disc ? rho : sqrt(du * du + r * r);
    const float dn = d / max(i.shape.x, 1e-4);
    const float window = saturate(2.0 * (look.z - dn));
    const float halo = i.view.z * exp(-dn) * exp(-2.2 * uc) * window;
    // The nozzle ring, its Gaussian widened to the pixel footprint (fwidth in nozzle widths).
    const float aa = fwidth(q.y);
    const float s2 = core_k.w + 0.25 * aa * aa;
    const float rr = r - core_k.z;
    const float xr = disc ? 0.0 : q.x;
    const float ring = i.view.y * i.params.z * halo_k.w * sqrt(core_k.w / s2) * exp(-rr * rr / (2.0 * s2)) * exp(-max(xr, 0.0) * halo_k.z) *
                       (xr >= -0.05 ? 1.0 : 0.0);
    // The lane's soft occlusion.
    const float4 lane = tex2Dlod(lane_sampler, float4((i.pixel + 0.5) * lane_sizes.xy, 0, 0));
    const bool occluder = lane.r >= 0.0 && lane.r <= 1.0;
    const float z = lane_form.x > 0.5 ? lane.b : lane_form.z / (lane.r - lane_form.y);
    const float valid = (z > 0.0 && z <= 3.402823466e38) ? 1.0 : 0.0;
    const float depth = i.view.x + i.view.w * clamp(i.local.x, 0.0, i.local.z); // the nearest axis point (disc: view.w 0)
    const float gap = z - (depth - i.shape.z);
    const float soft_body = occluder ? valid * saturate(gap / max(look.x * i.shape.y, 1e-4)) : 1.0;
    const float soft_halo = occluder ? valid * saturate(gap / max(look.y * i.shape.y, 1e-4)) : 1.0;
    const float3 ring_colour = lerp(i.tint, i.fog * float3(1.0, 0.95, 0.85), 0.6);
    const float3 result = (colour * body + ring_colour * ring) * soft_body + i.tint * (halo * soft_halo);
    return float4(max(result, 0.0), 0.0);
}
