// Weapon effects, the fireball impostor (docs/architecture/weapon-look.md 2.14, 3.1 the hull hit's hot-gas burst and
// its smoke, docs/architecture/hull-burst-look.md section 2; 3.2 the explosions at full size): one quad per fireball, the
// sphere's screen rectangle (renderer/weapon_effects_pass.h WeaponFireballVertex, stride 76: the corner in view space at
// the sphere's front, or the whole screen just past the near plane; the sphere's view-space centre and radius; its
// look; its tint and share; its disc: rotation, lobe seed, advection and the third octave's weight; stride 80 since step
// 3: the white-hot core's gain; stride 92 since the fire material, docs/architecture/fire-material.md: the core's gain,
// the fire weight, the size LOD and the game-time age in ms as one float4), so each pixel is
// shaded once per fireball. The disc rotation's cosine and sine are taken here, once a vertex. Clip-space output (w = view z,
// z = view z - near z). Compiled with tools/shaders/generate_weapon_effects_programs.py (vs_3_0).
float4 projection : register(c0); // m00, m11, m20, m21 of the jittered projection
float4 limits : register(c2);     // near view z, unused x3
struct Output {
    float4 position : POSITION;
    float4 sphere : TEXCOORD0;
    float4 look : TEXCOORD1;
    float4 tint : TEXCOORD2;
    float4 disc : TEXCOORD3; // cos theta, sin theta, lobe seed, the third octave's weight
    float advect : TEXCOORD4;
    float4 core : TEXCOORD5; // the white-hot core's gain, the fire weight, the size LOD, the age (game ms)
};
Output main(float3 corner : POSITION, float4 sphere : TEXCOORD0, float4 look : TEXCOORD1, float4 tint : TEXCOORD2,
            float4 disc : TEXCOORD3, float4 core : TEXCOORD4) {
    Output o;
    o.position = float4(projection.x * corner.x + projection.z * corner.z, projection.y * corner.y + projection.w * corner.z,
                        corner.z - limits.x, corner.z);
    o.sphere = sphere;
    o.look = look;
    o.tint = tint;
    float s, c;
    sincos(disc.x, s, c);
    o.disc = float4(c, s, disc.y, disc.w);
    o.advect = disc.z;
    o.core = core;
    return o;
}
