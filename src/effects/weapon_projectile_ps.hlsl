// Weapon projectiles (docs/architecture/projectile-look.md section 2 and "Implementation (2026-11-08)"): one bolt as a
// streak of light, family-generic (every colour, width and length is the vertex's, from the row's look and its family's
// tuning). Across the axis two Gaussians, the core (sigma from its full width) and the glow; along it each is flat over
// the segment with logistic end ramps, Phi(x + h) - Phi(x - h) with Phi(u) = 1 / (1 + 2^(-m u)) and m = 2.45546 / sigma
// (1.702 / ln 2), the CPU's normalisation in the radiance scale: the half maximum sits at each end, and the previous
// frame's streak, ending where this one starts, adds to one flat streak (the two ramps telescope; FL 59 A). RT0 the FP16
// scene takes the radiance (alpha 0 under ONE/ONE keeps the scene's alpha); RT1 is the lane, masked to GREEN by the pass:
// the far flag 32 max(rgb) of the production B' route (docs/architecture/bolts-through-taa.md), so the resolve keeps
// the bolt out of the held history.
// Compiled with tools/shaders/generate_weapon_effects_programs.py (ps_3_0).
struct Input {
    float4 local : TEXCOORD0; // along, across, half length (px)
    float4 shape : TEXCOORD1; // core and glow radiance scales, their 1 / (2 sigma^2)
    float4 core : TEXCOORD2;  // core colour
    float4 glow : TEXCOORD3;  // glow colour
};
struct Output {
    float4 scene : COLOR0;
    float4 flag : COLOR1;
};
Output main(Input i) {
    float y2 = i.local.y * i.local.y;
    float2 m = 2.45546 * sqrt(2.0 * i.shape.zw); // 1 / (2 sigma^2) -> m / sigma (core, glow)
    float2 tail = 1.0 / (1.0 + exp2(min(-m * (i.local.x + i.local.z), 60.0)));
    float2 head = 1.0 / (1.0 + exp2(min(-m * (i.local.x - i.local.z), 60.0)));
    float2 along = tail - head;
    float3 c = i.core.rgb * (i.shape.x * exp(-y2 * i.shape.z) * along.x) +
               i.glow.rgb * (i.shape.y * exp(-y2 * i.shape.w) * along.y);
    Output o;
    o.scene = float4(c, 0.0);
    o.flag = (32.0 * max(c.r, max(c.g, c.b))).xxxx;
    return o;
}
