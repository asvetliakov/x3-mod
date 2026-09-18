// Scene-end sun-shadow application over up to five cascades
// (docs/architecture/shadow-cascades.md, section 2). The single-map quad
// (sun_shadow_apply_ps.hlsl) with the map chosen per pixel: RT2 read, view
// depth (RT2.b when select.w > 0, the A32B32G32R32F lane of
// docs/architecture/shadow-receiver-depth.md; else the z/w law) and view
// position as there; the sun-space position of every cascade
// (three dp4 each) and ddx/ddy of the *view position* once, before any branch
// (the sun rows are affine in the view position, so each cascade's map-UV and
// depth derivatives are arithmetic on those six values, valid inside a
// branch). Cascade i contains the pixel when max(|x_i|, |y_i|) <= margin and
// z_i in [0, 1]; the first containing cascade is selected. In the outer band
// of the margin, t = saturate((max(|x_i|, |y_i|) - start) * inverse_band), the
// factor is lerp(f_i, f_next, t): f_next is cascade i + 1's PCF when that
// cascade contains the pixel, 1 (lit) for the last cascade (the distance
// fade), and f_i itself otherwise. An absent cascade (flag valid = 0: not
// replayed, or retained from before a Reset or a refused frame) counts as lit.
// Each cascade's 3x3 PCF is the single-map kernel (rotated by the jitter
// index, taps snapped to texel centres, receiver-plane bias clamped, constant
// bias) with its own size and bias constants; the taps are texldl (no
// gradient instruction inside a branch) under a ps_3_0 dynamic branch, so a
// pixel pays one PCF, two inside a band; the nine taps are a ps_3_0 loop over
// the rotated offsets the pass uploads (c4-c12), which keeps the program
// inside the 512 instruction slots every ps_3_0 device has. Outside every cascade, sentinel,
// share-free or fully lit: exactly 1 (the multiply leaves the target
// byte-identical). Compiled by tools/shaders/generate_rigid_motion_pixel.py
// into src/renderer/sun_shadow_cascade_apply_program_inc.h.
// Texel convention: the replay rasterizes under D3D9, where map texel (i, j)
// holds the depth at screen position (i, j), i.e. at map position (i, j) / N,
// while a texture lookup addresses that texel at ((i, j) + 0.5) / N. The
// receiver's lookup position is therefore suv = muv + 0.5 / N: its nearest
// texel is floor(suv N) (= round(muv N)), and a tap at tapUV holds the depth
// of the point tapUV - suv away from the receiver (the receiver-plane term).
sampler depthShareTex : register(s0); // G32R32F or A32B32G32R32F: r = device depth (z/w, -1 sentinel), g = sun share s, b = view depth w (wide only)
sampler mapTex0 : register(s1);       // R32F sun-space depth maps, nearest cascade first
sampler mapTex1 : register(s2);
sampler mapTex2 : register(s3);
sampler mapTex3 : register(s4);
sampler mapTex4 : register(s5);
float4 view : register(c0);    // x = m00, y = m11, z = m20, w = m21 (the jittered projection latch)
float4 terms : register(c1);   // x = m22, y = m32, z = exponent, w = relative view-depth step that voids the plane fit
float4 select : register(c3);  // x = margin, y = band start, z = 1 / band width, w = 1 when RT2.b carries the view depth (A32B32G32R32F), 0 for the z/w law
float4 taps[9] : register(c4); // xy = the 3x3 kernel offsets in texels, rotated by the frame's jitter index (row-major, j then i)
// Per cascade i at c(13 + 5 i): the three view -> sun rows, (map size, 1 / size,
// constant bias, receiver-plane clamp) and (valid, last, slope margin / size, 0).
// An unused cascade has zero rows with row 0 w = 2 (never contains a pixel).
// Slope margin (sun_shadow_apply_pass.h, sun_shadow_bias_slope_texels_*): every
// tap's plane term is lowered by slope_texels texels of the plane's depth slope
// |g.x| + |g.y| (g per uv, so flags.z carries texels / size) before its clamp; on
// a sun-grazing plane the plane term extrapolates a whole texel of depth from a
// sub-texel derivative baseline, so the receiver's fp32 noise reaches the taps
// as a tenth of that texel, above the constant bias. Zero keeps the older law.
float4 cascades[25] : register(c13);

