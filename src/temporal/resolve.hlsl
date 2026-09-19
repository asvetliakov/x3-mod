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
float4 options : register(c7); // motion enabled, reactive enabled, snapshot mode (resolve_snapshot.hlsl only; 0 here), depth-sentinel policy
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
// c22.y is A of the filtered current sample, read only by the variant compiled
// with X3M_CURRENT_FILTER (resolve_filter.hlsl; TemporalPass binds it when
// A > 0 and this program, whose bytecode the define leaves untouched,
// otherwise). The colour that enters the blend is then the normalised
// exp(-A d^2) average of the finite weighted 3x3 samples the clip already
// fetches, d in pixels from this pixel's centre to each neighbour's jittered
// sample position (neighbour offset minus the current jitter: the sample at p
// shows content at p - jitter). The clip box and the variance stay those of
// the unfiltered samples; the filtered colour is a convex combination of them,
// so it lies inside the min/max box and the inverse weighting stays exact.
// docs/verification/motion-output.md, "Run 139", mechanism 1a.
float4 luminance : register(c22); // k, current-filter A, alpha history (X3M_THIN_CLIP variants), line-filter A (X3M_LINE_FILTER variants)
// X3M_LINE_FILTER (resolve_line.hlsl, resolve_thin_line.hlsl, resolve_age_line.hlsl;
// docs/architecture/taa-lattice-crawl.md section 9): the same filtered current
// sample with A = c22.w, applied only where s8 (the line mask TemporalPass draws
// with line_mask_ps.hlsl just before this program: a line-like pixel in the
// current 3x3 depth) is set. Off the mask the blend is the unfiltered program's.
// X3M_FAR_STABILIZE (resolve_far.hlsl; docs/architecture/taa-distant-line-fade.md
// section 9): the age variant with the mask at s8, whose channels TemporalPass
// fills per frame: r = filter weight (the dilated line mask and / or farw, the
// far gate saturate((d - d0) * inv) of the centre depth, 0 on the sentinel), g =
// farw for the history weight (0 when that component is off). The current
// sample is lerp(point, filtered, r) and the history weight
// lerp(w, min(n / (n + 1), c24.y), g * (1 - saturate((speed - c24.z) * c24.w)));
// c24.yzw = W_FAR, 0.5, 1 / 1.5 here (one gate: the adaptive weight's own LO /
// HI gate is not compiled and the two options exclude each other). r = g = 0
// is the thin / plain blend exactly: x + 0 * (y - x) with finite y.
#ifdef X3M_FAR_STABILIZE
#define X3M_AGE_WEIGHT 1
#define X3M_LINE_FILTER 1
#endif
#ifdef X3M_LINE_FILTER
#define X3M_CURRENT_FILTER 1
#define X3M_FILTER_A luminance.w
sampler2D lineMask : register(s8);
#else
#define X3M_FILTER_A luminance.y
#endif
// Flicker suppression (docs/architecture/taa-flicker-suppression.md). The
// plain program and resolve_filter.hlsl compile none of it: their bytes are
// those of step 0. X3M_THIN_CLIP (resolve_thin*.hlsl): where the current 3x3
// depth mixes the sentinel with geometry, the history is pulled only part of
// the way to the clip box, old = lerp(clamp(old), old, S), S = c24.x faded
// out between 2 and 4 px/frame of screen speed; with c22.z > 0.5 the output
// alpha is the history alpha blended with the same weight and clamped to the
// current 3x3 alpha range (HDR route: bloom's authored-glow weight).
// X3M_AGE_WEIGHT (resolve_age*.hlsl, implies the thin clip): COLOR1 is the
// per-pixel accumulated-frame count n (R32F, s7 the previous one); every
// current-only return writes 1, the blend min(n + 1, 64), and the history
// weight is min(n / (n + 1), wmax), wmax = c24.y falling to c5.z between
// c24.z and c24.z + 1 / c24.w px/frame.
#ifdef X3M_AGE_WEIGHT
#define X3M_THIN_CLIP 1
sampler2D previousAge : register(s7);
#endif
#ifdef X3M_THIN_CLIP
float4 flicker : register(c24); // thin-clip S, age wmax, speed LO, 1 / (HI - LO)
#endif
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
#ifdef X3M_THIN_CLIP
#define HISTORY_SUM float4
#else
#define HISTORY_SUM float3
#endif
void historyTap(float2 uv, float weight, inout HISTORY_SUM sum, inout float total, inout bool reactive) {
    if (weight != 0) {
        if (options.y > 0.5 && !maskSafe(fetch(previousReactive, uv).r)) reactive = true;
#ifdef X3M_THIN_CLIP
        float4 color = fetch(previousColor, uv);
        if (finiteColor(color.rgb)) {
            // History alpha is this program's own output; a nonfinite one is
            // refused by the range test at the blend.
            sum += float4(weigh(color.rgb), color.a) * weight;
            total += weight;
        }
#else
        float3 color = fetch(previousColor, uv).rgb;
        if (finiteColor(color)) {
            sum += weigh(color) * weight;
            total += weight;
        }
#endif
    }
}
// Keep the bounded neighborhood loops rolled to fit the ps_3_0 static
// instruction budget. Traversal and arithmetic order stay unchanged. SM3 has
// no dynamic temporary-component indexing: select the exact Catmull-Rom weight
// for the loop index in [0,3] without changing its value.
float loopWeight(float4 weights, int index) {
    return index == 0 ? weights.x : (index == 1 ? weights.y : (index == 2 ? weights.z : weights.w));
}
#ifdef X3M_AGE_WEIGHT
struct ResolveOutput { float4 color : COLOR0; float4 age : COLOR1; };
ResolveOutput emit(float4 color, float age) { ResolveOutput o; o.color = color; o.age = float4(age, 0, 0, 1); return o; }
ResolveOutput main(float2 uv : TEXCOORD0) {
#else
#define emit(color, age) (color)
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
#endif
    // The mask-snapshot modes (options.z) live in resolve_snapshot.hlsl.
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
        if (options.w < 1.5) return emit(float4(color, alpha), 1);
        depth = 1;
        farPlane = true;
    }
    if (history.w < 0.5 || history.z <= 0 || !finiteColor(raw) || !validDepth(depth))
        return emit(float4(color, alpha), 1);
    if (options.y > 0.5 && !maskSafe(fetch(currentReactive, uv).r))
        return emit(float4(color, alpha), 1);

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
#ifdef X3M_THIN_CLIP
    // thin: the 3x3 (centre included) holds both a valid depth and the sentinel.
    bool sawSentinel = farPlane, sawValid = !farPlane;
