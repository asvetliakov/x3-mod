#pragma once
// Live same-draw motion route (checkpoint B1). Opt-in with X3M_MOTION_OUTPUT=1.
// One instance per hooked device, owned by capture.cpp's Device and called only
// from its hooks under the capture mutex. Every device call made here goes
// through the NATIVE vtable slots supplied at attach, never the hooked table,
// so the shadow state and the scene selector observe application calls only.
//
// Responsibilities: shader-variant registry, application state shadow, motion
// (RT1, RGBA32F) and current-depth (RT2, R32F) render target ownership across
// Reset/release, one-time capability self-test, per-frame sentinel fill,
// per-draw gate evaluation with variant substitution and exact restoration,
// per-draw sub-pixel jitter (X3M_MOTION_JITTER=1), previous-row history, the
// cut detector, the live camera state read at the scene Clear (the far-plane
// reprojection of sentinel pixels, X3M_TAA_SENTINEL), the temporal resolve at
// the bloom copy (X3M_TAA=1, owning one TemporalPass per device), the FP16
// HDR scene redirect with its logical-binding shim and write-back policy
// (X3M_HDR=1, owning one HdrPass per device) and capture-frame diagnostics.
// See docs/architecture/live-motion-route.md (Implementation section),
// docs/architecture/temporal-integration.md (steps 1 and 3) and
// docs/architecture/hdr-scene-path.md ("Stage 1 implementation").
#include <d3d9.h>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>
#include "chase_camera.h"
#include "../renderer/scene_boundary.h"
#include "../renderer/motion_history.h"
#include "../renderer/motion_row_history.h"
#include "../renderer/camera_reprojection.h"
#include "../renderer/hdr_pass.h"
#include "../renderer/linear_material.h"
#include "linear_cutout.h"
#include "../renderer/linear_emission.h"
#include "../renderer/linear_emission_pass.h"
#include "../renderer/linear_distance_fade.h"
#include "fade_region.h"
namespace x3m::renderer { struct MotionOutputProfile; class TemporalPass; }
namespace x3m::telemetry { struct State; }
namespace x3m {
// Distinct clip-row constant windows the profile table names (c24-27 for the
// point-light programs, c0-3 for the light-free variants). The route's shadow
// captures every window; a draw reads the window of its VS row. The actual
// count is derived from the table at compile time in motion_output.cpp.
inline constexpr std::size_t motion_matrix_windows_max = 4;
// Render states the route reads per draw and shadows from the SetRenderState
// hook (X3M_STATE_SHADOW): the selector's z states, the gate-4 opaque-draw
// checks and the write masks saved around RT1/RT2 (motion_output.cpp lists them).
inline constexpr std::size_t motion_shadow_state_count = 32;
}

