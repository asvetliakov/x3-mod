// Original D3D9 temporal resolve; see README.md for sampler and coordinate contracts.
sampler2D currentColor : register(s0);
sampler2D currentDepth : register(s1);
sampler2D previousColor : register(s2);
sampler2D previousDepth : register(s3);
sampler2D motionOverride : register(s4);
sampler2D currentReactive : register(s5);
sampler2D previousReactive : register(s6);
#ifndef X3M_HISTORY_TAPS16
// The 5-tap history reconstruction (the default; X3M_HISTORY_TAPS16 compiles the
// 16-tap point form of the earlier programs, resolve_*taps16.hlsl): the previous
// colour and the previous reactive mask bound a second time, s11 / s12 with LINEAR
// min / mag (TemporalPass, only for these programs); s2 / s6 stay point-sampled
// for the exact texel read at rest.
sampler2D previousColorLinear : register(s11);
sampler2D previousReactiveLinear : register(s12);
#endif
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
// options.z is the strict sky history term (0 off, 3 on; policy 2 only;
// docs/architecture/seta-motion.md): an unrouted far-plane pixel on its own path
// (motion alpha not 1, no closer neighbour won the dilation) accepts sentinel
// history taps only; a routed pixel (alpha 1, e.g. a fade-band draw with RT2
// masked over sky) keeps the test below whatever its depth target. Without
// it the relative tolerance (c6.y, 0.02 of depth 1) proves any geometry beyond
// device depth 0.98 "at or behind" the far plane, so the hull of a station that
// moved away this frame (depth 0.9997 under SETA, 5-30 px/frame) is accepted as
// the sky's history. The same term makes the dilated 1-px band beside a
// silhouette that moves bandSpeed px/frame or faster across this pixel
// relative to a world-static point at its depth current-only (the band term
// below). The mask and snapshot programs upload their own c7.z.
float4 options : register(c7); // motion enabled, reactive enabled, strict sky term (the resolve) / mode (mask and snapshot programs), depth-sentinel policy
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
// c24.yzw = the weight target, speed LO, 1 / (HI - LO) here (one gate: the adaptive weight's own LO /
// HI gate is not compiled and the two options exclude each other). r = g = 0
// is the thin / plain blend exactly: x + 0 * (y - x) with finite y.
// Thin-region gate (taa-lattice-crawl.md section 13), same mask: b = strength of
// the fragmented-depth region around this pixel, already closed by the fastest
// pixel within its reach. The history is pulled only (1 - b * c24.x) of the way to
// the clip box (c24.x = 1: clip off there) and the history weight is the larger of
// lerp(w, min(n / (n + 1), c24.y), g * slow) and lerp(w, min(n / (n + 1), c5.x), b). The 3x3
// sentinel soft clip of the thin variants is not compiled here (its S shares
// c24.x); b = a = 0 is the clamp and the far blend exactly.
// X3M_CAMERA_GATE (resolve_far_camera.hlsl; taa-lattice-crawl.md section 32.1):
// the camera-relative gate mode of the thin region. The mask is drawn by
// line_mask_camera_ps.hlsl: b = the region strength under the gate on
// min(screen speed, camera-relative speed), a = the strength the plain gate on
// the screen speed alone would give (a <= b). Where the camera term alone keeps
// the region open (b > a) that added strength (b - a) pulls the history toward
// the 7x7 min / max box of the weighted current colour (s9 / s10, drawn by
// TemporalPass just before this program from the current colour) instead of
// toward the unclipped history: old = lerp(clip3, old, a * c24.x) + (b - a) *
// c24.x * (box(old) - clip3). Where the plain gate was closed (a = 0) that is
// lerp(clip3, box(old), b * c24.x), the replay's box7 numerics; where a = b
// (rest, or a pixel the plain gate leaves open) the second term is 0 * finite
// and the history, the weight and every other term are the far program's
// exactly. No branch and no division; the box targets hold finite values at
// every pixel (0 where the box pass skipped one). The weight target follows b.
#ifdef X3M_CAMERA_GATE
#define X3M_FAR_STABILIZE 1
sampler2D boxLow : register(s9);
sampler2D boxHigh : register(s10);
#endif
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
// Exit reset of the strict sky history (docs/architecture/seta-sky-hull-share-decay.md;
// X3M_AGE_WEIGHT variants only, uploaded with c24 as one block): a band pixel that
// accepts history while its correspondence moves at least EXIT_PX px/frame against the
// camera path (below the band threshold, else it is refused) writes its age negated;
// the next frame a strict-sky pixel (nearest == 1) whose nearest reprojected age texel
// is negative keeps no history that one frame (keep 0, count restarted), so the hull
// share it took in the band leaves in one frame instead of decaying at the history
// weight. c25.x = EXIT_PX^2, or 1e30 when the option is off or the strict term is not in
// effect (the pass decides): under strict no band pixel below the band threshold
// reaches it, and under loose only a band pixel whose correspondence lies 1e15 px or
// more from the camera path would (a camera path at prepare's 1e15 bound; the mark is
// then written but read by nothing: the reset below is gated by the strict term's own
// tolerance and the count is read through abs). yzw = A, B, F of the motion history weight
// (docs/architecture/taa-motion-history-weight.md, at the depth proof below): 0, 1, 1 when off.
#ifdef X3M_AGE_WEIGHT
float4 skyExit : register(c25);
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
// Strict sky history, band term (docs/architecture/seta-motion.md section 4): an
// unrouted far-plane pixel in the dilated band whose routed correspondence
// differs from the rotation-only camera path by at least c5.y^(1/2) px/frame
// (the band threshold, X3M_TAA_SKY_HISTORY_BAND_PX, default 3, uploaded squared
// in c5.y by the pass) is current-only under strict. Below it (a static or slow
// object, or any object under a pan) the band keeps the geometry path.

