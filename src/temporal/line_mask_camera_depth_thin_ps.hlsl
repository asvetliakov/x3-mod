// Thin-vote twin of line_mask_camera_depth_ps.hlsl (X3M_TAA_THIN_VOTE; docs/architecture/taa-thin-geometry-alternatives.md
// section 3.2): the tests draw also sets b on a pixel whose lane texel carries a draw-time thin vote; see line_mask_ps.hlsl,
// X3M_THIN_VOTE. Bound instead of the plain program only while the option is on and the lane is four-channel.
#define X3M_CAMERA_GATE 1
#define X3M_MASK_DEPTH_OUT 1
#define X3M_THIN_VOTE 1
#include "line_mask_ps.hlsl"
