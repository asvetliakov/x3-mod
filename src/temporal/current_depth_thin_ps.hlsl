// Thin-vote twin of the current-depth fragment (current_depth_ps.hlsl), appended by the
// transformer (src/renderer/material_motion.cpp) only with X3M_TAA_THIN_VOTE on
// (docs/architecture/taa-thin-geometry-alternatives.md section 3.2, "Implemented"): .r/.g = z/w
// and .b = the clip w exactly as the plain fragment writes them, .a = c2.x instead of w, which
// the transformer relocates to c218, the third register of the route's per-draw pixel upload:
// 1 - thin on an opaque routed row (1 = no vote, below 1 = the thin fraction of the draw's
// triangles), read by the mask chain's tests draw (line_mask_ps.hlsl, X3M_THIN_VOTE). The
// lane-off R32F RT2 drops .a at the format. With the option off the plain fragment is used.
float4 thin : register(c2);
float4 main(float2 clip : TEXCOORD1) : COLOR0 {
    return float4((clip.x / clip.y).xx, clip.y, thin.x);
}
