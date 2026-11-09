// The bullet re-shade under the additive route with the far flag bound (bolt_far_composite; the route's flag variant
// law, src/renderer/linear_emission_sm1.h far_flag): oC0 the re-shaded radiance, oC1 = oC0 (bound masked off; ps_2_0
// forbids oC gaps), oC2 = 32 * max(rgb) for the lane's .g under the GREEN write mask.
#include "weapon_bullet_reshade.hlsl"

struct Out {
    float4 c0 : COLOR0;
    float4 c1 : COLOR1;
    float4 c2 : COLOR2;
};
Out main(float4 colour : COLOR0, float2 uv : TEXCOORD0) {
    Out o;
    o.c0 = reshade_colour(colour, uv);
    o.c1 = o.c0;
    o.c2 = 32.0 * max(max(o.c0.r, o.c0.g), o.c0.b);
    return o;
}
