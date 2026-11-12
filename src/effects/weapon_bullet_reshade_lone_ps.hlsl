// The bullet re-shade on the lone path (docs/reverse-engineering/fl/bullet-draw-batch.md section 7), one output: a
// bullet body drawn through the material submit with its effects.fx material, whose vertex shader (the effects DEFAULT
// or INSTANCE one) passes the UV in TEXCOORD0.xy and the fade (g_AlphaValue x the fog clip) in COLOR0.x, where the
// batch's INSTANCE_BULLETS VS passes the node alpha in COLOR0.a. The same law (weapon_bullet_reshade.hlsl) with the
// fade as the vertex alpha; the game's own program also applies an affine colour matrix (c0..c2), which the batch
// pass does not, so the lone bolt matches the batch bolt of the same body.
#include "weapon_bullet_reshade.hlsl"

float4 main(float4 colour : COLOR0, float2 uv : TEXCOORD0) : COLOR0 {
    return reshade_colour(float4(0.0, 0.0, 0.0, colour.x), uv);
}
