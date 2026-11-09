// The bullet re-shade's law (included by weapon_bullet_reshade_ps.hlsl and weapon_bullet_reshade_flag_ps.hlsl;
// X3M_WEAPON_EFFECTS=reshade; docs/reverse-engineering/fl/bullet-draw-batch.md section 6). Our pixel program bound in
// place of the game's INSTANCE_BULLETS pixel shader (the B1 body ec1f5c4a2f4e1445 and its two aliases:
// `tex t0; mul r0.rgb, t0, v0.a; mov r0.a, t0.a`) for the bullet batch draws only, with the game's own vertex shader,
// geometry, texture and sampler. ps_2_0: the game's vertex shader is vs_1_1, which a ps_3_0 program cannot pair with.
// Inputs are the vanilla program's: COLOR0 (the VS passes the vertex colour, RGB 0 and the node's alpha in .a) and
// TEXCOORD0 (the instance UV).
//
// The texture sample is the shape. Its luminance, normalised by the IRE strip's peak (c31.w), is remapped:
//   radiance = gain * rgb * smoothstep(0, edge, l)   the dimmer body, the dim fringe fading smoothly to black
//            + core * l^4 * mix(white, hue, 0.35)    a narrow hot term from the top of the range, white-shifted
// times the vertex alpha, as vanilla; alpha stays the sample's (the pass's alpha test sees what it saw).
// c31 = (gain, edge, core, luminance normaliser), written per substituted draw and restored after it; under the
// additive route (HDR, DESTBLEND ONE) gain and core carry the route's emission gain.
sampler2D DiffuseTexSampler : register(s0);
float4 reshade : register(c31);

float4 reshade_colour(float4 colour, float2 uv) {
    const float4 t = tex2D(DiffuseTexSampler, uv);
    const float l = saturate(dot(t.rgb, float3(0.2126, 0.7152, 0.0722)) * reshade.w);
    const float body = reshade.x * smoothstep(0.0, reshade.y, l);
    const float peak = max(max(t.r, t.g), max(t.b, 1.0 / 256.0));
    const float3 hue = t.rgb / peak;
    const float l2 = l * l;
    const float3 rgb = body * t.rgb + (reshade.z * l2 * l2) * lerp(float3(1.0, 1.0, 1.0), hue, 0.35);
    return float4(rgb * colour.a, t.a);
}
