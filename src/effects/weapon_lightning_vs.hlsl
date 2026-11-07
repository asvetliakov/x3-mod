// Weapon effects, the ion class's lightning segments (docs/architecture/hull-impact-classes.md 4.6; renderer/
// weapon_effects_pass.h WeaponLightningVertex, stride 64; the stage's stage::ion_segment_quad): a = the hit's contact
// point (view) and the segment's radiance, b = the segment's ends (u, v) in the hit's tangent plane, c and d = the
// stage's tangent axes (view units a plane unit) with the core's half width (px, signed: the corner's end) and the depth
// window (view units, signed: the corner's side).
// The plane's normal (review 2026-11-07, as the station decal's normal, weapon_shell_ps.hlsl): with the lane bound to
// vertex sampler 0 (limits.z), the lane's view points at the contact pixel and its four neighbours (weapon_soft_depth.
// hlsl's depth rule); per screen axis the neighbour nearer the contact's depth (a silhouette or a crease on one side),
// both within the depth window; their cross product turned towards the eye. The stage's axes are turned onto it by the
// smallest rotation taking the stage's normal (their cross product, towards the eye) to it, so the channels keep their
// directions. The stage's plane stays where the lane cannot give one: the contact off screen or behind the near plane,
// the lane's surface at the contact pixel not within the depth window of the contact (sky, another surface in front),
// no neighbour within it on an axis, a degenerate normal, or no vertex texture fetch (limits.z 0).
// Then the ends projected to pixels (VPOS space), the quad oriented along the segment in pixels and the corona's half
// width (limits.y x the core's) + 1 px past it on every side, each corner at its end's view depth; clip space from
// pixels (w = view z, z = view z - near z). An end behind the near plane collapses the quad (nothing drawn).
// Compiled with tools/shaders/generate_weapon_effects_programs.py (vs_3_0).
float4 projection : register(c0); // m00, m11, m20, m21 of the jittered projection
float4 viewport : register(c1);   // W/2, H/2, 2/W, 2/H
float4 limits : register(c2);     // near view z, ion_corona_x, the lane fetch (1 on), unused
float4 lane_form : register(c3);  // four_channel (1: .b is the view depth), m22, m32 (R32F: z = m32 / (d - m22)), unused
float4 lane_sizes : register(c4); // W, H, 1/W, 1/H of the lane
sampler2D lane_sampler : register(s0);
struct Output {
    float4 position : POSITION;
    float4 ends : TEXCOORD0;  // A, B in pixels
    float4 shape : TEXCOORD1; // core half width, z_A, z_B, depth window
    float4 e : TEXCOORD2;     // radiance, unused x3
};
// The lane's view depth at an integer pixel centre; ok false for the sky sentinel or an invalid depth.
float lane_z(float2 px, out bool ok) {
    float4 d = tex2Dlod(lane_sampler, float4((px + 0.5) * lane_sizes.zw, 0, 0));
    float z = lane_form.x > 0.5 ? d.b : lane_form.z / (d.r - lane_form.y);
    ok = d.r >= 0.0 && d.r <= 1.0 && z > 0.0 && z <= 3.402823466e38;
    return z;
}
float3 view_at(float2 px, float z) {
    float2 ndc = float2(px.x * viewport.z - 1.0, 1.0 - px.y * viewport.w);
    return float3((ndc.x - projection.z) / projection.x * z, (ndc.y - projection.w) / projection.y * z, z);
}
float2 to_px(float3 v) {
    float iz = 1.0 / v.z;
    return float2((projection.x * v.x * iz + projection.z + 1.0) * viewport.x,
                  (1.0 - projection.y * v.y * iz - projection.w) * viewport.y);
}
Output main(float4 a : POSITION, float4 b : TEXCOORD0, float4 c : TEXCOORD1, float4 d : TEXCOORD2) {
    Output o;
    float3 C = a.xyz, t1 = c.xyz, t2 = d.xyz, eye = -a.xyz;
    float hw = abs(c.w), tol = abs(d.w);
    float3 nc = cross(t1, t2);
    nc = (dot(nc, eye) < 0.0 ? -nc : nc) * rsqrt(max(dot(nc, nc), 1e-30));
    float3 ng = nc;
    [branch] if (limits.z > 0.5 && C.z > limits.x) {
        float2 p0 = floor(to_px(C) + 0.5);
        [branch] if (all(p0 >= 1.0) && all(p0 <= lane_sizes.xy - 2.0)) {
            bool o0, ol, orr, ou, od;
            float z0 = lane_z(p0, o0);
            float zl = lane_z(p0 - float2(1, 0), ol), zr = lane_z(p0 + float2(1, 0), orr);
            float zu = lane_z(p0 - float2(0, 1), ou), zd = lane_z(p0 + float2(0, 1), od);
            float el = ol ? abs(zl - z0) : 3.0e38, er = orr ? abs(zr - z0) : 3.0e38;
            float eu = ou ? abs(zu - z0) : 3.0e38, ed = od ? abs(zd - z0) : 3.0e38;
            [branch] if (o0 && abs(z0 - C.z) <= tol && min(el, er) <= tol && min(eu, ed) <= tol) {
                float3 v0 = view_at(p0, z0);
                float3 dx = er <= el ? view_at(p0 + float2(1, 0), zr) - v0 : v0 - view_at(p0 - float2(1, 0), zl);
                float3 dy = ed <= eu ? view_at(p0 + float2(0, 1), zd) - v0 : v0 - view_at(p0 - float2(0, 1), zu);
                float3 n = cross(dx, dy);
                float nn = dot(n, n);
                if (nn > 1e-30 && nn < 1e30) ng = (dot(n, eye) < 0.0 ? -n : n) * rsqrt(nn);
            }
        }
    }
    // the smallest rotation taking nc to ng (Rodrigues with k = nc x ng, |k| = sin): v cos + k x v + k (k . v) / (1 + cos)
    float cs = dot(nc, ng);
    [branch] if (cs < 0.9999995 && cs > -0.98) {
        float3 k = cross(nc, ng);
        float ik = 1.0 / (1.0 + cs);
        t1 = t1 * cs + cross(k, t1) + k * (dot(k, t1) * ik);
        t2 = t2 * cs + cross(k, t2) + k * (dot(k, t2) * ik);
    }
    float3 A = C + b.x * t1 + b.y * t2, B = C + b.z * t1 + b.w * t2;
    o.shape = float4(hw, A.z, B.z, tol);
    o.e = float4(a.w, 0.0, 0.0, 0.0);
    o.ends = 0.0;
    o.position = float4(0.0, 0.0, -1.0, 1.0);
    [branch] if (A.z > limits.x && B.z > limits.x) {
        float2 pa = to_px(A), pb = to_px(B), e = pb - pa;
        float len2 = dot(e, e);
        float2 along = len2 > 1e-6 ? e * rsqrt(len2) : float2(1.0, 0.0);
        float2 across = float2(-along.y, along.x);
        float q = limits.y * hw + 1.0;
        bool at_b = c.w > 0.0;
        float2 corner = (at_b ? pb : pa) + along * (at_b ? q : -q) + across * (d.w > 0.0 ? q : -q);
        float z = at_b ? B.z : A.z;
        o.ends = float4(pa, pb);
        o.position = float4((corner.x * viewport.z - 1.0) * z, (1.0 - corner.y * viewport.w) * z, z - limits.x, z);
    }
    return o;
}
