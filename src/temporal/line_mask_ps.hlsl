// Line mask of the line-filtered temporal resolve (resolve.hlsl X3M_LINE_FILTER;
// docs/architecture/taa-lattice-crawl.md section 9). Two draws of this program
// into TemporalPass's two owned A8R8G8B8 targets, because the mask does not fit
// the resolve variants' ps_3_0 slot budget (inline it cost 73 slots):
//   c7.z = 0  s1 = the current R32F depth. Output 1 where the pixel is
//             LINE-LIKE: valid depth d whose two opposite neighbours at distance
//             1 along the horizontal, the vertical or one diagonal are both
//             background, i.e. the -1 sentinel or a valid depth q farther by the
//             margin, (1 - q) * 1.1 < 1 - d (1 - depth falls as 1 / distance).
//             With c7.w > 1.5 (line width 2) a side also counts as background
//             when the neighbour at distance 2 is: geometry up to 2 px wide.
//             A silhouette has geometry on one side and is not line-like (a
//             convex corner is, along the diagonal across it).
//   c7.z = 1  s1 = the first target. Output the 3x3 maximum: the resolve
//             filters the line pixels and the pixels the line's energy spills to.
// s1 is point/clamp, single level (TemporalPass::normalize); c4.xy = 1 / size.
sampler2D source : register(s1);
float4 sizeJitter : register(c4);
float4 options : register(c7);
static const float lineMargin = 1.1;
float fetch(float2 uv) { return tex2Dlod(source, float4(uv, 0, 0)).r; }
bool validDepth(float v) { return v >= 0 && v <= 1; }
// d is valid; q is compared only when valid, so no NaN reaches the <.
bool lineBackground(float q, float d) { return (q <= -0.5 && q >= -1e30) || (validDepth(q) && (1 - q) * lineMargin < 1 - d); }

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float result = 0;
    if (options.z > 0.5) {
        [loop] for (int ny = -1; ny <= 1; ++ny) {
            [loop] for (int nx = -1; nx <= 1; ++nx) result = max(result, fetch(uv + float2(nx, ny) * sizeJitter.xy));
        }
    } else {
        float depth = fetch(uv);
        if (validDepth(depth)) {
            [loop] for (int k = 0; k < 4; ++k) {
                float2 along = (k == 0 ? float2(1, 0) : (k == 1 ? float2(0, 1) : (k == 2 ? float2(1, 1) : float2(1, -1)))) * sizeJitter.xy;
                bool before = lineBackground(fetch(uv - along), depth), after = lineBackground(fetch(uv + along), depth);
                [branch] if (options.w > 1.5) {
                    before = before || lineBackground(fetch(uv - 2 * along), depth);
                    after = after || lineBackground(fetch(uv + 2 * along), depth);
                }
                if (before && after) result = 1;
            }
        }
    }
    return float4(result, result, result, result);
}
