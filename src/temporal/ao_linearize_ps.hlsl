// Ambient occlusion chain, pass 1 (docs/architecture/ambient-occlusion.md,
// sections 1 and 2): the route's full-resolution R32F device depth (z/w in
// [0,1], -1 sentinel where no routed draw wrote) point-sampled at the even
// full-resolution texel (2i, 2j) of every half-resolution texel (i, j) and
// linearized to view depth z = m32 / (d - m22) (zn * m22 / (m22 - d) in the
// note's form). The sentinel stays -1 so every later pass can test `z < 0`.
// Compiled by tools/shaders/generate_rigid_motion_pixel.py into
// src/renderer/ambient_occlusion_linearize_program_inc.h.
sampler depthTex : register(s0);
float4 projection : register(c0); // x = m22, y = m32
float4 sizes : register(c1);      // xy = full width/height, zw = half width/height

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float2 halfPixel = floor(uv * sizes.zw);
    float2 fullUV = (halfPixel * 2.0 + 0.5) / sizes.xy;
    float d = tex2D(depthTex, fullUV).r;
    float z = projection.y / (d - projection.x);
    float value = (d < 0.0) ? -1.0 : z;
    return value.xxxx;
}
