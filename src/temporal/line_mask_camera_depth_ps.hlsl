// The camera-gate mask's first draw with the current depth copy folded in (docs/architecture/taa-high-resolution.md S1):
// COLOR1 = the current-depth texel at s1 for the R32F depth history; see line_mask_ps.hlsl, X3M_MASK_DEPTH_OUT.
#define X3M_CAMERA_GATE 1
#define X3M_MASK_DEPTH_OUT 1
#include "line_mask_ps.hlsl"
