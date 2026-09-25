// Effects stage, ripple decal (docs/architecture/effects-modernisation-opus.md 3.3, the hit with no owner box): one
// camera-facing quad per hit at the hit point P (effects_stage_core.h DecalVertex, stride 32), sized by the radius the
// record carries, built in view space so it faces the eye. Window-exact positions (w = 1).
// Compiled with tools/shaders/generate_rigid_motion_pixel.py (target vs_3_0).
float4 view_rows[3] : register(c0);   // world -> view
float4 projection : register(c3);     // m00, m11, m20, m21 of the jittered projection
float4 viewport : register(c4);       // W/2, H/2, 2/W, 2/H
float4 limits : register(c5);         // near view z, SOFT (soft radius as a multiple of the decal radius), unused, unused
struct Output {
    float4 position : POSITION;
    float4 raster : TEXCOORD0;   // window position (px), view z, soft radius
    float4 shade : TEXCOORD1;    // corner (-1..1)^2, age (seconds), alpha
};
float3 to_view(float3 p) { return float3(dot(p, view_rows[0].xyz) + view_rows[0].w, dot(p, view_rows[1].xyz) + view_rows[1].w, dot(p, view_rows[2].xyz) + view_rows[2].w); }
Output main(float4 p : POSITION, float4 corner : TEXCOORD0) {
    Output o;
    float3 v = to_view(p.xyz) + float3(corner.x * p.w, corner.y * p.w, 0.0);
    float3 centre = to_view(p.xyz);
    float z = max(v.z, limits.x);
    float2 ndc = float2(projection.x * v.x / z + projection.z, projection.y * v.y / z + projection.w);
    float2 at = (float2(1, -1) * ndc + 1.0) * viewport.xy;
    bool visible = centre.z >= limits.x && corner.w > 0.0;
    o.position = visible ? float4(at * viewport.zw * float2(1, -1) + float2(-1, 1), 0.5, 1.0) : float4(2.0, 2.0, 0.5, 1.0);
    o.raster = float4(at, centre.z, p.w * limits.y);
    o.shade = corner;
    return o;
}
