// Ambient occlusion chain, pass 2: the half-resolution GTAO horizon search
// (docs/architecture/ambient-occlusion.md, section 2; XeGTAO-style). Input is
// the half-resolution scale-free view depth zs = 1 / (m22 - d) of
// ao_linearize_ps.hlsl (-1 sentinel), which is the view depth divided by the
// constant |m32|; the caller scales the radius and the falloff constants by
// the same factor, so nothing here changes (the pixels-per-view-unit factor
// is a ratio, normals are normalized and every depth test is relative).
// Per pixel: the view position from the folded ray terms, a normal from the
// min-difference of the four depth neighbours (5 taps), 2 slices x 2 sides x 4
// steps = 16 depth taps, cosine-weighted horizon integral with a distance
// falloff, radius in view units capped in half-resolution pixels. Horizons
// are measured as each tap's elevation above the reconstructed tangent plane
// (see horizonTap), a choice over XeGTAO's angle from the view vector: with
// texel-quantized taps that angle leaves the slice plane and an unoccluded
// plane reads below 1 (float64 reference at 1280x768: mean 0.9969, minimum
// 0.8623 at the borders), while the elevation is exactly 0 on the surface. Every tap
// samples the centre of the texel it names, so the CPU reference of
// verification/probe/ambient_occlusion_reference.h reproduces the same texel
// choices. Per-slice results are normalized by the slice's unoccluded value
// (cos n + n sin n) and weighted by the projected normal length, so an
// unoccluded plane yields exactly 1 in every slice (the flat-plane identity
// the fixture asserts). Output is the occlusion 1 - visibility (see the end).
// Sentinel centre -> 0; sentinel taps are the far plane
// (never occluders). The 4x4 noise is the Bayer index of the pixel, rotated by
// the caller's integer jitter index. The slice loop stays rolled.
// Compiled into src/renderer/ambient_occlusion_gtao_program_inc.h.
sampler depthTex : register(s0); // half-resolution R32F linear depth (-1 sentinel)
float4 size : register(c0);      // xy = 1 / half width/height, zw = half width/height
float4 ray : register(c1);       // view ray of a half pixel: (x * ray.x + ray.z, y * ray.y + ray.w, 1) * z
float4 radius : register(c2);    // x = radius (scaled view units), y = falloff mul, z = falloff add, w = max radius (half-res px)
float4 noise : register(c3);     // x = integer rotation, y = m11 * half height / 2 (px per view unit at z = 1)
float4 halfUV : register(c7);    // xy = 0.5 / half width/height (the texel-centre offset), zw = half width/height - 1

static const float HALF_PI = 1.57079633;