// Every input is a single-level texture, point-sampled except s11 / s12 (the 5-tap
// history's LINEAR second binding of s2 / s6); an explicit LOD keeps the fetches
// free of gradients so they may sit under real branches.
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
// One Catmull-Rom history tap of the 16-tap form (X3M_HISTORY_TAPS16). Nonfinite
// taps contribute no energy and the remaining weights renormalize; a nonzero-weight
// tap with reactive previous coverage (mask policy) rejects the whole lookup.
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
// The age target is R32F: only .x is stored, so the count is written to every lane (one
// instruction fewer than a float4 with constant lanes; the stored bytes are the same).
ResolveOutput emit(float4 color, float age) { ResolveOutput o; o.color = color; o.age = age; return o; }
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
    float farPlaneTerm = 0; // farPlane as the band term's select operand (one cmp)
    if (options.w > 0.5 && depth <= -0.5) {
        if (options.w < 1.5) return emit(float4(color, alpha), 1);
        depth = 1;
        farPlane = true;
        farPlaneTerm = 1;
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
#if defined(X3M_THIN_CLIP) && !defined(X3M_FAR_STABILIZE)
#define X3M_SENTINEL_SOFT_CLIP 1
    // thin: the 3x3 (centre included) holds both a valid depth and the sentinel.
    bool sawSentinel = farPlane, sawValid = !farPlane;
#endif
#ifdef X3M_AGE_WEIGHT
    // Slot budget of the age variants (taa-motion-history-weight.md, as built): the centre texel is
    // fetched with the eight neighbours (one tap more per pixel) instead of being skipped, which
    // drops the skip test and its branch (5-7 slots) exactly: the centre cannot win the dilation
    // (neighbor < nearest fails on its own depth; a far-plane pixel's texel is the sentinel, which
    // fails neighbor >= 0) and the soft-clip flags it would set are the ones the initialisers set.
#define X3M_OFF_CENTRE(kx, ky) true
#else
#define X3M_OFF_CENTRE(kx, ky) (kx != 0 || ky != 0)
#endif
    [loop] for (int ky = -1; ky <= 1; ++ky) {
        [loop] for (int kx = -1; kx <= 1; ++kx) {
            if (X3M_OFF_CENTRE(kx, ky)) {
                float neighbor = fetch(currentDepth, uv + float2(kx, ky) * sizeJitter.xy).r;
                // neighbor >= 0 && neighbor < nearest is validDepth(neighbor) && neighbor < nearest here:
                // nearest starts at the validated centre depth (<= 1) and only falls, and a NaN fails >=.
                if (neighbor >= 0 && neighbor < nearest) { nearest = neighbor; dilate = float2(kx, ky); }
#ifdef X3M_SENTINEL_SOFT_CLIP
                if (validDepth(neighbor)) sawValid = true;
                if (neighbor <= -0.5 && neighbor >= -1e30) sawSentinel = true;
#endif
            }
        }
    }
    float2 dilatedUV = uv + dilate * sizeJitter.xy;
    // The dilated band of the sky: a far-plane pixel a valid neighbour below 1
    // won the dilation for (nearest < 1 here, before the alpha scaling below;
    // its own path, the fade-band draw included, keeps nearest == 1). Read by
    // the strict band term at the disocclusion test.
    float band = nearest < 1 ? farPlaneTerm : 0;

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
    // The correspondence is valid when its four clip components are finite and
    // within 1e20 and w exceeds the minimum W: as a count, 4 exactly when all
    // hold (each step is a >= compare a NaN fails; a NaN w fails the magnitude
    // step whatever the w step does). Summed into the usability test below.
    float validity = dot(step(abs(previousClip), 1e20), 1) - step(previousClip.w, rejection.w);
    previousUV = float2(previousClip.x, -previousClip.y) / max(previousClip.w, rejection.w) * 0.5 + 0.5;
    // Previous unjittered texture-center UV of the content plus the CURRENT
    // jitter (never the previous one): history lives on the unjittered grid.
    previousUV += 0.5 * sizeJitter.xy + sizeJitter.zw;
    expectedDepth = previousClip.z / max(previousClip.w, rejection.w);
    // The band term's reference: the camera path at the dilated position. The
    // route uploads the far-plane reprojection (camera_far_plane_reprojection:
    // zero z column, no translation), so whatever nearest is this is the
    // rotation-only path of a direction at infinity, and the routed
    // correspondence's displacement against it is the translation parallax
    // (an approaching station under SETA: 3-38 px/frame, a pan: 0). Feeding
    // translation-aware rows (the camera gate's c8 terms of the line mask)
    // here would follow a static silhouette under translation and silently
    // disable the band term.
    float2 cameraUV = previousUV;
    if (options.x > 0.5) {
        float4 motion = fetch(motionOverride, dilatedUV);
        // Band term, alpha gate: only an unrouted pixel (its own alpha not 1)
        // is the dilated band; a routed sentinel-depth draw beside closer
        // geometry (a fade-band square, an engine glow) keeps the geometry
        // path it takes today. (A NaN alpha clears the band: fail-open.)
        band *= step(fetch(motionOverride, uv).w, 0.5);
        // A routed correspondence (alpha 1) is never the strict sky path: nearest
        // drops to 0 for it and stays for alpha 0 / -1 (sge + mul; a NaN alpha
        // gives 0 and rejects below anyway). nearest's only readers after this
        // point are the fill-pair test below and the strict term.
        nearest *= step(motion.w, 0.5);
        if (motion.w == 1) {
            // RG is the producer's previous unjittered texture-center UV of the
            // content at this jittered sample; add the current jitter only.
            previousUV = motion.xy + sizeJitter.zw;
            expectedDepth = motion.z;
            validity = dot(step(abs(motion), 1e20), 1); // 4 when finite and within 1e20
        } else if (motion.w != 0) {
            // The route fills every unrouted pixel of RT1 with alpha -1 and
            // RT2 with the depth sentinel in one draw, so a far-plane pixel
            // that is its own correspondence (no closer neighbor won the
            // dilation) carries exactly that pair: under policy 2 it keeps
            // the camera path. A dilated neighbor with alpha -1 is a routed
            // draw without history (mode 0) and still rejects; any other
            // alpha rejects as before. (>= -1 && <= -1: exactly -1, NaN-safe.)
            // nearest >= 1 is all(dilate == 0) here: a far-plane pixel starts at
            // 1, only a neighbour below 1 lowers it, and alpha -1 left it alone.
            if (!(farPlane && nearest >= 1 && motion.w >= -1 && motion.w <= -1)) validity = 0;
        }
    }
    // Camera-relative displacement of the correspondence in px/frame: the routed
    // previous position against the rotation-only camera path (both
    // texture-centre UVs with the current jitter; 0 on the camera path itself,
    // so a pan past a static object never refuses). Squared against the band
    // threshold c5.y (px^2, X3M_TAA_SKY_HISTORY_BAND_PX squared by the pass):
    // no square root. The camera path cannot be NaN here (prepare bounds the
    // rows at 1e15 and every operand is finite); a wildly large one refuses
    // (fail closed: the pixel is current-only, never NaN), a NaN would clear
    // the select and keep the geometry path (the file's cmp convention).
    float2 relative = (previousUV - cameraUV) / sizeJitter.xy;
    float refused = dot(relative, relative) >= history.y ? band : 0;
#ifdef X3M_AGE_WEIGHT
    // Exit mark: the band pixel's parallax reaches c25.x (EXIT_PX^2) but not the band
    // threshold (a refused pixel never reaches the blend below). 0 or 1, a select on
    // band (never a product with the unbounded parallax).
    float exiting = dot(relative, relative) >= skyExit.x ? band : 0;
#endif
    // The dilated pixel's velocity, applied to this pixel.
    previousUV -= dilate * sizeJitter.xy;
    // The lookup is usable when the correspondence is valid (validity 4), its
    // expected depth lies in [0, 1] and the tap position inside the texture
    // (two float3 steps whose dot is 3): 7 exactly when all hold, the same
    // decision as the seven range tests (every step is a >= or <= compare, so a
    // NaN fails it and the lookup is refused as before).
    float3 lookup = float3(previousUV, expectedDepth);
    if (dot(step(0, lookup), step(lookup, 1)) + validity < 6.5)
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
    // Strict sky history (c7.z = 3, else 0): an unrouted pixel (alpha not 1)
    // whose 3x3 holds no depth below 1 has nearest == 1 (the far-plane pixel on
    // its own path; a valid neighbour below 1 would have won the dilation) and
    // no silhouette to accumulate: geometry in its history was an occluder that
    // moved away, whatever its depth. The term lifts the threshold above every
    // valid depth so only sentinel taps prove (a history depth of exactly 1.0
    // that is not the sentinel rejects too). A dilated far-plane pixel
    // (nearest < 1), a routed pixel (alpha 1: a fade-band draw over sky keeps
    // its own history, its depth target being motion.z) and every geometry
    // pixel keep the test as is; a routed pixel rasterised at exactly 1.0 is
    // therefore never strict. Band term (section 4 there): the dilated band
    // (band == 1) whose correspondence moves bandSpeed px/frame or more
    // relative to the camera path (refused, above) refuses the lookup under
    // strict. Its taps land beside last frame's silhouette a whole pixel or
    // more away, whose colour history holds that edge's accumulated hull share
    // whatever its depth (a sentinel tap beside the previous edge is as dark
    // as a hull tap), so a depth proof cannot separate the trail from the sky:
    // the pixel is current-only that frame. Below the relative speed (a static
    // or slow object, any object under a pan) nothing changes. Slot budget:
    // see the note.
    tolerance -= step(1, nearest) * options.z;
    // A tap proves when it is a valid depth at or behind the threshold or the
    // sentinel. validDepth's lower bound is folded into the threshold (max with
    // 0: identical for every finite tap; a NaN fails every step), the two
    // exclusive range tests are one float2 step pair and the proof, 0 or 1
    // exactly, scales the weight (weight * 1 and proven + 0 are exact).
    // considered starts at refused * c7.z: 0 normally; under strict (c7.z = 3)
    // a refused band pixel starts at 3, which the proof (at most the footprint's
    // weight, 1) can never reach, so the test below refuses it.
    float threshold = max(expectedDepth - tolerance, 0);
#ifdef X3M_AGE_WEIGHT
    // Motion history weight (docs/architecture/taa-motion-history-weight.md): the keep weight at
    // the blend is capped by a floor that opens with the squared motion of the correspondence,
    // cap = saturate(max(F, parallax2 * A + B)), c25.yzw = A, B, F, 1 at or below V0 px/frame
    // and F at or above V1 (A = -(1 - F) / (V1^2 - V0^2), B = 1 - A V0^2). parallax2 is the
    // smaller of two squared displacements: the translation parallax against the rotation-only
    // camera path (the band term's quantity: 0 under a pan, the SETA approach's px/frame on a
    // world-static hull) and the pixel's own screen motion (previousUV - uv, the lookup's
    // displacement: 0 on a static hull and on one that moves with the camera, the player's ship
    // in the external view or an escort under a turn, whose parallax is the turn itself). Only
    // a hull that moves on screen AND against the camera path, the SETA hull, is capped; a pan,
    // a co-moving hull and rest keep 1. A hull under SETA then accumulates a shorter history
    // (fewer Catmull-Rom resamples, less softening) while nothing below V0 changes. Off
    // uploads 0, 1, 1 and the cap is exactly 1 for every input: previousUV and uv lie inside
    // the texture here (the lookup test above), so the screen term is at most W^2 + H^2 px^2
    // and the min is finite even where the parallax overflowed to +inf (a routed pixel whose
    // camera path is 1e15 px off screen: min(+inf, finite) is the finite one), never NaN
    // (relative is a difference of finite values), so 0 * parallax2 + 1 is exactly 1 and
    // min(keep, 1) at the blend is keep bit for bit. (The compiler sinks this to the blend
    // beside the speed gate's length(), whose dp2add it shares, and keeps parallax2 live
    // across the loops.)
    float2 screenPx = (previousUV - uv) / sizeJitter.xy;
    float parallax2 = min(dot(relative, relative), dot(screenPx, screenPx));
    float cap = saturate(max(skyExit.w, parallax2 * skyExit.y + skyExit.z));
#endif
    float considered = refused * options.z, proven = 0;
    [loop] for (int ty = 0; ty < 2; ++ty) {
        [loop] for (int tx = 0; tx < 2; ++tx) {
            float weight = (tx ? f.x : 1 - f.x) * (ty ? f.y : 1 - f.y);
            // Taps of negligible weight cannot reject: they carry no visible energy.
            if (weight > 0.01) {
                considered += weight;
                float previous = fetch(previousDepth, tap + float2(tx, ty) * sizeJitter.xy).r;
                float2 atLeast = step(float2(threshold, -1e30), previous);
                float2 atMost = step(previous, float2(1, -0.5));
                proven += weight * dot(atLeast, atMost);
            }
        }
    }
    if (proven < considered - 0.001) return emit(float4(color, alpha), 1);

#ifdef X3M_HISTORY_TAPS16
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
#else
    // History color: Catmull-Rom (a = -0.5) through five hardware-bilinear taps
    // (Jimenez, "Filmic SMAA", SIGGRAPH 2016; Karis, UE4 TAA). Per axis, with t the
    // centre of texel 1 of the 4x4 neighbourhood (`tap`, texels 0..3 at t - 1 .. t + 2)
    // and f in [0, 1) the fraction:
    //   w0 = -f/2 + f^2 - f^3/2 = -f (1 - f)^2 / 2     (<= 0)
    //   w1 = 1 - 5 f^2 / 2 + 3 f^3 / 2                 (>= 0)
    //   w2 = f/2 + 2 f^2 - 3 f^3 / 2                   (>= 0)
    //   w3 = -f^2/2 + f^3/2 = -f^2 (1 - f) / 2          (<= 0), w0 + w1 + w2 + w3 = 1.
    // Texels 1 and 2 are one bilinear fetch at t + w2 / (w1 + w2) (in texels) weighted
    // w12 = w1 + w2 >= 1, because a bilinear sample at fraction h = w2 / w12 is
    // (1 - h) c1 + h c2 and w12 (1 - h) = w1, w12 h = w2 exactly. The 2-D filter is
    // the product of the axes; its sixteen texels form nine blocks (0, 12, 3 per axis),
    // of which the centre block (w12x w12y) and the four edge blocks (w0x w12y, w3x w12y,
    // w12x w0y, w12x w3y) are single bilinear fetches. The four corner blocks (products of
    // two non-positive weights, together (w0x + w3x)(w0y + w3y) <= 1/64 of the mass, the
    // most at f = 1/2) are dropped, and the five weights renormalised by their sum,
    //   total = 1 - (w0x + w3x)(w0y + w3y) in [63/64, 1],
    // so a constant history reconstructs that constant.
    // Three semantic changes against the 16-tap form (the filter unit cannot see per-tap
    // values):
    // (a) HDR route (k > 0, the shipping exposure k): the colour is filtered first and
    //     weigh()ed after, where the 16-tap form weighed every tap before the sum. At k = 0
    //     identical; at k > 0 a bright texel pulls the filtered history harder (weigh() is
    //     concave in luma, so with non-negative weights the average of the weighed taps is
    //     at most the weighed average), bounded by the unchanged 3x3 clip. Weighing each of the five fetches instead (per block, still not per tap)
    //     costs 24-30 slots per program (age 517, far 518, far_camera 526 of 512; measured),
    //     so it does not fit.
    // (b) The mask test sees the twelve texels of the five blocks only: a reactive texel
    //     whose only contribution is a dropped corner block no longer rejects the history.
    // (c) Out-of-range texels are no longer dropped one by one with the rest renormalised:
    //     a NaN / Inf texel of nonzero filter weight makes the filtered result nonfinite and
    //     refuses the whole lookup (current only); a finite texel above rejection.z (65000,
    //     below FP16's 65504) is averaged in, and the lookup is refused only when the
    //     filtered result itself exceeds rejection.z.
    // On the texel grid (f = 0, static content) the plain program reads
    // one exact point texel from s2 under a real branch, as the 16-tap program did. The history is this
    // program's own output, finite by construction, so a nonfinite or out-of-range
    // filtered result refuses the lookup (current only) instead of renormalising per tap.
    // Mask policy: the five bilinear samples of the previous (0 / 1, owned) mask at the
    // same positions, each scaled by the magnitude of its weight: the sum is nonzero
    // exactly when some texel of nonzero weight in a fetched block is reactive (every
    // term is >= 0), NaN when a sample is, and maskSafe refuses both.
    HISTORY_SUM accumulated;
    float total = 1;
    bool reactive = false;
#ifdef X3M_THIN_CLIP
    // Slot budget: as in the 16-tap form the variants have no rest branch (it costs them
    // about 20 slots and far_camera 522 of 512). On the texel grid s = w2 = 0, so t12 is
    // the texel centre, the centre weight 1, the edge weights 0 and the total 1.
    {
#else
    [branch] if (all(f == 0)) {
        reactive = options.y > 0.5 && !maskSafe(fetch(previousReactive, tap).r);
        accumulated = fetch(previousColor, tap).rgb;
    } else {
#endif
        // s = -(w0 + w3) = f (1 - f) / 2, so w0 = -s (1 - f), w3 = -s f, w12 = 1 + s, and the five weights sum to
        // w12x w12y + (w0x + w3x) w12y + w12x (w0y + w3y) = 1 - sx sy.
        float2 g = 1 - f;
        float2 s = 0.5 * f * g;
        float2 w12 = 1 + s;
        float2 w2 = f * (0.5 + f * (2 - 1.5 * f));
        // Texel-centre positions are biased by +1/1024 texel. The float32 UV of a texel centre lands within
        // [-1.2e-4, +2.4e-4] texel of it for W = 1280 .. 5120 (float32 emulation,
        // verification/results/taa-high-resolution/s3_centre_error.py; negative at W = 3440), so a filter unit that
        // truncates its 8-bit sub-texel fraction instead of rounding would take 1/256 of the left / upper neighbour
        // at every exact centre and the history feedback would compound it. t0 and t3 always sit on centres, t12
        // does where w2 / w12 is 0 (f = 0 on that axis: the rest of the thin / age / far programs, which have no point
        // branch); with the bias those errors are in [+8.5e-4, +1.3e-3], positive and below 1/512, which rounding and
        // truncation both map to fraction 0. t12 takes max(w2 / w12, 1/1024) rather than an added bias, so every
        // fraction the filter resolves (>= 1/512) is unchanged and no systematic sub-texel shift enters the history.
        float2 centred = tap + sizeJitter.xy * (1.0 / 1024);
        float2 t0 = centred - sizeJitter.xy, t3 = centred + 2 * sizeJitter.xy, t12 = tap + max(w2 / w12, 1.0 / 1024) * sizeJitter.xy;
        // Edge blocks: left, right, top, bottom (each <= 0).
        float centre = w12.x * w12.y;
        float4 edge = -float4(s.x * g.x, s.x * f.x, s.y * g.y, s.y * f.y) * float4(w12.y, w12.y, w12.x, w12.x);
        total = 1 - s.x * s.y;
#ifdef X3M_THIN_CLIP
#define X3M_HISTORY_FETCH(uv) fetch(previousColorLinear, uv)
#else
#define X3M_HISTORY_FETCH(uv) fetch(previousColorLinear, uv).rgb
#endif
        accumulated = X3M_HISTORY_FETCH(t12) * centre + X3M_HISTORY_FETCH(float2(t0.x, t12.y)) * edge.x +
                      X3M_HISTORY_FETCH(float2(t3.x, t12.y)) * edge.y + X3M_HISTORY_FETCH(float2(t12.x, t0.y)) * edge.z +
                      X3M_HISTORY_FETCH(float2(t12.x, t3.y)) * edge.w;
#undef X3M_HISTORY_FETCH
        if (options.y > 0.5) {
            float4 marks = float4(fetch(previousReactiveLinear, float2(t0.x, t12.y)).r, fetch(previousReactiveLinear, float2(t3.x, t12.y)).r,
                                  fetch(previousReactiveLinear, float2(t12.x, t0.y)).r, fetch(previousReactiveLinear, float2(t12.x, t3.y)).r);
            reactive = !maskSafe(fetch(previousReactiveLinear, t12).r * centre - dot(marks, edge));
        }
    }
    float3 filteredHistory = accumulated.rgb / total;
    if (reactive || !finiteColor(filteredHistory)) return emit(float4(color, alpha), 1);
    float3 old = weigh(filteredHistory);
#endif
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
#ifdef X3M_FAR_STABILIZE
    float4 stabilise = fetch(lineMask, uv);
    float soft = stabilise.b * flicker.x;
#else
    // saturate(2 - 0.5 speed) is 1 - saturate((speed - 2) * 0.5) for the finite speed (one mad_sat).
    float soft = sawValid && sawSentinel ? flicker.x * saturate(2 - 0.5 * speed) : 0;
#endif
#ifdef X3M_CAMERA_GATE
    // The strength the camera term added (b - a) takes the history clipped to the current 7x7 box; the screen gate's own share (a) the unclipped one.
    float3 clipped = clamp(old, low, high);
    float3 boxed = clamp(old, fetch(boxLow, uv).rgb, fetch(boxHigh, uv).rgb);
    old = lerp(clipped, old, stabilise.a * flicker.x) + (stabilise.b - stabilise.a) * flicker.x * (boxed - clipped);
#else
    old = lerp(clamp(old, low, high), old, soft);
#endif
#else
    old = clamp(old, low, high);
#endif
#ifdef X3M_CURRENT_FILTER
    // The centre sample is finite here, so filterTotal >= exp(-A * 0.5) > 0.
#ifdef X3M_FAR_STABILIZE
    weighted += stabilise.r * (filtered / filterTotal - weighted);
#elif defined(X3M_LINE_FILTER)
    if (fetch(lineMask, uv).r > 0.5) weighted = filtered / filterTotal;
#else
    weighted = filtered / filterTotal;
#endif
#endif
    float keep = history.z;
#ifdef X3M_AGE_WEIGHT
    // Nearest reprojected texel of the previous age target: its sign is the exit mark,
    // its magnitude the count. Above 64, or a NaN (the <= fails), restarts the count;
    // the lower bound of the old [1, 64] test is not needed: this program writes counts
    // of 1..64 only, 0 never, and s7 is bound only behind a valid history every pixel
    // of which this program wrote, so no |age| below 1 can be read (slot budget).
    float ageRaw = fetch(previousAge, tap + float2(f.x >= 0.5 ? 1 : 0, f.y >= 0.5 ? 1 : 0) * sizeJitter.xy).r;
    float age = abs(ageRaw);
    age = age <= 64 ? age : 1;
    // Exit reset (seta-sky-hull-share-decay.md section 4): a strict-sky pixel under
    // strict whose texel was marked keeps nothing this frame, its count restarted (age
    // 0 here: the adaptive weight min(0 / 1, ..) is 0, the write min(0 + 1, 64) is 1;
    // the far variants zero keep below), so the blend is the current sample,
    // x + 0 * (old - x), the alpha its current alpha, and its history is the sky from
    // the next frame on. The predicate is one max: tolerance is below 0 exactly where
    // the far-plane term above took c7.z = 3 off it (a strict-sky pixel under strict)
    // and >= 0 everywhere else (loose included), and the texel is below 0 exactly when
    // marked, so max(tolerance, ageRaw) >= 0 is "not both", i.e. "keep the history"
    // (a NaN is unreachable in either operand: every tolerance operand is finite, and
    // only this program writes the age target, counts of 1..64, behind a valid
    // history). A pixel still in the band, a routed pixel and every geometry pixel
    // read the mark as "keep" and continue the count through the sign (abs above).
    // The read stays here, after the depth proof and the history taps, so the
    // Catmull-Rom weight arithmetic of these variants keeps the plain program's
    // instruction order and rounding (a read before the verdict made the compiler
    // regroup it).
    float keeping = max(tolerance, ageRaw);
    age = keeping >= 0 ? age : 0;
#ifdef X3M_FAR_STABILIZE
    // Two gated targets, each exact: the far weight c24.y through g and this pixel's own speed gate, the thin-region weight c5.x
    // (the resolve never read c5.xy) through b. b = 0 is the far blend exactly, young pixels below the base weight included.
    float ramp = age / (age + 1);
    float farKeep = keep + stabilise.g * (1 - saturate((speed - flicker.z) * flicker.w)) * (min(ramp, flicker.y) - keep);
    keep = stabilise.b > 0 ? max(farKeep, keep + stabilise.b * (min(ramp, history.x) - keep)) : farKeep;
    keep = keeping >= 0 ? keep : 0; // the exit reset (the adaptive form above is 0 through the age)
#else
    keep = min(age / (age + 1), lerp(flicker.y, history.z, saturate((speed - flicker.z) * flicker.w)));
#endif
    // The motion history weight's cap (computed with the depth proof above): min never raises a
    // weight (young pixels, the exit reset's 0); before the alpha history so it uses the same weight.
    keep = min(keep, cap);
#endif
#ifdef X3M_THIN_CLIP
    // Alpha history (c22.z is 0 or 1): same weight, clamped to the current 3x3
    // alpha range. Weight 0 is the current alpha exactly; a result outside the
    // range (>= and <= only: a NaN fails) keeps the current alpha.
    float blendedAlpha = lerp(alpha, clamp(accumulated.a / total, lowAlpha, highAlpha), keep * luminance.z);
#ifdef X3M_AGE_WEIGHT
    // Slot budget of the age variants (taa-motion-history-weight.md, as built): the same two range
    // tests through one min, one to three slots fewer. Exact on the condition that lowAlpha,
    // highAlpha and blendedAlpha are finite: the two differences are then the ones the compares
    // form and min(a, b) >= 0 is a >= 0 and b >= 0. A NaN blendedAlpha (a NaN history alpha, or
    // the current alpha through a NaN clamp) gives min(NaN, NaN), which fails as before. A
    // non-finite lowAlpha or highAlpha (an FP16 scene alpha the game wrote as NaN or infinite;
    // the range starts at the finite current alpha and a NaN neighbour does not move min / max
    // on the verified backend) is the one input where the form may differ from the two
    // compares: the original kept the current alpha there, this one follows the backend's min
    // with a NaN operand. Alpha history is the HDR route's option only (c22.z).
    if (min(blendedAlpha - lowAlpha, highAlpha - blendedAlpha) >= 0) alpha = blendedAlpha;
#else
    if (blendedAlpha >= lowAlpha && blendedAlpha <= highAlpha) alpha = blendedAlpha;
#endif
#endif
#ifdef X3M_AGE_WEIGHT
    // The count continues (1 after a reset); a marked band pixel writes it negated (the
    // exit mark: -exiting >= 0 is "not exiting", one cmp with a negate modifier).
    float aged = min(age + 1, 64);
    return emit(float4(unweigh(lerp(weighted, old, keep)), alpha), -exiting >= 0 ? aged : -aged);
#else
    return emit(float4(unweigh(lerp(weighted, old, keep)), alpha), min(age + 1, 64));
#endif
}
