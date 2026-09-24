// Include body of thin_box_hold_ps.hlsl, the only program built from it (X3M_REGION_HOLD_MASK below): the ungated
// form (b > a on the dilated chain's composed mask) was removed with that chain (2026-09-24, A' only).
// 7x7 min / max box of the current colour for the camera-relative thin-region
// gate (resolve.hlsl X3M_CAMERA_GATE; docs/architecture/taa-lattice-crawl.md
// section 32.1). Drawn by TemporalPass after the mask draws and before the
// resolve into two owned A16B16G16R16F targets, COLOR0 the minimum and COLOR1
// the maximum (rgb; alpha 1), which the resolve reads at s9 / s10. s0 = the
// current colour (the resolve's own input), s8 = the final mask: a pixel whose
// camera-gated strength b does not exceed its screen-gated strength a is
// skipped (the resolve never reads its texels). Every tap is weighed exactly as
// the resolve weighs its clip statistics (c22.x = k), non-finite taps are
// skipped (|v| <= c6.z, the resolve's HDR limit), and taps outside the frame
// repeat the edge texel (clamp): the box is the set of colours present in the
// current 7x7, in the resolve's domain. c4 = 1 / size. Point / clamp, one level.
// X3M_REGION_HOLD_MASK (thin_box_hold_ps.hlsl; A', resolve.hlsl X3M_REGION_HOLD): s8 is the mask's
// TESTS target (r = screen openness, a = camera openness, b = the flag / class code) and the resolve composes the
// region itself, so the box opens where the tests texel lets the camera term add strength: camera openness above screen openness (a > r) inside the region. Computed texels are
// marked COLOR0.a = 1 (0 elsewhere), which the resolve reads before using the box; it takes the 3x3 clip where unmarked.
sampler2D currentColor : register(s0);
sampler2D lineMask : register(s8);
float4 sizeJitter : register(c4);
float4 rejection : register(c6);
float4 luminance : register(c22);
static const float3 lumaWeights = float3(0.2126, 0.7152, 0.0722);
float lumaFloored(float3 c) { return max(dot(c, lumaWeights), 0); }
float4 fetch(sampler2D s, float2 uv) { return tex2Dlod(s, float4(uv, 0, 0)); }
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
bool boxOpen(float4 m, float2 uv) { return m.a > m.r && inRegion(m, uv); }
#define X3M_SKIPPED_ALPHA 0
#else
#error "include body of thin_box_hold_ps.hlsl only: the ungated box program was removed with the dilated chain"
#endif
struct BoxOutput { float4 low : COLOR0; float4 high : COLOR1; };
BoxOutput main(float2 uv : TEXCOORD0) {
    BoxOutput o;
    o.low = float4(0, 0, 0, X3M_SKIPPED_ALPHA);
    o.high = float4(0, 0, 0, 1);
    float4 mask = fetch(lineMask, uv);
    [branch] if (boxOpen(mask, uv)) {
        float3 low = rejection.z, high = -rejection.z;
        [loop] for (int ny = -3; ny <= 3; ++ny) {
            [loop] for (int nx = -3; nx <= 3; ++nx) {
                float3 neighbor = fetch(currentColor, uv + float2(nx, ny) * sizeJitter.xy).rgb;
                if (finiteColor(neighbor)) {
                    neighbor = weigh(neighbor);
                    low = min(low, neighbor); high = max(high, neighbor);
                }
            }
        }
        o.low.rgb = low;
#ifdef X3M_REGION_HOLD_MASK
        o.low.a = 1; // computed: the resolve may use this box
#endif
        o.high.rgb = high;
    }
    return o;
}
