// Weapon effects, the sprite batch (docs/architecture/weapon-look.md 2.5, 2.14, 3.1: the hull hit's flash, sparks and
// heavy rings; renderer/weapon_effects_pass.h WeaponSpriteVertex, stride 48). Kinds by the colour's alpha:
// - a flash or a ring (alpha > 0): the stage expanded its quad on the CPU: a = the corner in clip space (ndc x z, ndc y z,
//   z - near z, z), b = (u, v, radiance, r) passed through; c.x 2 marks a ring (c.y its soft radius), 0 a flash (c.z
//   its core's share of the halo colour: 0 a white core, the beam spot's weapon-coloured one).
// - a streak (alpha 0, a spark; the performance pass moved its expansion here): a = its head (this frame's position,
//   view space) and its radiance, b = its tail (the previous frame's position through the previous frame's view) and
//   its width (view units), c = the corner (u -1 tail / +1 head, x the spark's rest-length factor; v -1 / +1 across)
//   and the radiance a diluted streak never drops below. Both ends through the frame's jittered projection to pixels;
//   the drawn length is the screen motion clamped to [rest, streak_cap x rest] (rest = max(spark_rest_w x the lit
//   width, floor_len_px) x |c.x|, the lit width max(its width in pixels, floor_lit_px)), the tail moved back along the
//   motion to it, the quad capped by half the lit width at both ends, and the radiance x rest / (rest + motion), not
//   below the hold. The stage's CPU twin is stage::streak_corners (the fixture measures through it).
// Compiled with tools/shaders/generate_weapon_effects_programs.py (vs_3_0).
float4 projection : register(c0); // m00, m11, m20, m21 of the jittered projection
float4 viewport : register(c1);   // W/2, H/2, 2/W, 2/H
float4 limits : register(c2);     // near view z, floor_lit_px, spark_rest_w, floor_len_px
float4 streak : register(c3);     // streak_cap, unused x3
struct Output {
    float4 position : POSITION;
    float4 shade : TEXCOORD0;   // u, v, radiance, radius (flash), band (ring) or width (streak), view units
    float4 colour : TEXCOORD1;  // linear colour, alpha: 0 a streak, else a flash's core fraction of its radius (a ring 1)
    float4 depth : TEXCOORD2;   // view z, the kind (2 a ring), a ring's soft radius, a flash core's colour share
};
float2 to_px(float3 v) {
    float2 ndc = projection.xy * v.xy / v.z + projection.zw;
    return float2(ndc.x + 1.0, 1.0 - ndc.y) * viewport.xy;
}
Output main(float4 a : POSITION, float4 b : TEXCOORD0, float3 c : TEXCOORD1, float4 colour : COLOR0) {
    Output o;
    o.colour = colour;
    if (colour.a > 0.5 / 255.0) {
        o.position = a;
        o.shade = b;
        o.depth = float4(a.w, c.x, c.y, c.z);
        return o;
    }
    float2 head = to_px(a.xyz), tail = to_px(b.xyz);
    float2 d = head - tail;
    float len2 = dot(d, d);
    float motion = sqrt(len2);
    float2 dir = len2 > 1e-6 ? d / motion : float2(1.0, 0.0);
    float lit = max(b.w * viewport.x * projection.x / a.z, limits.y);
    float rest = max(limits.z * lit, limits.w) * abs(c.x);
    float drawn = clamp(motion, rest, streak.x * rest);
    float hw = 0.5 * lit;
    float2 n = float2(-dir.y, dir.x) * hw;
    float2 p = (c.x > 0.0 ? head + dir * hw : head - dir * (drawn + hw)) + n * c.y;
    float z = c.x > 0.0 ? a.z : b.z;
    o.position = float4((p.x * viewport.z - 1.0) * z, (1.0 - p.y * viewport.w) * z, z - limits.x, z);
    o.shade = float4(c.x > 0.0 ? 1.0 : -1.0, c.y, max(a.w * rest / (rest + motion), c.z), b.w);
    o.depth = float4(z, 0.0, 0.0, 0.0);
    return o;
}
