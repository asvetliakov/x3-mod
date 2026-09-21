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
#ifdef FOG_LOOK
// Look presets L1-L3 (docs/architecture/fog-density-runtime-integration.md, "Look presets"). FOG_LOOK 1 is
// the shaped law; 2 adds Beer-powder, the two-tap self-shadow and the sample offset (L3 = non-zero c31.zw).
// All rows come from renderer::fog_look_constants; the unshaped programs never declare them.
float4 look_remap : register(c25);      // coverage c, 1/(1-c), exponent p, sky column cap
float4 look_albedo : register(c26);     // scatter albedo RGB, shaft visibility floor
float4 look_ambient0 : register(c27);   // ambient radiance away from the sun RGB, multiple-scatter weight / 4
float4 look_ambient1 : register(c28);   // ambient radiance toward the sun RGB, multiple-scatter visibility floor
float4 look_lobe0 : register(c29);      // 1+g*g, 2*g, w*(1-g*g)/4 of the forward lobe
float4 look_lobe1 : register(c30);      // the same of the back lobe
float4 look_self : register(c31);       // self-shadow strength, powder strength, near and far offset scale
float4 look_taps : register(c32);       // sun-ward tap distance, the length it stands for
float4 look_extinction : register(c33); // extinction exponent RGB of T_rgb = pow(T,k), offset frame shift
float4 look_warp : register(c34);       // domain warp: octave 1 cycles per unit and amplitude, octave 2 the same
float4 look_edge : register(c35);       // coverage variation amplitude
#endif

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

