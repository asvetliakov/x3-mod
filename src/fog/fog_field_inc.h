// Spatial family field. c0 is corrected exactly once by the caller for jitter + quad centres.
sampler2D depth_texture : register(s0);
sampler2D atlas_texture : register(s1);
float4 projection : register(c0);
float4 sizes : register(c1); // full W,H,half W,H
float4 camera_sigma : register(c2); // modulo-period world camera, sigma
float4 sun_horizon : register(c3);
float4 inverse_view0 : register(c4);
float4 inverse_view1 : register(c5);
float4 inverse_view2 : register(c6);
float4 phase_gamma : register(c7); // 1+g*g, 2*g, 1-g*g, decode exponent
float4 radiance_encode : register(c8); // E/pi RGB, reciprocal decode exponent

// Same slots in march and composite: the latter also repairs full-resolution
// depth edges by marching. R32F is point sampled; comparisons are filtered here.
sampler2D shadow0 : register(s4);
sampler2D shadow1 : register(s5);
sampler2D shadow2 : register(s6);
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
float fog_visibility(float3 view_position) {
    float4 p = float4(view_position,1.0);
    float3 p0 = float3(dot(p,shadow_cascades[0]),dot(p,shadow_cascades[1]),dot(p,shadow_cascades[2]));
    float3 p1 = float3(dot(p,shadow_cascades[4]),dot(p,shadow_cascades[5]),dot(p,shadow_cascades[6]));
    float3 p2 = float3(dot(p,shadow_cascades[8]),dot(p,shadow_cascades[9]),dot(p,shadow_cascades[10]));
    float w0 = shadow_weight(p0,shadow_cascades[3].w);
    float w1 = (1.0-w0)*shadow_weight(p1,shadow_cascades[7].w);
    float w2 = (1.0-w0-w1)*shadow_weight(p2,shadow_cascades[11].w);
    float shade = 0.0;
    [branch] if (w0 > 0.0) shade += w0*(1.0-fog_pcf(shadow0,p0,shadow_cascades[3]));
    [branch] if (w1 > 0.0) shade += w1*(1.0-fog_pcf(shadow1,p1,shadow_cascades[7]));
    [branch] if (w2 > 0.0) shade += w2*(1.0-fog_pcf(shadow2,p2,shadow_cascades[11]));
    return saturate(1.0-shade);
}

float4 field_sample(float3 world_position) {
    float3 u = frac(world_position / 32768.0) * 128.0 - 0.5;
    float z0 = floor(u.z);
    float fz = u.z - z0;
    z0 = z0 - 128.0 * floor(z0 / 128.0);
    float z1 = z0 + 1.0;
    z1 = z1 - 128.0 * floor(z1 / 128.0);
    float row0 = floor(z0 / 12.0), row1 = floor(z1 / 12.0);
    float2 uv0 = (float2(z0 - 12.0 * row0, row0) * 130.0 + u.xy + 1.5) / float2(1560.0,1430.0);
    float2 uv1 = (float2(z1 - 12.0 * row1, row1) * 130.0 + u.xy + 1.5) / float2(1560.0,1430.0);
    return lerp(tex2Dlod(atlas_texture,float4(uv0,0,0)),tex2Dlod(atlas_texture,float4(uv1,0,0)),fz);
}
bool geometry(float4 d) { return d.r >= 0.0 && d.r <= 1.0; }
// Ordered comparisons reject NaN and infinity without propagating them into weights.
bool valid_geometry_depth(float4 d) { return d.b > 0.0 && d.b <= 3.402823466e38; }
float4 march_pixel(float2 uv) {
    float4 depth = tex2Dlod(depth_texture,float4(uv,0,0));
    if (geometry(depth) && !valid_geometry_depth(depth)) return float4(0,0,0,1);
    float3 view = float3((2.0*uv.x-1.0-projection.z)/projection.x,
                        (1.0-2.0*uv.y-projection.w)/projection.y,1.0);
    float distance = geometry(depth) ? min(depth.b*length(view),sun_horizon.w) : sun_horizon.w;
    float3 direction = normalize(float3(dot(view,inverse_view0.xyz),dot(view,inverse_view1.xyz),dot(view,inverse_view2.xyz)));
    float phase_base = phase_gamma.x - phase_gamma.y * dot(direction,sun_horizon.xyz);
    float3 phase = (phase_gamma.z / (4.0 * phase_base * sqrt(phase_base))) * radiance_encode.rgb;
    float3 view_direction = normalize(view);
    float ds = distance / 24.0;
    float3 S = 0.0; float T = 1.0;
    [loop] for (int i=0; i<24; ++i) {
        float4 field = field_sample(camera_sigma.xyz + direction * (ds*(i+0.5)));
        float a = 1.0-exp(-camera_sigma.w*field.a*ds);
        float3 chroma = field.rgb/max(field.a,1e-8);
        // Empty field samples need neither projection nor map reads. Visibility
        // attenuates incident sun only; extinction and exact empty identity remain.
        float visibility = 1.0;
        [branch] if (field.a > 0.0 && shadow_select.x > 0.0)
            visibility = fog_visibility(view_direction * (ds*(i+0.5)));
        S += (T*a*phase)*chroma*visibility;
        T *= 1.0-a;
    }
    return float4(S,T);
}
