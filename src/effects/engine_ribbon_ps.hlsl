// Engine ribbons, phase 3 (docs/architecture/engine-effects-modern.md sections 3 and 4): the pixel program of the
// stage's second indexed draw, ONE/ONE on the FP16 scene after the plumes (alpha 0: the scene's alpha is untouched).
// The strip's radiance (I_ribbon(s) x preset x (1 - u) x the fade and the near-camera weight, per point on the CPU) and
// colour arrive interpolated; across the strip a smooth profile (1 - a^2)^2, a = across in [-1, 1].
// Soft occlusion against the completed lane, the plume halo's test (RT2 at s0, point sampled at the pixel): .b the view
// depth on the four-channel lane, z/w in .r inverted with m32 / (d - m22) on the R32F lane; .r outside [0, 1] (the
// sentinel) = no occluder; a non-finite or non-positive depth occludes. vis = saturate((lane - z_centre) / (SOFT x
// value)), z_centre the strip's centre-line view depth, SOFT 1.0.
// Compiled with tools/shaders/generate_rigid_motion_pixel.py.
sampler2D lane_sampler : register(s0);
float4 lane_sizes : register(c0); // 1/W, 1/H of the target, unused x2
float4 lane_form : register(c1);  // four_channel (1: .b is the view depth), m22, m32 (R32F: z = m32 / (d - m22)), unused
float4 look : register(c2);       // SOFT (x value), unused x3
struct Input {
    float4 strip : TEXCOORD0;
    float4 shape : TEXCOORD1;
    float3 tint : TEXCOORD2;
    float2 pixel : VPOS;
};
float4 main(Input i) : COLOR0 {
    float a = saturate(abs(i.strip.y));
    float profile = (1.0 - a * a) * (1.0 - a * a);
    float4 lane = tex2Dlod(lane_sampler, float4((i.pixel + 0.5) * lane_sizes.xy, 0, 0));
    bool occluder = lane.r >= 0.0 && lane.r <= 1.0;
    float z = lane_form.x > 0.5 ? lane.b : lane_form.z / (lane.r - lane_form.y);
    float valid = (z > 0.0 && z <= 3.402823466e38) ? 1.0 : 0.0;
    float vis = occluder ? valid * saturate((z - i.shape.y) / max(look.x * i.shape.z, 1e-4)) : 1.0;
    return float4(max(i.tint * (i.shape.x * profile * vis), 0.0), 0.0);
}