#ifdef FOG_LOOK
// Look programs: two cascades (FogPass binds the two coarsest current maps to slots 0 and 1; the finest map
// spans a few fog bins only), the first that holds the point inside its blend-band start, one 2x2 comparison
// of that map. The three-way cross-fade above costs 207 instruction slots, which would put the look programs
// past the 512 a ps_3_0 device has to offer (CrossOver reports exactly 512). Same rows, bias and texel law.
float fog_look_visibility(float3 view_position) {
    float4 p = float4(view_position,1.0);
    float3 p0 = float3(dot(p,shadow_cascades[0]),dot(p,shadow_cascades[1]),dot(p,shadow_cascades[2]));
    float3 p1 = float3(dot(p,shadow_cascades[4]),dot(p,shadow_cascades[5]),dot(p,shadow_cascades[6]));
    float2 m = max(abs(float2(p0.x,p1.x)),abs(float2(p0.y,p1.y))), z = float2(p0.z,p1.z);
    float2 inside = float2(shadow_cascades[3].w,shadow_cascades[7].w)*step(m,shadow_select.z)*step(0.0,z)*step(z,1.0);
    inside.y *= 1.0-inside.x; // one-hot: valid flags are 0 or 1
    float3 q = p0*inside.x+p1*inside.y;
    float4 info = shadow_cascades[3]*inside.x+shadow_cascades[7]*inside.y;
    float2 texel = float2(q.x,-q.y)*0.5*info.x + 0.5*info.x;
    float2 base = floor(texel), f = frac(texel);
    float4 uv = (base.xyxy+float4(0.5,0.5,1.5,1.5))*info.y;
    float4 d = 3.0e38; // no cascade: lit
    [branch] if (inside.x > 0.0) d = float4(tex2Dlod(shadow0,float4(uv.xy,0,0)).r,tex2Dlod(shadow0,float4(uv.zy,0,0)).r,tex2Dlod(shadow0,float4(uv.xw,0,0)).r,tex2Dlod(shadow0,float4(uv.zw,0,0)).r);
    else [branch] if (inside.y > 0.0) d = float4(tex2Dlod(shadow1,float4(uv.xy,0,0)).r,tex2Dlod(shadow1,float4(uv.zy,0,0)).r,tex2Dlod(shadow1,float4(uv.xw,0,0)).r,tex2Dlod(shadow1,float4(uv.zw,0,0)).r);
    float4 lit = step(q.z-info.z,d);
    return lerp(lerp(lit.x,lit.y,f.x),lerp(lit.z,lit.w,f.x),f.y);
}
#endif

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
#ifdef FOG_LOOK
// Look programs: the same texels and the same trilinear weights as below in a smaller body (it helps keep
// the look programs inside 512 instruction slots). The four lanes of one texel are consecutive Z slices, so a
// tent over the lanes interpolates Z inside a group; lane 3 blends into lane 0 of the next group (31 wraps to
// 0). That second fetch stays unconditional: made conditional, this D3DX compiler emits a truncated program
// with no output write (125 slots, no oC0).
float level_sample(sampler2D atlas, float4 local, float3 ray) {
    float3 q = (local.xyz+ray)*local.w;
    float3 b = floor(q), f = q-b;
    float3 s = 128.0*frac(b/128.0); // node mod 128, exact: b is an integer below 2^16
    float group = floor(s.z*0.25), row = floor(group*0.125), lane = s.z-4.0*group+f.z;
    float next = group >= 31.0 ? 0.0 : group+1.0, next_row = floor(next*0.125);
    float4 texel = lane_fetch(atlas,float2(group-8.0*row,row),s.xy+f.xy+0.5);
    float above = lane_fetch(atlas,float2(next-8.0*next_row,next_row),s.xy+f.xy+0.5).r;
    return dot(texel,saturate(1.0-abs(float4(0,1,2,3)-lane)))+above*saturate(lane-3.0);
}
#else
float level_sample(sampler2D atlas, float4 local, float3 ray) {
    float3 q = (local.xyz+ray)*local.w;
    float3 b = floor(q), f = q-b;
    float3 s = 128.0*frac(b/128.0); // node mod 128, exact: b is an integer below 2^16
    float z1 = s.z >= 127.0 ? 0.0 : s.z+1.0;
    return lerp(slice_fetch(atlas,s.z,s.xy,f.xy),slice_fetch(atlas,z1,s.xy,f.xy),f.z);
}
#endif
bool geometry(float4 d) { return d.r >= 0.0 && d.r <= 1.0; }
// Ordered comparisons reject NaN and infinity without propagating them into weights.
bool valid_geometry_depth(float4 d) { return d.b > 0.0 && d.b <= 3.402823466e38; }
#ifndef FOG_LOOK
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
#else
// Soft edge: zero below the coverage, zero-slope toe (exponent >= 1.5). `cover` moves the coverage by a low
// frequency world-anchored term, so a cloud edge is not one iso-surface of the stored noise.
float look_density(float rho, float cover) {
    float x = saturate((rho-look_remap.x-cover)*look_remap.y);
    return x > 0.0 ? pow(x,look_remap.z) : 0.0; // exact zero below the coverage: empty samples stay empty
}
// Parabolic sine of period 1, range [-1,1].
float3 look_wave(float3 x) { float3 t = frac(x)-0.5; return t*(8.0-16.0*abs(t)); }
// The shaped law on the same 24+40 bins. Per pixel: two-lobe phase, two-colour ambient, sky column cap and
// (FOG_LOOK 2) the interleaved-gradient sample offset. Per sample: density remap, shaft visibility floor,
// the half-extinction isotropic octave, and (FOG_LOOK 2) two far-level taps toward the sun.
float4 march_depth(float2 uv, float4 depth) {
    if (geometry(depth) && !valid_geometry_depth(depth)) return float4(0,0,0,1);
    float3 view = float3((2.0*uv.x-1.0-projection.z)/projection.x,
                        (1.0-2.0*uv.y-projection.w)/projection.y,1.0);
    // Every ray ends at the column cap (look_remap.w), sky and geometry alike, so a distant hull and the sky
    // pixel beside it carry the same in-scatter; the taper keeps its 3:1 shape over the last quarter.
    float distance = min(geometry(depth) ? depth.b*length(view) : sun_horizon.w,min(sun_horizon.w,look_remap.w));
    float3 direction = normalize(float3(dot(view,inverse_view0.xyz),dot(view,inverse_view1.xyz),dot(view,inverse_view2.xyz)));
    float3 view_direction = normalize(view);
    // Scalars that live across the loop share registers (ps_3_0 has 32 temporaries and the compiler gives
    // every live scalar its own): steps = near step, far step, taper start, taper reciprocal.
    float4 steps = float4(min(distance,12000.0)/24.0,max(distance-12000.0,0.0)/40.0,0.75*look_remap.w,4.0/look_remap.w);
    float2 offset = 0.5;
#if FOG_LOOK >= 2 && !defined(FOG_LOOK_NO_OFFSET) // repair pixels (depth-class edges) keep the bin centres
    float2 cell = floor(uv*sizes.xy)*0.5 + look_extinction.w;
    offset += (frac(52.9829189*frac(dot(cell,float2(0.06711056,0.00583715)))) - 0.5)*look_self.zw;
#endif
    float3 sum = float3(0,0,1); // sun-lit, multiple-scatter lift, T
    [loop] for (int i=0; i<64; ++i) {
        float2 bin = i < 24 ? float2(steps.x,steps.x*(i+offset.x)) : float2(steps.y,12000.0+steps.y*(i-24+offset.y)); // ds, s
        [branch] if (bin.x > 0.0) {
            float t = saturate((bin.y-20000.0)/10000.0);
            float lambda = (1.0-t*t*(3.0-2.0*t))*chroma_ready.w;
            // Domain warp, no fetch: two octaves of a world-anchored periodic offset (whole cycles per fine
            // window of 65536 units, so camera_local's modulo never shows), each axis driven by the two others.
            // It bends the rounded, lattice-aligned value-noise silhouettes; the product also moves the coverage.
            float3 world = fine_local.xyz+direction*bin.y;
            float3 wave1 = look_wave(world.yzx*look_warp.x), wave2 = look_wave(world.zxy*look_warp.z);
            float3 ray = direction*bin.y+wave1*look_warp.y+wave2*look_warp.w;
            float rho = 0.0;
            [branch] if (lambda > 0.0) rho += lambda*level_sample(fine_atlas,fine_local,ray);
            [branch] if (lambda < 1.0) rho += (1.0-lambda)*level_sample(far_atlas,far_local,ray);
            float e = saturate((bin.y-steps.z)*steps.w);
            float cover = look_edge.x*wave1.x*wave2.y;
            rho = look_density(rho,cover)*(1.0-e*e*(3.0-2.0*e));
            float2 a = 1.0-exp(-camera_sigma.w*rho*bin.x*float2(1.0,0.5)); // extinction, half-extinction octave
            float2 light = 1.0; // shaft visibility, sun-ward self-shadow
            [branch] if (rho > 0.0) {
#ifndef FOG_DENSITY_NO_SHAFTS
                [branch] if (shadow_select.x > 0.0) light.x = fog_look_visibility(view_direction*bin.y);
#endif
#if FOG_LOOK >= 2
                // One far-level tap toward the sun (shadowing is low frequency and the far window covers it)
                // stands for the optical depth over look_taps.y units; a second tap needs an inner loop that
                // puts repair past 512 instruction slots. Powder also counts the sample's own density.
                float2 r = float2(look_density(level_sample(far_atlas,far_local,ray+sun_horizon.xyz*look_taps.x),cover)*look_taps.y,0.0);
                float tau = camera_sigma.w*look_self.x*r.x;
                light.y = exp(-tau)*(1.0-look_self.y*exp(-2.0*(tau+camera_sigma.w*look_self.x*rho*look_taps.x)));
#endif
            }
            sum.xy += sum.z*a*float2(light.y*lerp(look_albedo.w,1.0,light.x),lerp(look_ambient1.w,1.0,light.x));
            sum.z *= 1.0-a.x;
        }
    }
    float cosine = dot(direction,sun_horizon.xyz);
    float2 lobe = float2(look_lobe0.x-look_lobe0.y*cosine,look_lobe1.x-look_lobe1.y*cosine);
    float phase = look_lobe0.z/(lobe.x*sqrt(lobe.x)) + look_lobe1.z/(lobe.y*sqrt(lobe.y));
    float3 ambient = lerp(look_ambient0.rgb,look_ambient1.rgb,0.5+0.5*cosine);
    return float4(look_albedo.rgb*(radiance_encode.rgb*(phase*sum.x+look_ambient0.w*sum.y)+ambient*(1.0-sum.z)),sum.z);
}
#endif
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
#ifdef FOG_LOOK
    // Tinted extinction, analytic from the scalar T: exact for a homogeneous tint ratio; T = 1 stays 1.
    float3 Trgb = pow(max(st.a,1e-6),look_extinction.rgb);
    return float4(pow(max(pow(max(scene.rgb,0.0),phase_gamma.w)*Trgb+st.rgb,0.0),radiance_encode.w),scene.a);
#endif
    return float4(pow(max(pow(max(scene.rgb,0.0),phase_gamma.w)*st.a+st.rgb,0.0),radiance_encode.w),scene.a);
}
