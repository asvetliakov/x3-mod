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
#include "../renderer/scene_boundary.h"
#include "../renderer/motion_history.h"
#include "../renderer/motion_row_history.h"
#include "../renderer/camera_reprojection.h"
#include "../renderer/hdr_pass.h"
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
inline constexpr std::size_t motion_shadow_state_count = 8;
}

namespace x3m {
struct MotionDrawCall {
    bool indexed = false, user_memory = false;
    D3DPRIMITIVETYPE topology = D3DPT_TRIANGLELIST;
    UINT primitives = 0, first = 0;
    INT base_vertex = 0;
    UINT min_vertex = 0, vertex_count = 0;
};
// The first gate that refused a draw; None means every gate passed. Numbers
// match the design document's gate list.
enum class MotionGate : unsigned { None = 0, Feature = 1, Scene = 2, Pair = 3, DrawState = 4, Scope = 5, History = 6 };
// Per-draw decision. Stack object; carries what after_draw must undo.
struct MotionRoute {
    MotionGate gate = MotionGate::Feature;
    bool routed = false, matched = false, scene = false;
    bool vs_set = false, ps_set = false, rt_set = false, write_set = false;
    bool vs_constants_set = false, ps_constants_set = false;
    bool depth = false, rt2_set = false, write2_set = false;   // RT2 bound for this draw (row has depth_output).
    bool jittered = false;                                     // Jittered rows written; restore after the draw.
    UINT jitter_register = 0;                                  // The VS row's clip-row window base.
    DWORD saved_write1 = 15, saved_write2 = 15;
    renderer::RigidDrawKey key{};
    std::uint64_t rows_hash = 0;
    std::uint64_t load_epoch = 0, registry_epoch = 0;
    std::uint64_t ticks = 0;  // CPU ticks of apply (before_draw) plus undo (after_draw); telemetry only.
};
// Why the temporal resolve did not run at this frame's bloom copy (X3M_TAA=1).
// None: it ran (see taa_result/taa_copy). NotReached: the selector never
// presented the AwaitCopy event (menu, rejected or unrecognized frame).
// CameraState: X3M_TAA_SENTINEL=2 (strict) and no far-plane transform this frame.
// Target: the engine scene-end hook fired while RT0 was not the latched main target.
enum class TaaSkip : unsigned { None = 0, Disabled = 1, NotReached = 2, NoJitter = 3, NotFilled = 4,
                                Recording = 5, Queries = 6, Initialize = 7, Container = 8, CameraState = 9, Target = 10 };
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
};
struct MotionFrameCounters {
    std::uint32_t draws = 0, routed = 0, matched = 0, gates[7]{};
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
    // block Apply/EndStateBlock, Reset, a failed restoration).
    std::uint32_t rs_queries = 0, rs_hits = 0, rs_gets = 0, rs_resyncs = 0;
    // Engine scene-end hook (X3M_SCENE_HOOK): signals this frame, signals that
    // arrived outside the selector's Scene phase (with the selector state of
    // the last one), draws evaluated after a Scene-phase signal (compositing
    // must follow the hook: those draws neither route nor jitter), whether the
    // selector reached the bloom copy afterwards, and the verdict (SceneEndCheck).
    std::uint32_t hook_signals = 0, hook_outside_scene = 0, hook_state = 0, draws_after_hook = 0;
    bool hook_scene_end = false, bloom_copy_seen = false;
    std::uint32_t scene_end_check = 0;
    MotionHdrCounters hdr;
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
};
#endif

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
    // Engine scene-end hook (X3M_SCENE_HOOK): `installed` records whether the
    // callsite patch is live (for the cross-check verdict); scene_end_hook is
    // the trampoline's signal, called before the engine's compositing call on
    // the render thread, outside any device hook. In the Scene phase it ends
    // routing/jitter for the frame, finishes the cut verdict and, with TAA on,
    // runs the temporal resolve on the bound RT0 (which must be the latched
    // main target); the StretchRect path then skips this frame's resolve.
    void configure_scene_hook(bool installed) noexcept { scene_hook_installed_ = installed; }
    void scene_end_hook() noexcept;
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
    void fixture_hdr_fault(unsigned kind, unsigned count) noexcept;
    HRESULT fixture_hdr_readback(float* out, std::size_t floats, UINT* width, UINT* height) noexcept;
    // Stage 2 exposure state: ev (consumed), ev_adapted, ev_target,
    // avg_log_l, dt, exposure, steps, k (eight floats).
    HRESULT fixture_hdr_exposure(float* out, std::size_t floats) const noexcept;
#endif