float pcf(sampler tex, float3 sun, float3 dpdx, float3 dpdy, float4 r0, float4 r1, float4 r2, float4 map, float4 flags, bool steady) {
    float2 muv = float2(sun.x, -sun.y) * 0.5 + 0.5;
    float2 duvdx = float2(dot(r0.xyz, dpdx), -dot(r1.xyz, dpdx)) * 0.5;
    float2 duvdy = float2(dot(r0.xyz, dpdy), -dot(r1.xyz, dpdy)) * 0.5;
    float dzdx = dot(r2.xyz, dpdx), dzdy = dot(r2.xyz, dpdy);
    // Receiver plane: solve [duvdx; duvdy] g = [dzdx; dzdy] for g = (dz/du, dz/dv).
    float det = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
    bool planar = abs(det) > 1e-12 && steady;
    float inv = planar ? 1.0 / det : 0.0;
    float2 g = float2(dzdx * duvdy.y - dzdy * duvdx.y, dzdy * duvdx.x - dzdx * duvdy.x) * inv;
    float2 suv = muv + 0.5 * map.y;
    float2 texel = floor(suv * map.x);
    // Slope margin: flags.z texels of |dz/du| + |dz/dv| (the pass uploads texels / size), folded
    // into the plane term before its clamp so the lowering stays within map.w (two slots per
    // cascade: g is 0 where the fit is dropped, and the dot's third operand takes the subtraction).
    float slope = (abs(g.x) + abs(g.y)) * flags.z;
    float lit = 0.0;
    [loop] for (int k = 0; k < 9; ++k) {
        float2 tapUV = (floor(texel + 0.5 + taps[k].xy) + 0.5) * map.y;
        float bias = planar ? clamp(dot(tapUV - suv, g) - slope, -map.w, map.w) : -map.w;
        float reference = sun.z + bias - map.z;
        lit += (tex2Dlod(tex, float4(tapUV, 0.0, 0.0)).r >= reference) ? 1.0 : 0.0;
    }
    return lit / 9.0;
}

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float4 ds = tex2D(depthShareTex, uv);
    float d = ds.r, s = saturate(ds.g);
    float z = select.w > 0.0 ? ds.b : terms.y / (d - terms.x);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 p = float4((ndc.x - view.z) * z / view.x, (ndc.y - view.w) * z / view.y, z, 1.0);
    // Derivatives before any branch (undefined under divergent control flow).
    float3 dpdx = ddx(p.xyz), dpdy = ddy(p.xyz);
    bool steady = abs(dpdx.z) + abs(dpdy.z) < terms.w * abs(z);
    float3 sun[5];
    float inside[5], band[5];
    for (int c = 0; c < 5; ++c) {
        sun[c] = float3(dot(p, cascades[c * 5]), dot(p, cascades[c * 5 + 1]), dot(p, cascades[c * 5 + 2]));
        float m = max(abs(sun[c].x), abs(sun[c].y));
        inside[c] = (m <= select.x && sun[c].z >= 0.0 && sun[c].z <= 1.0) ? 1.0 : 0.0;
        band[c] = saturate((m - select.y) * select.z);
    }
    // The first containing cascade carries 1 - t b, its successor t when it
    // contains the pixel (b = 1 then, and for the last cascade, whose t fades
    // to lit); absent cascades weigh nothing (lit).
    float chosen0 = inside[0];
    float chosen1 = (1.0 - inside[0]) * inside[1];
    float chosen2 = (1.0 - inside[0]) * (1.0 - inside[1]) * inside[2];
    float chosen3 = (1.0 - inside[0]) * (1.0 - inside[1]) * (1.0 - inside[2]) * inside[3];
    float chosen4 = (1.0 - inside[0]) * (1.0 - inside[1]) * (1.0 - inside[2]) * (1.0 - inside[3]) * inside[4];
    float covered = chosen0 + chosen1 + chosen2 + chosen3 + chosen4;
    float w0 = chosen0 * (1.0 - band[0] * max(inside[1], cascades[4].y));
    float w1 = chosen1 * (1.0 - band[1] * max(inside[2], cascades[9].y)) + chosen0 * band[0] * inside[1];
    float w2 = chosen2 * (1.0 - band[2] * max(inside[3], cascades[14].y)) + chosen1 * band[1] * inside[2];
    float w3 = chosen3 * (1.0 - band[3] * max(inside[4], cascades[19].y)) + chosen2 * band[2] * inside[3];
    float w4 = chosen4 * (1.0 - band[4] * cascades[24].y) + chosen3 * band[3] * inside[4];
    w0 *= cascades[4].x; w1 *= cascades[9].x; w2 *= cascades[14].x; w3 *= cascades[19].x; w4 *= cascades[24].x;
    float shade = 0.0;
    [branch] if (w0 > 0.0) shade += w0 * (1.0 - pcf(mapTex0, sun[0], dpdx, dpdy, cascades[0], cascades[1], cascades[2], cascades[3], cascades[4], steady));
    [branch] if (w1 > 0.0) shade += w1 * (1.0 - pcf(mapTex1, sun[1], dpdx, dpdy, cascades[5], cascades[6], cascades[7], cascades[8], cascades[9], steady));
    [branch] if (w2 > 0.0) shade += w2 * (1.0 - pcf(mapTex2, sun[2], dpdx, dpdy, cascades[10], cascades[11], cascades[12], cascades[13], cascades[14], steady));
    [branch] if (w3 > 0.0) shade += w3 * (1.0 - pcf(mapTex3, sun[3], dpdx, dpdy, cascades[15], cascades[16], cascades[17], cascades[18], cascades[19], steady));
    [branch] if (w4 > 0.0) shade += w4 * (1.0 - pcf(mapTex4, sun[4], dpdx, dpdy, cascades[20], cascades[21], cascades[22], cascades[23], cascades[24], steady));
    float f = saturate(1.0 - shade);
    bool valid = d >= 0.0 && s > 0.0 && covered > 0.0;
    float base = saturate(1.0 - (1.0 - f) * s);
    float shadowed = base > 0.0 ? pow(base, terms.z) : 0.0;
    float factor = (valid && f < 1.0) ? shadowed : 1.0;
    return float4(factor, factor, factor, 1.0);
}
