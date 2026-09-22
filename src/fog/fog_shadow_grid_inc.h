// The sun-shadow rows and comparison helpers of every stored-density program, and the sun-visibility slice grid
// (docs/architecture/fog-shadow-pass.md) shared by its pass (fog_density_visibility_grid_ps.hlsl, FOG_GRID_PASS)
// and its readers (march and repair under FOG_SHADOW_PASS). Included by fog_density_field_inc.h.
float4 shadow_select : register(c9); // enabled, margin, band start, reciprocal band
float4 shadow_cascades[12] : register(c10); // 3 view->map rows, (N,1/N,bias,valid)

float fog_pcf(sampler2D map, float3 p, float4 info) {
    // D3D9 replay texel i stores screen i/N, sampled at (i+.5)/N.
    // Interpolate the four comparisons around that point, not raw depths.
    float2 texel = float2(p.x,-p.y)*0.5*info.x + 0.5*info.x;
    float2 base = floor(texel), f = frac(texel);
    float2 uv = (base+0.5)*info.y;
    float reference = p.z-info.z;
    float a = tex2Dlod(map,float4(uv,0,0)).r >= reference ? 1.0:0.0;
    float b = tex2Dlod(map,float4(uv+float2(info.y,0),0,0)).r >= reference ? 1.0:0.0;
    float c = tex2Dlod(map,float4(uv+float2(0,info.y),0,0)).r >= reference ? 1.0:0.0;
    float d = tex2Dlod(map,float4(uv+info.yy,0,0)).r >= reference ? 1.0:0.0;
    return lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y);
}
float shadow_weight(float3 p, float valid) {
    float m = max(abs(p.x),abs(p.y));
    return valid * ((m <= shadow_select.y && p.z >= 0.0 && p.z <= 1.0) ?
        1.0-saturate((m-shadow_select.z)*shadow_select.w):0.0);
}
#if defined(FOG_SHADOW_PASS) || defined(FOG_GRID_PASS)
// The grid rows (renderer::fog_grid_constants). Slice j of 64 spans the sky ray's bin j: 24 of 500 units, then 40 of
// (cap - 12000) / 40; slice j sits in tile j div 4 of the 4x4 atlas layout, lane j mod 4 of its RGBA8 texel.
float4 grid_cascade[3] : register(c36); // per cascade: texel_world, range_world, range_world / texel_world (0: fixed kernel), 0
float4 grid_slices : register(c39);     // 500, far slice width, 1/500, 1/far slice width
float4 grid_layout : register(c40);     // tile width, tile height, 1/atlas width, 1/atlas height
float4 grid_penumbra : register(c41);   // sun angle x penumbra, radius min, radius max (texels of the sampled map), frame term
float grid_slice(float s) { return clamp(s < 12000.0 ? floor(s*grid_slices.z) : 24.0+floor((s-12000.0)*grid_slices.w),0.0,63.0); }
// Atlas texel origin of slice j's tile (xy) and its lane (z).
float3 grid_tile(float j) { float t = floor(j*0.25), row = floor(t*0.25); return float3(float2(t-4.0*row,row)*grid_layout.xy,j-4.0*t); }
#endif
