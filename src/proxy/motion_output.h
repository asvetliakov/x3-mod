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
#include "own_ship_cache.h"
#include "../renderer/scene_boundary.h"
#include "../renderer/motion_history.h"
#include "../renderer/depth_prepass_profiles.h"
#include "../renderer/sun_share_frame.h"
#include "../renderer/motion_row_history.h"
#include "../renderer/camera_reprojection.h"
#include "../temporal/resolve.h"
#include "../renderer/fog_pass.h"
#include "../renderer/sun_occlusion_pass.h"
#include "fog_card_policy.h"
#include "fog_sector_policy.h"
#include "fog_card_mask.h"
#include "fog_card_match.h"
#include "../renderer/hdr_pass.h"
#include "../renderer/linear_material.h"
#include "linear_cutout.h"
#include "screen_emission_admission.h"
#include "../renderer/linear_emission.h"
#include "../renderer/linear_emission_pass.h"
#include "../renderer/linear_distance_fade.h"
#include "fade_region.h"
#include "fade_route_core.h"
#include "shadow_replay_candidates.h"
#include "shadow_replay_depth.h"
#include "shadow_retention.h"
#include "shadow_caster_class.h"
#include "shadow_replay_sun_point.h"
#include "sun_light_poll.h"
#include "../renderer/shadow_replay_pass.h"
#include "../renderer/shadow_replay_projection.h"
#include "../renderer/sun_shadow_apply_pass.h"
namespace x3m::renderer { struct MotionOutputProfile; class TemporalPass; }
namespace x3m::ownership { class AdmissionMonitor; struct BufferLockView; }
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
// Why a scene draw did not match (diagnostics only: the capture `motion_route`
// row's `unmatched=` field, asteroid-fog-temporal.md "Run 130"). Recorded on
// the refusal path from values the gate already read; no getter, no lookup.
// Gate 4 records the first failing check of the opaque chain, or, for a
// recognised fade-band draw of a fade pair, the fade arm's own refusal step.
enum class UnmatchedReason : std::uint8_t {
    None = 0,
    Feature, Scene, XtPair, Unregistered, Pair,                       // gates 1-3
    UserMemory, ReadFailed, NoZWrite, Blended, State, Instanced, Rows, Geometry, // gate 4, opaque chain
    FadeCaps, FadeInstanced, FadeRows, FadeGeometry, FadeConstants, FadeOrigin, FadeThreshold, // gate 4, fade arm
    OverlayNode,                                                      // gate 4, overlay arm: not right after a routed draw of the same node
    Scope, History,                                                   // gates 5-6 (routed, mode 0 / no previous rows)
    Count
};
constexpr const char* unmatched_reason_name(UnmatchedReason reason) noexcept {
    constexpr const char* names[unsigned(UnmatchedReason::Count)] = {
        "none", "feature", "scene", "xt_pair", "unregistered", "pair",
        "user_memory", "read_failed", "no_zwrite", "blended", "state", "instanced", "rows", "geometry",
        "fade_caps", "fade_instanced", "fade_rows", "fade_geometry", "fade_constants", "fade_origin", "fade_threshold",
        "overlay_node", "scope", "history"};
    return unsigned(reason) < unsigned(UnmatchedReason::Count) ? names[unsigned(reason)] : "unknown";
}
// Per-draw decision. Stack object; carries what after_draw must undo.
struct MotionRoute {
    MotionGate gate = MotionGate::Feature;
    UnmatchedReason unmatched = UnmatchedReason::None;
    bool routed = false, matched = false, scene = false;
    bool static_assumed = false; // X3M_TAA_UNMATCHED_STATIC: a new key's previous rows came from the static-world assumption (matched stays false)
    bool composition = false, submit = true, evaluated = false;
    HRESULT submission_error = D3DERR_INVALIDCALL;
    HRESULT preparation_error = S_OK; // First internal failure; never replaces the native draw result.
    renderer::LinearCompositionPolicy composition_policy = renderer::LinearCompositionPolicy::AdditiveEmission;
    bool cutout_candidate = false, cutout = false; // requested exact scene pair; admitted exact cutout arm (cutout pair)
    bool alpha_tested = false; // admitted with alpha test on (cutout arm or tested-opaque arm); excluded from replay candidates (W3)
    bool cutout_test_known = false, cutout_color_known = false, cutout_alpha_known = false, cutout_z_known = false, cutout_zfunc_known = false;
    bool cutout_blend_known = false, cutout_source_over = false; // exact observed source-over triple: not a miss
    DWORD cutout_test = 0, cutout_color = 0, cutout_alpha = 0, cutout_z = 0, cutout_zfunc = 0, cutout_blend = 0;
    bool linear_material = false; // Combined color+motion pair actually bound.
    FogCardMask fog_card_mask{};
    bool source_gain = false;     // Source-gain PS bound natively for this draw; restored after it.
    bool source_gain_screen = false; // ... and DESTBLEND ONE substituted for the native INVSRCCOLOR (screen substitution); restored after it.
    bool hull_gain = false;       // Hull-emitter gain PS bound natively for this ONE/ONE draw (emitter plan phase 3); restored after it.
    bool original_fill = false;   // Original-fill PS selected in the routed pair (undone with the route).
    bool hull_lightmap = false;   // Hull light-map gain PS (fill K composed) selected in the routed pair (undone with the route).
    bool hull_lightmap_widen = false; // The widened light-map variant (per-draw texel footprint lanes in c217.yz) selected instead of the gained one.
    bool widen_filter_set = false;    // The light-map stage's MINFILTER raised to ANISOTROPIC for this widened draw (undo restores widen_filter_saved).
    std::uint8_t widen_filter_stage = 0; DWORD widen_filter_saved = 0;
    bool vs_set = false, ps_set = false, rt_set = false, write_set = false;
    bool vs_constants_set = false, ps_constants_set = false;
    // Scoped shader restoration (docs/architecture/ownership-shadow-lifetime-diagnosis.md):
    // the device's actual VS/PS bindings, owned through the public getters before
    // this draw's first injected shader bind (acquire_restore) and released after
    // its last restore (after_draw). A null is an owned null binding. Every
    // restore of an injected bind uses these, never the borrowed shadow pointers.
    IDirect3DVertexShader9* restore_vs = nullptr;
    IDirect3DPixelShader9* restore_ps = nullptr;
    bool restore_held = false;
    renderer::LensDraw lens{};    // Partial sun occlusion: what the lens wrap bound for this draw; put back by after_draw.
    bool sun_receiver = false, sun_color_writer = false;
    bool sun_stamp = false; // gate-3 refused scene draw the lane may stamp invalid after the native draw (sun_stamp_call_ holds its arguments)
    // Sun-lane refusal diagnostics (sun_share_frame.h): the gate reason
    // recorded while the lane is on, and the z/z-write states the selector
    // read (bit0 z, bit1 z write, bit2 both known). Never consulted by
    // eligibility or availability.
    std::uint8_t sun_refusal = 0, sun_z_state = 0;
    // Gate-4 states as the chain read them, for the writer signature line only:
    // bits 0-3 RT0 mask, bit 4 alpha test on, bit 5 sRGB write on, bits 6-8 the
    // three values known (read and succeeded). Zero when gate 4 was not reached.
    std::uint16_t sun_draw_state = 0;
    // A cutout pair admitted by the tested-opaque arm (alpha test on, exact arm
    // unconfigured): drawn with its native MIPMAPLODBIAS so alpha-test coverage
    // matches the native draw (biased stages are restored before the draw).
    bool native_mip_bias = false;
    bool depth = false, rt2_set = false, write2_set = false;   // RT2 bound for this draw (row has depth_output).
    bool stream0_frequency_known = false; UINT stream0_frequency = 0; // gate 4's GetStreamSourceFreq(0) of this draw, reused by the depth lease
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
    // Caster retention (shadow-caster-retention.md): the scope's observer epoch,
    // registry and node class bits, from the reads sample_scope already makes.
    std::uint64_t observer_epoch = 0;
    std::uintptr_t registry = 0;
    std::uint32_t node_flags12c = 0, node_flags130 = 0;
    std::uint64_t ticks = 0;  // CPU ticks of apply (before_draw) plus undo (after_draw); telemetry only.
    // Conservative screen rectangle of an admitted distance-fade draw
    // (docs/architecture/linear-distance-fade-region.md, step 1): derived
    // after admission, logged and counted; the bracket does not consume it yet.
    fade_region::Region fade_region{};
    bool fade_region_evaluated = false;
    // Rectangle area per mille of its denominator, exactly the f_permille the
    // fade_region line reports; only meaningful with fade_region_evaluated.
    unsigned fade_region_permille = 0;
    // Step B of docs/architecture/screen-emission-region.md: the locked-prefix
    // rectangle of a non-indexed TRIANGLELIST draw from StartVertex 0 of a
    // stride-24 FLOAT3 stream (X3M_SCREEN_EMISSION_BOUND=1). prefix_region.bound
    // false means no bound: step C refuses such a draw, never the full viewport.
    fade_region::Region prefix_region{};
    bool prefix_evaluated = false;
    unsigned prefix_region_permille = 0; // area per mille of the viewport (witness f histogram)
    // Fade-band motion arm (fade_route_core.h): a reviewed pair in the exact
    // fade-band state admitted by its fade fraction estimate (per mille). RT2
    // is bound with its write mask cleared for such a draw; RT1 blends
    // exactly (motion alpha 1 under SRCALPHA/INVSRCALPHA).
    bool fade_arm = false;
    bool fade_held = false; // admitted below the threshold by the hysteresis band only
    // Overlay arm (asteroid-fog-temporal.md "Run 130"): a reviewed non-fade
    // pair's source-over sub-mesh (the hull glass/window layer) drawn right
    // after a routed draw of the same node, admitted through the fade arm at
    // permille 1000 with no fraction or hysteresis (its alpha is material, not
    // distance).
    bool overlay = false;
    unsigned fade_permille = 0;
    // Additive option: DESTBLEND ONE applied for this draw (restored to the
    // shadowed INVSRCCOLOR after it) and, with gain != 1, the gained PS bound.
    bool screen_additive = false, screen_additive_ps = false, screen_additive_alpha = false;
    // Origin distance of the draw's rows (fade_route_core.h origin_distance) for
    // the caster-candidate counter; negative when the camera latch or rows refuse.
    float candidate_distance = -1.f;
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
// Every path that drops the TAA history and the history's camera view
// (invalidate_taa), for the consolidated `taa_invalidate site=<name>` line:
// one bit per site, logged at most once per frame per site at the frame's
// end or at a frame (re)begin (a Reset is logged by after_reset's re-begin,
// with the frame the proxy began at the last Present). Sites on the light
// setter paths record the bit only.
enum class TaaInvalidateSite : unsigned {
    RestoreFailed = 0,          // a route restore point failed (bindings, mip restore, lazy flush, undo, rollback)
    StateLost = 1,              // a draw or resolve while motion state is already lost
    Skip = 2,                   // resolve_allowed refused the resolve (taa_skip carries the reason)
    Target = 3,                 // RT0 at the scene end is not the latched main/FP16 target
    Container = 4,              // GetContainer of a route target failed
    ResolveFailed = 5,          // the pass run or the copy-back failed
    NotResolved = 6,            // the frame ended without a resolve (menu, rejected, never reached)
    PresentFailed = 7,          // Present returned a failure
    Reset = 8,                  // device Reset
    ComparisonExposure = 9,     // the comparison exposure toggle reseeds the history
    ComparisonStateFailed = 10, // a comparison-control operation failed
    CompositionStateLost = 11,  // the linear composition pass lost device state
    CompositionReaders = 12,    // the main-target readers are unknown while the enhanced image is live
    CompositionExport = 13,     // the enhanced image was exported (quarantine)
    CompositionAttach = 14,     // the composition pass could not attach or lacks a required policy
    CompositionBegin = 15,      // the composition frame could not begin
    CompositionRefused = 16,    // a required scene-source draw was refused
    CompositionPrepare = 17,    // preparing a required composition failed
    CompositionIncomplete = 18, // the composition ended without a linear image
    CutoutMissed = 19,          // a requested cutout pair forwarded natively without owned motion (cutout::missed)
    FogTransition = 20,         // keep/replace/off/fault transitions only
    Count = 21
};
const char* taa_invalidate_site_name(TaaInvalidateSite site) noexcept;
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
    renderer::ReadbackTiming readback_timing{};
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
    // The history's camera view was valid at the sentinel-policy call (before
    // the resolve relatches it): camera_state prev_valid_at_policy.
    bool camera_previous_valid = false;
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
    // Cutout pairs (linear_cutout.h identity) on a frame whose exact cutout
    // arm is inactive (cutout_arm_active_ false: configured mip bias, HDR off
    // or a non-Ready verdict), where they can only reach the route through
    // the tested-opaque arm. cutout_opaque_routed counts successful routed
    // draws, cutout_opaque_lane those of them that bound the lane variant
    // with the share extraction (route.sun_receiver: oC2.g written), and
    // cutout_opaque_refused the refused ones, bucketed by the route's
    // SunUntrackedReason (sun_share_frame.h). Diagnostics only.
    std::uint32_t cutout_opaque_routed = 0, cutout_opaque_refused = 0, cutout_opaque_lane = 0;
    std::uint32_t cutout_opaque_reasons[renderer::sun_untracked_reason_count]{};
    // Fade-band arm: reviewed-pair draws in the exact fade-band state routed
    // by the arm, and those recognised but refused (fraction below the
    // threshold, unreadable constants, device not ready), which then take
    // the fade bracket or the native path exactly as before.
    std::uint32_t fade_routed = 0, fade_refused = 0, fade_held = 0; // fade_held: of fade_routed, admitted by the hysteresis band
    std::uint32_t overlay_routed = 0, overlay_refused = 0; // overlay arm (same-node source-over sub-mesh); not in fade_routed/fade_refused
    std::uint32_t depth_routed = 0, jittered = 0;
    // Scene draws with ZENABLE and ZWRITEENABLE on that went out unjittered
    // while the jitter was active: every one breaks the "whole scene moves
    // together" invariant behind the LESSEQUAL tests of later jittered draws
    // (asteroid-fog-temporal.md, run 47). Zero is the expected value.
    std::uint32_t unjittered_depth_writers = 0;
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
    // lazy_mask_writes: routed draws of lazy mode (one count per draw) that met
    // an application COLORWRITEENABLE1, or COLORWRITEENABLE2 on a depth row,
    // other than 15 and took the per-draw mask write/restore. The route's own
    // RT2 = 0 write of a fade-band draw under a mask of 15 is not counted: it is
    // route policy, not the application's mask.
    std::uint32_t set_rt = 0, jitter_writes = 0, lazy_flushes = 0, lazy_mask_writes = 0, readbacks = 0;
    std::uint64_t gate_ticks = 0, route_draw_ticks = 0, set_rt_ticks = 0, jitter_ticks = 0, fill_ticks = 0;
    std::uint64_t lease_retire_ticks = 0;
    std::uint32_t lease_retire_calls = 0, lease_retire_records = 0, lease_retire_refs = 0, lease_retire_clock_errors = 0;
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
    // Scoped shader restoration: public GetVertexShader/GetPixelShader calls
    // made for injected draws (two per acquiring draw, none on an unmodified
    // draw) and injections declined because a getter failed.
    std::uint32_t restore_getters = 0, restore_declines = 0;
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
    std::uint32_t flags12c = 0, flags130 = 0; // node class bits (caster retention's excluded classes)
    std::uint64_t observer_epoch = 0;         // the lifetime observer's epoch (a retention key component)
};
struct MotionOutputFixtureConfig {
    std::uint32_t size = sizeof(MotionOutputFixtureConfig);
    std::uint64_t background_vs[3]{}, background_ps[3]{};
    MotionOutputFixtureScope scope{};
    std::uint32_t emission_scene_owner = 0;
    std::uint32_t force_taa_readback = 0; // Successful resolve output only; no per-draw capture.
    std::uint32_t observe_native_wrap = 0; // Native indexed-draw observation only.
    std::uint32_t suppress_lightmap_widen = 0; // Bind the gained variant instead of the widened one (the widening script's texld against the block at k = 1).
};
// Device-owned last native indexed submission. Failure is diagnostic only and
// never changes source submission, route state, or the caller's WRAP values.
// Caster retention seam: the synthetic lifetime observer (motion_output_shadow_retention_inc.h lists the operations).
void shadow_retention_fixture_lifetime(unsigned op, std::uint64_t a, std::uint64_t b) noexcept;
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
    // The lane's RT2 is A32B32G32R32F (docs/architecture/shadow-receiver-depth.md,
    // the only encoding since 2026-09-18): .r = z/w, .g = share, .b (= .a) = the
    // routed fragments' interpolated clip w, the apply quad's receiver depth. Off
    // the lane RT2 is R32F. The format is fixed at target creation and rebuilt
    // after Reset.
    void configure_sun_shadow_lane(bool requested) noexcept { sun_lane_requested_=requested; }
    D3DFORMAT lane_depth_format() const noexcept { return sun_lane_active_?D3DFMT_A32B32G32R32F:D3DFMT_R32F; }
    // Scene-end sun-shadow application (docs/architecture/legacy-sun-application.md,
    // section 2; X3M_SUN_SHADOW_APPLY=1): one quad multiplying the FP16 scene
    // target by 1 - (1 - f) s after the depth replay of the same frame and
    // before the fog and the resolve. Requires the lane and the depth replay (the
    // caller enables all three); exponent 1 on original shading, 1 / 2.2 with
    // linear materials. Off: nothing.
    // bias_units: the constant compare bias in world units (X3M_SUN_SHADOW_BIAS_UNITS);
    // clamp_texels: the receiver-plane clamp and non-planar fallback in world
    // texels (X3M_SUN_SHADOW_BIAS_CLAMP_TEXELS); renderer::sun_shadow_apply_bias
    // resolves both per frame with the cascade. slope_texels: the cascade
    // program's slope-scaled margin in texels of the receiver plane's depth
    // slope (X3M_SUN_SHADOW_BIAS_SLOPE_TEXELS; sun_shadow_apply_pass.h). Out-of-range values keep the defaults.
    void configure_sun_shadow_apply(bool requested, double bias_units=renderer::sun_shadow_bias_units_default,
                                    double clamp_texels=renderer::sun_shadow_bias_clamp_texels_default,
                                    double slope_texels=renderer::sun_shadow_bias_slope_texels_default) noexcept {
        sun_apply_requested_=requested;
        sun_apply_bias_units_=bias_units>=renderer::sun_shadow_bias_units_min&&bias_units<=renderer::sun_shadow_bias_units_max?bias_units:renderer::sun_shadow_bias_units_default;
        sun_apply_clamp_texels_=clamp_texels>=renderer::sun_shadow_bias_clamp_texels_min&&clamp_texels<=renderer::sun_shadow_bias_clamp_texels_max?clamp_texels:renderer::sun_shadow_bias_clamp_texels_default;
        sun_apply_slope_texels_=slope_texels>=renderer::sun_shadow_bias_slope_texels_min&&slope_texels<=renderer::sun_shadow_bias_slope_texels_max?slope_texels:renderer::sun_shadow_bias_slope_texels_default;
    }
    // Caster-candidate counter (shadow_replay_candidates.h; X3M_SHADOW_REPLAY_CANDIDATES=1):
    // integer bookkeeping per routed draw, one shadow_replay_candidates line per
    // scene end, at most 16 shadow_replay_lock_witness lines per device. Off: nothing.
    // cap: managed candidates recorded (and replayed) per frame, 1..record_capacity
    // (X3M_SHADOW_REPLAY_CAP, default shadow_replay::default_cap); the rest count `capped`.
    void configure_shadow_replay_candidates(bool requested, ownership::AdmissionMonitor* monitor, unsigned cap=shadow_replay::default_cap) noexcept {
        candidates_requested_=requested; candidates_monitor_=requested?monitor:nullptr;
        candidate_cap_=cap<1u?1u:cap>shadow_replay::record_capacity?shadow_replay::record_capacity:cap;
    }
    // Object bounds log (docs/architecture/engine-frame-time.md, "Object bounds
    // log"; X3M_OBJECT_BOUNDS_LOG=1): on capture frames only, one object_bounds
    // line per routed draw whose object box the candidate route above already
    // computed, giving its projected screen box, depth range and frustum corner
    // count. Diagnostic only; nothing reads it back. Off (the default): one bool
    // test on the box path, no line and no transform.
    void configure_object_bounds_log(bool requested) noexcept { object_bounds_log_ = requested; }
    // One-cascade depth replay (shadow_replay_depth.h; X3M_SHADOW_REPLAY_DEPTH=1):
    // the leased slice-0 candidates re-issued into a private sun-space map at
    // the scene end, one shadow_replay_depth line per frame. Requires the
    // counter above (the caller enables both). Off: nothing.
    // size, extent (half-extent in the sun basis) and depth_half (world units)
    // are the cascade-0 box (X3M_SHADOW_REPLAY_SIZE/_EXTENT/_DEPTH_HALF, read
    // once at device creation); the candidate box test, the replay projection
    // and the apply quad all take them from depth_cascade_. Out-of-range values
    // keep the defaults.
    void configure_shadow_replay_depth(bool requested, unsigned size, float extent=renderer::shadow_replay_extent_default,
                                       float depth_half=renderer::shadow_replay_depth_half_default) noexcept {
        depth_replay_requested_=requested&&candidates_requested_;
        depth_replay_size_=size>=renderer::shadow_replay_size_min&&size<=renderer::shadow_replay_size_max?size:renderer::shadow_replay_size_default;
        depth_replay_extent_=extent>=renderer::shadow_replay_extent_min&&extent<=renderer::shadow_replay_extent_max?extent:renderer::shadow_replay_extent_default;
        depth_replay_depth_half_=depth_half>=renderer::shadow_replay_depth_half_min&&depth_half<=renderer::shadow_replay_depth_half_max?depth_half:renderer::shadow_replay_depth_half_default;
        depth_cascade_.size=depth_replay_size_; depth_cascade_.half_extent=depth_replay_extent_; depth_cascade_.set_depth_half(depth_replay_depth_half_);
    }
    // Sun-shadow cascades (docs/architecture/shadow-cascades.md; X3M_SHADOW_CASCADES):
    // a set with count >= 1 replaces the single map by that many camera-centred
    // maps in one replay transaction, a cascade mask per candidate, per-cascade
    // caps, the issue budget with the far cascade on alternate frames, and the
    // cascade apply program. Requires the depth replay (the caller enables
    // both); count 0 (the default) leaves the single-map path untouched.
    void configure_shadow_cascades(const renderer::ShadowCascadeSet& set) noexcept { depth_cascade_config_=set; }
    // Own-ship-adaptive cascade 0 (shadow-cascade-extents.md, section 5;
    // X3M_SHADOW_CASCADE_ADAPTIVE_C0 = k, 0 or absent off): E0 = max(configured,
    // k x own-ship radius) with hysteresis, and the ladder behind it sliding
    // with E0 at `ratio` per cascade (X3M_SHADOW_CASCADE_LADDER_RATIO, default 5).
    void configure_shadow_cascade_adaptive(float k, float ratio=renderer::shadow_cascade_ladder_ratio_default) noexcept { cascade_adaptive_k_=k; cascade_ladder_ratio_=ratio; }
    // Per-frame sun trace (X3M_SHADOW_SUN_TRACE=1, launcher --shadow-sun-trace;
    // default off): one `shadow_sun_frame` line per frame while the cascades are
    // on, so the re-derivation rate is measurable between the sparse
    // shadow_replay_sun_point lines. No effect on any decision.
    void configure_shadow_sun_trace(bool trace) noexcept { point_sun_trace_=trace; }
    // Caster retention (shadow-caster-retention.md): census or live, on the cascades only; off by default.
    void configure_shadow_retention(shadow_retention::Mode mode, std::uint32_t age_cap, double eps, bool timing) noexcept {
        retention_mode_=mode; retention_age_cap_=age_cap; retention_eps_=eps; retention_timing_=timing;
    }
    // The device Release hook: a held application resource the application has
    // already released pins one device reference that device_references() cannot
    // count. When a Release could be the application's final one (the count is
    // within retention_references() of it) the hook flushes the store first;
    // a false positive costs only the off-screen shadows until resubmission.
    unsigned retention_references() const noexcept { return retention_&&!reference_accounting_busy()?retention_->store.references():0u; }
    void retention_before_final_release() noexcept { flush_shadow_retention(shadow_retention::Flush::Teardown); }
    const renderer::ShadowCascadeSet& shadow_cascades() const noexcept { return depth_cascades_; }
    const renderer::ShadowReplayCascade& shadow_replay_cascade() const noexcept { return depth_cascade_; }
    bool sun_shadow_lane_enabled() const noexcept { return sun_lane_active_; }
    // Diagnostic snapshot at scene end BEFORE the fog/TAA; not a later color-owner lease.
    const renderer::SunShareFrame& sun_shadow_frame() const noexcept { return sun_frame_; }
    // Borrowed same-frame exclusion M; valid only while sun_shadow_frame().available.
    IDirect3DSurface9* sun_shadow_coverage() const noexcept {
        return sun_frame_.available&&sun_frame_.coverage_required&&composition_?composition_->coverage_target():nullptr;
    }
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
    // X3M_TAA_UNMATCHED_STATIC (default off): 0 off, 1 "node" (a new key whose
    // object was drawn last frame under another key), 2 "all" (any new key).
    void configure_unmatched_static(unsigned mode) noexcept { unmatched_static_ = mode <= 2 ? mode : 0; }
    // X3M_TAA_SKY_HISTORY=strict (default with the TAA route since Run 68 A, resolved by the caller): the resolve's strict sky term whenever the
    // camera path is in effect. A sky pixel whose 3x3 holds no routed geometry
    // accepts sentinel history taps only (docs/architecture/seta-motion.md).
    // band_px: X3M_TAA_SKY_HISTORY_BAND_PX (1..16, default 3), the band term's threshold in px/frame (seta-motion.md section 4).
    // exit_px: X3M_TAA_SKY_HISTORY_EXIT_PX (0 off, else 0.125..band_px; the caller's default 0.25 under strict since Run 68 A), the exit reset's parallax floor
    // (docs/architecture/seta-sky-hull-share-decay.md); needs strict (the caller refuses it otherwise) and an age program
    // (the far stabiliser, the thin region or the adaptive weight: taa_initialize logs it unavailable and drops it otherwise).
    void configure_sky_history(bool strict, float band_px = 3.f, float exit_px = 0.f) noexcept {
        sky_history_strict_ = strict; sky_history_band_px_ = band_px >= 1.f && band_px <= 16.f ? band_px : 3.f;
        sky_history_exit_px_ = strict && x3::temporal::valid_sky_history_exit(exit_px, sky_history_band_px_) ? exit_px : 0.f;
    }
    // RT1/RT2 binding policy (X3M_MOTION_RT_MODE). perdraw (default): each
    // routed draw binds RT1/RT2 and COLORWRITEENABLE1/2 and after_draw puts
    // the application's values back. lazy (experiment): the bindings stay
    // across consecutive routed draws and restore_bindings() puts them back
    // before any application call that could observe or depend on them
    // (capture.cpp calls it from those hooks; before_draw calls it for every
    // draw that does not route). The write masks are never held: a routed
    // draw whose mask differs from 15 writes and restores it as per-draw mode
    // does (lazy_mask_writes), so lazy needs no SetRenderState/GetRenderState
    // hook (docs/architecture/route-per-draw-cost.md, lever 3). Effective at
    // attach; equivalence is proven by the motion-output fixture's burst cases.
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
    // Hybrid unhook (docs/architecture/state-call-fast-path.md, step 5): with
    // the SetRenderState/SetSamplerState hooks not installed (`state_hooks_`
    // false, the production configuration) the route has no write
    // observation. Every render-state, blend, fill-mode and sampler-sRGB
    // reader then takes its value from GetRenderState/GetSamplerState at the
    // draw, once per state per draw: begin_draw_reads drops the per-draw cache
    // at the top of before_draw, the *_known helpers fill it on demand, and
    // the restore-after-substitution sites read the values the admission
    // cached. The mip bias goes on for the routed draw only and comes back
    // right after it (after_draw), so no application LODBIAS write can land
    // under the route's bias. With the hooks installed the helpers return
    // the shadow's flags unchanged.
    void configure_state_hooks(bool installed) noexcept { state_hooks_ = installed; }
    bool state_hooks() const noexcept { return state_hooks_; }
    const char* render_state_mode() const noexcept { return !state_hooks_ ? "get" : state_shadow_ ? "shadow" : "native"; }
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
    // X3M_TAA_MIP_BIAS=<float> (-0.5 by default with the TAA resolve, 0: off): the
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
    // Source-only encoded gain of the same twenty additive pairs
    // (docs/architecture/linear-emission-cost.md, "Implemented"): a PS
    // variant with one colour MUL, selected per draw in the native
    // ADD/ONE/ONE state while the FP16 scene target is active. No bracket,
    // no composition, no per-draw work beyond two native SetPixelShader
    // calls. Gain 1 is off (no variant is created). Configure before attach.
    void configure_emission_source_gain(float gain) noexcept; // one gain for all twenty pairs
    bool emission_source_gain_requested() const noexcept { return emission_source_gain_requested_; }
    // Emitter plan phase 3: the same gain over the twelve hull programs' ADD
    // ONE/ONE draws (X3M_HULL_EMISSION_GAIN); 1 = off. The guide lights are
    // switched with the effects gain (Ctrl+Shift+F6), the hull light-map gain
    // alone (Ctrl+Shift+F4): one entry point, lightmap = true for the F4
    // family, false for the guide lights. Returns the new state of the family
    // the key drove: 1 on, 0 off, -1 not requested (logged no-op); the
    // prebuilt variants stay, nothing is created or released.
    void configure_hull_emission_gain(float gain) noexcept;
    int hull_emission_gain_toggle(bool lightmap) noexcept;
    bool hull_emission_gain_requested() const noexcept { return hull_emission_gain_requested_; }
    // Runtime A/B of the source gain (Ctrl+Shift+F6, comparison-hotkeys.md):
    // the prebuilt variants stay; the per-draw path stops selecting them (and
    // stops the screen substitution), so the draw goes out exactly as it
    // would without the option. Returns the new state (1 on / 0 off), or -1
    // when the option was not requested (gain 1, no variant): a logged no-op.
    int emission_source_gain_toggle() noexcept;
    bool emission_source_gain_enabled() const noexcept { return source_gain_enabled_; }
    // Option C (docs/architecture/original-shading-critique.md 1a): fill in
    // linear light inside the original hull pixel programs, finite 0..0.5, 0
    // is off (no variant is created). Excludes linear materials; configure
    // after configure_linear_materials and before attach.
    void configure_original_fill(float fill) noexcept;
    bool original_fill_requested() const noexcept { return original_fill_requested_; }
    // Hull self-illumination gain (hull-self-illumination.md 5,
    // X3M_HULL_LIGHTMAP_GAIN): the fill variant of the 100 reviewed light-map
    // programs plus one MUL of the sampled light map by G, finite 1..8, 1 is
    // off (no variant is created). Excludes linear materials; needs HDR only;
    // configure after configure_linear_materials and configure_original_fill,
    // before attach. Ctrl+Shift+F4 switches this gain alone
    // (hull_lightmap_enabled_); the guide lights follow Ctrl+Shift+F6.
    void configure_hull_lightmap_gain(float gain) noexcept;
    bool hull_lightmap_gain_requested() const noexcept { return hull_lightmap_gain_requested_; }
    // Light-map far fade (X3M_LIGHT_MAP_FAR_FADE=P0,P1[,G]; call after
    // configure_hull_lightmap_gain): the gain variants read the per-draw gain
    // from c217.w (no DEF) and every routed draw of a gain pair uploads
    // fade_route::lightmap_far_gain there with the motion ABI's own two
    // vectors (no additional constant write). Finite 0 < P0 < P1, G in
    // [0, gain]; anything else, or no gain, leaves the option off and the
    // programs and uploads byte for byte the constant-gain ones.
    bool configure_lightmap_far_fade(float p0, float p1, float floor) noexcept;
    bool lightmap_far_fade() const noexcept { return lightmap_far_fade_; }
    // Hull emissive widening (X3M_HULL_EMISSIVE_WIDENING=K[,B]; call after
    // configure_hull_lightmap_gain, before attach; docs/architecture/
    // hull-emissive-widening.md "As built (R1 + R2)"): beside every gained
    // light-map variant a widened one whose light-map fetch is the block of
    // linear_material.h (k = clamp(K . texels per pixel, 1, K) per pixel from
    // the light map's own footprint, the axis-separated gate, the boost B);
    // every routed draw of a gain pair uploads ((W K)^2, (H K)^2) of the
    // light-map stage's shadowed level-0 size in c217.yz with the motion ABI's
    // own two vectors, and the route binds the widened variant on every
    // opaque, non-alpha-tested gain draw whose light-map stage holds a mip
    // chain of known size (the block itself is the texld bit for bit at
    // k = 1), raising the stage's MINFILTER to ANISOTROPIC for the draw when
    // the shadowed value is anything else (restored by undo). Finite K in
    // (1, 8], B in [1, K]; anything else, or no gain, leaves the option off
    // (no variant, c217.yz stay 0).
    bool configure_hull_emissive_widening(float k, float b) noexcept;
    bool hull_emissive_widening() const noexcept { return lightmap_widen_; }
    void configure_linear_distance_fade(bool requested) noexcept;
    // Step C of docs/architecture/screen-emission-region.md: the packed screen
    // bracket (policy 8) for the nine SM1 screen pairs of
    // screen_emission_admission.h. Configure before attach; needs the linear
    // material route (the fade prerequisites). Default off. `gain` is the
    // step E composition gain g (X3M_SCREEN_EMISSION_GAIN, default 1: the
    // composed bullet is native by construction), handed to the pass before
    // its attach; the caller validates the range.
    void configure_screen_emission(bool requested, float gain = 1.f) noexcept;
    bool screen_emission_requested() const noexcept { return screen_emission_requested_; }
    float screen_emission_gain() const noexcept { return screen_emission_gain_; }
    // Additive option (screen-emission-region.md, "Additive option"): the same
    // nine pairs in the same exact native screen state draw in place with
    // DESTBLEND ONE (ADD/ONE/ONE) and, for gain != 1, a colour-gained PS2
    // variant created at registration; no bracket, no copies, no bound. Needs
    // the FP16 redirect (X3M_HDR) and the motion-output hooks; exclusive with
    // the packed route (the caller refuses both; whichever of the two
    // configure calls runs second, the packed route wins). `gain` finite 1..8.
    // `alpha` is the per-source bloom attenuation k of
    // docs/architecture/bloom-per-source-attenuation.md (option 1, blend-state
    // form), finite 0..1, applied only when `alpha_requested`. Absent (the
    // default) leaves the alpha law at today's `a + D.a` and touches no alpha
    // state per draw; an out-of-range k is dropped the same way.
    void configure_screen_emission_additive(bool requested, float gain,
        bool alpha_requested = false, float alpha = 1.f) noexcept;
    bool screen_emission_additive_requested() const noexcept { return screen_additive_requested_; }
    // Runtime A/B of the additive option (Ctrl+Shift+F5): off leaves the draw
    // in its native screen blend with the native program, exactly like a
    // refused draw. Returns the new state, or -1 when the option was not
    // requested or its gain is 1 (no variant exists): a logged no-op.
    int screen_emission_additive_toggle() noexcept;
    bool screen_emission_additive_enabled() const noexcept { return screen_additive_enabled_; }
    // Diagnostic fade-region witness (X3M_FADE_WITNESS=<k>, note section 7,
    // step 1): every k-th frame without an admitted emission draw the M
    // coverage target is read back once and its covered pixels counted
    // against the union of that frame's derived rectangles. Off (0) costs
    // nothing per draw or per frame.
    void configure_fade_witness(unsigned frames) noexcept;
    // Fade-band motion arm threshold (X3M_FADE_ROUTE=<permille>, default
    // 500; fade_route::threshold_off disables the arm): a reviewed pair drawn
    // in the exact fade-band state routes (own RT1 motion, RT2 masked, no
    // fade bracket, no M coverage) when its fade fraction estimate reaches
    // the threshold. Configure before attach.
    void configure_fade_route(unsigned threshold_permille) noexcept;
    unsigned fade_route_threshold() const noexcept { return fade_route_threshold_; }
    // Diagnostic distant-shimmer trace (X3M_SHIMMER_TRACE=1, off by default;
    // docs/architecture/linear-distance-fade-region.md, "Shimmer trace"):
    // every frame records the identity of the Asteroid-class scene draws into
    // a fixed per-frame array and logs them plus the frame's TAA state after
    // Present. Off costs one predicate per draw and nothing else.
    void configure_shimmer_trace(bool requested) noexcept;
    // X3M_SCREEN_EMISSION_TIMING=1 with the screen-emission route: one
    // screen_emission_frame diagnostic line per Present (packed admissions,
    // bracket pixels and the wall-clock frame time). Off costs one predicate
    // per Present; on, one QPC and one log call.
    void configure_screen_emission_timing(bool requested) noexcept;
    bool composition_requested() const noexcept { return linear_emission_requested_ || distance_fade_requested_ || screen_emission_requested_; }
    // The blend-state shadow (SRCBLEND/DESTBLEND/BLENDOP/SEPARATEALPHA) is fed
    // for the composition producers and for the source-gain admission.
    bool blend_shadow_requested() const noexcept { return composition_requested() || emission_source_gain_requested_ || hull_emission_gain_requested_ || screen_additive_requested_ || fog_cards_replace_; }
    bool composition_operation_active() const noexcept { return composition_busy_; }
    bool draw_submission_blocked() const noexcept { return composition_busy_ || composition_state_lost_ || motion_state_lost_; }
    void configure_mip_bias(float bias) noexcept;
    float mip_bias() const noexcept { return mip_bias_; }
    bool mip_bias_active() const noexcept { return mip_bias_bits_ != 0 && jitter_requested_; }
    // After a successful application SetTexture / SetSamplerState (light
    // hooks). `levels` is the texture's level count when `queried` (the hook
    // asks the texture once per pointer change, inside its native section).
    void set_texture(DWORD stage, IDirect3DBaseTexture9* texture, DWORD levels, bool queried, int reader = 2, DWORD width = 0, DWORD height = 0, std::uint64_t identity = 0) noexcept;
    int composition_texture_reader(DWORD stage, IDirect3DBaseTexture9* texture) noexcept; // native CPU section
    void before_texture_write(IDirect3DBaseTexture9* texture) noexcept;
    bool texture_levels_wanted(DWORD stage, IDirect3DBaseTexture9* texture, std::uint64_t identity = 0) const noexcept;
    // Hull emissive widening: the SetTexture hook reads the proxy's resource identity only for the two light-map
    // stages the transform uses (s2 DEFAULT, s3 BUMPMAP: the coverage JSON) and only when the pointer differs from the
    // shadow or the shadow's identity is unknown; the identity keys the size shadow with the pointer.
    bool texture_identity_wanted(DWORD stage, IDirect3DBaseTexture9* texture) const noexcept;
    // Hull emissive widening: the level-0 size of a newly bound 2D texture is
    // read once per pointer change beside its level count (GetType,
    // GetLevelDesc(0), both documented; 0 x 0 for a cube, volume or failed
    // query), by the SetTexture hook and resync_samplers; never per draw.
    bool texture_size_wanted() const noexcept { return lightmap_widen_; }
    static void texture_level0_size(IDirect3DBaseTexture9* texture, DWORD& width, DWORD& height) noexcept;
    void set_sampler_state(DWORD stage, D3DSAMPLERSTATETYPE type, DWORD value) noexcept;
    void before_set_sampler_state(DWORD stage, D3DSAMPLERSTATETYPE type) noexcept;
    void sampler_state_failed(DWORD stage, D3DSAMPLERSTATETYPE type) noexcept;
    // X3M_FRAME_TIMING only: the bindings the per-draw pair and batchability
    // counters read (src/proxy/frame_timing.h). Pure shadow reads, no Get*
    // call and no device access; `valid` is false when the shadow is not live
    // (the route is off, or a state block is recording) and the draw is then
    // not classified. Program identities are the proxy's bytecode hashes, the
    // buffer and declaration identities the shadow's own.
    struct BindingShadow {
        std::uint64_t vs_hash = 0, ps_hash = 0;
        std::uint64_t stream0 = 0, indices = 0, declaration = 0;
        std::uint64_t textures[4]{}; // stage 0..3 pointers
        bool valid = false;
    };
    BindingShadow binding_shadow() const noexcept;
    // X3M_TAA_SHARPEN in [0, 1]: post-resolve RCAS of the display image
    // (docs/architecture/temporal-integration.md "Post-resolve sharpen"); 0.75
    // by default with the TAA resolve, and an explicit 0 leaves both routes
    // bit-identical to the unsharpened ones. On
    // the 8-bit route the pass draws it in place of the copy-back; on the HDR
    // route the write-back's sharpened program draws it (configure_hdr's
    // HdrConfig::sharpen carries the same value to the pass).
    void configure_taa_sharpen(float sharpness) noexcept { taa_sharpen_ = sharpness; }
    // Partial sun occlusion, step 1 (X3M_SUN_OCCLUSION / _LOG / _RADIUS / _CURVE;
    // docs/architecture/sun-partial-occlusion.md). begin / end are the lens bracket's
    // listener (src/proxy/sun_occlusion.h, the engine's call 0x00472491): begin runs the
    // 1x1 visibility pass against this frame's RT2, prepare_lens wraps one lens-scene
    // draw (the draw hooks call it only while sun_occlusion::bracket_open()).
    // radius_u: the disc's half-width as a fraction of the back-buffer width (X3M_SUN_OCCLUSION_RADIUS; the
    // engine's record size saturates for the sun, so the radius is configured, not derived).
    // core_fraction (X3M_SUN_OCCLUSION_CORE_F, default on; =0 = clipped only): a clipped core body is also multiplied by f, like the ghosts.
    struct SunOcclusionConfig { bool requested = false, log = false; float radius_u = sun_occlusion::core::radius_default_u, curve = 1.f; bool core_fraction = false; };
    void configure_sun_occlusion(const SunOcclusionConfig& config) noexcept { sun_occlusion_ = config; }
    void sun_occlusion_begin() noexcept;
    void sun_occlusion_end() noexcept;
    void prepare_lens(const MotionDrawCall& call, MotionRoute& route) noexcept;
    // X3M_TAA_CURRENT_FILTER (A of the resolve's filtered current sample, 0
    // off) and X3M_TAA_HISTORY_WEIGHT (c5.z, default 0.9); both validated by
    // the caller and read at the pass's initialisation / every resolve.
    void configure_taa_resolve(float current_filter, float history_weight) noexcept { taa_current_filter_ = current_filter; taa_history_weight_ = history_weight; }
    // Flicker suppression (docs/architecture/taa-flicker-suppression.md), all
    // off by default: X3M_TAA_THIN_CLIP (S, 0..1), X3M_TAA_ADAPTIVE_WEIGHT
    // (WMAX[,LO,HI]; refused at the pass's initialisation without the thin
    // clip, below the history weight, or on a device without two render
    // targets of independent bit depths) and X3M_TAA_ALPHA_HISTORY (HDR route
    // only). With all three off the pass never creates the variant programs.
    // X3M_TAA_LINE_FILTER (A of the line-masked filtered current sample, 0 off;
    // docs/architecture/taa-lattice-crawl.md section 9). Before attach, like the others.
    void configure_taa_line_filter(float a, unsigned width) noexcept { taa_line_filter_ = a; taa_line_width_ = width == 2 ? 2u : 1u; }
    // X3M_TAA_FAR_STABILISER=W[,A[,F0,F1[,LO,HI]]] (docs/architecture/taa-distant-line-fade.md
    // sections 9-10), off by default: far history weight W (0 off), far current
    // filter A (0 off), gate footprints F0 < F1 in world units per pixel, speed
    // gate of the weight LO < HI in px/frame.
    void configure_taa_far(float weight, float filter, float f0, float f1, float lo, float hi) noexcept { taa_far_weight_ = weight; taa_far_filter_ = filter; taa_far_f0_ = f0; taa_far_f1_ = f1; taa_far_lo_ = lo; taa_far_hi_ = hi; }
    // X3M_TAA_THIN_REGION=W[,RELAX[,LO,HI]] (docs/architecture/taa-lattice-crawl.md
    // section 13), off by default: history weight W on fragmented-depth regions
    // (0 off), clip relaxation RELAX (1 = clip off there), speed gate LO < HI
    // px/frame. Runs on the far-stabiliser program and shares its speed gate.
    // camera_gate: X3M_TAA_THIN_REGION_GATE=camera (section 32.1), the
    // camera-relative gate with the 7x7 box clip; off = the screen-speed gate.
    // emissive: X3M_TAA_THIN_REGION_EMISSIVE=E (thin-glow-lines.md 8.3 R3;
    // taa-lattice-crawl.md section 32.7), the emissive vote in the mask (0 off);
    // turned off at initialisation with the thin region off, and the mask is then
    // bit for bit what it was. E is in the units of the scene the pass binds, so
    // E >= 1 needs the HDR route (the 8-bit route's copy never exceeds 1).
    void configure_taa_thin_region(float weight, float relax, float lo, float hi, bool gate_given, bool camera_gate = false, float emissive = 0.f) noexcept { taa_thin_weight_ = weight; taa_thin_relax_ = relax; taa_thin_camera_gate_ = camera_gate; taa_thin_emissive_ = emissive; if (gate_given) { taa_far_lo_ = lo; taa_far_hi_ = hi; } }
    // X3M_TAA_SENTINEL_STABILISER=S[,E] (docs/architecture/temporal-integration.md
    // "Distant unrouted stations under a pan"), off by default: thin-region
    // strength S of unrouted depth-sentinel pixels through the camera gate, box
    // clipped; emitter bound E (0 none). Turned off at initialisation without
    // the camera gate or the separable box programs.
    void configure_taa_sentinel(float strength, float emitter) noexcept { taa_sentinel_strength_ = strength; taa_sentinel_emitter_ = emitter; }
    void configure_taa_flicker(float thin_clip, float adaptive_weight, float adaptive_lo, float adaptive_hi, bool alpha_history) noexcept {
        taa_thin_clip_ = thin_clip; taa_adaptive_weight_ = adaptive_weight; taa_adaptive_lo_ = adaptive_lo; taa_adaptive_hi_ = adaptive_hi; taa_alpha_history_ = alpha_history;
    }
    // Volumetric sun fog (X3M_VOLUMETRIC_FOG=1; docs/architecture/volumetric-fog.md,
    // spatial family implementation): after sun apply, before resolve.
    // `strength` is density tuning S/.02; `anisotropy` is Henyey-Greenstein g;
    // `everywhere` explicitly forces debug bluewell for unknown families; `timing`
    // logs one volumetric_fog_frame line per frame. Off: one branch per scene end
    // and one hash compare per pixel-shader bind are skipped entirely.
    void configure_volumetric_fog(bool requested, float strength, float anisotropy, bool everywhere, bool timing, bool replace_cards = false) noexcept {
        fog_requested_ = requested; fog_strength_ = strength; fog_anisotropy_ = anisotropy; fog_everywhere_ = everywhere; fog_timing_ = timing; fog_cards_replace_ = requested && replace_cards;
    }
    // X3M_VOLUMETRIC_FOG_RANGE=stored (fog-density-runtime-integration.md): the two-level
    // stored-density field out to 30-40 km instead of the family atlas. Off (legacy): one
    // branch at the owner latch and one at the scene end, nothing else exists. A capability
    // refusal logs one volumetric_fog_cache line and keeps the legacy path.
    void configure_volumetric_fog_range(bool stored) noexcept { fog_density_requested_ = fog_requested_ && stored; }
    // The single stored-density look (fog_look_math.h): its tuning, read once at init.
    void configure_volumetric_fog_look(const renderer::FogLookTuning& tuning) noexcept {
        fog_density_config_.look = tuning;
    }
    // X3M_FOG_SHADOW_PASS=1 (fog-shadow-pass.md): the sun-visibility grid pass of the stored range, read once at init.
    void configure_volumetric_fog_shadow_pass(bool on) noexcept { fog_shadow_pass_launch_ = on; fog_density_config_.shadow_pass = on; }
    // Ctrl+Shift+F11 (comparison-hotkeys.md; launched with the pass only): flips the grid variant the next owner latch
    // hands to FogPass::prepare_density, so every frame draws one variant and off is the launch-off in-march path (the
    // grid target stays allocated). One fog_shadow_pass_toggle line per press; returns the new state, -1 without the pass.
    int volumetric_fog_shadow_pass_toggle() noexcept;
    // DllMain DLL_PROCESS_DETACH only (FogPass::abandon_density_worker): no join, no lock, no log.
    void abandon_volumetric_fog_worker() noexcept { if (fog_) fog_->abandon_density_worker(); }
    // Ctrl+Alt+F9 toggles the pass, Ctrl+Alt+F10 steps the strength through
    // renderer::fog_strength_steps (comparison-hotkeys.md). One
    // volumetric_fog_toggle / volumetric_fog_strength line per press. -1: option off.
    void volumetric_fog_sector_sample(std::uint64_t frame, const sector_background::Sample&) noexcept;
    void volumetric_fog_begin_frame() noexcept; // after comparison hotkeys
    int volumetric_fog_toggle() noexcept;
    int volumetric_fog_step() noexcept;
    // FPS overlay second line: -1 option off, else (enabled, strength in 1/1000, current active family).
    int volumetric_fog_overlay_state() const noexcept {
        return !fog_requested_ ? -1 : int(fog_enabled_) | int(fog_sector_.current(frame_) && !fog_cards_.fault) << 1 | int(fog_strength_ * 1000.f + .5f) << 2;
    }
    float volumetric_fog_strength() const noexcept { return fog_strength_; }
    // Ctrl+Shift+F12 (comparison-hotkeys.md, "Sun shadows at rest"): the
    // at-rest A/B of the sun shadows. Off, the scene end runs neither the
    // cascade/single-map replay transaction (no map cleared or drawn, no
    // retained caster issued) nor the apply quad; everything else (the lane,
    // the candidate counter, TAA, capture) is unchanged. Every retained basis
    // is dropped on both edges, so the first frame back on replays every
    // cascade instead of publishing a stale map, the far one included whatever
    // the budget's alternate-frame rule would say. Returns the new state
    // (1 on / 0 off), or -1 on a device with neither the replay nor the apply
    // requested (a logged no-op, nothing changed); one bool test at the scene
    // end, nothing per draw.
    int sun_shadow_toggle() noexcept;
    bool sun_shadow_enabled() const noexcept { return sun_shadow_enabled_; }
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
                         IDirect3DPixelShader9* sun_motion_variant = nullptr;
                         IDirect3DPixelShader9* sun_material_variant = nullptr;
                         IDirect3DPixelShader9* sun_xt_variant = nullptr;
                         bool sun_extraction = false;
                         // Original-shading share producer (legacy-sun-application.md
                         // section 1): the motion/depth variant composed with the
                         // --original-fill K plus the code-value share in oC2.g. PS
                         // only; created once at registration with the lane on and
                         // linear materials off; null is the fail-closed refusal.
                         IDirect3DPixelShader9* sun_original_variant = nullptr;
                         // The same share variant composed with the hull light-map gain
                         // (X3M_HULL_LIGHTMAP_GAIN); selected over sun_original_variant while
                         // the F4 flag is on; null without the option or the term.
                         IDirect3DPixelShader9* sun_original_lightmap_variant = nullptr;
                         // The widened forms of the two gained variants above
                         // (X3M_HULL_EMISSIVE_WIDENING); null without the option or the term.
                         IDirect3DPixelShader9* sun_original_lightmap_widen_variant = nullptr;
                         IDirect3DPixelShader9* hull_lightmap_widen_variant = nullptr;
                         std::uint8_t hull_lightmap_stage = 0; // the light-map sampler stage (2/3) of a widened program; 0 = none
                         // XT DEFAULT is pair-specific: the shared VS retains
                         // its generic objects for every earlier exact pair.
                         IUnknown* xt_default_ordinary_variant = nullptr;
                         IDirect3DVertexShader9* xt_default_linear_variant = nullptr;
                         IDirect3DPixelShader9* emission_variant = nullptr;
                         IDirect3DPixelShader9* source_gain_variant = nullptr; // colour-MUL variant at the source gain (PS only; the VS stays original)
                         // Hull-emitter gain (emitter plan phase 3): the whole-output
                         // variant of one of the twelve hull programs, keyed on the PS
                         // alone; hull_program marks a covered original whether or not
                         // its variant was created (a covered draw without one is counted).
                         IDirect3DPixelShader9* hull_gain_variant = nullptr;
                         bool hull_program = false;
                         IDirect3DPixelShader9* original_fill_variant = nullptr; // motion variant plus the option C fill block (PS only)
                         IDirect3DPixelShader9* hull_lightmap_variant = nullptr; // the fill variant (K, or the motion variant at K=0) plus the light-map gain MUL (PS only)
                         IDirect3DPixelShader9* screen_variant = nullptr; // step C packed producer (PS only; the VS stays original)
                         IDirect3DPixelShader9* screen_additive_variant = nullptr; // additive option, gain != 1 only (AdditiveGain)
                         IUnknown* distance_fade_variant = nullptr;
                         bool registered = false; // Valid original, independent of motion support.
                         const renderer::MotionOutputProfile* row = nullptr;
                         // Depth-only prepass program (depth_prepass_profiles.h):
                         // jittered like a row's VS, never routed. Exclusive with row.
                         const renderer::DepthPrepassProfile* prepass = nullptr;
                         // LightDir_Dir0's float register from the program's own
                         // constant table (caster counter on; -1: none or beyond
                         // the shadowed c0..c31): the sun is at c4, c5 or c0
                         // depending on the program.
                         std::int8_t sun_register = -1;
                         std::uint8_t major = 0; // version token major (0 until registered)
                         bool depth_out = false; }; // PS: renderer::pixel_program_writes_depth, one walk at registration
    struct Shadow {
        IDirect3DVertexShader9* vs = nullptr;
        IDirect3DPixelShader9* ps = nullptr;
        IDirect3DPixelShader9* ps_sun_motion = nullptr;
        IDirect3DPixelShader9* ps_sun_material = nullptr;
        IDirect3DPixelShader9* ps_sun_xt = nullptr;
        bool ps_sun_extraction = false;
        // The bound PS's original share variant and whether the bound VS/PS
        // is a reviewed pair with both motion variants under original shading
        // (refreshed with the pair identities, never at a draw).
        IDirect3DPixelShader9* ps_sun_original = nullptr;
        IDirect3DPixelShader9* ps_sun_original_lightmap = nullptr; // the bound PS's gained share variant (light-map gain)
        bool original_share_pair = false;
        bool original_share_refused = false; // reviewed original pair whose share producer refused: fill/motion variant, frame failed
        std::uint64_t vs_hash = 0, ps_hash = 0;
        bool vs_registered = false, ps_registered = false;
        bool fog_card_pair = false, fog_card_source = false;
        bool ps_depth_out = false; // the bound PS writes oDepth / texdepth (ShaderEntry::depth_out)
        std::uint8_t vs_major = 0, ps_major = 0; // the bound programs' shader-model major versions (0: unbound or unknown)
        std::int8_t ps_sun_register = -1; // the bound PS's LightDir_Dir0 register (ShaderEntry::sun_register)
        IDirect3DPixelShader9* ps_emission_variant = nullptr;
        IDirect3DPixelShader9* ps_source_gain_variant = nullptr;
        // The bound PS's source-gain variant when the bound VS/PS is one of
        // the twenty reviewed pairs; null otherwise (one pointer test per draw).
        IDirect3DPixelShader9* source_gain_eligible_variant = nullptr;
        unsigned source_gain_pair = renderer::linear_emission_pair_count; // registry index of the bound pair (first-admission log)
        // Hull-emitter gain: the bound PS is a covered hull program (one bool
        // test per draw; false without the option) and its variant (null when
        // creation failed: counted refused_variant at the draw).
        bool ps_hull_program = false;
        IDirect3DPixelShader9* ps_hull_gain_variant = nullptr;
        // Original fill: the bound PS's fill variant and whether the bound
        // VS/PS is a reviewed linear-material pair with both motion variants
        // (refreshed with the pair identities, never at a draw).
        IDirect3DPixelShader9* ps_original_fill_variant = nullptr;
        bool original_fill_pair = false;
        // Hull light-map gain: the bound PS's gained variant and the same
        // reviewed-pair predicate (refreshed with the pair identities).
        IDirect3DPixelShader9* ps_hull_lightmap_variant = nullptr;
        bool hull_lightmap_pair = false;
        // Hull emissive widening: the widened forms of the bound PS's gained
        // variants and the light-map stage they read (refreshed with the pair).
        IDirect3DPixelShader9* ps_hull_lightmap_widen = nullptr;
        IDirect3DPixelShader9* ps_sun_original_lightmap_widen = nullptr;
        std::uint8_t hull_lightmap_stage = 0;
        IDirect3DPixelShader9* ps_screen_variant = nullptr;
        // Exact SM1 screen pair (screen_emission_admission.h) and its created
        // packed producer; shader eligibility only, admission is per draw.
        bool screen_pair = false;
        IDirect3DPixelShader9* screen_eligible_variant = nullptr;
        // Additive option: the same pair identity keyed on its own request
        // (never joins the packed route's counters) and its gained PS.
        bool screen_additive_pair = false;
        unsigned screen_additive_index = screen_emission::pair_count; // table index of the bound pair (per-frame telemetry mask)
        IDirect3DPixelShader9* ps_screen_additive_variant = nullptr;
        IDirect3DVertexShader9* vs_fade_variant = nullptr;
        IDirect3DPixelShader9* ps_fade_variant = nullptr;
        std::uint32_t fade_sampler_mask = 0; // exact six-pair contract, independent of creation readiness
        // Fade-band arm identity: one of the seven fade pairs whose VS has a
        // known g_AlphaValue/g_FogClip register pair (fade_route_core.h).
        // Refreshed with the other pair identities, never at a draw.
        bool fade_route_pair = false;
        fade_route::Registers fade_route_registers{};
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
        bool cutout_pair = false; // identity (cutout::pair) independent of variant, capability and linear-material availability
        // Asteroid-family pair identity for the shimmer trace only (the six
        // distance-fade pairs of the material tables). Refreshed with the
        // other pair identities, never at a draw, and only while the trace is on.
        bool asteroid_pair = false;
        const renderer::MotionOutputProfile* vs_row = nullptr;
        const renderer::DepthPrepassProfile* vs_prepass = nullptr; // jitter-only clip rows (no pair, never routes)
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
        // Pool class of the bound buffers (candidate counter on only; Unknown otherwise).
        shadow_replay::PoolClass stream0_pool = shadow_replay::PoolClass::Unknown, indices_pool = shadow_replay::PoolClass::Unknown;
        DWORD fill_mode = 0;          // D3DRS_FILLMODE, kept only with composition requested
        bool fill_mode_known = false;
        std::uint32_t position_offset = 0, position_type = 0;
        // Every element of the bound declaration reads stream 0 (from the same
        // GetDeclaration read that hashes it): the depth lease's multistream
        // verdict, once per SetVertexDeclaration instead of per leased draw.
        bool declaration_stream0_only = false;
        renderer::Surface rt0, depth;
        bool extra_rt[4]{};
        renderer::Viewport viewport;
        bool recording = false;
        DWORD states[motion_shadow_state_count]{};      // application render states (shadow_states order)
        bool states_known[motion_shadow_state_count]{};
        // SRCBLEND, DESTBLEND, BLENDOP, SEPARATEALPHABLENDENABLE. The first
        // three are the nine-state fade check's blend triple; the fourth is
        // logged by the capture-only motion_route line and is not part of it.
        // SRCBLEND, DESTBLEND, BLENDOP, SEPARATEALPHABLENDENABLE, then
        // SRCBLENDALPHA, DESTBLENDALPHA, BLENDOPALPHA and BLENDFACTOR
        // (composition_blend_index); the last five are what the additive
        // option's alpha attenuation restores after its draw.
        DWORD composition_blend[8]{};
        bool composition_blend_known[8]{};
    };
    struct SavedState;
    template<typename Fn> Fn native(unsigned slot) const noexcept { return reinterpret_cast<Fn>(native_[slot]); }
    bool sun_lane_self_test(D3DFORMAT depth_format, char* reason, std::size_t reason_size) noexcept;
    void qualify_sun_lane() noexcept;
    void publish_sun_lane(const char* source) noexcept;
    bool sun_lane_requested_=false, sun_lane_qualified_=false, sun_lane_active_=false, sun_lane_failed_=false;
    bool sun_owner_valid_=false; // publish_sun_lane's owner term of this frame (the apply quad's precondition)
    unsigned sun_original_variants_=0, sun_original_refused_=0; // original share producer: created / refused (fail closed to the fill or motion variant)
    unsigned sun_original_refused_draws_=0; // this frame's routed depth writers of a share-refused reviewed pair (frame failed)
    // Scene-end apply pass (sun_shadow_apply_pass.h): attached once per device
    // epoch for the FP16 target (a refusal is final until Reset), run once per
    // frame after the depth replay. Storage only: no per-draw cost.
    bool sun_apply_requested_=false, sun_apply_attach_failed_=false, sun_apply_applied_=false, sun_apply_attempted_=false;
    bool sun_shadow_enabled_=true; // Ctrl+Shift+F12: the scene-end replay/apply gate (on until a press)
    bool sun_shadow_force_replay_=false; // the frame back on replays every cascade, whatever the budget's parity rule says
    double sun_apply_bias_units_=renderer::sun_shadow_bias_units_default;         // world units; resolved per frame with the cascade (sun_shadow_apply_bias)
    double sun_apply_clamp_texels_=renderer::sun_shadow_bias_clamp_texels_default; // world texels; the receiver-plane clamp and non-planar fallback
    double sun_apply_slope_texels_=renderer::sun_shadow_bias_slope_texels_default; // texels of the plane's depth slope; the cascade program's slope-scaled margin
    std::unique_ptr<renderer::SunShadowApplyPass> sun_apply_;
    HRESULT sun_apply_attach_result_=S_FALSE;
    std::uint64_t sun_apply_frame_=~std::uint64_t(0);
    unsigned sun_apply_logs_=0;
    // Sun-shadow apply cost, as the shadow frame line reports it (apply_us=,
    // apply_cascades=): the QPC around the pass's submission and the number of
    // cascade maps the quad actually sampled. The apply runs after the replay of
    // the same frame, so the frame line carries the previous frame's apply, and
    // zeros when that frame ran no apply (`sun_apply_frame_` says which).
    double sun_apply_us_=0.;
    unsigned sun_apply_sampled_=0;
    void run_sun_shadow_apply() noexcept;
    bool ensure_sun_shadow_apply() noexcept;
    // Partial sun occlusion (motion_output_sun_occlusion_inc.h). Storage only outside the lens bracket.
    SunOcclusionConfig sun_occlusion_{};
    std::unique_ptr<renderer::SunOcclusionPass> sun_occlusion_pass_;
    bool sun_occlusion_attach_failed_ = false;
    bool lens_frame_active_ = false, lens_suppress_ = false; // this bracket: draws are wrapped / dropped (the override answered but no fraction exists)
    std::uintptr_t lens_record_ = 0;                          // the record the fraction belongs to (another one seeds)
    std::uint64_t lens_pass_qpc_ = 0;
    sun_occlusion::core::Hold lens_hold_{};                   // a skipped pass keeps the last fraction for at most four frames
    unsigned lens_draws_ = 0, lens_wrapped_ = 0, lens_clipped_ = 0, lens_refused_ = 0, lens_dropped_ = 0, lens_other_ = 0;
    // Step 2: this bracket's RT2 (one reference, begin .. end), its size and the sun's uv for the body classification.
    IDirect3DTexture9* lens_depth_ = nullptr;
    unsigned lens_depth_width_ = 0, lens_depth_height_ = 0;
    float lens_sun_u_ = .5f, lens_sun_v_ = .5f;
    bool lens_chain_drawn_ = false;                           // a bracket with draws ran this frame (the Present-time back-buffer readback under the log)
    bool ensure_sun_occlusion() noexcept;
    void finish_lens(MotionRoute& route) noexcept;
    void release_lens_depth() noexcept;
    void sun_lens_present_readback() noexcept;
    D3DFORMAT sun_lane_depth_formats_[3]{};
    unsigned sun_lane_depth_count_=0;
    bool sun_lane_depth_qualified(D3DFORMAT format) const noexcept {
        for(unsigned i=0;i<sun_lane_depth_count_;++i)if(sun_lane_depth_formats_[i]==format)return true;
        return false;
    }
    renderer::SunShareFrame sun_frame_{};
    // Capped per-device cache of distinct untracked-writer signatures, each
    // logged once (sun_shadow_lane_writer); overflow counts the rest. Fixed
    // storage cleared at attach and before Reset; a linear scan of at most 64
    // entries per draw already counted untracked (lane on only).
    struct SunWriterSignature { std::uint64_t vs, ps, declaration; std::uint32_t stride; std::uint8_t reason, z_state, registered; };
    static constexpr unsigned sun_writer_capacity = 64;
    SunWriterSignature sun_writers_[sun_writer_capacity]{};
    unsigned sun_writer_count_ = 0, sun_writer_overflow_ = 0;
    void note_sun_untracked_writer(const MotionRoute& route, renderer::SunUntrackedReason reason) noexcept;
    bool sun_coverage_current_=false, sun_composition_completed_=false;
    IDirect3DPixelShader9* sun_sentinel_ps_=nullptr;
    // Invalid-share stamp (directional-shadows.md "Unroutable depth writer"): a gate-3
    // refused depth writer after a receiver is re-issued once with the application's own
    // VS/geometry, ZFUNC EQUAL, no depth write, RT0/RT1 masked and RT2 masked to .g,
    // writing share -1 over exactly the pixels it won. [0] ps_2_0 (SM1/SM2 originals),
    // [1] ps_3_0. A stamp that cannot run or fails keeps the frame veto.
    IDirect3DPixelShader9* sun_stamp_ps_[2]{};
    bool sun_stamp_ps_failed_[2]{}; // created on first use; a failed create is not retried on this device
    MotionDrawCall sun_stamp_call_{};
    unsigned sun_stamp_prims_=0; // primitives re-issued by this frame's successful stamps (flight sanity figure)
    unsigned sun_stamps_=0, sun_stamp_refused_=0; // per frame; refused: a candidate whose stamp did not run or failed (vetoes as before)
    void arm_sun_stamp(const MotionDrawCall& call, MotionRoute& route) noexcept;
    bool sun_stamp_draw(MotionRoute& route) noexcept;
    // Caster-candidate counter storage: fixed, cleared at begin_frame and after
    // publication; the witness count is per device (attach clears it).
    bool candidates_requested_=false;
    bool object_bounds_log_=false; // X3M_OBJECT_BOUNDS_LOG: object_bounds lines on capture frames (diagnostic)
    ownership::AdmissionMonitor* candidates_monitor_=nullptr;
    shadow_replay::Frame candidates_{};
    shadow_replay::PoolCache candidate_pools_{};
    unsigned candidate_witnesses_=0;
    std::uint64_t candidates_published_frame_=~std::uint64_t(0); // frame serial of the last frame line (once per frame)
    std::uint32_t candidates_line_truncated_=0; // frame lines whose cascade tail did not fit its bound (never expected; shadow_replay_candidates_truncated lines)
    float candidate_slice_near_=shadow_replay::slice0_near; // production constant; the seam fixture may lower it
    unsigned candidate_cap_=shadow_replay::record_capacity;
    // Casters by bounds (shadow-replay-gates.md): the vertex-extent cache, the
    // frame's queued extent reads (wrapper AddRef held until the scene-end
    // read or the release), and the frame's view -> sun rows for the box test
    // (computed at the first draw that needs them; the sun is this frame's
    // LightDir_Dir0 write or, before one, the previous frame's).
    shadow_replay::ExtentCache candidate_extents_{};
    shadow_replay::PendingExtent candidate_extent_reads_[shadow_replay::extent_reads_per_frame]{};
    unsigned candidate_extent_read_count_=0;
    unsigned candidate_extent_priority_count_=0; // the queue's first entries: re-reads of ranges answering stale (read first, never crowded out by new ranges)
    shadow_replay::PendingExtent object_bounds_alpha_reads_[shadow_replay::extent_reads_per_frame]{}; // X3M_OBJECT_BOUNDS_LOG: alpha-tested extents, queued after the casters
    unsigned object_bounds_alpha_read_count_=0;
    float candidate_bounds_rows_[12]{};
    renderer::ShadowCascadeBounds candidate_cascade_bounds_{}; // cascades on: every cascade's box for the one bounds pass
    int candidate_bounds_state_=0; // 0 not computed this frame, 1 valid, -1 unavailable
    // Minimum light-space footprint (X3M_SHADOW_CASCADE_MIN_FOOTPRINT;
    // renderer/shadow_cascade_footprint_core.h): the frame's resolved thresholds
    // (recomputed with the bounds latch, so a ladder commit, an FOV change or a
    // Reset re-resolves them) and the per-cascade measure of the draw in hand.
    // Off: the law is cleared, the mask test passes no measure array at all and
    // the draw path is byte-identical.
    renderer::ShadowCascadeFootprintLaw candidate_footprint_law_{};
    renderer::ShadowCascadeFootprintLaw candidate_footprint_logged_{}; // the last law the shadow_cascade_footprint line reported
    unsigned candidate_footprint_lines_=0;                             // bounded diagnostics (footprint_line_max)
    float candidate_footprint_[renderer::shadow_cascade_max]{};        // the draw's lateral sun-space footprint per cascade
    void refresh_footprint_law() noexcept;                             // the law from the live set, the camera latch and the target width
    // The frame's one validated sun (shadow_replay_sun.h): pixel float
    // registers c0..c31 shadowed as the application writes them, sampled at
    // every routed z-writing draw from the bound program's own LightDir_Dir0
    // register; the latch survives Reset (the sun is world-fixed) and is per device.
    float candidate_ps_constants_[shadow_replay::sun_register_limit][4]{};
    std::uint32_t candidate_ps_written_=0;
    shadow_replay::SunLatch sun_latch_{};
    shadow_replay::SunVerdict sun_verdict_=shadow_replay::SunVerdict::None; // this frame's, resolved once at the scene end
    std::uint32_t candidate_bounds_unavailable_=0; // this frame's extent-known draws that found no sun for the box test
    // Cascades: the sun as a polled world position, one held direction per
    // cascade (shadow_replay_sun_point.h); the latch above stays the source
    // whenever the poll is unavailable, unvalidated or implausible.
    shadow_replay::PointSun point_sun_{};
    unsigned point_sun_summary_count_=0;  // frames since the last shadow_replay_sun_point_summary line
    std::int64_t point_sun_poll_ticks_=0; // this frame's poll in QPC ticks (the draw path stays integer-only: a 64-bit conversion is x87 on i686)
    sun_light_poll::Sample point_sun_sample_{};
    shadow_replay::PointSunReason point_sun_logged_=shadow_replay::PointSunReason::Count; // the last source/reason an event line reported
    bool point_sun_trace_=false;          // X3M_SHADOW_SUN_TRACE: one shadow_sun_frame line per cascaded frame
    void poll_point_sun(const float constant[4], bool agrees) noexcept;
    const float* cascade_sun(unsigned cascade) noexcept; // the frame's sun of one cascade: the held point direction, else the latch's
    bool sample_candidate_sun(float out[4], int& reg) noexcept;
    shadow_replay::PoolClass candidate_pool_of(std::uint64_t id, IDirect3DResource9* buffer, bool vertex) noexcept;
    void note_candidate_distance(MotionRoute& route, const float* rows) noexcept;
    void note_candidate_draw(const MotionRoute& route) noexcept;
    void log_object_bounds(const MotionRoute& route, const float* rows, const float* lo, const float* hi, bool alpha_tested = false, bool stale = false) noexcept;
    void note_object_bounds_alpha_read(const shadow_replay::ExtentKey& key, std::uintptr_t identity) noexcept; // X3M_OBJECT_BOUNDS_LOG: hold an alpha-tested draw's missing extent
    void queue_object_bounds_alpha_reads() noexcept;   // ... queue the held keys behind the frame's caster reads
    void release_object_bounds_alpha_reads() noexcept; // ... drop them (with release_candidate_extents)
    bool ensure_candidate_bounds_rows() noexcept;
    void queue_candidate_extent(const shadow_replay::ExtentKey& key, std::uintptr_t identity, bool priority) noexcept;
    void read_candidate_extents() noexcept;    // the scene end: Lock READONLY through the wrapper, scan, cache, release
    void release_candidate_extents() noexcept; // drop the queue without reading (frame without scene end, Reset, teardown)
    // Count-only tested-opaque-arm bookkeeping for a cutout pair (after_draw).
    void note_cutout_opaque(const MotionRoute& route, HRESULT result) noexcept;
    void publish_shadow_replay_candidates() noexcept;
    // Depth replay storage (motion_output_shadow_replay_inc.h): the geometry
    // leases parallel to candidates_.records, the frame's sun constant as the
    // slot-109 hook last saw it, the pass and its attach verdict.
    bool depth_replay_requested_=false, depth_replay_attach_failed_=false;
    unsigned depth_replay_size_=renderer::shadow_replay_size_default;
    float depth_replay_extent_=renderer::shadow_replay_extent_default, depth_replay_depth_half_=renderer::shadow_replay_depth_half_default;
    std::unique_ptr<renderer::ShadowReplayPass> depth_replay_;
    HRESULT depth_replay_attach_result_=S_FALSE;
    // The record list's parallel arrays: the inline storage for record_capacity
    // records, or (a cascade set with more records: shadow-cascade-extents.md,
    // "Caster pool control") storage allocated once at attach, candidate_capacity_
    // entries each; the pointers select which. No allocation after attach.
    shadow_replay::DepthGeometry depth_geometry_inline_[shadow_replay::record_capacity]{};
    renderer::ShadowReplayDraw depth_draws_inline_[shadow_replay::record_capacity]{}; // the scene-end transaction's draw list (too large for the stack at 512)
    shadow_replay::DepthGeometry* depth_geometry_=depth_geometry_inline_;
    renderer::ShadowReplayDraw* depth_draws_=depth_draws_inline_;
    unsigned candidate_capacity_=shadow_replay::record_capacity;
    std::unique_ptr<shadow_replay::Record[]> candidate_records_ext_;
    std::unique_ptr<shadow_replay::DepthGeometry[]> depth_geometry_ext_;
    std::unique_ptr<renderer::ShadowReplayDraw[]> depth_draws_ext_;
    std::unique_ptr<bool[]> candidate_quiet_ext_;            // the scene end's per-record quiet verdicts beyond the inline stack array
    std::unique_ptr<std::uint16_t[]> candidate_select_scratch_; // importance drop order: one index per record (allocated while the option is on)
    std::unique_ptr<shadow_caster_class::Ring> candidate_class_ring_; // static-only cascades: the per-draw anchor ring, sized to the record capacity (allocated while the option is on)
    std::unique_ptr<shadow_replay::KeptEntry[]> candidate_kept_last_;  // importance order: last frame's kept casters (two slots per record; allocated while the option is on)
    std::unique_ptr<shadow_replay::FlipEntry[]> candidate_flip_entries_; // cascade-membership flips: the previous frame's mask per caster key (two slots per record; allocated while cascades are on)
    shadow_replay::FlipTable candidate_flips_;                           // the table over that storage (detached while cascades are off: no work, no fields)
    unsigned depth_cascade_draw_caps_[renderer::shadow_cascade_max]{}; // the per-cascade bound the draw path applies (the cap, or the record capacity under the importance order)
    std::uint8_t depth_cascade_static_mask_=0; // bit i: cascade i admits static casters only (none by default)
    std::uint8_t depth_cascade_backface_mask_=0; // bit i: cascade i replays back faces (ShadowCascadeSet::backface_mask; the texel law by default)
    double depth_cascade_class_eps_[renderer::shadow_cascade_max]{}; // per cascade the static/moving drift threshold (renderer::shadow_cascade_class_eps of the live set)
    void refresh_cascade_policy() noexcept; // the three above from depth_cascades_ (attach, and every ladder commit)
    std::uint8_t classify_candidate_static(const MotionRoute& route, const float* rows, const float* lo, const float* hi, std::uint8_t wanted, bool& miss) noexcept; // shadow_caster_class.h: bit i = static at cascade i's eps
    void note_refused_sighting(const MotionRoute& route, const ownership::BufferLockView& vb, const shadow_replay::ExtentEntry* extent) noexcept; // the static gate's refusal is still a sighting (cause 1)
    bool attach_candidate_storage() noexcept; // sizes the arrays above for depth_cascades_; false leaves the cascades off
    renderer::ShadowReplayCascade depth_cascade_{};
    renderer::ShadowReplayBasis depth_basis_{}; // basis of the last replayed frame (seam readback)
    unsigned depth_replayed_=0;                  // draws replayed on depth_replayed_frame_ (0: the map is not this frame's)
    std::uint64_t depth_replayed_frame_=~std::uint64_t(0);
    unsigned depth_refusal_logs_[shadow_replay::depth_reason_count]{};
    // Cascades (shadow-cascades.md): the configured set, this device's set
    // (the seam override and the sizes the pass kept after halving), and the
    // transaction's issue storage, allocated once at attach (sum of the caps;
    // none while cascades are off). The retained bases live in the pass.
    renderer::ShadowCascadeSet depth_cascade_config_{}, depth_cascades_{};
    std::unique_ptr<renderer::ShadowReplayIssue[]> depth_issues_;
    unsigned depth_issue_capacity_=0;
    bool depth_cascade_frame_ok_=false; // this frame's cascade transaction was not refused (the apply's precondition)
    bool depth_cascades_on() const noexcept { return depth_replay_requested_&&depth_cascades_.count!=0; }
    void run_shadow_replay_cascades(const bool* quiet) noexcept;
    // Own-ship-adaptive cascade 0 (motion_output_shadow_adaptive_inc.h): the
    // device's configured set (the seam narrowing and the halved sizes applied)
    // that E0 adapts from, the committed state, this frame's own ship (resolved
    // once per frame at its first candidate draw) and the frame's radius
    // accumulator; the node cache answers "descends from the own root" per
    // scope node without repeating the walk. Off (k = 0): none of it runs.
    float cascade_adaptive_k_=0.f, cascade_ladder_ratio_=renderer::shadow_cascade_ladder_ratio_default;
    renderer::ShadowCascadeSet depth_cascade_base_{};
    renderer::ShadowCascadeAdaptive cascade_adaptive_{};
    std::uint64_t own_ship_frame_=~std::uint64_t(0);
    std::uintptr_t own_ship_node_=0; std::uint32_t own_ship_handle_=0, own_ship_status_=0;
    float own_radius_frame_=0.f; unsigned own_draws_frame_=0;
    own_ship::Cache own_cache_{}; // (node, handle) -> descends from the root; flushed on root / epoch change (own_ship_cache.h)
    bool cascade_adaptive_on() const noexcept { return cascade_adaptive_k_>0.f&&depth_cascades_on(); }
    void resolve_own_ship() noexcept;
    bool own_ship_draw(std::uintptr_t node, std::uint32_t handle, std::uint64_t load_epoch, std::uint64_t registry_epoch) noexcept;
    void note_own_ship_draw(const shadow_replay::ExtentEntry& extent) noexcept;
    void update_adaptive_cascades() noexcept;
    void log_cascade_set(const char* reason) noexcept;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    bool fixture_own_ship_set_=false; std::uintptr_t fixture_own_ship_node_=0, fixture_own_part_node_=0; std::uint32_t fixture_own_ship_handle_=0, fixture_own_part_handle_=0;