namespace x3m {
struct MotionDrawCall {
    bool indexed = false, user_memory = false;
    D3DPRIMITIVETYPE topology = D3DPT_TRIANGLELIST;
    UINT primitives = 0, first = 0;
    INT base_vertex = 0;
    UINT min_vertex = 0, vertex_count = 0;
    bool composition_permission = false; // Capture-owned scene/thread ticket; DIP only.
};
// The first gate that refused a draw; None means every gate passed. Numbers
// match the design document's gate list.
enum class MotionGate : unsigned { None = 0, Feature = 1, Scene = 2, Pair = 3, DrawState = 4, Scope = 5, History = 6 };
// Per-draw decision. Stack object; carries what after_draw must undo.
struct MotionRoute {
    MotionGate gate = MotionGate::Feature;
    bool routed = false, matched = false, scene = false;
    bool composition = false, submit = true, evaluated = false;
    HRESULT submission_error = D3DERR_INVALIDCALL;
    HRESULT preparation_error = S_OK; // First internal failure; never replaces the native draw result.
    renderer::LinearCompositionPolicy composition_policy = renderer::LinearCompositionPolicy::AdditiveEmission;
    bool cutout_candidate = false, cutout = false; // requested exact scene pair; admitted alpha-test arm
    bool cutout_test_known = false, cutout_color_known = false, cutout_alpha_known = false, cutout_z_known = false, cutout_zfunc_known = false;
    DWORD cutout_test = 0, cutout_color = 0, cutout_alpha = 0, cutout_z = 0, cutout_zfunc = 0;
    bool linear_material = false; // Combined color+motion pair actually bound.
    bool vs_set = false, ps_set = false, rt_set = false, write_set = false;
    bool vs_constants_set = false, ps_constants_set = false;
    bool depth = false, rt2_set = false, write2_set = false;   // RT2 bound for this draw (row has depth_output).
    bool jittered = false;                                     // Jittered rows written; restore after the draw.
    UINT jitter_register = 0;                                  // The VS row's clip-row window base.
    DWORD saved_write1 = 15, saved_write2 = 15;
    // Temporal/depth plus at most two scalar destinations and their sources.
    // Read-only snapshots have no attempted bit; all snapshots precede writes.
    DWORD saved_wrap[6]{};
    std::uint8_t wrap_index[6]{}, wrap_count = 0, wrap_attempted = 0;
    renderer::RigidDrawKey key{};
    std::uint64_t rows_hash = 0;
    std::uint64_t load_epoch = 0, registry_epoch = 0;
    std::uint64_t ticks = 0;  // CPU ticks of apply (before_draw) plus undo (after_draw); telemetry only.
    // Conservative screen rectangle of an admitted distance-fade draw
    // (docs/architecture/linear-distance-fade-region.md, step 1): derived
    // after admission, logged and counted; the bracket does not consume it yet.
    fade_region::Region fade_region{};
    bool fade_region_evaluated = false;
    // Rectangle area per mille of its denominator, exactly the f_permille the
    // fade_region line reports; only meaningful with fade_region_evaluated.
    unsigned fade_region_permille = 0;
};
// Why the temporal resolve did not run at this frame's bloom copy (X3M_TAA=1).
// None: it ran (see taa_result/taa_copy). NotReached: the selector never
// presented the AwaitCopy event (menu, rejected or unrecognized frame).
// CameraState: X3M_TAA_SENTINEL=2 (strict) and no far-plane transform this frame.
// Target: the engine scene-end hook fired while RT0 was not the latched main target.
// Msaa: the latched main target is multisampled (the route refused the frame:
// RT1/RT2 textures cannot share its sample count; motion_output_msaa_refused).
enum class TaaSkip : unsigned { None = 0, Disabled = 1, NotReached = 2, NoJitter = 3, NotFilled = 4,
                                Recording = 5, Queries = 6, Initialize = 7, Container = 8, CameraState = 9, Target = 10, Msaa = 11 };
// Where this frame's resolve ran: at the engine scene-end hook (X3M_SCENE_HOOK,
// before the compositing call 0x004c4750) or at the bloom copy (the StretchRect
// hook, the fallback when the engine hook is absent or did not fire in the
// scene phase). Logged as scene_end_source=none|hook|stretchrect.
enum class SceneEndSource : unsigned { None = 0, Hook = 1, StretchRect = 2 };
// Cross-check of the engine hook against the selector at Present
// (scene_end_check): Agree = the hook fired once in the Scene phase and the
// selector then reached the bloom copy with no scene draw in between; HookOnly
// = the hook fired in the Scene phase and no bloom copy followed (glow off);
// StretchOnly = the bloom copy came without a hook signal (hook not installed;
// a disagreement when it is); Disagree = a signal outside the Scene phase, more
// than one signal, a scene draw between the hook and the copy, or StretchOnly
// with the hook installed.
enum class SceneEndCheck : unsigned { None = 0, Agree = 1, HookOnly = 2, StretchOnly = 3, Disagree = 4 };
// Where this frame's FP16 redirect ended (hdr_frame end=...): at the engine
// scene-end hook, at the recognized bloom copy, before an application write
// into the main target's contents (StretchRect destination, ColorFill,
// UpdateSurface), at Present (the terminal fallback: every frame without a
// recognized scene end, e.g. glow off without the engine hook), after the
// latching Clear failed, or dropped without a write-back (Reset, release).
enum class HdrEnd : unsigned { None = 0, Hook = 1, BloomCopy = 2, ContentWrite = 3, Present = 4, ClearFailed = 5, Dropped = 6 };
struct MotionHdrCounters {
    bool redirected = false;     // the latching Clear bound the FP16 target as RT0
    std::uint32_t end = 0;       // HdrEnd
    std::uint32_t writebacks = 0, flushes = 0, suspended = 0, resumed = 0;
    std::uint32_t source = 0;    // renderer::HdrWritebackSource of the last write-back
    bool unwind = false;         // a write-back did not complete cleanly (ladder taken)
    const char* unwind_reason = "none";
    HRESULT unwind_draw = S_FALSE, unwind_restore = S_FALSE, unwind_stretch = S_FALSE, unwind_bind = S_FALSE;
    bool blocked = false, recheck_ran = false, recheck_passed = false; // blocked after an unwind; the recovery self test
    bool dirty_at_present = false; // content reached the target after the last write-back and Present ended the redirect
    bool refused_msaa = false;
    HRESULT target_create = S_FALSE, latch_bind = S_FALSE;
    std::uint64_t redirect_ticks = 0, writeback_ticks = 0, writeback_draw_ticks = 0, writeback_stretch_ticks = 0;
    std::uint64_t bind_ticks = 0, recheck_ticks = 0;
    // Stage 2: the last write-back's tonemap/fallback verdict and meter
    // result, the latch's meter readback and adaptation step, the phase
    // timings (meter chain inside the draw, readback lock at the latch).
    bool tonemap = false, fallback = false, stepped = false;
    HRESULT tonemap_draw = S_FALSE, meter = S_FALSE, readback = S_FALSE;
    std::uint64_t meter_ticks = 0, readback_ticks = 0;
    // Post-resolve sharpen: the last write-back's RCAS verdict and whether a
    // sharpened draw fell back to the unsharpened program this frame.
    bool sharpened = false, sharpen_fallback = false;
};
struct MotionTaaCounters {
    bool attempted = false;      // The main-target bloom copy was recognized this frame.
    bool resolved = false;       // run() and the copy-back both succeeded: the main target holds the resolved image.
    bool used_history = false;   // The resolve blended the previous frame (false on the first frame, cuts, Reset).
    std::uint32_t skip = 0;      // TaaSkip
    HRESULT result = S_FALSE, restore = S_OK, copy = S_FALSE;
    // Depth-sentinel policy the resolve ran with (renderer::SentinelDecision):
    // 2 reprojects sentinel pixels through the camera at the far plane.
    std::uint32_t camera_policy = 1, camera_reason = 1;
    bool camera_cut = false;     // rotation since the previous resolved frame exceeded X3M_CAMERA_CUT_DEG
    float camera_rotation_deg = 0;
    std::uint32_t source = 0;    // SceneEndSource of the attempt
    // Stage 3 of the HDR scene path: the resolve ran on the FP16 scene target
    // (in.color, no copy; its output is what the write-back samples) with k
    // of the luminance weighting (0 on the 8-bit path).
    bool hdr = false;
    float k = 0.f;
    // Post-resolve sharpen (X3M_TAA_SHARPEN): the display image of this frame
    // is RCAS of the resolved image (8-bit path: the pass drew it into the
    // main target instead of the copy-back; HDR path: the write-back's
    // sharpened program).
    bool sharpened = false;
};
struct MotionFrameCounters {
    std::uint32_t draws = 0, routed = 0, matched = 0, gates[7]{};
    std::uint32_t cutout_routed = 0, cutout_missed = 0;
    std::uint32_t depth_routed = 0, jittered = 0;
    std::uint32_t apply_failures = 0, restore_failures = 0;
    bool latched = false, filled = false;
    HRESULT fill_result = S_FALSE, fill_restore = S_OK;
    // Jitter used for this frame's scene draws (raster pixels, +X right, +Y
    // down) and the previous latched frame's. Neither is uploaded to the
    // variants (PS c216.zw stays zero: history rows are unjittered); the
    // resolve receives both through its frame inputs.
    bool jitter_active = false;
    std::uint32_t jitter_index = 0;
    float jitter[2]{}, jitter_previous[2]{};
    // Cut detector: median screen displacement of the matched draws' projected
    // origins against their previous rows, and the fraction of keyed routed
    // draws whose key the previous frame lacked; the resolve rejects history
    // for a frame whose verdict is set.
    bool cut = false;
    std::uint32_t displacement_samples = 0, keyed = 0, missing = 0;
    // Live camera reads at the latching Clear (background view) and at the
    // depth-only Clear that starts the scene phase (scene view; the one the
    // far-plane reprojection uses). Failure codes: camera_state::ReadFailure
    // and renderer::CameraFailure of the scene read.
    bool camera_scene_valid = false, camera_background_valid = false;
    std::uint32_t camera_reads = 0, camera_read_failure = 0, camera_failure = 0;
    float cut_median_px = 0, cut_missing_fraction = 0, cut_median_bound_px = 0, cut_missing_bound = 0;
    MotionTaaCounters taa;
    // Per-frame cost totals (docs/verification/telemetry.md, "Route and
    // boundary cost"). Counts are always kept; tick totals are CPU-side QPC
    // wall clock taken only with X3M_TELEMETRY=1 (zero otherwise), never GPU
    // time. set_rt counts every route-issued SetRenderTarget of the per-draw
    // apply/undo path and the lazy flush (the fill's and the resolve's own
    // SetRenderTarget calls are inside fill_ticks and the taa phases instead).
    std::uint32_t set_rt = 0, jitter_writes = 0, lazy_flushes = 0, readbacks = 0;
    std::uint64_t gate_ticks = 0, route_draw_ticks = 0, set_rt_ticks = 0, jitter_ticks = 0, fill_ticks = 0;
    std::uint64_t lazy_flush_ticks = 0, readback_ticks = 0;
    std::uint64_t taa_run_ticks = 0, taa_capture_ticks = 0, taa_copy_color_ticks = 0, taa_copy_depth_ticks = 0;
    std::uint64_t taa_draw_ticks = 0, taa_apply_ticks = 0, taa_copy_back_ticks = 0;
    // Render-state shadow (X3M_STATE_SHADOW): shadowed state queries of the
    // route (gate evaluation and the RT1/RT2 write-mask saves), how many the
    // shadow answered (hits), every native GetRenderState the route issued
    // (gets: shadow misses plus the fill's state save; with the shadow off
    // every query is a get) and shadow resynchronizations this frame (state
    // block Apply/EndStateBlock, Reset, a failed restoration); sb_resyncs is
    // the state-block share of them (an Apply can change any shadowed state,
    // so the full re-read is required: iteration 10's one resync per frame on
    // the latch-only transition screen is attributed through this field).
    std::uint32_t rs_queries = 0, rs_hits = 0, rs_gets = 0, rs_resyncs = 0, sb_resyncs = 0;
    // A failed application SetRenderState drops that one shadow entry. It is
    // not a resync (no re-read follows), so it is counted apart: a resync
    // must still show at least one shadow miss at the next query.
    std::uint32_t rs_invalidations = 0;
    // Engine scene-end hook (X3M_SCENE_HOOK): signals this frame, signals that
    // arrived outside the selector's Scene phase (with the selector state of
    // the last one), draws evaluated after a Scene-phase signal (compositing
    // must follow the hook: those draws neither route nor jitter), whether the
    // selector reached the bloom copy afterwards, and the verdict (SceneEndCheck).
    std::uint32_t hook_signals = 0, hook_outside_scene = 0, hook_state = 0, draws_after_hook = 0;
    bool hook_scene_end = false, bloom_copy_seen = false;
    std::uint32_t scene_end_check = 0;
    MotionHdrCounters hdr;
    std::uint32_t material_routed = 0, material_bump_routed = 0, material_refused = 0, material_bind_failures = 0;
    // Mip LOD bias of the routed draws (X3M_TAA_MIP_BIAS): SetSamplerState
    // calls the route issued to apply and to restore the bias this frame,
    // routed draws that had at least one biased stage, the stages biased at
    // least once (bit mask), native reads that filled the sampler shadow
    // (GetSamplerState; the level counts come from the SetTexture hook),
    // application writes of MIPMAPLODBIAS this frame (the game never issues
    // one: docs/reverse-engineering/sampler-states-and-mips.md) and failed
    // native calls of the apply/restore path.
    std::uint32_t mip_bias_sets = 0, mip_bias_restores = 0, mip_bias_draws = 0, mip_bias_stages = 0;
    std::uint32_t mip_bias_reads = 0, mip_bias_game_writes = 0, mip_bias_failures = 0;
};
// Halton(2,3) sample i (1-based) centred on zero, in raster pixels.
float motion_jitter_sample(unsigned index, unsigned axis) noexcept;

#ifdef X3M_MOTION_OUTPUT_FIXTURE
// Original synthetic fixture seam; absent from production builds. It replaces
// the game observers with a caller-supplied scope and lets the fixture's own
// shaders act as the selector's background family. Exported by capture.cpp.
struct MotionOutputFixtureScope {
    std::uint32_t known = 0;
    std::uint64_t load_epoch = 0, registry_epoch = 0, node_serial = 0, camera_serial = 0;
    std::uintptr_t node = 0, camera = 0, registry = 0, mesh = 0;
    std::uint32_t node_handle = 0, camera_handle = 0, model = 0, lod = 0;
};
struct MotionOutputFixtureConfig {
    std::uint32_t size = sizeof(MotionOutputFixtureConfig);
    std::uint64_t background_vs[3]{}, background_ps[3]{};
    MotionOutputFixtureScope scope{};
    std::uint32_t emission_scene_owner = 0;
    std::uint32_t force_taa_readback = 0; // Successful resolve output only; no per-draw capture.
    std::uint32_t observe_native_wrap = 0; // Native indexed-draw observation only.
};
// Device-owned last native indexed submission. Failure is diagnostic only and
// never changes source submission, route state, or the caller's WRAP values.
struct MotionOutputFixtureWrapSnapshot {
    std::uint64_t sequence = 0;
    HRESULT result = D3DERR_NOTFOUND;
    std::uint32_t valid = 0;
    DWORD values[16]{};
};
#endif

// Synchronous pre-compositor handoff after the normal AgX write-back. All
// pointers and the display reference are borrowed only until the callback
// returns. The caller must copy display and AddRef scene/main if retaining
// them, and separately qualify owner/thread/frame/Reset lifetime. This record
// does not authorize a later GPU write or register Reset-time revocation.
struct MotionHdrScene {
    IDirect3DDevice9* device;
    IDirect3DTexture9* scene;
    IDirect3DSurface9* main;
    std::uint64_t device_id, frame, generation;
    const renderer::HdrDisplaySnapshot& display;
};
using MotionHdrSceneCallback = void (*)(void* context, const MotionHdrScene& scene) noexcept;

class MotionOutput {
public:
    MotionOutput() noexcept;
    ~MotionOutput();
    MotionOutput(const MotionOutput&) = delete;
    MotionOutput& operator=(const MotionOutput&) = delete;

