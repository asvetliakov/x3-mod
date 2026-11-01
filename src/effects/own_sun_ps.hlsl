// Own sun (docs/architecture/own-sun-pass.md sections 2-4 and 14, step 1b): the pixel program of the stage's one quad,
// ONE/ONE into the FP16 scene after the fog pass and before the TAA resolve: a star seen from space, a tiny hot core with
// thin rays of irregular length radiating from it, two long horizontal streaks and a faint wide halo. p = the pixel's
// offset from the sun in core radii (the quad reaches 24 across and 16 up and down), r = |p|, theta = atan2(p.y, p.x):
//   core    D(r) = 1 - smoothstep(c3.w, 1, r), colour c3 (above the bloom threshold, under the meter's guard)
//   glow    tint c4 x [ halo + rays + streaks ], every term under the TAA stabiliser's emitter bound, behind the core:
//   halo    A_h / (1 + r^2 / rho_h^2) x W(r), W(r) = saturate(1 - (r / R_g)^2)^2: zero at the glow radius
//   rays    N spokes; t = theta N / 2 pi + offset, spoke k = floor(t + 0.5) mod N, a = t - floor(t + 0.5); per spoke the
//           hash h1 = frac(k (0.6180340 + 0.1547 k)) sets the length L_k = L_max (0.25 + 0.75 h1^2) (a few long,
//           many short) and h2 = frac(k 0.7548777 + 0.31) the strength 0.6 + 0.4 h2; across the spoke a Gaussian of the
//           perpendicular distance r (2 pi a / N) over a width w0 (1 - 0.7 r / L_k) that tapers outward; along it
//           saturate(1 - r / L_k)^2. The two nearest spokes are summed (the nearest and its neighbour on the pixel's
//           side, a - sign(a)), so the profile is continuous across a spoke boundary. Static in screen space.
//   streaks A_s exp(-(p.y / w_s)^2) exp(-|p.x| / L_s) x saturate(1 - (p.x / X_s)^2)^2: the long horizontal pair
//   radiance = lerp(glow, core, D) x open x (D + (1 - D) x visible)
//   open    = the nine taps (the fragment and the eight knight moves) of the scene depth lane (RT2 at s0, point sampled)
//             whose .r < 0 (the route's sentinel), / 9: the per-pixel clip over the whole quad, rays included
//   visible = the same sentinel test at 13 points of the core (its centre c6.xy, six at 0.45 and six at 0.9 core radii,
//             30 degrees apart from the inner ring; c6.zw the core radius as lane uv), weighted 1 - 0.1 rho^2 and
//             normalised (one tap moves it by at most 8.1 %, an outer one by 7.4 %, so an edge across the core under TAA
//             jitter no longer flickers the burst in 20 % steps; 17 taps cost 216 slots): a covered core takes its halo,
//             rays and streaks with it, as the engine's chain went with its probe
// The constants come from src/proxy/own_sun_core.h pixel_constants() (the host test evaluates the same law with them);
// colours carry the fog's far transmittance. Alpha 0: the scene's alpha is left as it is under ONE/ONE. Compiled with
// tools/shaders/generate_own_sun_programs.py (ps_3_0).
sampler2D lane_sampler : register(s0);
float4 texel : register(c0);     // 1/W, 1/H of the lane (the target's size), unused x2
float4 knight_d : register(c1);  // 2/W, 1/H, 1/W, 2/H: the knight offsets (+2, +1) and (+1, +2) as lane uv
float4 knight_w : register(c2);  // -2/W, 1/H, -1/W, 2/H: the knight offsets (-2, +1) and (-1, +2)
float4 core_rgb : register(c3);  // K lerp(c / m, 1, 0.6) m T, the core's inner edge (core radii)
float4 glow_rgb : register(c4);  // c T K / K_default, unused
float4 amplitude : register(c5); // A_h, A_r x sun_rays, A_s x sun_rays, unused
float4 core_taps : register(c6); // the core's centre (lane uv, jittered), the core radius as lane uv (x, y)
float4 spokes : register(c7);    // N / 2 pi, the spokes' offset (spoke units), L_max, (2 pi / N) / w0 sqrt(log2 e)
float4 extent : register(c8);    // 1 / rho_h^2, sqrt(log2 e) / w_s, log2 e / L_s, 1 / R_g (core radii): exp folded to exp2
float4 counts : register(c9);    // 1 / X_s, N, 1 / N, 2 pi / N
struct Input {
    float2 local : TEXCOORD0; // offset from the sun's centre in core radii
    float2 pixel : VPOS;
};
// tex2D, not tex2Dlod: no flow control here, the lane has one level and is point sampled with MIPFILTER NONE, so the
// implicit level is the only one (one slot and no coordinate move per tap; 14 taps).
float lane(float2 uv) {
    return tex2D(lane_sampler, uv).r;
}
float4 sentinel(float4 v) {
    return v < 0 ? 1.0 : 0.0;
}
// One spoke's ray at radius r, `across` spoke units from its axis (the index wraps modulo N for the hash).
float spoke(float index, float across, float r) {
    const float k = index - counts.y * floor(index * counts.z);
    const float h1 = frac(k * (0.6180340 + k * 0.1547));
    const float h2 = frac(k * 0.7548777 + 0.31);
    const float along = r / (spokes.z * (0.25 + 0.75 * h1 * h1));
    const float fade = saturate(1 - along);
    const float perpendicular = r * across * spokes.w / (1 - 0.7 * saturate(along)); // the taper is >= 0.3
    return (0.6 + 0.4 * h2) * exp2(-perpendicular * perpendicular) * fade * fade;
}
float4 main(Input i) : COLOR {
    const float2 base = (i.pixel + 0.5) * texel.xy;
    float4 a, b;
    a.x = lane(base + knight_d.xy);
    a.y = lane(base - knight_d.xy);
    a.z = lane(base + knight_w.xy);
    a.w = lane(base - knight_w.xy);
    b.x = lane(base + knight_d.zw);
    b.y = lane(base - knight_d.zw);
    b.z = lane(base + knight_w.zw);
    b.w = lane(base - knight_w.zw);
    const float open = (dot(sentinel(a) + sentinel(b), 1) + sentinel(lane(base)).x) / 9.0;
    // The core's visible fraction: its centre, six taps at 0.45 and six at 0.9 core radii, weighted 1 - 0.1 rho^2.
    const float2 c = core_taps.xy;
    float4 va, vb, ve;
    va.x = lane(c + float2(0.4500000, 0.0000000) * core_taps.zw);
    va.y = lane(c + float2(0.2250000, 0.3897114) * core_taps.zw);
    va.z = lane(c + float2(-0.2250000, 0.3897114) * core_taps.zw);
    va.w = lane(c + float2(-0.4500000, 0.0000000) * core_taps.zw);
    vb.x = lane(c + float2(-0.2250000, -0.3897114) * core_taps.zw);
    vb.y = lane(c + float2(0.2250000, -0.3897114) * core_taps.zw);
    vb.z = lane(c + float2(0.7794229, 0.4500000) * core_taps.zw);
    vb.w = lane(c + float2(0.0000000, 0.9000000) * core_taps.zw);
    ve.x = lane(c + float2(-0.7794229, 0.4500000) * core_taps.zw);
    ve.y = lane(c + float2(-0.7794229, -0.4500000) * core_taps.zw);
    ve.z = lane(c + float2(-0.0000000, -0.9000000) * core_taps.zw);
    ve.w = lane(c + float2(0.7794229, -0.4500000) * core_taps.zw);
    const float visible = sentinel(lane(c)).x * 0.0806940 + dot(sentinel(va) + float4(sentinel(vb).xy, 0, 0), 0.0790599) +
                          dot(float4(0, 0, sentinel(vb).zw) + sentinel(ve), 0.0741578); // weight / sum
    const float2 p = i.local;
    const float r2 = dot(p, p);
    const float r = sqrt(r2);
    const float d = 1 - smoothstep(core_rgb.w, 1.0, r);
    // Halo.
    const float w = saturate(1 - r2 * extent.w * extent.w);
    const float halo = amplitude.x / (1 + r2 * extent.x) * w * w;
    // Rays: theta by a minimax arctangent (|error| < 1e-5 rad), the nearest spoke, its hashed length and strength.
    const float2 q = abs(p);
    const float lo = min(q.x, q.y), hi = max(max(q.x, q.y), 1e-6);
    const float u = lo / hi, u2 = u * u;
    float theta = ((((-0.0134804 * u2 + 0.0574773) * u2 - 0.1212391) * u2 + 0.1956359) * u2 - 0.3329946) * u2 * u + 0.9999956 * u;
    theta = q.y > q.x ? 1.57079633 - theta : theta;
    theta = p.x < 0 ? 3.14159265 - theta : theta;
    theta = p.y < 0 ? -theta : theta;
    const float t = theta * spokes.x + spokes.y;
    const float nearest = floor(t + 0.5);
    const float across = t - nearest;
    const float side = across >= 0 ? 1 : -1;
    const float rays = amplitude.y * (spoke(nearest, across, r) + spoke(nearest + side, across - side, r));
    // Streaks: one exponential for both factors.
    const float sy = p.y * extent.y;
    const float sx = saturate(1 - p.x * p.x * counts.x * counts.x);
    const float streaks = amplitude.z * exp2(-(sy * sy + abs(p.x) * extent.z)) * sx * sx;
    const float3 glow = glow_rgb.rgb * (halo + rays + streaks);
    const float3 rgb = lerp(glow, core_rgb.rgb, d) * open * (d + (1 - d) * visible);
    return float4(rgb, 0);
}
