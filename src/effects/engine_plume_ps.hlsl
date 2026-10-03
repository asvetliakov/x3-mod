// Engine plumes (docs/architecture/engine-effects-modern.md sections 3, 4, "Plume look redesign" and "After flight C"):
// the pixel program of the stage's one indexed draw, ONE/ONE on the FP16 scene (alpha 0: the scene's alpha is
// untouched). The law is the Engine Exhaust Lab's plume() (tools/effects/engine_exhaust_lab.html, modern branch),
// analytic, no texture but the lane. Lengths are in nozzle widths n (local.w, world units: engine_plumes_core.h
// Look::nozzle_width x value): x along the axis from the nozzle, y across, r = |y|; u = x / L with L the pulsed length
// (the CPU pulses it per nozzle and frame). The constants come from engine_plumes_core.h Look through c3..c15 (one block,
// uploaded per frame).
// Body (the axial quad; the revised law of docs/architecture/engine-exhaust-look-critique.md section 3, "Implemented"):
// width w(u) (the mouth bulge, a near-cylinder tapering towards the tail, the tail narrowing), radial = r / w; the
// streak field S2 = fbm(2.2 p + (5, 2, 1)) - 0.4375, p = ((x - phase) 1.0, 4.5 y, seed + 0.7 t) (aspect 4.5 : 1 along
// the axis, advected by the nozzle's flow phase: shape.w, the frame's accumulator x the nozzle's flow_factor,
// engine_plumes_core.h nozzle_phase, so the world speed is one from value 500 up and capitals crawl). A peaked radial
// profile 0.08 + 0.92 exp(-(radial / 0.32)^2) plus the outer sheath 4 outer m (1 - m), m = smoothstep(0.25, 0.9, radial)
// (the tuning pass: the darker tint's layer carrying the visible width; c18.y, side view only) with a thin hot core
// exp(-(radial / (0.45 core))^2) (x (1 + 0.6 core),
// the core's boost cooling to half over smoothstep(0.2, 0.8, u): the axis at u 0.5 keeps its hue under AgX);
// the edge 1 - smoothstep(0.45, 1, radial + erosion), the erosion from the streaks growing along the plume
// (erode 1.6 S2 (0.6 + 0.8 u): tongues lick the outline, the tail frays); the tail's fade on u + tongue S2 (the end
// breaks into tongues); shock cells that carve the body, 1 - a (1 - c), c = (0.5 + 0.5 cos(2 pi u / period))^3,
// a = gap exp(-5 cfade u) smoothstep(0, period / 2, u) (1 - smoothstep(0.3, 0.9, radial)) s (crests at the body's
// level, dark gaps; ramped in over the first half period); the turbulence 1 + 2.2 turb S2 (0.4 + 0.6 radial)
// (a steady core, a boiling sheath); the heat in the core and the first 0.3 L only; the colour core -> tint ->
// darker tint: the head colour (peak.rgb) to the tint over smoothstep(0.2, 0.5, u), the tint darkening to 0.6 of
// itself over smoothstep(0.6, 1, u), across to 0.5 of the tint over smoothstep(0.25, 0.9, radial), white by the heat;
// the mouth ramp (after flight D): the body x (1 - dip (1 - smoothstep(0, ramp, u))), c16, so the nozzle reads no
// brighter than 0.85 of the body's peak at any throttle (the disc's samples carry the same factor in their tail column).
// Detail level (tint.a, the CPU's smoothstep(16, 40) of the projected nozzle width in px): the body is
// lerp(slab, revised, detail), the slab the previous law's smooth radial shape and cosine cells without streaks (a plume a few
// pixels wide stays TAA-stable when it moves and keeps the far dots' brightness), and the colour's rim and tail stops,
// the tongues scale with it; the halo's falloff along runs from the previous law's exp(-2.2 u) to exp(-4 u) (the CPU
// sets its e-fold and the disc's halo gain by the same level). The turbulence stays at every level.
// Halo: exp(-d / sigma) x exp(-4 u), d the distance to the segment nozzle..tip, sigma the nozzle's (0.32 halo, the
// preset's), a glow at the mouth, tapered to 0 over the last 0.5 sigma before the quad's reach (`look.z` sigma).
// Nozzle ring: a thin ring at the mouth, its Gaussian widened to the pixel footprint (energy kept).
// The halo's and the ring's radiances follow the body's throttle curve I(s) / I(1) (the CPU sets them).
// The mouth terms (the ring and the halo) combine as a soft maximum, (a^4 + b^4)^(1/4), not a sum; the body adds. Where
// the disc is drawn the axial quad hands its mouth over to it: x (1 - disc weight x (1 - smoothstep(0.3, 0.8, d))), d the
// screen-plane distance from the nozzle in nozzle widths.
// Disc (kind 1, the camera-facing nozzle disc; after flight C the end-on plume): the body integrated along the axis, the
// mean over the 8 samples u_k of c8..c15 (1 / w, tail, carve, heat) of the law at radial = rho / w_k (the hot centre,
// the peaked body, the cells carving rings in radial 0.3..0.9), its colour the samples' three stops weighted by their
// radiance; its streaks in polar form, the direction on a circle of radius 2.5 in the noise (seamless: no atan2 cut)
// and 4.5 rho - 1.6 phase radially (spokes scrolling outward, the flow along the line of sight), the erosion at the
// along-mean growth 1, the turbulence at radial 2 rho; the halo radially about the nozzle; the ring (its end-on
// radiance x Look::disc_ring, the CPU's; its Gaussian Look::disc_ring_width x the side's, c18.z); the total soft-capped, cap x (1 - exp(-total / cap)) (view.w; the CPU set the
// radiances and the cap, engine_plumes_core.h build_nozzle).
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
float4 fire_k : register(c4);     // 2 / period (the cells' ramp-in), erosion (erode x 1.6), turbulence (turb x 2.2), tail fade start
float4 cell_k : register(c5);     // the cells' gap depth min(1, 1.7 shock), 2 pi / period, 5 cfade, 1.2 tail
float4 core_k : register(c6);     // heat, 1 / (0.45 core) (the hot core's Gaussian), ring radius, ring sigma^2
float4 halo_k : register(c7);     // hand-over inner, outer (nozzle widths), the ring's axial falloff, 2 (params.z: I_ring / I_core / 2)
float4 disc_k[8] : register(c8);  // the disc's samples u_k = (k + 0.5) / 8: 1 / w, tail (with the mouth ramp), carve a (1 - c) (without the throttle), heat
float4 mouth_k : register(c16);   // the mouth ramp: dip, end (x L); the spill: glow_through, 1 / spill_depth (x value)
float4 spill_k : register(c17);   // the spill's inner and outer reach (nozzle widths, screen plane), 1 / spill_depth_max, the tail's tongues
float4 detail_k : register(c18);  // the previous law's cosine cell amplitude (shock) at the detail level 0, the outer sheath's
                                  // weight (4 outer), the end-on ring's radial scale (1 / disc_ring_width), unused
