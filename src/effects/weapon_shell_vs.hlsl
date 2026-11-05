// Weapon effects, shield hit (docs/architecture/weapon-effects.md 5.1.1): the vertex program of the hex patch. The
// proxy's unit polar cap (POSITION float3 = (cos phi, sin phi, rho), renderer/weapon_effects_pass.cpp build_cap) is bent
// onto the owner's ellipsoid as the spherical cap the stage computed: the unit-sphere point p = cos(rho a) dir +
// sin(rho a) (cos phi u + sin phi v) (a the cap's half-angle, (u, v, dir) right-handed in the owner's frame), placed in
// VIEW space as centre + A0 p.x + A1 p.y + A2 p.z (the owner's local axes scaled by its radius, taken to view space on
// the CPU in double so a far camera loses nothing), one draw per patch. Only the cap is rasterized, so the cost follows
// the patch, not the owner. The ellipsoid normal is A^-T p = sum A_k p_k / |A_k|^2; p is the coordinate the pixel
// program builds the lattice from. Window-exact positions (w = 1). Compiled with
// tools/shaders/generate_weapon_effects_programs.py (vs_3_0).
float4 projection : register(c0);     // m00, m11, m20, m21 of the jittered projection routed draws use
float4 viewport : register(c1);       // W/2, H/2, 2/W, 2/H of the target
float4 centre : register(c2);         // view-space centre, near view z
float4 axes[3] : register(c3);        // view-space axes A0, A1, A2 (xyz); the soft radius (view units) in axes[0].w
float4 cap[3] : register(c6);         // the cap's direction (owner frame) and half-angle; its u; its v
struct Output {
    float4 position : POSITION;
    float4 raster : TEXCOORD0;   // window position (px), view z, soft radius
    float3 normal : TEXCOORD1;   // view-space ellipsoid normal (unnormalised)
    float3 local : TEXCOORD2;    // the unit-sphere point (owner frame)
    float3 view : TEXCOORD3;     // view-space position
};
Output main(float3 d : POSITION) {
    Output o;
    float s, c;
    sincos(d.z * cap[0].w, s, c);
    float3 p = cap[0].xyz * c + (cap[1].xyz * d.x + cap[2].xyz * d.y) * s;
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
