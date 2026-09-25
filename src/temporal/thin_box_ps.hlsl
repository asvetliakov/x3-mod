// Include body of thin_box_hold_ps.hlsl, the only program built from it (X3M_REGION_HOLD_MASK below): the ungated
// form (b > a on the dilated chain's composed mask) was removed with that chain (2026-09-24, A' only).
// 7x7 min / max box of the current colour for the camera-relative thin-region
// gate (resolve.hlsl X3M_CAMERA_GATE; docs/architecture/taa-lattice-crawl.md
// section 32.1). Drawn by TemporalPass before the resolve into two owned
// A16B16G16R16F targets, COLOR0 the minimum and COLOR1 the maximum (rgb), which
// the resolve reads at s9 / s10. s0 = the current colour (the resolve's own
// input). Every tap is weighed exactly as the resolve weighs its clip statistics
// (c22.x = k), non-finite taps are skipped (|v| <= c6.z, the resolve's HDR
// limit), and taps outside the frame repeat the edge texel (clamp): the box is the
// set of colours present in the current 7x7, in the resolve's domain. c4 = 1 /
// size. Point / clamp, one level.
// X3M_REGION_HOLD_MASK (thin_box_hold_ps.hlsl; A' with the mask fold, resolve.hlsl X3M_REGION_HOLD and
// docs/architecture/taa-mask-fold.md section 4.3): the box opens on the previous frame's region hold at the same texel (s7,
// unreprojected: the age target the resolve wrote). A pixel's first frame in the region (a new flag of any source: search,
// vote or emissive) is not open here; the resolve takes the same 7x7 in place there. The same-frame thin vote of the design
// is not read: its 16-byte lane fetch per gate test cost 1.7 ms in the half-resolution pair at 5120x1440 (FOLD_TIMING), and
// the in-place box gives those pixels the reference box. Computed texels are marked COLOR0.a = 1 (0 elsewhere), which the
// resolve reads before using the box.
sampler2D currentColor : register(s0);
float4 sizeJitter : register(c4);
float4 rejection : register(c6);
float4 luminance : register(c22);
static const float3 lumaWeights = float3(0.2126, 0.7152, 0.0722);
float lumaFloored(float3 c) { return max(dot(c, lumaWeights), 0); }
float4 fetch(sampler2D s, float2 uv) { return tex2Dlod(s, float4(uv, 0, 0)); }
bool finiteColor(float3 v) { return all(v == v) && all(abs(v) <= rejection.z); }
float3 weigh(float3 c) { return c * (luminance.x > 0 ? 1 / (1 + luminance.x * lumaFloored(c)) : 1); }
#ifdef X3M_REGION_HOLD_MASK
sampler2D previousAge : register(s7);
// The previous frame's region hold at the same texel (the age target the resolve wrote last frame, the hold in the low 7 bits of
// the 16-bit fraction; resolve.hlsl X3M_REGION_HOLD). A NaN or a count above 65 reads as no hold.
bool heldRegion(float2 uv) {
    float v = abs(fetch(previousAge, uv).r);
    float held = v <= 65 ? frac(v) * 65536 : 0;
    return held - 128 * floor(held * (1.0 / 128)) > 0;
}
bool boxOpen(float2 uv) { return heldRegion(uv); }
#else
#error "include body of thin_box_hold_ps.hlsl only: the ungated box program was removed with the dilated chain"
#endif
struct BoxOutput { float4 low : COLOR0; float4 high : COLOR1; };
BoxOutput main(float2 uv : TEXCOORD0) {
    BoxOutput o;
    o.low = float4(0, 0, 0, 0);
    o.high = float4(0, 0, 0, 1);
    [branch] if (boxOpen(uv)) {
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
        o.low = float4(low, 1); // computed: the resolve may use this box
        o.high.rgb = high;
    }
    return o;
}
