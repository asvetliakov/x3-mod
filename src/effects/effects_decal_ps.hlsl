// Effects stage, ripple decal (docs/architecture/effects-modernisation-opus.md 3.3): rings expanding from the hit point
// over the quad with a hex lattice, a short central flash, the sprite's tint, an edge fade at the quad rim, and the
// two-sided fade against the lane so the decal wraps the hull it sits on (surface_affinity). Alpha 0 under ONE/ONE.
#include "effects_soft_depth.hlsl"
float4 look : register(c2);      // ring intensity, hex scale, ring speed (radii per second), ring width (radii)
float4 tint : register(c3);      // rgb tint, T (ripple seconds)
float4 flash : register(c4);     // flash seconds, flash intensity, flash sharpness, unused
float hex_lines(float2 q) {
    float2 s = float2(1.0, 1.7320508);
    float2 a = q - s * floor(q / s) - s * 0.5;
    float2 b = q + s * 0.5; b = b - s * floor(b / s) - s * 0.5;
    float d = sqrt(min(dot(a, a), dot(b, b)));
    return smoothstep(0.30, 0.42, d);
}
float4 main(float4 raster : TEXCOORD0, float4 shade : TEXCOORD1) : COLOR0 {
    float d = length(shade.xy);
    float age = shade.z;
    float life = saturate(1.0 - age / tint.w);
    float ring = exp(-pow((d - look.z * age) / look.w, 2.0)) * life;
    float hex = 0.55 + 0.45 * hex_lines(shade.xy * look.y);
    float edge = saturate((1.0 - d) * 4.0);
    float burst = exp(-d * d * flash.z) * saturate(1.0 - age / flash.x);
    float affinity = surface_affinity(raster.xy, raster.z, raster.w);
    float3 colour = (tint.rgb * look.x * ring * hex + flash.y * burst) * edge * affinity * shade.w;
    return float4(max(colour, 0.0), 0.0);
}
