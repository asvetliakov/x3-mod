// Original D3D9 temporal resolve; see README.md for sampler and coordinate contracts.
sampler2D currentColor : register(s0);
sampler2D currentDepth : register(s1);
sampler2D previousColor : register(s2);
sampler2D previousDepth : register(s3);
sampler2D motionOverride : register(s4);
sampler2D currentReactive : register(s5);
sampler2D previousReactive : register(s6);
float4 reprojection0 : register(c0);
float4 reprojection1 : register(c1);
float4 reprojection2 : register(c2);
float4 reprojection3 : register(c3);
float4 sizeJitter : register(c4); // 1/W, 1/H, current jitter UV xy
// c5.xy carries the previous raster jitter (UV) for ABI compatibility only; the
// resolve never reads it. History is the accumulated output on the UNJITTERED
// pixel grid and output pixel p represents unjittered position p. The jittered
// raster sample at p shows content at p - current jitter (the sub-pixel offset
// is the supersampling); its previous unjittered position q is what the motion
// producer/camera path yields, and history is sampled at q + current jitter
// ("pixel center minus velocity"). For a static scene that is p exactly (f = 0),
// so the output is stable across phases. Adding the previous jitter instead
// moved the taps by the jitter difference every frame: oscillation plus blur.
// c5.z is the history weight: the fraction of the accepted history kept per
// frame. 0.9 (the route default) converges an edge to its jitter-averaged
// coverage in about 20 frames with a per-frame ripple of (1-w) times the
// contrast at a toggling sample; 0.85-0.95 is the sensible range (lower is
// faster and noisier, higher smears clamp-bounded ghosting for longer).
float4 history : register(c5); // (previous jitter UV xy, unused), weight, valid
// Depth tolerance is max(absolute, relative * |expected|) in device-depth units.
float4 rejection : register(c6); // absolute device-depth tolerance, relative tolerance, HDR limit, minimum W
// options.w selects the depth-sentinel policy: 0 off; 1 a current pixel whose
// depth is the -1 sentinel (no routed opaque draw wrote it) is current-only;
// 2 such a pixel is reprojected through the camera path with depth = far (1),
// for a route that supplies a valid clip_to_previous for the background.
float4 options : register(c7); // motion enabled, reactive enabled, mask snapshot mode, depth-sentinel policy
// c22.x is k of the reversible luminance weighting (c8..c21 are the AgX
// block of the HDR write-back, left clear). Every colour that enters the
// temporal statistics -- the current pixel, its 3x3 neighbourhood and every
// history tap -- is scaled by w = 1 / (1 + k * luma) first, so the min/max
// box, mean +/- sigma clip and blend run in a bounded domain where a bright
// sub-pixel feature (a firefly) carries a fraction of its radiance; the blend
// result is mapped back by 1 / (1 - k * luma'), which is exact for a
// stationary pixel (history equals current). The stored history stays in
// engine radiance: nothing is rescaled when k changes between frames. k = 0
// selects the identity through a compare, not through 1/(1+0): the current
// and history colours are multiplied by the constant 1.0 exactly, so the
// 8-bit route's output is bit-identical to the unweighted resolve. Values
// that reach the weighting are already finite and <= rejection.z (65000) in
// magnitude, but not necessarily positive (an FP16 scene keeps the negative
// result of a subtractive blend): the luma is floored at 0 in both
// directions, so 1 + k * luma >= 1 (a pixel of non-positive luma is the
// identity, its inverse too) and the weighted domain is finite with every
// weight in (0, 1]. The inverse of a convex combination of weighted colours
// (luma' < 1/k strictly) is exact; the per-channel clamp can move the
// history to a box corner whose luma exceeds every neighbour's, so the
// inverse denominator is floored at 1/65504 rather than proven positive:
// the output is then finite but large (a one-pixel flash that the next
// frame's weighting bounds again), never Inf or NaN.
float4 luminance : register(c22); // k, unused, unused, unused
static const float3 lumaWeights = float3(0.2126, 0.7152, 0.0722);
static const float unweighFloor = 1.0 / 65504.0;
float lumaFloored(float3 c) { return max(dot(c, lumaWeights), 0); }

