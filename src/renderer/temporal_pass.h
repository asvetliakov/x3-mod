#pragma once
#include <d3d9.h>
#include <cstdint>
#include "../temporal/resolve.h"

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
enum class ReactivePolicy { Unavailable, KnownNonReactive, RequiredMask, DerivedFromDepthSentinel, SupplementalMaskWithDepthSentinel };
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
    IDirect3DTexture9* motion = nullptr; // RGBA32F if PerPixel; alpha ABI in temporal/README
    IDirect3DTexture9* reactive = nullptr; // RequiredMask: R32F; Supplemental: A16B16G16R16F raw red coverage
    UINT width = 0, height = 0;
    std::uint64_t epoch = 0; // stable camera/scene/resource regime, not frame/clear count
    float clip_to_previous[16]{}; // unjittered, row-major, column-vector multiplication
    // Raster pixels, positive Y down. The camera path subtracts the current
    // jitter before the inverse projection (the motion texture is rasterized on
    // the current jittered grid already, so the motion path uses it nowhere).
    // The previous jitter is uploaded for ABI stability only: the resolve never
    // adds it (history is on the unjittered grid; the producer's RG is already
    // the previous unjittered UV).
    float current_jitter[2]{}, previous_jitter[2]{};
    float weight = .9f;
    float rejection[4]{.0001f, .02f, 65000.f, .000001f}; // max(absolute, relative*depth) depth tolerance, HDR limit, minimum W
    // k of the resolve's reversible luminance weighting (resolve.h, c22.x):
    // 0 (the default, the 8-bit route) is the exact identity; the HDR route
    // uploads the exposure multiplier its write-back applies to the resolved
    // image, so the weighted domain is the display-relative luminance. Finite,
    // 0 <= k <= 65504; run refuses anything else.
    float luminance_k = 0.f;
    // A of the filtered current sample (resolve.h, c22.y; resolve_filter.hlsl):
    // 0 (the default) binds the plain resolve program and the run is
    // bit-identical to a run without the field; in (0, 4] the pass binds the
    // filtered variant, whose blend takes the exp(-A d^2) average of the 3x3
    // current samples instead of the point sample (the clip box is unchanged).
    // Requires a pass initialised with the filtered program; anything else,
    // or a value outside [0, 4], refuses the run.
    float current_filter = 0.f;
    // A of the line-masked filtered current sample (c22.w; resolve_line.hlsl,
    // resolve_thin_line.hlsl, resolve_age_line.hlsl; docs/architecture/
    // taa-lattice-crawl.md section 9): 0 (the default) selects the programs a
    // run without the field binds; in (0, 4] the same exp(-A d^2) average
    // enters the blend only where the current 3x3 depth holds a line-like
    // pixel (geometry with background on both sides along one of four
    // directions). Requires configure_line_filter() and current_filter == 0
    // (the global filter already covers every pixel); anything else refuses.
    float line_filter = 0.f;
    // Width of the line mask in pixels, 1 (default) or 2: with 2 a side of the
    // pixel also counts as background when the neighbour at distance 2 is
    // (replay: more of thick or distant lattices, about 4 % softer silhouettes
    // against 1 %). Anything else refuses a line-filtered run.
    unsigned line_width = 1;
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
    // adaptive_weight (one gate) and current_filter, and beside a line_filter
    // of a different A (one Gaussian per frame). farw = 0 pixels are the thin /
    // plain blend bit for bit.
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
    // shares its speed gate, and like it excludes adaptive_weight and
    // current_filter; with either of the two, thin_clip must be 0 (the program
    // has no 3x3 sentinel soft clip). Pixels outside the region are the far /
    // plain blend bit for bit.
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
    // the mask's fragmentation channel and follows the whole existing chain (11x11
    // grow, 17x17 speed gate, camera gate, 7x7 box clip); the resolve programs are
    // untouched. Unrouted sentinel pixels (lasers, engine glows, sky) are outside
    // the class and keep the sentinel law, as do non-finite taps (|L| > 65000 or
    // NaN), which can neither vote nor lower a neighbour's 3x3 minimum. E is in the
    // units of the scene this pass binds for the resolve: with an 8-bit color_surface
    // that is the display-referred copy, where nothing exceeds 1 and E >= 1 never
    // fires; the FP16 `color` input is the HDR scene the design's E = 1 assumes.
    // Finite and >= 0, read and validated only with thin_region_weight > 0; costs 9
    // scene taps in the mask's tests draw and no extra motion fetch (the speed gate's
    // sample is shared).
    float thin_region_emissive = 0.f;
    // Camera-relative gate mode of the thin region (taa-lattice-crawl.md section
    // 32.1), off by default (the screen-speed gate above, bit for bit). On: the
    // region's gate speed is min(screen speed, camera-relative speed), the
    // routed correspondence measured against the camera path at the pixel's
    // depth, so a coherent camera pan keeps the region open, and where the
    // camera term alone keeps it open the relaxed history is clipped to the 7x7
    // min / max box of the current colour (one extra MRT draw into two owned
    // FP16 targets). Pixels the screen-speed gate leaves open, and every pixel
    // at rest, are the screen-gate run's exactly. Needs camera_gate_available()
    // (configure_far() created the three programs) and line_filter == 0 (the
    // mask's r channel carries the second gate); anything else refuses the run.
    // Ignored without thin_region_weight. A failed box-target allocation that is
    // not a lost device falls back to the screen-speed gate for the session
    // (camera_gate_failed(); re-armed by Reset), the mask fallback's policy.
    // The box pair exists only while the gate runs: the first run without it
    // releases the pair (a configuration change, never per frame).
    bool thin_region_camera_gate = false;
    // Sentinel stabiliser (docs/architecture/temporal-integration.md "Distant
    // unrouted stations under a pan"), off by default (0: every target bit for
    // bit the camera-gate run's). S in (0, 1]: an UNROUTED pixel on the depth
    // sentinel (motion alpha exactly -1; distant stations the engine draws
    // blended without depth, sky) gets thin-region strength S * the 17x17
    // minimum of the camera openness through the camera mask, always with the
    // 7x7 box clip, never the unclipped history; the resolve program is
    // untouched. The box pass then covers most of the sky and runs in its
    // separable form (two draws, one more FP16 pair, same bytes as the 49-tap
    // program). sentinel_emitter = E: where the raw luma maximum of the 7x7
    // exceeds E and the pixel's own depth is the sentinel, the box is the
    // inner 3x3 (lasers, trails, suns stay within one pixel of the tight clip);
    // 0 = no bound. Both ignored unless the camera gate is requested, and the
    // emitter bound is neither read nor validated at S = 0. Needs
    // configure_sentinel() (sentinel_available()); a failed row-target allocation that is not a lost
    // device turns the stabiliser off for the session (sentinel_failed();
    // re-armed by Reset) and the camera gate carries on as without it.
    float sentinel_strength = 0.f, sentinel_emitter = 1.f;
    // Depth and translation term of the camera gate's camera path, c8 of the
    // camera mask program only (camera_reprojection.h camera_depth_parallax();
    // taa-lattice-crawl.md section 32.3). All zero = the far-plane path of
    // clip_to_previous; a non-finite value is uploaded as zero.
    float camera_depth_parallax[4]{};
    // The latch-free form (camera_lane_parallax(): (DX, DY, DW, 1)), c9 of the
    // same program: used where current_depth is A32B32G32R32F, per pixel from its
    // .b (clip w = view z); the pass binds that texture at s5 of the tests draw
    // and camera_depth_parallax is then not consulted (a valid depth without a
    // positive .b stays on the far-plane path). Ignored (c9 = 0, s5 unbound) for
    // any other depth input, w != 1 or a non-finite value: camera_depth_parallax
    // is the fallback for the frame.
    float camera_lane_parallax[4]{};
    // Speed gate of far_weight and of the thin region, px/frame: full below far_speed_lo, the base weight from far_speed_hi (0 <= lo < hi <= 64).
    float far_speed_lo = x3::temporal::kFarSpeedLo, far_speed_hi = x3::temporal::kFarSpeedHi;
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
    // configure_flicker() to have succeeded, else the run is refused.
    // thin_clip S in [0, 1]: where the current 3x3 depth mixes the sentinel
    // and geometry the history is pulled only (1 - S) of the way to the clip
    // box, fading out between 2 and 4 px/frame.
    float thin_clip = 0.f;
    // adaptive_weight WMAX (0 off; weight <= WMAX <= 0.99): the history weight
    // becomes min(n / (n + 1), wmax(speed)) with n the per-pixel age kept in
    // an owned R32F pair written as COLOR1; wmax falls from WMAX to `weight`
    // between adaptive_lo and adaptive_hi px/frame. Refused without thin_clip
    // > 0 (alone it dims thin lattices) and without age_available().
    float adaptive_weight = 0.f, adaptive_lo = x3::temporal::kAdaptiveLoDefault, adaptive_hi = x3::temporal::kAdaptiveHiDefault;
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
    // The age target written by this run (adaptive_weight > 0), else null;
    // same borrowing rules. For capture dumps only.
    IDirect3DTexture9* age = nullptr;
    // Line filter / far stabiliser runs: the owned A8R8G8B8 mask the resolve read at s8 (r filter weight, g far
    // history-weight gate); diagnostic, same borrowing rules. Keep last: run() fills the struct positionally.
    IDirect3DTexture9* stabiliser_mask = nullptr;
};
struct Diagnostics {
    HRESULT operation = S_OK, restoration = S_OK;
    bool history_valid = false;
    bool reset_pending = false; // before_reset seen, after_reset(SUCCEEDED) not yet
    std::uint64_t completed_frames = 0;
    // CPU-side QueryPerformanceCounter ticks of the last run's phases, taken
    // only with configure_timing(true); zero otherwise. Wall clock around the
    // device calls (submission cost, driver work, any blocking), never GPU
    // execution time. The five phases nest inside run and do not overlap.
    bool timed = false;
    std::uint64_t ticks_capture = 0;    // state block Capture plus the binding getters
    std::uint64_t ticks_copy_color = 0; // 8-bit color to FP16 scratch StretchRect, or the same-format staging copy in draw mode (allocations included)
    std::uint64_t ticks_copy_depth = 0; // R32F StretchRect or G32R32F/A32B32G32R32F point-sampled .r copy to R32F history
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
                       void* const* native_vtable = nullptr, const DWORD* sharpen = nullptr, const DWORD* copy = nullptr,
                       const DWORD* resolve_filtered = nullptr) noexcept;
    // `resolve_filtered` (ps_3_0 bytecode of src/temporal/resolve_filter.hlsl)
    // may be null; FrameInputs::current_filter > 0 is then refused. A device
    // that refuses the program at creation does not fail initialize: the
    // filter is unavailable and current_filter_result() holds the HRESULT
    // (S_FALSE when no program was supplied).
    // Creates the embedded flicker-suppression variants (temporal_resolve_program.h);
    // they survive Reset like the other programs. Call once after initialize
    // when any of thin_clip / adaptive_weight / alpha_history will be used; the
    // default path never creates them. A failure leaves the pass usable
    // without the options. age_available() additionally needs two simultaneous
    // render targets and D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS (R32F beside
    // A16B16G16R16F), read from the device caps at initialize.
    HRESULT configure_flicker() noexcept;
    // Creates the embedded line-filter variants (plain, thin clip and, with the
    // age caps, age weight). Call once after initialize when line_filter will
    // be used; the default path never creates them. A failure leaves the pass
    // usable without the option. A line-filtered run draws the line mask
    // (line_mask_ps.hlsl, two quads into two owned A8R8G8B8 targets of the
    // frame size, 8 bytes per pixel, created on the first such run) and binds
    // it at s8 for the resolve.
    HRESULT configure_line_filter() noexcept;
    bool line_filter_available() const noexcept { return line_mask_ != nullptr && line_ != nullptr && thin_line_ != nullptr; }
    bool age_line_available() const noexcept { return age_line_ != nullptr; }
    // Creates the far-stabiliser program (and the mask program); needs the age
    // caps. A failure leaves the pass usable without the option.
    // reference_program: fixtures only (an earlier build of resolve_far.hlsl for an identity comparison); production passes none.
    HRESULT configure_far(const DWORD* reference_program = nullptr) noexcept;
    bool far_available() const noexcept { return mrt_age_ && line_mask_ != nullptr && far_ != nullptr; }
    // The camera-gate programs (mask, resolve, box) configure_far creates on top; optional, a refusal leaves the screen-speed gate.
    bool camera_gate_available() const noexcept { return far_available() && line_mask_camera_ != nullptr && far_camera_ != nullptr && thin_box_ != nullptr; }
    bool camera_gate_failed() const noexcept { return boxes_failed_; }
    // The two separable box programs of the sentinel stabiliser, created only on request (a session that never turns
    // the stabiliser on never owns them); needs camera_gate_available(). A failure leaves the pass usable without it.
    HRESULT configure_sentinel() noexcept;
    bool sentinel_available() const noexcept { return camera_gate_available() && thin_box_rows_ != nullptr && thin_box_columns_ != nullptr; }
    bool sentinel_failed() const noexcept { return box_rows_failed_; }
    HRESULT sentinel_result() const noexcept { return box_rows_result_; }
    HRESULT camera_gate_result() const noexcept { return boxes_result_; }
    // The mask targets could not be created (not a lost device): the line
    // filter and the far stabiliser are off for the rest of the session, runs
    // that ask for them proceed without (history kept), and this holds the
    // HRESULT for the caller's one log line. before_reset re-arms one attempt
    // (a Reset frees video memory; one CreateTexture pair per Reset cannot
    // storm). The age pair a far run allocated before the fallback stays
    // until the next resize or Reset: dropping it would cut the history.
    bool line_masks_failed() const noexcept { return line_masks_failed_; }
    HRESULT line_masks_result() const noexcept { return line_masks_result_; }
    bool flicker_available() const noexcept { return thin_ != nullptr && (!resolve_filtered_ || thin_filtered_ != nullptr); }
    bool age_available() const noexcept { return flicker_available() && mrt_age_ && age_ != nullptr && (!resolve_filtered_ || age_filtered_ != nullptr); }
    // The mask-snapshot program (resolve_snapshot.hlsl) is created by initialize
    // and optional: a refusal leaves every run without a mask policy working;
    // RequiredMask / SupplementalMaskWithDepthSentinel runs are then refused.
    bool snapshot_available() const noexcept { return snapshot_ != nullptr; }
    HRESULT snapshot_result() const noexcept { return snapshot_result_; }
    bool current_filter_available() const noexcept { return resolve_filtered_ != nullptr; }
    HRESULT current_filter_result() const noexcept { return resolve_filtered_result_; }
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
private:
    struct SavedState;
    template<class Fn> Fn call(unsigned slot) const noexcept {
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
    IDirect3DPixelShader9 *decoder_ = nullptr, *resolve_ = nullptr, *snapshot_ = nullptr, *resolve_filtered_ = nullptr, *sharpen_ = nullptr, *copy_ = nullptr;
    HRESULT resolve_filtered_result_ = S_FALSE, snapshot_result_ = S_FALSE;
    IDirect3DPixelShader9 *thin_ = nullptr, *thin_filtered_ = nullptr, *age_ = nullptr, *age_filtered_ = nullptr;
    bool line_masks_failed_ = false;
    HRESULT line_masks_result_ = S_OK;
    IDirect3DPixelShader9* far_ = nullptr;
    IDirect3DPixelShader9 *line_mask_ = nullptr, *line_ = nullptr, *thin_line_ = nullptr, *age_line_ = nullptr;
    // Line mask targets (A8R8G8B8, default pool, released with the histories): [0] line-like, [1] its 3x3 maximum.
    IDirect3DTexture9* line_masks_[2]{};
    IDirect3DSurface9* line_mask_surfaces_[2]{};
    HRESULT ensure_line_masks() noexcept;
    // Camera-relative gate (section 32.1): its mask and resolve programs, the 7x7 box pass and its two A16B16G16R16F targets
    // ([0] minimum, [1] maximum; default pool, released with the histories, allocated on the first camera-gate run).
    IDirect3DPixelShader9 *line_mask_camera_ = nullptr, *far_camera_ = nullptr, *thin_box_ = nullptr;
    IDirect3DTexture9* boxes_[2]{};
    IDirect3DSurface9* box_surfaces_[2]{};
    bool boxes_failed_ = false;
    HRESULT boxes_result_ = S_OK;
    HRESULT ensure_boxes() noexcept;
    // Sentinel stabiliser: the separable box programs and the row targets ([0] row minimum + raw luma maximum, [1] row
    // maximum; A16B16G16R16F, default pool, released with the histories and by the first run without the stabiliser).
    IDirect3DPixelShader9 *thin_box_rows_ = nullptr, *thin_box_columns_ = nullptr;
    IDirect3DTexture9* box_rows_[2]{};
    IDirect3DSurface9* box_row_surfaces_[2]{};
    bool box_rows_failed_ = false;
    HRESULT box_rows_result_ = S_OK;
    HRESULT ensure_box_rows() noexcept;
    bool mrt_age_ = false; // caps: >= 2 simultaneous RTs with independent bit depths
    IDirect3DTexture9* ages_[2]{};          // R32F per-pixel accumulated-frame count (adaptive weight only)
    IDirect3DSurface9* age_surfaces_[2]{};
    IDirect3DVertexShader9* quad_vs_ = nullptr;          // vs_3_0 pass-through of every quad (survives Reset)
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
    bool copy_by_draw_ = false;
    bool quad_fvf_ = false; // fixture-only XYZRHW twin (X3M_QUAD_FVF_SWITCH builds); always false in production
};
} // namespace x3m::renderer
