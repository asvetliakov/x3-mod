#pragma once
#include <d3d9.h>
#include <cstdint>
#include "../temporal/resolve.h"
#include "gpu_sync_timing_core.h"

namespace x3m::renderer {
enum class MotionPolicy { Unavailable, KnownCameraOnly, PerPixel };
// DerivedFromDepthSentinel needs no mask texture: a current pixel whose R32F
// depth is the -1 sentinel (no routed opaque draw wrote it) resolves
// current-only; a sentinel previous tap remains valid background behind a
// silhouette under the existing disocclusion rules. It requires
// the direct R32F depth input; the D24X8 decoder cannot carry the sentinel.
// SupplementalMaskWithDepthSentinel retains that baseline and additionally
// requires complete current coverage for the enhanced source set (including
// native-result fallback and zero gain). Raw FP16 red is exactly zero when safe;
// all other values are reactive. The snapshot canonicalizes and expands by one
// pixel before resolving color; previous snapshots must not be expanded again.
// Incomplete coverage uses Unavailable with a null mask, which cannot seed history.
enum class ReactivePolicy {
    Unavailable,
    KnownNonReactive,
    RequiredMask,
    DerivedFromDepthSentinel,
    SupplementalMaskWithDepthSentinel
};
// What feeds the thin region's flag (X3M_TAA_THIN_REGION_SOURCE; docs/architecture/taa-thin-geometry-alternatives.md
// section 3.2, docs/architecture/taa-mask-fold.md): Both (the value when unset) = the screen-space fragmented-depth
// search united with the thin vote where it is cast; Vote = the vote alone (the search is skipped; the launcher's
// default since the mask fold); Screen = the search alone, which only the screen-gate chain can still draw (its plain
// tests program): a camera-gate run with Screen is refused. Where the vote is not cast a Vote run is a Both run
// (Diagnostics::thin_region_source says what ran).
enum class ThinRegionSource : unsigned { Both = 0, Screen = 1, Vote = 2 };
inline const char* thin_region_source_name(ThinRegionSource source) noexcept {
    return source == ThinRegionSource::Screen ? "screen" : source == ThinRegionSource::Vote ? "vote" : "both";
}
struct FrameInputs {
    // Current color: exactly one of the two.
    // A16B16G16R16F texture at the frame size, native, complete local
    // viewport: the HDR route's FP16 scene target (stage 3 of the HDR scene
    // path) or a fixture's exact FP16 image. Sampled directly as s0: no copy,
    // no format conversion, no gate. The resolved output is Output::color (an
    // owned FP16 history texture); the caller decides what consumes it (the
    // HDR write-back samples it; the 8-bit route copies it back).
    IDirect3DTexture9* color = nullptr;
    // A8R8G8B8/X8R8G8B8 default-pool render-target surface (need not be a texture
    // level). Copied once into an owned FP16 scratch that becomes s0: by a
    // format-converting StretchRect (configure_copy(false), the default) or,
    // where the device does not grant that conversion (configure_copy(true)),
    // by a same-format StretchRect into an owned staging texture and one
    // identity draw; no gamma conversion is applied anywhere (values are
    // linear-encoded). In draw mode the pass also draws the new history back
    // into this surface after the resolve (Output::display_written), so the
    // caller's copy-back StretchRect is not needed.
    IDirect3DSurface9* color_surface = nullptr;
    // Current depth: exactly one of the two.
    IDirect3DTexture9* depth_snapshot = nullptr; // verified native D24X8 comparison snapshot; runs the decoder draw
    // R32F, G32R32F or A32B32G32R32F .r device depth z/w in [0,1]; -1 sentinel.
    // R32F copies directly; the wider formats point-sample .r through the configured
    // identity copy shader into R32F history. No decoder draw runs.
    IDirect3DTexture9* current_depth = nullptr;
    IDirect3DTexture9* motion = nullptr;   // RGBA32F if PerPixel; alpha ABI in temporal/README
    IDirect3DTexture9* reactive = nullptr; // RequiredMask: R32F; Supplemental: A16B16G16R16F raw red coverage
    UINT width = 0, height = 0;
    std::uint64_t epoch = 0;      // stable camera/scene/resource regime, not frame/clear count
    float clip_to_previous[16]{}; // unjittered, row-major, column-vector multiplication
    // Raster pixels, positive Y down. The camera path subtracts the current
    // jitter before the inverse projection (the motion texture is rasterized on
    // the current jittered grid already, so the motion path uses it nowhere).
    // The previous jitter is uploaded for ABI stability only: the resolve never
    // adds it (history is on the unjittered grid; the producer's RG is already
    // the previous unjittered UV).
    float current_jitter[2]{}, previous_jitter[2]{};
    float weight = .9f;
    float rejection[4]{.0001f, .02f, 65000.f, .000001f}; // max(absolute, relative*depth) depth tolerance, HDR limit,
                                                         // minimum W
    // k of the resolve's reversible luminance weighting (resolve.h, c22.x):
    // 0 (the default, the 8-bit route) is the exact identity; the HDR route
    // uploads the exposure multiplier its write-back applies to the resolved
    // image, so the weighted domain is the display-relative luminance. Finite,
    // 0 <= k <= 65504; run refuses anything else.
    float luminance_k = 0.f;
    // Far stabiliser (docs/architecture/taa-distant-line-fade.md section 9),
    // two separately switchable components gated by farw = saturate((depth -
    // far_d0) * far_inv) of the pixel's own depth (0 on the sentinel; the
    // caller derives the pair with x3::temporal::far_gate and passes far_inv =
    // 0, mask off, on a frame without a valid projection). far_weight: 0 off,
    // else within [weight, 0.99]: history weight lerp(weight, min(n / (n + 1),
    // far_weight), farw * (1 - saturate((speed - lo) / (hi - lo)))), on the age
    // target. far_filter: 0 off, else A in (0, 4]: current sample lerp(point,
    // exp(-A d^2) average, farw). Either needs configure_far() and
    // MotionPolicy::PerPixel (the speed gate reads the routed motion); refused beside
    // adaptive_weight (one gate) and thin_clip. farw = 0 pixels are the plain
    // blend bit for bit.
    float far_weight = 0.f, far_filter = 0.f, far_d0 = 0.f, far_inv = 0.f;
    // Thin-region stabiliser (docs/architecture/taa-lattice-crawl.md section 13):
    // where the depth is FRAGMENTED (some 7-tap line through the pixel changes
    // between geometry and its background at least twice; the 11x11 around such
    // pixels, grown by 5), and nothing within 8 px moves faster than the speed gate below,
    // the history is pulled only (1 - thin_region_relax) of the way to the
    // variance clip (1: clip off) and the history weight rises to
    // min(n / (n + 1), thin_region_weight): the cumulative mean of the jitter
    // cycle until the cap binds. 0 off, else within [weight, 0.99]. Runs on the
    // far-stabiliser program (configure_far(), PerPixel motion, the age target),
    // shares its speed gate, and like it excludes adaptive_weight and needs
    // thin_clip 0 (the program has no 3x3 sentinel soft clip). Pixels outside
    // the region are the far / plain blend bit for bit.
    float thin_region_weight = 0.f, thin_region_relax = 1.f;
    // Emissive vote of the thin region (docs/architecture/thin-glow-lines.md 8.3
    // R3; taa-lattice-crawl.md section 32.7), E in scene luma: 0 (the default)
    // leaves every mask target bit for bit what a run without the field writes.
    // E > 0 also puts a ROUTED pixel of valid depth (motion alpha 1, the routing
    // the resolve reads) whose own HDR luma L exceeds E and whose 3x3 luma minimum
    // is below L / 3 into the region: a local peak, i.e. a thin emissive strip on a
    // distant hull, not a uniformly lit panel. Those pixels then take the
    // thin-region weight min(n / (n + 1), thin_region_weight) at rest and, with the
    // camera gate, under a camera pan, instead of the base weight. The vote lands in
    // the mask's fragmentation channel and follows the whole existing chain (the
    // screen gate's 11x11 grow and 17x17 speed gate, or the camera gate's region
    // hold and 7x7 box clip); the resolve programs are
    // untouched. Unrouted sentinel pixels (lasers, engine glows, sky) are outside
    // the class and keep the sentinel law, as do non-finite taps (|L| > 65000 or
    // NaN), which can neither vote nor lower a neighbour's 3x3 minimum. E is in the
    // units of the scene this pass binds for the resolve: with an 8-bit color_surface
    // that is the display-referred copy, where nothing exceeds 1 and E >= 1 never
    // fires; the FP16 `color` input is the HDR scene the design's E = 1 assumes.
    // Finite and >= 0, read and validated only with thin_region_weight > 0; costs 9
    // scene taps in the tests (the camera-gate resolve's, or the screen-gate chain's
    // tests draw) and no extra motion fetch (the speed gate's sample is shared).
    float thin_region_emissive = 0.f;
    // Camera-relative gate mode of the thin region (taa-lattice-crawl.md section
    // 32.1), off by default (the screen-speed gate above, bit for bit). On: the
    // region's gate speed is min(screen speed, camera-relative speed), the
    // routed correspondence measured against the camera path at the pixel's
    // depth, so a coherent camera pan keeps the region open, and where the
    // camera term alone keeps it open the relaxed history is clipped to the 7x7
    // min / max box of the current colour (one extra MRT draw into two owned
    // FP16 targets). Needs camera_gate_available() (configure_far() created the
    // programs and the history draws 5 taps); anything else refuses the run.
    // Ignored without thin_region_weight. A failed box-target allocation that is
    // not a lost device turns the thin region off for the session (no fallback
    // program set; camera_gate_failed(), camera_gate_result(); re-armed by
    // Reset): later camera-gate runs resolve without it, a far stabiliser of
    // their own carries on.
    // The box pair exists only while the gate runs: the first run without it
    // releases the pair (a configuration change, never per frame).
    // The camera gate runs A' (docs/architecture/taa-plan-lifted-slot-cap.md step 1, the region hold; the only
    // camera-gate path since Run 79 A) with the mask fold (docs/architecture/taa-mask-fold.md): no mask draw; the
    // camera-gate resolve computes the per-pixel tests itself (both gates, farw, the flag: the thin vote, the
    // fragmented-depth search, the emissive vote) from current_depth, the motion and the scene, composes the region,
    // holds it thin_region_hold_frames after the pixel was last flagged and holds the camera gate's closure as long
    // (its own openness the smaller of the pixel's and its nearest-depth 3x3 neighbour's; the screen gate is not held),
    // both carried along the reprojection in the fraction of the age count (resolve.hlsl X3M_REGION_HOLD), and writes
    // the next depth history as a third render target (R32F, the current depth's .r bit for bit): NumSimultaneousRTs >=
    // 3 (camera_gate_available()). The box programs open on last frame's region hold and mark what they computed; where
    // the camera term adds strength and the box did not run (a pixel's first frame in the region) the resolve takes the
    // same 7x7 in place. Needs current_depth (a D24X8 snapshot is refused). A run that leaves the camera gate after a
    // camera-gate run restarts the history (the other age programs read whole counts).
    bool thin_region_camera_gate = false;
    // The hold length in frames: the jitter period (a pixel flagged in any phase stays in the region the whole
    // cycle). 1..64, read with the camera gate only; anything else refuses the run.
    unsigned thin_region_hold_frames = 8;
    // Thin vote (X3M_TAA_THIN_VOTE; docs/architecture/taa-thin-geometry-alternatives.md section 3.2), off by default.
    // On, with the thin region and an A32B32G32R32F current depth, the flag is also set on a pixel whose current-depth
    // .a (the route's 1 - thin) is in [0, 1): read by the camera-gate resolve (c10.z), or, on the screen-gate chain, by
    // the depth-folding tests draw's twin (configure_thin_vote()). Diagnostics::thin_vote names what ran; never refuses
    // a run.
    bool thin_vote = false;
    // Source of the thin region's flag (ThinRegionSource above), read with thin_region_weight > 0 only. Vote needs the
    // vote cast (the thin_vote conditions above) and falls back to Both without it; Screen refuses a camera-gate run
    // and draws the plain tests program on the screen-gate chain. No extra draw, constant upload or program either way
    // (c10.y). A value outside the enum refuses the run.
    ThinRegionSource thin_region_source = ThinRegionSource::Both;
    // Depth and translation term of the camera gate's camera path, c8 of the
    // camera-gate resolve only (camera_reprojection.h camera_depth_parallax();
    // taa-lattice-crawl.md section 32.3). All zero = the far-plane path of
    // clip_to_previous; a non-finite value is uploaded as zero.
    float camera_depth_parallax[4]{};
    // The latch-free form (camera_lane_parallax(): (DX, DY, DW, 1)), c9 of the
    // same program: used where current_depth is A32B32G32R32F, per pixel from its
    // .b (clip w = view z), which the resolve reads at s1,
    // and camera_depth_parallax is then not consulted (a valid depth without a
    // positive .b stays on the far-plane path). Ignored (c9 = 0, s5 unbound) for
    // any other depth input, w != 1 or a non-finite value: camera_depth_parallax
    // is the fallback for the frame.
    float camera_lane_parallax[4]{};
    // Speed gate of far_weight and of the thin region, px/frame: full below far_speed_lo, the base weight from
    // far_speed_hi (0 <= lo < hi <= 64).
    float far_speed_lo = x3::temporal::kFarSpeedLo, far_speed_hi = x3::temporal::kFarSpeedHi;
    // The far weight's motion gate on the camera-gate resolve (X3M_TAA_FAR_GATE; docs/architecture/taa-mask-fold.md
    // section 4.2 addendum): true (the default) opens it on the camera-relative openness of the region (a world-static
    // far pixel keeps far_weight under a camera pan), false on the pixel's screen speed (the gate before 2026-09-25).
    // c11.x of that program; the far program (no camera gate) always uses the screen speed and ignores it.
    bool far_camera_gate = true;
    // The far clip (X3M_TAA_FAR_CLIP; docs/architecture/taa-mask-fold.md section 4.2 addendum "far clip"): on the
    // camera-gate resolve a pixel outside the thin region whose farw * openC exceeds this threshold clips its history
    // against the 7x7 min / max of the weighed current colour (taken in place on that branch, or the box targets where
    // they are marked) instead of the 3x3 variance clip. c13.z of that program (kFarClipOff with the far gate off); 0
    // (the default) is any far weight, x3::temporal::kFarClipOff (2) the 3x3 clip everywhere. Finite, in [0, 2];
    // anything else refuses the run. The far program (no camera gate) has no far clip and ignores it.
    float far_clip = x3::temporal::kFarClipThreshold;
    // Post-resolve sharpen of the display image (sharpen.h, rcas.hlsl;
    // docs/architecture/temporal-integration.md "Post-resolve sharpen"): 0
    // (the default) draws nothing and the run is bit-identical to a run
    // without the field; in (0, 1] the pass draws RCAS of its freshly written
    // FP16 history INTO color_surface (the game's 8-bit target) after the
    // resolve, replacing the caller's copy-back (Output::display_written).
    // The history itself is never sharpened. Requires color_surface (the FP16
    // input path has no 8-bit destination here: the HDR write-back sharpens)
    // and a pass initialised with the sharpen program; anything else refuses
    // the run. Finite, 0 <= sharpen <= 1.
    float sharpen = 0.f;
    // Flicker suppression (docs/architecture/taa-flicker-suppression.md), all
    // off by default; with all three off the pass binds the plain programs and
    // the run is bit-identical to a run without the fields. Any of them needs
    // configure_flicker() to have succeeded, else the run is refused. The DLL
    // sets alpha_history only: its thin-clip and adaptive-weight options were
    // removed 2026-09-23 (cleanup batch 6); thin_clip and adaptive_weight stay
    // here as the way the temporal fixture selects the age program
    // (resolve_age.hlsl), on which its exit-reset rows run.
    // thin_clip S in [0, 1]: where the current 3x3 depth mixes the sentinel
    // and geometry the history is pulled only (1 - S) of the way to the clip
    // box, fading out between 2 and 4 px/frame.
    float thin_clip = 0.f;
    // adaptive_weight WMAX (0 off; weight <= WMAX <= 0.99): the history weight
    // becomes min(n / (n + 1), wmax(speed)) with n the per-pixel age kept in
    // an owned R32F pair written as COLOR1; wmax falls from WMAX to `weight`
    // between adaptive_lo and adaptive_hi px/frame. Refused without thin_clip
    // > 0 (alone it dims thin lattices) and without age_available().
    float adaptive_weight = 0.f, adaptive_lo = x3::temporal::kAdaptiveLoDefault,
          adaptive_hi = x3::temporal::kAdaptiveHiDefault;
    // The output alpha is the history alpha blended with the pixel's history
    // weight and clamped to the current 3x3 alpha range, instead of the
    // current alpha. FP16 `color` input only (the HDR route: bloom reads the
    // resolved alpha as its authored-glow weight); refused with color_surface.
    bool alpha_history = false;
    MotionPolicy motion_policy = MotionPolicy::Unavailable;
    // Unknown coverage produces current-only output and cannot establish usable
    // history. RequiredMask demands complete conservative visible RGB coverage,
    // independent of alpha; missing/wrong input rejects the run.
    ReactivePolicy reactive_policy = ReactivePolicy::Unavailable;
    bool history_allowed = false, camera_cut = false;
    bool cut = false; // route cut detector verdict for this frame; rejects history like camera_cut
    // With either depth-sentinel policy: reproject sentinel pixels through
    // clip_to_previous at the far plane instead of resolving them current-only.
    // Set true only when clip_to_previous is an actual valid camera
    // reprojection; leave false when that contract is unavailable.
    bool sentinel_camera = false;
    // With sentinel_camera: the strict sky history (resolve c7.z = 3). A far-plane
    // pixel whose 3x3 holds no geometry accepts sentinel history taps only, so the
    // hull of an object that moved away this frame never becomes the sky's history
    // (docs/architecture/seta-motion.md). Ignored without sentinel_camera.
    bool sentinel_strict_sky = false;
    // Band threshold of the strict sky history in px/frame (resolve c5.y, uploaded
    // squared; seta-motion.md section 4): an unrouted sky pixel in the dilated band
    // whose routed correspondence moves at least this much against the rotation-only
    // camera path is current-only under strict. 1..16; read with sentinel_strict_sky only.
    float sky_history_band_px = 3.f;
    // Exit reset of the strict sky history (resolve c25.x, uploaded squared with c24 by
    // the age variants; docs/architecture/seta-sky-hull-share-decay.md): a band pixel
    // that accepts history while its parallax is at least this (px/frame) writes its
    // age negated, and the strict-sky pixel that reads the mark next frame is
    // current-only once. 0 off, else 0.125..sky_history_band_px (run() refuses the rest);
    // read with sentinel_strict_sky and an age program only (the pass uploads the off
    // value otherwise, so the age target's bytes are those of the option off).
    float sky_history_exit_px = 0.f;
    // Motion history weight (resolve c25.yzw, uploaded with c24 by the age variants;
    // docs/architecture/taa-motion-history-weight.md): the history keep weight is capped
    // at saturate(max(F, p^2 A + B)), 1 at or below motion_weight_v0 px/frame of the
    // pixel's translation parallax against the rotation-only camera path and F at or
    // above motion_weight_v1, so a hull under SETA translation accumulates a shorter
    // history (less resample softening) while rest, pans and slow flight are untouched.
    // motion_weight 0 off (the upload is 0, 1, 1: the cap is 1 and the blend bit for bit
    // the run's without the fields), else 0.5 <= F < 1 with 0 <= V0 < V1 <= 64 (run()
    // refuses the rest). Read by the age programs only; the caller keeps it 0 without
    // the camera path (relative would then be the screen motion, a pan included).
    float motion_weight = 0.f, motion_weight_v0 = 2.f, motion_weight_v1 = 8.f;
    bool caller_scene_open = true;
    bool caller_stateblock_recording = false;
    bool caller_queries_idle = false; // positive knowledge: no active occlusion/statistics query
};
struct Output {
    // Borrowed native objects; no AddRef. Valid only until next run, invalidate,
    // before_reset, shutdown or destruction. Never retain across those boundaries.
    IDirect3DTexture9* color = nullptr;
    IDirect3DTexture9* depth = nullptr;
    std::uint64_t generation = 0;
    bool used_history = false;
    IDirect3DTexture9* reactive = nullptr; // owned R32F snapshot with either mask policy; same borrowing rules
    // Level 0 of color, for the caller's copy-back StretchRect into the 8-bit
    // main target. The caller owns state save/restore around the whole
    // copy / run / copy-back sequence; run restores only what it touched.
    IDirect3DSurface9* color_surface = nullptr;
    // FrameInputs::sharpen > 0, or the copy-by-draw mode with an 8-bit input:
    // the pass drew the display image (sharpened, or the identity copy of the
    // new history) into FrameInputs::color_surface itself; the caller must
    // not copy back.
    bool display_written = false;
    // The sharpened draw's result: S_FALSE when not requested, S_OK when it
    // drew (display_written), otherwise the failure that kept the resolve
    // (the history set is published regardless) and left the display to the
    // caller's copy-back; a lost device fails the run instead.
    HRESULT sharpen_result = S_FALSE;
    // The copy-by-draw write-back's result: S_FALSE when not requested (stretch
    // mode, an FP16 input, or the sharpen draw already wrote the display),
    // S_OK when it drew (display_written), otherwise the failure that left the
    // display to the caller's copy-back; a lost device fails the run instead.
    HRESULT copy_result = S_FALSE;
    // The age target written by this run (adaptive_weight > 0 or a far-program run), else null;
    // same borrowing rules. For capture dumps only.
    IDirect3DTexture9* age = nullptr;
    // Far stabiliser / screen-gate thin region runs: the owned A8R8G8B8 mask the resolve read at s8 (r filter weight, g
    // far history-weight gate); null on a camera-gate run (no mask draw since the mask fold). Diagnostic, same
    // borrowing rules. Keep here: run() fills the struct positionally.
    IDirect3DTexture9* stabiliser_mask = nullptr;
    // Camera-gate runs: the box pair the resolve read at s9 / s10 ([0] minimum, .a = 1 where computed; [1] maximum), W
    // x H, or W/2 x H/2 when Diagnostics::box_half; null otherwise. Diagnostic (fixtures), same borrowing rules; after
    // stabiliser_mask because run() fills the struct positionally.
    IDirect3DTexture9* box_low = nullptr;
    IDirect3DTexture9* box_high = nullptr;
};
struct Diagnostics {
    HRESULT operation = S_OK, restoration = S_OK;
    bool history_valid = false;
    bool reset_pending = false; // before_reset seen, after_reset(SUCCEEDED) not yet
    std::uint64_t completed_frames = 0;
    // The last run wrote the next depth history as COLOR1 of the mask chain's first draw instead of a copy draw
    // (docs/architecture/taa-high-resolution.md S1: a two- or four-channel current depth, a far-program run, the
    // depth-folding mask program created); false otherwise.
    bool depth_folded = false;
    // Why the last run folded or not: "resolve_mrt" (a camera-gate run: the resolve wrote it as RT2, from any
    // current_depth format), "lane_mrt" (folded into the screen-gate chain's first mask draw), "r32f_depth" (an R32F
    // current depth needs no copy draw), "d24_decode" (the D24X8 snapshot is decoded), "far_off" (no far-program run),
    // "mrt_caps" (fewer than two targets or no MRTINDEPENDENTBITDEPTHS), "program" (the folding program was not
    // created), "not_run" (refused before the decision).
    const char* depth_fold_reason = "not_run";
    // History reconstruction of the program the last run drew (docs/architecture/taa-high-resolution.md S3): 5 (the
    // 5-tap bilinear Catmull-Rom programs, the only form), 0 when no resolve was drawn.
    unsigned history_taps = 0;
    // A' (FrameInputs::thin_region_camera_gate): the last run drew the folded hold resolve (no mask draw).
    bool region_hold = false;
    // FrameInputs::thin_vote: the last run cast the thin vote (the camera-gate resolve read it, or the screen-gate
    // chain drew the tests draw's thin-vote twin).
    bool thin_vote = false;
    // Why (or why not): "vote", "not_requested", "thin_region_off", "no_lane" (a camera-gate run on an R32F depth,
    // which has no .a), "no_fold" (screen-gate chain: no depth-folding tests draw: an R32F or D24X8 depth, no far run
    // or MRT caps; depth_fold_reason says which), "two_channel_depth" (a G32R32F lane has no .a), "no_twin_program"
    // (screen-gate chain: configure_thin_vote did not create it), "screen_source" (screen-gate chain,
    // FrameInputs::thin_region_source Screen), "not_run".
    const char* thin_vote_reason = "not_run";
    // FrameInputs::thin_region_source as drawn by the last run: Vote only when the vote was cast with c10.y = 1 (the
    // search skipped), Screen when a screen-gate Screen run drew the plain tests program with the thin region on, Both
    // otherwise (a Vote run without the vote, a run without the thin region).
    ThinRegionSource thin_region_source = ThinRegionSource::Both;
    // S4 (configure_box_resolution(2)): the last run drew the camera gate's box at half resolution.
    bool box_half = false;
    // Why (or why not): "half", "not_requested" (the full-resolution box is configured), "no_camera_gate" (no
    // camera-gate run), "odd_size" (an odd width or height: the resolve's point read of the half-resolution box needs
    // an even size; that run draws the full-resolution box), "target" (the half-resolution targets were refused, not a
    // lost device: full resolution until Reset re-arms them), "not_run".
    const char* box_resolution_reason = "not_run";
    // CPU-side QueryPerformanceCounter ticks of the last run's phases, taken
    // only with configure_timing(true); zero otherwise. Wall clock around the
    // device calls (submission cost, driver work, any blocking), never GPU
    // execution time. The five phases nest inside run and do not overlap.
    bool timed = false;
    std::uint64_t ticks_capture = 0;    // state block Capture plus the binding getters
    std::uint64_t ticks_copy_color = 0; // 8-bit color to FP16 scratch StretchRect, or the same-format staging copy in
                                        // draw mode (allocations included)
    std::uint64_t ticks_copy_depth = 0; // R32F StretchRect or G32R32F/A32B32G32R32F point-sampled .r copy to R32F
                                        // history
    std::uint64_t ticks_draw = 0;       // normalize, scene bracket, decoder/resolve/mask quads
    std::uint64_t ticks_apply = 0;      // binding restoration plus state block Apply
};
class TemporalPass {
public:
    TemporalPass() = default;
    ~TemporalPass();
    TemporalPass(const TemporalPass&) = delete;
    TemporalPass& operator=(const TemporalPass&) = delete;
    // Device is BORROWED, never AddRef'd. Bytecode consumed synchronously by D3D.
    // Caller serializes rendering/reset and invokes before_reset/shutdown before
    // native Reset or final device teardown. No wrapper refs, global maps or cycles.
    // `native_vtable`, when given, is the device's ORIGINAL method table: every
    // device call of the pass then goes through those slots instead of the
    // object's current vtable, so a caller that hooked the device (the proxy)
    // never observes the pass's own calls. Without it the object's vtable is
    // read at every call. `decoder` may be null; the D24X8 snapshot input is
    // then refused (the route supplies R32F depth and needs no decoder).
    // `sharpen` (ps_3_0 bytecode of src/temporal/taa_sharpen_ps.hlsl) may be
    // null; FrameInputs::sharpen > 0 is then refused. `copy` (the ps_3_0
    // identity program, src/temporal/hdr_writeback_ps.hlsl) may be null;
    // configure_copy(true) is then refused at run. Every quad the pass draws
    // binds the embedded vs_3_0 pass-through (quad_vertex_program.h) and its
    // declaration, both created here and surviving Reset.
    HRESULT initialize(IDirect3DDevice9* native_device, const DWORD* decoder, const DWORD* resolve,
                       void* const* native_vtable = nullptr, const DWORD* sharpen = nullptr,
                       const DWORD* copy = nullptr) noexcept;
    // Creates the embedded flicker-suppression variants (temporal_resolve_program.h);
    // they survive Reset like the other programs. Call once after initialize
    // when any of thin_clip / adaptive_weight / alpha_history will be used; the
    // default path never creates them. A failure leaves the pass usable
    // without the options. age_available() additionally needs two simultaneous
    // render targets and D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS (R32F beside
    // A16B16G16R16F), read from the device caps at initialize.
    HRESULT configure_flicker() noexcept;
    // Creates the far-stabiliser program (and the mask program); needs the age
    // caps. A failure leaves the pass usable without the option. A far run
    // draws the mask (line_mask_ps.hlsl into owned A8R8G8B8 targets of the
    // frame size, 4 bytes per pixel each, created on the first such run: two for
    // the screen-gate chain and the far stabiliser alone, one for a camera-gate
    // run) and binds it at s8 for the resolve. The camera-gate programs are
    // optional on top (camera_gate_available(), camera_programs_result()).
    // reference_program: fixtures only (an earlier build of resolve_far.hlsl for an identity comparison);
    // camera_program: fixtures only (the camera-gate resolve replaced, e.g. by its X3M_FOLD_TESTS_OUT twin that writes
    // the per-pixel tests as the colour); production passes neither.
    HRESULT configure_far(const DWORD* reference_program = nullptr, const DWORD* camera_program = nullptr) noexcept;
    bool far_available() const noexcept { return mrt_age_ && line_mask_ != nullptr && far_ != nullptr; }
    // S3 (docs/architecture/taa-high-resolution.md): the resolve reconstructs the history with the 5-tap bilinear
    // Catmull-Rom form, which samples the previous colour and reactive mask a second time at s11 / s12 with LINEAR min
    // / mag filtering. That needs linear filtering of A16B16G16R16F and R32F textures (TextureFilterCaps MINFLINEAR |
    // MAGFLINEAR and CheckDeviceFormat D3DUSAGE_QUERY_FILTER of both, read at initialize); a device without it refuses
    // initialize with D3DERR_NOTAVAILABLE (the 16-tap point programs were removed on 2026-09-25: no fallback program
    // set).
    bool bilinear_history_available() const noexcept { return bilinear_history_; }
    // Why the 5-tap programs are unusable (initialize refused): "ok", "not_initialized", "filter_caps"
    // (TextureFilterCaps), "adapter_query" (GetDirect3D / GetCreationParameters / GetDisplayMode failed), "fp16_filter"
    // or "r32f_filter" (the format query).
    const char* bilinear_history_reason() const noexcept { return bilinear_history_reason_; }
    // The same filter requirement as a standalone query, before any pass exists (MotionOutput decides at device attach
    // whether TAA, and with it the jitter, can run at all): GetDeviceCaps through `native_vtable` (the device's own
    // table when null), then the checks initialize runs. S_OK, or the refusal with *reason set as
    // bilinear_history_reason() would be (plus "device_caps" when GetDeviceCaps itself failed). Creates nothing and
    // keeps no reference.
    static HRESULT query_history_filtering(IDirect3DDevice9* device, void* const* native_vtable,
                                           const char** reason) noexcept;
    // The camera-gate programs configure_far creates on top (the folded hold resolve and its 49-tap box; the hold
    // resolve is 5-tap only, so none without bilinear_history_available()), a history drawn with 5 taps and three
    // simultaneous render targets (the resolve writes colour, age and depth: NumSimultaneousRTs >= 3, read at
    // initialize): the camera gate can run. Optional: a refusal leaves no camera-gate path (AGENTS.md "Shader slot
    // budget": no fallback program set) and the caller decides what runs instead. camera_programs_result() holds the
    // first failed creation (D3DERR_NOTAVAILABLE without the filter caps, S_OK when every program was created).
    bool camera_gate_available() const noexcept {
        return far_available() && far_camera_hold_ != nullptr && thin_box_hold_ != nullptr && render_targets_ >= 3;
    }
    HRESULT camera_programs_result() const noexcept { return camera_programs_result_; }
    // D3DCAPS9::NumSimultaneousRTs as initialize read it (the camera gate needs 3), for the caller's refusal row.
    unsigned simultaneous_render_targets() const noexcept { return render_targets_; }
    bool camera_gate_failed() const noexcept { return boxes_failed_; }
    HRESULT camera_gate_result() const noexcept { return boxes_result_; }
    // S4 (docs/architecture/taa-high-resolution.md S4; taa-plan-lifted-slot-cap.md step 2), a session setting: 1 (the
    // default) draws the camera gate's box at full resolution, every target bit for bit a pass without this call; 2
    // draws it at half resolution on every camera-gate run of an even size: the separable pair
    // thin_box_rows_half_ps.hlsl / thin_box_columns_half_ps.hlsl into row targets of W/2 x (H/2 + 1) and box targets of
    // W/2 x H/2 (A16B16G16R16F, default pool, allocated on the first such run, released with the histories and by a run
    // at the other resolution), whose box at each pixel contains the full-resolution 7x7 box (the 8x8 block window),
    // read by the resolve with its point sampler at the block texel. 2 creates the two programs (none in a session that
    // never asks); a refusal drops both, keeps 1 and returns the failure for the caller's one log row (no other
    // fallback program set). Anything else is E_INVALIDARG and changes nothing. A change keeps the history. Refused
    // half-resolution targets (the row pair or the box pair) that are not a lost device fall back to the
    // full-resolution box for the session (Diagnostics::box_resolution_reason "target"; re-armed by Reset).
    HRESULT configure_box_resolution(unsigned divisor) noexcept;
    unsigned box_resolution() const noexcept { return box_divisor_; }
    bool box_half_failed() const noexcept { return box_half_failed_; }
    HRESULT box_half_result() const noexcept { return box_half_result_; }
    // Thin vote (FrameInputs::thin_vote): the thin-vote twin of the screen-gate chain's depth-folding tests program,
    // created only on request (a session that never asks holds none; a failure leaves the plain tests draw). The
    // camera-gate resolve reads the vote itself (no program): thin_vote_available() is true wherever either path can
    // cast it.
    HRESULT configure_thin_vote() noexcept;
    bool thin_vote_available() const noexcept { return line_mask_depth_thin_ != nullptr || camera_gate_available(); }
    // D3DCAPS9::MaxPixelShader30InstructionSlots as initialize read it, for the caller's one log row (AGENTS.md "Shader
    // slot budget": the programs are created whatever the figure; a device that refuses one takes that program's
    // failure path).
    unsigned ps30_instruction_slots() const noexcept { return ps30_slots_; }
    // The owned A8R8G8B8 mask targets currently allocated (0..2): a camera-gate run keeps none (the mask fold), the
    // screen-gate chain and the far stabiliser alone two. Diagnostic (the caller logs it with its first completed run).
    unsigned line_mask_targets() const noexcept { return (line_masks_[0] ? 1u : 0u) + (line_masks_[1] ? 1u : 0u); }
    // The mask targets could not be created (not a lost device): the far
    // stabiliser and the thin region are off for the rest of the session, runs
    // that ask for them proceed without (history kept), and this holds the
    // HRESULT for the caller's one log line. before_reset re-arms one attempt
    // (a Reset frees video memory; one CreateTexture pair per Reset cannot
    // storm). The age pair a far run allocated before the fallback stays
    // until the next resize or Reset: dropping it would cut the history.
    bool line_masks_failed() const noexcept { return line_masks_failed_; }
    HRESULT line_masks_result() const noexcept { return line_masks_result_; }
    bool flicker_available() const noexcept { return thin_ != nullptr; }
    bool age_available() const noexcept { return flicker_available() && mrt_age_ && age_ != nullptr; }
    // The mask-snapshot program (resolve_snapshot.hlsl) is created by initialize
    // and optional: a refusal leaves every run without a mask policy working;
    // RequiredMask / SupplementalMaskWithDepthSentinel runs are then refused.
    bool snapshot_available() const noexcept { return snapshot_ != nullptr; }
    HRESULT snapshot_result() const noexcept { return snapshot_result_; }
    // How an 8-bit color_surface input reaches the FP16 scratch and how the
    // resolved history reaches it again: false (default) by format-converting
    // StretchRect both ways (the caller copies back); true by a same-format
    // StretchRect into an owned staging texture plus identity draws both ways
    // (Output::display_written). The route decides per device from its
    // attach-time round-trip self test (docs/architecture/platform-portability.md).
    void configure_copy(bool by_draw) noexcept { copy_by_draw_ = by_draw; }
    bool copy_by_draw() const noexcept { return copy_by_draw_; }
    HRESULT run(const FrameInputs&, Output*) noexcept;
    void invalidate() noexcept;
    // Reset protocol, mirroring MotionOutput: before_reset releases every
    // default-pool object (histories, masks, scratch, the cached state block)
    // and keeps the compiled shaders and device; run is refused until
    // after_reset reports a successful Reset, after which resources are
    // re-created lazily on the next run.
    void before_reset() noexcept;
    void after_reset(HRESULT result) noexcept;
    void shutdown() noexcept; // full teardown, including shaders
    Diagnostics diagnostics() const noexcept { return diagnostics_; }
    // Phase timing of run (Diagnostics::ticks_*): off by default; costs five
    // QPC pairs per run when on and changes no device call.
    void configure_timing(bool enabled) noexcept { timing_ = enabled; }
    // --gpu-sync-timing only (docs/architecture/engine-frame-time.md, "TAA stage cost"): the run's five sub-pass
    // boundaries (gpu_sync_timing::TaaCopy .. TaaDisplay) inside the caller's Taa pair; null (the default) is one
    // branch per boundary and no device call.
    void configure_sync_timing(gpu_sync_timing::Marks* marks) noexcept { sync_marks_ = marks; }

private:
    struct SavedState;
    template <class Fn> Fn call(unsigned slot) const noexcept {
        return reinterpret_cast<Fn>((vtable_ ? vtable_ : *reinterpret_cast<void* const* const*>(device_))[slot]);
    }
    HRESULT allocate(UINT width, UINT height, bool reactive, bool age) noexcept;
    HRESULT ensure_scratch() noexcept;
    HRESULT ensure_staging(D3DFORMAT format) noexcept;
    HRESULT ensure_block() noexcept;
    HRESULT normalize(UINT w, UINT h) noexcept;
    HRESULT quad(UINT w, UINT h) noexcept;
    void release_history() noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* vtable_ = nullptr;
    // One D3DSBT_ALL block per device generation: captured before and applied
    // after every run instead of being created per frame. Default-pool-like:
    // released before Reset and re-created lazily afterwards.
    IDirect3DStateBlock9* block_ = nullptr;
    IDirect3DPixelShader9 *decoder_ = nullptr, *resolve_ = nullptr, *snapshot_ = nullptr, *sharpen_ = nullptr,
                          *copy_ = nullptr;
    HRESULT snapshot_result_ = S_FALSE;
    IDirect3DPixelShader9 *thin_ = nullptr, *age_ = nullptr;
    bool bilinear_history_ = false;
    const char* bilinear_history_reason_ = "not_initialized";
    HRESULT query_bilinear_history(const D3DCAPS9& caps) noexcept;
    bool line_masks_failed_ = false;
    HRESULT line_masks_result_ = S_OK;
    IDirect3DPixelShader9* far_ = nullptr;
    IDirect3DPixelShader9* line_mask_ = nullptr;
    // Mask targets (A8R8G8B8, default pool, released with the histories; line_mask_ps.hlsl's modes decide the layout).
    // A camera-gate run needs none and releases both (a configuration change, never per frame).
    IDirect3DTexture9* line_masks_[2]{};
    IDirect3DSurface9* line_mask_surfaces_[2]{};
    HRESULT ensure_line_masks(bool both) noexcept;
    // Camera-relative gate (section 32.1) with A' and the mask fold: the hold resolve (resolve_far_camera_hold.hlsl)
    // and the region-gated 7x7 box pass (thin_box_hold_ps.hlsl) with its two A16B16G16R16F targets ([0] minimum, [1]
    // maximum; default pool, released with the histories, allocated on the first camera-gate run).
    IDirect3DPixelShader9 *far_camera_hold_ = nullptr, *thin_box_hold_ = nullptr;
    HRESULT camera_programs_result_ = S_OK;
    // S1 (docs/architecture/taa-high-resolution.md): line_mask_ built with X3M_MASK_DEPTH_OUT, bound only for the
    // screen-gate chain's first draw on a two- or four-channel current depth, which then writes depths_[next] as COLOR1
    // (R32F beside the A8R8G8B8 mask; mrt_age_ covers MRTINDEPENDENTBITDEPTHS). Optional: null keeps the copy draw.
    IDirect3DPixelShader9* line_mask_depth_ = nullptr;
    IDirect3DPixelShader9* line_mask_depth_thin_ = nullptr; // configure_thin_vote
    IDirect3DTexture9* boxes_[2]{};
    IDirect3DSurface9* box_surfaces_[2]{};
    bool boxes_failed_ = false;
    HRESULT boxes_result_ = S_OK;
    HRESULT ensure_boxes(bool half) noexcept;
    bool boxes_half_ = false; // the size boxes_ were created at (half: W/2 x H/2)
    // S4: the row targets of the half-resolution pair ([0] row minimum, [1] row maximum; A16B16G16R16F W/2 x (H/2 + 1),
    // default pool, released with the histories and by the first run without the half-resolution box).
    bool hold_history_ = false; // the current age target carries hold fractions (written by the hold program)
    unsigned ps30_slots_ = 0;
    IDirect3DTexture9* box_rows_[2]{};
    IDirect3DSurface9* box_row_surfaces_[2]{};
    HRESULT ensure_box_rows() noexcept;
    // S4: the half-resolution pair (configure_box_resolution(2)) and the configured divisor (2 only while both exist).
    IDirect3DPixelShader9 *thin_box_rows_half_ = nullptr, *thin_box_columns_half_ = nullptr;
    unsigned box_divisor_ = 1;
    bool box_half_failed_ = false;
    HRESULT box_half_result_ = S_OK;
    bool mrt_age_ = false;         // caps: >= 2 simultaneous RTs with independent bit depths
    IDirect3DTexture9* ages_[2]{}; // R32F per-pixel accumulated-frame count (age and far programs)
    IDirect3DSurface9* age_surfaces_[2]{};
    IDirect3DVertexShader9* quad_vs_ = nullptr; // vs_3_0 pass-through of every quad (survives Reset)
    IDirect3DVertexDeclaration9* quad_declaration_ = nullptr;
    IDirect3DTexture9* colors_[2]{};
    IDirect3DTexture9* depths_[2]{};
    IDirect3DTexture9* reactive_[2]{};
    IDirect3DTexture9* scratch_ = nullptr; // FP16 copy of an 8-bit color_surface input
    IDirect3DTexture9* staging_ = nullptr; // draw mode: same-format copy of the 8-bit input the identity draw samples
    IDirect3DSurface9* staging_surface_ = nullptr;
    D3DFORMAT staging_format_ = D3DFMT_UNKNOWN;
    IDirect3DSurface9* color_surfaces_[2]{};
    IDirect3DSurface9* depth_surfaces_[2]{};
    IDirect3DSurface9* reactive_surfaces_[2]{};
    IDirect3DSurface9* scratch_surface_ = nullptr;
    UINT width_ = 0, height_ = 0, render_targets_ = 0, streams_ = 0, current_ = 0;
    std::uint64_t generation_ = 0;
    x3::temporal::HistoryState history_{};
    ReactivePolicy reactive_policy_ = ReactivePolicy::Unavailable;
    Diagnostics diagnostics_{};
    bool timing_ = false;
    gpu_sync_timing::Marks* sync_marks_ = nullptr;
    bool copy_by_draw_ = false;
    bool quad_fvf_ = false; // fixture-only XYZRHW twin (X3M_QUAD_FVF_SWITCH builds); always false in production
};
} // namespace x3m::renderer
