// Dust motes inside the stored-density fog (docs/architecture/fog-dust-motes.md): the capsule's pixel program, drawn
// ONE/ONE on the fogged FP16 scene after the repair with the constants and samplers the repair leaves bound. The
// fog's own law at the mote centre: the look's remapped density (warp, coverage waves, taper), the two-lobe phase on
// the mote direction, the two-colour ambient and the sun visibility with its floor (the in-march lookup), times the
// capsule falloff and the RT2 occlusion of the pixel. Alpha 0: the scene's alpha is untouched. Included by
// fog_dust_motes_look_ps.hlsl.
#include "fog_density_field_inc.h"
float4 main(float4 raster : TEXCOORD0, float4 world : TEXCOORD1, float4 view : TEXCOORD2, float4 capsule : TEXCOORD3) : COLOR0 {
    float2 e = float2(raster.z - clamp(raster.z,0.0,capsule.x), raster.w);
    float cover = saturate(1.0 - dot(e,e)/(view.w*view.w));
    float3 colour = 0.0;
    [branch] if (cover > 0.0) {
        // The RT2 texel of this pixel (the interpolated window position is the pixel's integer centre).
        float4 d = tex2Dlod(depth_texture,float4((floor(raster.xy + 0.5) + 0.5)/sizes.xy,0,0));
        float occlusion = geometry(d) ? (valid_geometry_depth(d) ? saturate((d.b - view.z)/max(capsule.y*view.z,1e-4)) : 0.0) : 1.0;
        float distance = length(world.xyz);
        // Density: the march's warp, level sample and remap at the mote (|q| <= 5000 < 20000: lambda is the fine ramp).
        float3 anchored = fine_local.xyz + world.xyz;
        float3 ray = world.xyz + look_wave(anchored.yzx*look_warp.x)*look_warp.y + look_wave(anchored.zxy*look_warp.z)*look_warp.w;
        float lambda = chroma_ready.w, rho = 0.0;
        [branch] if (lambda > 0.0) rho += lambda*level_sample(fine_atlas,fine_local,ray);
        [branch] if (lambda < 1.0) rho += (1.0 - lambda)*level_sample(far_atlas,far_local,ray);
        float3 warped = fine_local.xyz + ray;
        float cover_shift = dot(look_wave(float3(dot(warped,float3(1,-2,1)/65536.0),dot(warped,float3(2,1,-1)/65536.0),dot(warped,float3(-1,1,2)/65536.0))),look_edge.x);
        float taper = saturate((distance - look_edge.y)*look_edge.z);
        float scale = look_density(rho,cover_shift)*(1.0 - taper*taper*(3.0 - 2.0*taper))*world.w*cover*occlusion;
        [branch] if (scale > 0.0) {
            float cosine = dot(world.xyz,sun_horizon.xyz)/max(distance,1e-3);
            float2 lobe = float2(look_lobe0.x - look_lobe0.y*cosine,look_lobe1.x - look_lobe1.y*cosine);
            float phase = look_lobe0.z/(lobe.x*sqrt(lobe.x)) + look_lobe1.z/(lobe.y*sqrt(lobe.y));
            float visibility = 1.0;
            [branch] if (shadow_select.x > 0.0) visibility = fog_look_visibility(view.xyz);
            float3 ambient = lerp(look_ambient0.rgb,look_ambient1.rgb,0.5 + 0.5*cosine);
            colour = look_albedo.rgb*(radiance_encode.rgb*phase*lerp(look_albedo.w,1.0,visibility) + ambient)*scale;
        }
    }
    return float4(pow(max(colour,0.0),radiance_encode.w),0.0);
}
