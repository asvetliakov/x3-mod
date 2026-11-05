// Weapon effects, flat hex patch (docs/architecture/weapon-effects.md 5.1.1): one camera-facing quad of the patch radius
// at the hit point of an owner without a usable ellipsoid, view space (renderer/weapon_effects_pass.cpp DecalVertex,
// stride 32). Window-exact positions (w = 1). Compiled with tools/shaders/generate_weapon_effects_programs.py (vs_3_0).
float4 projection : register(c0);     // m00, m11, m20, m21 of the jittered projection
float4 viewport : register(c1);       // W/2, H/2, 2/W, 2/H
float4 limits : register(c2);         // near view z, unused x3
struct Output {
    float4 position : POSITION;
    float4 raster : TEXCOORD0;   // window position (px), view z, soft radius
    float4 shade : TEXCOORD1;    // corner (-1..1)^2, unused, alpha
};
Output main(float4 p : POSITION, float4 corner : TEXCOORD0) {
    Output o;
    float3 v = p.xyz + float3(corner.x * p.w, corner.y * p.w, 0.0);
    float z = max(v.z, limits.x);
    float2 ndc = float2(projection.x * v.x / z + projection.z, projection.y * v.y / z + projection.w);
    float2 at = (float2(1, -1) * ndc + 1.0) * viewport.xy;
    bool visible = p.z >= limits.x && corner.w > 0.0;
    o.position = visible ? float4(at * viewport.zw * float2(1, -1) + float2(-1, 1), 0.5, 1.0) : float4(2.0, 2.0, 0.5, 1.0);
    o.raster = float4(at, p.z, corner.z);
    o.shade = float4(corner.xy, 0.0, corner.w);
    return o;
}
