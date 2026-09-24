// Include body of thin_box_rows_hold_ps.hlsl, the only program built from it (X3M_REGION_HOLD_MASK below): the ungated
// form (b > a on the dilated chain's composed mask) was removed with that chain (2026-09-24, A' only).
// Sentinel stabiliser (docs/architecture/temporal-integration.md, "Distant
// unrouted stations under a pan"): the separable form of thin_box_ps.hlsl, used
// by TemporalPass only while the stabiliser is on (the box then covers most of
// the sky; 7 + 14 fetches instead of 49). This draw: per pixel, over the 7 taps
// of its ROW, COLOR0 = (minimum rgb, the raw luma maximum), COLOR1 = (maximum
// rgb, 1), every tap weighed and filtered exactly as thin_box_ps.hlsl does
// (c22.x = k; |v| <= c6.z), clamp addressing. A row result is read by the
// columns pass only from the pixels up to three rows away in its column whose
// final mask (s8, bound before this draw) opens the box (b > a); a pixel with no
// such pixel among those seven (clamped at the frame edge exactly as the
// columns pass clamps its taps) is discarded, its row texels never read
// (docs/architecture/engine-frame-time.md, "TAA stage cost"). Minimum / maximum
// are exactly separable and FP16 rounding is monotone, so the columns pass
// reproduces the 49-tap box bit for bit. A row with no finite tap writes
// (c6.z, -c6.z, luma 0), the neutral elements, as the 49-tap loop starts from.
// X3M_REGION_HOLD_MASK (thin_box_rows_hold_ps.hlsl; A', resolve.hlsl X3M_REGION_HOLD): s8 is the mask's
// TESTS target (r = screen openness, a = camera openness, b = the flag / class code) and the resolve composes the
// region itself, so the box opens where the tests texel lets the camera term add strength: the sentinel class (b code 1/255 or 1), or camera openness above screen openness (a > r) inside the region. Computed texels are
// marked COLOR0.a = 1 (0 elsewhere), which the resolve reads before using the box; it takes the 3x3 clip where unmarked.
sampler2D currentColor : register(s0);
sampler2D lineMask : register(s8);
float4 sizeJitter : register(c4);
float4 rejection : register(c6);
float4 luminance : register(c22);
static const float3 lumaWeights = float3(0.2126, 0.7152, 0.0722);
float lumaFloored(float3 c) { return max(dot(c, lumaWeights), 0); }
bool finiteColor(float3 v) { return all(v == v) && all(abs(v) <= rejection.z); }
float3 weigh(float3 c) { return c * (luminance.x > 0 ? 1 / (1 + luminance.x * lumaFloored(c)) : 1); }
#ifdef X3M_REGION_HOLD_MASK
// Region membership at a texel: this frame's flag (b code above 0.5), or the previous frame's region hold at the same texel
// (s7, the age target the resolve wrote last frame, unreprojected: one frame late at the region's moving edge, where the
// resolve then takes the 3x3 clip; resolve.hlsl X3M_REGION_HOLD, the hold in the low 7 bits of the 16-bit fraction).
sampler2D previousAge : register(s7);
bool inRegion(float4 m, float2 uv) {
    if (m.b > 0.5) return true;
    float v = abs(tex2Dlod(previousAge, float4(uv, 0, 0)).r);
    float held = v <= 65 ? frac(v) * 65536 : 0;
    return held - 128 * floor(held * (1.0 / 128)) > 0;
}
// The sentinel class (code 1/255 or 1; the stabiliser is on whenever these programs run), or camera openness above screen
// openness inside the region.
bool boxOpen(float4 m, float2 uv) { return (m.b > 0.5 / 255 && (m.b < 1.5 / 255 || m.b > 254.5 / 255)) || (m.a > m.r && inRegion(m, uv)); }
#else
#error "include body of thin_box_rows_hold_ps.hlsl only: the ungated box program was removed with the dilated chain"
#endif
struct BoxOutput { float4 low : COLOR0; float4 high : COLOR1; };
BoxOutput main(float2 uv : TEXCOORD0) {
    BoxOutput o;
    // The pixel's own mask first (a sky pixel of the stabiliser opens there), then outward; the columns pass reads a row
    // texel at uv.y + ny * sizeJitter.y, ny = -3..3, so the readers of this texel are the pixels at uv.y - ny * sizeJitter.y.
    float4 own = tex2Dlod(lineMask, float4(uv, 0, 0));
    float needed = boxOpen(own, uv) ? 1 : 0;
    [loop] for (int ny = 1; ny <= 3 && needed < 0.5; ++ny) {
        float2 upper = float2(uv.x, uv.y - ny * sizeJitter.y), lower = float2(uv.x, uv.y + ny * sizeJitter.y);
        float4 above = tex2Dlod(lineMask, float4(upper, 0, 0));
        float4 below = tex2Dlod(lineMask, float4(lower, 0, 0));
        if (boxOpen(above, upper) || boxOpen(below, lower)) needed = 1;
    }
    // Discarded (no write) and, under the branch, not computed: a discard alone need not skip the taps on SIMD hardware.
    clip(needed - 0.5);
    o.low = 0; o.high = 0;
    [branch] if (needed > 0.5) {
        float3 low = rejection.z, high = -rejection.z;
        float rawMax = 0;
        [loop] for (int nx = -3; nx <= 3; ++nx) {
            float3 neighbor = tex2Dlod(currentColor, float4(uv.x + nx * sizeJitter.x, uv.y, 0, 0)).rgb;
            if (finiteColor(neighbor)) {
                rawMax = max(rawMax, lumaFloored(neighbor));
                neighbor = weigh(neighbor);
                low = min(low, neighbor); high = max(high, neighbor);
            }
        }
        o.low = float4(low, rawMax);
        o.high = float4(high, 1);
    }
    return o;
}
