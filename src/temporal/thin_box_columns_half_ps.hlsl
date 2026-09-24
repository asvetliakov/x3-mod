// S4 (docs/architecture/taa-high-resolution.md S4, taa-plan-lifted-slot-cap.md step 2; X3M_TAA_BOX_RESOLUTION=half): the column
// draw of the camera gate's box at half resolution, the region-gated twin of thin_box_columns_hold_ps.hlsl, into the two box
// targets (W/2 x H/2) the resolve reads at s9 / s10 with POINT sampling at its full-resolution texel centre (texel (x >> 1,
// y >> 1) for an even W and H). Texel (bx, by) is the block of pixels 2bx..2bx+1 x 2by..2by+1; its 8x8 window (columns and
// rows 2b-3..2b+4) is the union of the four pixels' 7x7 windows, its COMMON 6x6 (2b-2..2b+3) their intersection and its
// inner 4x4 (2b-1..2b+2) the union of their 3x3. It reads the row pairs by-1..by+2 of thin_box_rows_half_ps.hlsl (rows
// 2by-3..2by+4): the min / max of the DIM taps (all taps with the emitter bound off) and where the bright ones lie.
// Containment: at every pixel of the block the box written here contains the box the full-resolution programs write for that
// pixel (clamped addressing keeps it: a clamped window is the in-frame part of the window). Without the emitter bound
// (c23.x = E = 0), or with no tap above E in the 8x8, it is the 8x8 box, which contains every 7x7. With a tap above E
// (the sentinel stabiliser's emitter bound: at full resolution a pixel on the sentinel whose 7x7 raw luma maximum exceeds E
// takes its inner 3x3, every other pixel its 7x7):
//   - all four pixels on the sentinel and a bright tap in the common 6x6: every open pixel takes its 3x3 at full resolution;
//     the block takes the inner 4x4, which contains each (the emitter's ghost stays within 2 px instead of 1);
//   - all four on the sentinel, the bright taps only in the outer ring: a pixel whose 7x7 reaches one takes its 3x3 (inside
//     the common 6x6, all dim), every other pixel its 7x7 (all dim); the block takes the 8x8 box of the dim taps, which
//     contains both and none of the bright taps;
//   - otherwise (a pixel off the sentinel keeps its whole 7x7): the 8x8 box of every tap, fetched here (64 taps; only blocks
//     that straddle a silhouette within 4 px of a bright tap).
// Gate: computed where ANY of the four pixels opens the full-resolution box (s8 the tests target, s7 last frame's age:
// thin_box_columns_ps.hlsl X3M_REGION_HOLD_MASK; the sentinel class only while the stabiliser runs, c23.y = 1), marked
// COLOR0.a = 1. The resolve takes the box only where the camera term added strength (b > a); a pixel whose own gate is closed
// takes the 3x3 clip at full resolution, and every box above contains that pixel's 3x3 too, so its bound is never tighter.
// Fail safe: a box with no finite tap is written as (0, 0), as the full-resolution columns do (its own sample is then not
// finite and the resolve does not read it).
// c4.xy = 1 / (W, H) of the FULL frame; c12.x = (H/2) / (H/2 + 1), c12.y = 1 / (H/2 + 1): the row target's scale and texel step.
// TEXCOORD0 = ((bx + 1/2) / (W/2), (by + 1/2) / (H/2)); pixel (2bx, 2by) is at TEXCOORD0 - c4.xy / 2.
sampler2D currentColor : register(s0);
sampler2D currentDepth : register(s1);
sampler2D rowLow : register(s2);
sampler2D rowHigh : register(s3);
sampler2D previousAge : register(s7);
sampler2D lineMask : register(s8);
float4 sizeJitter : register(c4);
float4 rejection : register(c6);
float4 halfRows : register(c12);
float4 luminance : register(c22);
float4 emitter : register(c23); // x = E of the emitter bound (0 = none), y = 1 while the sentinel stabiliser runs
static const float3 lumaWeights = float3(0.2126, 0.7152, 0.0722);
float lumaFloored(float3 c) { return max(dot(c, lumaWeights), 0); }
float4 fetch(sampler2D s, float2 uv) { return tex2Dlod(s, float4(uv, 0, 0)); }
bool finiteColor(float3 v) { return all(v == v) && all(abs(v) <= rejection.z); }
bool sentinelDepth(float v) { return v <= -0.5 && v >= -1e30; }
float3 weigh(float3 c) { return c * (luminance.x > 0 ? 1 / (1 + luminance.x * lumaFloored(c)) : 1); }
bool inRegion(float4 m, float2 uv) {
    if (m.b > 0.5) return true;
    float v = abs(fetch(previousAge, uv).r);
    float held = v <= 65 ? frac(v) * 65536 : 0;
    return held - 128 * floor(held * (1.0 / 128)) > 0;
}
bool boxOpen(float2 uv) {
    float4 m = fetch(lineMask, uv);
    return (emitter.y > 0.5 && m.b > 0.5 / 255 && (m.b < 1.5 / 255 || m.b > 254.5 / 255)) || (m.a > m.r && inRegion(m, uv));
}
struct BoxOutput { float4 low : COLOR0; float4 high : COLOR1; };
BoxOutput main(float2 uv : TEXCOORD0) {
    BoxOutput o;
    o.low = float4(0, 0, 0, 0);
    o.high = float4(0, 0, 0, 1);
    const float2 p = uv - 0.5 * sizeJitter.xy; // pixel (2bx, 2by)
    const float2 right = float2(sizeJitter.x, 0), down = float2(0, sizeJitter.y);
    [branch] if (boxOpen(p) || boxOpen(p + right) || boxOpen(p + down) || boxOpen(p + right + down)) {
        float3 low = rejection.z, high = -rejection.z;
        float anyBright = 0, commonBright = 0;
        [unroll] for (int k = -1; k <= 2; ++k) {
            const float2 at = float2(uv.x, uv.y * halfRows.x + k * halfRows.y);
            const float4 pairLow = fetch(rowLow, at), pairHigh = fetch(rowHigh, at);
            low = min(low, pairLow.rgb); high = max(high, pairHigh.rgb);
            // code = upper + 2 lower + 4 any. The common 6x6 rows 2by-2..2by+3: the lower row of pair by-1, both rows of
            // by and by+1, the upper row of by+2.
            const float code = pairLow.a, upper = frac(code * 0.5) > 0.25 ? 1 : 0, lower = frac(floor(code * 0.5) * 0.5) > 0.25 ? 1 : 0;
            anyBright = max(anyBright, code > 3.5 ? 1 : 0);
            if (k >= 0) commonBright = max(commonBright, upper);
            if (k <= 1) commonBright = max(commonBright, lower);
        }
        [branch] if (anyBright > 0.5) {
            const bool sky = sentinelDepth(fetch(currentDepth, p).r) && sentinelDepth(fetch(currentDepth, p + right).r) &&
                             sentinelDepth(fetch(currentDepth, p + down).r) && sentinelDepth(fetch(currentDepth, p + right + down).r);
            [branch] if (!sky || commonBright > 0.5) {
                // The inner 4x4 on the sky, the 8x8 of every tap across a silhouette.
                const float first = sky ? -1 : -3, last = sky ? 2 : 4;
                low = rejection.z; high = -rejection.z;
                [loop] for (float my = first; my <= last; ++my) {
                    [loop] for (float mx = first; mx <= last; ++mx) {
                        float3 neighbor = fetch(currentColor, p + float2(mx, my) * sizeJitter.xy).rgb;
                        if (finiteColor(neighbor)) {
                            neighbor = weigh(neighbor);
                            low = min(low, neighbor); high = max(high, neighbor);
                        }
                    }
                }
            }
        }
        if (any(low > high)) { low = 0; high = 0; }
        o.low = float4(low, 1); // computed: the resolve may use this box
        o.high.rgb = high;
    }
    return o;
}