struct Input {
    float4 local : TEXCOORD0;     // x, y (world), L (pulsed, world; the disc: the ring's radiance), n (nozzle width, world)
    float4 shape : TEXCOORD1;     // halo sigma0 (nozzle widths), value, occlusion bias, the nozzle's flow phase (nozzle widths)
    float4 view : TEXCOORD2;      // the nozzle's view z, I_core, I_halo, the axis's view z component (the disc: the cap)
    float4 tint : TEXCOORD3;      // the mean colour: the tail's, the halo's and the ring's; A the detail level
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
    // The streak field S2 (zero mean): the axial quad's stretched 4.5 : 1 along the axis and translated by the nozzle's
    // flow phase; the disc's polar, the direction on a circle in the noise (seed and clock moving its centre) and the
    // radius scrolling outward with the flow along the line of sight.
    const float seed = i.params.y * 1861.5 + t * 0.7;
    const float2 dir = q * (1.0 / max(rho, 1e-4));
    const float3 p = disc ? float3(dir.x * 2.5 + seed, dir.y * 2.5, rho * 4.5 - phase * 1.6)
                          : float3(q.x - phase, q.y * 4.5, seed);
    const float S2 = fbm(p * 2.2 + float3(5.0, 2.0, 1.0)) - 0.4375;
    // The ring's Gaussian widened to the pixel footprint (fwidth in nozzle widths, outside the branch).
    const float aa = fwidth(q.y);
    const float s2 = core_k.w + 0.25 * aa * aa;
    const float ring_gain = sqrt(core_k.w / s2);
    // The detail level (the CPU's, by the projected nozzle width): 0 the slab law's smooth radial shape, 1 the revised
    // law's peaked profile and structure.
    const float3 tint = i.tint.rgb;
    const float detail = i.tint.a;
    const float3 ring_colour = lerp(tint, i.fog.rgb * float3(1.0, 0.95, 0.85), 0.6);
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
        // The body integrated along the axis: the law at the 8 samples; its colour accumulated per sample as the head
        // colour's, the tint's and white's weights (A, M, H; the along stops smoothstep(0.2, 0.5, u_k) and
        // 1 - 0.4 smoothstep(0.6, 1, u_k) are constants per sample).
        const float erosion = fire_k.y * S2;
        const float turbulence = 1.0 + fire_k.z * S2 * (0.4 + 0.6 * saturate(2.0 * rho));
        float B = 0.0, H = 0.0, A = 0.0, M = 0.0;
        [unroll] for (int k = 0; k < 8; ++k) {
            const float4 d = disc_k[k];
            const float uk = (k + 0.5) / 8.0;
            const float radial = rho * d.x;
            const float edge = 1.0 - smoothstep(0.45, 1.0, radial + erosion);
            const float rc = radial * core_k.y, rp = radial * (1.0 / 0.32);
            const float hot = exp(-rc * rc);
            const float b = d.y * edge * (1.0 - d.z * s * (1.0 - smoothstep(0.3, 0.9, radial))) * (0.08 + 0.92 * exp(-rp * rp)) *
                            (1.0 + 0.6 * hot * (1.0 - 0.5 * smoothstep(0.2, 0.8, uk)));
            const float bh = b * d.w * hot;
            const float rim = smoothstep(0.25, 0.9, radial), g = smoothstep(0.2, 0.5, uk);
            const float bc = (b - bh) * (1.0 - rim);
            B += b;
            H += bh;
            A += bc * (1.0 - g);
            M += bc * g * (1.0 - 0.4 * smoothstep(0.6, 1.0, uk)) + (b - bh) * 0.5 * rim;
        }
        // As the detail level falls below 1 the slab law's integrated profile takes over: a fit of the previous law's 8-sample mean at
        // s = 1 (plume_slab_disc_fit.py, rho scaled by the bulge), x 1.8 / 4.13 (its kappa over the current one: 0.448 x 1.8 / 4.13); its
        // colour the revised samples' mean hue.
        const float rs = rho * (0.575 / shape_k.x);
        const float slab = 0.1953 * (1.0 - smoothstep(0.24, 0.52, rs)) * (1.0 + 0.7 * (1.0 - smoothstep(0.0, 0.3, rs)));
        const float body = i.view.y * turbulence * soft_body * lerp(slab, 0.125 * B, detail);
        // The hue: the revised samples' three stops at detail 1; at 0 the head and the tint halved, white by the heat (no
        // rim or tail darkening, as the axial quad's slab).
        const float inv = B > 1e-6 ? 1.0 / B : 0.0;
        const float3 hue = lerp(lerp(lerp(i.peak.rgb, tint, 0.5), white, H * inv), B > 1e-6 ? (A * i.peak.rgb + M * tint + H * white) * inv : tint,
                                detail);
        const float3 coloured = body * hue;
        const float dn = rho / max(i.shape.x, 1e-4);
        const float halo = i.view.z * exp(-dn) * saturate(2.0 * (look.z - dn)) * soft_halo;
        // The end-on ring: its Gaussian disc_ring_width x the side's (rr x its inverse), a glowing rim that reaches into
        // the hot centre instead of a drawn outline over a dark annulus.
        const float rr = (rho - core_k.z) * detail_k.z;
        const float ring = i.local.z * ring_gain * exp(-rr * rr / (2.0 * s2)) * soft_body;
        const float4 mouth = soft_max(ring, ring_colour, halo, tint);
        const float total = body + mouth.w;
        const float cap = max(i.view.w, 1e-6);
        const float shown = cap * (1.0 - exp(-total / cap));
        result = total > 1e-20 ? (coloured + mouth.rgb) * (shown / total) : float3(0.0, 0.0, 0.0);
    } else {
        const float L = max(i.local.z, 1e-6) / n; // nozzle widths
        const float x = q.x, r = abs(q.y);
        const float u = x / L;
        const float uc = saturate(u);
        // Width: the bulge at the mouth, the line between cylinder and cone, the tail narrowing.
        const float b = shape_k.x * (1.0 - 0.55 * exp(-9.0 * uc)) * (1.0 + 0.35 * smoothstep(0.0, 0.25, uc) * exp(-4.0 * uc));
        const float w = min(shape_k.x + shape_k.y * uc, b + shape_k.z) * max(1.0 - shape_k.w * smoothstep(0.6, 1.0, uc), 0.05);
        const float radial = r / w;
        const float edge = 1.0 - smoothstep(0.45, 1.0, radial + fire_k.y * S2 * (0.6 + 0.8 * uc));
        const float tail = (1.0 - smoothstep(fire_k.w, 1.0, u + spill_k.w * S2 * detail)) * exp(-u * cell_k.w) *
                           (1.0 - mouth_k.x * (1.0 - smoothstep(0.0, mouth_k.y, u)));
        const float inside = (u >= 0.0 && u <= 1.0) ? 1.0 : 0.0;
        const float crest = 0.5 + 0.5 * cos(cell_k.y * u);
        const float envelope = exp(-u * cell_k.z) * smoothstep(0.0, 1.0, u * fire_k.x) * (1.0 - smoothstep(0.3, 0.9, radial)) * s;
        const float cells = 1.0 - cell_k.x * envelope * (1.0 - crest * crest * crest);
        const float rc = radial * core_k.y, rp = radial * (1.0 / 0.32);
        const float hot = exp(-rc * rc);
        const float turbulence = 1.0 + fire_k.z * S2 * (0.4 + 0.6 * saturate(radial));
        const float heat = core_k.x * hot * (1.0 - smoothstep(0.05, 0.3, u));
        const float3 tone = lerp(i.peak.rgb, tint * (1.0 - 0.4 * detail * smoothstep(0.6, 1.0, uc)), smoothstep(0.2, 0.5, uc));
        const float rim = smoothstep(0.25, 0.9, radial);
        const float3 colour = lerp(lerp(tone, 0.5 * tint, detail * rim), white, heat);
        // As the detail level falls below 1 the previous law takes over: its radial shape (the edge 0.55..1, the core
        // 1 + 0.6 (1 - smoothstep(0, 1.4 core, radial))) and its cosine cells 1 + shock cos(2 pi u / period) on the
        // carving's envelope (the fade, the half-period ramp, the radial mask 0.3..0.9, s), without the streaks.
        const float slab = (1.0 - smoothstep(0.55, 1.0, radial)) * (1.0 + 0.6 * (1.0 - smoothstep(0.0, 1.0, radial * core_k.y * 0.3214286))) *
                           (1.0 + detail_k.x * envelope * (2.0 * crest - 1.0));
        // The outer sheath (the tuning pass): the darker tint's layer over the same window as its colour, weight
        // detail_k.y, outside the lane and the hot core (0.06 of it at radial 0.35), so the visible width follows the
        // bulge and taper while the axis peak and the carved lane stay.
        const float peaked = edge * cells * (0.08 + 0.92 * exp(-rp * rp) + detail_k.y * rim * (1.0 - rim)) * (1.0 + 0.6 * hot * (1.0 - 0.5 * smoothstep(0.2, 0.8, uc)));
        const float body = i.view.y * tail * inside * lerp(slab, peaked, detail) * turbulence * soft_body;
        // Halo about the segment nozzle..tip, the nozzle's sigma; the window saturate((reach - d) / (0.5 sigma)) only
        // tapers its outer part.
        const float du = x - clamp(x, 0.0, L);
        const float dn = sqrt(du * du + r * r) / max(i.shape.x, 1e-4);
        const float halo = i.view.z * exp(-dn) * exp(-(2.2 + 1.8 * detail) * uc) * saturate(2.0 * (look.z - dn)) * soft_halo;
        const float rr = r - core_k.z;
        const float ring = i.view.y * i.params.z * halo_k.w * ring_gain * exp(-rr * rr / (2.0 * s2)) * exp(-max(x, 0.0) * halo_k.z) *
                           (x >= -0.05 ? 1.0 : 0.0) * soft_body;
        // The mouth's hand-over to the disc: inside the disc's footprint (the screen-plane distance from the nozzle,
        // (x sin(view), y)) the axial quad gives way by the disc's weight, so the two draws do not stack at the mouth.
        const float handover = 1.0 - i.params.w * (1.0 - smoothstep(halo_k.x, halo_k.y, d_screen));
        result = (colour * body + soft_max(ring, ring_colour, halo, tint).rgb) * handover;
    }
    return float4(max(result, 0.0), 0.0);
}