    // Lifecycle. attach runs the capability gate and the one-time mixed-format
    // MRT self test; on failure the route stays disabled for this device.
    // `stats` (may be null) receives the route's CPU telemetry metrics
    // (telemetry::Metric::Route*/Taa*) when telemetry is enabled.
    void attach(IDirect3DDevice9* device, void** native, std::uint64_t device_id,
                const D3DCAPS9& caps, bool requested, telemetry::State* stats = nullptr) noexcept;
    bool enabled() const noexcept { return enabled_; }
    // The capture owner must not apply a combined resource-reference heuristic
    // during child destruction or the temporal pass's reference-count probe.
    bool reference_accounting_busy() const noexcept { return releasing_ || taa_busy_ || composition_busy_ || (composition_ && composition_->reference_accounting_busy()); }
    // Current CPU-side admission only: the capture caller also qualifies the
    // owner/thread/frame/Reset ticket and actual post-compositor device state.
    // The redirect itself is Off after a successful handoff.
    bool bloom_boundary_available() const noexcept {
        return enabled_ && hdr_enabled_ && scene_open_ && !active_queries_ && !shadow_.recording
            && !reference_accounting_busy() && !motion_state_lost_ && !hdr_blocked_ && !counters_.hdr.unwind && !counters_.restore_failures;
    }
    // RT2 (R32F current depth) is produced on this device: three simultaneous
    // targets, R32F render-target support and the three-format self test.
    bool depth_enabled() const noexcept { return depth_enabled_; }
    // Per-draw jitter (X3M_MOTION_JITTER=1) with a Halton(2,3) sequence of
    // `samples` entries advanced at each latching Clear; effective from the
    // next latch. Default off. Bounds of the cut detector: the median bound
    // is stated at 1280 px width and scaled by width/1280 at run time.
    void configure_jitter(bool enabled, unsigned samples) noexcept;
    void configure_cut_bounds(float median_px_at_1280, float missing_fraction) noexcept;
    // Temporal resolve at the bloom copy (X3M_TAA=1; requires the route, RT2
    // and jitter). `debug` writes the resolved FP16 image and the pre-resolve
    // 8-bit color in capture frames (X3M_TAA_DEBUG). Effective at attach.
    void configure_taa(bool requested, bool debug) noexcept;
    bool taa_enabled() const noexcept { return taa_enabled_; }
    // How the resolve moves the 8-bit main target to FP16 and back: false, by
    // format-converting StretchRect (the adapter granted the conversion and the
    // attach-time 4x4 round trip reproduced the bytes); true, by same-format
    // copies and identity draws (TemporalPass::configure_copy). Logged as
    // taa_copy=stretch|draw in motion_output_device.
    bool taa_copy_by_draw() const noexcept { return taa_copy_draw_; }
    // Depth-sentinel policy of the resolve (X3M_TAA_SENTINEL: auto | 1 | 2),
    // the camera rotation bound that declares a cut (X3M_CAMERA_CUT_DEG) and
    // the cadence of the camera_state line (X3M_CAMERA_LOG frames; capture
    // frames always log). Effective immediately.
    void configure_sentinel(renderer::SentinelMode mode, float cut_degrees, unsigned log_frames) noexcept;
    // RT1/RT2 binding policy (X3M_MOTION_RT_MODE). perdraw (default): each
    // routed draw binds RT1/RT2 and COLORWRITEENABLE1/2 and after_draw puts
    // the application's values back. lazy (experiment): the bindings stay
    // across consecutive routed draws and restore_bindings() puts them back
    // before any application call that could observe or depend on them
    // (capture.cpp calls it from those hooks; before_draw calls it for every
    // draw that does not route). Effective at attach; equivalence is proven
    // by the motion-output fixture's burst cases.
    void configure_rt_mode(bool lazy) noexcept { lazy_mode_ = lazy; }
    bool lazy_rt_mode() const noexcept { return lazy_mode_; }
    void restore_bindings() noexcept;
    // Render-state shadow (X3M_STATE_SHADOW, default on): the SetRenderState
    // hook feeds set_render_state and the route answers its per-draw state
    // queries from the shadow instead of GetRenderState. The shadow starts
    // unknown, fills lazily (one native read per state), is dropped by every
    // shadow resynchronization (state block Apply/EndStateBlock, Reset) and
    // by any failed restoration of the route's own state changes. Off: every
    // query is a native GetRenderState (the previous behaviour, A/B).
    void configure_state_shadow(bool enabled) noexcept { state_shadow_ = enabled; }
    bool state_shadow() const noexcept { return state_shadow_; }
    // BEFORE the application's SetRenderState (light hook: no logging, no
    // telemetry record): in lazy mode an application write to a write mask the
    // route holds first puts the application's bindings back, so the write
    // lands where the application expects it (closes the lazy-mode hole).
    void before_set_render_state(D3DRENDERSTATETYPE state) noexcept;
    // After a successful application SetRenderState; ignored while recording.
    void set_render_state(D3DRENDERSTATETYPE state, DWORD value) noexcept;
    void render_state_failed(D3DRENDERSTATETYPE state) noexcept;
    // Engine scene-end hook (X3M_SCENE_HOOK): `installed` records whether the
    // callsite patch is live (for the cross-check verdict); scene_end_hook is
    // the trampoline's signal, called before the engine's compositing call on
    // the render thread, outside any device hook. In the Scene phase it ends
    // routing/jitter for the frame, finishes the cut verdict and, with TAA on,
    // runs the temporal resolve on the bound RT0 (which must be the latched
    // main target); the StretchRect path then skips this frame's resolve.
    void configure_scene_hook(bool installed) noexcept { scene_hook_installed_ = installed; }
    // Optional callback is a retain/copy-only handoff before redirect cleanup;
    // it must not throw, reenter MotionOutput, Reset/destroy/invalidate resources
    // or perform GPU preparation. Preparation starts after this method returns.
    // No callback on an ineligible boundary, failed/skipped TAA, or unclean AgX
    // write-back. With TAA off, a clean unresolved FP16 scene may be handed off.
    // Null callback preserves the existing path without new COM/snapshot work.
    void scene_end_hook(MotionHdrSceneCallback callback = nullptr, void* context = nullptr) noexcept;
    // FP16 HDR scene path, stage 1 (X3M_HDR=1; requires the route). Effective
    // at attach: the pass runs its capability gate and four-format self test
    // there. The redirect binds the owned A16B16G16R16F target as RT0 at the
    // latching Clear and writes it back into the application's main target at
    // the scene end (hook, bloom copy) or, failing those, flushes at EndScene
    // and ends at Present. Every consumer of the proxy keeps seeing the
    // application's logical RT0 through the methods below.
    void configure_hdr(bool requested, const renderer::HdrConfig& config) noexcept { hdr_requested_ = requested; hdr_config_ = config; }
    bool hdr_enabled() const noexcept { return hdr_enabled_; }
    // Stage 2: the adapted exposure multiplier as the TAA luminance weighting
    // k (exported for stage 3; nothing consumes it yet; 0 without the meter).
    float hdr_taa_k() const noexcept { return hdr_taa_k_; }
    // X3M_TAA_K: a fixed k for the resolve's luminance weighting on the HDR
    // path (>= 0; 0 is the unweighted resolve); negative selects the derived
    // value (the write-back's exposure multiplier, see the latch).
    void configure_taa_k(float k) noexcept { taa_k_override_ = k; }
    // X3M_TAA_MIP_BIAS=<float> (0: off, the default; -0.5 intended): the
    // D3DSAMP_MIPMAPLODBIAS the route applies while the jitter is active to
    // every sampler stage a routed draw samples a mip chain through (texture
    // with more than one level and MIPFILTER other than NONE), set once per
    // stage when a routed draw first needs it and put back to the value read
    // at that time before any draw that does not route, at the scene end, at
    // Present, before Reset and before every application call that could
    // observe the device's sampler state (the restore points of
    // restore_bindings). The application's SetTexture and SetSamplerState
    // reach set_texture / set_sampler_state (light hooks; the sampler-state
    // hook also serves linear material admission) and feed the sampler shadow, so
    // no GetSamplerState runs per draw; an application write of the bias
    // itself is counted and logged and its value becomes the restore value.
    // docs/architecture/temporal-integration.md, "Mip LOD bias".
    // Opt-in combined reviewed DEFAULT material+motion variants, configured before attach.
    // Gains are shader-local DEFs; no application constants are modified.
    void configure_linear_materials(bool requested, const renderer::LinearMaterialConfig& config) noexcept;
    bool linear_materials_requested() const noexcept { return linear_material_requested_; }
    // Opt-in original-draw emission composition. Configure before
    // attach; gain is immutable thereafter. Variants always include coverage.
    void configure_linear_emissions(bool requested, float gain) noexcept;
    bool linear_emissions_requested() const noexcept { return linear_emission_requested_; }
    void configure_linear_distance_fade(bool requested) noexcept;
    // Diagnostic fade-region witness (X3M_FADE_WITNESS=<k>, note section 7,
    // step 1): every k-th frame without an admitted emission draw the M
    // coverage target is read back once and its covered pixels counted
    // against the union of that frame's derived rectangles. Off (0) costs
    // nothing per draw or per frame.
    void configure_fade_witness(unsigned frames) noexcept;
    // Diagnostic distant-shimmer trace (X3M_SHIMMER_TRACE=1, off by default;
    // docs/architecture/linear-distance-fade-region.md, "Shimmer trace"):
    // every frame records the identity of the Asteroid-class scene draws into
    // a fixed per-frame array and logs them plus the frame's TAA state after
    // Present. Off costs one predicate per draw and nothing else.
    void configure_shimmer_trace(bool requested) noexcept;
    bool composition_requested() const noexcept { return linear_emission_requested_ || distance_fade_requested_; }
    bool composition_operation_active() const noexcept { return composition_busy_; }
    bool draw_submission_blocked() const noexcept { return composition_busy_ || composition_state_lost_ || motion_state_lost_; }
    void configure_mip_bias(float bias) noexcept;
    float mip_bias() const noexcept { return mip_bias_; }
    bool mip_bias_active() const noexcept { return mip_bias_bits_ != 0 && jitter_requested_; }
    // After a successful application SetTexture / SetSamplerState (light
    // hooks). `levels` is the texture's level count when `queried` (the hook
    // asks the texture once per pointer change, inside its native section).
    void set_texture(DWORD stage, IDirect3DBaseTexture9* texture, DWORD levels, bool queried, int reader = 2) noexcept;
    int composition_texture_reader(DWORD stage, IDirect3DBaseTexture9* texture) noexcept; // native CPU section
    void before_texture_write(IDirect3DBaseTexture9* texture) noexcept;
    bool texture_levels_wanted(DWORD stage, IDirect3DBaseTexture9* texture) const noexcept;
    void set_sampler_state(DWORD stage, D3DSAMPLERSTATETYPE type, DWORD value) noexcept;
    void before_set_sampler_state(DWORD stage, D3DSAMPLERSTATETYPE type) noexcept;
    void sampler_state_failed(DWORD stage, D3DSAMPLERSTATETYPE type) noexcept;
    // X3M_TAA_SHARPEN in [0, 1]: post-resolve RCAS of the display image
    // (docs/architecture/temporal-integration.md "Post-resolve sharpen"); 0
    // (default) leaves both routes bit-identical to the unsharpened ones. On
    // the 8-bit route the pass draws it in place of the copy-back; on the HDR
    // route the write-back's sharpened program draws it (configure_hdr's
    // HdrConfig::sharpen carries the same value to the pass).
    void configure_taa_sharpen(float sharpness) noexcept { taa_sharpen_ = sharpness; }
    bool hdr_redirected() const noexcept { return hdr_state_ != HdrState::Off; }
    // BEFORE the application's SetRenderTarget: the surface to bind natively.
    // Index 0 while redirected: the application's main surface maps to the FP16
    // target (re-entry after an excursion), any other surface is forwarded
    // verbatim after the pending content is written back (the redirect is
    // suspended until the main surface is bound again). Other indices and the
    // non-redirected state return `surface`.
    IDirect3DSurface9* before_set_render_target(DWORD index, IDirect3DSurface9* surface) noexcept;
    // The application's GetRenderTarget(0) while redirected: *out receives the
    // logical main surface (one added reference) and true is returned; false
    // means forward the call natively.
    bool hdr_logical_render_target(DWORD index, IDirect3DSurface9** out) noexcept;
    // BEFORE an application read of a surface's contents (GetRenderTargetData
    // source, a StretchRect source other than the bloom copy): the pending
    // FP16 content is written back into the main target first (a flush; the
    // redirect continues). BEFORE an application write into a surface's
    // contents (StretchRect destination, ColorFill, UpdateSurface): the
    // redirect ends first so the write is not overwritten later.
    void before_render_target_read(IDirect3DSurface9* surface) noexcept;
    void before_render_target_write(IDirect3DSurface9* surface) noexcept;
    // BEFORE the application's EndScene: the FP16 content is flushed into the
    // main target while draws are still legal (the redirect stays on for a
    // possible BeginScene that follows; Present ends it).
    void before_end_scene() noexcept;
    // Cadence of the periodic motion_output_frame line with telemetry on
    // (every `frames` frames; default 60; capture frames always log).
    void configure_frame_log(unsigned frames) noexcept { frame_log_interval_ = frames ? frames : 60u; }
    // Device references held by owned objects (variants, sentinel shader,
    // motion target surface), one per object in every reference model the
    // route runs under (native D3D9 and the ownership wrapper; see
    // ensure_target). The release hook releases them before the
    // application's final Release.
    unsigned device_references() const noexcept;
    void release_resources() noexcept;
    void before_reset() noexcept;
    void after_reset(HRESULT result) noexcept;

