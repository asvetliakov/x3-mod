// Identity tonemap of the FP16 HDR scene path, stage 1 (docs/architecture/
// hdr-scene-path.md, "Stage 1 implementation"): a point-sampled copy of the
// owned A16B16G16R16F scene target into the game's A8R8G8B8 main target.
// Alpha is the scene alpha, carried through unchanged (the compositor's
// downsample program derives its highlight mask from 1 - saturate(alpha) of
// the scene copy); values above 1 clamp in the 8-bit target, exactly as the
// original draw would have clamped them. No gamma decode, no exposure: with
// every draw writing values in [0, 1] the presented frame must equal the
// X3M_HDR=0 frame. Stage 2 replaces this program with the AgX transform.
sampler2D scene : register(s0);
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    return tex2D(scene, uv);
}
