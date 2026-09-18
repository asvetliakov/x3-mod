// Current-depth fragment appended to the transformed material pixel programs
// (src/renderer/material_motion.cpp). The vertex variant exports the current
// clip z and w of the same position the rasterizer used, interpolated with
// perspective correction; z/w per pixel then equals the screen-linear device
// depth the rasterizer writes for the same sample (MinZ 0, MaxZ 1 viewport),
// so RT2 carries ordinary device depth in .r ([0,1]) wherever a routed draw
// covered the pixel and keeps the fill sentinel (-1) elsewhere. The lanes .b
// and .a carry the interpolated clip w itself (the linear view depth): the
// sun-shadow lane's A32B32G32R32F RT2 keeps .b as the apply quad's precise
// receiver depth (docs/architecture/shadow-receiver-depth.md); an R32F or
// G32R32F RT2 drops those lanes at the format, so one fragment serves every
// mode and .r is produced by the same two instructions in all of them. No
// literal: the transformer relocates only the input, one temporary and the
// color output, and the fragment must add no constant to the pixel ABI range.
float4 main(float2 clip : TEXCOORD1) : COLOR0 {
    return float4((clip.x / clip.y).xx, clip.yy);
}