// Variance-clip width in standard deviations of the current 3x3 neighborhood.
// 1.25 keeps a history that is one full neighborhood extreme away from an
// 8:1 mixed neighborhood (the min/max box still bounds it) and dims a
// nearly-aligned one-pixel line by at most about 5%; 1.0 would dim thin lines
// visibly, larger values readmit more clamp-bounded ghosting.
static const float clipGamma = 1.25;
// Sub-texel offsets below this (float rounding of the jitter round trip, about
// 1e-6 texel) snap to the texel grid so a static scene reads exactly one texel.
static const float snapEpsilon = 1e-4;

// Every input is a single-level texture with point sampling; an explicit LOD
// keeps the fetches free of gradients so they may sit under real branches.
float4 fetch(sampler2D s, float2 uv) { return tex2Dlod(s, float4(uv, 0, 0)); }
bool finiteColor(float3 v) { return all(v == v) && all(abs(v) <= rejection.z); }
bool validDepth(float v) { return v == v && v >= 0 && v <= 1; }
float3 cleanColor(float3 v) { return finiteColor(v) ? v : float3(0, 0, 0); }
// Exactly zero is safe. Positive, negative and nonfinite mask values reject.
// Only >= and <= comparisons are NaN-safe (false) after compilation on the
// verified backend: v == v folds to true, and < or > compile to negated
// forms a NaN passes. Every test whose false branch must catch a NaN is
// written with >= and <= for that reason.
bool maskSafe(float v) { return v >= 0 && v <= 0; }
float3 weigh(float3 c) { return c * (luminance.x > 0 ? 1 / (1 + luminance.x * lumaFloored(c)) : 1); }
float3 unweigh(float3 c) { return c * (luminance.x > 0 ? 1 / max(1 - luminance.x * lumaFloored(c), unweighFloor) : 1); }
float snapFraction(inout float base, float f) {
    if (f > 1 - snapEpsilon) { base += 1; return 0; }
    return f < snapEpsilon ? 0 : f;
}
// One Catmull-Rom history tap. Nonfinite taps contribute no energy and the
// remaining weights renormalize; a nonzero-weight tap with reactive previous
// coverage (mask policy) rejects the whole lookup.
void historyTap(float2 uv, float weight, inout float3 sum, inout float total, inout bool reactive) {
    if (weight != 0) {
        if (options.y > 0.5 && !maskSafe(fetch(previousReactive, uv).r)) reactive = true;
        float3 color = fetch(previousColor, uv).rgb;
        if (finiteColor(color)) {
            sum += weigh(color) * weight;
            total += weight;
        }
    }
}
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    // Explicit GPU snapshot mode, used by TemporalPass only after validating s5.
    // Canonicalize coverage into owned R32F history; never infer it from alpha.
    if (options.z > 0.5)
        return float4(maskSafe(fetch(currentReactive, uv).r) ? 0 : 1, 0, 0, 1);
    float4 current = fetch(currentColor, uv);
    float3 raw = current.rgb;
    float3 color = cleanColor(raw);
    // The output alpha is the current alpha (the 8-bit main target keeps
    // whatever the game wrote there); history alpha is never blended.
    float alpha = current.a == current.a ? current.a : 1;
    float depth = fetch(currentDepth, uv).r;
    // Depth-sentinel policy (options.w): a negative current depth marks a pixel
    // no routed opaque draw wrote (background, particles, unknown programs).
    // Policy 1 keeps it current-only: with the identity matrix the route
    // uploaded before the camera read, camera reprojection would accumulate a
    // moving background in place. Policy 2 reprojects it through the camera
    // path at the far plane, for a route that supplies a real far-plane
    // clip_to_previous (the route's camera reprojection); such a pixel keeps
    // the camera path whether its motion alpha is 0 or the route's fill
    // sentinel -1 (see the motion block). Sentinel HISTORY taps are never a
    // rejection reason: they are the background behind a silhouette (see the
    // disocclusion test below).
    // (<= -0.5 rather than < 0: a NaN depth must fail this test and reach
    // validDepth below, never become a far-plane pixel under policy 2.)
    bool farPlane = false;
    if (options.w > 0.5 && depth <= -0.5) {
        if (options.w < 1.5) return float4(color, alpha);
        depth = 1;
        farPlane = true;
    }
    if (history.w < 0.5 || history.z <= 0 || !finiteColor(raw) || !validDepth(depth))
        return float4(color, alpha);
    if (options.y > 0.5 && !maskSafe(fetch(currentReactive, uv).r))
        return float4(color, alpha);

    // Closest-depth dilation: the correspondence (camera reprojection or the
    // producer's motion) is taken from the closest valid pixel of the 3x3
    // around this one, the center winning ties, and its VELOCITY is applied
    // here. A pixel on the far side of a silhouette then follows the
    // foreground object it borders instead of the background's motion, and
    // the closest depth is also the disocclusion threshold below. The full
    // 3x3 rather than the cross is required: on the jitter phases that
    // uncover a silhouette's corner pixel on both axes its only foreground
    // neighbor is the diagonal one, and with the cross the corner rejected
    // its own foreground history and lost half of its coverage (fixture
    // "silhouette", corner pixels).
    float2 dilate = 0;
    float nearest = depth;
    [unroll] for (int ky = -1; ky <= 1; ++ky) {
        [unroll] for (int kx = -1; kx <= 1; ++kx) {
            if (kx != 0 || ky != 0) {
                float neighbor = fetch(currentDepth, uv + float2(kx, ky) * sizeJitter.xy).r;
                if (validDepth(neighbor) && neighbor < nearest) { nearest = neighbor; dilate = float2(kx, ky); }
            }
        }
    }
    float2 dilatedUV = uv + dilate * sizeJitter.xy;

    // Texture centers use (pixel + .5)/size, but the raw D3D9 viewport maps
    // unadjusted projection NDC zero to raster pixel size/2. Remove the texture
    // half-texel before inverting the camera; restore it after prior projection.
    // The current color/depth/motion are the jittered rasterization read at
    // this pixel. The camera reconstruction removes the current jitter to get
    // the content's unjittered position, reprojects it, and restores the same
    // jitter below so a static camera lands on this pixel's own texel center.
    float2 unjittered = dilatedUV - 0.5 * sizeJitter.xy - sizeJitter.zw;
    float4 currentClip = float4(unjittered.x * 2 - 1, 1 - unjittered.y * 2, nearest, 1);
    float4 previousClip = float4(dot(reprojection0, currentClip), dot(reprojection1, currentClip),
                                 dot(reprojection2, currentClip), dot(reprojection3, currentClip));
    float2 previousUV;
    float expectedDepth;
    bool valid = all(previousClip == previousClip) && all(abs(previousClip) <= 1e20)
                 && previousClip.w > rejection.w;
    previousUV = float2(previousClip.x, -previousClip.y) / max(previousClip.w, rejection.w) * 0.5 + 0.5;
    // Previous unjittered texture-center UV of the content plus the CURRENT
    // jitter (never the previous one): history lives on the unjittered grid.
    previousUV += 0.5 * sizeJitter.xy + sizeJitter.zw;
    expectedDepth = previousClip.z / max(previousClip.w, rejection.w);
    if (options.x > 0.5) {
        float4 motion = fetch(motionOverride, dilatedUV);
        if (motion.w == 1) {
            // RG is the producer's previous unjittered texture-center UV of the
            // content at this jittered sample; add the current jitter only.
            previousUV = motion.xy + sizeJitter.zw;
            expectedDepth = motion.z;
            valid = all(motion == motion) && all(abs(motion) <= 1e20);
        } else if (motion.w != 0) {
            // The route fills every unrouted pixel of RT1 with alpha -1 and
            // RT2 with the depth sentinel in one draw, so a far-plane pixel
            // that is its own correspondence (no closer neighbor won the
            // dilation) carries exactly that pair: under policy 2 it keeps
            // the camera path. A dilated neighbor with alpha -1 is a routed
            // draw without history (mode 0) and still rejects; any other
            // alpha rejects as before. (>= -1 && <= -1: exactly -1, NaN-safe.)
            if (!(farPlane && all(dilate == 0) && motion.w >= -1 && motion.w <= -1)) valid = false;
        }
    }
    // The dilated pixel's velocity, applied to this pixel.
    previousUV -= dilate * sizeJitter.xy;
    if (!valid || !validDepth(expectedDepth) || any(previousUV < 0) || any(previousUV > 1))
        return float4(color, alpha);

    float2 position = previousUV / sizeJitter.xy - 0.5;
    float2 base = floor(position);
    float2 f = position - base;
    f.x = snapFraction(base.x, f.x);
    f.y = snapFraction(base.y, f.y);
    float2 tap = (base + 0.5) * sizeJitter.xy;

    // Disocclusion test, the only use of depth for rejection. Every
    // contributing tap of the bilinear footprint must be proven either BEHIND
    // (or at) the expected previous depth of the (dilated) surface or the -1
    // sentinel; equivalently, the footprint's closest valid depth must not be
    // in front. History clearly in front was an occluder that has moved away,
    // so the lookup is rejected. History behind the surface or the sentinel
    // is the background this surface's silhouette moved over as the jitter
    // flipped its coverage; it is accepted and bounded by the neighborhood
    // clip below, which is what lets edges and thin features accumulate
    // their supersampled coverage instead of staying current-only. Because
    // the threshold is the closest depth of the cross, a background pixel
    // beside a silhouette accepts the foreground history it held on covered
    // phases. Nonfinite or above-range history depth is corrupt input, not
    // the sentinel: the proof is accumulated from >= and <= comparisons only,
    // which a NaN fails (the compiler folds v == v to true and emits < and >
    // as negated forms that a NaN passes), so it fails closed.
    float tolerance = max(rejection.x, rejection.y * abs(expectedDepth));
    float considered = 0, proven = 0;
    [unroll] for (int ty = 0; ty < 2; ++ty) {
        [unroll] for (int tx = 0; tx < 2; ++tx) {
            float weight = (tx ? f.x : 1 - f.x) * (ty ? f.y : 1 - f.y);
            // Taps of negligible weight cannot reject: they carry no visible energy.
            if (weight > 0.01) {
                considered += weight;
                float previous = fetch(previousDepth, tap + float2(tx, ty) * sizeJitter.xy).r;
                if (validDepth(previous) && previous >= expectedDepth - tolerance) proven += weight;
                if (previous <= -0.5 && previous >= -1e30) proven += weight;
            }
        }
    }
    if (proven < considered - 0.001) return float4(color, alpha);

    // History color: Catmull-Rom over the 4x4 texel neighborhood (16 point
    // taps; the samplers are point-filtered by contract, so the 9-tap form that
    // relies on hardware bilinear filtering is unavailable). Its negative lobes
    // keep detail that repeated bilinear resampling would blur away at
    // fractional velocities; the neighborhood clip bounds the overshoot. A
    // lookup on the texel grid (static content) reads that texel only.
    float3 accumulated = 0;
    float total = 0;
    bool reactive = false;
    [branch] if (all(f == 0)) {
        historyTap(tap, 1, accumulated, total, reactive);
    } else {
        float2 f2 = f * f, f3 = f2 * f;
        float2 w0 = -0.5 * f + f2 - 0.5 * f3;
        float2 w1 = 1 - 2.5 * f2 + 1.5 * f3;
        float2 w2 = 0.5 * f + 2 * f2 - 1.5 * f3;
        float2 w3 = -0.5 * f2 + 0.5 * f3;
        float4 wx = float4(w0.x, w1.x, w2.x, w3.x), wy = float4(w0.y, w1.y, w2.y, w3.y);
        [unroll] for (int j = 0; j < 4; ++j) {
            [unroll] for (int i = 0; i < 4; ++i)
                historyTap(tap + float2(i - 1, j - 1) * sizeJitter.xy, wx[i] * wy[j], accumulated, total, reactive);
        }
    }
    if (reactive || total < 0.5) return float4(color, alpha);
    float3 old = accumulated / total;
    // From here on every colour is in the weighted domain (identity at k = 0);
    // the early returns above hand the unweighted current colour through.
    float3 weighted = weigh(color);

    // Neighborhood clip: the history is clamped per channel to mean +/- gamma
    // sigma of the finite current 3x3, intersected with the 3x3 min/max box as
    // the fallback bound. A silhouette or thin-line pixel keeps accumulating
    // its coverage because its neighborhood contains both sides; a stale
    // history that no longer matches the neighborhood is pulled into it.
    // Invalid neighboring values cannot poison the statistics.
    float3 low = weighted, high = weighted, mean = 0, square = 0;
    float count = 0;
    [unroll] for (int ny = -1; ny <= 1; ++ny) {
        [unroll] for (int nx = -1; nx <= 1; ++nx) {
            float3 neighbor = fetch(currentColor, uv + float2(nx, ny) * sizeJitter.xy).rgb;
            if (finiteColor(neighbor)) {
                neighbor = weigh(neighbor);
                low = min(low, neighbor); high = max(high, neighbor);
                mean += neighbor; square += neighbor * neighbor; count += 1;
            }
        }
    }
    mean /= count;
    float3 sigma = sqrt(max(square / count - mean * mean, 0));
    low = max(low, mean - clipGamma * sigma);
    high = min(high, mean + clipGamma * sigma);
    old = clamp(old, low, high);
    return float4(unweigh(lerp(weighted, old, history.z)), alpha);
}