    // Frame boundaries, called from the Present hook.
    void begin_frame(std::uint64_t frame, bool capture) noexcept;
    void before_present() noexcept;
    void after_present(HRESULT result) noexcept;

    struct ComparisonExposure {
        bool ready = false, automatic = false, frame_used = false;
        float ev = 0.f;
        const char* reason = "hdr_unavailable";
    };
    ComparisonExposure comparison_exposure() const noexcept;
    bool comparison_toggle_exposure() noexcept;
    // Late notice/controls admission. Present still follows a refused notice.
    bool comparison_boundary_available() const noexcept {
        return enabled_ && !scene_open_ && !active_queries_ && !shadow_.recording
            && !reference_accounting_busy() && !draw_submission_blocked()
            && hdr_state_ == HdrState::Off && !hdr_blocked_;
    }
    void comparison_state_failed(HRESULT result) noexcept;

    // Variant registry: called after a successful native create with the hash
    // the capture already computed. Pointer reuse replaces the old entry.
    void register_vertex_shader(IDirect3DVertexShader9* shader, const DWORD* code,
                                std::size_t bytes, std::uint64_t hash) noexcept;
    void register_pixel_shader(IDirect3DPixelShader9* shader, const DWORD* code,
                               std::size_t bytes, std::uint64_t hash) noexcept;

    // Shadow updates from successful application setter calls. Ignored while a
    // state block is recording, because recorded calls do not reach the device.
    void set_vertex_shader(IDirect3DVertexShader9* shader) noexcept;
    void set_pixel_shader(IDirect3DPixelShader9* shader) noexcept;
    void set_vertex_constants_f(UINT start, const float* data, UINT count) noexcept;
    void set_vertex_constants_i(UINT start, const int* data, UINT count) noexcept;
    void set_pixel_constants_f(UINT start, const float* data, UINT count) noexcept;
    void set_stream_source(UINT stream, IDirect3DVertexBuffer9* buffer, UINT offset, UINT stride) noexcept;
    void set_indices(IDirect3DIndexBuffer9* buffer) noexcept;
    void set_vertex_declaration(IDirect3DVertexDeclaration9* declaration) noexcept;
    void set_fvf(DWORD fvf) noexcept;
    void set_viewport(const D3DVIEWPORT9* viewport) noexcept;
    void begin_stateblock() noexcept;
    void end_stateblock() noexcept;
    // A state block Apply changes device state outside the setter hooks:
    // resynchronize the whole shadow from the public getters (once per Apply).
    void stateblock_applied() noexcept;

