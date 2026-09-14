// Ambient occlusion chain, pass 1 (docs/architecture/ambient-occlusion.md,
// sections 1 and 2): the route's full-resolution R32F device depth (z/w in
// [0,1], -1 sentinel where no routed draw wrote) point-sampled at the even
// full-resolution texel (2i, 2j) of every half-resolution texel (i, j) and
// linearized. The stored value is the scale-free view depth
// zs = 1 / (m22 - d), the view depth z = m32 / (d - m22) divided by the
// constant |m32| (the caller validates m22 > 1 and m32 < 0, so m22 - d > 0
// for every device depth in [0, 1]). The horizon search is invariant under a
// uniform scale of view space when the radius and the falloff constants are
// scaled with it (the caller divides them by |m32|), normals are normalized
// and every depth comparison is relative, so this saves one multiply here and
// keeps the later passes' depth tests unchanged. The sentinel stays -1 so
// every later pass can test `z < 0`.
// This pass exists because the half-resolution depth is read ~35 times per
// half pixel by the rest of the chain: folding it into the horizon search
// (step 1b, measured) replaces those reads by full-resolution ones and costs
// more than the render pass it saves at 1280x768.
// Compiled by tools/shaders/generate_rigid_motion_pixel.py into
// src/renderer/ambient_occlusion_linearize_program_inc.h.
sampler depthTex : register(s0);
float4 size : register(c0);    // xy = 1 / half width/height, zw = half width/height
float4 depthUV : register(c4); // xy = 2 / full width/height, zw = 0.5 / full width/height
float4 terms : register(c5);   // x = m22

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float2 pixel = floor(uv * size.zw);
    float d = tex2D(depthTex, pixel * depthUV.xy + depthUV.zw).r;
    float value = (d < 0.0) ? -1.0 : 1.0 / (terms.x - d);
    return value.xxxx;
}
