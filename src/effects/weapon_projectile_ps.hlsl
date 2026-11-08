// Weapon projectiles (docs/architecture/projectile-look.md section 2 and "Implementation (2026-11-08)"): one segment of a
// bolt's comet as a streak of light, family-generic (every colour, width and length is the vertex's, from the row's look
// and its family's tuning). Across the axis two Gaussians, the core and the glow, exp2(-q^2) with q the core's across
// offset (linear over the quad, sigma sqrt(2 / ln 2) per unit) and the glow's q r (r = sigma_core / sigma_glow); along it
// each is flat over the segment with logistic end ramps, Phi(x + h) - Phi(x - h) with Phi = 1 / (1 + 2^u), the core's
// exponents u = -m (x +- h) linear over the quad (m = 2.45546 / sigma, 1.702 / ln 2) and the glow's u r: the half maximum
// sits at each end and two segments sharing an end add to one streak (the ramps telescope; FL 59 A). Both are times the
// trail's falloff F = saturate((8 2^e - 1) / 7), e = -3 age / trail_ms linear along the segment (1 at the head, 0 at
// trail_ms; e = 0 without a trail: F = 1, FL 60 A). The head segment's core adds the head, 4 a (Phi(x - h + L) -
// Phi(x - h)) over the last L = 1.5 core widths (m L = 8.67327), a the core colour's alpha. RT0 the FP16 scene takes the
// radiance (alpha 0 under ONE/ONE keeps the scene's alpha), a pixel under 1/256 of scene white (the quads' radiance cut)
// is discarded; RT1 is the lane, masked to GREEN by the pass: the far flag
// 32 max(rgb) of the production B' route (docs/architecture/bolts-through-taa.md), so the resolve keeps the bolt out of
// the held history.
// Compiled with tools/shaders/generate_weapon_effects_programs.py (ps_3_0).
struct Input {
    float4 local : TEXCOORD0; // q, the core's tail and head ramp exponents, the falloff exponent
    float3 shape : TEXCOORD1; // core and glow radiance scales, sigma_core / sigma_glow
    float4 core : TEXCOORD2;  // core colour, the head's gain / 4
    float4 glow : TEXCOORD3;  // glow colour
};
struct Output {
    float4 scene : COLOR0;
    float4 flag : COLOR1;
};
Output main(Input i) {
    const float2 k = float2(1.0, i.shape.z);
    const float2 q = i.local.xx * k;
    const float2 across = exp2(-q * q);
    const float2 tail = 1.0 / (1.0 + exp2(min(i.local.y * k, 60.0)));
    const float2 head = 1.0 / (1.0 + exp2(min(i.local.z * k, 60.0)));
    const float2 along = (tail - head) * saturate(exp2(i.local.w) * (8.0 / 7.0) - 1.0 / 7.0);
    const float window = 1.0 / (1.0 + exp2(min(i.local.z - 8.67327, 60.0))) - head.x;
    float3 c = i.core.rgb * (i.shape.x * across.x * (along.x + 4.0 * i.core.a * window)) +
               i.glow.rgb * (i.shape.y * across.y * along.y);
    const float peak = max(c.r, max(c.g, c.b));
    clip(peak - 1.0 / 256.0); // under the quads' radiance cut: no blend, no flag (FL 60 A's budget: the trail's dim area)
    Output o;
    o.scene = float4(c, 0.0);
    o.flag = (32.0 * peak).xxxx;
    return o;
}
