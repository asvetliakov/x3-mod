// The sun-visibility slice grid (docs/architecture/fog-shadow-pass.md): one quad over the 4x4-tile RGBA8 atlas, a
// quarter-resolution texel per 4x4 full pixels, tile t holding slices 4t..4t+3 in the RGBA lanes. Per texel: the
// march's view ray through the texel (the same c0 row and pixel law, full pixel 4q+2), then for each of the tile's
// four slices four stratified positions along the slice, each the three-cascade .85-.95 cross-fade of a 2x2 filtered
// comparison. Tap 0 sits on the ray and the nearest of its four depths gives the blocker distance; taps 1-3 add a
// light-space disc offset of that penumbra radius (0.0093 x the distance: the 0.53 degree sun; found on the shadow
// side of an edge only, so the penumbra grows from the edge into the shadow) at 120 degree steps. The stratum and the
// disc rotation come from the texel's interleaved-gradient noise plus the TAA phase (held without a resolve), so one
// bilinear read of the march averages 16 positions per slice and TAA integrates the phases. No depth is read: every
// texel holds a full column, so a bilinear read never meets an unwritten neighbour. Documented ps_3_0 only.
#define FOG_GRID_PASS
#include "fog_shadow_grid_inc.h"
sampler2D shadow0 : register(s4);
sampler2D shadow1 : register(s5);
sampler2D shadow2 : register(s6);
float4 projection : register(c0);
float4 sizes : register(c1); // full W,H,half W,H
// fog_pcf's 2x2 comparison (x) and the reference minus the nearest of its four depths (y): the blocker search.
float2 grid_pcf(sampler2D map, float3 p, float4 info) {
    float2 texel = float2(p.x,-p.y)*0.5*info.x + 0.5*info.x;
    float2 base = floor(texel), f = frac(texel);
    float4 uv = (base.xyxy+float4(0.5,0.5,1.5,1.5))*info.y;
    float4 d = float4(tex2Dlod(map,float4(uv.xy,0,0)).r,tex2Dlod(map,float4(uv.zy,0,0)).r,tex2Dlod(map,float4(uv.xw,0,0)).r,tex2Dlod(map,float4(uv.zw,0,0)).r);
    float reference = p.z-info.z;
    float4 lit = step(reference,d);
    return float2(lerp(lerp(lit.x,lit.y,f.x),lerp(lit.z,lit.w,f.x),f.y),reference-min(min(d.x,d.y),min(d.z,d.w)));
}
float4 main(float2 uv:TEXCOORD0):COLOR0 {
    float2 pixel = floor(uv/grid_layout.zw);
    float2 tile = floor((pixel+0.5)/grid_layout.xy);
    float2 q = pixel-tile*grid_layout.xy+0.5; // in-tile texel centre: full pixel 4q
    float2 full = q*4.0/sizes.xy;
    float3 view = float3((2.0*full.x-1.0-projection.z)/projection.x,(1.0-2.0*full.y-projection.w)/projection.y,1.0);
    float3 direction = normalize(view);
    float slice0 = 4.0*(tile.y*4.0+tile.x);
    // Projection is affine in the view position: light-space p_i(s) = a_i + s b_i along the ray.
    float3 b0 = float3(dot(direction,shadow_cascades[0].xyz),dot(direction,shadow_cascades[1].xyz),dot(direction,shadow_cascades[2].xyz));
    float3 b1 = float3(dot(direction,shadow_cascades[4].xyz),dot(direction,shadow_cascades[5].xyz),dot(direction,shadow_cascades[6].xyz));
    float3 b2 = float3(dot(direction,shadow_cascades[8].xyz),dot(direction,shadow_cascades[9].xyz),dot(direction,shadow_cascades[10].xyz));
    float3 a0 = float3(shadow_cascades[0].w,shadow_cascades[1].w,shadow_cascades[2].w);
    float3 a1 = float3(shadow_cascades[4].w,shadow_cascades[5].w,shadow_cascades[6].w);
    float3 a2 = float3(shadow_cascades[8].w,shadow_cascades[9].w,shadow_cascades[10].w);
    // Interleaved gradient noise of the atlas texel (the stratum) and its transpose (the disc rotation), advanced per phase.
    float noise = frac(52.9829189*frac(dot(pixel,float2(0.06711056,0.00583715))));
    float turn = frac(52.9829189*frac(dot(pixel,float2(0.00583715,0.06711056))));
    float xi = frac(noise+grid_penumbra.w);
    float2 spin; sincos(6.2831853*frac(turn+grid_penumbra.w),spin.y,spin.x);
    float2 side = float2(-spin.y,spin.x);
    float4 result = 0.0;
    [loop] for (int j=0; j<4; ++j) {
        float slice = slice0+j;
        float2 span = slice < 24.0 ? float2(grid_slices.x*slice,grid_slices.x) : float2(12000.0+grid_slices.y*(slice-24.0),grid_slices.y); // start, width
        float3 radius = grid_penumbra.y; // per cascade, texels of its map; tap 0 sets it
        float lit = 0.0;
        [loop] for (int k=0; k<4; ++k) {
            float s = span.x+(k+xi)*0.25*span.y;
            float2 disc = k < 1 ? 0.0 : k < 2 ? spin : k < 3 ? -0.5*spin+0.8660254*side : -0.5*spin-0.8660254*side;
            float3 p0 = a0+s*b0, p1 = a1+s*b1, p2 = a2+s*b2;
            float w0 = shadow_weight(p0,shadow_cascades[3].w);
            float w1 = (1.0-w0)*shadow_weight(p1,shadow_cascades[7].w);
            float w2 = (1.0-w0-w1)*shadow_weight(p2,shadow_cascades[11].w);
            float shade = 0.0;
            [branch] if (w0 > 0.0) {
                float2 r = grid_pcf(shadow0,p0+float3(disc*(2.0*radius.x*shadow_cascades[3].y),0.0),shadow_cascades[3]);
                radius.x = k < 1 ? clamp(r.y*grid_cascade[0].z*grid_penumbra.x,grid_penumbra.y,grid_penumbra.z) : radius.x;
                shade += w0*(1.0-r.x);
            }
            [branch] if (w1 > 0.0) {
                float2 r = grid_pcf(shadow1,p1+float3(disc*(2.0*radius.y*shadow_cascades[7].y),0.0),shadow_cascades[7]);
                radius.y = k < 1 ? clamp(r.y*grid_cascade[1].z*grid_penumbra.x,grid_penumbra.y,grid_penumbra.z) : radius.y;
                shade += w1*(1.0-r.x);
            }
            [branch] if (w2 > 0.0) {
                float2 r = grid_pcf(shadow2,p2+float3(disc*(2.0*radius.z*shadow_cascades[11].y),0.0),shadow_cascades[11]);
                radius.z = k < 1 ? clamp(r.y*grid_cascade[2].z*grid_penumbra.x,grid_penumbra.y,grid_penumbra.z) : radius.z;
                shade += w2*(1.0-r.x);
            }
            lit += 1.0-shade;
        }
        result += 0.25*lit*saturate(1.0-abs(float4(0,1,2,3)-j));
    }
    return result;
}