    // Scene-boundary events, after the application call completed. The
    // SetRenderTarget/depth events also refresh the binding shadow.
    void before_clear(DWORD count, DWORD flags, float z) noexcept;
    void after_clear(HRESULT result) noexcept;
    void after_set_render_target(DWORD index, IDirect3DSurface9* surface, HRESULT result) noexcept;
    void after_set_depth(IDirect3DSurface9* surface, HRESULT result) noexcept;
    // BEFORE the application's StretchRect: when the selector is in AwaitCopy
    // and this is the main-target bloom copy it waits for, the temporal
    // resolve runs and its output is copied into the main target first, so the
    // application copies (and later presents) the resolved image. Every device
    // call goes through the native slots; the main target is written only
    // after a completely successful run. Nothing happens otherwise.
    void before_stretch(IDirect3DSurface9* source, const RECT* source_rect,
                        IDirect3DSurface9* destination, const RECT* destination_rect) noexcept;
    void after_stretch(IDirect3DSurface9* source, const RECT* source_rect,
                       IDirect3DSurface9* destination, const RECT* destination_rect, HRESULT result) noexcept;
    // Scene and query tracking for the resolve's caller contract: the pass
    // borrows an open scene and refuses to draw while an application query is
    // between Issue(BEGIN) and Issue(END). Called after the application call.
    void after_begin_scene(HRESULT result) noexcept;
    void after_end_scene(HRESULT result) noexcept;
    void query_active(bool active) noexcept;
    void after_color_fill(IDirect3DSurface9* destination, const RECT* rect, HRESULT result) noexcept;
    void unsupported(HRESULT result) noexcept;

    // Per-draw route. before_draw performs the pending sentinel fill, evaluates
    // the gates cheapest first and, when routed, substitutes the variant pair,
    // reserved constants and RT1. after_draw restores exactly what was set,
    // feeds the selector and writes capture diagnostics.
    MotionRoute before_draw(const MotionDrawCall& call) noexcept;
    void after_draw(MotionRoute& route, HRESULT result) noexcept;
    const MotionFrameCounters& counters() const noexcept { return counters_; }

#ifdef X3M_MOTION_OUTPUT_FIXTURE
    void fixture_configure(const MotionOutputFixtureConfig& config) noexcept;
    // target 1: the RGBA32F motion target (4 floats per pixel); 2: the R32F
    // depth target (1 float per pixel).
    HRESULT fixture_readback(unsigned target, float* out, std::size_t floats, UINT* width, UINT* height) noexcept;
    // The PS c216-217 values the last routed draw uploaded (eight floats).
    HRESULT fixture_last_pixel_abi(float* out, std::size_t floats) const noexcept;
    // HDR fault injection (renderer::HdrFault kinds; before attach the fault
    // is queued for the pass) and the FP16 target as floats (4 per pixel).
    bool fixture_emission_owner() const noexcept { return fixture_configured_ && fixture_.emission_scene_owner; }
    void fixture_emission_fault(unsigned kind, unsigned count) noexcept;
    HRESULT fixture_setter_result(HRESULT result, unsigned slot, unsigned selector) noexcept;
    unsigned fixture_emission_status(unsigned key) const noexcept;
    void fixture_hdr_fault(unsigned kind, unsigned count) noexcept;
    HRESULT fixture_hdr_readback(float* out, std::size_t floats, UINT* width, UINT* height) noexcept;
    // Stage 2 exposure state: ev (consumed), ev_adapted, ev_target,
    // avg_log_l, dt, exposure, steps, k (eight floats); with 16 floats also
    // lit_fraction, lit_median_log, p99_max_log, ev_key, ev_limit, tiles,
    // lit, lit_mean_log (the space-aware statistic); with 18 also ev_fresh
    // (the target before the dead band) and the lit tiles' total weight.
    HRESULT fixture_hdr_exposure(float* out, std::size_t floats) const noexcept;
#endif

private:
    // A program's profile row (first row of a supported class hosting it) is
    // recorded once at registration. SetShader caches both owned variants;
    // per-draw checks use the original pair and cached state only. The material
    // object never replaces the ordinary motion fallback for shared stages.
    struct ShaderEntry { std::uint64_t hash = 0; IUnknown* variant = nullptr;
                         IUnknown* material_variant = nullptr;
                         // XT DEFAULT is pair-specific: the shared VS retains
                         // its generic objects for every earlier exact pair.
                         IUnknown* xt_default_ordinary_variant = nullptr;
                         IDirect3DVertexShader9* xt_default_linear_variant = nullptr;
                         IDirect3DPixelShader9* emission_variant = nullptr;
                         IUnknown* distance_fade_variant = nullptr;
                         bool registered = false; // Valid original, independent of motion support.
                         const renderer::MotionOutputProfile* row = nullptr; };
    struct Shadow {
        IDirect3DVertexShader9* vs = nullptr;
        IDirect3DPixelShader9* ps = nullptr;
        std::uint64_t vs_hash = 0, ps_hash = 0;
        bool vs_registered = false, ps_registered = false;
        IDirect3DPixelShader9* ps_emission_variant = nullptr;
        IDirect3DVertexShader9* vs_fade_variant = nullptr;
        IDirect3DPixelShader9* ps_fade_variant = nullptr;
        std::uint32_t fade_sampler_mask = 0; // exact six-pair contract, independent of creation readiness
        bool emission_pair = false;
        // Only the original exact pair and created three-output PS qualify.
        // This is shader eligibility, not scene/blend/reader/pass admission.
        IDirect3DPixelShader9* emission_eligible_variant = nullptr;
        // Exact pair contract, refreshed at actual shader setters and completed
        // registration only. Technique is explicit: Asteroid BUMP and hull
        // DEFAULT both use mask 0x0f. The complete cached contract also
        // carries scalar WRAP relocations; unknown clears every field.
        renderer::LinearMaterialPairContract material_contract{};
        IDirect3DVertexShader9* vs_variant = nullptr;
        IDirect3DPixelShader9* ps_variant = nullptr;
        IDirect3DVertexShader9* vs_material_variant = nullptr;
        IDirect3DPixelShader9* ps_material_variant = nullptr;
        IDirect3DVertexShader9* vs_xt_default_ordinary = nullptr;
        IDirect3DVertexShader9* vs_xt_default_linear = nullptr;
        IDirect3DPixelShader9* ps_xt_default_ordinary = nullptr;
        // Published only at registration/SetShader; draws neither look up nor
        // validate the four objects. An incomplete pair stays native-forward.
        bool xt_default_pair = false, xt_default_ready = false;
        bool cutout_pair = false; // identity independent of variant/capability availability
        // Asteroid-family pair identity for the shimmer trace only (the six
        // distance-fade pairs of the material tables). Refreshed with the
        // other pair identities, never at a draw, and only while the trace is on.
        bool asteroid_pair = false;
        const renderer::MotionOutputProfile* vs_row = nullptr;
        float rows[motion_matrix_windows_max][16]{}; // Each window's four rows as submitted
        bool rows_known[motion_matrix_windows_max]{};
        float vs_reserved[16]{};      // application c252-255, restored only if written
        bool vs_reserved_written = false;
        float ps_reserved[8]{};       // application c216-217
        bool ps_reserved_written = false;
        int integer0[4]{};
        bool integer0_known = false;
        std::uint64_t stream0 = 0, indices = 0, declaration = 0;
        UINT stream0_offset = 0, stream0_stride = 0;
        // The application buffer identities of SetStreamSource/SetIndices (the
        // public ownership wrappers the capture hooks observe; never
        // dereferenced here): the fade bound table proves a subset record
        // holds exactly these.
        std::uintptr_t stream0_identity = 0, indices_identity = 0;
        DWORD fill_mode = 0;          // D3DRS_FILLMODE, kept only with composition requested
        bool fill_mode_known = false;
        std::uint32_t position_offset = 0, position_type = 0;
        renderer::Surface rt0, depth;
        bool extra_rt[4]{};
        renderer::Viewport viewport;
        bool recording = false;
        DWORD states[motion_shadow_state_count]{};      // application render states (shadow_states order)
        bool states_known[motion_shadow_state_count]{};
        // SRCBLEND, DESTBLEND, BLENDOP, SEPARATEALPHABLENDENABLE. The first
        // three are the nine-state fade check's blend triple; the fourth is
        // logged by the capture-only motion_route line and is not part of it.
        DWORD composition_blend[4]{};
        bool composition_blend_known[4]{};
    };
    struct SavedState;
    template<typename Fn> Fn native(unsigned slot) const noexcept { return reinterpret_cast<Fn>(native_[slot]); }
    bool self_test(bool with_depth, char* reason, std::size_t reason_size) noexcept;
    // The 4x4 StretchRect round trip of one 8-bit format through FP16 and
    // back (D1 of the native-Windows audit): exact 8-bit bytes, FP16 within
    // 1/1024 of v/255. Decides taa_copy at attach; never a TAA refusal.
    bool stretch_round_trip(D3DFORMAT format, char* detail, std::size_t detail_size) noexcept;
    HRESULT bind_quad_program() noexcept;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    bool fixture_stretch_fault() const noexcept { return fixture_stretch_fault_; }
#else
    static constexpr bool fixture_stretch_fault() noexcept { return false; }
#endif
    HRESULT draw_quad(IDirect3DSurface9* rt0, IDirect3DSurface9* rt1, IDirect3DSurface9* rt2,
                      IDirect3DPixelShader9* shader, UINT width, UINT height, HRESULT* restore) noexcept;
    HRESULT readback_surface(IDirect3DSurface9* surface, D3DFORMAT format, unsigned bytes_per_pixel,
                             const wchar_t* prefix, const wchar_t* extension, const char* tag, const char* format_name,
                             UINT width, UINT height) noexcept;
    void apply_jitter(MotionRoute& route) noexcept;
    void restore_jitter(MotionRoute& route) noexcept;
    void evaluate_draw(const MotionDrawCall& call, MotionRoute& route) noexcept;
    void refresh_linear_material_contract() noexcept;
    void probe_cutout_caps(bool force = false) noexcept;
    bool cutout_arm_configured() const noexcept;
    void release_mip_bias_retry_bound() noexcept;
    bool cutout_draw_state() noexcept;
    void mark_cutout_candidate(MotionRoute& route) noexcept;
    void report_xt_default_unavailable() noexcept;
    void report_mip_bias_game_write_failure() noexcept;
    void refresh_linear_emission_contract() noexcept;
    void prepare_composition(const MotionDrawCall&, MotionRoute&) noexcept;
    void derive_fade_region(MotionRoute&) noexcept;
    // Step-1 rectangle of the bound draw (resolve, rows, jitter, viewport,
    // fill mode, clip to the owning target); the counters, witness and log
    // stay in derive_fade_region. Shared by the admitted route and the
    // capture-only refused-draw diagnostic.
    fade_region::Region fade_rectangle(const MotionRoute& route, fade_region::Result& bound, bool& of_viewport, unsigned& permille, bool read_only) noexcept;
    // Shadowed application render state for the capture-only motion_route
    // line: the last value the shadow saw, or -1 when it is unknown. Reads no
    // device state, so a capture frame costs no extra GetRenderState.
    long shadow_state_field(D3DRENDERSTATETYPE state) const noexcept;
    long composition_blend_field(unsigned index) const noexcept;
    void record_fade_refused(const MotionRoute& route, unsigned refusal) noexcept;
    void log_fade_refused() noexcept;
    void witness_readback() noexcept;       // Present boundary, every k-th frame, one bounded readback
    void release_fade_witness() noexcept;   // Reset and retirement drop the system-memory copy
    void finish_composition(HRESULT, renderer::LinearCompositionPolicy) noexcept;
    bool publish_composition() noexcept;
    void begin_composition_frame() noexcept;
    void composition_export() noexcept;
    void release_composition_identity() noexcept;

