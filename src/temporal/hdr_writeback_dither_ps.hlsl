// The HDR route's identity write-back (hdr_writeback_ps.hlsl) with the static
// display dither of the 8-bit store (display_dither.hlsl, +-0.5 code): bound
// by hdr_pass.cpp in place of the plain identity copy when X3M_HDR_DITHER is
// on. The plain program stays the one every 8-bit to 8-bit copy uses (the
// bloom main copy, the temporal pass's copy-back) and the one the capability
// self test checks. No constant register: the amplitude is fixed here, so no
// stale device constant can reach this draw. Alpha carried unchanged.
// Compiled by tools/shaders/generate_rigid_motion_pixel.py --shader
// hdr_writeback_dither into src/renderer/hdr_writeback_dither_program_inc.h.
// Bolts through the TAA (bolt_far.hlsl): the flagged texels take the pre-resolve
// scene texel (s2, c29.x = W) before the dither; the rest copy bit for bit.
#include "display_dither.hlsl"
#include "bolt_far.hlsl"
sampler2D scene : register(s0);
float4 main(float2 uv : TEXCOORD0, float2 vpos : VPOS) : COLOR0 {
    float4 c = boltCompositeBranch(tex2D(scene, uv), uv);
    return float4(displayDither(c.rgb, vpos, 1.0 / 255.0), c.a);
}
