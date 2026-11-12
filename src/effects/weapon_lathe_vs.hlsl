// Weapon effects, shield hit (docs/architecture/weapon-effects.md 5.1.1 "Placement", 2026-11-12): the vertex program of
// the lathe bubble. A lathe draw is a band of the owner's profile lathe (renderer/weapon_effects_pass.h
// WeaponLatheVertex: the owner-frame point in view units and its chart coordinates u, v'; built once per ship type by
// the stage, src/proxy/weapon_effects_lathe.h): the view point is centre + sum_k L_k p_k (the owner's local axes in
// view, taken on the CPU in double, its centre too), clip as the shell's (w = view z, z = view z - near z: the
// rasterizer clips at the near plane). The pixel program gets the chart point (u, v' x period, view units) and the view
// z. Compiled with tools/shaders/generate_weapon_effects_programs.py (vs_3_0).
float4 projection : register(c0);     // m00, m11, m20, m21 of the jittered projection routed draws use
float4 viewport : register(c1);       // unused by the lathe
float4 centre : register(c2);         // the owner's view-space centre, near view z
float4 axes[3] : register(c3);        // the owner's local axes in view (xyz)
float4 chart : register(c6);          // the chart's period (view units), unused x3
struct Output {
    float4 position : POSITION;
    float4 chart_z : TEXCOORD0;       // chart u, chart v, view z, 0
};
Output main(float3 p : POSITION, float2 s : TEXCOORD0) {
    float3 v = centre.xyz + axes[0].xyz * p.x + axes[1].xyz * p.y + axes[2].xyz * p.z;
    Output o;
    o.position = float4(projection.x * v.x + projection.z * v.z, projection.y * v.y + projection.w * v.z, v.z - centre.w, v.z);
    o.chart_z = float4(s.x, s.y * chart.x, v.z, 0.0);
    return o;
}
