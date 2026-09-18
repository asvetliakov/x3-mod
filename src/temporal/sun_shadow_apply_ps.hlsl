// Scene-end sun-shadow application (docs/architecture/legacy-sun-application.md,
// section 2): one quad over the jittered raster multiplying the FP16 scene
// target by 1 - (1 - f) s under ZERO/SRCCOLOR blending, where s is the sun
// share the route's programs wrote to RT2.g and f the 3x3 PCF visibility of
// the same frame's cascade-0 depth replay map. Per pixel: RT2 (.r = device
// z/w with the -1 sentinel, .g = share, .b = the interpolated clip w on the
// A32B32G32R32F lane) point-sampled at the pixel; view depth z = .b when
// limits.z > 0 (the wide RT2, docs/architecture/shadow-receiver-depth.md),
// else by the AO law z = m32 / (d - m22) (ao_linearize_ps.hlsl); NDC from the quad
// UV minus the frame's jitter (the latched m20/m21, as ao_gtao_ps.hlsl); the
// view position (x z / m00, y z / m11, z); sun-space NDC x, y and normalized
// depth through the replay frame's three view -> sun rows; the map UV snapped
// to the texel centre; receiver-plane depth bias from dsx/dsy of the sun depth
// against the map UV (clamped; across a depth discontinuity the plane fit is
// dropped and the receiver is pulled towards the light by the clamp value, so a
// silhouette pixel is not acne) plus a
// constant; nine taps of the 3x3 kernel rotated by the frame's jitter index,
// each snapped to its own texel centre so a planar receiver compares against
// its own extrapolated depth. A sentinel, share-free, off-cascade or fully lit
// pixel writes exactly 1 (the multiply leaves the target byte-identical); the
// exponent (1 for original shading, 1 / 2.2 for converted materials) is applied
// only to a shadowed factor. Alpha is 1 so the target's alpha is unchanged.
// Compiled by tools/shaders/generate_rigid_motion_pixel.py into
// src/renderer/sun_shadow_apply_program_inc.h.
// Texel convention: the replay rasterizes under D3D9, where map texel (i, j)
// holds the depth at screen position (i, j), i.e. at map position (i, j) / N,
// while a texture lookup addresses that texel at ((i, j) + 0.5) / N. The
// receiver's lookup position is therefore suv = muv + 0.5 / N: its nearest
// texel is floor(suv N) (= round(muv N)), and a tap at tapUV holds the depth
// of the point tapUV - suv away from the receiver (the receiver-plane term).
sampler depthShareTex : register(s0); // G32R32F or A32B32G32R32F: r = device depth (z/w, -1 sentinel), g = sun share s, b = view depth w (wide only)
sampler sunMapTex : register(s1);     // R32F sun-space depth map of the same frame
float4 view : register(c0);     // x = m00, y = m11, z = m20, w = m21 (the jittered projection latch)
float4 terms : register(c1);    // x = m22, y = m32, z = exponent, w = constant bias (normalized sun depth)
float4 sunRow0 : register(c2);  // view (x, y, z, 1) -> sun NDC x
float4 sunRow1 : register(c3);  // -> sun NDC y
float4 sunRow2 : register(c4);  // -> normalized sun depth
float4 map : register(c5);      // x = map size, y = 1 / size, z = cos, w = sin of the kernel rotation
float4 limits : register(c6);   // x = receiver-plane bias clamp and non-planar fallback, y = relative view-depth step that voids the plane fit, z = 1 when RT2.b carries the view depth (A32B32G32R32F), 0 for the z/w law

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float4 ds = tex2D(depthShareTex, uv);
    float d = ds.r, s = saturate(ds.g); // the share is a fraction; a producer fault above 1 must not zero the pixel
    float z = limits.z > 0.0 ? ds.b : terms.y / (d - terms.x);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 p = float4((ndc.x - view.z) * z / view.x, (ndc.y - view.w) * z / view.y, z, 1.0);
    float3 sun = float3(dot(p, sunRow0), dot(p, sunRow1), dot(p, sunRow2));
    float2 muv = float2(sun.x, -sun.y) * 0.5 + 0.5;
    // Derivatives before any branch (undefined under divergent control flow).
    float2 duvdx = ddx(muv), duvdy = ddy(muv);
    float dzdx = ddx(sun.z), dzdy = ddy(sun.z);
    float dvx = ddx(z), dvy = ddy(z);
    // Receiver plane: solve [duvdx; duvdy] g = [dzdx; dzdy] for g = (dz/du, dz/dv).
    float det = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
    bool planar = abs(det) > 1e-12 && abs(dvx) + abs(dvy) < limits.y * abs(z);
    float inv = planar ? 1.0 / det : 0.0;
    float2 g = float2(dzdx * duvdy.y - dzdy * duvdx.y, dzdy * duvdx.x - dzdx * duvdy.x) * inv;
    bool valid = d >= 0.0 && s > 0.0 && all(muv >= 0.0) && all(muv <= 1.0) && sun.z >= 0.0 && sun.z <= 1.0;
    float2 suv = muv + 0.5 * map.y;
    float2 texel = floor(suv * map.x);
    float lit = 0.0;
    [unroll] for (int j = -1; j <= 1; ++j) {
        [unroll] for (int i = -1; i <= 1; ++i) {
            float2 o = float2(map.z * i - map.w * j, map.w * i + map.z * j);
            float2 tapUV = (floor(texel + 0.5 + o) + 0.5) * map.y;
            float bias = planar ? clamp(dot(tapUV - suv, g), -limits.x, limits.x) : -limits.x;
            float reference = sun.z + bias - terms.w;
            lit += (tex2D(sunMapTex, tapUV).r >= reference) ? 1.0 : 0.0;
        }
    }
    float f = lit / 9.0;
    float base = saturate(1.0 - (1.0 - f) * s);
    float shadowed = base > 0.0 ? pow(base, terms.z) : 0.0;
    float factor = (valid && f < 1.0) ? shadowed : 1.0;
    return float4(factor, factor, factor, 1.0);
}
