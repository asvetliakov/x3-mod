// Effects stage, shield hit (docs/architecture/effects-modernisation-opus.md 3.3): the vertex program of the shell.
// The proxy's unit icosphere (POSITION float3, 320 triangles) is placed on the hit ship's inflated ellipsoid
// world = centre + A0 p.x + A1 p.y + A2 p.z (the object -> world columns scaled by the box half-extents x 1.2), one
// draw per hit ship. The ellipsoid normal is A^-T p = sum A_k p_k / |A_k|^2, taken to view space for the Fresnel rim;
// the unit-sphere point p is the local coordinate the hit slots are expressed in. Window-exact positions (w = 1).
// Compiled with tools/shaders/generate_rigid_motion_pixel.py (target vs_3_0).
float4 view_rows[3] : register(c0);   // world -> view: view_j = dot(p, rows[j].xyz) + rows[j].w
float4 projection : register(c3);     // m00, m11, m20, m21 of the jittered projection routed draws use
float4 viewport : register(c4);       // W/2, H/2, 2/W, 2/H of the target
float4 centre : register(c5);         // world centre, near view z
float4 axes[3] : register(c6);        // world axes A0, A1, A2 of the inflated ellipsoid (xyz), soft radius (view units) in axes[0].w
struct Output {
    float4 position : POSITION;
    float4 raster : TEXCOORD0;   // window position (px), view z, soft radius
    float3 normal : TEXCOORD1;   // view-space ellipsoid normal (unnormalised)
    float3 local : TEXCOORD2;    // the unit-sphere point
    float3 view : TEXCOORD3;     // view-space position
};
float3 to_view(float3 p) { return float3(dot(p, view_rows[0].xyz) + view_rows[0].w, dot(p, view_rows[1].xyz) + view_rows[1].w, dot(p, view_rows[2].xyz) + view_rows[2].w); }
float3 rotate_view(float3 d) { return float3(dot(d, view_rows[0].xyz), dot(d, view_rows[1].xyz), dot(d, view_rows[2].xyz)); }
Output main(float3 p : POSITION) {
    Output o;
    float3 world = centre.xyz + axes[0].xyz * p.x + axes[1].xyz * p.y + axes[2].xyz * p.z;
    float3 normal = axes[0].xyz * (p.x / max(dot(axes[0].xyz, axes[0].xyz), 1e-12)) + axes[1].xyz * (p.y / max(dot(axes[1].xyz, axes[1].xyz), 1e-12)) + axes[2].xyz * (p.z / max(dot(axes[2].xyz, axes[2].xyz), 1e-12));
    float3 v = to_view(world);
    float z = max(v.z, centre.w);
    float2 ndc = float2(projection.x * v.x / z + projection.z, projection.y * v.y / z + projection.w);
    float2 at = (float2(1, -1) * ndc + 1.0) * viewport.xy;
    bool visible = v.z >= centre.w;
    o.position = visible ? float4(at * viewport.zw * float2(1, -1) + float2(-1, 1), 0.5, 1.0) : float4(2.0, 2.0, 0.5, 1.0);
    o.raster = float4(at, v.z, axes[0].w);
    o.normal = rotate_view(normal);
    o.local = p;
    o.view = v;
    return o;
}
