// Fade-owner twin of the current-depth fragment (current_depth_ps.hlsl), appended by the transformer
// (src/renderer/material_motion.cpp) only with X3M_FADE_RT2_OWNER on (docs/architecture/fade-rt2-ownership.md):
// .r/.g = z/w and .b = the clip w exactly as the plain fragment writes them, .a = max(w * c2.z + c2.x, c2.y) from
// c218, the third register of the route's per-draw pixel upload (the transformer relocates c2 there). The route
// uploads, per routed row: c218.y = 1 on a fade-arm row (the fade owner: the engine's SRCALPHA/INVSRCALPHA blend then
// stores src * 1 + dst * 0, the exact depth) and 0 elsewhere; c218.x = the thin vote's 1 - thin with
// X3M_TAA_THIN_VOTE on (0 otherwise); c218.z = 1 on a non-fade row with the thin vote off (.a = w, the plain
// fragment's value) and 0 otherwise. Every product and sum is exact for a finite w > 0, so a non-fade row writes the
// value the plain (vote off) or thin (vote on) fragment writes, and a fade-arm row writes 1. The lane-off R32F RT2
// drops .g/.b/.a at the format; the blend factor is still this alpha.
float4 lane : register(c2);
float4 main(float2 clip : TEXCOORD1) : COLOR0 {
    return float4((clip.x / clip.y).xx, clip.y, max(clip.y * lane.z + lane.x, lane.y));
}
