// Weapon effects, shield hit (docs/architecture/weapon-effects.md 5.1; the dropped effects stage's shell of 9375a1e7,
// effects-modernisation-opus.md 3.3): the vertex program of the shell. The proxy's unit icosphere (POSITION float3, 162
// vertices, 320 triangles) is placed on the hit object's shell in VIEW space, centre + A0 p.x + A1 p.y + A2 p.z (the
// owner's local axes scaled by its radius, taken to view space on the CPU in double so a far camera loses nothing),
// one draw per shell. The ellipsoid normal is A^-T p = sum A_k p_k / |A_k|^2; the unit-sphere point p is the local
// coordinate the hit slots are expressed in. Window-exact positions (w = 1). Compiled with
// tools/shaders/generate_weapon_effects_programs.py (vs_3_0).
float4 projection : register(c0);     // m00, m11, m20, m21 of the jittered projection routed draws use
float4 viewport : register(c1);       // W/2, H/2, 2/W, 2/H of the target
float4 centre : register(c2);         // view-space centre, near view z
float4 axes[3] : register(c3);        // view-space axes A0, A1, A2 (xyz); the soft radius (view units) in axes[0].w
struct Output {
    float4 position : POSITION;
    float4 raster : TEXCOORD0;   // window position (px), view z, soft radius
    float3 normal : TEXCOORD1;   // view-space ellipsoid normal (unnormalised)
    float3 local : TEXCOORD2;    // the unit-sphere point
    float3 view : TEXCOORD3;     // view-space position
};
Output main(float3 p : POSITION) {
    Output o;
    float3 v = centre.xyz + axes[0].xyz * p.x + axes[1].xyz * p.y + axes[2].xyz * p.z;
    float3 normal = axes[0].xyz * (p.x / max(dot(axes[0].xyz, axes[0].xyz), 1e-12)) + axes[1].xyz * (p.y / max(dot(axes[1].xyz, axes[1].xyz), 1e-12)) + axes[2].xyz * (p.z / max(dot(axes[2].xyz, axes[2].xyz), 1e-12));
    float z = max(v.z, centre.w);
    float2 ndc = float2(projection.x * v.x / z + projection.z, projection.y * v.y / z + projection.w);
    float2 at = (float2(1, -1) * ndc + 1.0) * viewport.xy;
    bool visible = v.z >= centre.w;
    o.position = visible ? float4(at * viewport.zw * float2(1, -1) + float2(-1, 1), 0.5, 1.0) : float4(2.0, 2.0, 0.5, 1.0);
    o.raster = float4(at, v.z, axes[0].w);
    o.normal = normal;
    o.local = p;
    o.view = v;
    return o;
}
