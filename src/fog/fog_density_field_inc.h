// Stored-density family field (docs/architecture/fog-density-runtime-integration.md, sections 2-3).
// c0 is corrected exactly once by the caller for jitter + quad centres.
// Two toroidal 1032x516 RGBA16F atlases: 32 Z groups of four nodes in RGBA lanes,
// 8x4 tiles of 129x129 texels (column/row 128 duplicates storage 0), storage = node mod 128.
sampler2D depth_texture : register(s0);
sampler2D fine_atlas : register(s1); // 512-unit nodes; LINEAR (POINT with FOG_DENSITY_EXACT_TEXELS)
sampler2D far_atlas : register(s7);  // 4096-unit nodes
float4 projection : register(c0);
float4 sizes : register(c1); // full W,H,half W,H
float4 camera_sigma : register(c2); // xyz unused, sigma (strength and far readiness folded in by the caller)
float4 sun_horizon : register(c3); // sun direction, horizon 200000
float4 inverse_view0 : register(c4);
float4 inverse_view1 : register(c5);
float4 inverse_view2 : register(c6);
float4 phase_gamma : register(c7); // 1+g*g, 2*g, 1-g*g, decode exponent
float4 radiance_encode : register(c8); // E/pi RGB, reciprocal decode exponent
float4 fine_local : register(c22); // camera modulo 128*512 (centred, |x| <= 64*512), 1/512
float4 far_local : register(c23);  // camera modulo 128*4096 (centred), 1/4096
float4 chroma_ready : register(c24); // family mean chroma RGB, fine readiness 0..1

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

// Trilinear reconstruction of the stored nodes of one level at camera-relative
// offset `ray` (world axes). Hardware bilinear in XY, shader lerp across the two
// Z lanes; group 31 lane 3 wraps to group 0 lane 0. Two fetches (eight texel-exact
// fetches with FOG_DENSITY_EXACT_TEXELS, the verification-only parity variant).
float4 lane_fetch(sampler2D atlas, float2 tile, float2 xy) {
    return tex2Dlod(atlas,float4((tile*129.0+xy)/float2(1032.0,516.0),0,0));
}
float slice_fetch(sampler2D atlas, float z, float2 sxy, float2 f) {
    float group = floor(z*0.25), row = floor(group*0.125);
    float4 mask = 1.0-saturate(abs(float4(0,1,2,3)-(z-4.0*group))); // integer lane: exactly 0 or 1
    float2 tile = float2(group-8.0*row,row);
#ifdef FOG_DENSITY_EXACT_TEXELS
    float a = dot(lane_fetch(atlas,tile,sxy+0.5),mask), b = dot(lane_fetch(atlas,tile,sxy+float2(1.5,0.5)),mask);
    float c = dot(lane_fetch(atlas,tile,sxy+float2(0.5,1.5)),mask), d = dot(lane_fetch(atlas,tile,sxy+1.5),mask);
    return lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y);
#else
    return dot(lane_fetch(atlas,tile,sxy+f+0.5),mask);
