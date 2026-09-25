// S4 (docs/architecture/taa-high-resolution.md S4, taa-plan-lifted-slot-cap.md step 2; X3M_TAA_BOX_RESOLUTION=half): the row
// draw of the camera gate's box at half resolution. The resolve reads the box targets (s9 / s10, W/2 x H/2) at its own
// full-resolution texel centre with POINT sampling, which lands on texel (x >> 1, y >> 1) exactly for an even W and H (u W/2 =
// x/2 + 1/4: never on a texel edge); TemporalPass runs this pair only for even sizes. Block (bx, by) is the pixels 2bx..2bx+1
// x 2by..2by+1, and the union of their 7x7 windows is the 8x8 window columns 2bx-3..2bx+4, rows 2by-3..2by+4. Those 8 rows are
// 4 row PAIRS when a pair starts at an odd row, so this draw's target is W/2 x (H/2 + 1) and texel (bx, g) covers the rows 2g-1
// and 2g (clamped to the frame) over the 8 columns 2bx-3..2bx+4. The columns draw (thin_box_columns_half_ps.hlsl) reads pairs
// by-1..by+2.
// COLOR0 = (minimum rgb, 0), COLOR1 = (maximum rgb, 1) of the pair's 16 taps, weighed and filtered exactly as the full-resolution
// box (c22.x = k; |v| <= c6.z; clamp addressing); a pair with no finite tap writes (c6.z, -c6.z), the neutral elements.
// Readers: pair g is read by the column texels by = g-2..g+1, whose pixels are rows 2g-4..2g+3 of columns 2bx, 2bx+1; a texel
// none of those 16 pixels opens is discarded and never computed. The gate (A' with the mask fold, taa-mask-fold.md section 4.3)
// is the full-resolution box's (thin_box_ps.hlsl X3M_REGION_HOLD_MASK): the previous frame's region hold at s7.
// Coordinates: TEXCOORD0 = ((bx + 1/2) / (W/2), (g + 1/2) / (H/2 + 1)); c4.xy = 1 / (W, H) of the FULL frame; the pixel column
// 2bx is at u - c4.x / 2 and the row 2g-1 at v (1 + 2 c4.y) - 1.5 c4.y.
sampler2D currentColor : register(s0);
sampler2D previousAge : register(s7);
float4 sizeJitter : register(c4);
float4 rejection : register(c6);
float4 luminance : register(c22);
static const float3 lumaWeights = float3(0.2126, 0.7152, 0.0722);
float lumaFloored(float3 c) { return max(dot(c, lumaWeights), 0); }
float4 fetch(sampler2D s, float2 uv) { return tex2Dlod(s, float4(uv, 0, 0)); }
bool finiteColor(float3 v) { return all(v == v) && all(abs(v) <= rejection.z); }
float3 weigh(float3 c) { return c * (luminance.x > 0 ? 1 / (1 + luminance.x * lumaFloored(c)) : 1); }
bool heldRegion(float2 uv) {
    float v = abs(fetch(previousAge, uv).r);
    float held = v <= 65 ? frac(v) * 65536 : 0;
    return held - 128 * floor(held * (1.0 / 128)) > 0;
}
bool boxOpen(float2 uv) { return heldRegion(uv); }
struct BoxOutput { float4 low : COLOR0; float4 high : COLOR1; };
BoxOutput main(float2 uv : TEXCOORD0) {
    BoxOutput o;
    const float x0 = uv.x - 0.5 * sizeJitter.x;                      // pixel column 2bx
    const float y0 = uv.y + (2 * uv.y - 1.5) * sizeJitter.y;         // pixel row 2g-1
    float needed = 0;
    [loop] for (int k = -3; k <= 4 && needed < 0.5; ++k) {
        const float y = y0 + k * sizeJitter.y;
        if (boxOpen(float2(x0, y)) || boxOpen(float2(x0 + sizeJitter.x, y))) needed = 1;
    }
    // Discarded (no write) and, under the branch, not computed: a discard alone need not skip the taps on SIMD hardware.
    clip(needed - 0.5);
    o.low = 0; o.high = 0;
    [branch] if (needed > 0.5) {
        float3 low = rejection.z, high = -rejection.z;
        [loop] for (int r = 0; r <= 1; ++r) {
            const float y = y0 + r * sizeJitter.y;
            [loop] for (int nx = -3; nx <= 4; ++nx) {
                float3 neighbor = fetch(currentColor, float2(x0 + nx * sizeJitter.x, y)).rgb;
                if (finiteColor(neighbor)) {
                    neighbor = weigh(neighbor);
                    low = min(low, neighbor); high = max(high, neighbor);
                }
            }
        }
        o.low = float4(low, 0);
        o.high = float4(high, 1);
    }
    return o;
}
