// Engine ribbons, phase 3 (docs/architecture/engine-effects-modern.md sections 3 and 5): the vertex program of the
// stage's second indexed draw. The CPU builder (src/proxy/engine_ribbons_core.h) placed both edges of every strip point
// in the camera's view space; this program applies the jittered projection the routed draws and the plumes use (clip
// z = (view z - NEAR) / 2, so the hardware's 0 <= z <= w clip is the near plane; depth is never tested) and passes the
// strip coordinates on.
// Compiled with tools/shaders/generate_rigid_motion_pixel.py (target vs_3_0).
float4 projection : register(c0); // m00, m11, m20 + 2 jx / W, m21 - 2 jy / H
float4 limits : register(c1);     // NEAR (view units), unused x3
struct Input {
    float3 position : POSITION;   // view space
    float4 strip : TEXCOORD0;     // u (0 nozzle .. 1 tail), across (-1 .. 1), unused x2
    float4 shape : TEXCOORD1;     // radiance, centre-line view depth, value (the SOFT base), unused
    float4 tint : COLOR0;         // the mean colour x the fog transmittance
};
struct Output {
    float4 position : POSITION;
    float4 strip : TEXCOORD0;
    float4 shape : TEXCOORD1;
    float3 tint : TEXCOORD2;
};
Output main(Input i) {
    Output o;
    float z = i.position.z;
    o.position = float4(projection.x * i.position.x + projection.z * z, projection.y * i.position.y + projection.w * z,
                        0.5 * (z - limits.x), z);
    o.strip = i.strip;
    o.shape = i.shape;
    o.tint = i.tint.rgb;
    return o;
}
