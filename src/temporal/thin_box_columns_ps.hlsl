// Sentinel stabiliser, second draw of the separable box (thin_box_rows_ps.hlsl
// wrote s2 = row minima + raw luma maximum, s3 = row maxima): the 7 taps of the
// pixel's COLUMN give the 7x7 minimum / maximum into the box targets the
// resolve reads at s9 / s10, on the pixels thin_box_ps.hlsl would draw (final
// mask s8, b > a; the rest are never read) and bit for bit what it would write.
// Emitter bound c23.x = E > 0: where the raw luma maximum of the 7x7 exceeds E
// and the pixel's own depth (s1) is the sentinel, the INNER 3x3 minimum /
// maximum of the current colour (s0) is written instead, so next to a laser, a
// trail, an explosion or a sun the history stays within one pixel of the tight
// clip. E = 0: no bound. Point / clamp, one level; c4 = 1 / size.
// Fail safe: a box with no finite tap (every tap of the chosen box non-finite,
// which includes the pixel's own sample, so the resolve is current-only there
// and does not read it) would be the inverted neutral pair (c6.z, -c6.z); it is
// written as (0, 0) instead, so no reader can ever clamp to -c6.z.
// X3M_REGION_HOLD_MASK (thin_box_columns_hold_ps.hlsl; A', resolve.hlsl X3M_REGION_HOLD): s8 is the mask's
// TESTS target (r = screen openness, a = camera openness, b = the flag / class code) and the resolve composes the
// region itself, so the box opens where the tests texel lets the camera term add strength: the sentinel class (b code 1/255 or 1), or camera openness above screen openness (a > r) inside the region. Computed texels are
// marked COLOR0.a = 1 (0 elsewhere), which the resolve reads before using the box; it takes the 3x3 clip where unmarked.
sampler2D currentColor : register(s0);
sampler2D currentDepth : register(s1);
sampler2D rowLow : register(s2);
sampler2D rowHigh : register(s3);
sampler2D lineMask : register(s8);
float4 sizeJitter : register(c4);
float4 rejection : register(c6);
float4 luminance : register(c22);
float4 emitter : register(c23); // E, unused
static const float3 lumaWeights = float3(0.2126, 0.7152, 0.0722);
float lumaFloored(float3 c) { return max(dot(c, lumaWeights), 0); }
float4 fetch(sampler2D s, float2 uv) { return tex2Dlod(s, float4(uv, 0, 0)); }
bool finiteColor(float3 v) { return all(v == v) && all(abs(v) <= rejection.z); }
bool sentinelDepth(float v) { return v <= -0.5 && v >= -1e30; }
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
#define X3M_SKIPPED_ALPHA 0
#else
bool boxOpen(float4 m, float2 uv) { return m.b > m.a; }
#define X3M_SKIPPED_ALPHA 1
#endif
struct BoxOutput { float4 low : COLOR0; float4 high : COLOR1; };
BoxOutput main(float2 uv : TEXCOORD0) {
    BoxOutput o;
    o.low = float4(0, 0, 0, X3M_SKIPPED_ALPHA);
    o.high = float4(0, 0, 0, 1);
    float4 mask = fetch(lineMask, uv);
    [branch] if (boxOpen(mask, uv)) {
        float3 low = rejection.z, high = -rejection.z;
        float rawMax = 0;
        [loop] for (int ny = -3; ny <= 3; ++ny) {
            float2 at = float2(uv.x, uv.y + ny * sizeJitter.y);
            float4 rowMinimum = fetch(rowLow, at);
            low = min(low, rowMinimum.rgb); high = max(high, fetch(rowHigh, at).rgb);
            rawMax = max(rawMax, rowMinimum.a);
        }
        [branch] if (emitter.x > 0 && rawMax > emitter.x && sentinelDepth(fetch(currentDepth, uv).r)) {
            low = rejection.z; high = -rejection.z;
            [loop] for (int my = -1; my <= 1; ++my) {
                [loop] for (int mx = -1; mx <= 1; ++mx) {
                    float3 neighbor = fetch(currentColor, uv + float2(mx, my) * sizeJitter.xy).rgb;
                    if (finiteColor(neighbor)) {
                        neighbor = weigh(neighbor);
                        low = min(low, neighbor); high = max(high, neighbor);
                    }
                }
            }
        }
        if (any(low > high)) { low = 0; high = 0; }
        o.low.rgb = low;
#ifdef X3M_REGION_HOLD_MASK
        o.low.a = 1; // computed: the resolve may use this box
#endif
        o.high.rgb = high;
    }
    return o;
}
