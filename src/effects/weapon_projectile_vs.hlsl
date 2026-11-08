// Weapon projectiles (docs/architecture/projectile-look.md, "Implementation (2026-11-08)"): the pass-through of the
// CPU-built screen-space quads (src/proxy/weapon_projectile_stage.h, renderer::WeaponProjectileVertex, stride 48). The
// position is window-exact clip space (FLOAT3, w = 1, z the end's depth); the four attributes (FLOAT3, FLOAT4 and two
// D3DCOLOR) go to weapon_projectile_ps.hlsl unchanged. Compiled with tools/shaders/generate_weapon_effects_programs.py (vs_3_0).
struct Vertex {
    float4 position : POSITION;
    float4 local : TEXCOORD0; // along, across (px about the segment's centre), the along profile's half length (px)
    float4 shape : TEXCOORD1; // core radiance scale, glow radiance scale, 1 / (2 sigma_core^2), 1 / (2 sigma_glow^2)
    float4 core : TEXCOORD2;  // core colour (rgb, D3DCOLOR)
    float4 glow : TEXCOORD3;  // glow colour (rgb, D3DCOLOR)
};
Vertex main(Vertex v) {
    return v;
}
