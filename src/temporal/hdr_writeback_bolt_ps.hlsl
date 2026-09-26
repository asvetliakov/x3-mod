// The HDR route's identity write-back (hdr_writeback_ps.hlsl) with the bolt
// composite (bolt_far.hlsl) and no dither: bound by hdr_pass.cpp in place of
// the plain identity copy when X3M_BOLT_FAR_COMPOSITE is on and X3M_HDR_DITHER
// off (the identity tonemap and the tonemap-failure fallback). The plain
// program stays pure: the temporal pass uses it as the lane's R32F point copy
// with nothing at s2 and no c29, where a composite would corrupt the depth
// history. Compiled by tools/shaders/generate_rigid_motion_pixel.py --shader
// hdr_writeback_bolt into src/renderer/hdr_writeback_bolt_program_inc.h.
#include "bolt_far.hlsl"
sampler2D scene : register(s0);
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    return boltCompositeBranch(tex2D(scene, uv), uv);
}