public:
    // The seam's player ship (root node and handle) and one synthetic part (node, handle) that the walk treats as descending from it.
    void fixture_own_ship(std::uintptr_t node, std::uint32_t handle, std::uintptr_t part, std::uint32_t part_handle) noexcept {
        fixture_own_ship_set_=true; fixture_own_ship_node_=node; fixture_own_ship_handle_=handle; fixture_own_part_node_=part; fixture_own_part_handle_=part_handle; own_ship_frame_=~std::uint64_t(0);
    }
private:
#endif
    void note_depth_geometry(const MotionRoute& route, unsigned index) noexcept;
    bool fill_depth_geometry(const MotionRoute& route, shadow_replay::DepthGeometry& g) noexcept; // rows, keys, cull mode and the declaration's own reference; no lease
    void release_depth_leases() noexcept;
    bool ensure_shadow_replay_depth() noexcept;
    void run_shadow_replay_depth(const bool* quiet) noexcept;
    void log_depth_refusal(shadow_replay::DepthReason reason, const char* detail, HRESULT result, unsigned stage) noexcept;
    // Caster retention (motion_output_shadow_retention_inc.h): the configured
    // mode and this device's state (null while off: every site tests it).
    shadow_retention::Mode retention_mode_=shadow_retention::Mode::Off;
    std::uint32_t retention_age_cap_=shadow_retention::age_cap_default;
    double retention_eps_=shadow_retention::eps_default;
    bool retention_timing_=false;
    std::unique_ptr<ShadowRetention> retention_;
    bool retention_live() const noexcept { return retention_&&retention_->mode==shadow_retention::Mode::Live; }
    void attach_shadow_retention() noexcept;
    void note_retention_draw(const MotionRoute& route, const shadow_replay::Record& record, const shadow_replay::DepthGeometry& geometry, const shadow_replay::ExtentEntry* extent) noexcept;
    void retention_scene_end(bool sun_source_switched) noexcept;
    void publish_shadow_retention() noexcept;
    void log_shadow_retention_summary(bool final) noexcept;
    void flush_shadow_retention(shadow_retention::Flush reason) noexcept;
    void retention_frame_begin() noexcept;
    void drain_retention_journal() noexcept;
    void release_retention_pending() noexcept;
    void detach_shadow_retention() noexcept;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
