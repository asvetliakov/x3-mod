// The hull hit re-shade's law (included by weapon_hull_reshade_effects_ps.hlsl, ps_2_0, and
// weapon_hull_reshade_classic_ps.hlsl, ps_3_0; X3M_WEAPON_EFFECTS=reshade; docs/architecture/hull-hit-look.md). Our pixel
// program bound in place of the game's for the draw of an impact body (node +0x144 in hull_reshade_core.h's list), with
// the game's vertex shader, geometry, texture, sampler, blend and Z state. The texel is the shape; its luminance l,
// normalised by the body's texture p99 (HH_LOOK.w), is remapped as the bullets' accepted law with a visible fade and a
// cooling term:
//   body  = gain x smoothstep(0, edge, l) x texel.rgb x (1 - cool (1 - f) (0, 0.65, 0.9))   white -> orange -> red
//   core  = core x l^4 x lerp(white, texel hue, 0.35)                                         the hot centre
//   env   = 1 - saturate(cells) x smoothstep(0.85, 1, 2 max |frac(cells x uv) - 1/2|)        the flipbook cell's edge
//   rgb   = (body + core) x f x env, f the node fade (g_AlphaValue x the optional fog factor)
// HH_LOOK = (hh_gain, hh_edge, hh_core, normaliser), HH_SHAPE = (hh_cool, cells, 0, 0); the wrapper defines the two
// registers (c30..c31 on ps_2_0, c192..c193 on ps_3_0), written per substituted draw and put back after it.
sampler2D DiffuseTexSampler : register(s0);
float4 hh_look : register(HH_LOOK);
float4 hh_shape : register(HH_SHAPE);

float3 hull_rgb(float4 t, float2 uv, float f) {
    const float l = saturate(dot(t.rgb, float3(0.2126, 0.7152, 0.0722)) * hh_look.w);
    const float3 cooled = 1.0 - (hh_shape.x * (1.0 - f)) * float3(0.0, 0.65, 0.9);
    const float3 body = (hh_look.x * smoothstep(0.0, hh_look.y, l)) * t.rgb * cooled;
    const float peak = max(max(t.r, t.g), max(t.b, 1.0 / 256.0));
    const float l2 = l * l;
    const float3 core = (hh_look.z * l2 * l2) * lerp(float3(1.0, 1.0, 1.0), t.rgb / peak, 0.35);
    const float2 q = abs(frac(uv * hh_shape.y) - 0.5);
    const float env = 1.0 - saturate(hh_shape.y) * smoothstep(0.85, 1.0, 2.0 * max(q.x, q.y));
    return (body + core) * (f * env);
}
