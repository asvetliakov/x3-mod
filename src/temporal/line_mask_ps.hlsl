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
// X3M_CAMERA_GATE (line_mask_camera_ps.hlsl; taa-lattice-crawl.md section 32.1): the
// camera-relative gate mode of the thin region, bound only in that mode and only
// with the thin region on and the line filter off (c7.w = 0). The gates travel
// as OPENNESS, saturate(1 - (speed - c6.z) * c6.w), not as closure: a non-finite
// correspondence then reads 0 = closed whether saturate returns 0 for a NaN or
// the NaN reaches the UNORM target (written as 0), with no comparison a compiler
// or backend may fold. The tests draw (c7.z = 0) writes r = the openness of the
// screen speed (the plain program's gate) and a = the larger of it and the
// openness of the camera-relative speed, i.e. the gate on min(screen speed,
// camera-relative speed); the camera-relative speed is the routed
// correspondence measured against the camera path c0..c3 at this pixel's depth,
// 0 where the pixel follows the camera path itself, and not measured (open) on a
// routed pixel on the depth sentinel, section 32.5 (c0..c3 are validated finite
// by the caller). The separable draws take the 17x17 MINIMUM of both (r carries
// no line mask) and the composition writes b = the camera-gated strength and a =
// the screen-gated strength (a <= b), so the resolve knows where the camera term
// alone keeps the region open. r = farw * c5.z there.
// c8 (camera program only; section 32.3) makes that camera path depth- and
// translation-aware: c0..c3 is the rotation-only far-plane matrix (zero z
// column), exact for the sentinel; on a valid depth the path adds c8.xyz * (d -
// c8.w), the camera-relative translation between the two views over the pixel's
// view z = m32 / (d - m22). c8.xyz = 0 (no translation, or no depth law) is the
// far-plane path bit for bit. With c9.w = 1 the caller has bound its four-channel
// current depth at s5 and a pixel whose .b (clip w = view z, current_depth_ps.hlsl)
// is positive takes c9.xyz / .b instead: no m22 / m32, which the engine keeps as
// per-submission scratch. With c9.w = 1 the c8 law is not used at all: a valid
// depth whose .b is not positive (the producer writes both in one draw, so none
// is expected) stays on the far-plane path, so a wrong m22 / m32 latch cannot
// close, through the 17x17 minimum, a window the lane opens. c9.w = 0: c8 alone.
// c6.x = S > 0 (camera program only; docs/architecture/temporal-integration.md, "sentinel stabiliser"): the composition
// reads this pixel's own depth at s6 and its motion at s4, and an UNROUTED SENTINEL pixel (depth sentinel, motion alpha
// exactly -1, per-pixel motion on) gets b = max(b, S * the 17x17 minimum of the camera openness); a keeps the screen
// strength, so the whole added strength takes the box-clipped history. Not dilated; a routed sentinel pixel (alpha 1,
// section 32.5 glass) is outside the class. S = 0 skips the two fetches and is the previous composition bit for bit.
// X3M_TAA_THIN_REGION_EMISSIVE (docs/architecture/thin-glow-lines.md 8.3 R3; taa-lattice-crawl.md section 32.7): c10.x = E > 0
// adds an EMISSIVE VOTE to b in the tests draw (c7.z = 0), reading this frame's scene at s0. E = 0 (the default) does not
// read s0 or c10 at all and the mask is what it was bit for bit. E is in the units of the bound scene: the HDR route binds the
// FP16 scene, the 8-bit route the FP16 copy of the display-referred target, where nothing exceeds 1 and E >= 1 never fires.
sampler2D scene : register(s0);
sampler2D source : register(s1);
sampler2D motionOverride : register(s4);
float4 reprojection0 : register(c0);
float4 reprojection1 : register(c1);
float4 reprojection2 : register(c2);
float4 reprojection3 : register(c3);
float4 sizeJitter : register(c4);
float4 farGate : register(c5);
float4 thinGate : register(c6); // sentinel-stabiliser S (camera program; else unused), on, speed LO, 1 / (HI - LO)
float4 options : register(c7);
float4 emissive : register(c10); // x = E, the emissive vote's luma threshold in scene units; 0 = off (no tap, no vote)
#ifdef X3M_CAMERA_GATE
float4 depthParallax : register(c8); // camera_depth_parallax(): (DX, DY, DW) / m32, m22; xyz = 0 is the far-plane path
float4 laneParallax : register(c9);  // camera_lane_parallax(): (DX, DY, DW), 1 where s5 carries the view z; w = 0: c8 alone
sampler2D laneDepth : register(s5);  // the caller's four-channel current depth (.b = clip w = view z), point / clamp
sampler2D ownDepth : register(s6);   // composition with c6.x > 0 only: the current depth (.r), point / clamp
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
#ifndef X3M_CAMERA_GATE
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