public:
    HRESULT fixture_shadow_replay_readback(float* out, std::size_t floats, UINT* width, UINT* height, float* params, unsigned param_floats, unsigned cascade=0) noexcept;
    // Retention seam: the store's levels and cumulative counters (index list in the inc file).
    unsigned fixture_shadow_retention_stats(std::uint64_t* out, unsigned count) noexcept;
    void fixture_shadow_retention_device_lost() noexcept { flush_shadow_retention(shadow_retention::Flush::Device); } // as a failed Present reports it
private:
#endif
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
    // Fade-band arm evaluation at gate 4 (the six shadowed states already
    // read); true admits the draw as a routed fade-band draw.
    bool fade_arm_admits(MotionRoute& route, const MotionDrawCall& call, DWORD z, DWORD z_write, std::size_t window, bool loop_bounded) noexcept;
    std::uint64_t fade_identity() noexcept; // the draw's node identity for the arm's hysteresis (0: unknown)
    void mark_cutout_candidate(MotionRoute& route) noexcept;
    void report_xt_default_unavailable() noexcept;
    void report_mip_bias_game_write_failure() noexcept;
    void refresh_linear_emission_contract() noexcept;
    void prepare_composition(const MotionDrawCall&, MotionRoute&) noexcept;
    void prepare_source_gain(const MotionDrawCall&, MotionRoute&) noexcept;
    void finish_source_gain(MotionRoute&) noexcept;
    // Hull-emitter gain: ONE/ONE admission from the draw-time blend shadow,
    // the PS bind (rolled back on failure) and the restore after the draw.
    void prepare_hull_gain(const MotionDrawCall&, MotionRoute&) noexcept;
    void finish_hull_gain(MotionRoute&) noexcept;
    void log_hull_emission_draw(const MotionDrawCall&, unsigned program) noexcept; // F8 capture frames only
    // Additive option: the exact-state admission, the DESTBLEND/PS apply
    // (rolled back on a failed second step) and the restore after the draw.
    void prepare_screen_additive(const MotionDrawCall&, MotionRoute&) noexcept;
    HRESULT apply_screen_additive_alpha() noexcept;
    HRESULT restore_screen_additive_alpha() noexcept;
    void finish_screen_additive(MotionRoute&) noexcept;
    void log_screen_additive_frame() noexcept;
    void derive_fade_region(MotionRoute&) noexcept;
    // Step-1 rectangle of the bound draw (resolve, rows, jitter, viewport,
    // fill mode, clip to the owning target); the counters, witness and log
    // stay in derive_fade_region. Shared by the admitted route and the
    // capture-only refused-draw diagnostic.
    fade_region::Region fade_rectangle(const MotionRoute& route, fade_region::Result& bound, bool& of_viewport, unsigned& permille, bool read_only,
                                       fade_region::BoundSource source = fade_region::BoundSource::Part, std::uint32_t vertex_count = 0,
                                       std::uint64_t* aabb_px = nullptr) noexcept;
    // Step B locked-prefix rectangle (screen-emission-region.md): counted and
    // logged per draw in capture frames; nothing consumes it before step C.
    void derive_prefix_region(const MotionDrawCall&, MotionRoute&) noexcept;
    // Capture-only packed-bracket luminance sample (packed_sample line).
    // `pre` selects the retained copy the readback fills: the pre copy is kept
    // whole until the post readback, so the rectangle can be compared.
    HRESULT sample_target_pixel(IDirect3DSurface9*, const renderer::Surface&, bool pre, std::int32_t x, std::int32_t y, float out[4]) noexcept;
    void sample_packed_pre(const MotionRoute&) noexcept;
    void sample_packed_post(const RECT& composed) noexcept;
    void release_packed_sample() noexcept;
    static constexpr unsigned packed_sample_cap = 4; // admitted packed draws sampled per capture frame
    // Whole-rectangle pre/post comparison of the two retained copies.
    struct PackedScan {
        HRESULT result = D3DERR_NOTFOUND;
        unsigned long pixels = 0, changed = 0; // scanned pixels (rectangle clipped to the target) and RGB-changed ones
        double max_pre = 0.0, max_post = 0.0, sum_pre = 0.0, sum_post = 0.0; // Rec.709 luminance
        std::int32_t argmax_x = 0, argmax_y = 0; // location of the post maximum
        float argmax_pre[3]{}, argmax_post[3]{};
    };
    HRESULT scan_packed_rect(const fade_region::Rect&, PackedScan&) noexcept;
    struct PackedSample {
        bool valid = false;
        unsigned sampled = 0; // this frame's samples (reset in begin_frame)
        IDirect3DSurface9* copy = nullptr; // retained system-memory readback surface (post), one per size/format
        IDirect3DSurface9* pre_copy = nullptr; // the same for the pre image, held until the post scan
        std::uint32_t copy_width = 0, copy_height = 0, copy_format = 0;
        fade_region::Rect rect{};
        unsigned clipped = 0;
        std::uint64_t index = 0;
        std::int32_t x = 0, y = 0;
        float pre[4]{};
        HRESULT pre_result = S_FALSE;
    } packed_sample_;
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
    unsigned linear_material_refusal() noexcept;
    HRESULT bind_variant_pair(MotionRoute& route, bool material) noexcept;
    // Timed wrappers over the native SetRenderTarget/COLORWRITEENABLE calls of
    // the per-draw path and the lazy flush; each counts into counters_.set_rt.
    HRESULT bind_target(DWORD index, IDirect3DSurface9* surface) noexcept;
    HRESULT bind_targets(MotionRoute& route) noexcept;
    // The lazy-mode flush behind restore_bindings (two unbinds).
    HRESULT flush_bindings() noexcept;
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
    // Draw-time readers (see configure_state_hooks): the shadow's flag with
    // the hooks on; with them off, one counted native read per draw.
    void begin_draw_reads() noexcept;
    bool state_known(unsigned index) noexcept;
    bool blend_known(unsigned index) noexcept;
    bool fill_mode_known() noexcept;
    bool sampler_srgb_known(unsigned stage) noexcept;
    long state_field(unsigned index) noexcept;
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
    void invalidate_taa(TaaInvalidateSite site) noexcept;
    // Logs the `taa_invalidate` line of every site recorded since the last
    // flush (frame end and frame begin; no work when nothing fired).
    void flush_taa_invalidate_log() noexcept;
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
    HRESULT acquire_restore(MotionRoute& route) noexcept;
    void release_restore(MotionRoute& route) noexcept;
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
    // Direct native path of the route's value-only calls
    // (docs/architecture/route-per-draw-cost.md, lever 1 stage A). Without the
    // ownership wrapper, or with a published admission monitor, direct_ is
    // native_ and direct_device_ is device_: the present path. With the
    // wrapper, attach fills direct_slots_ for the seven value-only slots from
    // ownership::borrowed_native_device's vtable; drop_direct() returns to the
    // aliases (release_resources, before the final logical release). The
    // borrowed device is never AddRef'd, stored elsewhere or handed out.
    static constexpr unsigned direct_slot_count = 110; // SetPixelShaderConstantF + 1
    void** direct_ = nullptr;
    IDirect3DDevice9* direct_device_ = nullptr;
    void* direct_slots_[direct_slot_count]{};
    void bind_direct() noexcept;
    void drop_direct() noexcept { direct_ = native_; direct_device_ = device_; }
    // A failing direct call: hand the result to the wrapper's observe_result so
    // device loss is observed as through the forwarder. Cold.
    HRESULT direct_failed(HRESULT hr) const noexcept;
    template<typename Fn, typename... Args> HRESULT direct_call(unsigned slot, Args... args) const noexcept {
        const HRESULT hr = reinterpret_cast<Fn>(direct_[slot])(direct_device_, args...);
        return __builtin_expect(FAILED(hr), 0) ? direct_failed(hr) : hr;
    }
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
    std::uint32_t taa_invalidate_pending_ = 0; // TaaInvalidateSite bits since the last flush
    renderer::LinearMaterialConfig linear_material_config_{};
    bool linear_emission_requested_ = false, distance_fade_requested_ = false, screen_emission_requested_ = false;
    float screen_emission_gain_ = 1.f;
    bool emission_source_gain_requested_ = false;
    float emission_source_gain_ = 1.f; // 1 = off (no variant, native bytes)
    // Runtime hotkey state, default on; never touched by a draw that does not
    // already reach the option's admission, and never used for creation.
    bool source_gain_enabled_ = true;
    // Source-gain draw accounting (per-frame line): admitted draws (total, of
    // which screen-substituted), refusals by blend state, screen draws refused
    // because the DESTBLEND substitution failed, by unknown state, by device
    // state, bind failures.
    struct { std::uint32_t admitted = 0, admitted_screen = 0, refused_blend = 0, refused_screen = 0, refused_unknown = 0, refused_state = 0, bind_failures = 0; } source_gain_counts_;
    // Per-device sample caps, one per logged reason: blend, screen_substitute_failed, bind_failed, state.
    std::uint32_t source_gain_logged_[4]{};
    std::uint32_t source_gain_pair_logged_ = 0; // bit per registry pair: first admission logged this device epoch (at most 20 lines)
    // Hull-emitter gain (emitter plan phase 3): X3M_HULL_EMISSION_GAIN=G
    // (finite 1..8, 1 = off, needs HDR only), one whole-output variant
    // per covered hull program at creation, ONE/ONE admission per draw; its
    // toggle with the effects gain, Ctrl+Shift+F6 (hull_gain_enabled_).
    // Per-frame accounting: admitted
    // draws, refusals by blend state (of which refused_opaque = blend off,
    // refused_alpha = SRCALPHA/INVSRCALPHA), covered draws without a variant,
    // ONE/ONE draws a route already took (expected 0: the routes admit no
    // ONE/ONE draw), unknown state, device state, bind failures; programs =
    // bit per admitted program.
    bool hull_emission_gain_requested_ = false;
    float hull_emission_gain_ = 1.f;
    bool hull_gain_enabled_ = true; // Ctrl+Shift+F6 with the effects gain, default on; gates entry to prepare_hull_gain only
    struct { std::uint32_t admitted = 0, refused_blend = 0, refused_variant = 0, refused_routed = 0, refused_unknown = 0, refused_state = 0, bind_failures = 0, programs = 0, refused_opaque = 0, refused_alpha = 0; } hull_gain_counts_;
    std::uint32_t hull_gain_logged_[4]{}; // per-device sample caps: blend, bind_failed, state, routed
    std::uint32_t hull_gain_program_logged_ = 0; // bit per hull program: first admission logged this device epoch (at most 12 lines)
    bool original_fill_requested_ = false; // X3M_ORIGINAL_FILL=K (finite 0..0.5), exclusive with linear materials
    float original_fill_ = 0.f;
    std::uint32_t original_fill_draws_ = 0; // routed draws that bound the fill variant this frame (frame line only)
    bool hull_lightmap_gain_requested_ = false; // X3M_HULL_LIGHTMAP_GAIN=G (finite 1..8, 1 = off), exclusive with linear materials
    float hull_lightmap_gain_ = 1.f;
    bool hull_lightmap_enabled_ = true; // Ctrl+Shift+F4, default on; gates the light-map variant selection only
    bool lightmap_far_fade_ = false;            // X3M_LIGHT_MAP_FAR_FADE accepted: dynamic gain variants, c217.w per draw
    float lightmap_fade_p0_ = 0.f, lightmap_fade_inv_ = 0.f, lightmap_fade_floor_ = 1.f;
    float lightmap_fade_m00_ = 0.f;             // the far fade's own P[0] latch (0 = none); camera_scene_ stays the TAA/candidate consumers' alone
    float lightmap_fade_gain_ = 0.f;            // this draw's uploaded gain (0 when the pair has no gain variant)
    float lightmap_fade_min_ = 0.f;             // frame line: least gain drawn
    std::uint32_t lightmap_fade_draws_ = 0;     // frame line: gain draws below the configured gain
    std::uint32_t hull_lightmap_draws_ = 0; // routed draws that bound a light-map gain variant (plain or share) this frame (frame line only)
    // Hull emissive widening (X3M_HULL_EMISSIVE_WIDENING=K[,B]): K and B are
    // baked into the variants' DEFs; per draw the route uploads the bound
    // light map's texel-footprint scale ((W K)^2, (H K)^2) in c217.yz
    // (lightmap_widen_draw_scale_, 0 for a pair without a gain variant or an
    // unknown size), counts the widened draws for the frame line and the
    // session summary, and the widened variants created (fixture counter).
    bool lightmap_widen_ = false;
    float lightmap_widen_k_ = 1.f, lightmap_widen_b_ = 1.f;
    float lightmap_widen_draw_scale_[2] = {0.f, 0.f};
    DWORD lightmap_widen_draw_size_[2] = {0, 0};                         // this draw's light-map level-0 size (capture-frame draw line)
    std::uint32_t lightmap_widen_draws_ = 0, lightmap_widen_held_ = 0;  // frame line: widened draws / gain draws that kept the un-widened variant
    std::uint32_t lightmap_widen_filter_sets_ = 0, lightmap_widen_filter_reads_ = 0, lightmap_widen_filter_failures_ = 0; // frame line: MINFILTER raised / read / failed
    std::uint32_t lightmap_widen_session_filter_sets_ = 0;
    bool ensure_widen_filter(MotionRoute& route) noexcept;
    std::uint32_t lightmap_widen_session_draws_ = 0, lightmap_widen_variants_ = 0;
    bool lightmap_widen_summary_logged_ = false;
    std::uint32_t sun_original_lightmap_variants_ = 0; // gained share variants created (fixture counter)
    bool screen_additive_requested_ = false; // X3M_SCREEN_EMISSION_ADDITIVE=G (finite 1..8), exclusive with the packed route
    float screen_additive_gain_ = 1.f;
    bool screen_additive_enabled_ = true; // Ctrl+Shift+F5 runtime A/B; the variant stays created
    // Per-source bloom attenuation of the additive draw (option 1): the scene
    // alpha the bloom extract weighs by becomes k*a + D.a. 1 = off (no alpha
    // state is touched); 0 uses SRCBLENDALPHA ZERO, anything between uses
    // BLENDFACTOR with k in every lane (the colour law is ONE/ONE/ADD and
    // reads no factor, so the shared constant cannot disturb it).
    bool screen_additive_alpha_requested_ = false, screen_additive_alpha_constant_ = false;
    float screen_additive_alpha_ = 1.f;
    DWORD screen_additive_alpha_factor_ = 0xffffffffu;
    unsigned screen_additive_alpha_applied_ = 0; // steps applied to the current draw (exact partial unwind)
    // Additive draws: admitted (DESTBLEND ONE around the native draw), refused
    // (unknown/different state, PROJECTED stage 0, recording, no FP16 target,
    // missing variant) and failed applies; fixture keys 60-62.
    unsigned screen_additive_admitted_ = 0, screen_additive_refused_ = 0, screen_additive_failures_ = 0;
    unsigned screen_additive_refusal_logged_ = 0; // bit per refusal reason already logged (one line each per device)
    // Per-frame additive accounting for the telemetry line (reset every
    // Present, logged only with telemetry on): this frame's admitted and
    // refused draws and the bit mask of the nine table indices admitted.
    unsigned screen_additive_frame_admitted_ = 0, screen_additive_frame_refused_ = 0, screen_additive_frame_pairs_ = 0;
    unsigned fade_route_threshold_ = 500; // per mille; fade_route::threshold_off = arm off
    fade_route::Hysteresis fade_hysteresis_; // per node identity; cleared at Reset
    // The last routed scene draw: node identity, lifetime serial, frame and
    // draw index. The overlay arm admits a source-over sub-mesh only as the
    // very next draw of the same frame, same node (gate 4, identity only) and
    // same lifetime serial (checked once sample_scope has read it, so a node
    // freed and reallocated at the same address within the frame is refused).
    // Cleared at Reset.
    std::uint64_t last_routed_node_ = 0, last_routed_lifetime_ = 0, last_routed_frame_ = ~std::uint64_t{0};
    std::uint32_t last_routed_draw_ = 0;
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
        // Packed screen brackets (policy 8, screen-emission-region.md step C):
        // exact screen pairs in the native screen state, those refused for
        // want of a locked-prefix bound (native, never the full viewport) or
        // for want of policy 8 (device caps), those prepared, completed Linear
        // or Incomplete, and the sum of their rectangles (also in region_pixels).
        unsigned packed_eligible = 0, packed_unbounded_refused = 0, packed_caps_refused = 0;
        unsigned packed_admitted = 0, packed_linear = 0, packed_incomplete = 0;
        std::uint64_t packed_region_pixels = 0;
        unsigned packed_sample_skipped = 0; // capture frames: admitted packed draws beyond packed_sample_cap
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
        // Step B locked-prefix bounds: qualifying draws (non-indexed
        // TRIANGLELIST, StartVertex 0, stride-24 FLOAT3 stream), those bound,
        // the lookup outcome and the sum of bound area fractions.
        unsigned prefix_draws = 0, prefix_bound = 0, prefix_refused = 0, prefix_instanced = 0, prefix_clipped = 0; // clipped: bound after a near-plane cut
        unsigned prefix_reason[unsigned(fade_region::Reason::Count)]{};
        unsigned prefix_lookup[unsigned(fade_region::prefix::Lookup::Count)]{};
        std::uint64_t prefix_permille_sum = 0;
        // Step D: over bound draws, the hull rectangle's pixels against the
        // near-clipped AABB rectangle of the same vertices (the step-B box),
        // the drawn vertices, and the derivation's ticks over every qualifying draw.
        std::uint64_t prefix_hull_px = 0, prefix_aabb_px = 0, prefix_vertices = 0, prefix_ticks = 0;
        unsigned prefix_rechecks = 0; // bound withdrawn: the record changed under the projection
    } composition_counts_;
    bool screen_emission_bound_ = false; // X3M_SCREEN_EMISSION_BOUND=1, read once at attach
    bool locked_prefix_log_ = false;     // X3M_LOCKED_PREFIX_LOG=1: the per-draw locked_prefix line on every frame (fixtures), not only capture frames
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
    bool screen_emission_timing_ = false;
    std::uint64_t present_qpc_ = 0, qpc_frequency_ = 0; // previous Present's stamp for screen_emission_frame
    void log_screen_emission_frame() noexcept;
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
    std::uint64_t target_generation_ = 0; // Successful allocation in this device/reset generation.
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
    // Finite huge is the practical-off median bound. One is exactly off for
    // the missing fraction because the cut predicate uses strict >.
    float cut_median_bound_ = 1e30f, cut_missing_bound_ = 1.f;
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
    bool sky_history_strict_ = false; // X3M_TAA_SKY_HISTORY=strict: FrameInputs::sentinel_strict_sky with the camera path
    float sky_history_band_px_ = 3.f; // X3M_TAA_SKY_HISTORY_BAND_PX: FrameInputs::sky_history_band_px
    float sky_history_exit_px_ = 0.f; // X3M_TAA_SKY_HISTORY_EXIT_PX: FrameInputs::sky_history_exit_px (0 without an age program)
    // Static-world previous rows for new keys (temporal-integration.md). The
    // camera verdict is evaluated once per frame, on the frame's first miss.
    unsigned unmatched_static_ = 0;
    std::uint64_t unmatched_static_frame_ = ~std::uint64_t{0};
    bool unmatched_static_camera_ = false;
    renderer::CameraState unmatched_static_current_{}, unmatched_static_previous_{}; // the latches the verdict was taken on; the rows use the same pair
    unsigned unmatched_static_frames_logged_ = 0;
    std::uint32_t unmatched_static_applied_ = 0, unmatched_static_object_unknown_ = 0, unmatched_static_camera_refused_ = 0, unmatched_static_rows_refused_ = 0;
    unsigned unmatched_static_logged_ = 0;
    bool unmatched_static_rows(const MotionRoute& route, const renderer::SubmittedMatrix& rows, renderer::SubmittedMatrix& previous) noexcept;
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
    // Lazy binding state: RT1 (and RT2) held by the route across routed draws.
    bool lazy_mode_ = false, lazy_rt1_ = false, lazy_rt2_ = false;
    bool state_shadow_ = true, state_hooks_ = true, scene_hook_installed_ = false;
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
        DWORD width = 0, height = 0; // level-0 size of a 2D texture (hull emissive widening only; 0 = unknown or not 2D)
        std::uint64_t identity = 0;  // the proxy's resource identity of `texture` when the size was read (a freed and reallocated texture at the same address re-reads)
        DWORD minfilter = 0; bool minfilter_known = false; // the application's MINFILTER (hull emissive widening: restored after a widened draw that raised it)
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
    float taa_current_filter_ = 0.f;          // X3M_TAA_CURRENT_FILTER (0: off, the plain resolve program)
    float taa_line_filter_ = 0.f;             // X3M_TAA_LINE_FILTER (0: off)
    unsigned taa_line_width_ = 1;             // X3M_TAA_LINE_FILTER=A,W: mask width 1 or 2 px
    float taa_far_weight_ = 0.f, taa_far_filter_ = 0.f, taa_far_f0_ = 80.f, taa_far_f1_ = 130.f, taa_far_lo_ = .03f, taa_far_hi_ = .25f; // X3M_TAA_FAR_STABILISER
    float taa_thin_weight_ = 0.f, taa_thin_relax_ = 1.f; // X3M_TAA_THIN_REGION
    bool taa_thin_camera_gate_ = false; // X3M_TAA_THIN_REGION_GATE=camera
    float taa_thin_emissive_ = 0.f;     // X3M_TAA_THIN_REGION_EMISSIVE=E: emissive vote of the thin region (thin-glow-lines.md 8.3 R3)
    float taa_sentinel_strength_ = 0.f, taa_sentinel_emitter_ = 1.f; // X3M_TAA_SENTINEL_STABILISER=S[,E]
    bool taa_masks_logged_ = false;           // the one line for TemporalPass::line_masks_failed()
    float taa_thin_clip_ = 0.f;               // X3M_TAA_THIN_CLIP (0: off)
    float taa_adaptive_weight_ = 0.f, taa_adaptive_lo_ = .1f, taa_adaptive_hi_ = .5f; // X3M_TAA_ADAPTIVE_WEIGHT (0: off)
    bool taa_alpha_history_ = false;          // X3M_TAA_ALPHA_HISTORY (HDR route only)
    float taa_history_weight_ = .9f;          // X3M_TAA_HISTORY_WEIGHT
    // 8-bit route: failed sharpened draws (the pass kept the resolve, the
    // copy-back presented it); at the limit the sharpen is no longer requested.
    unsigned taa_sharpen_failures_ = 0;
    static constexpr unsigned sharpen_failure_limit = 3;
    // Spatial family fog. Copied current-frame engine values select a profile;
    // the native-card source latch is observational. Reset clears readiness and
    // replacement fault; no engine pointers survive as dereferenceable state.
    std::unique_ptr<renderer::FogPass> fog_;
    renderer::FogSectorLatch fog_latch_{}; // observational only
    FogSectorFrame fog_sector_{};
    bool fog_requested_ = false, fog_enabled_ = true, fog_everywhere_ = false, fog_timing_ = false, fog_disabled_ = false, fog_attach_failed_ = false, fog_sun_fallback_logged_ = false;
    float fog_strength_ = renderer::fog_strength_default, fog_anisotropy_ = renderer::fog_anisotropy_default;
    unsigned fog_failures_ = 0, fog_logs_ = 0;
    std::uint64_t fog_frame_ = ~std::uint64_t(0), fog_applied_frames_ = 0;
    const char* fog_last_reason_ = "";
    bool fog_cards_replace_ = false, fog_card_ready_checked_ = false, fog_card_ready_ = false;
    FogCardPolicy fog_cards_{};
    unsigned fog_card_mode_ = 0, fog_card_logs_ = 0;
    std::uint64_t fog_card_logged_frame_ = 0, fog_transition_frame_ = ~std::uint64_t(0), fog_card_last_report_ = ~std::uint64_t(0), fog_card_observed_total_ = 0, fog_card_suppressed_total_ = 0, fog_card_refused_total_ = 0;
    const char* fog_card_fault_reason_ = "none";
    // Stored-density range. The camera is the previous scene end's (read after the owner latch).
    bool fog_density_requested_ = false, fog_density_refused_ = false, fog_density_prepared_ = false, fog_density_camera_valid_ = false;
    bool fog_density_config_logged_ = false, fog_density_ready_logged_[2]{}, fog_shadow_pass_refused_logged_ = false;
    bool fog_shadow_pass_launch_ = false; // X3M_FOG_SHADOW_PASS=1 at launch: arms the F11 toggle and the grid fields of volumetric_fog_frame
    const char* fog_grid_last_march_ = "none"; // the march variant the last frame row printed (a change forces a throttled row)
    std::uint64_t fog_grid_logged_frame_ = 0; // the last change-driven frame row
    static constexpr std::uint64_t fog_grid_change_frames = 60; // at most one change-driven row per 60 frames (the card row's spacing)
    static constexpr unsigned fog_grid_change_cap = 16; // change-driven rows per session; the toggle row is not budgeted
    unsigned fog_grid_change_logs_ = 0;
    unsigned fog_density_logs_ = 0;
    std::uint64_t fog_density_sample_frame_ = ~std::uint64_t(0), fog_density_key_ = 0;
    long long fog_density_epoch_qpc_ = 0, fog_density_sample_qpc_ = 0;
    static constexpr unsigned fog_density_gap_ms = 500; // a longer gap in scene samples is a load
    double fog_density_camera_[3]{};
    renderer::FogDensityConfig fog_density_config_{};
    void prepare_volumetric_fog_density(UINT width, UINT height) noexcept;
    void fog_density_epoch(const char* reason) noexcept;
    bool fog_density_active() const noexcept { return fog_density_requested_ && !fog_density_refused_; }
    void fog_transition_invalidate() noexcept;
    void fog_card_transition(unsigned mode) noexcept;
    void fault_fog_cards(const char* reason) noexcept;
    void prepare_fog_card(const MotionDrawCall&, MotionRoute&) noexcept;
    void finish_fog_card(MotionRoute&, HRESULT) noexcept;
    const char* fog_frame_prerequisite() const noexcept;
    const char* fog_frame_parameters(renderer::FogFrame&, float weight, bool& sun_tracked) noexcept;
    bool attach_volumetric_fog() noexcept;
    void prepare_volumetric_fog_targets(UINT width, UINT height) noexcept;
    void reconcile_volumetric_fog(const renderer::FogFrame&, const renderer::FogResult&, HRESULT) noexcept;
    void complete_volumetric_fog(const char* skip, HRESULT result, const renderer::FogResult& out) noexcept;
    void run_volumetric_fog() noexcept;
    void disable_volumetric_fog(const char* why, HRESULT result) noexcept;
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
    // X3M_FIXTURE_SCREEN_RECT=l,t,r,b replaces a bound locked-prefix rectangle
    // (the straddling case: the witness must fire); X3M_FIXTURE_SCREEN_CAPS_FAULT=1
    // withholds policy 8 from the attach request (the caps-refusal case).
    bool fixture_screen_rect_set_ = false, fixture_screen_caps_fault_ = false;
    fade_region::Rect fixture_screen_rect_{};
    float fixture_last_pixel_abi_[8]{};
    unsigned fixture_emission_exchange_fault_ = 0;
    unsigned fixture_cutout_cap_fault_ = 0, fixture_cutout_vs_fault_ = 0, fixture_cutout_ps_fault_ = 0;
    unsigned fixture_cutout_rs_fault_ = 0, fixture_cutout_sampler_fault_ = 0;
    unsigned fixture_hdr_fault_kind_ = 0, fixture_hdr_fault_count_ = 0; // queued until the pass exists
#endif
};
} // namespace x3m