#endif
}
float level_sample(sampler2D atlas, float4 local, float3 ray) {
    float3 q = (local.xyz+ray)*local.w;
    float3 b = floor(q), f = q-b;
    float3 s = 128.0*frac(b/128.0); // node mod 128, exact: b is an integer below 2^16
    float z1 = s.z >= 127.0 ? 0.0 : s.z+1.0;
    return lerp(slice_fetch(atlas,s.z,s.xy,f.xy),slice_fetch(atlas,z1,s.xy,f.xy),f.z);
}
bool geometry(float4 d) { return d.r >= 0.0 && d.r <= 1.0; }
// Ordered comparisons reject NaN and infinity without propagating them into weights.
bool valid_geometry_depth(float4 d) { return d.b > 0.0 && d.b <= 3.402823466e38; }
// One loop of 64 bins: 24 over [0,min(L,12000)], 40 over [12000,L] (zero width, no
// reads, when L <= 12000). Fine level to 30000, far from 20000, horizon taper
// 150000-200000. A single loop inlines each level sampler and fog_visibility once.
float4 march_depth(float2 uv, float4 depth) {
    if (geometry(depth) && !valid_geometry_depth(depth)) return float4(0,0,0,1);
    float3 view = float3((2.0*uv.x-1.0-projection.z)/projection.x,
                        (1.0-2.0*uv.y-projection.w)/projection.y,1.0);
    float distance = geometry(depth) ? min(depth.b*length(view),sun_horizon.w) : sun_horizon.w;
    float3 direction = normalize(float3(dot(view,inverse_view0.xyz),dot(view,inverse_view1.xyz),dot(view,inverse_view2.xyz)));
    float phase_base = phase_gamma.x - phase_gamma.y * dot(direction,sun_horizon.xyz);
    float3 phase = (phase_gamma.z / (4.0 * phase_base * sqrt(phase_base))) * radiance_encode.rgb;
    float3 view_direction = normalize(view);
    float near_step = min(distance,12000.0)/24.0, far_step = max(distance-12000.0,0.0)/40.0;
    float lit = 0.0, T = 1.0;
    [loop] for (int i=0; i<64; ++i) {
        float ds = i < 24 ? near_step : far_step;
        float s = (i < 24 ? near_step*i : 12000.0+far_step*(i-24)) + 0.5*ds;
        [branch] if (ds > 0.0) {
            float t = saturate((s-20000.0)/10000.0);
            float lambda = (1.0-t*t*(3.0-2.0*t))*chroma_ready.w;
            float rho = 0.0;
            [branch] if (lambda > 0.0) rho += lambda*level_sample(fine_atlas,fine_local,direction*s);
            [branch] if (lambda < 1.0) rho += (1.0-lambda)*level_sample(far_atlas,far_local,direction*s);
            float e = saturate((s-150000.0)/50000.0);
            rho *= 1.0-e*e*(3.0-2.0*e);
            float a = 1.0-exp(-camera_sigma.w*rho*ds);
            // Empty samples need neither projection nor map reads. Visibility
            // attenuates incident sun only; extinction and exact empty identity remain.
            float visibility = 1.0;
#ifndef FOG_DENSITY_NO_SHAFTS // verification-only parity variant; production always compiles shafts
            [branch] if (rho > 0.0 && shadow_select.x > 0.0)
                visibility = fog_visibility(view_direction*s);
#endif
            lit += T*a*visibility;
            T *= 1.0-a;
        }
    }
    return float4(lit*phase*chroma_ready.rgb,T);
}
float4 march_pixel(float2 uv) { return march_depth(uv,tex2Dlod(depth_texture,float4(uv,0,0))); }
// Composite and repair share the half-footprint class law: the weight of half
// sample q for full pixel depth d (zero across geometry/sky classes and for
// invalid half depths), so both programs agree on which pixels need repair.
float footprint_weight(float4 d, float2 q, float w) {
    float4 hd = tex2Dlod(depth_texture,float4((q*2.0+0.5)/sizes.xy,0,0));
    if (geometry(d) != geometry(hd) || (geometry(hd) && !valid_geometry_depth(hd))) w = 0.0;
    // Preserve the existing relative-depth law within the compatible class.
    else if (geometry(d)) w *= exp(-abs(hd.b-d.b)/max(0.05*min(d.b,hd.b),1e-6)) + 1e-4;
    return w;
}
// Depth class per component: 0 sky, 1 valid geometry, 2 invalid geometry (NaN, infinity or <= 0;
// ordered comparisons, the same sets as geometry()/valid_geometry_depth()).
float4 depth_class(float4 r, float4 b) {
    float4 g = step(0.0,r)*step(r,1.0);
    return g*(2.0-(1.0-step(b,0.0))*step(b,3.402823466e38));
}
// Sign of the composite's weight only: a compatible tap contributes w*(exp(..)+1e-4) > 0
// for every nonzero bilinear weight (0.25, 0.5 or 1), so weight > 0 is exactly "some tap
// with w > 0 has the pixel's class and that class is not invalid". Returns 1 when the
// pixel needs the full-resolution march (valid pixel class, no compatible half sample).
float needs_repair(float4 d, float2 pixel) {
    float2 hp = pixel*0.5;
    float2 base = floor(hp), f = frac(hp), top = sizes.zw-1.0;
    // base >= 0, so only the upper clamp of the composite's footprint applies.
    float4 q01 = (min(base.xyxy+float4(0,0,1,0),top.xyxy)*2.0+0.5)/sizes.xyxy;
    float4 q23 = (min(base.xyxy+float4(0,1,1,1),top.xyxy)*2.0+0.5)/sizes.xyxy;
    float4 h0 = tex2Dlod(depth_texture,float4(q01.xy,0,0)), h1 = tex2Dlod(depth_texture,float4(q01.zw,0,0));
    float4 h2 = tex2Dlod(depth_texture,float4(q23.xy,0,0)), h3 = tex2Dlod(depth_texture,float4(q23.zw,0,0));
    float4 c = depth_class(float4(h0.r,h1.r,h2.r,h3.r),float4(h0.b,h1.b,h2.b,h3.b));
    float cd = depth_class(d.rrrr,d.bbbb).x;
    float4 w = float4((1.0-f.x)*(1.0-f.y),f.x*(1.0-f.y),(1.0-f.x)*f.y,f.x*f.y);
    float4 compatible = (1.0-step(w,0.0))*(1.0-abs(sign(c-cd)));
    return (cd < 2.0 && dot(compatible,1.0) <= 0.0) ? 1.0:0.0;
}
float4 fog_apply(float4 scene, float4 st) {
    return float4(pow(max(pow(max(scene.rgb,0.0),phase_gamma.w)*st.a+st.rgb,0.0),radiance_encode.w),scene.a);
}
