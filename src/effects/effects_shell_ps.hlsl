// Effects stage, shield hit (docs/architecture/effects-modernisation-opus.md 3.3): the shell pixel program. Front faces
// only (the view-space normal must face the eye); a Fresnel rim pow(1 - |n.v|, RIM_POWER) that fades with the newest
// hit's age, up to four rings expanding from their hit direction on the unit sphere (a Gaussian in the angle theta about
// c x age, fading as 1 - age / T), a hex lattice modulation of the rings, a short flash at each hit point, the tint of
// the native sprite, and the soft fade against the lane where the shell cuts the hull. Alpha 0 under ONE/ONE.
#include "effects_soft_depth.hlsl"
float4 look : register(c2);      // rim intensity, ring intensity, rim power, hex scale
float4 tint : register(c3);      // rgb tint, alpha
float4 timing : register(c4);    // hit count, T (ripple seconds), ring speed (radians per second), ring width (radians)
float4 hits[4] : register(c5);   // local hit direction (unit sphere), age in seconds
float4 flash : register(c9);     // flash seconds, flash intensity, flash sharpness, unused
float hex_lines(float2 q) {
    float2 s = float2(1.0, 1.7320508);
    float2 a = q - s * floor(q / s) - s * 0.5;
    float2 b = q + s * 0.5; b = b - s * floor(b / s) - s * 0.5;
    float d = sqrt(min(dot(a, a), dot(b, b)));
    return smoothstep(0.30, 0.42, d);
}
float4 main(float4 raster : TEXCOORD0, float3 normal : TEXCOORD1, float3 local : TEXCOORD2, float3 view : TEXCOORD3) : COLOR0 {
    float3 n = normalize(normal);
    float3 eye = normalize(-view);
    float facing = dot(n, eye);
    // The back hemisphere (drawn with CULLMODE NONE: the icosphere's winding is not relied upon) leaves before the rings.
    [branch] if (facing <= 0.0) return float4(0, 0, 0, 0);
    float rim = pow(1.0 - saturate(facing), look.z);
    float3 p = normalize(local);
    float rings = 0.0, burst = 0.0, newest = timing.y;
    [unroll] for (int i = 0; i < 4; ++i) {
        float on = step(float(i) + 0.5, timing.x);
        float theta = acos(clamp(dot(p, hits[i].xyz), -1.0, 1.0));
        float age = hits[i].w;
        float life = saturate(1.0 - age / timing.y);
        float ring = exp(-pow((theta - timing.z * age) / timing.w, 2.0)) * life;
        rings += on * ring;
        burst += on * exp(-theta * theta * flash.z) * saturate(1.0 - age / flash.x);
        newest = min(newest, age + (1.0 - on) * timing.y);
    }
    float envelope = saturate(1.0 - newest / timing.y);
    float hex = 0.55 + 0.45 * hex_lines(float2(p.x + 0.5 * p.z, p.y + 0.5 * p.z) * look.w);
    float vis = soft_visibility(raster.xy, raster.z, raster.w);
    float3 colour = tint.rgb * (look.x * rim * envelope + look.y * rings * hex) + flash.y * burst;
    colour *= tint.a * vis * step(0.0, facing);
    return float4(max(colour, 0.0), 0.0);
}
