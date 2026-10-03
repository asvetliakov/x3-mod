// Engine plumes (docs/architecture/engine-effects-modern.md sections 3, 4, "Plume look redesign" and "After flight C"):
// the pixel program of the stage's one indexed draw, ONE/ONE on the FP16 scene (alpha 0: the scene's alpha is
// untouched). The law is the Engine Exhaust Lab's plume() (tools/effects/engine_exhaust_lab.html, modern branch),
// analytic, no texture but the lane. Lengths are in nozzle widths n (local.w, world units: engine_plumes_core.h
// Look::nozzle_width x value): x along the axis from the nozzle, y across, r = |y|; u = x / L with L the pulsed length
// (the CPU pulses it per nozzle and frame). The constants come from engine_plumes_core.h Look through c3..c15 (one block,
// uploaded per frame).
// Body (the axial quad): width w(u) (the mouth bulge, a near-cylinder tapering towards the tail, the tail narrowing),
// radial = r / w, 3-octave value noise in ((x - phase) 1.6, y 3, seed + 0.7 t), phase the nozzle's flow in its nozzle
// widths (shape.w: the frame's accumulator x the nozzle's flow_factor, engine_plumes_core.h nozzle_phase, so the world
// speed is one from value 500 up and capitals crawl): n1 eats the edge (erosion), n2
// modulates the radiance (turbulence); shock diamonds along the core fading along the plume, ramped in over the first
// period (smoothstep(0, period, u): the mouth is no crest); a white-hot core cooling into the body's colour, which runs
// from the head colour (peak.rgb) to the tint (the mean) over smoothstep(0.3, 1, u) (two-tone, gap 5); the tail fade; the
// mouth ramp (after flight D): the body x (1 - dip (1 - smoothstep(0, ramp, u))), c16, so the nozzle reads no brighter
// than 0.85 of the body's peak at any throttle (the disc's samples carry the same factor in their tail column).
// Halo: exp(-d / sigma) x exp(-2.2 u), d the distance to the segment nozzle..tip, sigma the nozzle's (sigma0, the
// preset's) along the whole plume as in the mock-up, tapered to 0 over the last 0.5 sigma before the quad's reach
// (`look.z` sigma). Nozzle ring: a thin ring at the mouth, its Gaussian widened to the pixel footprint (energy kept).
// The halo's and the ring's radiances follow the body's throttle curve I(s) / I(1) (the CPU sets them).
// The mouth terms (the ring and the halo) combine as a soft maximum, (a^4 + b^4)^(1/4), not a sum; the body adds. Where
// the disc is drawn the axial quad hands its mouth over to it: x (1 - disc weight x (1 - smoothstep(0.3, 0.8, d))), d the
// screen-plane distance from the nozzle in nozzle widths.
// Disc (kind 1, the camera-facing nozzle disc; after flight C the end-on plume): the body integrated along the axis, the
// mean over the 8 samples u_k of c8..c15 (w, tail, cell, heat) of the law at radial = rho / w_k (the shock cells' rings
// at 0.8 w_k), its noise in the disc's plane (x 3 across, as the axial quad's y) with the flow along the line of sight as
// the third coordinate; the halo radially about the nozzle; the ring; the total soft-capped, cap x (1 - exp(-total /
// cap)) (view.w; the CPU set the radiances and the cap, engine_plumes_core.h build_nozzle).
// Soft occlusion against the completed lane (RT2 at s0, point sampled at the pixel): .b the view depth on the
// four-channel lane, z/w in .r inverted with m32 / (d - m22) on the R32F lane; .r outside [0, 1] (the sentinel) = no
// occluder; a non-finite or non-positive depth occludes. The plume's depth is that of the nearest axis point (the
// nozzle's view z plus the axis's view z component x u clamped to [0, L]; the disc's is the nozzle's), pulled towards the
// camera by the bias when the exhaust faces it: vis = saturate((lane - (z_axis - bias)) / SOFT), SOFT 0.15 value for the
// body and the ring and 1.0 value for the halo, so a plume behind a hull shows only past its silhouette.
// Nozzle spill (gap 3): the halo's visibility is at least glow_through (c16.z) within spill_inner nozzle widths of the
// nozzle in the screen plane, tapering to 0 at spill_reach (c17.xy), x saturate(1 + gap / min(spill_depth value,
// spill_depth_max)) (c16.w, c17.z: only an occluder near the nozzle's depth, its own hull, lets it through; the guard at
// most 300 world units, so a ship passing in front of a capital's nozzle does not); the body and the ring unchanged.
// Compiled with tools/shaders/generate_rigid_motion_pixel.py.
sampler2D lane_sampler : register(s0);
float4 lane_sizes : register(c0); // 1/W, 1/H of the target, unused x2
float4 lane_form : register(c1);  // four_channel (1: .b is the view depth), m22, m32 (R32F: z = m32 / (d - m22)), unused
float4 look : register(c2);       // SOFT body, SOFT halo (x value), halo reach (x sigma), seconds (the stage's clock)
float4 shape_k : register(c3);    // 0.5 bulge, (0.04 - 0.5 bulge) taper, 0.5 bulge taper, tail narrowing x taper
float4 fire_k : register(c4);     // 1 / period, erosion (erode x 1.6 x 0.6), turbulence (turb x 2.2), tail fade start
float4 cell_k : register(c5);     // shock, 2 pi / period, 5 cfade, 1.2 tail
float4 core_k : register(c6);     // heat, 1 / (1.4 core), ring radius, ring sigma^2
float4 halo_k : register(c7);     // hand-over inner, outer (nozzle widths), the ring's axial falloff, 2 (params.z: I_ring / I_core / 2)
float4 disc_k[8] : register(c8);  // the disc's samples u_k = (k + 0.5) / 8: w, tail (with the mouth ramp), cell (without the throttle), heat
float4 mouth_k : register(c16);   // the mouth ramp: dip, end (x L); the spill: glow_through, 1 / spill_depth (x value)
float4 spill_k : register(c17);   // the spill's inner and outer reach (nozzle widths, screen plane), 1 / spill_depth_max, 0
struct Input {
    float4 local : TEXCOORD0;     // x, y (world), L (pulsed, world; the disc: the ring's radiance), n (nozzle width, world)
    float4 shape : TEXCOORD1;     // halo sigma0 (nozzle widths), value, occlusion bias, the nozzle's flow phase (nozzle widths)
    float4 view : TEXCOORD2;      // the nozzle's view z, I_core, I_halo, the axis's view z component (the disc: the cap)
    float3 tint : TEXCOORD3;      // the mean colour: the tail's, the halo's and the ring's
    float4 params : TEXCOORD4;    // throttle s, seed (0..1), I_ring / I_core / 2, the disc's weight
    float4 fog : TEXCOORD5;       // the fog transmittance per channel: the white-hot core's and the ring's (the tint has it); sin(view)
    float4 peak : TEXCOORD6;      // the head colour (with the fog), the kind (0 axial, 1 disc)
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
// The mouth terms' soft maximum: (a^4 + b^4)^(1/4), the colours weighted by a^4 and b^4 (rgb, magnitude).
float4 soft_max(float a, float3 ca, float b, float3 cb) {
    const float a2 = a * a, b2 = b * b;
    const float a4 = a2 * a2, b4 = b2 * b2, m4 = a4 + b4;
    const float m = sqrt(sqrt(m4));
    return m4 > 1e-30 ? float4((ca * a4 + cb * b4) * (m / m4), m) : float4(0.0, 0.0, 0.0, 0.0);
}
float4 main(Input i) : COLOR0 {
    const float n = max(i.local.w, 1e-6);
    const bool disc = i.peak.w > 0.5;
    const float2 q = i.local.xy / n;
    const float rho = length(q);
    const float s = i.params.x;
    const float t = look.w;
    const float phase = i.shape.w;
    // Noise: the axial quad's field translated along the axis by the nozzle's flow phase; the disc's in its own plane
    // (both screen axes across the plume) with the flow along the line of sight as the third coordinate.
    const float3 p = disc ? float3(q.x * 3.0, q.y * 3.0, i.params.y * 1861.5 + t * 0.7 - phase * 1.6)
                          : float3((q.x - phase) * 1.6, q.y * 3.0, i.params.y * 1861.5 + t * 0.7);
    const float n1 = fbm(p);
    const float n2 = fbm(p * 2.2 + float3(5.0, 2.0, 1.0));
    const float erosion = fire_k.y * (n1 - 0.5);
    const float turbulence = 1.0 + fire_k.z * (n2 - 0.5);
    // The ring's Gaussian widened to the pixel footprint (fwidth in nozzle widths, outside the branch).
    const float aa = fwidth(q.y);
    const float s2 = core_k.w + 0.25 * aa * aa;
    const float ring_gain = sqrt(core_k.w / s2);
    const float3 ring_colour = lerp(i.tint, i.fog.rgb * float3(1.0, 0.95, 0.85), 0.6);
    const float3 white = i.fog.rgb * float3(1.0, 0.97, 0.9);
    // The lane's soft occlusion.
    const float4 lane = tex2Dlod(lane_sampler, float4((i.pixel + 0.5) * lane_sizes.xy, 0, 0));
    const bool occluder = lane.r >= 0.0 && lane.r <= 1.0;
    const float z = lane_form.x > 0.5 ? lane.b : lane_form.z / (lane.r - lane_form.y);
    const float valid = (z > 0.0 && z <= 3.402823466e38) ? 1.0 : 0.0;
    const float axis_z = disc ? 0.0 : i.view.w;
    const float depth = i.view.x + axis_z * clamp(i.local.x, 0.0, i.local.z); // the nearest axis point (disc: the nozzle)
    const float gap = z - (depth - i.shape.z);
    const float soft_body = occluder ? valid * saturate(gap / max(look.x * i.shape.y, 1e-4)) : 1.0;
    // The screen-plane distance from the nozzle in nozzle widths: the axial quad's (x sin(view), y), the disc's rho; the
    // mouth's hand-over and the nozzle spill.
    const float d_screen = sqrt(q.x * q.x * (disc ? 1.0 : i.fog.w * i.fog.w) + q.y * q.y);
    const float spill = mouth_k.z * (1.0 - smoothstep(spill_k.x, spill_k.y, d_screen)) *
                        saturate(1.0 + gap * max(mouth_k.w / max(i.shape.y, 1e-4), spill_k.z));
    const float soft_halo = occluder ? valid * max(saturate(gap / max(look.y * i.shape.y, 1e-4)), spill) : 1.0;
    float3 result;
    [branch] if (disc) {
        // The body integrated along the axis: the law at the 8 samples, the white-hot core's share alongside, and the
        // two-tone share of the rest (the tail colour's weight smoothstep(0.3, 1, u_k), a constant per sample).
        float B = 0.0, H = 0.0, T = 0.0;
        [unroll] for (int k = 0; k < 8; ++k) {
            const float4 d = disc_k[k];
            const float radial = rho / d.x;
            const float edge = 1.0 - smoothstep(0.55, 1.0, radial + erosion);
            const float core_mask = 1.0 - smoothstep(0.0, 1.0, radial * core_k.y);
            const float b = d.y * edge * (1.0 + d.z * s * (1.0 - smoothstep(0.0, 0.8, radial))) * (1.0 + 0.6 * core_mask);
            const float bh = b * d.w * core_mask;
            B += b;
            H += bh;
            T += (b - bh) * smoothstep(0.3, 1.0, (k + 0.5) / 8.0);
        }
        const float body = i.view.y * 0.125 * B * turbulence * soft_body;
        const float coloured = B - H;
        const float3 tone = lerp(i.peak.rgb, i.tint, coloured > 1e-20 ? T / coloured : 1.0);
        const float3 colour = lerp(tone, white, B > 0.0 ? H / B : 0.0);
        const float dn = rho / max(i.shape.x, 1e-4);
        const float halo = i.view.z * exp(-dn) * saturate(2.0 * (look.z - dn)) * soft_halo;
        const float rr = rho - core_k.z;
        const float ring = i.local.z * ring_gain * exp(-rr * rr / (2.0 * s2)) * soft_body;
        const float4 mouth = soft_max(ring, ring_colour, halo, i.tint);
        const float total = body + mouth.w;
        const float cap = max(i.view.w, 1e-6);
        const float shown = cap * (1.0 - exp(-total / cap));
        result = total > 1e-20 ? (colour * body + mouth.rgb) * (shown / total) : float3(0.0, 0.0, 0.0);
    } else {
        const float L = max(i.local.z, 1e-6) / n; // nozzle widths
        const float x = q.x, r = abs(q.y);
        const float u = x / L;
        const float uc = saturate(u);
        // Width: the bulge at the mouth, the line between cylinder and cone, the tail narrowing.
        const float b = shape_k.x * (1.0 - 0.55 * exp(-9.0 * uc)) * (1.0 + 0.35 * smoothstep(0.0, 0.25, uc) * exp(-4.0 * uc));
        const float w = min(shape_k.x + shape_k.y * uc, b + shape_k.z) * max(1.0 - shape_k.w * smoothstep(0.6, 1.0, uc), 0.05);
        const float radial = r / w;
        const float edge = 1.0 - smoothstep(0.55, 1.0, radial + erosion);
        const float tail = (1.0 - smoothstep(fire_k.w, 1.0, u)) * exp(-u * cell_k.w) * (1.0 - mouth_k.x * (1.0 - smoothstep(0.0, mouth_k.y, u)));
        const float inside = (u >= 0.0 && u <= 1.0) ? 1.0 : 0.0;
        const float cells = 1.0 + cell_k.x * cos(cell_k.y * u) * exp(-u * cell_k.z) * smoothstep(0.0, 1.0, u * fire_k.x) *
                                      (1.0 - smoothstep(0.0, 0.8, radial)) * s;
        const float core_mask = 1.0 - smoothstep(0.0, 1.0, radial * core_k.y); // 1 - smoothstep(0, 1.4 core, radial)
        const float heat = core_k.x * core_mask * (1.0 - smoothstep(0.0, 0.55, u));
        const float3 colour = lerp(lerp(i.peak.rgb, i.tint, smoothstep(0.3, 1.0, u)), white, heat);
        const float body = i.view.y * edge * tail * inside * cells * turbulence * (1.0 + 0.6 * core_mask) * soft_body;
        // Halo about the segment nozzle..tip, the nozzle's sigma; the window saturate((reach - d) / (0.5 sigma)) only
        // tapers its outer part.
        const float du = x - clamp(x, 0.0, L);
        const float dn = sqrt(du * du + r * r) / max(i.shape.x, 1e-4);
        const float halo = i.view.z * exp(-dn) * exp(-2.2 * uc) * saturate(2.0 * (look.z - dn)) * soft_halo;
        const float rr = r - core_k.z;
        const float ring = i.view.y * i.params.z * halo_k.w * ring_gain * exp(-rr * rr / (2.0 * s2)) * exp(-max(x, 0.0) * halo_k.z) *
                           (x >= -0.05 ? 1.0 : 0.0) * soft_body;
        // The mouth's hand-over to the disc: inside the disc's footprint (the screen-plane distance from the nozzle,
        // (x sin(view), y)) the axial quad gives way by the disc's weight, so the two draws do not stack at the mouth.
        const float handover = 1.0 - i.params.w * (1.0 - smoothstep(halo_k.x, halo_k.y, d_screen));
        result = (colour * body + soft_max(ring, ring_colour, halo, i.tint).rgb) * handover;
    }
    return float4(max(result, 0.0), 0.0);
}
