// S4 (docs/architecture/taa-high-resolution.md S4, taa-plan-lifted-slot-cap.md step 2; X3M_TAA_BOX_RESOLUTION=half): the column
// draw of the camera gate's box at half resolution into the two box targets (W/2 x H/2) the resolve reads at s9 / s10 with POINT
// sampling at its full-resolution texel centre (texel (x >> 1, y >> 1) for an even W and H). Texel (bx, by) is the block of
// pixels 2bx..2bx+1 x 2by..2by+1; its 8x8 window (columns and rows 2b-3..2b+4) is the union of the four pixels' 7x7 windows.
// It reads the row pairs by-1..by+2 of thin_box_rows_half_ps.hlsl (rows 2by-3..2by+4) and writes their min / max: the 8x8 box.
// Containment: at every pixel of the block the box written here contains the box the full-resolution program writes for that
// pixel (clamped addressing keeps it: a clamped window is the in-frame part of the window).
// Gate: computed where ANY of the four pixels opens the full-resolution box (thin_box_ps.hlsl X3M_REGION_HOLD_MASK, A' with the
// mask fold: the previous frame's region hold at s7), marked COLOR0.a = 1. The resolve takes the box only where the camera term added strength (b > a); where b > a and the block was not computed it
// takes the 7x7 in place (resolve.hlsl), and a pixel whose own gate is closed takes the 3x3 clip, which every box above
// contains. Fail safe: a box with no finite tap is written as (0, 0), as the full-resolution program does (its own sample is
// then not finite and the resolve does not read it).
// c4.xy = 1 / (W, H) of the FULL frame; c12.x = (H/2) / (H/2 + 1), c12.y = 1 / (H/2 + 1): the row target's scale and texel step.
// TEXCOORD0 = ((bx + 1/2) / (W/2), (by + 1/2) / (H/2)); pixel (2bx, 2by) is at TEXCOORD0 - c4.xy / 2.
sampler2D rowLow : register(s2);
sampler2D rowHigh : register(s3);
sampler2D previousAge : register(s7);
float4 sizeJitter : register(c4);
float4 rejection : register(c6);
float4 halfRows : register(c12);
float4 fetch(sampler2D s, float2 uv) { return tex2Dlod(s, float4(uv, 0, 0)); }
bool heldRegion(float2 uv) {
    float v = abs(fetch(previousAge, uv).r);
    float held = v <= 65 ? frac(v) * 65536 : 0;
    return held - 128 * floor(held * (1.0 / 128)) > 0;
}
bool boxOpen(float2 uv) { return heldRegion(uv); }
struct BoxOutput { float4 low : COLOR0; float4 high : COLOR1; };
BoxOutput main(float2 uv : TEXCOORD0) {
    BoxOutput o;
    o.low = float4(0, 0, 0, 0);
    o.high = float4(0, 0, 0, 1);
    const float2 p = uv - 0.5 * sizeJitter.xy; // pixel (2bx, 2by)
    const float2 right = float2(sizeJitter.x, 0), down = float2(0, sizeJitter.y);
    [branch] if (boxOpen(p) || boxOpen(p + right) || boxOpen(p + down) || boxOpen(p + right + down)) {
        float3 low = rejection.z, high = -rejection.z;
        [unroll] for (int k = -1; k <= 2; ++k) {
            const float2 at = float2(uv.x, uv.y * halfRows.x + k * halfRows.y);
            low = min(low, fetch(rowLow, at).rgb); high = max(high, fetch(rowHigh, at).rgb);
        }
        if (any(low > high)) { low = 0; high = 0; }
        o.low = float4(low, 1); // computed: the resolve may use this box
        o.high.rgb = high;
    }
    return o;
}
