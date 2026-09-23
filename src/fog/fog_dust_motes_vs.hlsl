// Dust motes inside the stored-density fog (docs/architecture/fog-dust-motes.md): the vertex program of the one
// indexed draw of N capsule quads. A world-anchored periodic lattice (unit seeds scaled by the cube side W = 2R) seen
// through a camera-centred window: q = seed W - (camera mod W), wrapped to [-R,R)^3, plus a slow per-axis drift. The
// capsule runs from the mote's previous screen position (the previous fog frame's basis, camera and drift time,
// projected with this frame's projection) to its current one, clamped to STREAK pixels, width clamp(K/|q|,SIZE,MAX_PX).
// A culled mote puts all four corners on one point outside the clip volume. No vertex texture fetch, no instancing,
// no point sprites. Positions are window-exact (w = 1); D3D9 rasterises pixel (i,j) at window coordinate (i,j).
// Compiled with tools/shaders/generate_rigid_motion_pixel.py (target vs_3_0).
float4 projection : register(c0); // m00, m11, m20, m21 of the jittered projection routed draws use (no quad pixel-centre term)
float4 current[3] : register(c1); // world->view columns: view_j = dot(q, current[j].xyz); .w: W, 1/W, R
float4 previous[3] : register(c4); // the previous fog frame's; .w: NEAR, 1/(0.25 R), 1/NEAR
float4 origin : register(c7);     // camera mod W (xyz, in [0,W)), streak valid (0 or 1)
float4 motion : register(c8);     // camera - previous camera (xyz, render units), STREAK (pixels)
float4 drift : register(c9);      // drift phase now, previous (radians), DRIFT (units), brightness (GAIN x density scale x far ramp)
float4 shape : register(c10);     // SIZE, MAX_PX (pixels), K = SIZE x R, SOFT
float4 viewport : register(c11);  // W/2, H/2, 2/W, 2/H of the target
struct Output {
    float4 position : POSITION;
    float4 raster : TEXCOORD0;  // window position, capsule coordinates (along from the streak start, across), pixels
    float4 world : TEXCOORD1;   // camera-relative world position q, brightness
    float4 view : TEXCOORD2;    // view position, capsule radius (pixels)
    float4 capsule : TEXCOORD3; // streak length (pixels), SOFT, window position of the mote centre
};
Output main(float3 seed : TEXCOORD0, float2 corner : TEXCOORD1) {
    Output o;
    float3 base = seed*current[0].w - origin.xyz;
    base -= current[0].w*floor(base*current[1].w + 0.5);
    float3 phi = 6.2831853*frac(seed.zxy*13.0);
    float3 q = base + drift.z*sin(drift.x + phi);
    float3 qp = base + motion.xyz + drift.z*sin(drift.y + phi);
    float d = length(q);
    float fade = saturate((current[2].w - d)*previous[1].w)*saturate((d - previous[0].w)*previous[2].w);
    float3 v = float3(dot(q,current[0].xyz),dot(q,current[1].xyz),dot(q,current[2].xyz));
    float3 vp = float3(dot(qp,previous[0].xyz),dot(qp,previous[1].xyz),dot(qp,previous[2].xyz));
    float2 centre = (float2(1,-1)*(projection.xy*v.xy/max(v.z,1.0) + projection.zw) + 1.0)*viewport.xy;
    float2 before = (float2(1,-1)*(projection.xy*vp.xy/max(vp.z,1.0) + projection.zw) + 1.0)*viewport.xy;
    float2 streak = (origin.w*step(1.0,vp.z))*(centre - before);
    float span = length(streak);
    float L = min(span, motion.w);
    float2 axis = span > 1e-4 ? streak/max(span,1e-4) : float2(1,0);
    float size_px = clamp(shape.z/max(d,1e-3), shape.x, shape.y);
    float r = 0.5*size_px;
    float brightness = drift.w*fade*size_px/(size_px + L);
    float u = (0.5 + 0.5*corner.x)*(L + 2.0*r) - r, w = corner.y*r;
    float2 at = centre - axis*L + axis*u + float2(-axis.y,axis.x)*w;
    o.position = (v.z >= 1.0 && brightness > 0.0) ? float4(at*viewport.zw*float2(1,-1) + float2(-1,1),0.5,1.0) : float4(2.0,2.0,0.5,1.0);
    o.raster = float4(at,u,w);
    o.world = float4(q,brightness);
    o.view = float4(v,r);
    o.capsule = float4(L,shape.w,centre);
    return o;
}
