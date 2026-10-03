// Engine plumes (docs/architecture/engine-effects-modern.md section 3 and "Plume look redesign"): the vertex program of the stage's one
// indexed draw. The CPU builder (src/proxy/engine_plumes_core.h) already placed every corner in the camera's view
// space (the axial billboard turned about the plume axis to face the camera, the camera-facing nozzle disc); this
// program applies the jittered projection the routed draws use and passes the plume coordinates on. Clip z is half
// of (view z - NEAR), so the hardware's 0 <= z <= w clip is the near plane at NEAR; depth is never tested.
// Compiled with tools/shaders/generate_rigid_motion_pixel.py (target vs_3_0).
float4 projection : register(c0); // m00, m11, m20 + 2 jx / W, m21 - 2 jy / H
float4 limits : register(c1);     // NEAR (view units), unused x3
struct Input {
    float3 position : POSITION;   // view space
    float4 local : TEXCOORD0;     // u | x, w | y (world units), L (pulsed; the disc: the ring's radiance), the nozzle width n
    float4 shape : TEXCOORD1;     // halo sigma at the nozzle (nozzle widths), value, occlusion bias, the nozzle's flow phase
    float4 intensity : TEXCOORD2; // I_core, I_halo, the axis's view z component (the disc: the soft cap), the nozzle's view z
    float4 tint : COLOR0;         // mean colour, largest channel 1
    float4 params : COLOR1;       // throttle s, seed, I_ring / I_core / 2, the disc's weight (bytes)
    float4 fog : COLOR2;          // the fog transmittance per channel (white without fog), sin(view)
    float4 peak : COLOR3;         // the head colour (the body's peak at the mean's luminance), the kind (0 axial, 1 disc)
};
struct Output {
    float4 position : POSITION;
    float4 local : TEXCOORD0;
    float4 shape : TEXCOORD1;
    float4 view : TEXCOORD2;      // the nozzle's view z, I_core, I_halo, the axis's view z component
    float3 tint : TEXCOORD3;
    float4 params : TEXCOORD4;
    float4 fog : TEXCOORD5;
    float4 peak : TEXCOORD6;
};
Output main(Input i) {
    Output o;
    float z = i.position.z;
    o.position = float4(projection.x * i.position.x + projection.z * z, projection.y * i.position.y + projection.w * z,
                        0.5 * (z - limits.x), z);
    o.local = i.local;
    o.shape = i.shape;
    o.view = float4(i.intensity.w, i.intensity.xyz);
    o.tint = i.tint.rgb;
    o.params = i.params;
    o.fog = i.fog;
    o.peak = i.peak;
    return o;
}
