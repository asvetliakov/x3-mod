// The muzzle flash re-shade's vertex shader for the effects DEFAULT pair (X3M_WEAPON_EFFECTS=reshade;
// docs/architecture/muzzle-flash-look.md section "The effects-pair body"), bound in place of the game's effects DEFAULT
// VS d5e1c75351ed3f04 (vs_2_0) for the draw of an effects-pair muzzle body, beside weapon_muzzle_reshade_effects_ps.hlsl.
// The game VS passes the pixel program no position, so this one computes the game VS's three outputs from the same
// inputs and constants, in its order and with its operations (docs/reverse-engineering/fl/muzzle-flash-draw.md section
// 6: the program read token by token; the native fixture compares the two on the same draw), and adds the model
// position:
//   oPos   = (pos, 1) . g_mWorldViewProjection rows c0..c3 (four dp4)
//   oT0.xy = (u, v, 1) . g_TexMatrix rows c10, c11 (two dp3)
//   oD0.x  = g_AlphaValue c12.x, times saturate(g_FogClip.x - g_FogClip.y |camera - world position|) when g_EnableFog
//            (b0): the world position (pos, 1) . g_mWorld rows c4..c6, the camera g_mViewInverse c7..c9 .w
//   oT1    = pos (model units; the pixel program scales it by 1 / (lit radius x mz_size))
// It reads only the game's own constants (the application's, unchanged) and the game's vertex declaration (POSITION,
// TEXCOORD0).
float4 g_mWorldViewProjection[4] : register(c0);
float4 g_mWorld[3] : register(c4);
float4 g_mViewInverse[3] : register(c7);
float4 g_TexMatrix[2] : register(c10);
float4 g_AlphaValue : register(c12);
float4 g_FogClip : register(c13);
bool g_EnableFog : register(b0);

struct Out {
    float4 position : POSITION;
    float4 colour : COLOR0;
    float2 uv : TEXCOORD0;
    float3 model : TEXCOORD1;
};

Out main(float4 position : POSITION, float2 uv : TEXCOORD0) {
    Out o;
    const float4 p = float4(position.xyz, 1.0);
    o.position = float4(dot(p, g_mWorldViewProjection[0]), dot(p, g_mWorldViewProjection[1]),
                        dot(p, g_mWorldViewProjection[2]), dot(p, g_mWorldViewProjection[3]));
    const float3 t = float3(uv, 1.0);
    o.uv = float2(dot(t, g_TexMatrix[0].xyz), dot(t, g_TexMatrix[1].xyz));
    float fade = g_AlphaValue.x;
    if (g_EnableFog) {
        const float3 world = float3(dot(p, g_mWorld[0]), dot(p, g_mWorld[1]), dot(p, g_mWorld[2]));
        const float3 eye = float3(g_mViewInverse[0].w, g_mViewInverse[1].w, g_mViewInverse[2].w) - world;
        fade = saturate(g_FogClip.x - g_FogClip.y * length(eye)) * g_AlphaValue.x;
    }
    o.colour = float4(fade, 0.0, 0.0, 0.0);
    o.model = position.xyz;
    return o;
}