    // 0 eligible, 1 unreviewed pair, 2 missing combined object, 3 HDR/decode,
    // 4 unknown or enabled sampler sRGB decode. Does not reject motion.
    unsigned linear_material_refusal() const noexcept;
    HRESULT bind_variant_pair(MotionRoute& route, bool material) noexcept;
    // Timed wrappers over the native SetRenderTarget/COLORWRITEENABLE calls of
    // the per-draw path and the lazy flush; each counts into counters_.set_rt.
    HRESULT bind_target(DWORD index, IDirect3DSurface9* surface) noexcept;
    HRESULT bind_targets(MotionRoute& route) noexcept;
    // The lazy-mode flush behind restore_bindings. `quiet` (the light
    // SetRenderState hook) records no telemetry metric and logs nothing: its
    // metrics and failure line are deferred to the next heavy call.
    template<bool quiet> HRESULT flush_bindings() noexcept;
    void record_deferred() noexcept;
    // Mip LOD bias (X3M_TAA_MIP_BIAS): apply on a routed draw, put every
    // biased stage back (a restore point), drop the sampler shadow and re-read
    // the bindings natively (state block Apply, Reset), one stage's restore.
    void apply_mip_bias() noexcept;
    HRESULT restore_mip_bias() noexcept;
    HRESULT restore_bindings_checked() noexcept;
    void restore_mip_bias_stage(unsigned stage, HRESULT* first) noexcept;
    void resync_samplers() noexcept;
    void log_mip_bias_game_write() noexcept;
    // Shadowed render-state query: the shadow's value when known, else one
    // native GetRenderState (counted) that fills the shadow.
    HRESULT render_state(D3DRENDERSTATETYPE state, DWORD* value) noexcept;
    HRESULT get_render_state_native(D3DRENDERSTATETYPE state, DWORD* value) noexcept;
    void invalidate_render_states() noexcept;
    bool resolve_allowed(SceneEndSource source) noexcept;
    // CPU tick stamp (0 without telemetry) and metric recording into stats_.
    std::uint64_t stamp() const noexcept;
    void record(unsigned metric, std::uint64_t ticks, bool failed = false, std::uint64_t bytes = 0) noexcept;
    void finish_cut_detector() noexcept;
    // Reads the engine camera into the background (latch) or scene slot.
    void read_camera(bool scene) noexcept;
    void log_camera_state() noexcept;
    bool ensure_taa() noexcept;
    // `hdr_scene` (stage 3): the FP16 scene target's texture; the resolve
    // samples it directly with k = hdr_taa_k_ and publishes its output in
    // hdr_resolved_ for the write-back instead of copying back into
    // `main_surface` (which then only serves the debug readback).
    HRESULT resolve(IDirect3DSurface9* main_surface, IDirect3DTexture9* hdr_scene) noexcept;
    // The stage-3 resolve at a scene end while the redirect is active (RT0 is
    // the FP16 target): the single attempt of the frame. Returns whether an
    // attempt was made; a later resolve_allowed of the same frame is then a
    // no-op, so the 8-bit path never runs after it.
    bool resolve_hdr(SceneEndSource source) noexcept;
    void invalidate_taa() noexcept;
    ULONG probe_references() noexcept;
    // Runs a TemporalPass call and folds the device references it created or
    // released into taa_references_ by probing the count before and after,
    // which is exact in both reference models (native and wrapper).
    template<typename Fn> void taa_call(Fn&& fn) noexcept;
    HRESULT save_state(SavedState& saved) noexcept;
    HRESULT restore_state(const SavedState& saved) noexcept;
    bool ensure_target(UINT width, UINT height) noexcept;
    void release_target() noexcept;
    void resync_shadow() noexcept;
    void fill_sentinel() noexcept;
    void describe_binding(DWORD index, IDirect3DSurface9* surface) noexcept;
    renderer::Event event(renderer::EventKind kind) noexcept;
    void bindings(renderer::Event& event) const noexcept;
    bool scene_bound() const noexcept;
    bool sample_scope(MotionRoute& route) noexcept;
    void observe(renderer::Event& event, HRESULT result) noexcept;
    HRESULT undo(MotionRoute& route) noexcept;
    void rollback_route(MotionRoute& route) noexcept;
    HRESULT apply_wrap_states(MotionRoute& route, const renderer::MotionOutputProfile& row) noexcept;
    HRESULT restore_wrap_states(MotionRoute& route) noexcept;
    void recover_motion_state() noexcept; // Successful Reset + actual state reads only.
    renderer::SceneSignatures signatures() const noexcept;
    void readback() noexcept;
    // HDR redirect (hdr_pass.h performs the device work; the policy is here).
    enum class HdrState { Off, Active, Suspended };
    void begin_redirect() noexcept;             // at the latching Clear (before it is forwarded)
    void end_redirect(HdrEnd reason, MotionHdrSceneCallback callback = nullptr, void* context = nullptr) noexcept;
    void flush_redirect() noexcept;             // write back, keep the FP16 target bound
    void drop_redirect() noexcept;              // Reset/release: no write-back
    renderer::HdrWriteback hdr_writeback(IDirect3DSurface9* final_rt0, bool write,
                                       renderer::HdrDisplaySnapshot* display = nullptr) noexcept;
    bool hdr_is_main(IDirect3DSurface9* surface) noexcept;
    void log_hdr_frame() noexcept;

