// Effects stage (docs/architecture/effects-modernisation-opus.md 2.2): the soft term against the completed lane
// (RT2 at s0, point sampled at the pixel's own texel). A four-channel lane carries the view depth in .b; the R32F lane
// carries device z/w in .r and is inverted with the fog's rule m32 / (d - m22). The sentinel (.r outside [0, 1]) means
// no occluder: fully visible. A lane texel whose .b is not a valid depth (0, negative, non-finite) occludes fully, as
// the dust motes treat it. Included by every stage pixel program; the caller declares nothing else at s0 / c0 / c1.
sampler2D lane_sampler : register(s0);
float4 lane_sizes : register(c0);  // W, H, 1/W, 1/H of the target
float4 lane_form : register(c1);   // four_channel (1: .b is the view depth), m22, m32 (R32F: z = m32 / (d - m22)), unused
float lane_view_depth(float2 raster, out bool occluder) {
    float4 d = tex2Dlod(lane_sampler, float4((floor(raster + 0.5) + 0.5) * lane_sizes.zw, 0, 0));
    occluder = d.r >= 0.0 && d.r <= 1.0;
    float z = lane_form.x > 0.5 ? d.b : lane_form.z / (d.r - lane_form.y);
    return z;
}
// saturate((z_scene - z_frag) / soft): 1 where nothing is in front of the fragment by more than the soft radius.
float soft_visibility(float2 raster, float view_z, float soft_radius) {
    bool occluder;
    float z = lane_view_depth(raster, occluder);
    if (!occluder) return 1.0;
    if (!(z > 0.0 && z <= 3.402823466e38)) return 0.0;
    return saturate((z - view_z) / max(soft_radius, 1e-4));
}
// Two-sided fade for a decal that sits on a surface: 1 at the surface depth, 0 one soft radius in front or behind.
float surface_affinity(float2 raster, float view_z, float soft_radius) {
    bool occluder;
    float z = lane_view_depth(raster, occluder);
    if (!occluder) return 0.0;
    if (!(z > 0.0 && z <= 3.402823466e38)) return 0.0;
    return saturate(1.0 - abs(z - view_z) / max(soft_radius, 1e-4));
}
