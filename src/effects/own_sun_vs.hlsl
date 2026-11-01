// Own sun (docs/architecture/own-sun-pass.md sections 2 and 9): the vertex program of the stage's one quad. The CPU
// (src/proxy/own_sun_core.h quad()) already placed the four corners in clip space with the jittered projection the routed
// draws use (w = 1, z = 0: depth is never tested) and gives each its disc coordinate (offset from the sun in disc radii);
// this program passes both on. Compiled with tools/shaders/generate_own_sun_programs.py (target vs_3_0).
struct Input {
    float4 position : POSITION; // clip space
    float2 local : TEXCOORD0;   // offset from the sun's centre in disc radii (+-3 at the corners)
};
struct Output {
    float4 position : POSITION;
    float2 local : TEXCOORD0;
};
Output main(Input i) {
    Output o;
    o.position = i.position;
    o.local = i.local;
    return o;
}
