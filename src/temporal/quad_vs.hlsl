// Pass-through vertex program of every full-screen quad the proxy draws
// (temporal resolve, sharpen, copy draws, HDR write-back / tonemap / meter
// chain, self tests and the sentinel fill). D3D9 pairs ps_3_0 programs with a
// vs_3_0 program; the fixed-function XYZRHW path is not a documented partner
// for them. The host supplies clip-space positions already shifted by half a
// pixel (src/renderer/quad_vertex_program.h, quad_vertices), so nothing here
// depends on the target size. Compiled with tools/shaders/generate_rigid_motion_pixel.py
// (target vs_3_0) into src/renderer/quad_vertex_program_inc.h.
void main(float4 position : POSITION, float2 texcoord : TEXCOORD0,
          out float4 outPosition : POSITION, out float2 outTexcoord : TEXCOORD0) {
    outPosition = position;
    outTexcoord = texcoord;
}
