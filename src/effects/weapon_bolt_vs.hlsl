// Weapon effects, step 4: the bolt capsule's vertex program (docs/architecture/bolt-muzzle-look.md 5.2; drawn in place
// of the late bullet draw, renderer/weapon_effects_pass.h WeaponBoltVertex, stride 128). The stage lays the velocity-
// stretch law on the CPU (weapon_effects_bolt_stage.h: the world segment, the eye-plane clip, both ends projected with
// their per-end widths and the floors, the streak to the previous head, the quad around the segment and the streak):
// each corner arrives in clip space with the game's depth law (z = m22 view z + m32, w = view z), so the depth test is
// the scene's own; the bolt's seven constant interpolants pass through. Compiled with generate_weapon_effects_programs.py
// (vs_3_0).
struct Output {
    float4 position : POSITION;
    float4 p0 : TEXCOORD0;
    float4 p1 : TEXCOORD1;
    float4 p2 : TEXCOORD2;
    float4 p3 : TEXCOORD3;
    float4 p4 : TEXCOORD4;
    float4 p5 : TEXCOORD5;
    float4 p6 : TEXCOORD6;
};
Output main(float4 clip : POSITION, float4 p0 : TEXCOORD0, float4 p1 : TEXCOORD1, float4 p2 : TEXCOORD2,
            float4 p3 : TEXCOORD3, float4 p4 : TEXCOORD4, float4 p5 : TEXCOORD5, float4 p6 : TEXCOORD6) {
    Output o;
    o.position = clip;
    o.p0 = p0;
    o.p1 = p1;
    o.p2 = p2;
    o.p3 = p3;
    o.p4 = p4;
    o.p5 = p5;
    o.p6 = p6;
    return o;
}