// The texel's depth, sampled at its centre in one mad from the pixel.
float depthAt(float2 pixel) {
    return tex2D(depthTex, pixel * size.xy + halfUV.xy).r;
}
float3 viewPosition(float2 pixel, float z) {
    return float3(pixel.x * ray.x + ray.z, pixel.y * ray.y + ray.w, 1.0) * z;
}
// One horizon tap: the texel at pixel + offset (clamped to the image), sampled
// at its centre. The tap's occlusion is its elevation above the reconstructed
// tangent plane, sin e = dot(delta, normal) / |delta| (0 for any tap on the
// surface itself, so a plane is never its own occluder under texel-quantized
// tap positions), faded by the distance falloff; the side keeps the maximum.
// A sentinel contributes nothing, nor does the centre texel itself: its
// delta is float32 noise (~1e-7 of the position), so the cut is relative to
// the depth (minDist = 1e-4 z, far below one texel's ~3e-3 z).
float horizonTap(float2 pixel, float2 offset, float3 centre, float3 normal, float minDist, float elevation) {
    float2 texel = clamp(floor(pixel + 0.5 + offset), 0.0, halfUV.zw);
    float z = depthAt(texel);
    float3 delta = viewPosition(texel, z) - centre;
    float dist = length(delta);
    float sine = saturate(dot(delta, normal) / max(dist, 1e-12)) * saturate(dist * radius.y + radius.z);
    return (z >= 0.0 && dist > minDist) ? max(elevation, sine) : elevation;
}

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float2 pixel = floor(uv * size.zw);
    float zc = depthAt(pixel);
    if (zc < 0.0) return float4(0.0, 0.0, 0.0, 0.0);
    float3 centre = viewPosition(pixel, zc);
    // Normal: the neighbour with the smaller depth difference on each axis;
    // off-image or sentinel neighbours lose, a lone invalid axis stays in-plane.
    float zl = depthAt(pixel + float2(-1.0, 0.0)), zr = depthAt(pixel + float2(1.0, 0.0));
    float zu = depthAt(pixel + float2(0.0, -1.0)), zd = depthAt(pixel + float2(0.0, 1.0));
    bool lv = zl >= 0.0 && pixel.x >= 1.0, rv = zr >= 0.0 && pixel.x <= halfUV.z - 1.0;
    bool uv_ = zu >= 0.0 && pixel.y >= 1.0, dv = zd >= 0.0 && pixel.y <= halfUV.w - 1.0;
    bool useR = rv && (!lv || abs(zr - zc) <= abs(zc - zl));
    bool useD = dv && (!uv_ || abs(zd - zc) <= abs(zc - zu));
    float zx = useR ? zr : (lv ? zl : zc);
    float zy = useD ? zd : (uv_ ? zu : zc);
    float2 ox = float2(useR || !lv ? 1.0 : -1.0, 0.0);
    float2 oy = float2(0.0, useD || !uv_ ? 1.0 : -1.0);
    float3 dx = (viewPosition(pixel + ox, zx) - centre) * ox.x;
    float3 dy = (viewPosition(pixel + oy, zy) - centre) * oy.y;
    float3 normal = normalize(cross(dx, dy));
    float3 view = normalize(-centre);
    if (dot(normal, view) < 0.0) normal = -normal;
    float radiusPx = clamp(radius.x * noise.y / zc, 2.0, radius.w);
    float minDist = zc * 1e-4;
    // Bayer 4x4 index of the pixel (0..15), rotated by the frame's jitter index.
    float x0 = pixel.x - 2.0 * floor(pixel.x * 0.5), y0 = pixel.y - 2.0 * floor(pixel.y * 0.5);
    float x1 = floor(pixel.x * 0.5) - 2.0 * floor(pixel.x * 0.25), y1 = floor(pixel.y * 0.5) - 2.0 * floor(pixel.y * 0.25);
    float bayer = (x0 + y0 - 2.0 * x0 * y0) * 8.0 + y0 * 4.0 + (x1 + y1 - 2.0 * x1 * y1) * 2.0 + y1;
    float sliceNoise = frac((bayer + noise.x) / 16.0);
    float stepIndex = bayer * 5.0 + noise.x * 3.0;
    float stepNoise = (stepIndex - 16.0 * floor(stepIndex / 16.0) + 0.5) / 16.0;
    float occluded = 0.0, weightSum = 0.0;
    [loop] for (int slice = 0; slice < 2; ++slice) {
        float phi = (float(slice) + sliceNoise) * HALF_PI;
        float2 dir = float2(cos(phi), sin(phi));
        float3 dirView = float3(dir.x, -dir.y, 0.0);
        float3 ortho = dirView - dot(dirView, view) * view;
        float3 axis = normalize(cross(ortho, view));
        float3 projected = normal - axis * dot(normal, axis);
        float projectedLength = max(length(projected), 1e-6);
        float cosN = saturate(dot(projected, view) / projectedLength);
        float sign = dot(ortho, projected) >= 0.0 ? 1.0 : -1.0;
        float n = sign * acos(cosN);
        float sinN = sign * sqrt(saturate(1.0 - cosN * cosN)); // sin(n) without a second sincos
        // Elevation sines of the two sides; the horizon angles from the view
        // vector are h1 = n + acos(e0) (side +dir, sweeping down from the
        // tangent at n + pi/2) and h0 = n - acos(e1), both inside n +- pi/2 by
        // construction, and cos(2h - n) expands through e without a sincos.
        float elevation0 = 0.0, elevation1 = 0.0;
        [unroll] for (int step = 0; step < 4; ++step) {
            float2 offset = dir * ((float(step) + stepNoise) * 0.25 * radiusPx);
            elevation0 = horizonTap(pixel, offset, centre, normal, minDist, elevation0);
            elevation1 = horizonTap(pixel, -offset, centre, normal, minDist, elevation1);
        }
        // The slice's missing arc, not its visible arc: with the horizon
        // angles h1 = n + acos(e0) and h0 = n - acos(e1), the two cosine-
        // weighted arcs (cos n + 2 h sin n - cos(2h - n)) / 4 sum to
        //   unoccluded - deficit,  deficit = cos n / 2 - (a0 - a1) sin n / 2
        //                                    + (cos(2h0 - n) + cos(2h1 - n)) / 4
        // with a0 = acos(e0), a1 = acos(e1) and unoccluded = cos n + n sin n.
        // Written this way the unoccluded case is exact in float: e0 = e1 = 0
        // gives a0 - a1 = 0 and cos(2h - n) = -cos n on both sides, so the
        // deficit is cos n / 2 - cos n / 2 = 0 under any association (every
        // term is a power-of-two multiple of cos n). The visible-arc form
        // cancelled only to within a rounding step, which left single-ulp
        // occlusion on parts of a flat plane.
        float a0 = acos(elevation0), a1 = acos(elevation1);
        float cos2h1n = cosN * (2.0 * elevation0 * elevation0 - 1.0) - sinN * 2.0 * elevation0 * sqrt(saturate(1.0 - elevation0 * elevation0));
        float cos2h0n = cosN * (2.0 * elevation1 * elevation1 - 1.0) + sinN * 2.0 * elevation1 * sqrt(saturate(1.0 - elevation1 * elevation1));
        float deficit = 0.5 * cosN - 0.5 * (a0 - a1) * sinN + 0.25 * (cos2h0n + cos2h1n);
        float unoccluded = cosN + n * sinN;
        occluded += projectedLength * deficit / unoccluded;
        weightSum += projectedLength;
    }
    // Stored as occlusion 1 - visibility (the sum of the per-slice deficits):
    // an unoccluded pixel is exactly 0 in
    // R16F (1 - epsilon would drop a whole ulp on a truncating store, and the
    // blur would drop another), and small occlusion keeps fp16's fine steps.
    float occlusion = saturate(occluded / weightSum);
    return occlusion.xxxx;
}
