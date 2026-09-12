// Current-depth fragment appended to the transformed material pixel programs
// (src/renderer/material_motion.cpp). The vertex variant exports the current
// clip z and w of the same position the rasterizer used, interpolated with
// perspective correction; z/w per pixel then equals the screen-linear device
// depth the rasterizer writes for the same sample (MinZ 0, MaxZ 1 viewport),
// so RT2 (R32F) carries ordinary device depth in [0,1] wherever a routed draw
// covered the pixel and keeps the fill sentinel (-1) elsewhere. No literal:
// the transformer relocates only the input, one temporary and the color
// output, and the fragment must add no constant to the pixel ABI range.
float4 main(float2 clip : TEXCOORD1) : COLOR0 {
    return (clip.x / clip.y).xxxx;
}
