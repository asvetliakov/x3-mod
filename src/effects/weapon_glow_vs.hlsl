// Weapon effects, the glow primitive (docs/architecture/weapon-look.md 2.8; the hull hit's glow and ember): one quad
// per light, the light sphere's screen rectangle (renderer/weapon_effects_pass.h WeaponGlowVertex, stride 48: the
// corner in view space at the sphere's front, or the whole screen just past the near plane; the light's centre and R,
// its radiance colour, its lobes' rotation and seed), so each pixel is shaded once per light. The lobes' rotation's
// cosine and sine are taken here, once a vertex. Clip-space output (w = view z, z = view z - near z).
// Compiled with tools/shaders/generate_weapon_effects_programs.py (vs_3_0).
float4 projection : register(c0); // m00, m11, m20, m21 of the jittered projection
float4 limits : register(c2);     // near view z, unused x3
struct Output {
    float4 position : POSITION;
    float4 light : TEXCOORD0;
    float4 colour : TEXCOORD1;
    float4 lobe : TEXCOORD2; // cos, sin of the rotation, the seed (< 0: round), unused
};
Output main(float3 corner : POSITION, float4 light : TEXCOORD0, float3 colour : TEXCOORD1, float2 lobe : TEXCOORD2) {
    Output o;
    o.position = float4(projection.x * corner.x + projection.z * corner.z, projection.y * corner.y + projection.w * corner.z,
                        corner.z - limits.x, corner.z);
    o.light = light;
    o.colour = float4(colour, 0.0);
    float s, c;
    sincos(lobe.x, s, c);
    o.lobe = float4(c, s, lobe.y, 0.0);
    return o;
}