private:
    // A program's profile row (first row of a supported class hosting it) is
    // recorded once at registration, so per-draw work is two map lookups at
    // SetShader time and one binary-search pair check at draw time.
    struct ShaderEntry { std::uint64_t hash = 0; IUnknown* variant = nullptr;
                         const renderer::MotionOutputProfile* row = nullptr; };
    struct Shadow {
        IDirect3DVertexShader9* vs = nullptr;
        IDirect3DPixelShader9* ps = nullptr;
        std::uint64_t vs_hash = 0, ps_hash = 0;
        IDirect3DVertexShader9* vs_variant = nullptr;
        IDirect3DPixelShader9* ps_variant = nullptr;
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
        std::uint32_t position_offset = 0, position_type = 0;
        renderer::Surface rt0, depth;
        bool extra_rt[4]{};
        renderer::Viewport viewport;
        bool recording = false;
        DWORD states[motion_shadow_state_count]{};      // application render states (shadow_states order)
        bool states_known[motion_shadow_state_count]{};
    };
    struct SavedState;
    template<typename Fn> Fn native(unsigned slot) const noexcept { return reinterpret_cast<Fn>(native_[slot]); }
    bool self_test(bool with_depth, char* reason, std::size_t reason_size) noexcept;
    HRESULT draw_quad(IDirect3DSurface9* rt0, IDirect3DSurface9* rt1, IDirect3DSurface9* rt2,
                      IDirect3DPixelShader9* shader, UINT width, UINT height, HRESULT* restore) noexcept;
    HRESULT readback_surface(IDirect3DSurface9* surface, D3DFORMAT format, unsigned bytes_per_pixel,
                             const wchar_t* prefix, const wchar_t* extension, const char* tag, const char* format_name,
                             UINT width, UINT height) noexcept;
    void apply_jitter(MotionRoute& route) noexcept;
    void restore_jitter(MotionRoute& route) noexcept;
    void evaluate_draw(const MotionDrawCall& call, MotionRoute& route) noexcept;
    // Timed wrappers over the native SetRenderTarget/COLORWRITEENABLE calls of
    // the per-draw path and the lazy flush; each counts into counters_.set_rt.
    HRESULT bind_target(DWORD index, IDirect3DSurface9* surface) noexcept;
    HRESULT bind_targets(MotionRoute& route) noexcept;
    // The lazy-mode flush behind restore_bindings. `quiet` (the light
    // SetRenderState hook) records no telemetry metric and logs nothing: its
    // metrics and failure line are deferred to the next heavy call.
    template<bool quiet> HRESULT flush_bindings() noexcept;
    void record_deferred() noexcept;
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
    void undo(MotionRoute& route) noexcept;
    renderer::SceneSignatures signatures() const noexcept;
    void readback() noexcept;
    // HDR redirect (hdr_pass.h performs the device work; the policy is here).
    enum class HdrState { Off, Active, Suspended };
    void begin_redirect() noexcept;             // at the latching Clear (before it is forwarded)
    void end_redirect(HdrEnd reason) noexcept;  // write back (when content is pending), rebind the main target, release it
    void flush_redirect() noexcept;             // write back, keep the FP16 target bound
    void drop_redirect() noexcept;              // Reset/release: no write-back
    renderer::HdrWriteback hdr_writeback(IDirect3DSurface9* final_rt0, bool write) noexcept;
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
    IDirect3DSurface9* target_surface_ = nullptr; // Level 0 of the owned RGBA32F texture (RT1).
    IDirect3DSurface9* depth_surface_ = nullptr;  // Level 0 of the owned R32F texture (RT2).
    UINT target_width_ = 0, target_height_ = 0;
    bool target_failed_ = false;
    IDirect3DPixelShader9* sentinel_ps_ = nullptr;      // One output: motion target alone.
    IDirect3DPixelShader9* sentinel_mrt_ps_ = nullptr;  // Two outputs: motion and depth targets.
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
    // Telemetry sink (capture.cpp's per-device State) and frame-line cadence.
    telemetry::State* stats_ = nullptr;
    unsigned frame_log_interval_ = 60;
    // Lazy binding state: RT1 (and RT2) bound by the route with the
    // application's COLORWRITEENABLE1/2 values saved at bind time.
    bool lazy_mode_ = false, lazy_rt1_ = false, lazy_rt2_ = false;
    DWORD lazy_write1_ = 15, lazy_write2_ = 15;
    bool state_shadow_ = true, scene_hook_installed_ = false;
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
    float fixture_last_pixel_abi_[8]{};
    unsigned fixture_hdr_fault_kind_ = 0, fixture_hdr_fault_count_ = 0; // queued until the pass exists
#endif
};
} // namespace x3m
