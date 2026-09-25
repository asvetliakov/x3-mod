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
// The camera-gate mode of the thin region has no mask draw since the mask fold (docs/architecture/taa-mask-fold.md): the
// A' resolve (resolve.hlsl X3M_REGION_HOLD) computes its tests itself. This program serves the screen-gate chain and the far
// stabiliser alone.
// X3M_TAA_THIN_REGION_EMISSIVE (docs/architecture/thin-glow-lines.md 8.3 R3; taa-lattice-crawl.md section 32.7): c10.x = E > 0
// adds an EMISSIVE VOTE to b in the tests draw (c7.z = 0), reading this frame's scene at s0. E = 0 (the default) does not
// read s0 or c10 at all and the mask is what it was bit for bit. E is in the units of the bound scene: the HDR route binds the
// FP16 scene, the 8-bit route the FP16 copy of the display-referred target, where nothing exceeds 1 and E >= 1 never fires.
// X3M_MASK_DEPTH_OUT (line_mask_depth_ps.hlsl; docs/architecture/taa-high-resolution.md S1):
// the tests draw (c7.z = 0) or the far-only draw (c7.z = 2) with s1 = the caller's two- or four-channel current depth
// itself instead of its R32F copy; COLOR1 = that centre texel, which the caller's R32F second target (the next depth
// history) stores as its .r, the value the copy draw wrote. Every tap reads .r, so the mask is the same bit for bit. Bound
// only for that one draw.
// X3M_THIN_VOTE (line_mask_depth_thin_ps.hlsl; X3M_TAA_THIN_VOTE, docs/architecture/
// taa-thin-geometry-alternatives.md section 3.2): with X3M_MASK_DEPTH_OUT only, s1 being the four-channel lane. The route's
// depth fragment writes .a = 1 - thin on an opaque routed row (thin = the draw's fraction of triangles 0.5..3 px tall; 1 =
// no vote), the fill leaves -1 and other writers 1. With the thin region on, the tests draw sets the flag on a valid depth
// whose .a is in [0, 1) and then skips the 7-tap line search and the emissive vote (the flag only ever becomes 1); every
// other pixel runs the search as the plain program does, so the mask differs only where a vote is cast.
// X3M_TAA_THIN_REGION_SOURCE=vote (taa-thin-geometry-alternatives.md section 3.2; TemporalPass ThinRegionSource::Vote):
// c10.y = 1, uploaded with c10.x on every mask draw and read by these twins only, skips the 7-tap line search on every
// pixel, so the flag is the vote alone (and the emissive vote where E > 0, which is its own opt-in). c10.y = 0 (every
// other run; the compiled ifc_ge on c10.y treats a NaN as "skip", but the pass only ever uploads 0 or 1) is the union above. "screen" is not a constant: the pass draws the plain program instead.
sampler2D scene : register(s0);
sampler2D source : register(s1);
sampler2D motionOverride : register(s4);
float4 reprojection0 : register(c0);
float4 reprojection1 : register(c1);
float4 reprojection2 : register(c2);
float4 reprojection3 : register(c3);
float4 sizeJitter : register(c4);
float4 farGate : register(c5);
float4 thinGate : register(c6); // x unused, on, speed LO, 1 / (HI - LO)
float4 options : register(c7);
float4 emissive : register(c10); // x = E, the emissive vote's luma threshold in scene units; 0 = off (no tap, no vote); y = 1: vote-only source (X3M_THIN_VOTE)
#ifdef X3M_MASK_DEPTH_OUT
static float4 centreTexel; // s1 at this pixel: the whole current-depth texel (main sets it first)
#endif
static const float lineMargin = 1.1;
static const float3 lumaWeights = float3(0.2126, 0.7152, 0.0722); // Rec.709, as resolve.hlsl and the box programs weigh luma
static const float emissiveFinite = 65000; // the resolve's own finite limit (rejection.z), as thin_box_*_ps.hlsl applies it
float4 fetch(float2 uv) { return tex2Dlod(source, float4(uv, 0, 0)); }
bool validDepth(float v) { return v >= 0 && v <= 1; }
bool sentinelDepth(float v) { return v <= -0.5 && v >= -1e30; }
// d is valid; q is compared only when valid, so no NaN reaches the <.
bool lineBackground(float q, float d) { return sentinelDepth(q) || (validDepth(q) && (1 - q) * lineMargin < 1 - d); }
bool classChange(float a, float b) { return (validDepth(a) && lineBackground(b, a)) || (validDepth(b) && lineBackground(a, b)); }
float farWeight(float depth) { return validDepth(depth) ? saturate((depth - farGate.x) * farGate.y) : 0; }
// Emissive vote of the thin region, tests draw only. A pixel qualifies when it is ROUTED with valid depth (motion alpha
// exactly 1, the routing the resolve itself reads, sampled once by the caller), its own scene luma L exceeds E, and the
// MINIMUM luma of its 3x3 is below L / 3: a local peak, i.e. a thin emissive strip on a hull, and not a uniformly lit panel,
// whose 3x3 minimum is its own luma. 9 taps of the scene through s0 and no further motion fetch. Unrouted sentinel pixels
// (lasers, engine glows, sky) are outside the class and keep the sentinel law. The vote lands in b and follows the whole
// existing chain: the 11x11 grow, the 17x17 speed gate, the camera gate and the 7x7 box clip.
// Non-finite taps, both ways, with no reliance on how a compiler folds max(NaN, 0): a tap is taken only when it compares
// finite (|L| <= 65000, the resolve's own limit, which a NaN fails in either direction and an infinity in one), and a tap
// that is not becomes 0 AT THE CENTRE (so the pixel's own luma cannot clear E) and the limit AS A NEIGHBOUR (so it cannot
// lower the 3x3 minimum and let the centre through). A pixel whose whole 3x3 is non-finite keeps lowest = centre = 0.
// Cost (docs/architecture/engine-frame-time.md, "TAA stage cost"): the centre tap is tested first. The vote needs centre > E, so
// a pixel whose own luma (0 when non-finite, the loop's rule) does not exceed E returns false before the eight neighbour taps;
// the loop below is unchanged and computes the same centre, so a pixel that passes votes exactly as before.
float sceneLuma(float2 uv) { return dot(tex2Dlod(scene, float4(uv, 0, 0)).rgb, lumaWeights); }
bool emissiveVote(float2 uv, float depth, float alpha) {
    if (!validDepth(depth) || !(options.x > 0.5)) return false;
    if (!(alpha >= 1 && alpha <= 1)) return false;
    float own = sceneLuma(uv);
    if (!((own == own && own <= emissiveFinite && own >= -emissiveFinite ? max(own, 0) : 0) > emissive.x)) return false;
    float centre = 0, lowest = emissiveFinite;
    [loop] for (int ny = -1; ny <= 1; ++ny) {
        [loop] for (int nx = -1; nx <= 1; ++nx) {
            float tap = sceneLuma(uv + float2(nx, ny) * sizeJitter.xy);
            bool finite = tap == tap && tap <= emissiveFinite && tap >= -emissiveFinite;
            lowest = min(lowest, finite ? max(tap, 0) : emissiveFinite);
            if (nx == 0 && ny == 0) centre = finite ? max(tap, 0) : 0;
        }
    }
    return centre > emissive.x && lowest * 3 < centre;
}
// Screen speed of this pixel's own correspondence, px/frame (no dilation: the 13x13 maximum of the later draws covers the neighbours).
float gateClosure(float2 uv, float depth, float4 motion) {
    float2 previousUV = uv;
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

#ifdef X3M_MASK_DEPTH_OUT
struct MaskDepthOut { float4 mask : COLOR0; float4 depth : COLOR1; };
MaskDepthOut main(float2 uv : TEXCOORD0)
{
    centreTexel = fetch(uv);
#else
float4 main(float2 uv : TEXCOORD0) : COLOR0
{
#endif
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
        // The k = 0 tap is the centre itself (min / max with itself is the identity on these UNORM8 values, and min / max
        // are exact and order-free), so the loops visit the 16 other taps only, each at the same uv + k * axis as before:
        // first the ten with |k| <= 5, which complete b, then the six with |k| = 6..8, which only the 17-tap a (and r) read.
        [loop] for (int j = 0; j < 10; ++j) {
            int k = j < 5 ? j - 5 : j - 4;
            float4 tap = fetch(uv + k * axis);
            result.a = max(result.a, tap.a);
            if (abs(k) <= 1) result.r = max(result.r, tap.r);
            result.b = max(result.b, tap.b);
        }
        // The composition multiplies the 17-tap values by fragmented = b * c6.y; where that is exactly 0 (b = 0 and c6.y
        // finite, or c6.y = 0) every product is 0 whatever the outer taps hold (they are finite UNORM8), so they are skipped.
        // The x draw (no composition) always takes them.
        float fragmented = result.b * thinGate.y;
        [branch] if (!compose || !(fragmented == 0)) {
            [loop] for (int j = 0; j < 6; ++j) {
                int k = j < 3 ? j - 8 : j + 3;
                float4 tap = fetch(uv + k * axis);
                result.a = max(result.a, tap.a);
            }
        }
        if (compose) {
            float2 far = centre.gg * farGate.zw;
            result.r = max(result.r, far.x); result.g = far.y;
            result.b *= (1 - result.a) * thinGate.y; result.a = 0;
        }
    } else
    {
        float depth = fetch(uv).r;
        result.g = farWeight(depth);
        [branch] if (thinGate.y > 0.5) {
            // One motion sample for the whole draw: the speed gate and the emissive vote read the same texel.
            float4 motion = tex2Dlod(motionOverride, float4(uv, 0, 0));
            result.a = gateClosure(uv, depth, motion);
            // b only ever becomes 1: the first fragmented line ends the search and a fragmented pixel skips the emissive vote.
#ifdef X3M_THIN_VOTE
            // The draw-time vote (lane .a = 1 - thin in [0, 1) on a routed pixel with a valid depth): flagged without the search.
            if (validDepth(depth) && centreTexel.a >= 0 && centreTexel.a < 1) result.b = 1;
            // Vote-only source (c10.y = 1): no search, an unvoted pixel stays unflagged.
            else [branch] if (!(emissive.y > 0.5))
#endif
            [loop] for (int k = 0; k < 4; ++k) {
                float2 along = (k == 0 ? float2(1, 0) : (k == 1 ? float2(0, 1) : (k == 2 ? float2(1, 1) : float2(1, -1)))) * sizeJitter.xy;
                float changes = 0, previous = fetch(uv - 3 * along).r;
                [loop] for (int t = -2; t <= 3; ++t) { float next = fetch(uv + t * along).r; if (classChange(previous, next)) changes += 1; previous = next; }
                if (changes >= 2) { result.b = 1; break; }
            }
            [branch] if (emissive.x > 0 && result.b < 0.5) { if (emissiveVote(uv, depth, motion.w)) result.b = 1; }
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
#ifdef X3M_MASK_DEPTH_OUT
    MaskDepthOut output;
    output.mask = result;
    output.depth = centreTexel;
    return output;
#else
    return result;
#endif
}