    IDirect3DDevice9* device_ = nullptr;
    void** native_ = nullptr;
    std::uint64_t id_ = 0, frame_ = 0, generation_ = 1, sequence_ = 0;
    D3DCAPS9 caps_{};
    bool requested_ = false, enabled_ = false, depth_enabled_ = false, capture_ = false, telemetry_ = false;
    bool history_available_ = false, releasing_ = false;
    std::map<void*, ShaderEntry> vertex_, pixel_;
    Shadow shadow_{};
    bool linear_material_requested_ = false;
    cutout::Capability cutout_caps_ = cutout::Capability::Pending;
    HRESULT cutout_cap_result_ = S_FALSE;
    std::uint32_t cutout_cap_queries_ = 0, cutout_cap_logs_ = 0;
    std::uint64_t cutout_probe_frame_ = 0;
    bool cutout_probe_frame_known_ = false, cutout_reset_pending_ = false;
    bool cutout_coverage_missed_ = false;
    renderer::LinearMaterialConfig linear_material_config_{};
    bool linear_emission_requested_ = false, distance_fade_requested_ = false;
    unsigned composition_required_producers_ = 0;
    HRESULT composition_attach_result_ = S_FALSE;
    renderer::LinearEmissionConfig linear_emission_config_{1.f, true};
    std::unique_ptr<renderer::LinearEmissionPass> composition_;
    bool composition_busy_ = false, composition_state_lost_ = false;
    bool motion_state_lost_ = false; // A failed restoration blocks native submissions until Reset.
    HRESULT motion_state_error_ = D3DERR_INVALIDCALL;
    bool composition_frame_stopped_ = false, composition_enhanced_ = false;
    bool composition_quarantined_ = false; // Export uncertainty survives frames and Reset.
    bool composition_readers_known_ = false, composition_identity_known_ = true;
    bool composition_effective_ = false, composition_attach_attempted_ = false;
    D3DFORMAT composition_adapter_format_ = D3DFMT_UNKNOWN, composition_depth_format_ = D3DFMT_UNKNOWN;
    IDirect3DTexture9* composition_main_texture_ = nullptr; // owning logical identity
    std::uint32_t composition_main_sampler_mask_ = 0, composition_reader_known_mask_ = 0;
    IDirect3DBaseTexture9* composition_textures_[21]{}; // borrowed; setters/resync only
    IUnknown* composition_main_identity_ = nullptr; // borrowed canonical identity, held by main texture
    bool composition_terminal_export_ = false, composition_diagnostic_export_ = false, composition_published_ = false;
    struct CompositionCounters {
        unsigned eligible_fade = 0, prepared_fade = 0, linear_fade = 0;
        // Logical copy/compose traffic: 56 bytes per target pixel per exchanged
        // bracket, 48 bytes per region pixel per in-place bracket; no source raster.
        std::uint64_t pool_traffic_bytes = 0;
        // In-place fade brackets (policy 4, step 3): prepared, completed Linear,
        // completed Incomplete (source, composite or restore failure; A|R
        // recovered from B|R unless recovery itself failed), recovery failures,
        // and the sum of the rectangles the pass actually backed up and composed.
        unsigned in_place = 0, in_place_linear = 0, in_place_incomplete = 0, recovery_failures = 0;
        std::uint64_t region_pixels = 0;
        HRESULT recovery = S_FALSE;
        unsigned prepared = 0, linear = 0, native = 0, incomplete = 0, refused = 0, suppressed = 0, exports = 0, exchanged = 0;
        HRESULT source = S_FALSE, prepare = S_FALSE, prepare_restore = S_FALSE, composition = S_FALSE, restore = S_FALSE, exchange = S_FALSE, ack = S_FALSE;
        unsigned refusal[6]{}; // pair, permission/scene, readiness, readers, frame stop, prepare failure
        unsigned prepare_failures = 0, composition_failures = 0, restore_failures = 0, exchange_failures = 0, ack_failures = 0;
        // Fade region derivation (step 1): admitted fade draws with a
        // box-derived rectangle versus the full viewport, bound-table outcome
        // and the sum of area fractions (region_fraction_sum / (bound + full)
        // is the mean f of the frame).
        unsigned region_bound = 0, region_full = 0, region_hit = 0, region_miss = 0, region_poisoned = 0, region_evicted = 0;
        unsigned region_reason[unsigned(fade_region::Reason::Count)]{};
        unsigned region_status[unsigned(fade_region::Status::Count)]{};
        std::uint64_t region_permille_sum = 0; // integer per-mille fractions; formatted only at the Present boundary
    } composition_counts_;
    fade_region::BoundTable fade_bounds_; // reserved with the composition pass, dropped at Reset/teardown
    // Fade-region witness state: this frame's derived rectangles with their
    // prepared flag (the union takes prepared draws only; past the capacity
    // the frame counts on, is flagged overflow and its union is the whole
    // target, which can never produce a false violation), the per-frame f
    // histogram (integer per-mille buckets), the per-DIP line budget, and
    // the retained system-memory copy plus one row of union flags, both
    // sized to the M target.
    unsigned fade_witness_interval_ = 0;
    bool witness_frame() const noexcept { return fade_witness_interval_ && frame_ % fade_witness_interval_ == 0; }
    struct FadeWitness {
        static constexpr unsigned rect_capacity = 1024, buckets = 8, line_budget = 64;
        fade_region::Rect rects[rect_capacity]{};
        bool prepared[rect_capacity]{};
        unsigned count = 0, prepared_count = 0, last = rect_capacity, logged = 0;
        bool overflow = false;
        unsigned f_hist[buckets]{};
        IDirect3DSurface9* copy = nullptr;
        UINT copy_width = 0, copy_height = 0;
        unsigned char* row = nullptr;
        UINT row_width = 0;
    } fade_witness_;
    // Shimmer trace: integer-only per-draw records, formatted once after
    // Present. No allocation, formatting or locking on the draw path.
    bool shimmer_trace_ = false;
    struct ShimmerDraw {
        std::uint64_t node = 0;
        std::uint32_t index = 0;          // draw index within the frame
        std::uint32_t model = 0, lod = 0;
        std::uint32_t vertex_count = 0, primitives = 0, topology = 0;
        std::uint64_t vertex_buffer = 0, index_buffer = 0;
        std::int32_t f_permille = -1;     // -1: not fade-admitted (no region derived)
        std::int32_t rect[4]{};           // fade_region rectangle; valid with region_known
        std::uint8_t gate = 0;
        bool routed = false, composition = false, region_known = false, indexed = false;
    };
    static constexpr unsigned shimmer_draw_capacity = 32;
    ShimmerDraw shimmer_draws_[shimmer_draw_capacity]{};
    unsigned shimmer_count_ = 0;   // qualifying draws this frame; beyond the capacity only counted
    void record_shimmer_draw(const MotionRoute& route) noexcept;
    void log_shimmer_frame(unsigned history_previous, unsigned history_current, unsigned committed) noexcept;
    // Capture-only pixel proof of the station source-over split
    // (docs/architecture/linear-station-source-over.md, section 4): a
    // recognised source-over draw of a fade pair that admission refused gets
    // the step-1 rectangle derived and recorded as integers here, formatted
    // once after Present as one fade_refused_rect line. No composition.
    struct FadeRefusedRect {
        std::uint64_t vs = 0, ps = 0, node = 0;
        std::uint32_t index = 0, model = 0, lod = 0;
        std::int32_t rect[4]{};
        std::uint32_t permille = 0;
        std::uint8_t refusal = 0, reason = 0, status = 0;
        bool bound = false, of_viewport = false;
    };
    static constexpr unsigned fade_refused_capacity = 16;
    FadeRefusedRect fade_refused_[fade_refused_capacity]{};
    unsigned fade_refused_count_ = 0; // this frame's refused recognised source-over draws; beyond the capacity only counted
    unsigned material_refusals_logged_ = 0;
    // Lightweight shader setters capture integers only. Formatting is deferred
    // to the existing full CPU-state boundary around Present, once per lifetime.
    struct XtDefaultUnavailable {
        std::uint64_t device = 0, vs = 0, ps = 0;
        unsigned ready_mask = 0;
        bool seen = false, pending = false;
    } xt_default_unavailable_;
    // The application's own D3DSAMP_MIPMAPLODBIAS write hook (an audited
    // light root) must stay integer-only: a failed pre-write restore is
    // recorded here and formatted after Present or at retirement.
    struct MipBiasGameWriteFailure {
        std::uint64_t device = 0, frame = 0;
        unsigned long index = 0, result = 0;
        bool pending = false;
    } mip_bias_game_write_failure_;
    IDirect3DSurface9* target_surface_ = nullptr; // Level 0 of the owned RGBA32F texture (RT1).
    IDirect3DSurface9* depth_surface_ = nullptr;  // Level 0 of the owned R32F texture (RT2).
    UINT target_width_ = 0, target_height_ = 0;
    bool target_failed_ = false;
    IDirect3DPixelShader9* sentinel_ps_ = nullptr;      // One output: motion target alone.
    IDirect3DPixelShader9* sentinel_mrt_ps_ = nullptr;  // Two outputs: motion and depth targets.
    // The vs_3_0 pass-through and declaration of the route's own quads (self
    // test, sentinel fill; renderer/quad_vertex_program.h), created at attach,
    // surviving Reset, one device reference each; quad_fvf_ is the
    // fixture-only XYZRHW twin (X3M_QUAD_FVF_SWITCH builds).
    IDirect3DVertexShader9* quad_vs_ = nullptr;
    IDirect3DVertexDeclaration9* quad_declaration_ = nullptr;
    bool quad_fvf_ = false;
    // The frame's RT0 at the initial Clear is multisampled (the selector never
    // latches one): no RT1/RT2 (textures cannot share its sample count, and
    // D3D9 requires all simultaneous targets to match), so the frame routes
    // and jitters nothing and the resolve skips (TaaSkip::Msaa). Logged once;
    // cleared by a single-sampled latch or Reset.
    bool main_msaa_ = false, msaa_logged_ = false;
    std::uint32_t main_msaa_samples_ = 0;
    // TAA copy mode decided at attach (taa_copy_by_draw); the round-trip
    // self test's verdict text for the device line.
    bool taa_copy_draw_ = false;
    char taa_stretch_test_[96] = "off";
    // Jitter sequence state: requested switch, sample count, latches seen,
    // this frame's and the previous latched frame's jitter in raster pixels.
    bool jitter_requested_ = false, jitter_active_ = false;
    unsigned jitter_samples_ = 8, jitter_latched_ = 0;
    float jitter_[2]{}, jitter_previous_[2]{};
    // Cut detector: displacement magnitudes of this frame's matched draws,
    // reserved once at attach (never grows per draw); bounds see configure.
    std::vector<float> displacements_;
    float cut_median_bound_ = 48.f, cut_missing_bound_ = .25f;
    bool cut_finished_ = false; // Verdict computed for this frame (end of scene phase or before Present).
    // Camera state: the scene view of this frame (read at the depth-only
    // Clear), the background view (diagnostics only), the scene view of the
    // last frame that completed a resolve (the history's camera; cleared with
    // the history) and the raw read diagnostics of the scene read.
    renderer::CameraState camera_scene_{}, camera_background_{}, camera_previous_{};
    std::uint64_t camera_previous_frame_ = 0;
    chase_camera::SnapCursor chase_snap_cursor_{}; // independent cut observation for this device's history
    std::uintptr_t camera_projection_address_ = 0, camera_view_address_ = 0;
    renderer::SentinelMode sentinel_mode_ = renderer::SentinelMode::Auto;
    float camera_cut_degrees_ = 20.f;
    unsigned camera_log_interval_ = 300;
    // Temporal resolve: requested switch, capability verdict at attach, lazy
    // initialization state, device references the pass holds (probed), the
    // application's scene state and active BEGIN/END queries.
    std::unique_ptr<renderer::TemporalPass> taa_;
    bool taa_requested_ = false, taa_enabled_ = false, taa_debug_ = false, taa_failed_ = false, taa_busy_ = false;
    unsigned taa_references_ = 0;
    bool scene_open_ = false;
    unsigned active_queries_ = 0;
    renderer::SceneBoundarySelector selector_;
    renderer::Surface main_, main_depth_;
    renderer::Event pending_{};
    bool pending_valid_ = false, fill_pending_ = false;
    // Sized at attach when the route is requested; a Device that never asked
    // for the route does not pay for the two reserved tables.
    renderer::MotionRowHistory history_{0};
    MotionFrameCounters counters_{};
    unsigned logged_failures_ = 0;
    unsigned hook_disagreements_logged_ = 0; // own budget: a latch-only screen must not starve the failure log
    // Telemetry sink (capture.cpp's per-device State) and frame-line cadence.
    telemetry::State* stats_ = nullptr;
    unsigned frame_log_interval_ = 60;
    // Lazy binding state: RT1 (and RT2) bound by the route with the
    // application's COLORWRITEENABLE1/2 values saved at bind time.
    bool lazy_mode_ = false, lazy_rt1_ = false, lazy_rt2_ = false;
    DWORD lazy_write1_ = 15, lazy_write2_ = 15;
    bool state_shadow_ = true, scene_hook_installed_ = false;
    // Sampler shadow of the mip LOD bias (X3M_TAA_MIP_BIAS), stages 0-15:
    // the application's texture binding (pointer identity only, never
    // dereferenced after the SetTexture hook that recorded it) and its level
    // count, the application's MIPFILTER (read natively once when a routed
    // draw first needs it, then trusted: the game shadows its sampler state
    // and writes once per value change), the value the bias replaced (read
    // natively once, or the application's own write) and whether the route's
    // bias is currently on the device. Bit masks select the stages a routed
    // draw inspects (bound) and the restore visits (biased): no allocation,
    // a few compares per bound stage per routed draw.
    struct SamplerShadow {
        IDirect3DBaseTexture9* texture = nullptr;
        DWORD levels = 0, mipfilter = 0, saved_bias = 0;
        DWORD srgb = 0;
        bool srgb_known = false;
        bool mipfilter_known = false, saved_known = false, biased = false;
    };
    static constexpr unsigned sampler_stage_count = 16;
    SamplerShadow samplers_[sampler_stage_count]{};
    std::uint32_t sampler_bound_mask_ = 0, sampler_biased_mask_ = 0;
    std::uint32_t sampler_restore_failed_mask_ = 0; // stages whose restore failed since the last Present (one attempt each)
    bool cutout_arm_active_ = false;                // begin_frame latch of cutout_arm_configured()
    float mip_bias_ = 0.f;
    DWORD mip_bias_bits_ = 0;
    // Session totals of the bias path (logged at release) and the last
    // application write of MIPMAPLODBIAS (recorded by the light hook, logged
    // by the next heavy call).
    std::uint32_t mip_bias_total_sets_ = 0, mip_bias_total_restores_ = 0, mip_bias_total_reads_ = 0;
    std::uint32_t mip_bias_total_game_writes_ = 0, mip_bias_logged_game_writes_ = 0, mip_bias_total_failures_ = 0;
    DWORD mip_bias_game_write_stage_ = 0, mip_bias_game_write_value_ = 0;
    bool mip_bias_summary_logged_ = false;
    // FP16 HDR scene path: the pass, the switch and the attach verdict, the
    // redirect state, the application's main surface held from the latch to
    // the end (one reference), the description of the FP16 target for the
    // shadow resynchronization, the dirty flag (content reached the target
    // since the last write-back), the block after an unwind (cleared by a
    // passing recheck at a later latch or by Reset) and the target allocation
    // failure latch (retried after Reset, like the motion target).
    std::unique_ptr<renderer::HdrPass> hdr_;
    renderer::HdrConfig hdr_config_{};
    bool hdr_requested_ = false, hdr_enabled_ = false;
    bool hdr_tonemap_disabled_logged_ = false;
    float hdr_taa_k_ = 0.f;
    float taa_k_override_ = -1.f;             // X3M_TAA_K (negative: derived)
    float taa_sharpen_ = 0.f;                 // X3M_TAA_SHARPEN (0: off)
    // 8-bit route: failed sharpened draws (the pass kept the resolve, the
    // copy-back presented it); at the limit the sharpen is no longer requested.
    unsigned taa_sharpen_failures_ = 0;
    static constexpr unsigned sharpen_failure_limit = 3;
    // The pass's resolved FP16 output (borrowed: valid until the pass's next
    // run, invalidate, before_reset or shutdown), published by the stage-3
    // resolve for the write-back of the same scene end and cleared with it.
    IDirect3DTexture9* hdr_resolved_ = nullptr;
    HdrState hdr_state_ = HdrState::Off;
    IDirect3DSurface9* hdr_main_ = nullptr;
    renderer::Surface hdr_target_{};
    bool hdr_dirty_ = false, hdr_blocked_ = false, hdr_target_failed_ = false, hdr_latch_pending_ = false;
    unsigned hdr_blocked_latches_ = 0, hdr_logged_ = 0;
    std::uint32_t hdr_pending_state_ = 0; // HdrState the application's SetRenderTarget(0) commits on success
    // Metrics of quiet lazy flushes (from the light SetRenderState hook),
    // recorded and logged at the next heavy call.
    std::uint32_t deferred_flushes_ = 0;
    std::uint64_t deferred_flush_ticks_ = 0;
    HRESULT deferred_flush_result_ = S_OK;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    MotionOutputFixtureConfig fixture_{};
    bool fixture_configured_ = false, fixture_abi_known_ = false;
    bool fixture_stretch_fault_ = false; // X3M_FIXTURE_STRETCH_FAULT=1: the round-trip self test "fails" (taa_copy=draw)
    // X3M_FIXTURE_FADE_RECT=l,t,r,b (fixture seam only): every admitted fade
    // draw reports this rectangle as its bound-derived region, so the witness
    // sees a sub-viewport rectangle (and a deliberately wrong one) although the
    // fixture has no seam scope. Never compiled into production.
    bool fixture_fade_rect_set_ = false;
    fade_region::Rect fixture_fade_rect_{};
    float fixture_last_pixel_abi_[8]{};
    unsigned fixture_emission_exchange_fault_ = 0;
    unsigned fixture_cutout_cap_fault_ = 0, fixture_cutout_vs_fault_ = 0, fixture_cutout_ps_fault_ = 0;
    unsigned fixture_cutout_rs_fault_ = 0, fixture_cutout_sampler_fault_ = 0;
    unsigned fixture_hdr_fault_kind_ = 0, fixture_hdr_fault_count_ = 0; // queued until the pass exists
#endif
};
} // namespace x3m
