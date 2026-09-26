// Bolts through the TAA (docs/architecture/bolts-through-taa.md, B'): the post-resolve composite of the write-back and
// the bloom extract. The late bullet draw marks its coverage in the lane's .g (RT2, g = base + K * max(rgb), K = 32);
// the camera-gate resolve raises the flag in its OUTPUT ALPHA only, -(1 + held) (held = the share the far / thin-region
// weight adds over the base weight; negative, since a blended scene alpha is never negative while the game's additive
// draws do sum alpha past any positive threshold), colour, age and depth history untouched. Here, at a resolved texel r
// whose alpha carries the flag, the pre-resolve FP16 scene texel s (the resolve's s0, bound at s2 by the pass) is blended
// in at W * share (W = X3M_BOLT_FAR_SHOW, c29.x; share = saturate(-r.a - 1) ramps the composite in with the far weight
// across the d0-d1 contour) and the alpha becomes the scene's own (the bolt's alpha law for the game's RT0 alpha and the
// bloom weight). Every other texel is untouched: the select is exact (no lerp of a zero weight, which would propagate a
// non-finite scene texel), so a program with the composite writes the same bits as one without it wherever r.a > -1.
// Off (c29.x = 0, or s2 = the same texture as s0): r.rgb = lerp(r, s, 0) = r at a flagged texel too, r.a = s.a.
// Branch-free on purpose: the bloom programs are qualified straight-line (bloom_shader_limits.py); the write-back
// programs take the same fragment so all consumers agree bit for bit.
#ifndef X3_BOLT_FAR
#define X3_BOLT_FAR
sampler2D boltScene : register(s2);
float4 boltShow : register(c29); // x = W in [0, 1] (0: off), yzw unused
float4 boltComposite(float4 r, float2 uv)
{
    float flag = -r.a - 1;                    // >= 0 exactly at a flagged texel (the resolve writes -(1 + held), held > 0)
    float4 s = tex2Dlod(boltScene, float4(uv, 0, 0));
    float3 mixed = lerp(r.rgb, s.rgb, boltShow.x * saturate(flag));
    return flag >= 0 ? float4(mixed, s.a) : r;
}
// The write-back programs (ps_3_0, full-screen, a few hundred flagged texels per firing frame): one compare per
// texel, the scene fetch and the blend only where the flag is set; the same bits as the select everywhere.
float4 boltCompositeBranch(float4 r, float2 uv)
{
    [branch] if (r.a <= -1) return boltComposite(r, uv);
    return r;
}
#endif
