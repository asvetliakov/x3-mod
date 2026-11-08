// Weapon projectiles (docs/architecture/projectile-look.md, "Implementation (2026-11-08)"): the pass-through of the
// CPU-built screen-space quads (src/proxy/weapon_projectile_stage.h, renderer::WeaponProjectileVertex, stride 48). The
// position is window-exact clip space (FLOAT3, w = 1, z the head's depth); the four attributes (FLOAT4, FLOAT3 and two
// D3DCOLOR) go to weapon_projectile_ps.hlsl unchanged. Compiled with tools/shaders/generate_weapon_effects_programs.py (vs_3_0).
struct Vertex {
    float4 position : POSITION;
    float4 local : TEXCOORD0; // linear over the quad: the core's across q, its tail and head ramp exponents, the falloff's
    float3 shape : TEXCOORD1; // core radiance scale, glow radiance scale, sigma_core / sigma_glow
    float4 core : TEXCOORD2;  // core colour (rgb, D3DCOLOR), alpha the head's gain / 4
    float4 glow : TEXCOORD3;  // glow colour (rgb, D3DCOLOR)
};
Vertex main(Vertex v) {
    return v;
}
