// Stabiliser mask of the temporal resolve: the line mask of the line filter
// (resolve.hlsl X3M_LINE_FILTER; docs/architecture/taa-lattice-crawl.md section
// 9) and the far gate of the far stabiliser (X3M_FAR_STABILIZE; docs/
// architecture/taa-distant-line-fade.md section 9). Drawn by TemporalPass into
// its two owned A8R8G8B8 targets just before the resolve, which reads the second
// at s8; the masks do not fit the resolve variants' ps_3_0 slot budget (the
// line mask alone cost 73 slots inline).
//   c7.z = 0  s1 = the current R32F depth. r = 1 where the pixel is LINE-LIKE
//             (c7.w >= 1; 0 with c7.w = 0): valid depth d whose two opposite
//             neighbours at distance 1 along the horizontal, the vertical or one
//             diagonal are both background, i.e. the -1 sentinel or a valid
//             depth q farther by the margin, (1 - q) * 1.1 < 1 - d (1 - depth
//             falls as 1 / distance). With c7.w > 1.5 (line width 2) a side also
//             counts as background when the neighbour at distance 2 is: geometry
//             up to 2 px wide. A silhouette has geometry on one side and is not
//             line-like (a convex corner is, along the diagonal across it).
//             g = farw, the far gate of this pixel (below).
//   c7.z = 1  s1 = the first target. r = max(3x3 maximum of r, farw * c5.z): the
//             resolve filters the line pixels, the pixels the line's energy
//             spills to and the far pixels; g = farw * c5.w, the far history-
//             weight gate. farw itself is never dilated.
//   c7.z = 2  s1 = the current depth, no line filter: r = farw * c5.z, g = farw *
//             c5.w in one draw into the second target.
// farw = saturate((d - c5.x) * c5.y) on a valid depth, 0 on the sentinel or an
// invalid depth; the host derives c5.xy from the frame's projection so that the
// gate is a pixel footprint in world units, and uploads c5.y = 0 (farw = 0)
// without a valid projection. c5.zw are the component switches (0 or 1).
// s1 is point/clamp, single level (TemporalPass::normalize); c4.xy = 1 / size.
sampler2D source : register(s1);
float4 sizeJitter : register(c4);
float4 farGate : register(c5);
float4 options : register(c7);
static const float lineMargin = 1.1;
float4 fetch(float2 uv) { return tex2Dlod(source, float4(uv, 0, 0)); }
bool validDepth(float v) { return v >= 0 && v <= 1; }
// d is valid; q is compared only when valid, so no NaN reaches the <.
bool lineBackground(float q, float d) { return (q <= -0.5 && q >= -1e30) || (validDepth(q) && (1 - q) * lineMargin < 1 - d); }
float farWeight(float depth) { return validDepth(depth) ? saturate((depth - farGate.x) * farGate.y) : 0; }

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 result = 0;
    if (options.z > 1.5) {
        result = farWeight(fetch(uv).r) * farGate.zw;
    } else if (options.z > 0.5) {
        result = fetch(uv).gg * farGate.zw;
        [loop] for (int ny = -1; ny <= 1; ++ny) {
            [loop] for (int nx = -1; nx <= 1; ++nx) result.r = max(result.r, fetch(uv + float2(nx, ny) * sizeJitter.xy).r);
        }
    } else {
        float depth = fetch(uv).r;
        result.g = farWeight(depth);
        if (validDepth(depth) && options.w > 0.5) {
            [loop] for (int k = 0; k < 4; ++k) {
                float2 along = (k == 0 ? float2(1, 0) : (k == 1 ? float2(0, 1) : (k == 2 ? float2(1, 1) : float2(1, -1)))) * sizeJitter.xy;
                bool before = lineBackground(fetch(uv - along).r, depth), after = lineBackground(fetch(uv + along).r, depth);
                [branch] if (options.w > 1.5) {
                    before = before || lineBackground(fetch(uv - 2 * along).r, depth);
                    after = after || lineBackground(fetch(uv + 2 * along).r, depth);
                }
                if (before && after) result.r = 1;
            }
        }
    }
    return float4(result, 0, 1);
}
