// Engine plumes, phase 2 (docs/architecture/engine-effects-modern.md sections 3 and 4): the pixel program of the
// stage's one indexed draw, ONE/ONE on the FP16 scene (alpha 0: the scene's alpha is untouched).
// Axial quad (kind 0): a core along the axis of radius r0 at the nozzle tapering to 0 at L, radiance I_core x the
// colour (the mean tint blended to the peak towards the axis), a one-pixel analytic edge; a halo exp(-d / sigma) x
// I_halo x the mean tint, d the distance to the segment nozzle..tip, sigma tapering to half at the tip, windowed to 0
// at the quad's reach (3 sigma at the nozzle).
// Disc (kind 1): the same core and halo radially about the nozzle (the hot disc's radius in local.w); the CPU weighted
// both by |axis . to_camera|.
// Soft occlusion against the completed lane (RT2 at s0, point sampled at the pixel): .b the view depth on the
// four-channel lane, z/w in .r inverted with m32 / (d - m22) on the R32F lane; .r outside [0, 1] (the sentinel) = no
// occluder; a non-finite or non-positive depth occludes. The plume's depth is that of the nearest axis point (the
// billboard's own depth moved along the axis to u clamped to [0, L]; the disc's is the nozzle's), pulled towards the
// camera by the bias when the exhaust faces it: vis = saturate((lane - (z_axis - bias)) / SOFT), SOFT 0.15 value for the
// core and 1.0 value for the halo, so a plume behind a hull shows only past its silhouette (glow_through 0).
// Compiled with tools/shaders/generate_rigid_motion_pixel.py.
sampler2D lane_sampler : register(s0);
float4 lane_sizes : register(c0); // 1/W, 1/H of the target, unused x2
float4 lane_form : register(c1);  // four_channel (1: .b is the view depth), m22, m32 (R32F: z = m32 / (d - m22)), unused
float4 look : register(c2);       // SOFT core, SOFT halo (x value), halo reach (x sigma), unused
struct Input {
    float4 local : TEXCOORD0;
    float4 shape : TEXCOORD1;
    float4 view : TEXCOORD2;
    float3 tint : TEXCOORD3;
    float3 peak : TEXCOORD4;
    float2 pixel : VPOS;
};
float4 main(Input i) : COLOR0 {
    float L = max(i.local.z, 1e-6), r0 = i.local.w;
    float sigma0 = i.shape.x, value = i.shape.y;
    bool disc = i.shape.w > 0.5;
    float aa = max(fwidth(i.local.y), 1e-6); // world units per pixel across the quad
    float w = abs(i.local.y);
    float u_axis = clamp(i.local.x, 0.0, L);
    float t = u_axis / L;
    float r = r0 * (1.0 - t);
    float on_axis = (i.local.x >= 0.0 && i.local.x <= L) ? 1.0 : 0.0;
    float du = i.local.x - u_axis;
    float rho = length(i.local.xy);
    float d = disc ? rho : sqrt(du * du + w * w);
    float radius = disc ? r0 : r;
    float across = disc ? rho : w;
    float core = (disc ? 1.0 : on_axis) * saturate((radius - across) / aa + 0.5);
    float centre = saturate(1.0 - across / max(radius, aa));
    float sigma = disc ? sigma0 : sigma0 * (1.0 - 0.5 * t);
    float window = saturate(1.0 - d / (look.z * sigma0));
    float halo = exp(-d / max(sigma, 1e-6)) * window * window;
    float4 lane = tex2Dlod(lane_sampler, float4((i.pixel + 0.5) * lane_sizes.xy, 0, 0));
    bool occluder = lane.r >= 0.0 && lane.r <= 1.0;
    float z = lane_form.x > 0.5 ? lane.b : lane_form.z / (lane.r - lane_form.y);
    float valid = (z > 0.0 && z <= 3.402823466e38) ? 1.0 : 0.0;
    float depth = i.view.x - i.view.w * du; // the nearest axis point (du = 0 on the disc: view.w is 0 there)
    float gap = z - (depth - i.shape.z);
    float soft_core = occluder ? valid * saturate(gap / max(look.x * value, 1e-4)) : 1.0;
    float soft_halo = occluder ? valid * saturate(gap / max(look.y * value, 1e-4)) : 1.0;
    float3 colour = lerp(i.tint, i.peak, centre) * (i.view.y * core * soft_core) + i.tint * (i.view.z * halo * soft_halo);
    return float4(max(colour, 0.0), 0.0);
}
