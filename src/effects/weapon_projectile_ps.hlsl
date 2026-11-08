// Weapon projectiles (docs/architecture/projectile-look.md section 2 and "Implementation (2026-11-08)"): one bolt as a
// line of light, family-generic (every colour, width and stretch is the vertex's, from the row's look and its family's
// tuning). The pixel's distance to the segment (its offset along the axis past the half length, and across it) feeds two
// Gaussians: the core (sigma from its full width) and the glow. RT0 the FP16 scene takes the radiance (alpha 0 under
// ONE/ONE keeps the scene's alpha); RT1 is the lane, masked to GREEN by the pass: the far flag 32 max(rgb) of the
// production B' route (docs/architecture/bolts-through-taa.md), so the resolve keeps the bolt out of the held history.
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
    float a = max(abs(i.local.x) - i.local.z, 0.0);
    float r2 = a * a + i.local.y * i.local.y;
    float3 c = i.core.rgb * (i.shape.x * exp(-r2 * i.shape.z)) + i.glow.rgb * (i.shape.y * exp(-r2 * i.shape.w));
    Output o;
    o.scene = float4(c, 0.0);
    o.flag = (32.0 * max(c.r, max(c.g, c.b))).xxxx;
    return o;
}
