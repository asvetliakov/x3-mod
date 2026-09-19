// Volumetric sun fog, quad 1 (docs/architecture/volumetric-fog.md, section 3 and
// "Stage 1 implementation"): the half-resolution lit fraction
//   F = int sigma T V dt / (1 - Tb)
// of the camera-local medium sigma0 exp(-t / R), tau(d) = tau_max (1 - exp(-d / R)).
// Half texel (i, j) is the even full-resolution pixel (2i, 2j) of the route's
// RT2 (.r device depth, -1 sentinel; .b view depth of the A32B32G32R32F lane,
// else m32 / (d - m22)). 16 samples of equal weight in transmittance:
//   q = (k + ign) / 16, tau = -ln(1 - q (1 - Tb)), t = -R ln(1 - tau / tau_max),
// ign = interleaved gradient noise of the half pixel offset by the TAA jitter
// index (the 8-sample resolve accumulates the phases). Per sample the first
// containing VALID cascade of the three bound (one nearest tap, texel centres
// as sun_shadow_cascade_apply_ps.hlsl, reference lowered by the cascade's bias
// clamp); outside every cascade, or none valid: lit. A sentinel pixel marches
// the whole medium (Tb = exp(-tau_max)); its far samples leave the last
// cascade and count lit. The scalar goes to every channel of an A8R8G8B8
// target. tools/analysis/fog_offline_mock.py, lit_fraction(), is the law.
// Compiled by tools/shaders/generate_rigid_motion_pixel.py into
// src/renderer/fog_march_program_inc.h.
sampler depthShareTex : register(s0);
sampler mapTex0 : register(s1);
sampler mapTex1 : register(s2);
sampler mapTex2 : register(s3);
float4 view : register(c0);       // m00, m11, m20, m21 (the jittered latch plus the quad pixel-centre term)
float4 depthTerms : register(c1); // m22, m32, 1 when .b carries the view depth, cascade select margin
float4 medium : register(c2);     // tau_max, R, 1 / tau_max, 5.588238 * jitter index
float4 halfSize : register(c3);   // half width, half height
float4 fullUV : register(c4);     // 2 / W, 2 / H, 0.5 / W, 0.5 / H
float4 cascades[12] : register(c5); // per cascade: three view -> sun rows, (size, 1 / size, bias, valid)

float tap(sampler tex, float3 s, float4 map) {
    float2 texel = clamp(floor((float2(s.x, -s.y) * 0.5 + 0.5 + 0.5 * map.y) * map.x), 0.0, map.x - 1.0);
    return (tex2Dlod(tex, float4((texel + 0.5) * map.y, 0.0, 0.0)).r >= s.z - map.z) ? 1.0 : 0.0;
}
float3 sunSpace(float4 p, int c) {
    return float3(dot(p, cascades[c * 4]), dot(p, cascades[c * 4 + 1]), dot(p, cascades[c * 4 + 2]));
}
bool contains(float3 s, float valid) {
    return valid > 0.0 && max(abs(s.x), abs(s.y)) <= depthTerms.w && s.z >= 0.0 && s.z <= 1.0;
}

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float2 pixel = floor(uv * halfSize.xy);
    float2 fuv = pixel * fullUV.xy + fullUV.zw;
    float4 ds = tex2D(depthShareTex, fuv);
    bool geometry = ds.r >= 0.0 && ds.r <= 1.0;
    float z = (depthTerms.z > 0.0 && ds.b > 0.0) ? ds.b : depthTerms.y / (ds.r - depthTerms.x);
    float2 ndc = float2(fuv.x * 2.0 - 1.0, 1.0 - fuv.y * 2.0);
    float3 ray = float3((ndc.x - view.z) / view.x, (ndc.y - view.w) / view.y, 1.0);
    float rayLength = length(ray);
    float3 dir = ray / rayLength;
    float falloff = geometry ? exp(-z * rayLength / medium.y) : 0.0;
    float reach = 1.0 - exp(-medium.x * (1.0 - falloff)); // 1 - Tb
    float2 cell = pixel + medium.w;
    float ign = frac(52.9829189 * frac(0.06711056 * cell.x + 0.00583715 * cell.y));
    float lit = 0.0;
    [loop] for (int k = 0; k < 16; ++k) {
        float q = (float(k) + ign) * 0.0625;
        float tau = -log(1.0 - q * reach);
        float t = -medium.y * log(max(1.0 - tau * medium.z, 1e-12));
        float4 p = float4(dir * t, 1.0);
        float3 s0 = sunSpace(p, 0), s1 = sunSpace(p, 1), s2 = sunSpace(p, 2);
        float visible = 1.0;
        [branch] if (contains(s0, cascades[3].w)) visible = tap(mapTex0, s0, cascades[3]);
        else {
            [branch] if (contains(s1, cascades[7].w)) visible = tap(mapTex1, s1, cascades[7]);
            else {
                [branch] if (contains(s2, cascades[11].w)) visible = tap(mapTex2, s2, cascades[11]);
            }
        }
        lit += visible;
    }
    return (lit * 0.0625).xxxx;
}
