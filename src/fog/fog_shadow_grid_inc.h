// The sun-shadow rows and comparison helpers of every stored-density program (the sun-visibility slice grid that
// also lived here went with the fog shadow pass on 2026-09-25). Included by fog_density_field_inc.h.
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
