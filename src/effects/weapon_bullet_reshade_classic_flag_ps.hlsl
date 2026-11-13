// The lone classic bullet re-shade (weapon_bullet_reshade_classic_ps.hlsl: COLOR0.a the fade, ps_3_0 beside the game's
// vs_3_0 494fe349b8bc12ec) with the far flag bound, under the game's own ONE / ONE (bullet_reshade_route.h
// lone_native_flag) or the additive route's DESTBLEND ONE: oC0 the re-shaded radiance with vanilla's alpha, oC1 = oC0
// (bound masked off; no oC gap), oC2 = 32 * max(rgb) for the lane's .g under the GREEN write mask, as
// weapon_bullet_reshade_lone_flag_ps.hlsl.
#include "weapon_bullet_reshade.hlsl"
sampler2D LightMapTexSampler : register(s2);
float4 g_EnableGlow : register(c4);

struct Out {
    float4 c0 : COLOR0;
    float4 c1 : COLOR1;
    float4 c2 : COLOR2;
};
Out main(float4 colour : COLOR0, float2 uv : TEXCOORD0_centroid) {
    Out o;
    const float4 r = reshade_colour(float4(0.0, 0.0, 0.0, colour.a), uv);
    o.c0 = float4(r.rgb, lerp(r.a, tex2D(LightMapTexSampler, uv).a, g_EnableGlow.x) * colour.a);
    o.c1 = o.c0;
    o.c2 = 32.0 * max(max(o.c0.r, o.c0.g), o.c0.b);
    return o;
}
