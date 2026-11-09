// The bullet re-shade, one output (the game's ONE / INVSRCCOLOR pass, or the additive route's DESTBLEND ONE without
// the far flag): weapon_bullet_reshade.hlsl.
#include "weapon_bullet_reshade.hlsl"

float4 main(float4 colour : COLOR0, float2 uv : TEXCOORD0) : COLOR0 {
    return reshade_colour(colour, uv);
}
