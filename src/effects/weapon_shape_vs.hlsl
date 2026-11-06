// Weapon effects, the shaped glows (docs/architecture/hull-impact-classes.md 3.5 the surface ring, 3.6 the filament glow):
// the glow primitive's quad (renderer/weapon_effects_pass.h WeaponShapeVertex, stride 92: the corner in view space at
// the box's front, or the whole screen just past the near plane; the box's centre and R, its radiance colour, the
// profile's terms and the owner's axes in view), so each pixel is shaded once per element. Clip-space output (w = view
// z, z = view z - near z). Compiled with tools/shaders/generate_weapon_effects_programs.py (vs_3_0).
float4 projection : register(c0); // m00, m11, m20, m21 of the jittered projection
float4 limits : register(c2);     // near view z, unused x3
struct Output {
    float4 position : POSITION;
    float4 light : TEXCOORD0;
    float4 colour : TEXCOORD1;
    float4 shape : TEXCOORD2;
    float4 ax : TEXCOORD3;
    float4 ay : TEXCOORD4;
    float4 az : TEXCOORD5;
};
Output main(float3 corner : POSITION, float4 light : TEXCOORD0, float3 colour : TEXCOORD1, float4 shape : TEXCOORD2,
            float3 ax : TEXCOORD3, float3 ay : TEXCOORD4, float3 az : TEXCOORD5) {
    Output o;
    o.position = float4(projection.x * corner.x + projection.z * corner.z, projection.y * corner.y + projection.w * corner.z,
                        corner.z - limits.x, corner.z);
    o.light = light;
    o.colour = float4(colour, 0.0);
    o.shape = shape;
    o.ax = float4(ax, 0.0);
    o.ay = float4(ay, 0.0);
    o.az = float4(az, 0.0);
    return o;
}