#else
// Openness of this pixel's own gates (no dilation: the 17x17 minimum of the later draws covers the neighbours): x = the camera
// gate, y = the screen-speed gate. A routed pixel whose depth is neither valid nor the sentinel has no camera path and keeps
// its screen speed. A routed pixel on the SENTINEL (blended glass routes motion but writes no depth; s1.r is the sentinel there
// with either depth source, R32F or the lane) has no distance and casts no vote in the camera gate (section 32.5): against the
// far plane its relative speed is the whole translation parallax, which the 17x17 minimum would spread over every strut.
// A VALID depth whose lane .b is not positive still votes from the far plane (fail closed on an inconsistent frame).
float gateOpenness(float speed) { return saturate(1 - (speed - thinGate.z) * thinGate.w); }
float2 gateOpen(float2 uv, float depth, float4 motion) {
    const bool routed = options.x > 0.5 && motion.w >= 1 && motion.w <= 1;
    const bool cameraPath = validDepth(depth) || sentinelDepth(depth);
    float2 cameraUV = uv;
    if (cameraPath) {
        float2 unjittered = uv - 0.5 * sizeJitter.xy - sizeJitter.zw;
        float4 clip = float4(unjittered.x * 2 - 1, 1 - unjittered.y * 2, validDepth(depth) ? depth : 1, 1);
        float3 previous = float3(dot(reprojection0, clip), dot(reprojection1, clip), dot(reprojection3, clip));
        // Section 32.3: the pixel's own depth and the camera translation. 1 / view z = (d - m22) / m32, so the previous clip
        // position over z is the far-plane image plus c8.xyz * (d - m22); the sentinel has no geometry and stays at infinity.
        // Preferred where the lane is bound (c9.w = 1): 1 / view z read directly from .b, no depth law at all.
        if (validDepth(depth)) {
            float viewZ = tex2Dlod(laneDepth, float4(uv, 0, 0)).b;
            // With the lane bound the law is never consulted: a valid depth whose .b is not positive stays on the far plane.
            previous += laneParallax.w > 0.5 ? laneParallax.xyz * (viewZ > 0 ? 1 / viewZ : 0) : depthParallax.xyz * (depth - depthParallax.w);
        }
        cameraUV = float2(previous.x, -previous.y) / max(previous.z, 1e-6) * 0.5 + 0.5 + 0.5 * sizeJitter.xy + sizeJitter.zw;
    }
    float2 previousUV = routed ? motion.xy + sizeJitter.zw : cameraUV;
    float screenSpeed = length((previousUV - uv) / sizeJitter.xy);
    float screen = gateOpenness(screenSpeed);
    // No vote on the sentinel, yet a non-finite routed correspondence must still read closed without a comparison: the screen
    // speed scaled by 1e-20 stays NaN / infinite when it is, and is below LO (open, no vote) for any finite speed under LO * 1e20
    // px/frame; a finite garbage speed beyond that grades to closed, which is the safe side.
    float relative = routed ? gateOpenness(validDepth(depth) ? length((previousUV - cameraUV) / sizeJitter.xy) : screenSpeed * 1e-20) : 1;
    return float2(cameraPath ? max(screen, relative) : screen, screen);
}
#endif

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
        // The k = 0 tap is the centre itself (min / max with itself is the identity on these UNORM8 values, and min / max
        // are exact and order-free), so the loop visits the 16 other taps only, each at the same uv + k * axis as before.
        [loop] for (int j = 0; j < 16; ++j) {
            int k = j < 8 ? j - 8 : j - 7;
            float4 tap = fetch(uv + k * axis);
#ifdef X3M_CAMERA_GATE
            result.ar = min(result.ar, tap.ar);
            if (abs(k) <= 5) result.b = max(result.b, tap.b);
#else
            result.a = max(result.a, tap.a);
            if (abs(k) <= 5) result.b = max(result.b, tap.b);
            if (abs(k) <= 1) result.r = max(result.r, tap.r);
#endif
        }
        if (compose) {
            float2 far = centre.gg * farGate.zw;
#ifdef X3M_CAMERA_GATE
            float fragmented = result.b * thinGate.y, cameraOpen = result.a;
            result.b = fragmented * result.a; result.a = fragmented * result.r;
            [branch] if (thinGate.x > 0) {
                float alpha = tex2Dlod(motionOverride, float4(uv, 0, 0)).w;
                if (sentinelDepth(tex2Dlod(ownDepth, float4(uv, 0, 0)).r) && options.x > 0.5 && alpha >= -1 && alpha <= -1)
                    result.b = max(result.b, thinGate.x * cameraOpen);
            }
            result.r = far.x; result.g = far.y;
#else
            result.r = max(result.r, far.x); result.g = far.y;
            result.b *= (1 - result.a) * thinGate.y; result.a = 0;
#endif
        }
    } else {
        float depth = fetch(uv).r;
        result.g = farWeight(depth);
        [branch] if (thinGate.y > 0.5) {
            // One motion sample for the whole draw: the speed gate and the emissive vote read the same texel.
            float4 motion = tex2Dlod(motionOverride, float4(uv, 0, 0));
#ifdef X3M_CAMERA_GATE
            result.ar = gateOpen(uv, depth, motion);
#else
            result.a = gateClosure(uv, depth, motion);
#endif
            // b only ever becomes 1: the first fragmented line ends the search and a fragmented pixel skips the emissive vote.
            [loop] for (int k = 0; k < 4; ++k) {
                float2 along = (k == 0 ? float2(1, 0) : (k == 1 ? float2(0, 1) : (k == 2 ? float2(1, 1) : float2(1, -1)))) * sizeJitter.xy;
                float changes = 0, previous = fetch(uv - 3 * along).r;
                [loop] for (int t = -2; t <= 3; ++t) { float next = fetch(uv + t * along).r; if (classChange(previous, next)) changes += 1; previous = next; }
                if (changes >= 2) { result.b = 1; break; }
            }
            [branch] if (emissive.x > 0 && result.b < 0.5) { if (emissiveVote(uv, depth, motion.w)) result.b = 1; }
        }
#ifndef X3M_CAMERA_GATE
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
#endif
    }
    return result;
}