#endif
    [loop] for (int ky = -1; ky <= 1; ++ky) {
        [loop] for (int kx = -1; kx <= 1; ++kx) {
            if (kx != 0 || ky != 0) {
                float neighbor = fetch(currentDepth, uv + float2(kx, ky) * sizeJitter.xy).r;
                if (validDepth(neighbor) && neighbor < nearest) { nearest = neighbor; dilate = float2(kx, ky); }
#ifdef X3M_THIN_CLIP
                if (validDepth(neighbor)) sawValid = true;
                if (neighbor <= -0.5 && neighbor >= -1e30) sawSentinel = true;
#endif
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
        return emit(float4(color, alpha), 1);

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
    [loop] for (int ty = 0; ty < 2; ++ty) {
        [loop] for (int tx = 0; tx < 2; ++tx) {
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
    if (proven < considered - 0.001) return emit(float4(color, alpha), 1);

    // History color: Catmull-Rom over the 4x4 texel neighborhood (16 point
    // taps; the samplers are point-filtered by contract, so the 9-tap form that
    // relies on hardware bilinear filtering is unavailable). Its negative lobes
    // keep detail that repeated bilinear resampling would blur away at
    // fractional velocities; the neighborhood clip bounds the overshoot. A
    // lookup on the texel grid (static content) reads that texel only.
    HISTORY_SUM accumulated = 0;
    float total = 0;
    bool reactive = false;
#ifdef X3M_THIN_CLIP
    // Slot budget: the variants drop the single-tap branch. On the texel grid
    // the weights are exactly (0, 1, 0, 0) and zero-weight taps are skipped,
    // so the loop reads that texel only and yields the same value.
    {
#else
    [branch] if (all(f == 0)) {
        historyTap(tap, 1, accumulated, total, reactive);
    } else {
#endif
        float2 f2 = f * f, f3 = f2 * f;
        float2 w0 = -0.5 * f + f2 - 0.5 * f3;
        float2 w1 = 1 - 2.5 * f2 + 1.5 * f3;
        float2 w2 = 0.5 * f + 2 * f2 - 1.5 * f3;
        float2 w3 = -0.5 * f2 + 0.5 * f3;
        float4 wx = float4(w0.x, w1.x, w2.x, w3.x), wy = float4(w0.y, w1.y, w2.y, w3.y);
        [loop] for (int j = 0; j < 4; ++j) {
            [loop] for (int i = 0; i < 4; ++i)
                historyTap(tap + float2(i - 1, j - 1) * sizeJitter.xy, loopWeight(wx, i) * loopWeight(wy, j), accumulated, total, reactive);
        }
    }
    if (reactive || total < 0.5) return emit(float4(color, alpha), 1);
    float3 old = accumulated.rgb / total;
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
#ifdef X3M_THIN_CLIP
    float lowAlpha = alpha, highAlpha = alpha;
#endif
#ifdef X3M_CURRENT_FILTER
    float2 jitterPixels = sizeJitter.zw / sizeJitter.xy;
    float3 filtered = 0;
    float filterTotal = 0;
#endif
    [loop] for (int ny = -1; ny <= 1; ++ny) {
        [loop] for (int nx = -1; nx <= 1; ++nx) {
#ifdef X3M_THIN_CLIP
            float4 neighborTexel = fetch(currentColor, uv + float2(nx, ny) * sizeJitter.xy);
            float3 neighbor = neighborTexel.rgb;
            lowAlpha = min(lowAlpha, neighborTexel.a); highAlpha = max(highAlpha, neighborTexel.a);
#else
            float3 neighbor = fetch(currentColor, uv + float2(nx, ny) * sizeJitter.xy).rgb;
#endif
            if (finiteColor(neighbor)) {
                neighbor = weigh(neighbor);
                low = min(low, neighbor); high = max(high, neighbor);
                mean += neighbor; square += neighbor * neighbor; count += 1;
#ifdef X3M_CURRENT_FILTER
                float2 sampleOffset = float2(nx, ny) - jitterPixels;
                float gaussian = exp(-X3M_FILTER_A * dot(sampleOffset, sampleOffset));
                filtered += neighbor * gaussian; filterTotal += gaussian;
#endif
            }
        }
    }
    mean /= count;
    float3 sigma = sqrt(max(square / count - mean * mean, 0));
    low = max(low, mean - clipGamma * sigma);
    high = min(high, mean + clipGamma * sigma);
#ifdef X3M_THIN_CLIP
    // Screen speed in px/frame: the history lookup against this pixel (both
    // carry the current jitter). S = 0 or thin = 0 is the clamp exactly.
    float speed = length((previousUV - uv) / sizeJitter.xy);
    float soft = sawValid && sawSentinel ? flicker.x * (1 - saturate((speed - 2) * 0.5)) : 0;
    old = lerp(clamp(old, low, high), old, soft);
#else
    old = clamp(old, low, high);
#endif
#ifdef X3M_CURRENT_FILTER
    // The centre sample is finite here, so filterTotal >= exp(-A * 0.5) > 0.
#ifdef X3M_FAR_STABILIZE
    float2 stabilise = fetch(lineMask, uv).rg;
    weighted += stabilise.r * (filtered / filterTotal - weighted);
#elif defined(X3M_LINE_FILTER)
    if (fetch(lineMask, uv).r > 0.5) weighted = filtered / filterTotal;
#else
    weighted = filtered / filterTotal;
#endif
#endif
    float keep = history.z;
#ifdef X3M_AGE_WEIGHT
    // Nearest reprojected texel of the previous age target; anything outside
    // [1, 64] (never written by this program) restarts the count.
    float age = fetch(previousAge, tap + float2(f.x >= 0.5 ? 1 : 0, f.y >= 0.5 ? 1 : 0) * sizeJitter.xy).r;
    age = age >= 1 && age <= 64 ? age : 1;
#ifdef X3M_FAR_STABILIZE
    keep += stabilise.g * (1 - saturate((speed - flicker.z) * flicker.w)) * (min(age / (age + 1), flicker.y) - keep);
#else
    keep = min(age / (age + 1), lerp(flicker.y, history.z, saturate((speed - flicker.z) * flicker.w)));
#endif
#endif
#ifdef X3M_THIN_CLIP
    // Alpha history (c22.z is 0 or 1): same weight, clamped to the current 3x3
    // alpha range. Weight 0 is the current alpha exactly; a result outside the
    // range (>= and <= only: a NaN fails) keeps the current alpha.
    float blendedAlpha = lerp(alpha, clamp(accumulated.a / total, lowAlpha, highAlpha), keep * luminance.z);
    if (blendedAlpha >= lowAlpha && blendedAlpha <= highAlpha) alpha = blendedAlpha;
#endif
    return emit(float4(unweigh(lerp(weighted, old, keep)), alpha), min(age + 1, 64));
}
