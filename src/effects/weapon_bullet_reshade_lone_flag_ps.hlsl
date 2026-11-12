// The lone-path bullet re-shade (weapon_bullet_reshade_lone_ps.hlsl: COLOR0.x is the fade) under the additive route
// with the far flag bound: oC0 the re-shaded radiance, oC1 = oC0 (bound masked off; ps_2_0 forbids oC gaps), oC2 =
// 32 * max(rgb) for the lane's .g under the GREEN write mask, as weapon_bullet_reshade_flag_ps.hlsl.
#include "weapon_bullet_reshade.hlsl"

struct Out {
    float4 c0 : COLOR0;
    float4 c1 : COLOR1;
    float4 c2 : COLOR2;
};
Out main(float4 colour : COLOR0, float2 uv : TEXCOORD0) {
    Out o;
    o.c0 = reshade_colour(float4(0.0, 0.0, 0.0, colour.x), uv);
    o.c1 = o.c0;
    o.c2 = 32.0 * max(max(o.c0.r, o.c0.g), o.c0.b);
    return o;
}
