// Effects stage, projectiles (docs/architecture/effects-modernisation-opus.md 3.1): the vertex program of the bolt
// draw. Four vertices per instance (effects_stage_core.h BoltVertex, stride 64); the program builds one camera-facing
// streak per instance in window space from the world centroid, the world axis and extents the CPU derived from the
// bullet buffer, and the previous-frame position (centre - velocity) of a matched instance: the capsule runs from the
// previous position to the current one, its length max(native, L_min) plus the streak, its core radius
// max(native half-width, W_min / 2), the halo HALO x wider. Positions are window-exact (w = 1) like the dust motes;
// an instance behind the camera plane collapses to one point outside the clip volume.
// Compiled with tools/shaders/generate_rigid_motion_pixel.py (target vs_3_0).
float4 view_rows[3] : register(c0);   // world -> view: view_j = dot(p, rows[j].xyz) + rows[j].w
float4 projection : register(c3);     // m00, m11, m20, m21 of the jittered projection routed draws use
float4 viewport : register(c4);       // W/2, H/2, 2/W, 2/H of the target
float4 footprint : register(c5);      // W_min (px, full width), L_min (px, full length), HALO (core radius multiple), k_stretch
float4 limits : register(c6);         // near view z, SOFT (soft radius as a multiple of the core's world radius), unused, unused
struct Output {
    float4 position : POSITION;
    float4 raster : TEXCOORD0;   // window position (px), capsule coordinates (u along from the centre, w across)
    float4 capsule : TEXCOORD1;  // L_back (px, towards the previous position), L_front (px), core radius r (px), halo radius R (px)
    float4 shade : TEXCOORD2;    // view z of the centre, alpha, soft radius (view units), unused
    float2 uv : TEXCOORD3;       // the instance's UV centroid on the bullet atlas
};
float3 to_view(float3 p) { return float3(dot(p, view_rows[0].xyz) + view_rows[0].w, dot(p, view_rows[1].xyz) + view_rows[1].w, dot(p, view_rows[2].xyz) + view_rows[2].w); }
float2 to_window(float3 v) {
    float z = max(v.z, limits.x);
    float2 ndc = float2(projection.x * v.x / z + projection.z, projection.y * v.y / z + projection.w);
    return (float2(1, -1) * ndc + 1.0) * viewport.xy;
}
Output main(float4 centre : POSITION, float4 axis : TEXCOORD0, float4 shape : TEXCOORD1, float4 motion : TEXCOORD2) {
    Output o;
    float3 v = to_view(centre.xyz);
    float3 tip = to_view(centre.xyz + axis.xyz * axis.w);
    float3 previous = to_view(centre.xyz - motion.xyz);
    float2 c = to_window(v), t = to_window(tip), p = to_window(previous);
    // The screen axis: the projected world axis, or the streak direction when the instance moved on screen.
    float2 along = t - c; float native_half = length(along);
    float2 streak = c - p; float travel = length(streak) * footprint.w;
    float2 e = native_half > 1e-3 ? along / native_half : float2(1, 0);
    if (travel > 1.0) e = streak / max(length(streak), 1e-6);
    float2 n = float2(-e.y, e.x);
    // Focal length in pixels: the half-width projects as world / z x m00 x W/2.
    float focal = projection.x * viewport.x;
    float half_width_px = shape.x * focal / max(v.z, limits.x);
    float r = max(half_width_px, 0.5 * footprint.x);
    float R = r * footprint.z;
    float half_len = max(native_half, 0.5 * footprint.y);
    float L_front = half_len, L_back = half_len + travel;
    float u = shape.y > 0.0 ? L_front + R : -(L_back + R);
    float w = shape.z * R;
    float2 at = c + e * u + n * w;
    bool visible = v.z >= limits.x && shape.w > 0.0;
    o.position = visible ? float4(at * viewport.zw * float2(1, -1) + float2(-1, 1), 0.5, 1.0) : float4(2.0, 2.0, 0.5, 1.0);
    o.raster = float4(at, u, w);
    o.capsule = float4(L_back, L_front, r, R);
    o.shade = float4(v.z, shape.w, max(shape.x, axis.w * 0.25) * limits.y, 0.0);
    o.uv = float2(motion.w, centre.w);
    return o;
}
