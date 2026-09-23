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
sampler2D currentColor : register(s0);
sampler2D lineMask : register(s8);
float4 sizeJitter : register(c4);
float4 rejection : register(c6);
float4 luminance : register(c22);
static const float3 lumaWeights = float3(0.2126, 0.7152, 0.0722);
float lumaFloored(float3 c) { return max(dot(c, lumaWeights), 0); }
bool finiteColor(float3 v) { return all(v == v) && all(abs(v) <= rejection.z); }
float3 weigh(float3 c) { return c * (luminance.x > 0 ? 1 / (1 + luminance.x * lumaFloored(c)) : 1); }
struct BoxOutput { float4 low : COLOR0; float4 high : COLOR1; };
BoxOutput main(float2 uv : TEXCOORD0) {
    BoxOutput o;
    // The pixel's own mask first (a sky pixel of the stabiliser opens there), then outward; the columns pass reads a row
    // texel at uv.y + ny * sizeJitter.y, ny = -3..3, so the readers of this texel are the pixels at uv.y - ny * sizeJitter.y.
    float4 own = tex2Dlod(lineMask, float4(uv, 0, 0));
    float needed = own.b > own.a ? 1 : 0;
    [loop] for (int ny = 1; ny <= 3 && needed < 0.5; ++ny) {
        float4 above = tex2Dlod(lineMask, float4(uv.x, uv.y - ny * sizeJitter.y, 0, 0));
        float4 below = tex2Dlod(lineMask, float4(uv.x, uv.y + ny * sizeJitter.y, 0, 0));
        if (above.b > above.a || below.b > below.a) needed = 1;
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
