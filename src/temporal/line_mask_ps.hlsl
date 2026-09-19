// Stabiliser mask of the temporal resolve: the line mask of the line filter
// (resolve.hlsl X3M_LINE_FILTER; docs/architecture/taa-lattice-crawl.md section
// 9), the far gate of the far stabiliser (X3M_FAR_STABILIZE; docs/architecture/
// taa-distant-line-fade.md section 9) and the thin-region gate (taa-lattice-
// crawl.md section 13). Drawn by TemporalPass into its two owned A8R8G8B8
// targets just before the resolve, which reads the final one at s8; the masks do
// not fit the resolve variants' ps_3_0 slot budget.
//   c7.z = 0  s1 = the current R32F depth, s4 = the motion target. Per pixel:
//             r = 1 where LINE-LIKE (c7.w >= 1; 0 with c7.w = 0): valid depth d
//             whose two opposite neighbours at distance 1 along the horizontal,
//             the vertical or one diagonal are both background, i.e. the -1
//             sentinel or a valid depth q farther by the margin, (1 - q) * 1.1 <
//             1 - d (1 - depth falls as 1 / distance); with c7.w > 1.5 a side also
//             counts when the neighbour at distance 2 is background.
//             g = farw, the far gate of this pixel (below).
//             b = 1 where FRAGMENTED (c6.y > 0.5): along one of the four 7-tap
//             lines through the pixel the depth changes class (valid against
//             its background, same test) at least twice: a lattice, shards,
//             struts; a plain silhouette crosses each line once and is not.
//             a = the speed-gate closure t = saturate((speed - c6.z) * c6.w) of
//             this pixel's own reprojection, px/frame: the routed motion where
//             its alpha is 1 (c7.x), else the camera path c0..c3 at the pixel's
//             depth (far plane on the sentinel), as resolve.hlsl computes it.
//   c7.z = 1  thin region on: s1 = the first target, maxima along x into the
//             second; c7.z = 3: s1 = the second, maxima along y back into the
//             FIRST, composed: r = max(3x3 maximum of r, farw * c5.z); g = farw *
//             c5.w; b = (11x11 maximum of b) * (1 - 17x17 maximum of a): the
//             thin-region strength, closed by the FASTEST pixel within 8 px,
//             the reach of the region itself (a background pixel a moving edge
//             has just uncovered carries the background's speed, its neighbours
//             the edge's); a = 0.
//   c7.z = 4  line filter without the thin region: s1 = the first target, one
//             draw into the second: r = max(3x3 maximum of r, farw * c5.z), g =
//             farw * c5.w, b = a = 0.
// Taps outside the frame repeat the edge texel (clamp): a line leaving the frame
// sees no further class change, a border pixel is as fragmented as its inside.
//   c7.z = 2  s1 = the current depth, far stabiliser alone: r = farw * c5.z, g =
//             farw * c5.w, b = a = 0, one draw into the second target.
// farw = saturate((d - c5.x) * c5.y) on a valid depth, else 0; c5.zw are the far
// component scales. s1 / s4 are point/clamp, single level; c4 = 1 / size, jitter UV.
sampler2D source : register(s1);
sampler2D motionOverride : register(s4);
float4 reprojection0 : register(c0);
float4 reprojection1 : register(c1);
float4 reprojection2 : register(c2);
float4 reprojection3 : register(c3);
float4 sizeJitter : register(c4);
float4 farGate : register(c5);
float4 thinGate : register(c6); // unused, on, speed LO, 1 / (HI - LO)
float4 options : register(c7);
static const float lineMargin = 1.1;
float4 fetch(float2 uv) { return tex2Dlod(source, float4(uv, 0, 0)); }
bool validDepth(float v) { return v >= 0 && v <= 1; }
bool sentinelDepth(float v) { return v <= -0.5 && v >= -1e30; }
// d is valid; q is compared only when valid, so no NaN reaches the <.
bool lineBackground(float q, float d) { return sentinelDepth(q) || (validDepth(q) && (1 - q) * lineMargin < 1 - d); }
bool classChange(float a, float b) { return (validDepth(a) && lineBackground(b, a)) || (validDepth(b) && lineBackground(a, b)); }
float farWeight(float depth) { return validDepth(depth) ? saturate((depth - farGate.x) * farGate.y) : 0; }
// Screen speed of this pixel's own correspondence, px/frame (no dilation: the 13x13 maximum of the later draws covers the neighbours).
float gateClosure(float2 uv, float depth) {
    float2 previousUV = uv;
    float4 motion = tex2Dlod(motionOverride, float4(uv, 0, 0));
    if (options.x > 0.5 && motion.w >= 1 && motion.w <= 1) previousUV = motion.xy + sizeJitter.zw;
    else if (validDepth(depth) || sentinelDepth(depth)) {
        float2 unjittered = uv - 0.5 * sizeJitter.xy - sizeJitter.zw;
        float4 clip = float4(unjittered.x * 2 - 1, 1 - unjittered.y * 2, validDepth(depth) ? depth : 1, 1);
        float3 previous = float3(dot(reprojection0, clip), dot(reprojection1, clip), dot(reprojection3, clip));
        previousUV = float2(previous.x, -previous.y) / max(previous.z, 1e-6) * 0.5 + 0.5 + 0.5 * sizeJitter.xy + sizeJitter.zw;
    }
    float speed = length((previousUV - uv) / sizeJitter.xy);
    return speed == speed ? saturate((speed - thinGate.z) * thinGate.w) : 1;
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float4 result = 0;
    if (options.z > 1.5 && options.z < 2.5) {
        result.rg = farWeight(fetch(uv).r) * farGate.zw;
    } else if (options.z > 3.5) {
        [loop] for (int ny = -1; ny <= 1; ++ny) {
            [loop] for (int nx = -1; nx <= 1; ++nx) result.r = max(result.r, fetch(uv + float2(nx, ny) * sizeJitter.xy).r);
        }
        float2 far = fetch(uv).gg * farGate.zw;
        result.r = max(result.r, far.x); result.g = far.y;
    } else if (options.z > 0.5) {
        // Separable maxima: c7.z = 1 along x into the second target, c7.z = 3 along y back into the first, which also composes.
        bool compose = options.z > 2.5;
        float2 axis = compose ? float2(0, sizeJitter.y) : float2(sizeJitter.x, 0);
        float4 centre = fetch(uv);
        result = centre;
        [loop] for (int k = -8; k <= 8; ++k) {
            float4 tap = fetch(uv + k * axis);
            result.a = max(result.a, tap.a);
            if (abs(k) <= 5) result.b = max(result.b, tap.b);
            if (abs(k) <= 1) result.r = max(result.r, tap.r);
        }
        if (compose) {
            float2 far = centre.gg * farGate.zw;
            result.r = max(result.r, far.x); result.g = far.y;
            result.b *= (1 - result.a) * thinGate.y; result.a = 0;
        }
    } else {
        float depth = fetch(uv).r;
        result.g = farWeight(depth);
        [branch] if (thinGate.y > 0.5) {
            result.a = gateClosure(uv, depth);
            [loop] for (int k = 0; k < 4; ++k) {
                float2 along = (k == 0 ? float2(1, 0) : (k == 1 ? float2(0, 1) : (k == 2 ? float2(1, 1) : float2(1, -1)))) * sizeJitter.xy;
                float changes = 0, previous = fetch(uv - 3 * along).r;
                [loop] for (int t = -2; t <= 3; ++t) { float next = fetch(uv + t * along).r; if (classChange(previous, next)) changes += 1; previous = next; }
                if (changes >= 2) result.b = 1;
            }
        }
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
    return result;
}
