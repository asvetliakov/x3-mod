#include "capture.h"
#include "capture_state.h"
#include "../fog/fog_density_cache.h"
#include "proxy_identity.h"
#include "telemetry.h"
#include "game_phases.h"
#include "voice_dmo_fallback.h"
#include "lod_scale.h"
#include "cull_census.h"
#include "collide_box_cull.h"
#include "collide_narrow_census.h"
#include "collide_sat_sse2.h"
#include "collide_memo.h"
#include "sun_occlusion.h"
#include "cull_small_parts.h"
#include "capture_arm_core.h"
#include "frame_timing.h"
#include "frame_phases.h"
#include "pass_phases.h"
#include "residual_phases.h"
#include "submit_phases.h"
#include "light_phases.h"
#include "loop_phases.h"
#include "media_cue.h"
#include "media_cue_sites.h"
#include "point_light_admission.h"
#include "loading_trace.h"
#include "gz_buffer.h"
#include "crypt_cache.h"
#include "resource_reader.h"
#include "engine_patch.h"
#include "sampling_profiler.h"
#include "scene_capture.h"
#include "object_trace.h"
#include "object_capture.h"
#include "sector_background.h"
#include "scene_hook.h"
#include "compositor_bridge.h"
#include "compositor_owner.h"
#include "../renderer/bloom_programs.h"
#include "../renderer/shader_population.h"
#include "chase_camera.h"
#include "chase_aim_trace.h"
#include "chase_transition.h"
#include "chase_lead.h"
#include "camera_state.h"
#include "sun_light_poll.h"
#include "object_lifetime.h"
#include "draw_input.h"
#include "motion_capture.h"
#include "motion_output.h"
#include "comparison_controls.h"
#include "comparison_notice.h"
#include "fps_overlay.h"
#include "engine_memory.h"
#include "cpu_state.h"
#include "../ownership/d3d9_ownership.h"
#include "../ownership/application_admission_abi.h"
#include <array>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <io.h>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace x3m {
namespace {
std::recursive_mutex mutex;
FILE* logfile;
HANDLE log_os_handle=INVALID_HANDLE_VALUE; // log_handle(): exception-context writes bypass stdio
std::wstring directory;
unsigned capture_start = 120;
unsigned capture_count = 1;
// X3M_CAPTURE_DELAY (launcher --capture-delay, frames, 0 = immediate): the F8
// press only arms the burst, which starts this many frames later. F8 cancels
// the game's SETA time compression, so a capture of the compressed case needs
// the delay to re-engage SETA (src/proxy/capture_arm_core.h).
unsigned capture_delay = 0;
// X3M_FRAME_END_STRIDE (1..frame_end_stride_max, default 300; launcher
// --frame-end-stride): frames between two frame_end lines. Read once at attach,
// used as a divisor on the Present path only; 1 logs every frame (about 100 B
// per frame). The default keeps the historical 300-frame cadence, and the
// other 300-frame reports of that path keep it whatever this is.
constexpr unsigned frame_end_stride_default = 300, frame_end_stride_max = 100000;
unsigned frame_end_stride = frame_end_stride_default;
bool scene_depth_capture_requested = false;
bool finite_positions_requested = false;
bool motion_capture_requested = false;
bool motion_output_requested = false;
bool motion_jitter_requested = false;
bool taa_requested = false, taa_debug_requested = false;
float taa_k_override = -1.f; // X3M_TAA_K (stage 3): fixed k of the resolve's luminance weighting on the HDR path; negative: derived from the exposure
// X3M_TAA_MIP_BIAS=<float> (default -0.5 with X3M_TAA=1, otherwise 0 = off,
// bit-identical): D3DSAMP_MIPMAPLODBIAS the route applies to the mip-mapped
// stages of routed draws while the jitter is on (docs/architecture/
// temporal-integration.md, "Mip LOD bias"); installs the light
// SetTexture/SetSamplerState hooks. An explicit 0 disables it.
float taa_mip_bias = 0.f;
// X3M_TAA_SHARPEN=<0..1> (requires X3M_TAA=1, default 0.75 there): post-resolve
// RCAS of the display image on both routes (docs/architecture/
// temporal-integration.md, "Post-resolve sharpen"); an explicit 0 is off, with
// bit-identical output. Off entirely without TAA.
float taa_sharpen = 0.f;
float taa_current_filter = 0.f;  // X3M_TAA_CURRENT_FILTER (0..4; 0 off)
float taa_line_filter = 0.f;     // X3M_TAA_LINE_FILTER=A[,W] (A 0..4; 0 off; ignored with X3M_TAA_CURRENT_FILTER > 0)
float taa_far[6] = {0.f, 0.f, 80.f, 130.f, .03f, .25f}; // X3M_TAA_FAR_STABILISER=W[,A[,F0,F1[,LO,HI]]]: far weight (0 off), far filter A (0 off), gate footprints, speed gate px/frame
float taa_thin_region[4] = {0.f, 1.f, .03f, .25f}; // X3M_TAA_THIN_REGION=W[,RELAX[,LO,HI]]
bool taa_thin_gate_given = false;
bool taa_thin_camera_gate = false; // X3M_TAA_THIN_REGION_GATE=camera (taa-lattice-crawl.md section 32.1)
float taa_sentinel[2] = {0.f, 1.f}; // X3M_TAA_SENTINEL_STABILISER=S[,E] (temporal-integration.md "sentinel stabiliser"): strength (0 off), emitter bound (0 none)
unsigned taa_line_width = 1;     // W: line mask width 1 (default) or 2 px
float taa_thin_clip = 0.f;       // X3M_TAA_THIN_CLIP (0..1; 0 off)
float taa_adaptive_weight = 0.f, taa_adaptive_lo = .1f, taa_adaptive_hi = .5f; // X3M_TAA_ADAPTIVE_WEIGHT=WMAX[,LO,HI] (0 off)
bool taa_alpha_history = false;  // X3M_TAA_ALPHA_HISTORY=1
float taa_history_weight = .9f;  // X3M_TAA_HISTORY_WEIGHT (0.5..0.98)
// X3M_HDR=1 (default off; requires X3M_MOTION_OUTPUT=1): the FP16 HDR scene
// path (docs/architecture/hdr-scene-path.md). Stage 2 switches, all
// defaulting to the stage-1 identity behaviour: X3M_HDR_TONEMAP=agx|identity,
// X3M_HDR_DECODE=gamma2.2|pow22|srgb|none, X3M_HDR_LOOK=none|golden|punchy,
// X3M_HDR_CLAMP=<float>, X3M_HDR_EXPOSURE=auto|manual|fixed, X3M_HDR_EV_MANUAL=<ev>
// (implies manual), X3M_HDR_EV=<offset> (alias X3M_HDR_EV_OFFSET),
// X3M_HDR_KEY, X3M_HDR_EV_MIN/MAX, X3M_HDR_ADAPT_UP/DOWN (seconds),
// X3M_HDR_METER_BG (tile background floor, scene units), X3M_HDR_METER_MIN_LIT
// (lit fraction below which the target is neutral), X3M_HDR_WHITE_TARGET
// (fraction of the tonemapper's white the brightest 1 % of tiles may reach),
// X3M_HDR_KEY_PULL (fraction of the key rule applied when the lit median is
// brighter than the key), X3M_HDR_EV_DEADBAND (the held target moves only
// when the fresh one differs by more), X3M_HDR_METER_EDGE_WEIGHT (tile weight
// at the frame corners for the lit statistic), X3M_HDR_DT_MS (fixed
// adaptation step; fixtures).
bool hdr_requested = false;
bool linear_emission_requested = false;
bool linear_distance_fade_requested = false;
bool screen_emission_requested = false; // X3M_SCREEN_EMISSION=1: packed screen policy 8 (screen-emission-region.md step C)
bool screen_emission_timing_requested = false; // X3M_SCREEN_EMISSION_TIMING=1: per-Present screen_emission_frame line, needs the option
float screen_emission_gain = 1.f;       // X3M_SCREEN_EMISSION_GAIN: step E composition gain g, finite 0.5..8, default 1
float emission_source_gain = 1.f;       // X3M_EMISSION_SOURCE_GAIN: source-only encoded gain of the twenty additive/screen emission pairs, finite 1..8, 1 = off (requires X3M_HDR=1)
float hull_emission_gain = 1.f;         // X3M_HULL_EMISSION_GAIN: the same gain over the twelve hull programs' ADD ONE/ONE draws (emitter plan phase 3), finite 1..8, 1 = off (requires X3M_HDR=1 only; independent of the effects gain, own key Ctrl+Shift+F4)
float original_fill = 0.f;             // X3M_ORIGINAL_FILL: linear-light fill inside the original hull pixel programs, finite 0..0.5, 0 = off (requires X3M_HDR=1, excludes X3M_LINEAR_MATERIALS=1)
bool lightmap_far_fade_requested = false; // X3M_LIGHT_MAP_FAR_FADE=P0,P1[,G]: the hull light-map gain fades to G (default 1) as the draw's footprint grows from P0 to P1 units/px; needs the gain
float lightmap_far_fade[3] = {0.f, 0.f, 1.f};
bool sun_occlusion_core_f = false; // X3M_SUN_OCCLUSION_CORE_F=1: the clipped core bodies are also scaled by f (flight comparison)
float sun_occlusion_radius = sun_occlusion::core::radius_default_u, sun_occlusion_curve = 1.f; // X3M_SUN_OCCLUSION_RADIUS (0.005..0.25, the disc's half-width as a fraction of the back-buffer width), X3M_SUN_OCCLUSION_CURVE (0.25..4, exponent on the used fraction)
bool hull_emissive_widening_requested = false; // X3M_HULL_EMISSIVE_WIDENING=K,Q0,Q1 (hull-emissive-widening.md): the light-map fetch of the gained hull variants widens to k x k px, k ramping 1 -> K over the footprint band [Q0, Q1] units/px; needs the gain
float hull_emissive_widening[3] = {1.f, 0.f, 0.f};
float hull_lightmap_gain = 1.f;        // X3M_HULL_LIGHTMAP_GAIN: gain on the light-map (self-illumination) term inside the original hull pixel programs, finite 1..8, 1 = off (requires X3M_HDR=1, excludes X3M_LINEAR_MATERIALS=1; Ctrl+Shift+F4 switches it alone)
bool screen_emission_additive_requested = false; // X3M_SCREEN_EMISSION_ADDITIVE=G: in-place ADD/ONE/ONE bullets with a colour gain (screen-emission-region.md, "Additive option")
float screen_emission_additive_gain = 1.f;       // G, finite 1..8; anything else refuses the option
bool screen_emission_additive_alpha_requested = false; // X3M_SCREEN_EMISSION_ADDITIVE_ALPHA=K: per-source bloom attenuation of the additive draw (bloom-per-source-attenuation.md, option 1)
float screen_emission_additive_alpha = 1.f;      // K, finite 0..1; absent or invalid keeps the native alpha law a + D.a
unsigned fade_witness_frames = 0; // X3M_FADE_WITNESS=<k>, 0 = off
unsigned fade_route_threshold = 500; // X3M_FADE_ROUTE=<permille>|off: fade-band motion arm threshold (fade_route_core.h), default 500
bool shimmer_trace_requested = false; // X3M_SHIMMER_TRACE=1, needs the route and TAA
// X3M_AMBIENT_OCCLUSION=1 (default off; requires X3M_MOTION_OUTPUT=1 and
// X3M_TAA=1): the half-resolution GTAO chain at the scene-end hook before the
// resolve (docs/architecture/ambient-occlusion.md, step 2). X3M_AO_RADIUS=<m>
// (0.1..100, default 2), X3M_AO_STRENGTH=<s> (0..1, default 0.5),
// X3M_AO_DEBUG=1 (factor written as grayscale; implies timing),
// X3M_AO_TIMING=1 (one ambient_occlusion_frame line per frame).
bool ambient_occlusion_requested = false, ambient_occlusion_debug = false, ambient_occlusion_timing = false;
// Ctrl+Shift+F12 (comparison-hotkeys.md, "Sun shadows at rest"): true once a
// device enabled the scene-end sun-shadow application, so the A/B key is
// polled only then; without --sun-shadow-apply the press is ignored.
bool sun_shadow_apply_requested = false;
// X3M_FPS_OVERLAY=1 (comparison-hotkeys.md, "FPS overlay"; default off): the
// frame-rate line on the presented image, Ctrl+Alt+F7 hides and shows it.
// Off, the Present path pays one branch and polls no key.
bool fps_overlay_requested = false;
float ambient_occlusion_radius = 2.f, ambient_occlusion_strength = .5f;
// X3M_VOLUMETRIC_FOG=1 (default off; docs/architecture/volumetric-fog.md, "Stage 1
// implementation"): X3M_VOLUMETRIC_FOG_STRENGTH=<tau_max> (0..0.1, default 0.02),
// X3M_VOLUMETRIC_FOG_ANISOTROPY=<g> (0..0.9, default 0.3),
// X3M_VOLUMETRIC_FOG_EVERYWHERE=1 (the sector rule forced on),
// X3M_VOLUMETRIC_FOG_TIMING=1 (one volumetric_fog_frame line per frame).
// Ctrl+Alt+F9 toggles the pass, Ctrl+Alt+F10 steps the strength (Shift up).
bool sector_background_requested = false; // read-only, independent of the fog pass
bool volumetric_fog_cards_replace = false;
bool volumetric_fog_requested = false, volumetric_fog_everywhere = false, volumetric_fog_timing = false;
// X3M_VOLUMETRIC_FOG_RANGE=legacy|stored (default legacy; fog-density-runtime-integration.md):
// stored selects the two-level stored-density field with its 30-40 km horizon. Anything else is legacy.
bool volumetric_fog_range_stored = false;
// X3M_VOLUMETRIC_FOG_LOOK=0..3 (default renderer::fog_look_default = 2; 0 is the unshaped law) and X3M_FOG_LOOK_<NAME>=<float> tuning
// (renderer::fog_look_fields; X3M_FOG_LOOK_AMBIENT_SUN / _AWAY = r,g,b): read once here, stored range only.
unsigned volumetric_fog_look = 0;
x3m::renderer::FogLookTuning volumetric_fog_look_tuning{};
float volumetric_fog_strength = x3m::renderer::fog_strength_default, volumetric_fog_anisotropy = x3m::renderer::fog_anisotropy_default;
float emission_gain = 1.f;
bool linear_material_requested = false;
x3m::renderer::LinearMaterialConfig linear_material_config{};
bool bloom_requested = false; // X3M_HDR_BLOOM=1; opt-in AgX compositor replacement
// X3M_BLOOM_SOURCE_CLAMP=C: bloom-only decoded-space ceiling on the extraction
// source (docs/architecture/bloom-falloff.md). kAgxClampOff is the unbounded
// feed this option leaves untouched when absent or invalid.
float bloom_source_clamp = x3::temporal::kAgxClampOff;
x3m::renderer::HdrConfig hdr_config{};
// X3M_MOTION_RT_MODE=lazy keeps the route's RT1/RT2 bindings across routed
// draws (experiment; default perdraw); X3M_MOTION_FRAME_LOG=<n> sets the
// periodic motion_output_frame cadence with telemetry on (default 60).
bool motion_rt_lazy = false;
unsigned motion_frame_log = 60;
// X3M_STATE_SHADOW selects the render-state configuration (hybrid unhook,
// docs/architecture/state-call-fast-path.md step 5). Unset (auto, -1): the
// SetRenderState and SetSamplerState hooks are not installed and the route
// reads its draw-time state with GetRenderState/GetSamplerState, unless
// X3M_FRAME_TIMING=1 (the diagnostic build still counts state calls)
// or a failed Get* capability check keeps the hooks on, in which case the
// shadow is on. 1: hooks on, shadow on (every per-draw query a shadow hit).
// 0: hooks off as auto; with a hook reason present, hooks on with the shadow
// off (every query a native GetRenderState, the A/B of the shadow itself).
// X3M_SCENE_HOOK is parsed by scene_hook (default on with the route).
int motion_state_shadow = -1;
// X3M_TAA_K=<k> (stage 3 of the HDR scene path; requires X3M_HDR=1 and
// X3M_TAA=1) fixes k of the resolve's luminance weighting (0: unweighted);
// unset: k = exp2(EV) of the AgX write-back, 0 with the identity write-back.
// X3M_TAA_SENTINEL=auto|1|2 selects the resolve's depth-sentinel policy
// (auto: far-plane camera reprojection whenever the live camera read yields a
// transform; 1: current-only; 2: strict, skip the resolve without one);
// X3M_CAMERA_CUT_DEG bounds the camera rotation per frame before a cut is
// declared (default 20); X3M_CAMERA_LOG is the camera_state line cadence (300).
x3m::renderer::SentinelMode taa_sentinel_mode = x3m::renderer::SentinelMode::Auto;
float camera_cut_degrees = 20.f;
// X3M_TAA_UNMATCHED_STATIC=node|all|off (requires X3M_TAA=1): a routed draw
// whose history key is new this frame gets previous rows from the static-world
// assumption (its current placement through the previous camera) instead of the
// missing-history sentinel. node: only when the same object was drawn last
// frame under another key; all: any new key. Absent is the run212-accepted
// default: node whenever the TAA route is on ("off" or "0" is the opt-out).
unsigned taa_unmatched_static = 0;
unsigned camera_log_frames = 300;
unsigned motion_jitter_samples = 8;
// The finite huge displacement bound is a practical off switch. A missing
// bound of 1 is exactly off because finish_cut_detector uses strict > and the
// missing fraction cannot exceed 1. Environment overrides retain the 48/.25
// diagnostic detector when explicitly requested.
float motion_cut_median_px = 1e30f, motion_cut_missing = 1.f;
// Component fixtures serialize every write and replay. The live capture mutex
// does not cover worker-thread VB/IB Lock/Unlock or mapped writes. No production
// exclusion token is available yet: do not turn a requested diagnostic into an
// unsafe GPU replay merely because the latest revisions looked unchanged.
constexpr bool motion_live_replay_available = false;
std::set<uint64_t> dumped;
// Session population of created programs (docs/architecture/mod-compatibility.md,
// "Making unknown programs visible"). Process-wide, not per device: a program
// is classified once, under the hook lock that already serialises shader
// creation and Present. The `dumped` set above is process-wide and never
// cleared, so each program is classified on its first creation only; a
// program first created before telemetry was enabled is therefore never
// reported (X3M_TELEMETRY is read once at load, so this cannot happen in a
// normal session).
renderer::ShaderPopulation shader_population;
uint64_t next_device_id = 1;
}
unsigned long long dll_load_qpc = 0; // stamped in DllMain (loader.cpp)
namespace {

// Each object owns a private copy of the backend vtable. We don't patch shared
// executable pages, wrap resources, or change IUnknown identity. Ex tails are
// retained if supported, even though this first capture implementation targets 9.
struct Hooks {
    void** original;
    std::vector<void*> table;
    Hooks(void* object, size_t size) {
        original = *static_cast<void***>(object);
        table.assign(original, original + size);
    }
    void install(void* object) { *static_cast<void***>(object) = table.data(); }
    template<typename Fn> Fn get(size_t slot) const {
        return reinterpret_cast<Fn>(original[slot]);
    }
    template<typename Fn> void set(size_t slot, Fn fn) {
        table[slot] = reinterpret_cast<void*>(fn);
    }
};
struct CompositorInvocation;
struct Device : Hooks {
    D3DCAPS9 caps{};
    uint64_t id = next_device_id++;
    uint64_t frame = 0;
    uint64_t draws = 0;
    uint64_t events = 0;
    uint64_t frame_end_qpc = 0; // stamp of the previous frame_end line (dt_ms)
    telemetry::State stats;
    SceneCapture scene_depth;
    DrawInputReader draw_inputs;
    MotionCapture motion;
    MotionOutput motion_output;
    renderer::BloomPass bloom;
    ComparisonControls comparison;
    ComparisonNotice comparison_notice;
    bool comparison_report_pending = false;
    char comparison_emitter_notice[40]{}; // last emitter-key result (F4 LIGHTMAP, F5 BULLETS, F6 EMISSION + GUIDE); empty = show the bloom line
    FpsOverlay fps_overlay; // --fps-overlay accumulator and Ctrl+Alt+F7 visibility
    ComparisonNotice fps_notice{72}; // its bitmap, one panel below the hotkey notice

    std::uint64_t bloom_effective_frame = UINT64_MAX;
    bool bloom_effective_on = false;
    CompositorInvocation* compositor = nullptr; // capture mutex; invocation owns its CPU/native pins
    std::uint64_t reset_generation = 0;
    DWORD scene_thread = 0;
    bool composition_scene_owner = false;
    std::uint64_t composition_scene_frame = 0;
    unsigned composition_draw_depth = 0;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    unsigned fixture_emission_source_calls = 0;
    unsigned fixture_primitive_source_calls = 0; // submitted DrawPrimitive calls this frame (screen fixture key 49)
    bool fixture_observe_native_wrap = false;
    MotionOutputFixtureWrapSnapshot fixture_wrap{};
#endif
    unsigned bloom_busy = 0; // suppress all final-reference inference during injected operations
    bool reset_active = false, bloom_attempted = false;
    unsigned bloom_failure_reports = 0;
    std::uint64_t bloom_prepared = 0, bloom_committed = 0;
    unsigned remaining = 0;
    bool capture = false;
    sector_background::Diagnostic sector_background_evidence;
    object_capture::Cache object_evidence; // capture-only; existing HookGuard owns it
    bool key_down = false;
    capture_arm::core::Pending capture_pending; // X3M_CAPTURE_DELAY only; cleared on Reset
    explicit Device(void* object, size_t size) : Hooks(object, size) {}
};
// Preserve the original serialization while exposing its CPU-side wait cost.
struct CaptureLock {
    std::lock_guard<std::recursive_mutex> lock{mutex};
};
struct HeldHookLock {
    std::unique_lock<std::recursive_mutex> lock;
    HeldHookLock():lock(mutex,std::defer_lock){const auto start=telemetry::now();lock.lock();telemetry::record(telemetry::process(),telemetry::Metric::LockWait,telemetry::now()-start);}
};
// The guards carry the X3M_FRAME_TIMING scope of the hooked entry point: the
// lock member is declared first, so the frame-timing stamps are taken with the
// hook mutex already held (the diagnostic's state is single-threaded by that
// lock) and released before it. The entry name defaults to the hooked
// function's own name at the call site; the bucket defaults to State, the draw
// hooks and the scene-end path pass their own.
struct HookGuard {
    HeldHookLock held;
    frame_timing::Scope timing;
    explicit HookGuard(frame_timing::Bucket bucket=frame_timing::Bucket::State,
                       const char* entry=__builtin_FUNCTION()):timing(bucket,entry){}
};
// Same serialization without the lock-wait telemetry (two QPC stamps per
// call), for the hot setter, binding and draw hooks of the motion route.
struct PlainHookGuard {
    std::lock_guard<std::recursive_mutex> lock{mutex};
    frame_timing::Scope timing;
    explicit PlainHookGuard(frame_timing::Bucket bucket=frame_timing::Bucket::State,
                            const char* entry=__builtin_FUNCTION()):timing(bucket,entry){}
};
// The outer stamps feed CaptureCpu (capture frames only) and the inner pair a
// backend metric; the draw hooks pass `backend` as their DrawBackend enable so
// an uncaptured draw with per-draw telemetry off takes no QPC stamp at all.
struct CallTimer {
    Device& ctx; uint64_t start, backend_start=0, backend_ticks=0;
    bool captured, stamps;
    explicit CallTimer(Device& value,bool backend=true):ctx(value),start(value.capture?telemetry::now():0),captured(value.capture),stamps(backend||value.capture){}
    void begin(){if(stamps)backend_start=telemetry::now();}
    void end(){if(stamps)backend_ticks=telemetry::now()-backend_start;}
    ~CallTimer(){if(captured)telemetry::record(ctx.stats,telemetry::Metric::CaptureCpu,telemetry::now()-start-backend_ticks);}
};
void presentation_parameters(const char* phase,uint64_t device,HWND focus,const D3DPRESENT_PARAMETERS* p){
    if(!telemetry::enabled())return;
    if(!p){log("telemetry_presentation phase=%s device=%llu focus_window=%p params_null=1",phase,device,focus);return;}
    log("telemetry_presentation phase=%s device=%llu thread=%lu focus_window=%p device_window=%p width=%u height=%u format=%u count=%u msaa=%u quality=%lu swap_effect=%u windowed=%d auto_depth=%d depth_format=%u flags=%08lx refresh=%u interval=%u",phase,device,GetCurrentThreadId(),focus,p->hDeviceWindow,p->BackBufferWidth,p->BackBufferHeight,p->BackBufferFormat,p->BackBufferCount,p->MultiSampleType,p->MultiSampleQuality,p->SwapEffect,p->Windowed,p->EnableAutoDepthStencil,p->AutoDepthStencilFormat,p->Flags,p->FullScreen_RefreshRateInHz,p->PresentationInterval);
}
void capture_event(Device& ctx,const char* operation,HRESULT result,bool before_draw=false){
    if(ctx.capture)log("capture_event device=%llu frame=%llu seq=%llu after_draw=%llu op=%s result=%08lx qpc=%llu",ctx.id,ctx.frame,++ctx.events,ctx.draws-(before_draw?1:0),operation,result,telemetry::now());
}
std::map<IDirect3D9*, std::unique_ptr<Hooks>> factories;
std::map<IDirect3DDevice9*, std::shared_ptr<Device>> devices;
// Dispatch trim (docs/architecture/state-call-fast-path.md, step 4): the light
// setter hooks paid a std::map lookup per call. The game drives one device, so
// a one-entry cache turns the repeat lookup into a pointer compare. Every read
// and every write below happens with the hook mutex held (the guards are
// constructed first), and the map is only mutated under that same mutex
// (hook_device, release_device), where the cache is dropped. Fail-closed: any
// pointer that is not the cached one takes the ordinary devices.at lookup, so
// an unhooked device is never served from the cache. That lookup is now inside
// a noexcept function, so an unknown device terminates where it previously
// threw std::out_of_range out of a stdcall hook into the game's frames, which
// had no handler either: both outcomes end the process, this one at the fault.
IDirect3DDevice9* cached_device_key = nullptr;
Device* cached_device_context = nullptr;
void forget_cached_device() noexcept { cached_device_key = nullptr; cached_device_context = nullptr; }
Device& device_context(IDirect3DDevice9* d) { return *devices.at(d); }
__attribute__((noinline)) Device& adopt_cached_device(IDirect3DDevice9* d) noexcept {
    Device& ctx = *devices.at(d);
    cached_device_key = d; cached_device_context = &ctx;
    return ctx;
}
inline Device& hooked_device(IDirect3DDevice9* d) noexcept {
    if (d == cached_device_key) return *cached_device_context;
    return adopt_cached_device(d);
}
// A null monitor (X3M_ADMISSION off) is the option-off path on which the ABI
// adapter constructs, observes and destroys nothing. The light setters test it
// here instead of paying the adapter's two cross-unit calls per hooked call;
// with X3M_ADMISSION=1 the monitor is non-null and the adapter is constructed
// and destroyed in exactly the same position as before. The monitor lookup
// itself is unchanged (one published acquire load, already on this path).
class LightAdmissionScope {
public:
    LightAdmissionScope() noexcept : monitor_(ownership::process_admission_monitor_published()) {
        if (monitor_) new (storage_) ownership::ApplicationAdmissionAbi(monitor_);
    }
    ~LightAdmissionScope() { if (monitor_) adapter()->~ApplicationAdmissionAbi(); }
    LightAdmissionScope(const LightAdmissionScope&) = delete;
    LightAdmissionScope& operator=(const LightAdmissionScope&) = delete;
private:
    ownership::ApplicationAdmissionAbi* adapter() noexcept {
        return std::launder(reinterpret_cast<ownership::ApplicationAdmissionAbi*>(storage_));
    }
    ownership::AdmissionMonitor* monitor_;
    alignas(ownership::ApplicationAdmissionAbi) unsigned char storage_[sizeof(ownership::ApplicationAdmissionAbi)];
};
// Rows outside the dispatch trim construct the adapter directly, as before.
struct DirectAdmissionScope {
    ownership::ApplicationAdmissionAbi adapter{ownership::process_admission_monitor()};
};
// Invocation storage is constructed explicitly by pre and destroyed by bridge
// finally. No GCC destructor is relied upon across original's Windows SEH.
struct CompositorInvocation {
    std::shared_ptr<Device> owner;
    IDirect3DDevice9* device = nullptr;
    compositor_owner::Snapshot identity{};
    renderer::BloomPrepare input{};
    renderer::BloomCandidate candidate{};
    bool native_pin = false, ready = false, revoked = false;
};
static_assert(sizeof(CompositorInvocation) <= X3M_CB_STORAGE_SIZE, "bridge invocation storage");
static_assert(alignof(CompositorInvocation) <= 16, "bridge invocation alignment");
#ifdef X3M_MOTION_OUTPUT_FIXTURE
// Test-only bridge context: synthetic retained scene, actual COM/BloomPass and
// production Release/Reset/cleanup. The fixture owns these borrowed inputs.
struct BloomLifetimeFixture {
    X3mCompositorBinding binding{};
    IDirect3DDevice9* device=nullptr;
    IDirect3DTexture9* scene=nullptr;
    IDirect3DSurface9* main=nullptr;
    IDirect3DSurface9* depth=nullptr;
    std::weak_ptr<Device> owner;
    unsigned counts[17]{};
    bool bound=false;
} bloom_lifetime_fixture;
#endif
struct BloomOperation {
    Device& owner;
    explicit BloomOperation(Device& d) noexcept : owner(d) { ++owner.bloom_busy; }
    ~BloomOperation() { --owner.bloom_busy; }
};
template<class T> void bloom_drop(T*& value) noexcept {
    T* old = value; value = nullptr; if (old) old->Release();
}
void revoke_compositor(Device& ctx) noexcept {
    if (auto* call = ctx.compositor) {
        call->revoked = true; call->ready = false;
        call->input.boundary.admitted = false;
        // Native pin deliberately survives Reset and reference cleanup.
        BloomOperation internal(ctx);
        bloom_drop(call->candidate.surface); call->candidate = {};
        bloom_drop(call->input.scene);
        bloom_drop(call->input.boundary.main);
        bloom_drop(call->input.boundary.depth);
    }
}
// State blocks change device state outside the setter hooks. With the motion
// route enabled, each block gets a private vtable so Apply can resynchronize
// the route's shadow. The block keeps its device alive, so the device pointer
// stays valid for the block's lifetime. Slots verified in abi_check.cpp.
struct StateBlockHooks : Hooks {
    IDirect3DDevice9* device;
    StateBlockHooks(void* object,IDirect3DDevice9* owner):Hooks(object,6),device(owner){}
};
std::map<IDirect3DStateBlock9*, std::unique_ptr<StateBlockHooks>> stateblocks;
// Queries change their result with every draw between Issue(BEGIN) and
// Issue(END); the resolve must not inject draws while one is open. With the
// route enabled every query object gets a private vtable (slots 2 Release,
// 6 Issue) so the route knows how many are open. EVENT queries have no BEGIN
// and never count.
struct QueryHooks : Hooks {
    IDirect3DDevice9* device;
    bool active=false;
    QueryHooks(void* object,IDirect3DDevice9* owner):Hooks(object,8),device(owner){}
};
std::map<IDirect3DQuery9*, std::unique_ptr<QueryHooks>> queries;
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
void fixture_apply(Device& ctx);
#endif
namespace {

uint64_t hash_bytes(const void* data, size_t size) {
    uint64_t hash = 14695981039346656037ull;
    auto bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
    return hash;
}
struct ObservedDraw {
    DrawInput input{};
    object_lifetime::Snapshot lifetime{};
    std::uintptr_t registry = 0;
    bool lifetime_observed = false;
    bool scene_draw = false;
};
ObservedDraw read_draw_input(Device& ctx,IDirect3DDevice9* device,const DrawArguments& arguments) {
    if(!ctx.capture)return {};
    ObservedDraw draw{};
    object_trace::Snapshot scope{};
    const bool scoped=object_trace::current(&scope);
    draw.scene_draw=ctx.scene_depth.collecting_scene();
    draw.input=ctx.draw_inputs.read(device,arguments,scoped?&scope:nullptr,
        draw.scene_draw?ctx.motion.geometry_frame():ownership::GeometryFrameHandle{});
    draw.lifetime_observed=object_lifetime::active();
    if(draw.lifetime_observed)draw.lifetime.reason=object_lifetime::Reason::LookupUnavailable;
    if(scoped&&!(draw.input.blockers&ObjectScope)) {
        draw.registry=scope.registry;
        if(object_lifetime::current(scope.registry,scope.node,scope.node_handle,
                                    scope.camera,scope.camera_handle,&draw.lifetime)) {
            auto& observation=draw.input.observation;
            observation.key.object_lifetime=draw.lifetime.node_serial;
            observation.key.camera_lifetime=draw.lifetime.camera_serial;
            observation.proofs|=renderer::LifetimeVerified;
        }
    }
    return draw;
}
void record_draw_input(Device& ctx,ObservedDraw& draw,HRESULT result) {
    if(!ctx.capture)return;
    auto& input=draw.input;
    DrawInputReader::complete(input,result);
    const auto& o=input.observation;const auto& k=o.key;
    object_lifetime::Snapshot after{};
    if(draw.lifetime.known) {
        const bool known=object_lifetime::current(draw.registry,k.node,k.node_handle,k.camera,k.camera_handle,&after);
        if(!known||after.observer_epoch!=draw.lifetime.observer_epoch||after.load_epoch!=draw.lifetime.load_epoch||
           after.registry_epoch!=draw.lifetime.registry_epoch||after.mutation_revision!=draw.lifetime.mutation_revision||
           after.node_serial!=draw.lifetime.node_serial||after.camera_serial!=draw.lifetime.camera_serial)
            input.observation.proofs&=~renderer::LifetimeVerified;
    } else after=draw.lifetime;
    if(draw.scene_draw)ctx.motion.observe(input,draw.registry,draw.lifetime);
    log("motion_input device=%llu frame=%llu index=%llu blockers=%08lx proofs=%lu position_path=%u vs=%016llx ps=%016llx declaration=%016llx rows_hash=%016llx color=%llu depth=%llu width=%u height=%u cull=%u vb=%llu vb_revision=%llu ib=%llu ib_revision=%llu position_offset=%u position_type=%u lifetime_verified=%u vertex_finite_verified=%u",
        ctx.id,ctx.frame,ctx.draws,static_cast<DWORD>(input.blockers),static_cast<DWORD>(o.proofs),unsigned(input.position_path),
        input.vertex_program,input.pixel_program,k.declaration,hash_bytes(o.submitted_wvp.data(),sizeof(o.submitted_wvp)),
        input.color_target,input.depth_target,input.width,input.height,unsigned(input.cull),
        k.vertex_buffer,k.vertex_revision,k.index_buffer,k.index_revision,k.position_offset,k.position_type,
        (o.proofs&renderer::LifetimeVerified)!=0,input.vertex_finite_verified);
    const auto& finite=input.finite_positions;const auto& indices=input.indices;
    log("motion_geometry device=%llu frame=%llu index=%llu source_qualified=%u source_hash=%016llx source_words=%u finite_requested=%u finite_state=%u finite_reason=%u finite_status=%08lx finite_generation=%llu finite_revision=%llu index_required=%u index_requested=%u index_known=%u index_range_verified=%u index_exact=%u index_min=%u index_max=%u index_reason=%u index_status=%08lx index_generation=%llu index_revision=%llu",
        ctx.id,ctx.frame,ctx.draws,input.replay_source.qualified(),input.replay_source.source_hash(),input.replay_source.source_words(),
        finite.requested,unsigned(finite.state),unsigned(finite.reason),finite.status,finite.generation,finite.revision,
        k.indexed,indices.requested,indices.known,input.index_range_verified,indices.exact_range,indices.minimum,indices.maximum,
        unsigned(indices.reason),indices.status,indices.generation,indices.revision);
    // Preserve the terminal failure record if the observer disabled itself
    // during either lookup. Its current active state must not hide that cause.
    if(draw.lifetime_observed)
        log("motion_lifetime device=%llu frame=%llu index=%llu before_known=%u after_known=%u before_reason=%u after_reason=%u registry=%p observer_epoch=%llu load_epoch=%llu registry_epoch=%llu mutation_before=%llu mutation_after=%llu node_serial=%llu camera_serial=%llu observer_epoch_after=%llu load_epoch_after=%llu registry_epoch_after=%llu node_serial_after=%llu camera_serial_after=%llu",
            ctx.id,ctx.frame,ctx.draws,draw.lifetime.known,after.known,unsigned(draw.lifetime.reason),unsigned(after.reason),
            reinterpret_cast<void*>(draw.registry),draw.lifetime.observer_epoch,draw.lifetime.load_epoch,draw.lifetime.registry_epoch,
            draw.lifetime.mutation_revision,after.mutation_revision,draw.lifetime.node_serial,draw.lifetime.camera_serial,
            after.observer_epoch,after.load_epoch,after.registry_epoch,after.node_serial,after.camera_serial);
}
// Drains the pending unknown-program lines and, when `population` is set and
// a counter moved, the session population line. Called from Present at the
// 300-frame cadence and once more when a device is destroyed, so the last
// movement of a session is not lost. Telemetry only; no other output.
void report_shader_population(bool population) {
    if(!telemetry::enabled())return;
    renderer::ShaderPopulation::Entry unknown{};
    while(shader_population.take(unknown))
        log("shader_unknown kind=%s id=%016llx version=%08lx bytes=%lu tables=%lu",
            unknown.vertex?"vs":"ps",static_cast<unsigned long long>(unknown.hash),
            static_cast<unsigned long>(unknown.version),static_cast<unsigned long>(unknown.bytes),
            static_cast<unsigned long>(renderer::shader_table_count()));
    if(population&&shader_population.counts_changed())
        log("shader_population known=%lu unknown=%lu overflow=%lu",
            static_cast<unsigned long>(shader_population.known()),
            static_cast<unsigned long>(shader_population.unknown()),
            static_cast<unsigned long>(shader_population.overflow()));
}
template<typename Shader> uint64_t shader_id(Shader* shader, const char* kind) {
    if (!shader) return 0;
    telemetry::Scope inspect(telemetry::process(),telemetry::Metric::ShaderInspect);
    const auto get_begin=telemetry::now();
    UINT bytes = 0;
    if (FAILED(shader->GetFunction(nullptr, &bytes)) || !bytes || bytes > 4*1024*1024) return 0;
    std::vector<unsigned char> code(bytes);
    if (FAILED(shader->GetFunction(code.data(), &bytes))) return 0;
    telemetry::record(telemetry::process(),telemetry::Metric::ShaderGetFunction,telemetry::now()-get_begin);
    const auto hash_begin=telemetry::now();
    auto hash = hash_bytes(code.data(), bytes);
    telemetry::record(telemetry::process(),telemetry::Metric::ShaderHash,telemetry::now()-hash_begin,false,bytes);
    if (dumped.insert(hash).second) {
        const auto dump_begin=telemetry::now();
        size_t written=0;bool dump_ok=false;
        // Once per distinct program, from the draw hooks' capture path: the
        // wide formatter is x87 CRT code, so the dump runs under its own
        // CPU-state envelope (cpu_state.h call_preserved).
        call_preserved([&]{
            wchar_t suffix[80];
            swprintf(suffix, 80, L"\\%hs_%016llx.bin", kind, static_cast<unsigned long long>(hash));
            FILE* file = _wfopen((directory + suffix).c_str(), L"wb");
            if (file) { written=fwrite(code.data(),1,bytes,file);const int closed=fclose(file);dump_ok=written==bytes&&closed==0; }
        });
        telemetry::record(telemetry::process(),telemetry::Metric::ShaderDump,telemetry::now()-dump_begin,!dump_ok,written);
        log("shader kind=%s id=%016llx bytes=%u dumped=%u", kind,
            static_cast<unsigned long long>(hash), bytes, dump_ok);
        // Classify this distinct program against every table the proxy keys
        // on, so a mod that changes what the game compiles is visible in the
        // log. Telemetry only, once per program, never per draw; the line
        // itself is emitted at the next Present.
        if (telemetry::enabled()) {
            std::uint32_t version = 0;
            if (bytes >= sizeof version) std::memcpy(&version, code.data(), sizeof version);
            shader_population.observe(hash, kind[0]=='v', version, bytes);
        }
    }
    return hash;
}
void surface_info(const char* name, IDirect3DSurface9* surface) {
    if (!surface) { log("surface role=%s ptr=0 identity=0",name); return; }
    IDirect3DBaseTexture9* container = nullptr;
    const HRESULT container_result=surface->GetContainer(IID_IDirect3DBaseTexture9,reinterpret_cast<void**>(&container));
    const auto parent_id=resource_id(container);
    const UINT parent_type=container ? container->GetType() : 0;
    if (container) container->Release();
    const auto id=resource_id(surface);
    D3DSURFACE_DESC desc{};
    const HRESULT result=surface->GetDesc(&desc);
    if (SUCCEEDED(result))
        log("surface role=%s ptr=%p identity=%llu width=%u height=%u format=%u usage=%lu msaa=%u container=%llu container_type=%u container_result=%08lx", name,
            surface,id,desc.Width,desc.Height,desc.Format,desc.Usage,desc.MultiSampleType,parent_id,parent_type,container_result);
    else log("surface role=%s ptr=%p identity=%llu result=%08lx",name,surface,id,result);
}
void ownership_depth_info(IDirect3DDevice9* d, uint64_t device, uint64_t frame, const char* phase) {
    // This is a borrowed diagnostic snapshot, never a resource adoption or a
    // GPU allocation. Native/default mode must not query ownership internals.
    if (!ownership::borrowed_native_device(d)) return;
    ownership::CopyDepthView view{};
    const HRESULT result = ownership::get_copy_depth_view(d, &view);
    const auto& desc = view.source_desc;
    log("ownership_copy_depth phase=%s device=%llu frame=%llu result=%08lx status=%08lx requested=%u available=%u source_bound=%u copy_valid=%u generation=%llu source_epoch=%llu copy_epoch=%llu source_width=%u source_height=%u source_format=%u source_type=%u source_usage=%lu source_pool=%u source_msaa=%u source_quality=%lu",
        phase,device, frame,result,view.status,view.requested,view.available,view.source_bound,view.copy_valid,
        view.generation,view.source_epoch,view.copy_epoch,desc.Width,desc.Height,desc.Format,desc.Type,
        desc.Usage,desc.Pool,desc.MultiSampleType,desc.MultiSampleQuality);
}
// First successful BeginScene, with a Present fallback for frames without one.
// Existing CPU boundary and HookGuard cover this reader; it adds no GPU work.
void sector_background_context(Device& ctx, bool scene_authority = false) {
    if(!(sector_background_requested || volumetric_fog_requested) || !ctx.sector_background_evidence.begin(ctx.frame))return;
    const DWORD saved_error=GetLastError();
    struct RestoreError { DWORD value; ~RestoreError(){SetLastError(value);} } restore_error{saved_error};
    sector_background::Sample value;
    if(object_trace::executable_verified()) {
        // Even without the motion route, revalidate pages after a load/realloc.
        engine_memory::next_frame();
        auto read=[](std::uintptr_t p,void* out,std::size_t n){return engine_memory::read(p,out,n);};
        value=sector_background::sample(read);
    } else value.status=sector_background::Status::ForeignExecutable;
    if(scene_authority && volumetric_fog_requested)ctx.motion_output.volumetric_fog_sector_sample(ctx.frame,value);
    if(!sector_background_requested || !ctx.sector_background_evidence.emit(value,GetTickCount64()))return;
    log("sector_background device=%llu frame=%llu status=%s registry=%08x active_handle=%u cockpit=%08x sector=%08x class48=%d index=%d count=%d table=%08x R=%08x row_valid=%u name_ptr=%08x name_valid=%u name=\"%s\" dust=%d near=%d far=%d stardust=%d rate0=%d rate1=%d rate2=%d rate3=%d rate4=%d rate5=%d rate6=%d rate7=%d neb=%08x stars=%08x camera=%08x camera_valid=%u cam_near=%d cam_far=%d flags270=%08x camera_check=%s config_valid=%u config768=%d far_floor=%d effective_far=%d ref_object=%08x ref_sector=%08x anchor_check=%s",
        ctx.id,ctx.frame,sector_background::name(value.status),value.registry,value.handle,value.cockpit,value.sector,int(value.class48),value.index,value.count,value.table,value.record,unsigned(value.row_valid),value.name_pointer,unsigned(value.name_valid),value.family,
        value.dust,value.fog_near,value.fog_far,value.stardust,value.rates[0],value.rates[1],value.rates[2],value.rates[3],value.rates[4],value.rates[5],value.rates[6],value.rates[7],value.neb,value.stars,value.camera,unsigned(value.camera_valid),value.cam_near,value.cam_far,value.flags270,sector_background::name(value.camera_check),unsigned(value.config_valid),value.config,value.far_floor,value.effective_far,value.ref_object,value.ref_sector,sector_background::name(value.anchor_check));
}
void object_context(Device& ctx) {
    // Capture-only checked reads and logging must not leak a Windows error.
    const DWORD saved_error=GetLastError();
    struct RestoreError { DWORD value; ~RestoreError(){SetLastError(value);} } restore_error{saved_error};
    if (!object_trace::active()) return;
    object_trace::Snapshot value{};
    const bool scoped=object_trace::current(&value);
    log("object_context device=%llu frame=%llu index=%llu scoped=%u valid=%lu session=%llu scope_depth=%lu mesh=%p node=%p node_handle=%lu camera=%p camera_handle=%lu registry=%p engine=%p model=%08lx lod=%08lx flags12c=%08lx flags130=%08lx",
        ctx.id,ctx.frame,ctx.draws,scoped,static_cast<unsigned long>(value.valid),value.session,
        static_cast<unsigned long>(value.scope_depth),reinterpret_cast<void*>(value.mesh),
        reinterpret_cast<void*>(value.node),static_cast<unsigned long>(value.node_handle),
        reinterpret_cast<void*>(value.camera),static_cast<unsigned long>(value.camera_handle),
        reinterpret_cast<void*>(value.registry),reinterpret_cast<void*>(value.engine),
        static_cast<unsigned long>(value.model),static_cast<unsigned long>(value.lod),
        static_cast<unsigned long>(value.flags12c),static_cast<unsigned long>(value.flags130));
    if(!scoped)return;
    auto read=[](std::uintptr_t p,void* out,std::size_t n){return engine_memory::read(p,out,n);};
    auto& evidence=ctx.object_evidence;
    if(evidence.begin(ctx.frame,ctx.reset_generation,read,0x608504)) {
        const auto& t=evidence.selected;
        log("object_target device=%llu frame=%llu reset=%llu epoch=%llu index=%llu status=%s registry=%08x active_handle=%u cockpit=%08x camera=%08x target=%08x target_id=%u root=%08x root_handle=%u",
            ctx.id,ctx.frame,ctx.reset_generation,evidence.epoch,ctx.draws,object_capture::name(t.status),t.registry,t.handle,t.cockpit,t.camera,t.target,t.target_id,t.root,t.root_handle);
    }
    unsigned ancestry_id=0,camera_id=0;bool fresh=false;
    if(value.valid&object_trace::Node) {
        ancestry_id=evidence.node(std::uint32_t(value.node),value.node_handle,value.parent,fresh);
        if(fresh) {
            const auto a=object_capture::ancestry(read,std::uint32_t(value.node),value.node_handle,value.parent,evidence.selected);
            log("object_ancestry device=%llu frame=%llu reset=%llu epoch=%llu index=%llu id=%u status=%s count=%u",ctx.id,ctx.frame,ctx.reset_generation,evidence.epoch,ctx.draws,ancestry_id,object_capture::name(a.status),a.count);
            for(unsigned i=0;i<a.count;++i)log("object_ancestor device=%llu frame=%llu reset=%llu epoch=%llu id=%u link=%u node=%08x handle=%u",ctx.id,ctx.frame,ctx.reset_generation,evidence.epoch,ancestry_id,i,a.links[i].node,a.links[i].handle);
        }
    }
    if(value.valid&object_trace::Camera) {
        camera_id=evidence.camera(std::uint32_t(value.camera),value.camera_handle,fresh);
        if(fresh) {
            const auto f=object_capture::fade(read,std::uint32_t(value.camera),0x606f34);
            log("object_fade device=%llu frame=%llu reset=%llu epoch=%llu index=%llu id=%u camera=%08x handle=%u valid=%u context=%08x flags270=%08x position=%08x,%08x,%08x near36c=%08x far370=%08x scale_bits=%08x config768=%08x",
                ctx.id,ctx.frame,ctx.reset_generation,evidence.epoch,ctx.draws,camera_id,std::uint32_t(value.camera),value.camera_handle,f.valid,f.context,f.flags,f.position[0],f.position[1],f.position[2],f.near_bits,f.far_bits,f.scale_bits,f.config);
        }
    }
    log("object_evidence device=%llu frame=%llu reset=%llu epoch=%llu index=%llu node_valid=%u parent=%08x alpha13c=%08x ancestry_id=%u ancestry_capacity=%u camera_valid=%u fade_id=%u fade_capacity=%u",
        ctx.id,ctx.frame,ctx.reset_generation,evidence.epoch,ctx.draws,bool(value.valid&object_trace::Node),value.parent,value.alpha13c,ancestry_id,
        bool(value.valid&object_trace::Node)&&!ancestry_id,bool(value.valid&object_trace::Camera),camera_id,bool(value.valid&object_trace::Camera)&&!camera_id);
    const auto rows=[&](const char* role,const uint32_t* bits,unsigned count){
        for(unsigned row=0;row<count;++row)
            log("object_matrix role=%s row=%u bits=%08lx,%08lx,%08lx,%08lx",role,row,
                static_cast<unsigned long>(bits[row*4]),static_cast<unsigned long>(bits[row*4+1]),
                static_cast<unsigned long>(bits[row*4+2]),static_cast<unsigned long>(bits[row*4+3]));
    };
    if(value.valid&object_trace::World)rows("world",value.world,4);
    if(value.valid&object_trace::WorldBasis)rows("world_basis",value.world_basis,4);
    if(value.valid&object_trace::View)rows("view",value.view,4);
    if(value.valid&object_trace::Projection)rows("projection",value.projection,4);
    if(value.valid&object_trace::Node){
        log("object_position bits=%08lx,%08lx,%08lx",static_cast<unsigned long>(value.position[0]),static_cast<unsigned long>(value.position[1]),static_cast<unsigned long>(value.position[2]));
        rows("scale",value.scale,1);
        for(unsigned row=0;row<3;++row)
            log("object_basis row=%u bits=%08lx,%08lx,%08lx",row,static_cast<unsigned long>(value.basis[row*3]),static_cast<unsigned long>(value.basis[row*3+1]),static_cast<unsigned long>(value.basis[row*3+2]));
    }
}
// X3M_FRAME_TIMING only: the per-draw program-pair and batchability counters
// (frame_timing.h). One predictable branch when the option is off; when it is
// on, one copy of the binding shadow the proxy already keeps (no Get* call, no
// allocation) and a table increment plus one comparison against the previous
// draw, under the hook mutex the draw path already holds.
void frame_timing_draw_state(Device& ctx, D3DPRIMITIVETYPE type, UINT primitives, INT base_vertex, UINT start_index,
                             bool user_memory) {
    if (!frame_timing::active) return;
    const auto bindings = ctx.motion_output.binding_shadow();
    frame_timing::DrawKey key;
    key.vs = bindings.vs_hash; key.ps = bindings.ps_hash;
    key.stream0 = bindings.stream0; key.indices = bindings.indices; key.declaration = bindings.declaration;
    for (unsigned stage = 0; stage < 4; ++stage) key.textures[stage] = bindings.textures[stage];
    key.primitive_type = static_cast<std::uint32_t>(type);
    key.primitives = static_cast<std::uint32_t>(primitives);
    key.start_index = static_cast<std::uint32_t>(start_index);
    key.base_vertex = static_cast<std::int32_t>(base_vertex);
    key.valid = bindings.valid;
    key.user_memory = user_memory; // D3D9 clears stream 0: never batched against a shadowed mesh
    frame_timing::draw_state(key);
}
void snapshot(IDirect3DDevice9* d, const char* kind, D3DPRIMITIVETYPE type, UINT primitives, bool user_memory=false,
              INT base_vertex=0, UINT start_index=0) {
    auto& ctx = *devices.at(d);
    ++ctx.draws;
    frame_timing::draw(primitives); // X3M_FRAME_TIMING only: one branch, one add
    frame_timing_draw_state(ctx,type,primitives,base_vertex,start_index,user_memory);
    if (!ctx.capture) return;
    telemetry::Scope timed(ctx.stats,telemetry::Metric::Snapshot);
    capture_event(ctx,"draw_begin",S_OK,true);
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    d->GetVertexShader(&vs); d->GetPixelShader(&ps);
    const auto vhash = shader_id(vs,"vs"), phash = shader_id(ps,"ps");
    if (vs) vs->Release();
    if (ps) ps->Release();
    log("draw device=%llu frame=%llu index=%llu kind=%s topology=%u primitives=%u vs=%016llx ps=%016llx",
        ctx.id,ctx.frame,ctx.draws,kind,type,primitives,vhash,phash);
    object_context(ctx);
    capture_geometry(d,user_memory,ctx.caps);
    IDirect3DSurface9* rt = nullptr;
    for (DWORD i = 0; i < 4; ++i) {
        if (SUCCEEDED(d->GetRenderTarget(i,&rt)) && rt) {
            const char role[4]={'r','t',char('0'+i),'\0'}; // no snprintf: the draw hooks are light-envelope code and the CRT formatter is x87
            surface_info(role,rt); rt->Release(); rt = nullptr;
        }
    }
    if (SUCCEEDED(d->GetDepthStencilSurface(&rt)) && rt) { surface_info("depth",rt); rt->Release(); }
    for (auto state : {D3DRS_FOGENABLE,D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHATESTENABLE,
                       D3DRS_ALPHAREF,D3DRS_ALPHAFUNC,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,
                       D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_CULLMODE,D3DRS_COLORWRITEENABLE,
                       D3DRS_SRGBWRITEENABLE,D3DRS_SEPARATEALPHABLENDENABLE,
                       // Alpha can inherit a different equation from RGB. Keep
                       // its factors/op and constant in the same F8 snapshot.
                       D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,D3DRS_BLENDOPALPHA,
                       D3DRS_BLENDFACTOR,
                       D3DRS_STENCILENABLE,D3DRS_STENCILFUNC,D3DRS_STENCILREF,
                       D3DRS_STENCILMASK,D3DRS_STENCILWRITEMASK,D3DRS_STENCILFAIL,
                       D3DRS_STENCILZFAIL,D3DRS_STENCILPASS,D3DRS_TWOSIDEDSTENCILMODE,
                       D3DRS_CCW_STENCILFUNC,D3DRS_CCW_STENCILFAIL,
                       D3DRS_CCW_STENCILZFAIL,D3DRS_CCW_STENCILPASS}) {
        DWORD value = 0;
        HRESULT hr = d->GetRenderState(state,&value);
        if (SUCCEEDED(hr)) log("state id=%u value=%lu",state,value);
    }
    D3DVIEWPORT9 vp{};
    if (SUCCEEDED(d->GetViewport(&vp))) log("viewport x=%lu y=%lu w=%lu h=%lu minz=%g maxz=%g",vp.X,vp.Y,vp.Width,vp.Height,vp.MinZ,vp.MaxZ);
    for (DWORD i=0; i<16; ++i) {
        IDirect3DBaseTexture9* texture = nullptr;
        if (SUCCEEDED(d->GetTexture(i,&texture)) && texture) {
            log("texture stage=%lu ptr=%p type=%u identity=%llu levels=%lu",i,texture,texture->GetType(),resource_id(texture),texture->GetLevelCount());
            if (texture->GetType()==D3DRTYPE_TEXTURE) {
                D3DSURFACE_DESC desc{};
                if (SUCCEEDED(static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0,&desc)))
                    log("texture_desc stage=%lu w=%u h=%u format=%u",i,desc.Width,desc.Height,desc.Format);
            }
            texture->Release();
        }
        for (auto state : {D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_MAXANISOTROPY,D3DSAMP_SRGBTEXTURE}) {
            DWORD value = 0;
            if (SUCCEEDED(d->GetSamplerState(i,state,&value))) log("sampler stage=%lu state=%u value=%lu",i,state,value);
        }
        // The LOD bias is a float bit pattern in the DWORD: logged raw (same
        // line shape as above) and reinterpreted; MAXMIPLEVEL shows whether
        // level 0 of the chain is reachable (sampler-states-and-mips.md, 5).
        DWORD value = 0;
        if (SUCCEEDED(d->GetSamplerState(i,D3DSAMP_MIPMAPLODBIAS,&value))) {
            float bias = 0.f; memcpy(&bias,&value,sizeof bias);
            log("sampler stage=%lu state=%u value=%lu bias=%g",i,unsigned(D3DSAMP_MIPMAPLODBIAS),value,double(bias));
        }
        if (SUCCEEDED(d->GetSamplerState(i,D3DSAMP_MAXMIPLEVEL,&value))) log("sampler stage=%lu state=%u value=%lu",i,unsigned(D3DSAMP_MAXMIPLEVEL),value);
    }
    IDirect3DVertexDeclaration9* declaration = nullptr;
    if (SUCCEEDED(d->GetVertexDeclaration(&declaration)) && declaration) {
        D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1]{}; UINT count=MAXD3DDECLLENGTH+1;
        if (SUCCEEDED(declaration->GetDeclaration(elements,&count)))
            for (UINT i=0; i<count; ++i) log("vertex_element stream=%u offset=%u type=%u method=%u usage=%u index=%u",elements[i].Stream,elements[i].Offset,elements[i].Type,elements[i].Method,elements[i].Usage,elements[i].UsageIndex);
        declaration->Release();
    }
    for (auto state : {D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION}) {
        D3DMATRIX m{};
        if (SUCCEEDED(d->GetTransform(state,&m)))
            for (unsigned i=0;i<4;++i) log("transform state=%u row=%u values=%.9g,%.9g,%.9g,%.9g",state,i,m.m[i][0],m.m[i][1],m.m[i][2],m.m[i][3]);
    }
    capture_constants(d,true,ctx.caps); capture_constants(d,false,ctx.caps);
}
void final_admission_metric(ownership::AdmissionMonitor* monitor,const char* phase) {
    const auto state=ownership::admission_snapshot(monitor);
    log("application_admission_final phase=%s active_roots=%llu waiting_roots=%llu admitted_roots=%llu promotions=%llu vetoes=%u first_veto=%u enabled=%u",
        phase,state.active_roots,state.waiting_roots,state.admitted_roots,state.promotions,
        unsigned(state.vetoes),unsigned(state.first_veto),monitor!=nullptr);
}
ULONG WINAPI release_device(IDirect3DDevice9* d) {
    CpuCallBoundary cpu;
    auto* monitor=ownership::process_admission_monitor();
    ownership::ApplicationAdmissionAbi admission(monitor);
    ULONG refs;bool last_device_destroyed=false;
    {
        HookGuard lock;
        auto& ctx=*devices.at(d);
        auto fn=ctx.get<ULONG (WINAPI*)(IDirect3DDevice9*)>(2);
        // A registered invocation owns an explicit native pin. Delay final
        // retirement until its cleanup drops transient aliases and releases that
        // pin through this hook. Never count transient surface aliases as native
        // device references or manufacture a zero return for the application.
        const bool accounting = !ctx.compositor && !ctx.bloom_busy
            && !ctx.motion_output.reference_accounting_busy() && !ctx.bloom.releasing();
        // Caster retention (shadow-caster-retention.md, "References"): a retained
        // resource the application already released pins one device reference the
        // accounting below cannot see, so the final-Release probe would never
        // match. While the count is within the store's references of the final
        // one the store is flushed first (flush=teardown); zero cost while the
        // store holds nothing, which is always the case with the option off.
        if (accounting) {
            const unsigned retained = ctx.motion_output.retention_references();
            if (retained) {
                ctx.get<ULONG (WINAPI*)(IDirect3DDevice9*)>(1)(d);
                const ULONG now = fn(d);
                if (now <= ctx.motion_output.device_references() + ctx.bloom.references() + 1 + retained) ctx.motion_output.retention_before_final_release();
            }
        }
        const unsigned held = accounting
            ? ctx.motion_output.device_references() + ctx.bloom.references() : 0;
        if (held) {
            ctx.motion_output.restore_bindings();
            const ULONG count=ctx.get<ULONG (WINAPI*)(IDirect3DDevice9*)>(1)(d);
            const ULONG after=fn(d);
            if(after==held+1){
                BloomOperation internal(ctx);
                ctx.bloom.shutdown(); ctx.motion_output.release_resources();
                log("motion_output_release device=%llu held=%u count=%lu released=1",ctx.id,held,count);
            }
        }
        cpu.before_original();
        refs=fn(d);cpu.after_original();
        if(!refs){game_phases::invalidate_device();report_shader_population(true); // session end: flush the last count movement
        telemetry::summary(devices.at(d)->stats,"device_destroy",devices.at(d)->frame);telemetry::summary(telemetry::process(),"device_destroy",devices.at(d)->frame);log("device_destroy ptr=%p device=%llu",d,devices.at(d)->id);forget_cached_device();devices.erase(d);}
        last_device_destroyed=!refs&&devices.empty();
    }
    // The profiler's quiescent stop: the last device is gone and the capture
    // mutex is released, so its final report cannot wait on a lock we hold.
    if(last_device_destroyed){
        sampling_profiler::shutdown();
        // The scene patch/binding remains installed for process lifetime. A
        // bridge may still be returning after this final native Release.
        chase_camera::note_last_device(); // kept for the process lifetime (review 31 A3): a recreated device could not re-claim the site
        resource_reader::report(); // final summary without telemetry; the reader itself stays installed (loading continues without a device)
        loading_trace::crypt_cache_report("session"); // cumulative totals; bounded native cache retained until process exit
        voice_dmo_fallback::shutdown(); // disarms the fault witness; the site patch stays with the other claims
        ownership::set_surface_lock_observer(nullptr); // the video blit witness: no surface outlives the last device, and the log closes after this
    }
    if(!refs){
        // A nested final factory Release can report this device root still
        // active. Finish the outer device root only after its own cleanup.
        admission.finish();
        final_admission_metric(monitor,"device");
    }
    return refs;
}
// Exact owner qualification remains separate from the game-call transport.
// This work is once per compositor, not part of any draw/setter hook.
enum class BloomRefusal : unsigned { Caller, Owner, Device, Nested, Thread, Reset, Glow,
    Scene, Pass, Boundary, Post, Count };
constexpr const char* bloom_refusal_names[] = {"caller", "owner", "device", "nested", "thread", "reset",
    "glow_off", "scene_handoff", "pass_unavailable", "boundary", "post_qualification"};
std::uint64_t bloom_calls = 0, bloom_refusals[unsigned(BloomRefusal::Count)]{};
void bloom_refuse(BloomRefusal reason,const Device* ctx=nullptr) noexcept {
    const auto index=unsigned(reason);
    const auto count=++bloom_refusals[index];
    if(count==1)log("bloom_refusal reason=%s count=%llu device=%llu ordinary_signal=%u",
        bloom_refusal_names[index],count,ctx?ctx->id:0,
        unsigned(reason==BloomRefusal::Glow || reason==BloomRefusal::Scene || reason==BloomRefusal::Pass
            || reason==BloomRefusal::Boundary || reason==BloomRefusal::Post));
}
bool compositor_glow_enabled(std::uintptr_t base) noexcept {
    std::uint32_t settings=0; unsigned char flags=0; SIZE_T copied=0;
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+0x206f34),
            &settings,sizeof settings,&copied) || copied!=sizeof settings
            || !settings || settings>UINT32_MAX-0x100u)return false;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(settings+0x100u),
        &flags,sizeof flags,&copied) && copied==sizeof flags && (flags&0x80u);
}
void retain_compositor_scene(void* storage,const MotionHdrScene& scene) noexcept {
    auto& call=*static_cast<CompositorInvocation*>(storage);
    const auto& ctx=*call.owner;
    if(call.revoked || call.input.scene || scene.device!=call.device || scene.device_id!=ctx.id
            || scene.frame!=ctx.frame || !scene.display.valid || !scene.scene || !scene.main)return;
    // Synchronous retain/copy only. MotionOutput still owns its writeback state;
    // no preparation, Reset or renderer reentry until scene_end_hook returns.
    scene.scene->AddRef(); call.input.scene=scene.scene;
    scene.main->AddRef(); call.input.boundary.main=scene.main;
    call.input.agx=scene.display.agx;
    call.input.decode=scene.display.decode;
    call.input.sharpen=scene.display.sharpen;
    call.input.sharpen_constants=scene.display.sharpen_constants;
    call.input.exact_sharpen=true;
    // Restore the native scene alpha's authored colored-glow intent before
    // AgX, with a complementary HDR-highlight contribution. These are linear
    // art weights, not a reproduction of the native display-space screen blend.
    // Favor the finer pyramid levels for a tighter core, with a small authored
    // gain increase and less weight in the widest halo.
    call.input.filter.authored_glow_gain=0.375f;
    call.input.filter.scatter=0.65f;
    call.input.filter.highlight_gain=0.05f;
    // Bloom-only source ceiling: the pyramid sees at most this decoded code,
    // the presented scene keeps its full HDR value (bloom-falloff.md).
    call.input.filter.source_clamp=bloom_source_clamp;
    // OFF retains the same filtering/RGB replacement, with zero final gain.
    // Skipping replacement would restore the original compositor's glow.
    call.input.filter.strength=ctx.comparison.bloom_requested?1.f:0.f;
}
bool compositor_current(const CompositorInvocation& call,bool refresh_owner=true) noexcept {
    if(!call.owner || !call.native_pin || call.revoked)return false;
    const Device& ctx=*call.owner;
    const auto found=devices.find(call.device);
    if(found==devices.end() || found->second.get()!=&ctx || ctx.compositor!=&call
            || ctx.reset_active || ctx.reset_generation!=call.input.boundary.reset
            || ctx.frame!=call.input.boundary.frame || ctx.scene_thread!=call.input.boundary.thread
            || GetCurrentThreadId()!=ctx.scene_thread || !ctx.motion_output.bloom_boundary_available())return false;
    if(!refresh_owner)return true;
    compositor_owner::Snapshot now{};
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    return compositor_owner::read(base,now)==compositor_owner::Result::Ok
        && compositor_owner::same(call.identity,now) && compositor_glow_enabled(base);
}
void compositor_pre(const X3mCompositorFrame* frame,void* storage,void*) {
    // This constructor is nonthrowing; cleanup can always destroy the object,
    // including when admission declines before a context is acquired.
    auto& call=*new(storage) CompositorInvocation{};
    CaptureLock lock;
    frame_timing::Scope timing(frame_timing::Bucket::Scene,"compositor_pre"); // X3M_FRAME_TIMING only
    if(++bloom_calls%300==0)
        log("bloom_admission calls=%llu caller=%llu owner=%llu device=%llu nested=%llu thread=%llu reset=%llu glow_off=%llu scene_handoff=%llu pass_unavailable=%llu boundary=%llu post_qualification=%llu",
            bloom_calls,bloom_refusals[0],bloom_refusals[1],bloom_refusals[2],bloom_refusals[3],bloom_refusals[4],bloom_refusals[5],
            bloom_refusals[6],bloom_refusals[7],bloom_refusals[8],bloom_refusals[9],bloom_refusals[10]);
    if(!scene_hook::compositor_active() || frame->caller_pc!=scene_hook::compositor_caller_pc()
            || !frame->caller_stack || (frame->caller_stack&3u)){bloom_refuse(BloomRefusal::Caller);return;}
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(compositor_owner::read(base,call.identity)!=compositor_owner::Result::Ok){bloom_refuse(BloomRefusal::Owner);return;}
    call.device=reinterpret_cast<IDirect3DDevice9*>(call.identity.device);
    const auto found=devices.find(call.device);
    if(found==devices.end()){bloom_refuse(BloomRefusal::Device);return;}
    Device& ctx=*found->second;
    if(ctx.compositor){
        // A nested original still executes, but invalidates the outer ticket;
        // no separately retained resources are destroyed inside its GPU call.
        ctx.compositor->revoked=true; ctx.compositor->ready=false; bloom_refuse(BloomRefusal::Nested,&ctx);return;
    }
    if(ctx.reset_active){bloom_refuse(BloomRefusal::Reset,&ctx);return;}
    if(!ctx.scene_thread || ctx.scene_thread!=GetCurrentThreadId()){bloom_refuse(BloomRefusal::Thread,&ctx);return;}
    call.owner=found->second;
    ctx.get<ULONG (WINAPI*)(IDirect3DDevice9*)>(1)(call.device); call.native_pin=true;
    ctx.compositor=&call;
    call.input.boundary.frame=ctx.frame; call.input.boundary.reset=ctx.reset_generation;
    call.input.boundary.thread=ctx.scene_thread;
    BloomOperation internal(ctx);
    const bool glow=compositor_glow_enabled(base);
    // Even glow-off and unavailable replacement retain the ordinary route's
    // scene-end resolve/writeback. New work consumes its exact completed image.
    ctx.motion_output.scene_end_hook(glow ? &retain_compositor_scene : nullptr,&call);
    if(!glow){bloom_refuse(BloomRefusal::Glow,&ctx);return;}
    if(!call.input.scene || !call.input.boundary.main || !compositor_current(call,false)){
        bloom_refuse(BloomRefusal::Scene,&ctx);return;
    }
    if(!ctx.bloom_attempted){
        ctx.bloom_attempted=true;
        const HRESULT hr=ctx.bloom.attach(call.device,ctx.original,ctx.caps,renderer::bloom_programs());
        log("bloom_attach device=%llu result=%08lx reason=%s references=%u",ctx.id,hr,ctx.bloom.caps().reason,ctx.bloom.references());
    }
    if(!ctx.bloom.enabled()){bloom_refuse(BloomRefusal::Pass,&ctx);return;}
    const HRESULT main=call.input.boundary.main->GetDesc(&call.input.boundary.main_desc);
    const HRESULT depth=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9**)>(40)
        (call.device,&call.input.boundary.depth);
    if(FAILED(main) || (FAILED(depth)&&depth!=D3DERR_NOTFOUND)
            || (call.input.boundary.depth && FAILED(call.input.boundary.depth->GetDesc(&call.input.boundary.depth_desc)))){
        bloom_refuse(BloomRefusal::Boundary,&ctx);return;
    }
    call.input.boundary.admitted=true;
    const auto begin=telemetry::now();
    const auto prepared=ctx.bloom.prepare(call.input);
    if(!prepared.state_preserved)ctx.motion_output.stateblock_applied();
    if(prepared.ready){
        call.candidate=prepared.candidate;
        call.candidate.surface->AddRef();
        call.ready=compositor_current(call);
        if(call.ready)++ctx.bloom_prepared;
    }
    if((call.ready && ctx.bloom_prepared==1) || (!prepared.ready && ctx.bloom_failure_reports++<8) || ctx.frame%300==0)
    {
        char clamp_text[24];
        const float clamp=call.input.filter.source_clamp;
        if(clamp<x3::temporal::kAgxClampOff)std::snprintf(clamp_text,sizeof clamp_text,"%g",double(clamp));
        else std::snprintf(clamp_text,sizeof clamp_text,"none");
        log("bloom_prepare device=%llu frame=%llu ready=%u reason=%s operation=%08lx restore=%08lx state_preserved=%u clamp=%s cpu_ticks=%llu bytes=%llu",
            ctx.id,ctx.frame,call.ready,prepared.reason,prepared.operation,prepared.restore,prepared.state_preserved,
            clamp_text,telemetry::now()-begin,ctx.bloom.resource_bytes());
    }
}
void compositor_post(const X3mCompositorFrame*,void* storage,void*) {
    auto& call=*static_cast<CompositorInvocation*>(storage);
    CaptureLock lock;
    frame_timing::Scope timing(frame_timing::Bucket::Scene,"compositor_post"); // X3M_FRAME_TIMING only
    if(!call.ready)return;
    if(!compositor_current(call)){bloom_refuse(BloomRefusal::Post,call.owner.get());return;}
    Device& ctx=*call.owner;
    BloomOperation internal(ctx);
    const auto begin=telemetry::now();
    const auto result=ctx.bloom.commit(call.candidate,call.input.boundary);
    call.ready=false; // every candidate is consumed at most once
    if(!result.state_preserved)ctx.motion_output.stateblock_applied();
    if(result.committed){
        ++ctx.bloom_committed;ctx.bloom_effective_frame=ctx.frame;
        ctx.bloom_effective_on=ctx.comparison.bloom_requested;
    }
    if((result.committed && ctx.bloom_committed==1) || (!result.committed && ctx.bloom_failure_reports++<8) || ctx.frame%300==0)
        log("bloom_commit device=%llu frame=%llu committed=%u reason=%s operation=%08lx restore=%08lx recovery=%08lx recovery_restore=%08lx original_preserved=%u state_preserved=%u cpu_ticks=%llu",
            ctx.id,ctx.frame,result.committed,result.reason,result.operation,result.restore,result.recovery,result.recovery_restore,
            result.original_preserved,result.state_preserved,telemetry::now()-begin);
}
void compositor_cleanup(const X3mCompositorFrame*,void* storage,void*,int abnormal) {
    auto& call=*static_cast<CompositorInvocation*>(storage);
    IDirect3DDevice9* pin=nullptr;
    {
        CaptureLock lock;
        if(call.owner){
            Device& ctx=*call.owner;
            if(ctx.compositor==&call){
                revoke_compositor(ctx);
                ctx.compositor=nullptr;
            }
            if(abnormal && ctx.bloom_failure_reports++<8)
                log("bloom_original_abnormal device=%llu frame=%llu",ctx.id,call.input.boundary.frame);
        }
        if(call.native_pin){call.native_pin=false;pin=call.device;}
    }
    // Keep capture mutex released: terminal Release can stop the profiler and
    // emit final reports. The CPU pin survives any map erase inside this hook.
    if(pin)release_device(pin);
    call.~CompositorInvocation();
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
void bloom_fixture_pre(const X3mCompositorFrame*,void* storage,void*) {
    auto& call=*new(storage) CompositorInvocation{};
    CaptureLock lock;
    auto& f=bloom_lifetime_fixture; ++f.counts[0];
    const auto found=devices.find(f.device);
    if(found==devices.end() || found->second->compositor)return;
    auto& ctx=*found->second;
    f.counts[14]=ctx.motion_output.device_references();
    f.counts[15]=ctx.motion_output.hdr_enabled();
    f.counts[16]=ctx.motion_output.taa_enabled();
    call.owner=found->second; f.owner=call.owner; call.device=f.device;
    ctx.get<ULONG (WINAPI*)(IDirect3DDevice9*)>(1)(f.device); call.native_pin=true;
    ctx.compositor=&call;
    ctx.scene_thread=GetCurrentThreadId();
    call.input.boundary.frame=ctx.frame; call.input.boundary.reset=ctx.reset_generation;
    call.input.boundary.thread=ctx.scene_thread;
    BloomOperation internal(ctx);
    if(!f.scene || !f.main)return;
    f.scene->AddRef();call.input.scene=f.scene;
    f.main->AddRef();call.input.boundary.main=f.main;
    if(f.depth){f.depth->AddRef();call.input.boundary.depth=f.depth;}
    if(FAILED(f.main->GetDesc(&call.input.boundary.main_desc))
            || (f.depth && FAILED(f.depth->GetDesc(&call.input.boundary.depth_desc))))return;
    x3::temporal::prepare(call.input.agx,1.f,65504.f,x3::temporal::AgxDecode::gamma22,x3::temporal::AgxLook::none);
    call.input.sharpen=0.37f;
    x3::temporal::prepare_sharpen(call.input.sharpen_constants,call.input.sharpen,
        call.input.boundary.main_desc.Width,call.input.boundary.main_desc.Height);
    call.input.exact_sharpen=true;
    call.input.boundary.admitted=true;
    if(!ctx.bloom_attempted){
        ctx.bloom_attempted=true;
        f.counts[12]=unsigned(ctx.bloom.attach(f.device,ctx.original,ctx.caps,renderer::bloom_programs()));
    }
    if(!ctx.bloom.enabled())return;
    const auto prepared=ctx.bloom.prepare(call.input);
    f.counts[12]=unsigned(prepared.operation);
    if(prepared.ready){
        call.candidate=prepared.candidate;call.candidate.surface->AddRef();
        call.ready=true;++f.counts[4];
    }
}
void bloom_fixture_post(const X3mCompositorFrame*,void* storage,void*) {
    auto& call=*static_cast<CompositorInvocation*>(storage);
    CaptureLock lock;
    auto& f=bloom_lifetime_fixture; ++f.counts[1];
    if(!call.owner || !call.ready || call.revoked)return;
    auto& ctx=*call.owner;
    if(ctx.compositor!=&call || ctx.reset_active || ctx.reset_generation!=call.input.boundary.reset
            || ctx.frame!=call.input.boundary.frame || GetCurrentThreadId()!=call.input.boundary.thread)return;
    BloomOperation internal(ctx);
    const auto committed=ctx.bloom.commit(call.candidate,call.input.boundary);
    call.ready=false;f.counts[13]=unsigned(committed.operation);
    if(committed.committed)++f.counts[5];
}
void bloom_fixture_cleanup(const X3mCompositorFrame* frame,void* storage,void* context,int abnormal) {
    ++bloom_lifetime_fixture.counts[2];
    if(abnormal)++bloom_lifetime_fixture.counts[3];
    compositor_cleanup(frame,storage,context,abnormal);
}
#endif
void finite_upload_metrics(IDirect3DDevice9* device,const Device& ctx,const char* phase) {
    if(!finite_positions_requested)return;
    ownership::FiniteUploadStatistics s{};
    const HRESULT hr=ownership::get_finite_upload_statistics(device,&s);
    // Cumulative per-owner counters and current process reservations, emitted in
    // batches. No resource uploads or payload contents are logged individually.
    log("finite_upload_metric device=%llu frame=%llu phase=%s result=%08lx status=%08lx requested=%u active=%u generation=%llu payload_bytes=%llu peak_payload_bytes=%llu sidecars=%llu metadata_bytes=%llu global_payload_bytes=%llu global_sidecars=%llu uploads=%llu publications=%llu invalidations=%llu allocation_failures=%llu scans=%llu classified_bytes=%llu scan_ticks=%llu qualifier_ticks=%llu queries=%llu query_ticks=%llu query_cache_hits=%llu position_components=%llu",
        ctx.id,ctx.frame,phase,hr,s.status,s.requested,s.active,s.generation,s.payload_bytes,s.peak_payload_bytes,s.sidecars,s.metadata_bytes,
        s.global_payload_bytes,s.global_sidecars,s.uploads,s.publications,s.invalidations,s.allocation_failures,s.scans,s.classified_bytes,
        s.scan_ticks,s.qualifier_ticks,s.queries,s.query_ticks,s.query_cache_hits,s.position_components);
    if(s.first_refusal.available){
        const auto& f=s.first_refusal;
        log("finite_upload_first_refusal device=%llu frame=%llu phase=%s reason=%u name=%s type=%u format=%u pool=%u size=%u usage=%08lx lock_flags=%08lx",
            ctx.id,ctx.frame,phase,unsigned(f.reason),ownership::finite_evidence_reason_name(f.reason),
            unsigned(f.type),unsigned(f.format),unsigned(f.pool),f.size,static_cast<DWORD>(f.usage),static_cast<DWORD>(f.lock_flags));
    }
    for(unsigned i=0;i<ownership::finite_evidence_reason_count;++i)if(s.reasons[i])
        log("finite_upload_reason device=%llu frame=%llu phase=%s reason=%u name=%s count=%llu",ctx.id,ctx.frame,phase,i,
            ownership::finite_evidence_reason_name(static_cast<ownership::FiniteEvidenceReason>(i)),s.reasons[i]);
}
bool comparison_foreground() noexcept {
    DWORD process=0;
    const HWND window=GetForegroundWindow();
    return window && GetWindowThreadProcessId(window,&process) && process==GetCurrentProcessId();
}
const char* comparison_bloom_reason(const Device& ctx) noexcept {
    if(!bloom_requested)return "not_prepared";
    if(!scene_hook::compositor_active())return "boundary_unavailable";
    if(!ctx.bloom_attempted)return "waiting_scene";
    if(!ctx.bloom.enabled())return ctx.bloom.caps().reason;
    return "ready";
}
bool comparison_bloom_ready(const Device& ctx) noexcept {
    return bloom_requested && scene_hook::compositor_active() && ctx.bloom.enabled();
}
void comparison_log(Device& ctx,const char* phase,const char* key,bool accepted) noexcept {
    const auto exposure=ctx.motion_output.comparison_exposure();
    const bool bloom_effective=ctx.bloom_effective_frame==ctx.frame;
    log("renderer_comparison device=%llu frame=%llu phase=%s key=%s accepted=%u exposure=%s effective_ev=%.6g exposure_ready=%u exposure_used=%u exposure_reason=%s bloom_requested=%u bloom_ready=%u bloom_used=%u bloom_effective=%u bloom_reason=%s bloom_off_filter_runs=1",
        ctx.id,ctx.frame,phase,key,accepted,exposure.automatic?"auto":"fixed",double(exposure.ev),
        exposure.ready,exposure.frame_used,exposure.reason,ctx.comparison.bloom_requested,
        comparison_bloom_ready(ctx),bloom_effective,bloom_effective&&ctx.bloom_effective_on,comparison_bloom_reason(ctx));
}
// One emitter toggle: the MotionOutput state change is already logged with
// its family and gain; this adds the key identity to the comparison record
// and the notice line. state<0 is the refusal (option not requested, or gain
// 1 so no variant exists); nothing is created or released either way.
// refused is the refusal word: the short form for the second half of a key
// that reports two families, so one F6 press fits the notice's 36 columns.
void comparison_emitter(Device& ctx,const char* key,const char* label,int state,const char* refused="UNAVAILABLE") noexcept {
    // Appends, so two or three keys sampled in the same frame each keep their
    // label; the caller clears the line before the first of them.
    const std::size_t used=std::strlen(ctx.comparison_emitter_notice);
    std::snprintf(ctx.comparison_emitter_notice+used,sizeof ctx.comparison_emitter_notice-used,"%s%s %s",
        used?" ":"",label,state<0?refused:state?"ON":"OFF");
    comparison_log(ctx,"request",key,state>=0);
}
void comparison_begin_frame(Device& ctx) noexcept {
    // Ordinary launches pay no comparison input/foreground polling. A
    // requested-but-refused capability still accepts the UNAVAILABLE notice.
    // Ctrl+Shift+F11 (ambient occlusion on/off) polls with the same sampler
    // when --ambient-occlusion is on; it has no notice and no report.
    const bool hdr_compare=hdr_requested && hdr_config.tonemap==renderer::HdrTonemap::Agx;
    // Emitter A/B keys (comparison-hotkeys.md): Ctrl+Shift+F4 the hull
    // light-map gain, F5 the additive bullets, F6 the emission source gain
    // together with the guide lights that follow it (F7 is
    // the telemetry marker and F8 the capture key). Any requested emitter
    // option opens the sampler; the individual keys are polled
    // unconditionally inside it so an unrequested option answers with a
    // logged refusal.
    const bool emitter_compare=screen_emission_additive_requested || emission_source_gain!=1.f || hull_emission_gain!=1.f || hull_lightmap_gain!=1.f;
    if(!hdr_compare && !ambient_occlusion_requested && !volumetric_fog_requested && !emitter_compare && !sun_shadow_apply_requested && !fps_overlay_requested)return;
    ComparisonKeys keys{};
    keys.foreground=comparison_foreground();
    keys.control=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;
    keys.shift=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0;
    keys.exposure=hdr_compare && (GetAsyncKeyState(VK_F9)&0x8000)!=0;
    keys.bloom=hdr_compare && (GetAsyncKeyState(VK_F10)&0x8000)!=0;
    keys.ambient_occlusion=ambient_occlusion_requested && (GetAsyncKeyState(VK_F11)&0x8000)!=0;
    // The emitter keys are polled with any emitter option on (an unrequested
    // one of the three still answers with a logged refusal); a launch with
    // only --fps-overlay polls its own chord and nothing else.
    keys.screen_additive=emitter_compare && (GetAsyncKeyState(VK_F5)&0x8000)!=0;
    keys.source_gain=emitter_compare && (GetAsyncKeyState(VK_F6)&0x8000)!=0;
    keys.hull_gain=emitter_compare && (GetAsyncKeyState(VK_F4)&0x8000)!=0;
    // Ctrl+Shift+F12: the sun shadows at rest (comparison-hotkeys.md, "Sun
    // shadows at rest"). Polled only with the apply requested, so a launch
    // without it never queries the key; no notice and no report, one
    // sun_shadow_toggle line per accepted press.
    keys.sun_shadow=sun_shadow_apply_requested && (GetAsyncKeyState(VK_F12)&0x8000)!=0;
    // Ctrl+Alt+F9 / F10 with Shift up: the volumetric fog on/off and its strength ladder, polled only with
    // --volumetric-fog (raw F9/F10 latches of their own; Ctrl+Shift+F9/F10 stay exposure and bloom).
    keys.fog_toggle=volumetric_fog_requested && (GetAsyncKeyState(VK_F9)&0x8000)!=0;
    keys.fog_step=volumetric_fog_requested && (GetAsyncKeyState(VK_F10)&0x8000)!=0;
    // Ctrl+Alt+F11 with Shift up: the stored fog's look preset L0..L3, polled only with --volumetric-fog-range stored.
    keys.fog_look=volumetric_fog_range_stored && (GetAsyncKeyState(VK_F11)&0x8000)!=0;
    // Ctrl+Alt+F7 with Shift up: the FPS overlay (comparison-hotkeys.md, "FPS
    // overlay"), polled only with --fps-overlay. The telemetry phase marker
    // is Ctrl+Shift+F7 (telemetry.cpp requires Shift), so the chords are
    // disjoint; the sampler applies the Alt/Shift rule and the F7 edge.
    keys.alt=(fps_overlay_requested || volumetric_fog_requested) && (GetAsyncKeyState(VK_MENU)&0x8000)!=0;
    keys.fps_overlay=fps_overlay_requested && (GetAsyncKeyState(VK_F7)&0x8000)!=0;
    const auto action=ctx.comparison.sample(keys);
    if(action.ambient_occlusion)ctx.motion_output.ambient_occlusion_toggle();
    if(action.sun_shadow)ctx.motion_output.sun_shadow_toggle();
    if(action.fog_toggle)ctx.motion_output.volumetric_fog_toggle();
    if(action.fog_step)ctx.motion_output.volumetric_fog_step();
    if(action.fog_look)ctx.motion_output.volumetric_fog_look_step();
    ctx.motion_output.volumetric_fog_begin_frame();
    if(action.fps_overlay)log("fps_overlay_toggle device=%llu frame=%llu visible=%u reason=key",ctx.id,ctx.frame,unsigned(ctx.fps_overlay.toggle()));
    const bool emitter=action.screen_additive||action.source_gain||action.hull_gain;
    if(emitter)ctx.comparison_emitter_notice[0]='\0';
    if(action.hull_gain)comparison_emitter(ctx,"ctrl_shift_f4","LIGHTMAP",ctx.motion_output.hull_emission_gain_toggle(true));
    if(action.screen_additive)comparison_emitter(ctx,"ctrl_shift_f5","BULLETS",ctx.motion_output.screen_emission_additive_toggle());
    // F6 is the effects group: the twenty engine/effects pairs and the ONE/ONE
    // guide lights, which take the same gain (comparison-hotkeys.md).
    if(action.source_gain){comparison_emitter(ctx,"ctrl_shift_f6","EMISSION",ctx.motion_output.emission_source_gain_toggle());
        comparison_emitter(ctx,"ctrl_shift_f6","GUIDE",ctx.motion_output.hull_emission_gain_toggle(false),"N/A");}
    if(emitter){ctx.comparison_notice.show(GetTickCount64());ctx.comparison_report_pending=true;}
    if(!action.exposure && !action.bloom)return;
    ctx.comparison_emitter_notice[0]='\0'; // an exposure/bloom press owns the second line again
    const bool boundary=!ctx.reset_active && !ctx.compositor && !ctx.bloom_busy
        && ctx.motion_output.comparison_boundary_available();
    if(action.exposure){
        const bool accepted=boundary && ctx.motion_output.comparison_toggle_exposure();
        comparison_log(ctx,"request","ctrl_shift_f9",accepted);
    }
    if(action.bloom){
        const bool accepted=boundary && comparison_bloom_ready(ctx);
        if(accepted)ctx.comparison.bloom_requested=!ctx.comparison.bloom_requested;
        comparison_log(ctx,"request","ctrl_shift_f10",accepted);
    }
    ctx.comparison_notice.show(GetTickCount64());ctx.comparison_report_pending=true;
}
void comparison_notice_text(Device& ctx) noexcept {
    const auto exposure=ctx.motion_output.comparison_exposure();
    char first[48]{},second[48]{};
    if(!exposure.ready) {
        if(!std::strcmp(exposure.reason,"auto_not_prepared"))
            std::snprintf(first,sizeof first,"FIXED EV %+.2f / NO AUTO",double(exposure.ev));
        else std::snprintf(first,sizeof first,"EXPOSURE UNAVAILABLE");
    } else if(exposure.automatic)
        std::snprintf(first,sizeof first,"EXPOSURE AUTO%s",exposure.frame_used?"":" / WAITING");
    else std::snprintf(first,sizeof first,"FIXED EV %+.2f%s",double(exposure.ev),exposure.frame_used?"":" / WAITING");
    if(ctx.comparison_emitter_notice[0])std::snprintf(second,sizeof second,"%s",ctx.comparison_emitter_notice);
    else if(!comparison_bloom_ready(ctx))std::snprintf(second,sizeof second,"BLOOM %s",
        !std::strcmp(comparison_bloom_reason(ctx),"waiting_scene")?"WAITING":"UNAVAILABLE");
    else std::snprintf(second,sizeof second,"BLOOM %s%s",ctx.comparison.bloom_requested?"ON":"OFF",
        ctx.bloom_effective_frame==ctx.frame && ctx.bloom_effective_on==ctx.comparison.bloom_requested?"":" REQUESTED");
    ctx.comparison_notice.text(first,second);
}
HRESULT WINAPI present(IDirect3DDevice9* d,const RECT* a,const RECT* b,HWND w,const RGNDATA* r) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    // Declare before the outer lock: final retirement can stop the profiler,
    // and must run after that lock is released. The holder keeps the CPU owner
    // alive through its normal hooked Release and any reentrant callbacks.
    struct NoticePin {
        IDirect3DDevice9* device=nullptr;
        std::shared_ptr<Device> owner;
        ~NoticePin(){if(device)device->Release();}
    } notice_pin;
    // Scene bucket: the proxy work of this hook is scene-end work (the pre-present
    // flush and the optional notice). Its scope closes after frame_timing::frame,
    // so that time is accounted to the following frame; the forwarded native
    // Present is subtracted and reported separately as present_us.
    HookGuard lock(frame_timing::Bucket::Scene);
    // The optional notice can reenter through documented device/surface APIs.
    // Pin CPU ownership for this entry; its native pin lasts through Present.
    auto owner=devices.at(d);
    auto& ctx=*owner;
    auto fn=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*)>(17);
    if(sector_background_requested || volumetric_fog_requested)sector_background_context(ctx); // menus/loading without BeginScene
    ctx.motion_output.before_present();
    if(ctx.comparison_report_pending){comparison_log(ctx,"frame","none",true);ctx.comparison_report_pending=false;}
    if(ctx.comparison_notice.visible(GetTickCount64()) && comparison_foreground()
            && !ctx.reset_active && !ctx.compositor && !ctx.bloom_busy
            && ctx.motion_output.comparison_boundary_available()){
        comparison_notice_text(ctx);
        ctx.get<ULONG (WINAPI*)(IDirect3DDevice9*)>(1)(d);notice_pin.device=d;notice_pin.owner=owner;
        BloomOperation internal(ctx);
        const auto notice=ctx.comparison_notice.draw(d,ctx.original,ctx.caps.NumSimultaneousRTs);
        if(FAILED(notice.restore))ctx.motion_output.comparison_state_failed(notice.restore);
        if(FAILED(notice.operation)||FAILED(notice.restore)){
            log("renderer_comparison_notice device=%llu frame=%llu operation=%08lx restore=%08lx drawn=%u",ctx.id,ctx.frame,notice.operation,notice.restore,notice.drawn);
            ctx.comparison_notice.hide();
        }
    }
    // The FPS overlay (comparison-hotkeys.md, "FPS overlay"): the same
    // admission and pin as the hotkey notice, its own bitmap one panel lower.
    // Off or hidden, this is the one branch the option costs per frame.
    if(ctx.fps_overlay.visible() && comparison_foreground()
            && !ctx.reset_active && !ctx.compositor && !ctx.bloom_busy
            && ctx.motion_output.comparison_boundary_available()){
        if(!notice_pin.device){ctx.get<ULONG (WINAPI*)(IDirect3DDevice9*)>(1)(d);notice_pin.device=d;notice_pin.owner=owner;}
        BloomOperation internal(ctx);
        const auto overlay=ctx.fps_notice.draw(d,ctx.original,ctx.caps.NumSimultaneousRTs);
        if(FAILED(overlay.restore))ctx.motion_output.comparison_state_failed(overlay.restore);
        // A failure keeps the mode on and retries next frame (a lost device
        // recovers by itself after Reset); one line per failure episode.
        if(ctx.fps_overlay.draw_outcome(FAILED(overlay.operation)||FAILED(overlay.restore)))
            log("renderer_fps_overlay device=%llu frame=%llu operation=%08lx restore=%08lx drawn=%u",ctx.id,ctx.frame,overlay.operation,overlay.restore,overlay.drawn);
    }
    const auto begin=telemetry::now();
    frame_phases::present_begin(); // X3M_FRAME_PHASES only: the present phase begins; same placement rule as frame_timing
    frame_timing::present_begin(); // ahead of before_original: pre-call instrumentation must not alter the native input state (cpu_state.h)
    cpu.before_original();
    const HRESULT hr=fn(d,a,b,w,r);cpu.after_original();
    frame_timing::present_end();
    frame_phases::present_end(); // X3M_FRAME_PHASES only: closes the frame at the native Present return
    ctx.motion_output.after_present(hr);
    const bool scene_confirmed=ctx.scene_depth.end_frame(hr);
    const bool motion_committed=ctx.motion.end_frame(scene_confirmed,hr);
    if(ctx.capture && motion_capture_requested)
        log("motion_frame device=%llu frame=%llu scene_confirmed=%u storage_history_committed=%u present=%08lx temporal_history_committed=0",
            ctx.id,ctx.frame,scene_confirmed,motion_committed,hr);
    const auto end=telemetry::now();
    game_phases::present_endpoint(reinterpret_cast<std::uintptr_t>(d),ctx.id,ctx.reset_generation,ctx.frame,ctx.capture,end,static_cast<std::uint32_t>(hr));
    game_phases::loading_phase_present(ctx.id,ctx.reset_generation,ctx.frame); // cadence-derived loading_phase lines, every mode
    voice_dmo_fallback::report(); // one atomic load per Present; lines only after an activation
    lod_scale::refresh(); // X3M_LOD_SCALE only: two bounded reads per Present, one store when the game value changed
    point_light_admission::present(ctx.id,ctx.frame,ctx.capture); // option on only: one point_light_admission_frame line, point_light_node samples on capture frames, memo serial bump
    cull_census::present(ctx.id,ctx.frame,ctx.capture); // X3M_CULL_CENSUS=1 only: the cull_census_frame row and the entry rows of a captured frame, then the ring is cleared
    collide_box_cull::present(ctx.id,ctx.frame,ctx.capture); // X3M_COLLIDE_BOX_CULL=1 only: reads and zeroes the four pair counters; one collide_census line per 300 frames, one collide_census_frame line per capture frame
    collide_narrow_census::present(ctx.id,ctx.frame,ctx.capture); // X3M_COLLIDE_NARROW_CENSUS=1 only: swaps the accepted-pair ring; one collide_narrow line per 300 frames, collide_narrow_pair rows on a capture frame
    sun_occlusion::present(ctx.frame+1); // X3M_SUN_OCCLUSION / _LOG only: the probe override's frame boundary
    collide_memo::present(ctx.id,ctx.frame,ctx.capture); // X3M_COLLIDE_MEMO=1 only: advances the memo's frame (entries expire after one frame untouched); one collide_memo line per 300 frames
    cull_small_parts::present(ctx.id,ctx.frame,ctx.capture); // X3M_CULL_SMALL_PARTS_PX only: the frame's threshold and culled count on a captured frame
    telemetry::present(ctx.stats,ctx.frame,ctx.capture,begin,end,hr);
    if(ctx.fps_overlay.visible()){
        // Shown only: one QueryPerformanceCounter per Present (the frame_end
        // clock), the text rebuilt when a 250 ms bucket closes. The second
        // line is the Ctrl+Shift+F12 state, only when that key is live.
        LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
        const bool refreshed=ctx.fps_overlay.frame(uint64_t(stamp.QuadPart),ctx.draws);
        const int shadows=!sun_shadow_apply_requested?-1:int(ctx.motion_output.sun_shadow_enabled());
        // With --volumetric-fog the second line also carries the fog state and the current
        // strength (Ctrl+Alt+F9 / F10); IDLE = the sector rule holds the medium at zero.
        const int fog=ctx.motion_output.volumetric_fog_overlay_state();
        const bool fog_changed=ctx.fps_overlay.fog(fog); // latched every shown frame
        if(ctx.fps_overlay.shadows(shadows)||fog_changed||refreshed){ // a state change rewrites the line the same frame
            const char* at_rest=shadows<0?"":shadows?"SHADOWS ON":"SHADOWS OFF";
            char second[40];
            if(fog<0)std::snprintf(second,sizeof second,"%s",at_rest);
            else if(!(fog&1))std::snprintf(second,sizeof second,"%s%sFOG OFF",at_rest,*at_rest?"  ":"");
            else {
                const int look=ctx.motion_output.volumetric_fog_look(); // -1 unless the stored range is on
                char preset[8]{};if(look>=0)std::snprintf(preset,sizeof preset," L%d",look);
                std::snprintf(second,sizeof second,"%s%sFOG %.2fx%s%s",at_rest,*at_rest?"  ":"",double(ctx.motion_output.volumetric_fog_strength() / .02f),preset,(fog&2)?"":" IDLE");
            }
            ctx.fps_notice.text(ctx.fps_overlay.line(),second);
        }
    }
    if(telemetry::enabled()&&(!ctx.stats.present_override_known||ctx.stats.present_override!=w)){
        log("telemetry_present_window device=%llu frame=%llu override=%p device_window=%p result=%08lx",ctx.id,ctx.frame,w,ctx.stats.window,hr);
        ctx.stats.present_override_known=true;ctx.stats.present_override=w;
    }
    // The first presented frame closes the trampoline install window: every
    // engine_patch claim belongs to initialize_log (before the device existed);
    // a later claim would write over code the loading threads may be executing.
    if(engine_patch::install_window_open())engine_patch::close_install_window("first_present");
    frame_timing::frame(ctx.frame,ctx.draws); // X3M_FRAME_TIMING only: per-frame sample, one line per 300-frame window
    frame_phases::frame(ctx.frame); // X3M_FRAME_PHASES only: takes the closed frame, one frame_phases line per 300-frame window
    media_cue::frame(ctx.frame); // X3M_MEDIA_CUE_* only: admits this thread, closes the frame's attempt count, drains the trace ring
    // Programs the game compiled that are in none of the proxy's tables
    // (docs/architecture/mod-compatibility.md, "Making unknown programs
    // visible"). One line per distinct unknown, at the first Present after it
    // was created; the population line follows the 300-frame cadence and only
    // when the counts moved.
    report_shader_population(ctx.frame%300==0);
    if (ctx.capture || ctx.frame%frame_end_stride==0) {
        // One QPC per logged line (every frame_end_stride frames or a capture frame), in every
        // mode: elapsed_ms since DllMain and dt_ms since the previous frame_end
        // line make load times readable from a plain --direct log. Integer only.
        static uint64_t frequency=0;
        if(!frequency){LARGE_INTEGER f{};QueryPerformanceFrequency(&f);frequency=f.QuadPart>0?uint64_t(f.QuadPart):1;}
        LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);const uint64_t now=uint64_t(stamp.QuadPart);
        const uint64_t elapsed_ms=(now-dll_load_qpc)*1000ull/frequency;
        const uint64_t dt_ms=ctx.frame_end_qpc?(now-ctx.frame_end_qpc)*1000ull/frequency:0;
        ctx.frame_end_qpc=now;
        log("frame_end device=%llu frame=%llu draws=%llu capture=%u present=%08lx elapsed_ms=%llu dt_ms=%llu qpc=%llu",ctx.id,ctx.frame,ctx.draws,ctx.capture,hr,elapsed_ms,dt_ms,now);
    }
    // The chase reports keep their own 300-frame cadence: --frame-end-stride
    // shortens the frame_end line only.
    if (ctx.capture || ctx.frame%300==0) {
        chase_camera::report(ctx.frame); // X3M_CAMERA=chase only (no line otherwise)
        chase_aim_trace::report(ctx.frame); // bounded cursor-fire diagnostics with telemetry
        chase_transition::report(ctx.frame); // bounded native view/lifetime observations
        chase_lead::report(ctx.frame); // predictive marker results, no per-draw reporting
    }
    if(ctx.capture||ctx.frame%300==0)finite_upload_metrics(d,ctx,"present");
    // With the route requested, log the wrapper's copy-depth epochs per capture
    // frame: source_epoch counts the application's depth clears that found the
    // original depth bound, so it witnesses that the route's own depth unbind
    // (the sentinel fill, restored inside the draw hook) never reached one.
    if(ctx.capture&&motion_output_requested)ownership_depth_info(d,ctx.id,ctx.frame,"present");
    if(ctx.capture||ctx.frame%300==0){
        if(auto* monitor=ownership::process_admission_monitor()){
            const auto state=ownership::admission_snapshot(monitor);
            log("admission_metric device=%llu frame=%llu phase=present active_roots=%llu waiting_roots=%llu admitted_roots=%llu promotions=%llu veto_bits=%lu first_reason=%u replay_active=%u live_replay_enabled=0 coverage_complete=0",
                ctx.id,ctx.frame,state.active_roots,state.waiting_roots,state.admitted_roots,state.promotions,
                static_cast<DWORD>(state.vetoes),unsigned(state.first_veto),state.replay_active);
        }
    }
    if (ctx.capture && ctx.remaining) --ctx.remaining;
    ++ctx.frame; ctx.draws=0; ctx.composition_scene_owner=false;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    ctx.fixture_emission_source_calls=0; ctx.fixture_primitive_source_calls=0;
#endif
    ctx.events=0; ctx.stats.frame=ctx.frame;
    const bool down=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
    // With X3M_CAPTURE_DELAY=0 the edge starts the burst as before; with a delay
    // it arms one, and a further F8 while it is pending neither re-arms nor
    // cancels. The capture_start schedule is unaffected either way.
    const auto arm=capture_arm::core::step(ctx.capture_pending,down&&!ctx.key_down,ctx.frame,capture_delay);
    if (arm==capture_arm::core::Action::arm)
        log("capture_armed device=%llu frame=%llu start_frame=%llu delay=%u",ctx.id,ctx.frame,ctx.capture_pending.start_frame,capture_delay);
    if (arm==capture_arm::core::Action::start || (capture_count && ctx.frame==capture_start)) ctx.remaining=capture_count ? capture_count : 1;
    ctx.key_down=down; ctx.capture=ctx.remaining>0;
    point_light_admission::begin_frame(ctx.capture); // option on only: enables the per-node sample for a capture frame
    cull_census::begin_frame(ctx.capture); // X3M_CULL_CENSUS=1 only: arms the two pass stubs for a capture frame
    cull_small_parts::begin_frame(); // X3M_CULL_SMALL_PARTS_PX only: this frame's threshold from the live projection scale and the back-buffer width
    ctx.scene_depth.begin_frame(d,ctx.id,ctx.frame,ctx.capture);
    ctx.motion_output.begin_frame(ctx.frame,ctx.capture);
    comparison_begin_frame(ctx);
    ctx.motion.begin_frame(d,ctx.frame,ctx.capture && motion_capture_requested && motion_live_replay_available &&
        object_trace::active() && object_lifetime::active());
    if (ctx.capture) log("frame_begin device=%llu frame=%llu",ctx.id,ctx.frame);
    if (logfile) { const auto begin=telemetry::now(); fflush(logfile); telemetry::record(ctx.stats,telemetry::Metric::LogFlush,telemetry::now()-begin); }
    return hr;
}
HRESULT reset_common(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p,D3DDISPLAYMODEEX* mode,bool extended) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    // Named for the hook the game called, not for this shared body.
    HookGuard lock(frame_timing::Bucket::State,extended?"reset_ex":"reset");
    auto& ctx=*devices.at(d);
    game_phases::invalidate_device(); // includes a refused reentrant attempt
    ctx.sector_background_evidence.invalidate(); // also on refused Reset; no retained engine memory
    ctx.object_evidence.invalidate(); // diagnostic association also ends on refused Reset
    // A Reset reentered from injected GPU work cannot destroy that work's
    // stack-local saved state. Ordinary Reset during original is supported.
    if(ctx.bloom_busy || ctx.motion_output.composition_operation_active())return D3DERR_INVALIDCALL;
    ++ctx.reset_generation; ctx.reset_active=true; ctx.scene_thread=0; ctx.composition_scene_owner=false;
    ctx.comparison_notice.hide();ctx.comparison.reset_focus();ctx.comparison_report_pending=false;ctx.comparison_emitter_notice[0]='\0';
    ctx.fps_overlay.reset();ctx.fps_notice.text("",""); // the window restarts after Reset; visibility is kept
    ctx.bloom_effective_frame=UINT64_MAX;
    revoke_compositor(ctx);
    ctx.capture=false; ctx.remaining=0;capture_arm::core::clear(ctx.capture_pending);ctx.stats.had_present=false;ctx.stats.last_frame_capture=false;++ctx.stats.resets;
    ctx.scene_depth.invalidate();
    ctx.motion.invalidate();
    {
        BloomOperation internal(ctx);
        ctx.bloom.before_reset(); ctx.bloom_attempted=false;
        ctx.motion_output.before_reset();
    }
    presentation_parameters("reset_before",ctx.id,ctx.stats.focus_window,p);
    log("reset_begin ptr=%p device=%llu",d,ctx.id);
    finite_upload_metrics(d,ctx,"reset_before");
    const auto begin=telemetry::now();
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if(bloom_lifetime_fixture.bound && bloom_lifetime_fixture.device==d){
        auto& counts=bloom_lifetime_fixture.counts; ++counts[6];
        if(auto* call=ctx.compositor){
            if(!call->input.scene && !call->input.boundary.main && !call->input.boundary.depth
                    && !call->candidate.surface)++counts[7];
            if(call->native_pin)++counts[10];
        }
    }
#endif
    cpu.before_original();
    const HRESULT hr=extended
        ? ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*,D3DDISPLAYMODEEX*)>(132)(d,p,mode)
        : ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*)>(16)(d,p);
    cpu.after_original(); ctx.reset_active=false;
    telemetry::record(ctx.stats,telemetry::Metric::Reset,telemetry::now()-begin,FAILED(hr));
    presentation_parameters("reset_after",ctx.id,ctx.stats.focus_window,p);
    ctx.motion_output.after_reset(hr);
    lod_scale::refresh(); // the multiplier may be rewritten if the device bring-up path re-runs
    point_light_admission::next_frame(); // a Reset also retires the frame's root verdicts
    cull_census::begin_frame(false); // a Reset disarms the census stubs and drops the partial frame
    sun_occlusion::device_reset(); // the 1x1 visibility targets went with the Reset: vanilla until a pass has run again
    collide_memo::device_reset(); // X3M_COLLIDE_MEMO=1 only: a Reset (device loss, mode change, the pause around it) drops the whole memo
    cull_small_parts::after_reset(p ? p->BackBufferWidth : 0u); // a Reset disarms the small-parts stub until the next frame's projection read; new back-buffer width
    ownership_depth_info(d,ctx.id,ctx.frame,"reset_after");
    finite_upload_metrics(d,ctx,"reset_after");
    if(SUCCEEDED(hr)&&p&&p->hDeviceWindow)ctx.stats.window=p->hDeviceWindow;
    telemetry::summary(ctx.stats,"reset",ctx.frame);
    log("reset_end device=%llu result=%08lx",ctx.id,hr); return hr;
}
HRESULT WINAPI reset(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p) {
    return reset_common(d,p,nullptr,false);
}
HRESULT WINAPI reset_ex(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p,D3DDISPLAYMODEEX* mode) {
    return reset_common(d,p,mode,true);
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
void fixture_observe_wrap(Device& ctx, IDirect3DDevice9* device) {
    if (!ctx.fixture_observe_native_wrap) return;
    auto& result = ctx.fixture_wrap;
    ++result.sequence; result.valid = 0; result.result = S_OK;
    using GetState = HRESULT (WINAPI*)(IDirect3DDevice9*,D3DRENDERSTATETYPE,DWORD*);
    // Slot58 is the saved native GetRenderState entry, not the application
    // getter hook: observation cannot flush a lazy MRT/WRAP transaction.
    for (unsigned index = 0; index < 16; ++index) {
        result.values[index] = 0;
        const auto state = D3DRENDERSTATETYPE(index < 8 ? D3DRS_WRAP0+index : D3DRS_WRAP8+index-8);
        const HRESULT hr = ctx.get<GetState>(58)(device,state,&result.values[index]);
        if (FAILED(hr) && SUCCEEDED(result.result)) result.result = hr;
    }
    result.valid = SUCCEEDED(result.result);
}
#endif
HRESULT WINAPI draw_primitive(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT s,UINT c) {
    LightCallBoundary cpu; // draw path audited x87-free: check_no_x87.py roots here (state-call-fast-path.md, Envelope)
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    PlainHookGuard lock(frame_timing::Bucket::Draw);
    auto& ctx=*devices.at(d);CallTimer timer(ctx,telemetry::enabled(telemetry::Metric::DrawBackend));
    if(ctx.motion_output.draw_submission_blocked())return ctx.motion_output.before_draw({false,false,t,c,s,0,0,0}).submission_error;
    if(ctx.capture)ctx.motion_output.restore_bindings(); // Capture diagnostics below read the application's bindings.
    auto input=read_draw_input(ctx,d,{DrawMethod::Primitive,t,c,s});
    ctx.scene_depth.before_draw(d,t,c);
    snapshot(d,"primitive",t,c,false,0,s); // s is StartVertex: the range field of a non-indexed draw
    if (devices.at(d)->capture) log("draw_args start_vertex=%u",s);
    // Step C (screen-emission-region.md): the bullet screen draws are
    // non-indexed; they get the same scene/thread permission and draw scope
    // as the indexed DIP so the packed bracket can admit them.
    const bool composition_permission=ctx.composition_scene_owner && ctx.composition_scene_frame==ctx.frame && ctx.scene_thread==GetCurrentThreadId()
        && !ctx.reset_active && !ctx.compositor && !ctx.bloom_busy && !ctx.composition_draw_depth
        && !ctx.motion_output.reference_accounting_busy();
    struct CompositionDrawScope {
        unsigned& depth; bool enabled;
        CompositionDrawScope(unsigned& d,bool e):depth(d),enabled(e){if(enabled)++depth;}
        ~CompositionDrawScope(){if(enabled)--depth;}
    } composition_scope(ctx.composition_draw_depth,ctx.motion_output.composition_requested());
    const MotionDrawCall draw_call{false,false,t,c,s,0,0,0,composition_permission};
    auto route=ctx.motion_output.before_draw(draw_call);
    if(sun_occlusion::bracket_open())ctx.motion_output.prepare_lens(draw_call,route); // X3M_SUN_OCCLUSION only, lens-scene draws only: one flag test otherwise
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if(route.submit)++ctx.fixture_primitive_source_calls;
#endif
    frame_timing::draw_native_begin(); // ahead of before_original, like present_begin (cpu_state.h)
    timer.begin();
    cpu.before_original();
    const HRESULT result=route.submit?devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT)>(81)(d,t,s,c):route.submission_error;cpu.after_original();
    frame_timing::draw_native_end();
    timer.end();telemetry::record(ctx.stats,telemetry::Metric::DrawBackend,timer.backend_ticks,FAILED(result));
    ctx.motion_output.after_draw(route,result);
    ctx.scene_depth.after_draw(result);
    record_draw_input(ctx,input,result);
    if (devices.at(d)->capture) log("draw_result device=%llu frame=%llu index=%llu result=%08lx",devices.at(d)->id,devices.at(d)->frame,devices.at(d)->draws,result);
    return result;
}
HRESULT WINAPI draw_indexed(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,INT b,UINT m,UINT n,UINT s,UINT c) {
    LightCallBoundary cpu; // draw path audited x87-free: check_no_x87.py roots here (state-call-fast-path.md, Envelope)
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    PlainHookGuard lock(frame_timing::Bucket::Draw);
    auto& ctx=*devices.at(d);CallTimer timer(ctx,telemetry::enabled(telemetry::Metric::DrawBackend));
    if(ctx.motion_output.draw_submission_blocked())return ctx.motion_output.before_draw({true,false,t,c,s,b,m,n}).submission_error;
    if(ctx.capture)ctx.motion_output.restore_bindings(); // Capture diagnostics below read the application's bindings.
    auto input=read_draw_input(ctx,d,{DrawMethod::Indexed,t,c,s,b,m,n});
    ctx.scene_depth.before_draw(d,t,c);
    snapshot(d,"indexed",t,c,false,b,s);
    if (devices.at(d)->capture) log("draw_args base_vertex=%d min_vertex=%u num_vertices=%u start_index=%u",b,m,n,s);
    const bool composition_permission=ctx.composition_scene_owner && ctx.composition_scene_frame==ctx.frame && ctx.scene_thread==GetCurrentThreadId()
        && !ctx.reset_active && !ctx.compositor && !ctx.bloom_busy && !ctx.composition_draw_depth
        && !ctx.motion_output.reference_accounting_busy();
    struct CompositionDrawScope {
        unsigned& depth; bool enabled;
        CompositionDrawScope(unsigned& d,bool e):depth(d),enabled(e){if(enabled)++depth;}
        ~CompositionDrawScope(){if(enabled)--depth;}
    } composition_scope(ctx.composition_draw_depth,ctx.motion_output.composition_requested());
    const MotionDrawCall draw_call{true,false,t,c,s,b,m,n,composition_permission};
    auto route=ctx.motion_output.before_draw(draw_call);
    if(sun_occlusion::bracket_open())ctx.motion_output.prepare_lens(draw_call,route); // X3M_SUN_OCCLUSION only, lens-scene draws only: one flag test otherwise
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if(route.submit){++ctx.fixture_emission_source_calls;fixture_observe_wrap(ctx,d);}
#endif
    frame_timing::draw_native_begin(); // ahead of before_original, like present_begin (cpu_state.h)
    timer.begin();
    cpu.before_original();
    const HRESULT result=route.submit?devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT)>(82)(d,t,b,m,n,s,c):route.submission_error;cpu.after_original();
    frame_timing::draw_native_end();
    timer.end();telemetry::record(ctx.stats,telemetry::Metric::DrawBackend,timer.backend_ticks,FAILED(result));
    ctx.motion_output.after_draw(route,result);
    ctx.scene_depth.after_draw(result);
    record_draw_input(ctx,input,result);
    if (devices.at(d)->capture) log("draw_result device=%llu frame=%llu index=%llu result=%08lx",devices.at(d)->id,devices.at(d)->frame,devices.at(d)->draws,result);
    return result;
}
HRESULT WINAPI draw_up(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT c,const void* data,UINT stride) {
    LightCallBoundary cpu; // draw path audited x87-free: check_no_x87.py roots here (state-call-fast-path.md, Envelope)
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    PlainHookGuard lock(frame_timing::Bucket::Draw);
    auto& ctx=*devices.at(d);CallTimer timer(ctx,telemetry::enabled(telemetry::Metric::DrawBackend));
    if(ctx.motion_output.draw_submission_blocked())return ctx.motion_output.before_draw({false,true,t,c,0,0,0,0}).submission_error;
    if(ctx.capture)ctx.motion_output.restore_bindings(); // Capture diagnostics below read the application's bindings.
    auto input=read_draw_input(ctx,d,{DrawMethod::UserMemory,t,c});
    ctx.scene_depth.before_draw(d,t,c);
    snapshot(d,"up",t,c,true);
    if (devices.at(d)->capture) log("draw_args vertex_ptr=%p stride=%u",data,stride);
    const MotionDrawCall draw_call{false,true,t,c,0,0,0,0};
    auto route=ctx.motion_output.before_draw(draw_call);
    if(sun_occlusion::bracket_open())ctx.motion_output.prepare_lens(draw_call,route); // X3M_SUN_OCCLUSION only, lens-scene draws only: one flag test otherwise
    frame_timing::draw_native_begin(); // ahead of before_original, like present_begin (cpu_state.h)
    timer.begin();
    cpu.before_original();
    const HRESULT result=route.submit?devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT)>(83)(d,t,c,data,stride):route.submission_error;cpu.after_original();
    frame_timing::draw_native_end();
    timer.end();telemetry::record(ctx.stats,telemetry::Metric::DrawBackend,timer.backend_ticks,FAILED(result));
    ctx.motion_output.after_draw(route,result);
    ctx.scene_depth.after_draw(result);
    record_draw_input(ctx,input,result);
    if (devices.at(d)->capture) log("draw_result device=%llu frame=%llu index=%llu result=%08lx",devices.at(d)->id,devices.at(d)->frame,devices.at(d)->draws,result);
    return result;
}
HRESULT WINAPI draw_indexed_up(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT m,UINT n,UINT c,const void* indices,D3DFORMAT f,const void* data,UINT stride) {
    LightCallBoundary cpu; // draw path audited x87-free: check_no_x87.py roots here (state-call-fast-path.md, Envelope)
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    PlainHookGuard lock(frame_timing::Bucket::Draw);
    auto& ctx=*devices.at(d);CallTimer timer(ctx,telemetry::enabled(telemetry::Metric::DrawBackend));
    if(ctx.motion_output.draw_submission_blocked())return ctx.motion_output.before_draw({true,true,t,c,0,0,m,n}).submission_error;
    if(ctx.capture)ctx.motion_output.restore_bindings(); // Capture diagnostics below read the application's bindings.
    auto input=read_draw_input(ctx,d,{DrawMethod::IndexedUserMemory,t,c,0,0,m,n});
    ctx.scene_depth.before_draw(d,t,c);
    snapshot(d,"indexed_up",t,c,true);
    if (devices.at(d)->capture) log("draw_args min_vertex=%u num_vertices=%u vertex_ptr=%p stride=%u index_ptr=%p index_format=%u",m,n,data,stride,indices,f);
    const MotionDrawCall draw_call{true,true,t,c,0,0,m,n};
    auto route=ctx.motion_output.before_draw(draw_call);
    if(sun_occlusion::bracket_open())ctx.motion_output.prepare_lens(draw_call,route); // X3M_SUN_OCCLUSION only, lens-scene draws only: one flag test otherwise
    frame_timing::draw_native_begin(); // ahead of before_original, like present_begin (cpu_state.h)
    timer.begin();
    cpu.before_original();
    const HRESULT result=route.submit?devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT)>(84)(d,t,m,n,c,indices,f,data,stride):route.submission_error;cpu.after_original();
    frame_timing::draw_native_end();
    timer.end();telemetry::record(ctx.stats,telemetry::Metric::DrawBackend,timer.backend_ticks,FAILED(result));
    ctx.motion_output.after_draw(route,result);
    ctx.scene_depth.after_draw(result);
    record_draw_input(ctx,input,result);
    if (devices.at(d)->capture) log("draw_result device=%llu frame=%llu index=%llu result=%08lx",devices.at(d)->id,devices.at(d)->frame,devices.at(d)->draws,result);
    return result;
}
HRESULT WINAPI clear(IDirect3DDevice9* d,DWORD n,const D3DRECT* r,DWORD f,D3DCOLOR c,float z,DWORD s) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);CallTimer timer(ctx);
    const auto motion_boundary=ctx.scene_depth.before_clear(d,n,r,f,z);
    if(motion_boundary.valid && motion_capture_requested && motion_live_replay_available){
        const auto begin=telemetry::now();
        const auto motion=ctx.motion.before_clear(d,motion_boundary);
        log("motion_replay device=%llu frame=%llu event=%llu attempted=%u candidate_produced=%u observations=%llu eligible=%llu matched=%llu rejected=%llu completed=%llu operation=%08lx restoration=%08lx cpu_ticks=%llu execution_known=%u execution_reason=%u active_queries=%llu continuity_known=%u color_coverage_known=%u temporal_consumed=0",
            ctx.id,ctx.frame,motion_boundary.sequence,motion.attempted,motion.produced,motion.observations,
            motion.eligible,motion.matched,motion.rejected,motion.completed,motion.operation,motion.restoration,
            telemetry::now()-begin,motion.execution.known,unsigned(motion.execution.reason),motion.execution.active_queries,
            motion.continuity_known,motion.color_coverage_known);
    }
    ctx.motion_output.before_clear(n,f,z);
    timer.begin();
    cpu.before_original();
    const HRESULT result=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD)>(43)(d,n,r,f,c,z,s);
    cpu.after_original();
    timer.end();
    ctx.motion_output.after_clear(result);
    const bool motion_confirmed=ctx.scene_depth.after_clear(d,result);
    if(motion_boundary.valid)ctx.motion.after_clear(motion_confirmed);
    if(ctx.capture){
        capture_event(ctx,"clear",result);
        log("clear flags=%lu color=%08lx z=%g stencil=%lu rect_count=%lu rect_ptr=%p",f,c,z,s,n,r);
        if(SUCCEEDED(result)&&r&&n){
            const DWORD count=n<16?n:16;
            log("clear_rects recorded=%lu omitted=%lu",count,n-count);
            for(DWORD i=0;i<count;++i)log("clear_rect index=%lu left=%ld top=%ld right=%ld bottom=%ld",i,r[i].x1,r[i].y1,r[i].x2,r[i].y2);
        }
        // A depth-only Clear can divide scene and HUD without a target rebind.
        IDirect3DSurface9* surface=nullptr;
        if(SUCCEEDED(d->GetRenderTarget(0,&surface))&&surface){surface_info("clear_rt0",surface);surface->Release();surface=nullptr;}
        if(SUCCEEDED(d->GetDepthStencilSurface(&surface))&&surface){surface_info("clear_depth",surface);surface->Release();}
        D3DVIEWPORT9 vp{};
        const HRESULT vp_result=d->GetViewport(&vp);
        log("clear_viewport result=%08lx x=%lu y=%lu w=%lu h=%lu minz=%.9g maxz=%.9g",vp_result,vp.X,vp.Y,vp.Width,vp.Height,vp.MinZ,vp.MaxZ);
    }
    return result;
}
HRESULT WINAPI set_rt(IDirect3DDevice9* d,DWORD index,IDirect3DSurface9* rt) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);CallTimer timer(ctx);
    ctx.motion_output.restore_bindings();
    // HDR redirect: the application's main surface maps to the FP16 target
    // while the scene is redirected; the shadow below records the logical binding.
    IDirect3DSurface9* physical=ctx.motion_output.before_set_render_target(index,rt);
    timer.begin();
    cpu.before_original();
    const HRESULT result=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9*)>(37)(d,index,physical);cpu.after_original();timer.end();
    ctx.scene_depth.after_set_rt(d,index,result);
    ctx.motion_output.after_set_render_target(index,rt,result);
    if(ctx.capture){capture_event(ctx,"set_rt",result);log("set_rt index=%lu result=%08lx ptr=%p",index,result,rt);if(SUCCEEDED(result))surface_info("binding",rt);}
    return result;
}
HRESULT WINAPI set_depth(IDirect3DDevice9* d,IDirect3DSurface9* depth) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);CallTimer timer(ctx);
    // D3D9 validates the depth surface against every bound target, so a held
    // RT1/RT2 (lazy RT mode) goes back first: the call sees the application's bindings.
    ctx.motion_output.restore_bindings();
    timer.begin();
    cpu.before_original();
    const HRESULT result=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*)>(39)(d,depth);cpu.after_original();timer.end();
    ctx.scene_depth.after_set_depth(d,result);
    ctx.motion_output.after_set_depth(depth,result);
    if(ctx.capture){capture_event(ctx,"set_depth",result);log("set_depth ptr=%p result=%08lx",depth,result);if(SUCCEEDED(result))surface_info("depth_binding",depth);}
    return result;
}
HRESULT WINAPI stretch_rect(IDirect3DDevice9* d,IDirect3DSurface9* source,const RECT* source_rect,IDirect3DSurface9* dest,const RECT* dest_rect,D3DTEXTUREFILTERTYPE filter){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);CallTimer timer(ctx);
    // The temporal resolve runs here, before the application's bloom copy of
    // the main target, so the copy and everything after it see the resolved image.
    ctx.motion_output.before_stretch(source,source_rect,dest,dest_rect);
    timer.begin();
    cpu.before_original();
    const HRESULT result=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,D3DTEXTUREFILTERTYPE)>(34)(d,source,source_rect,dest,dest_rect,filter);cpu.after_original();timer.end();
    // The application's own copy (the resolve above is excluded: it runs before timer.begin).
    telemetry::record(ctx.stats,telemetry::Metric::StretchBackend,timer.backend_ticks,FAILED(result));
    ctx.scene_depth.after_stretch(d,source,source_rect,dest,dest_rect,result);
    ctx.motion_output.after_stretch(source,source_rect,dest,dest_rect,result);
    if(ctx.capture){
        capture_event(ctx,"stretch_rect",result);log("stretch_rect source=%p dest=%p filter=%u source_rect_null=%u dest_rect_null=%u",source,dest,filter,source_rect==nullptr,dest_rect==nullptr);
        if(SUCCEEDED(result)){
            if(source_rect)log("stretch_source_rect left=%ld top=%ld right=%ld bottom=%ld",source_rect->left,source_rect->top,source_rect->right,source_rect->bottom);
            if(dest_rect)log("stretch_dest_rect left=%ld top=%ld right=%ld bottom=%ld",dest_rect->left,dest_rect->top,dest_rect->right,dest_rect->bottom);
            surface_info("stretch_source",source);surface_info("stretch_dest",dest);
        }
    }
    return result;
}
HRESULT WINAPI begin_scene(IDirect3DDevice9* d){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*)>(41)(d);cpu.after_original();
    if(SUCCEEDED(hr)&&(sector_background_requested || volumetric_fog_requested))sector_background_context(ctx,true);
    ctx.motion_output.after_begin_scene(hr);
    lod_scale::refresh(); // X3M_LOD_SCALE only: catches the bring-up write before the first frame's LOD pass
    if(SUCCEEDED(hr)) {
        ctx.scene_thread=GetCurrentThreadId(); ctx.composition_scene_owner=false; ctx.composition_scene_frame=ctx.frame;
        if(ctx.motion_output.composition_requested() && scene_hook::active()) {
            compositor_owner::Snapshot identity{};
            ctx.composition_scene_owner=compositor_owner::read(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),identity)==compositor_owner::Result::Ok
                && reinterpret_cast<IDirect3DDevice9*>(identity.device)==d;
        }
#ifdef X3M_MOTION_OUTPUT_FIXTURE
        if(ctx.motion_output.fixture_emission_owner())ctx.composition_scene_owner=true;
#endif
    }
    return hr;
}
HRESULT WINAPI end_scene(IDirect3DDevice9* d){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock(frame_timing::Bucket::Scene);auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    ctx.motion_output.before_end_scene(); // HDR: flush the FP16 content while draws are legal
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*)>(42)(d);cpu.after_original();
    ctx.motion_output.after_end_scene(hr);
    ctx.composition_scene_owner=false;
    return hr;
}
ULONG WINAPI query_release(IDirect3DQuery9* query){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    auto& hooks=*queries.at(query);
    auto fn=hooks.get<ULONG(WINAPI*)(IDirect3DQuery9*)>(2);
    cpu.before_original();
    const ULONG refs=fn(query);cpu.after_original();
    if(!refs){
        // An open query released without END no longer changes with draws.
        const auto device=devices.find(hooks.device);
        if(hooks.active&&device!=devices.end())device->second->motion_output.query_active(false);
        queries.erase(query);
    }
    return refs;
}
HRESULT WINAPI query_issue(IDirect3DQuery9* query,DWORD flags){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    auto& hooks=*queries.at(query);
    const auto device=devices.find(hooks.device);
    // A query bracket must cover the application's bindings only.
    if(device!=devices.end())device->second->motion_output.restore_bindings();
    cpu.before_original();
    const HRESULT hr=hooks.get<HRESULT(WINAPI*)(IDirect3DQuery9*,DWORD)>(6)(query,flags);cpu.after_original();
    if(SUCCEEDED(hr)&&device!=devices.end()){
        const bool begin=(flags&D3DISSUE_BEGIN)!=0, end=(flags&D3DISSUE_END)!=0;
        if(begin&&!end&&!hooks.active){hooks.active=true;device->second->motion_output.query_active(true);}
        else if(end&&hooks.active){hooks.active=false;device->second->motion_output.query_active(false);}
    }
    return hr;
}
HRESULT WINAPI create_query(IDirect3DDevice9* d,D3DQUERYTYPE type,IDirect3DQuery9** out){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,D3DQUERYTYPE,IDirect3DQuery9**)>(118)(d,type,out);cpu.after_original();
    if(ctx.capture)log("create_query type=%u result=%08lx ptr=%p",unsigned(type),hr,out?*out:nullptr);
    if(SUCCEEDED(hr)&&out&&*out&&!queries.count(*out)){
        auto hooks=std::make_unique<QueryHooks>(*out,d);
        hooks->set(2,query_release);hooks->set(6,query_issue);
        auto entry=queries.emplace(*out,std::move(hooks));
        entry.first->second->install(*out);
    }
    return hr;
}
// Optional scene capture must not mistake omitted GPU writes for a contiguous
// known render sequence. Forward these calls unchanged and reject the candidate.
HRESULT WINAPI update_surface(IDirect3DDevice9* d,IDirect3DSurface9* source,const RECT* rect,IDirect3DSurface9* dest,const POINT* point){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    ctx.motion_output.before_render_target_write(dest); // HDR: a write into the main target ends the redirect first
    cpu.before_original();
    const auto hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const POINT*)>(30)(d,source,rect,dest,point);cpu.after_original();
    ctx.scene_depth.unsupported("UpdateSurface",hr);ctx.motion_output.unsupported(hr);return hr;
}
HRESULT WINAPI update_texture(IDirect3DDevice9* d,IDirect3DBaseTexture9* source,IDirect3DBaseTexture9* dest){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    ctx.motion_output.before_texture_write(dest);
    cpu.before_original();
    const auto hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DBaseTexture9*,IDirect3DBaseTexture9*)>(31)(d,source,dest);cpu.after_original();
    ctx.scene_depth.unsupported("UpdateTexture",hr);ctx.motion_output.unsupported(hr);return hr;
}
HRESULT WINAPI color_fill(IDirect3DDevice9* d,IDirect3DSurface9* surface,const RECT* rect,D3DCOLOR color){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    ctx.motion_output.before_render_target_write(surface); // HDR: a fill of the main target ends the redirect first
    cpu.before_original();
    const auto hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,D3DCOLOR)>(35)(d,surface,rect,color);cpu.after_original();
    ctx.scene_depth.after_color_fill(d,surface,rect,hr);
    ctx.motion_output.after_color_fill(surface,rect,hr);
    if(ctx.capture){
        capture_event(ctx,"color_fill",hr);
        log("color_fill target=%p rect=%p result=%08lx color=%08lx rect_null=%u",surface,rect,hr,color,rect==nullptr);
        // A failed native call may reject before reading either pointer. Logging
        // must not make a previously untouched invalid argument observable.
        if(SUCCEEDED(hr)){
            if(rect)log("color_fill_rect left=%ld top=%ld right=%ld bottom=%ld",rect->left,rect->top,rect->right,rect->bottom);
            if(surface)surface_info("color_fill_target",surface);
        }
    }
    return hr;
}
HRESULT WINAPI draw_rect_patch(IDirect3DDevice9* d,UINT handle,const float* segments,const D3DRECTPATCH_INFO* info){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    cpu.before_original();
    const auto hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,const float*,const D3DRECTPATCH_INFO*)>(115)(d,handle,segments,info);cpu.after_original();
    ctx.scene_depth.unsupported("DrawRectPatch",hr);ctx.motion_output.unsupported(hr);return hr;
}
HRESULT WINAPI draw_tri_patch(IDirect3DDevice9* d,UINT handle,const float* segments,const D3DTRIPATCH_INFO* info){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    cpu.before_original();
    const auto hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,const float*,const D3DTRIPATCH_INFO*)>(116)(d,handle,segments,info);cpu.after_original();
    ctx.scene_depth.unsupported("DrawTriPatch",hr);ctx.motion_output.unsupported(hr);return hr;
}
// These resource hooks are installed only when CPU telemetry is enabled. The
// backend receives every pointer/flag unchanged; returned objects are not wrapped.
HRESULT WINAPI create_texture(IDirect3DDevice9* d,UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    cpu.before_original();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**,HANDLE*)>(23)(d,w,h,levels,usage,format,pool,out,shared);cpu.after_original();
    telemetry::record(ctx.stats,telemetry::Metric::Texture,telemetry::now()-begin,FAILED(result));return result;
}
HRESULT WINAPI create_volume(IDirect3DDevice9* d,UINT w,UINT h,UINT depth,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DVolumeTexture9** out,HANDLE* shared){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    cpu.before_original();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DVolumeTexture9**,HANDLE*)>(24)(d,w,h,depth,levels,usage,format,pool,out,shared);cpu.after_original();
    telemetry::record(ctx.stats,telemetry::Metric::VolumeTexture,telemetry::now()-begin,FAILED(result));return result;
}
HRESULT WINAPI create_cube(IDirect3DDevice9* d,UINT edge,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DCubeTexture9** out,HANDLE* shared){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    cpu.before_original();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DCubeTexture9**,HANDLE*)>(25)(d,edge,levels,usage,format,pool,out,shared);cpu.after_original();
    telemetry::record(ctx.stats,telemetry::Metric::CubeTexture,telemetry::now()-begin,FAILED(result));return result;
}
HRESULT WINAPI create_vb(IDirect3DDevice9* d,UINT length,DWORD usage,DWORD fvf,D3DPOOL pool,IDirect3DVertexBuffer9** out,HANDLE* shared){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    cpu.before_original();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,DWORD,DWORD,D3DPOOL,IDirect3DVertexBuffer9**,HANDLE*)>(26)(d,length,usage,fvf,pool,out,shared);cpu.after_original();
    telemetry::record(ctx.stats,telemetry::Metric::VertexBuffer,telemetry::now()-begin,FAILED(result),SUCCEEDED(result)?length:0);return result;
}
HRESULT WINAPI create_ib(IDirect3DDevice9* d,UINT length,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DIndexBuffer9** out,HANDLE* shared){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    cpu.before_original();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DIndexBuffer9**,HANDLE*)>(27)(d,length,usage,format,pool,out,shared);cpu.after_original();
    telemetry::record(ctx.stats,telemetry::Metric::IndexBuffer,telemetry::now()-begin,FAILED(result),SUCCEEDED(result)?length:0);return result;
}
HRESULT WINAPI create_rt(IDirect3DDevice9* d,UINT w,UINT h,D3DFORMAT format,D3DMULTISAMPLE_TYPE ms, DWORD quality,BOOL lockable,IDirect3DSurface9** out,HANDLE* shared){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    cpu.before_original();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,D3DFORMAT,D3DMULTISAMPLE_TYPE,DWORD,BOOL,IDirect3DSurface9**,HANDLE*)>(28)(d,w,h,format,ms,quality,lockable,out,shared);cpu.after_original();
    telemetry::record(ctx.stats,telemetry::Metric::RenderTarget,telemetry::now()-begin,FAILED(result));return result;
}
HRESULT WINAPI create_depth(IDirect3DDevice9* d,UINT w,UINT h,D3DFORMAT format,D3DMULTISAMPLE_TYPE ms,DWORD quality,BOOL discard,IDirect3DSurface9** out,HANDLE* shared){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    cpu.before_original();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,D3DFORMAT,D3DMULTISAMPLE_TYPE,DWORD,BOOL,IDirect3DSurface9**,HANDLE*)>(29)(d,w,h,format,ms,quality,discard,out,shared);cpu.after_original();
    telemetry::record(ctx.stats,telemetry::Metric::DepthStencil,telemetry::now()-begin,FAILED(result));return result;
}
bool cursor_change_allowed(telemetry::State& stats){
    const auto stamp=telemetry::now();if(stats.last_cursor_change && stamp-stats.last_cursor_change<telemetry::frequency()/4){++stats.cursor_changes_suppressed;return false;}stats.last_cursor_change=stamp;return true;
}
HRESULT WINAPI cursor_properties(IDirect3DDevice9* d,UINT x,UINT y,IDirect3DSurface9* surface){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);auto& stats=ctx.stats;const auto begin=telemetry::now();
    cpu.before_original();
    const HRESULT result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,IDirect3DSurface9*)>(10)(d,x,y,surface);cpu.after_original();
    telemetry::record(stats,telemetry::Metric::CursorProperties,telemetry::now()-begin,FAILED(result));
    if((!stats.properties_known||stats.cursor_x!=x||stats.cursor_y!=y||stats.cursor_surface!=surface||FAILED(result))&&cursor_change_allowed(stats))log("telemetry_cursor_api device=%llu frame=%llu op=properties hotspot=%u,%u surface=%p result=%08lx",ctx.id,ctx.frame,x,y,surface,result);
    if(SUCCEEDED(result)){stats.properties_known=true;stats.cursor_x=x;stats.cursor_y=y;stats.cursor_surface=surface;}
    return result;
}
void WINAPI cursor_position(IDirect3DDevice9* d,int x,int y,DWORD flags){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);auto& stats=ctx.stats;const auto begin=telemetry::now();
    cpu.before_original();
    ctx.get<void(WINAPI*)(IDirect3DDevice9*,int,int,DWORD)>(11)(d,x,y,flags);cpu.after_original();
    telemetry::record(stats,telemetry::Metric::CursorPosition,telemetry::now()-begin);
    const auto stamp=telemetry::now();
    if((!stats.last_position||x!=stats.logged_position_x||y!=stats.logged_position_y||flags!=stats.logged_position_flags) && (!stats.last_position||stamp-stats.last_position>=telemetry::frequency()/4)){log("telemetry_cursor_api device=%llu frame=%llu op=position x=%d y=%d flags=%lu",ctx.id,ctx.frame,x,y,flags);stats.last_position=stamp;stats.logged_position_x=x;stats.logged_position_y=y;stats.logged_position_flags=flags;}
    else ++stats.position_suppressed;

}
BOOL WINAPI cursor_show(IDirect3DDevice9* d,BOOL show){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);auto& stats=ctx.stats;const auto begin=telemetry::now();
    cpu.before_original();
    const BOOL previous=ctx.get<BOOL(WINAPI*)(IDirect3DDevice9*,BOOL)>(12)(d,show);cpu.after_original();
    telemetry::record(stats,telemetry::Metric::CursorShow,telemetry::now()-begin);
    if((!stats.api_show_known||stats.api_show!=show)&&cursor_change_allowed(stats))log("telemetry_cursor_api device=%llu frame=%llu op=show requested=%d previous_visible=%d",ctx.id,ctx.frame,show,previous);
    stats.api_show_known=true;stats.api_show=show;return previous;
}
HRESULT WINAPI create_vs(IDirect3DDevice9* d,const DWORD* code,IDirect3DVertexShader9** out) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    const auto begin=telemetry::now();
    cpu.before_original();
    HRESULT hr=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,const DWORD*,IDirect3DVertexShader9**)>(91)(d,code,out);cpu.after_original();
    telemetry::record(devices.at(d)->stats,telemetry::Metric::ShaderVS,telemetry::now()-begin,FAILED(hr));
    if(SUCCEEDED(hr)&&out){
        const auto hash=shader_id(*out,"vs");
        UINT bytes=0;
        if(motion_output_requested&&*out&&SUCCEEDED((*out)->GetFunction(nullptr,&bytes)))
            devices.at(d)->motion_output.register_vertex_shader(*out,code,bytes,hash);
    }
    return hr;
}
HRESULT WINAPI create_ps(IDirect3DDevice9* d,const DWORD* code,IDirect3DPixelShader9** out) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    const auto begin=telemetry::now();
    cpu.before_original();
    HRESULT hr=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,const DWORD*,IDirect3DPixelShader9**)>(106)(d,code,out);cpu.after_original();
    telemetry::record(devices.at(d)->stats,telemetry::Metric::ShaderPS,telemetry::now()-begin,FAILED(hr));
    if(SUCCEEDED(hr)&&out){
        const auto hash=shader_id(*out,"ps");
        UINT bytes=0;
        if(motion_output_requested&&*out&&SUCCEEDED((*out)->GetFunction(nullptr,&bytes)))
            devices.at(d)->motion_output.register_pixel_shader(*out,code,bytes,hash);
    }
    return hr;
}
// Shadow-state hooks for the live motion route, installed only when
// X3M_MOTION_OUTPUT=1 and the device passed the route's capability gate. Each
// forwards unchanged and records the application's new binding after native
// success; the route restores exactly these values. Slot indices are verified
// against the SDK layout in abi_check.cpp.
//
// `boundary`/`guard` select the CPU-state contract. Every row is light:
// LightCallBoundary (MXCSR + last error) with a plain lock preserves the same
// application-observed state as CpuCallBoundary without four FNSAVE/FRSTOR per
// call, because verification/probe/check_no_x87.py proves the absence of x87
// opcodes on every function these hooks reach in the built DLL. The
// stream/indices/declaration/FVF shadows call D3D accessors after the native
// call (resource_id private data, GetDeclaration): D3D runtime entry points
// like the native slot itself, which vanilla calls with no envelope; their
// LastError/MXCSR effects are undone by the boundary's destructor. Their
// metadata-failure log() line is self-preserving (see log). The parameters
// stay so a row can be returned to the full boundary if its path changes.
//
// `admission_scope`, `lookup` and `native_spec` carry the dispatch trim (step 4
// of docs/architecture/state-call-fast-path.md) on the per-state-write setters:
// the inline null-admission scope, the cached device lookup, and `noexcept` on
// the forwarded slot, which is a COM/HRESULT entry point that propagates no C++
// exception in either the Windows runtime or Wine, so saying so removes the
// exception region the call otherwise carries. The binding shadows
// (stream/indices/declaration/FVF) are per-draw, not per-state-write, and keep
// the direct adapter, the map lookup and the plain slot type.
#define X3M_SHADOW_HOOK(boundary,guard,admission_scope,lookup,native_spec,name,slot,signature,call,update) \
HRESULT WINAPI name signature { \
    boundary cpu; \
    admission_scope admission; \
    guard lock;auto& ctx=lookup(d); \
    cpu.before_original(); \
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)signature native_spec>(slot)call;cpu.after_original(); \
    if(SUCCEEDED(hr))ctx.motion_output.update; \
    return hr; \
}
X3M_SHADOW_HOOK(LightCallBoundary,PlainHookGuard,LightAdmissionScope,hooked_device,noexcept,set_vs,92,(IDirect3DDevice9* d,IDirect3DVertexShader9* shader),(d,shader),set_vertex_shader(shader))
X3M_SHADOW_HOOK(LightCallBoundary,PlainHookGuard,LightAdmissionScope,hooked_device,noexcept,set_ps,107,(IDirect3DDevice9* d,IDirect3DPixelShader9* shader),(d,shader),set_pixel_shader(shader))
X3M_SHADOW_HOOK(LightCallBoundary,PlainHookGuard,LightAdmissionScope,hooked_device,noexcept,set_vs_constant_f,94,(IDirect3DDevice9* d,UINT start,const float* data,UINT count),(d,start,data,count),set_vertex_constants_f(start,data,count))
X3M_SHADOW_HOOK(LightCallBoundary,PlainHookGuard,LightAdmissionScope,hooked_device,noexcept,set_vs_constant_i,96,(IDirect3DDevice9* d,UINT start,const int* data,UINT count),(d,start,data,count),set_vertex_constants_i(start,data,count))
X3M_SHADOW_HOOK(LightCallBoundary,PlainHookGuard,LightAdmissionScope,hooked_device,noexcept,set_ps_constant_f,109,(IDirect3DDevice9* d,UINT start,const float* data,UINT count),(d,start,data,count),set_pixel_constants_f(start,data,count))
X3M_SHADOW_HOOK(LightCallBoundary,PlainHookGuard,DirectAdmissionScope,device_context,,set_stream_source,100,(IDirect3DDevice9* d,UINT stream,IDirect3DVertexBuffer9* buffer,UINT offset,UINT stride),(d,stream,buffer,offset,stride),set_stream_source(stream,buffer,offset,stride))
X3M_SHADOW_HOOK(LightCallBoundary,PlainHookGuard,DirectAdmissionScope,device_context,,set_indices,104,(IDirect3DDevice9* d,IDirect3DIndexBuffer9* buffer),(d,buffer),set_indices(buffer))
X3M_SHADOW_HOOK(LightCallBoundary,PlainHookGuard,LightAdmissionScope,hooked_device,noexcept,set_viewport,47,(IDirect3DDevice9* d,const D3DVIEWPORT9* viewport),(d,viewport),set_viewport(viewport))
X3M_SHADOW_HOOK(LightCallBoundary,PlainHookGuard,DirectAdmissionScope,device_context,,set_declaration,87,(IDirect3DDevice9* d,IDirect3DVertexDeclaration9* declaration),(d,declaration),set_vertex_declaration(declaration))
X3M_SHADOW_HOOK(LightCallBoundary,PlainHookGuard,DirectAdmissionScope,device_context,,set_fvf,89,(IDirect3DDevice9* d,DWORD fvf),(d,fvf),set_fvf(fvf))
#undef X3M_SHADOW_HOOK
// Render-state shadow (X3M_STATE_SHADOW, default on). Light boundary like the
// other hot setters: nothing before the native call (lazy RT mode never holds
// a write mask: route-per-draw-cost.md lever 3), after it the shadow store;
// check_no_x87.py walks this hook too.
HRESULT WINAPI set_render_state(IDirect3DDevice9* d,D3DRENDERSTATETYPE state,DWORD value){
    LightCallBoundary cpu;
    LightAdmissionScope admission;
    PlainHookGuard lock;auto& ctx=hooked_device(d);
    cpu.before_original();
    HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,D3DRENDERSTATETYPE,DWORD)noexcept>(57)(d,state,value);cpu.after_original();
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    hr=ctx.motion_output.fixture_setter_result(hr,57,unsigned(state));
#endif
    if(SUCCEEDED(hr))ctx.motion_output.set_render_state(state,value);
    else ctx.motion_output.render_state_failed(state);
    return hr;
}
// Texture tracking serves mip bias and emission reader identity. Sampler
// state tracking serves mip bias and linear materials (X3M_LINEAR_MATERIALS):
// successful SRGBTEXTURE writes establish material admission without changing
// sampler decoding. Light boundary like the other hot setters: integer
// stores on both sides of the native call. The texture's level count is read
// once per pointer change, inside the native section, from the object the
// application just passed (valid by the call's own contract); GetLevelCount
// is a D3D runtime accessor like the native SetTexture beside it, an
// indirect call check_no_x87.py does not walk (same as the native slot).
HRESULT WINAPI set_texture(IDirect3DDevice9* d,DWORD stage,IDirect3DBaseTexture9* texture){
    LightCallBoundary cpu;
    LightAdmissionScope admission;
    PlainHookGuard lock;auto& ctx=hooked_device(d);
    const bool query=ctx.motion_output.texture_levels_wanted(stage,texture);
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*)noexcept>(65)(d,stage,texture);
    const DWORD levels=SUCCEEDED(hr)&&query?texture->GetLevelCount():0;
    const int reader=SUCCEEDED(hr)?ctx.motion_output.composition_texture_reader(stage,texture):2;
    cpu.after_original();
    if(SUCCEEDED(hr))ctx.motion_output.set_texture(stage,texture,levels,query,reader);
    return hr;
}
HRESULT WINAPI set_sampler_state(IDirect3DDevice9* d,DWORD stage,D3DSAMPLERSTATETYPE type,DWORD value){
    LightCallBoundary cpu;
    LightAdmissionScope admission;
    PlainHookGuard lock;auto& ctx=hooked_device(d);
    if(type==D3DSAMP_MIPMAPLODBIAS)ctx.motion_output.before_set_sampler_state(stage,type); // the only type it acts on
    cpu.before_original();
    HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,D3DSAMPLERSTATETYPE,DWORD)noexcept>(69)(d,stage,type,value);cpu.after_original();
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    if(type==D3DSAMP_SRGBTEXTURE)hr=ctx.motion_output.fixture_setter_result(hr,69,stage);
#endif
    if(SUCCEEDED(hr))ctx.motion_output.set_sampler_state(stage,type,value);
    else ctx.motion_output.sampler_state_failed(stage,type);
    return hr;
}
HRESULT WINAPI begin_stateblock(IDirect3DDevice9* d){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings(); // Recording starts from the application's bindings.
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*)>(60)(d);cpu.after_original();
    if(SUCCEEDED(hr))ctx.motion_output.begin_stateblock();
    return hr;
}
// Lazy-mode and HDR hooks (X3M_MOTION_RT_MODE=lazy, X3M_HDR=1): the
// application's target getters must report its own bindings, so a kept
// RT1/RT2 is released first and, while the scene is redirected to the FP16
// target, GetRenderTarget(0) answers with the application's logical main
// surface (the logical-binding shim) and a read of the main target's contents
// receives the pending FP16 content first.
HRESULT WINAPI get_rt(IDirect3DDevice9* d,DWORD index,IDirect3DSurface9** out){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    if(ctx.motion_output.hdr_logical_render_target(index,out))return S_OK;
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9**)>(38)(d,index,out);cpu.after_original();
    return hr;
}
HRESULT WINAPI get_rt_data(IDirect3DDevice9* d,IDirect3DSurface9* source,IDirect3DSurface9* dest){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    ctx.motion_output.before_render_target_read(source);
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*,IDirect3DSurface9*)>(32)(d,source,dest);cpu.after_original();
    return hr;
}
ULONG WINAPI stateblock_release(IDirect3DStateBlock9* block){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    auto fn=stateblocks.at(block)->get<ULONG(WINAPI*)(IDirect3DStateBlock9*)>(2);
    cpu.before_original();
    const ULONG refs=fn(block);cpu.after_original();
    if(!refs)stateblocks.erase(block);
    return refs;
}
HRESULT WINAPI stateblock_apply(IDirect3DStateBlock9* block){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    auto& hooks=*stateblocks.at(block);
    const auto device=devices.find(hooks.device);
    if(device!=devices.end())device->second->motion_output.restore_bindings(); // Apply must land on the application's bindings.
    cpu.before_original();
    const HRESULT hr=hooks.get<HRESULT(WINAPI*)(IDirect3DStateBlock9*)>(5)(block);cpu.after_original();
    if(SUCCEEDED(hr)&&device!=devices.end())device->second->motion_output.stateblock_applied();
    return hr;
}
void hook_stateblock(IDirect3DDevice9* d,IDirect3DStateBlock9* block){
    if(!block||stateblocks.count(block))return;
    auto hooks=std::make_unique<StateBlockHooks>(block,d);
    hooks->set(2,stateblock_release);hooks->set(5,stateblock_apply);
    auto entry=stateblocks.emplace(block,std::move(hooks));
    entry.first->second->install(block);
}
HRESULT WINAPI create_stateblock(IDirect3DDevice9* d,D3DSTATEBLOCKTYPE type,IDirect3DStateBlock9** out){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings(); // The block captures the application's bindings.
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,D3DSTATEBLOCKTYPE,IDirect3DStateBlock9**)>(59)(d,type,out);cpu.after_original();
    if(SUCCEEDED(hr)&&out)hook_stateblock(d,*out);
    return hr;
}
HRESULT WINAPI end_stateblock(IDirect3DDevice9* d,IDirect3DStateBlock9** out){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DStateBlock9**)>(61)(d,out);cpu.after_original();
    // Recording ends whether or not the block was produced; the shadow resyncs.
    ctx.motion_output.end_stateblock();
    if(SUCCEEDED(hr)&&out)hook_stateblock(d,*out);
    return hr;
}
void hook_device(IDirect3DDevice9* d,HWND window,HWND focus) {
    if (devices.count(d)) return;
    IDirect3DDevice9Ex* ex=nullptr;
    const bool supports_ex=SUCCEEDED(d->QueryInterface(IID_IDirect3DDevice9Ex,reinterpret_cast<void**>(&ex)))
        && static_cast<void*>(ex)==static_cast<void*>(d);
    if(ex) ex->Release();
    auto ctx=std::make_shared<Device>(d,supports_ex?134:119);
    ctx->scene_depth.configure(scene_depth_capture_requested);
    ctx->stats.device=ctx->id;ctx->stats.window=window;ctx->stats.focus_window=focus;
    const HRESULT caps_result=d->GetDeviceCaps(&ctx->caps);
    log("capture_caps result=%08lx streams=%lu vs_float_count=%lu ps_version=%08lx",caps_result,ctx->caps.MaxStreams,ctx->caps.MaxVertexShaderConst,ctx->caps.PixelShaderVersion);
    ctx->set(2,release_device); ctx->set(16,reset); ctx->set(17,present);
    if(supports_ex)ctx->set(132,reset_ex);
    ctx->set(37,set_rt);ctx->set(43,clear);
    if(scene_depth_capture_requested){
        ctx->set(34,stretch_rect);ctx->set(39,set_depth);
        ctx->set(30,update_surface);ctx->set(31,update_texture);ctx->set(35,color_fill);
        ctx->set(115,draw_rect_patch);ctx->set(116,draw_tri_patch);
    }
    if(telemetry::enabled()){
        ctx->set(34,stretch_rect);ctx->set(39,set_depth);
        ctx->set(10,cursor_properties);ctx->set(11,cursor_position);ctx->set(12,cursor_show);
        ctx->set(23,create_texture);ctx->set(24,create_volume);ctx->set(25,create_cube);
        ctx->set(26,create_vb);ctx->set(27,create_ib);ctx->set(28,create_rt);ctx->set(29,create_depth);
    }
    ctx->set(81,draw_primitive); ctx->set(82,draw_indexed); ctx->set(83,draw_up); ctx->set(84,draw_indexed_up);
    ctx->set(91,create_vs); ctx->set(106,create_ps);
    // Publish only after the owning map allocation succeeds.
    forget_cached_device(); // a reused address must not answer from a retired context
    auto entry=devices.emplace(d,std::move(ctx));
    entry.first->second->install(d);
    log("device_hooked ptr=%p device=%llu ex=%u",d,devices.at(d)->id,supports_ex);
    // Attach after install: the route's own device calls use the native table
    // captured by Hooks, so nothing here re-enters the hooks.
    auto& hooked=*devices.at(d);
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    fixture_apply(hooked);
#endif
    hooked.motion_output.configure_jitter(motion_jitter_requested,motion_jitter_samples);
    hooked.motion_output.configure_cut_bounds(motion_cut_median_px,motion_cut_missing);
    hooked.motion_output.configure_taa(taa_requested,taa_debug_requested);
    hooked.motion_output.configure_taa_k(taa_k_override);
    hooked.motion_output.configure_mip_bias(taa_mip_bias);
    hooked.motion_output.configure_taa_sharpen(taa_sharpen);
    hooked.motion_output.configure_sun_occlusion({sun_occlusion::override_enabled(),sun_occlusion::logging(),sun_occlusion_radius,sun_occlusion_curve,sun_occlusion_core_f});
    hooked.motion_output.configure_taa_resolve(taa_current_filter,taa_history_weight);
    hooked.motion_output.configure_taa_line_filter(taa_line_filter,taa_line_width);
    hooked.motion_output.configure_taa_far(taa_far[0],taa_far[1],taa_far[2],taa_far[3],taa_far[4],taa_far[5]);
    hooked.motion_output.configure_taa_thin_region(taa_thin_region[0],taa_thin_region[1],taa_thin_region[2],taa_thin_region[3],taa_thin_gate_given,taa_thin_camera_gate);
    hooked.motion_output.configure_taa_sentinel(taa_sentinel[0],taa_sentinel[1]);
    hooked.motion_output.configure_taa_flicker(taa_thin_clip,taa_adaptive_weight,taa_adaptive_lo,taa_adaptive_hi,taa_alpha_history);
    hooked.motion_output.configure_rt_mode(motion_rt_lazy);
    hooked.motion_output.configure_frame_log(motion_frame_log);
    hooked.motion_output.configure_sentinel(taa_sentinel_mode,camera_cut_degrees,camera_log_frames);
    hooked.motion_output.configure_unmatched_static(taa_unmatched_static);
    // Render-state configuration (hybrid unhook): the reasons that keep the
    // SetRenderState/SetSamplerState hooks installed, then the capability
    // check of the documented reads the unhooked route depends on (the proxy
    // strips PUREDEVICE, so a non-pure device answers Get*; a device that
    // does not fails closed to the hooked configuration). Both reads go
    // through the saved native entries (58, 68), never through a hook.
    const char* state_hooks_reason=motion_state_shadow==1?"explicit":frame_timing::active?"frame_timing":nullptr;
    if(!state_hooks_reason){
        DWORD value=0;
        const bool get_ok=SUCCEEDED(hooked.get<HRESULT(WINAPI*)(IDirect3DDevice9*,D3DRENDERSTATETYPE,DWORD*)>(58)(d,D3DRS_ZENABLE,&value))
            &&SUCCEEDED(hooked.get<HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,D3DSAMPLERSTATETYPE,DWORD*)>(68)(d,0,D3DSAMP_SRGBTEXTURE,&value));
        if(!get_ok)state_hooks_reason="get_failed";
    }
    const bool state_hooks=state_hooks_reason!=nullptr;
    hooked.motion_output.configure_state_hooks(state_hooks);
    hooked.motion_output.configure_state_shadow(state_hooks&&motion_state_shadow!=0);
    log("state_hooks device=%llu installed=%u reason=%s state_shadow=%u rs_mode=%s",hooked.id,state_hooks,state_hooks_reason?state_hooks_reason:"none",
        hooked.motion_output.state_shadow(),hooked.motion_output.render_state_mode());
    // Production scene patch and immutable binding persist across devices.
    hooked.motion_output.configure_scene_hook(scene_hook::active());
    hooked.motion_output.configure_hdr(hdr_requested,hdr_config);
    hooked.motion_output.configure_linear_materials(linear_material_requested,linear_material_config);
    hooked.motion_output.configure_linear_emissions(linear_emission_requested,emission_gain);
    hooked.motion_output.configure_linear_distance_fade(linear_distance_fade_requested);
    hooked.motion_output.configure_screen_emission(screen_emission_requested,screen_emission_gain);
    hooked.motion_output.configure_emission_source_gain(emission_source_gain);
    hooked.motion_output.configure_hull_emission_gain(hull_emission_gain);
    hooked.motion_output.configure_original_fill(original_fill);
    hooked.motion_output.configure_hull_lightmap_gain(hull_lightmap_gain);
    if(lightmap_far_fade_requested){
        const bool accepted=hooked.motion_output.configure_lightmap_far_fade(lightmap_far_fade[0],lightmap_far_fade[1],lightmap_far_fade[2]);
        log("light_map_far_fade_configured accepted=%u",unsigned(accepted));
    }
    if(hull_emissive_widening_requested){
        const bool accepted=hooked.motion_output.configure_hull_emissive_widening(hull_emissive_widening[0],hull_emissive_widening[1],hull_emissive_widening[2]);
        log("hull_emissive_widening_configured accepted=%u k=%g q0=%g q1=%g",unsigned(accepted),double(hull_emissive_widening[0]),double(hull_emissive_widening[1]),double(hull_emissive_widening[2]));
    }
    hooked.motion_output.configure_screen_emission_additive(screen_emission_additive_requested,screen_emission_additive_gain,screen_emission_additive_alpha_requested,screen_emission_additive_alpha);
    hooked.motion_output.configure_fade_witness(fade_witness_frames);
    hooked.motion_output.configure_fade_route(fade_route_threshold);
    hooked.motion_output.configure_shimmer_trace(shimmer_trace_requested);
    // Sun-share lane (directional-shadows.md section 2; legacy-sun-application.md
    // section 4.1): the route, TAA and the FP16 scene; no linear-material
    // prerequisite since the original share producer (original shading binds
    // its own share variant, the converted materials theirs).
    bool sun_lane_enabled=false;
    { wchar_t lane[4]{};
      const bool asked=GetEnvironmentVariableW(L"X3M_SUN_SHADOW_LANE",lane,4)==1&&lane[0]==L'1';
      sun_lane_enabled=asked&&motion_output_requested&&taa_requested&&hdr_requested;
      // The lane's RT2 is A32B32G32R32F with .b = the clip w (shadow-receiver-depth.md);
      // the former X3M_SUN_SHADOW_RECEIVER_DEPTH option is gone (the launcher refuses
      // `device`, accepts `linear` as a no-op) and the DLL reads no such variable.
      hooked.motion_output.configure_sun_shadow_lane(sun_lane_enabled); }
    // Caster-candidate counter (shadow-replay-gates.md section 3): the route
    // plus the ownership wrapper (loader.cpp enables the lock bookends on the
    // same switch); no TAA, HDR, linear-material or lane prerequisite.
    // The one-cascade depth replay (same note, "Implemented: cascade-0 depth
    // replay fixture") rides the counter with the same two prerequisites;
    // X3M_SHADOW_REPLAY_SIZE (64..4096, default 1024) sizes the map.
    { wchar_t setting[4]{};
      const bool asked=GetEnvironmentVariableW(L"X3M_SHADOW_REPLAY_CANDIDATES",setting,4)==1&&setting[0]==L'1';
      const bool depth_asked=GetEnvironmentVariableW(L"X3M_SHADOW_REPLAY_DEPTH",setting,4)==1&&setting[0]==L'1';
      const bool wrapped=GetEnvironmentVariableW(L"X3M_OWNERSHIP",setting,4)==1&&setting[0]==L'1';
      const bool enabled=(asked||depth_asked)&&motion_output_requested&&wrapped;
      if(asked||depth_asked)log("shadow_replay_candidates_mode requested=1 enabled=%u motion_output=%u ownership=%u",enabled,motion_output_requested,wrapped);
      unsigned cap=shadow_replay::default_cap; // X3M_SHADOW_REPLAY_CAP (1..record_capacity, default 512): managed candidates recorded per frame
      { wchar_t text[16]{}; if(GetEnvironmentVariableW(L"X3M_SHADOW_REPLAY_CAP",text,16)>0){ const unsigned long v=wcstoul(text,nullptr,10); if(v>=1&&v<=shadow_replay::record_capacity)cap=unsigned(v); } }
      hooked.motion_output.configure_shadow_replay_candidates(enabled,enabled?ownership::process_admission_monitor():nullptr,cap);
      if(depth_asked){
          // The cascade-0 box, read once here (shadow_replay_projection.h ranges):
          // X3M_SHADOW_REPLAY_SIZE (64..4096, default 1024), X3M_SHADOW_REPLAY_EXTENT
          // (half-extent, 50..4000, default 250), X3M_SHADOW_REPLAY_DEPTH_HALF (128..8192, default 512).
          unsigned size=renderer::shadow_replay_size_default; float extent=renderer::shadow_replay_extent_default, depth_half=renderer::shadow_replay_depth_half_default; wchar_t text[32]{}; wchar_t* end=nullptr;
          if(GetEnvironmentVariableW(L"X3M_SHADOW_REPLAY_SIZE",text,32)>0){ const unsigned long v=wcstoul(text,nullptr,10); if(v>=renderer::shadow_replay_size_min&&v<=renderer::shadow_replay_size_max)size=unsigned(v); }
          if(GetEnvironmentVariableW(L"X3M_SHADOW_REPLAY_EXTENT",text,32)>0){ end=nullptr; const float v=wcstof(text,&end); if(end!=text&&*end==L'\0'&&v>=renderer::shadow_replay_extent_min&&v<=renderer::shadow_replay_extent_max)extent=v; }
          if(GetEnvironmentVariableW(L"X3M_SHADOW_REPLAY_DEPTH_HALF",text,32)>0){ end=nullptr; const float v=wcstof(text,&end); if(end!=text&&*end==L'\0'&&v>=renderer::shadow_replay_depth_half_min&&v<=renderer::shadow_replay_depth_half_max)depth_half=v; }
          log("shadow_replay_depth_mode requested=1 enabled=%u size=%u extent=%.9g depth_half=%.9g cap=%u motion_output=%u ownership=%u",enabled,size,double(extent),double(depth_half),cap,motion_output_requested,wrapped);
          hooked.motion_output.configure_shadow_replay_depth(enabled,size,extent,depth_half);
          // Sun-shadow cascades (docs/architecture/shadow-cascades.md): X3M_SHADOW_CASCADES
          // is the ascending half-extent list (1..5 values, 50..150000 units); absent,
          // empty or "0" keeps the single map above. X3M_SHADOW_CASCADE_SIZES (one
          // value for all or one per cascade, 64..4096, default 4096),
          // X3M_SHADOW_CASCADE_CAPS (likewise, 1..4096, defaults 128,512,1024,1024,1024)
          // and X3M_SHADOW_CASCADE_BUDGET (issues per frame, 1..4096, default 640).
          // Caster pool control (shadow-cascade-extents.md): X3M_SHADOW_CASCADE_RECORDS
          // (records per cascade, 1..4096, default 1024), X3M_SHADOW_CASCADE_STATIC_FROM
          // (the first static-only cascade, 1..count-1; absent: none; the count or more is refused) and
          // X3M_SHADOW_CASCADE_DROP_ORDER (submission | importance) and X3M_SHADOW_CASCADE_LARGE_MIN
          // (world units, 0..1e6, default 0: a static-only cascade also admits moving casters of that extent).
          // X3M_SHADOW_CASCADE_BACKFACE_FROM (0..count-1 | none; absent: every cascade whose world texel is
          // 8 u or more) selects the cascades that replay back faces.
          // A malformed list leaves the cascades off (the single map stays).
          { wchar_t list[128]{};
            const auto parse=[](const wchar_t* text,double* out,unsigned capacity)->unsigned{
                unsigned count=0; const wchar_t* cursor=text;
                while(*cursor){ if(count>=capacity)return 0; wchar_t* end=nullptr; const double v=wcstod(cursor,&end); if(end==cursor)return 0; out[count++]=v; if(*end==L',')cursor=end+1; else if(*end==L'\0')break; else return 0; if(!*cursor)return 0; }
                return count; };
            const DWORD length=GetEnvironmentVariableW(L"X3M_SHADOW_CASCADES",list,128);
            if(length>0&&length<128&&!(length==1&&list[0]==L'0')){
                double values[renderer::shadow_cascade_max]{}; float extents[renderer::shadow_cascade_max]{};
                unsigned sizes[renderer::shadow_cascade_max], caps[renderer::shadow_cascade_max], budget=renderer::shadow_cascade_budget_default;
                for(unsigned i=0;i<renderer::shadow_cascade_max;++i){ sizes[i]=renderer::shadow_cascade_size_default; caps[i]=renderer::shadow_cascade_cap_defaults[i]; }
                const unsigned count=parse(list,values,renderer::shadow_cascade_max);
                const char* reason=count?nullptr:"extents";
                for(unsigned i=0;i<count;++i)extents[i]=float(values[i]);
                const auto integers=[&](const wchar_t* name,unsigned* out)->bool{
                    const DWORD n=GetEnvironmentVariableW(name,list,128); if(!n)return true; if(n>=128)return false;
                    double v[renderer::shadow_cascade_max]{}; const unsigned got=parse(list,v,renderer::shadow_cascade_max);
                    if(got!=1&&got!=count)return false;
                    for(unsigned i=0;i<count;++i){ const double x=v[got==1?0:i]; if(!(x>=1.&&x<=65536.)||x!=double(unsigned(x)))return false; out[i]=unsigned(x); }
                    return true; };
                if(!reason&&!integers(L"X3M_SHADOW_CASCADE_SIZES",sizes))reason="sizes";
                if(!reason&&!integers(L"X3M_SHADOW_CASCADE_CAPS",caps))reason="caps";
                if(!reason&&GetEnvironmentVariableW(L"X3M_SHADOW_CASCADE_BUDGET",list,128)>0){ wchar_t* end=nullptr; const unsigned long v=wcstoul(list,&end,10); if(end==list||*end!=L'\0'||v<renderer::shadow_cascade_budget_min||v>renderer::shadow_cascade_budget_max)reason="budget"; else budget=unsigned(v); }
                unsigned records[renderer::shadow_cascade_max]; for(unsigned i=0;i<renderer::shadow_cascade_max;++i)records[i]=renderer::shadow_cascade_records_default;
                unsigned static_from=renderer::shadow_cascade_static_from_none; bool importance=false; float large_min=0.f;
                if(!reason&&!integers(L"X3M_SHADOW_CASCADE_RECORDS",records))reason="records";
                if(!reason&&GetEnvironmentVariableW(L"X3M_SHADOW_CASCADE_STATIC_FROM",list,128)>0){ wchar_t* end=nullptr; const unsigned long v=wcstoul(list,&end,10); if(end==list||*end!=L'\0'||v<1||v>=count)reason="static_from"; else static_from=unsigned(v); }
                if(!reason&&GetEnvironmentVariableW(L"X3M_SHADOW_CASCADE_DROP_ORDER",list,128)>0){ if(!wcscmp(list,L"importance"))importance=true; else if(wcscmp(list,L"submission"))reason="drop_order"; }
                renderer::ShadowCascadeSet set{};
                char static_text[12]; std::snprintf(static_text,sizeof static_text,"%u",static_from<renderer::shadow_cascade_max?static_from:0u);
                if(!reason&&!renderer::shadow_cascade_set(extents,count,sizes,caps,budget,set))reason="range";
                if(!reason&&GetEnvironmentVariableW(L"X3M_SHADOW_CASCADE_LARGE_MIN",list,128)>0){ wchar_t* end=nullptr; const double v=wcstod(list,&end); if(end==list||*end!=L'\0'||!(v>=0.)||v>double(renderer::shadow_cascade_large_min_max))reason="large_min"; else large_min=float(v); }
                // Back-face casters (shadow_replay_projection.h, shadow_cascade_backface_texel_default): absent, the texel
                // law (every cascade whose world texel is 8 u or more); X3M_SHADOW_CASCADE_BACKFACE_FROM = K (0..count-1)
                // that cascade and beyond; "none" no cascade; the count or more is refused (never a silent no-op).
                unsigned backface_from=renderer::shadow_cascade_backface_from_texel;
                if(!reason&&GetEnvironmentVariableW(L"X3M_SHADOW_CASCADE_BACKFACE_FROM",list,128)>0){
                    if(!wcscmp(list,L"none"))backface_from=renderer::shadow_cascade_static_from_none;
                    else { wchar_t* end=nullptr; const unsigned long v=wcstoul(list,&end,10); if(end==list||*end!=L'\0'||v>=count)reason="backface_from"; else backface_from=unsigned(v); } }
                if(!reason&&!renderer::shadow_cascade_pool(set,records,static_from,importance,large_min,backface_from))reason="pool";
                if(reason||!enabled)set=renderer::ShadowCascadeSet{};
                // Own-ship-adaptive cascade 0 (shadow-cascade-extents.md, section 5):
                // X3M_SHADOW_CASCADE_ADAPTIVE_C0 = k within [0.5, 8] (E0 = max(E0, k x
                // own-ship radius) with hysteresis; the ladder behind it slides with E0
                // at X3M_SHADOW_CASCADE_LADDER_RATIO per cascade, within [2, 16], default 5);
                // absent, "0" or out of range: off (a ratio out of range: the default).
                float adaptive_k=0.f, ladder_ratio=renderer::shadow_cascade_ladder_ratio_default;
                if(set.count&&GetEnvironmentVariableW(L"X3M_SHADOW_CASCADE_ADAPTIVE_C0",list,128)>0){ wchar_t* end=nullptr; const double v=wcstod(list,&end);
                    if(end!=list&&*end==L'\0'&&v>=double(renderer::shadow_cascade_adaptive_k_min)&&v<=double(renderer::shadow_cascade_adaptive_k_max))adaptive_k=float(v); }
                if(adaptive_k>0.f&&GetEnvironmentVariableW(L"X3M_SHADOW_CASCADE_LADDER_RATIO",list,128)>0){ wchar_t* end=nullptr; const double v=wcstod(list,&end);
                    if(end!=list&&*end==L'\0'&&v>=double(renderer::shadow_cascade_ladder_ratio_min)&&v<=double(renderer::shadow_cascade_ladder_ratio_max))ladder_ratio=float(v); }
                static_assert(renderer::shadow_cascade_max==5,"the mode line lists five cascades");
                const auto ext=[&](unsigned i){ return double(set.count>i?set.cascades[i].half_extent:0.f); };
                const auto sz=[&](unsigned i){ return set.count>i?set.cascades[i].size:0u; };
                char backface_text[12]; std::snprintf(backface_text,sizeof backface_text,"%u",set.backface_from<set.count?set.backface_from:0u);
                log("shadow_cascades_mode requested=1 enabled=%u reason=%s cascades=%u extents=%.9g,%.9g,%.9g,%.9g,%.9g sizes=%u,%u,%u,%u,%u caps=%u,%u,%u,%u,%u budget=%u depth_light=%.9g records=%u,%u,%u,%u,%u static_from=%s drop_order=%s large_min=%.9g adaptive_c0=%.9g ladder_ratio=%.9g backface_from=%s backface_mask=%u",
                    set.count!=0,reason?reason:enabled?"ok":"replay",set.count,ext(0),ext(1),ext(2),ext(3),ext(4),sz(0),sz(1),sz(2),sz(3),sz(4),
                    set.caps[0],set.caps[1],set.caps[2],set.caps[3],set.caps[4],set.budget,double(set.count?set.cascades[0].depth_toward_light:0.f),
                    set.records[0],set.records[1],set.records[2],set.records[3],set.records[4],set.static_from<set.count?static_text:"none",set.importance?"importance":"submission",double(set.large_min),double(adaptive_k),double(ladder_ratio),
                    !set.count?"none":set.backface_from==renderer::shadow_cascade_backface_from_texel?"texel":set.backface_from<set.count?backface_text:"none",unsigned(set.count?set.backface_mask():0u));
                hooked.motion_output.configure_shadow_cascades(set);
                hooked.motion_output.configure_shadow_cascade_adaptive(adaptive_k,ladder_ratio);
                // Per-frame sun trace (X3M_SHADOW_SUN_TRACE=1, default off): one
                // shadow_sun_frame line per frame while the cascades are on, for the
                // re-derivation rate between the sparse shadow_replay_sun_point lines.
                { const bool trace_asked=GetEnvironmentVariableW(L"X3M_SHADOW_SUN_TRACE",setting,4)==1&&setting[0]==L'1';
                  if(trace_asked)log("shadow_sun_trace_mode requested=1 enabled=%u cascades=%u",set.count!=0,set.count);
                  hooked.motion_output.configure_shadow_sun_trace(trace_asked&&set.count!=0); } } }
          // Sun-shadow caster retention (docs/architecture/shadow-caster-retention.md), cascades only, default off:
          // X3M_SHADOW_RETENTION_CENSUS=1 runs the store without references or replay (stage 1),
          // X3M_SHADOW_CASTER_RETENTION=1 replays retained static casters (stage 2; wins over the census).
          // X3M_SHADOW_CASTER_RETENTION_AGE (frames, 1..10000000, default 7200) and
          // X3M_SHADOW_CASTER_RETENTION_EPS (units, 1e-4..100, default 0.05) calibrate both;
          // X3M_SHADOW_RETENTION_TIMING=1 adds the per-draw cost to the frame line (two counter reads per recorded draw).
          { const auto flag=[&](const wchar_t* name){ return GetEnvironmentVariableW(name,setting,4)==1&&setting[0]==L'1'; };
            const bool census=flag(L"X3M_SHADOW_RETENTION_CENSUS"), live=flag(L"X3M_SHADOW_CASTER_RETENTION");
            if(census||live){
                std::uint32_t age=shadow_retention::age_cap_default; double eps=shadow_retention::eps_default; wchar_t number[32]{}; wchar_t* stop=nullptr;
                if(GetEnvironmentVariableW(L"X3M_SHADOW_CASTER_RETENTION_AGE",number,32)>0){ const unsigned long v=wcstoul(number,&stop,10); if(stop!=number&&*stop==L'\0'&&v>=shadow_retention::age_cap_min&&v<=shadow_retention::age_cap_max)age=std::uint32_t(v); }
                if(GetEnvironmentVariableW(L"X3M_SHADOW_CASTER_RETENTION_EPS",number,32)>0){ stop=nullptr; const double v=wcstod(number,&stop); if(stop!=number&&*stop==L'\0'&&v>=shadow_retention::eps_min&&v<=shadow_retention::eps_max)eps=v; }
                const shadow_retention::Mode mode=!enabled?shadow_retention::Mode::Off:live?shadow_retention::Mode::Live:shadow_retention::Mode::Census;
                log("shadow_retention_mode requested=1 enabled=%u mode=%s age_cap=%u eps=%.9g",enabled,!enabled?"off":live?"live":"census",unsigned(age),eps);
                hooked.motion_output.configure_shadow_retention(mode,age,eps,flag(L"X3M_SHADOW_RETENTION_TIMING")); } } }
      // Scene-end sun-shadow application (legacy-sun-application.md section 2;
      // X3M_SUN_SHADOW_APPLY=1): the lane and the depth replay of the same
      // frame; exponent 1 on original shading, 1 / 2.2 with linear materials.
      // X3M_SUN_SHADOW_BIAS_UNITS (world units, 0..1000, default 0.53571875)
      // is the constant compare bias and X3M_SUN_SHADOW_BIAS_CLAMP_TEXELS
      // (1..64, default 20.97152) the receiver-plane clamp and non-planar
      // fallback in world texels, both resolved per frame with the cascade.
      { const bool apply_asked=GetEnvironmentVariableW(L"X3M_SUN_SHADOW_APPLY",setting,4)==1&&setting[0]==L'1';
        const bool apply_enabled=apply_asked&&sun_lane_enabled&&depth_asked&&enabled;
        // X3M_SUN_SHADOW_BIAS_SLOPE_TEXELS (0..8, default 0.2) is the cascade
        // program's slope-scaled margin in texels of the receiver plane's
        // depth slope (sun_shadow_apply_pass.h).
        double bias_units=renderer::sun_shadow_bias_units_default, clamp_texels=renderer::sun_shadow_bias_clamp_texels_default, slope_texels=renderer::sun_shadow_bias_slope_texels_default; wchar_t text[32]{}; wchar_t* end=nullptr;
        if(GetEnvironmentVariableW(L"X3M_SUN_SHADOW_BIAS_UNITS",text,32)>0){ end=nullptr; const double v=wcstod(text,&end); if(end!=text&&*end==L'\0'&&v>=renderer::sun_shadow_bias_units_min&&v<=renderer::sun_shadow_bias_units_max)bias_units=v; }
        if(GetEnvironmentVariableW(L"X3M_SUN_SHADOW_BIAS_CLAMP_TEXELS",text,32)>0){ end=nullptr; const double v=wcstod(text,&end); if(end!=text&&*end==L'\0'&&v>=renderer::sun_shadow_bias_clamp_texels_min&&v<=renderer::sun_shadow_bias_clamp_texels_max)clamp_texels=v; }
        { wchar_t slope_text[32]{}; if(GetEnvironmentVariableW(L"X3M_SUN_SHADOW_BIAS_SLOPE_TEXELS",slope_text,32)>0){ end=nullptr; const double v=wcstod(slope_text,&end); if(end!=slope_text&&*end==L'\0'&&v>=renderer::sun_shadow_bias_slope_texels_min&&v<=renderer::sun_shadow_bias_slope_texels_max)slope_texels=v; } }
        if(apply_asked)log("sun_shadow_apply_mode requested=1 enabled=%u lane=%u replay=%u linear_materials=%u bias_units=%.9g clamp_texels=%.9g slope_texels=%.9g",apply_enabled,sun_lane_enabled,depth_asked&&enabled,linear_material_requested,bias_units,clamp_texels,slope_texels);
        sun_shadow_apply_requested=sun_shadow_apply_requested||apply_enabled; // opens the Ctrl+Shift+F12 sampler
        hooked.motion_output.configure_sun_shadow_apply(apply_enabled,bias_units,clamp_texels,slope_texels); } }
    hooked.motion_output.configure_ambient_occlusion(ambient_occlusion_requested,ambient_occlusion_radius,ambient_occlusion_strength,ambient_occlusion_debug,ambient_occlusion_timing);
    hooked.motion_output.configure_volumetric_fog(volumetric_fog_requested,volumetric_fog_strength,volumetric_fog_anisotropy,volumetric_fog_everywhere,volumetric_fog_timing,volumetric_fog_cards_replace);
    hooked.motion_output.configure_volumetric_fog_range(volumetric_fog_range_stored);
    hooked.motion_output.configure_volumetric_fog_look(volumetric_fog_look,volumetric_fog_look_tuning);
    { LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency); // the frame_end clock; one read per device
      hooked.fps_overlay.configure(fps_overlay_requested,frequency.QuadPart>0?uint64_t(frequency.QuadPart):1); }
    hooked.motion_output.configure_screen_emission_timing(screen_emission_timing_requested);
    hooked.motion_output.attach(d,hooked.original,hooked.id,hooked.caps,motion_output_requested,&hooked.stats);
    // The engine-memory reader's counters at device creation (integers only;
    // telemetry::summary repeats the line with phase=summary).
    engine_memory_line("create",hooked.id,hooked.frame);
    if(sector_background_requested)hooked.set(41,begin_scene); // standalone diagnostic needs no motion route
    if(hooked.motion_output.enabled()){
        // The route needs the complete selector event stream plus setter
        // shadows. Installed only after the capability gate passed, so a device
        // the route refuses pays nothing per setter call; the private table is
        // already live, so these slots take effect immediately.
        hooked.set(34,stretch_rect);hooked.set(39,set_depth);
        hooked.set(30,update_surface);hooked.set(31,update_texture);hooked.set(35,color_fill);
        hooked.set(115,draw_rect_patch);hooked.set(116,draw_tri_patch);
        hooked.set(92,set_vs);hooked.set(107,set_ps);hooked.set(94,set_vs_constant_f);hooked.set(96,set_vs_constant_i);
        hooked.set(109,set_ps_constant_f);hooked.set(100,set_stream_source);hooked.set(104,set_indices);
        hooked.set(87,set_declaration);hooked.set(89,set_fvf);hooked.set(47,set_viewport);
        hooked.set(59,create_stateblock);hooked.set(60,begin_stateblock);hooked.set(61,end_stateblock);
        // Scene and query tracking for the resolve's caller contract.
        hooked.set(41,begin_scene);hooked.set(42,end_scene);hooked.set(118,create_query);
        // Render-state shadow: the application's render-state writes, only in
        // the hooked configuration (state_hooks above: explicit shadow, frame
        // timing's state-call counts, or a failed Get* check). Otherwise the route reads at the draw.
        if(hooked.motion_output.state_hooks())hooked.set(57,set_render_state);
        // Texture levels are needed for mip bias and for the hull emissive
        // widening's mip-chain gate; the composition reader identity stays on
        // SetTexture in both configurations.
        if(hooked.motion_output.mip_bias_active()||hooked.motion_output.hull_emissive_widening()||hooked.motion_output.composition_requested())hooked.set(65,set_texture);
        // Sampler writes (mip-bias restore ahead of an application LODBIAS
        // write, sRGB decode shadow of the material and screen gates) in the
        // hooked configuration only; frame timing counts them like the rest.
        if(hooked.motion_output.state_hooks()&&(hooked.motion_output.mip_bias_active()||hooked.motion_output.linear_materials_requested()||hooked.motion_output.screen_emission_requested()||hooked.motion_output.screen_emission_additive_requested()||frame_timing::active))hooked.set(69,set_sampler_state);
        // Lazy binding: the application's target getters restore first (its
        // write masks are never held, so GetRenderState stays unhooked).
        if(hooked.motion_output.lazy_rt_mode()){hooked.set(38,get_rt);hooked.set(32,get_rt_data);}
        // HDR redirect: the application's GetRenderTarget(0) and its reads of
        // the main target's contents go through the logical-binding shim.
        if(hooked.motion_output.hdr_enabled()){hooked.set(38,get_rt);hooked.set(32,get_rt_data);}
    }
    ownership_depth_info(d,devices.at(d)->id,devices.at(d)->frame,"create_after");
}
ULONG WINAPI release_factory(IDirect3D9* d) {
    CpuCallBoundary cpu;
    auto* monitor=ownership::process_admission_monitor();
    ownership::ApplicationAdmissionAbi admission(monitor);
    ULONG refs;
    {
        HookGuard lock;
        cpu.before_original();
        refs=factories.at(d)->get<ULONG (WINAPI*)(IDirect3D9*)>(2)(d);
        cpu.after_original();
        if(!refs)factories.erase(d);
    }
    if(!refs){
        // End this application root after native/map cleanup and after releasing
        // capture's mutex. An outer caller may still be active; this snapshot is
        // diagnostic, never a general assertion of process-wide quiescence.
        admission.finish();
        final_admission_metric(monitor,"factory");
    }
    return refs;
}
HRESULT WINAPI create_device(IDirect3D9* d,UINT adapter,D3DDEVTYPE type,HWND window,DWORD flags,D3DPRESENT_PARAMETERS* p,IDirect3DDevice9** out) {
    // Which copy of the shipped-twice helper DLL this process loaded: the game
    // directory carries its own d3dx9_37.dll next to the system one. Logged at
    // the first device creation, when the game's imports are resolved. Ahead of
    // the hook mutex and of the HookGuard's frame-timing scope, so the one file
    // hash neither blocks another hooked call nor lands in the first window's
    // state bucket; its own cost is reported as hash_us=.
    {
        static std::atomic<bool> helper_module_logged{false};
        if(!helper_module_logged.exchange(true)){LightCallBoundary boundary;proxy_identity::log_loaded_module(L"d3dx9_37.dll");}
    }
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HRESULT hr=E_FAIL;
    HookGuard lock;
    // The startup motion route (including HDR/TAA/bloom) saves and resyncs
    // state through documented Get* calls. PUREDEVICE forbids those reads.
    // Keep hardware/mixed/software VP and every other application flag intact;
    // normalize only this optional optimization before either the native or
    // ownership-wrapped factory creates the device. No retry with pure flags:
    // one native call retains its ordinary failure/output/parameter semantics.
    const DWORD effective_flags=motion_output_requested ? flags & ~D3DCREATE_PUREDEVICE : flags;
    log("device_creation_policy requested=%08lx effective=%08lx state_reads=%u",flags,effective_flags,unsigned(motion_output_requested));
    presentation_parameters("create_before",0,window,p);
    if(p) log("create_device adapter=%u flags=%08lx width=%u height=%u format=%u windowed=%u msaa=%u interval=%u",adapter,flags,p->BackBufferWidth,p->BackBufferHeight,p->BackBufferFormat,p->Windowed,p->MultiSampleType,p->PresentationInterval);
    const auto begin=telemetry::now();
    cpu.before_original();
    hr=factories.at(d)->get<HRESULT (WINAPI*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**)>(16)(d,adapter,type,window,effective_flags,p,out);cpu.after_original();
    telemetry::record(telemetry::process(),telemetry::Metric::CreateDevice,telemetry::now()-begin,FAILED(hr));
    presentation_parameters("create_after",0,window,p);
    if(SUCCEEDED(hr)&&p) cull_small_parts::set_backbuffer_width(p->BackBufferWidth); // X3M_CULL_SMALL_PARTS_PX only: the pixel scale of the threshold
    log("create_device_result hr=%08lx",hr);
    if(SUCCEEDED(hr)&&out&&*out){hook_device(*out,p&&p->hDeviceWindow?p->hDeviceWindow:window,window);}
    return hr;
}
}
bool screen_emission_route_enabled() noexcept { return screen_emission_requested; } // the one gate the loader's scan enable shares
// The lens bracket's listener (src/proxy/sun_occlusion.h): the engine's render thread, inside its
// `call 0x0047e6e0` for the lens scene, under the thunk's full CPU-state boundary.
void sun_lens_begin() { CaptureLock lock; for(auto& entry:devices) entry.second->motion_output.sun_occlusion_begin(); }
void sun_lens_end() { CaptureLock lock; for(auto& entry:devices) entry.second->motion_output.sun_occlusion_end(); }
void initialize_log(HMODULE module) {
    CaptureLock lock;
    wchar_t path[32768]{}; GetModuleFileNameW(module,path,32768);
    directory=path; directory.resize(directory.find_last_of(L"\\/"));
    directory+=L"\\x3-modern-captures"; CreateDirectoryW(directory.c_str(),nullptr);
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t suffix[100]; swprintf(suffix,100,L"\\session-%04u%02u%02u-%02u%02u%02u-%lu.log",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId());
    const char* source="game";
    logfile=_wfopen((directory+suffix).c_str(),L"w");
    if(!logfile){
        // W3 of the native-Windows audit: the game directory may be read-only
        // (Program Files without Steam's ACL grant, a virtualised install);
        // the log then goes to %LOCALAPPDATA%\x3-modern-renderer\captures
        // (the variable, else %USERPROFILE%\AppData\Local) and the first line
        // records which directory was taken. capture_directory() follows.
        const auto environment=[](const wchar_t* name){
            std::wstring value(GetEnvironmentVariableW(name,nullptr,0),L'\0');
            if(value.size()<2) return std::wstring();
            const DWORD length=GetEnvironmentVariableW(name,&value[0],static_cast<DWORD>(value.size()));
            if(!length||length>=value.size()) return std::wstring(); // gone or grown in between
            value.resize(length);
            return value;
        };
        std::wstring base=environment(L"LOCALAPPDATA");
        if(base.empty()){ base=environment(L"USERPROFILE"); if(!base.empty()) base+=L"\\AppData\\Local"; }
        if(!base.empty()){
            base+=L"\\x3-modern-renderer"; CreateDirectoryW(base.c_str(),nullptr);
            base+=L"\\captures"; CreateDirectoryW(base.c_str(),nullptr);
            logfile=_wfopen((base+suffix).c_str(),L"w");
            if(logfile){ directory=base; source="localappdata"; }
        }
    }
    if(logfile) setvbuf(logfile,nullptr,_IOFBF,1024*1024);
    if(logfile) log_os_handle=reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(logfile)));
    {
        // Logged as UTF-8 through WideCharToMultiByte: the CRT's %ls conversion
        // runs in the "C" locale and drops the whole line on a character it
        // cannot represent (a non-ASCII user name under %LOCALAPPDATA%, a
        // non-ASCII game path).
        std::string utf8(static_cast<std::size_t>(WideCharToMultiByte(CP_UTF8,0,directory.c_str(),-1,nullptr,0,nullptr,nullptr)),'\0');
        if(utf8.size()>1) utf8.resize(static_cast<std::size_t>(WideCharToMultiByte(CP_UTF8,0,directory.c_str(),-1,&utf8[0],static_cast<int>(utf8.size()),nullptr,nullptr))-1);
        else utf8.clear();
        log("capture_dir=%s source=%s",utf8.c_str(),source);
    }
    // Wall-clock anchor of this session (docs/verification/sampling-profiler.md,
    // "Audio correlation"): one UTC instant read next to one QueryPerformanceCounter
    // reading, with the counter frequency and the local-time offset. Every
    // window and slow-frame line carries qpc= ticks, so a line stamped in local
    // wall clock by another component (GLib prints HH:MM:SS.mmm on the
    // launcher's terminal) can be converted to UTC and mapped onto a frame.
    {
        // GetSystemTimePreciseAsFileTime is documented since Windows 8; resolved
        // dynamically so an older system falls back to the coarser documented call.
        using PreciseTimeFn=void(WINAPI*)(LPFILETIME);
        const HMODULE kernel=GetModuleHandleW(L"kernel32.dll");
        const FARPROC precise_proc=kernel?GetProcAddress(kernel,"GetSystemTimePreciseAsFileTime"):nullptr;
        const PreciseTimeFn precise=precise_proc?reinterpret_cast<PreciseTimeFn>(reinterpret_cast<void*>(precise_proc)):nullptr; // via void*: -Wcast-function-type (fixture builds use -Werror)
        LARGE_INTEGER counter{},frequency{};
        FILETIME utc{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&counter);
        if(precise) precise(&utc); else GetSystemTimeAsFileTime(&utc);
        TIME_ZONE_INFORMATION zone{};
        const DWORD kind=GetTimeZoneInformation(&zone);
        // Win32 states UTC = local + bias, so the local offset is the negated bias.
        const long bias=kind==TIME_ZONE_ID_INVALID?0:zone.Bias+(kind==TIME_ZONE_ID_DAYLIGHT?zone.DaylightBias:zone.StandardBias);
        SYSTEMTIME moment{};
        if(FileTimeToSystemTime(&utc,&moment))
            log("clock_anchor utc=%04u-%02u-%02uT%02u:%02u:%02u.%03uZ qpc=%llu qpc_frequency=%llu local_offset_min=%ld",
                unsigned(moment.wYear),unsigned(moment.wMonth),unsigned(moment.wDay),unsigned(moment.wHour),unsigned(moment.wMinute),
                unsigned(moment.wSecond),unsigned(moment.wMilliseconds),
                counter.QuadPart>0?static_cast<unsigned long long>(counter.QuadPart):0ull,
                frequency.QuadPart>0?static_cast<unsigned long long>(frequency.QuadPart):0ull,
                kind==TIME_ZONE_ID_INVALID?0l:-bias);
    }
    // Session identity ahead of every derived *_mode line, so that a gameplay
    // log always names the DLL and the options it came from (docs/architecture/
    // platform-portability.md, "Session identity"). Attach only: one file hash
    // and one environment scan, never on the render path.
    proxy_identity::log_identity(module);
    wchar_t setting[32]{};
    if(GetEnvironmentVariableW(L"X3M_CAPTURE_START",setting,32)>0) capture_start=wcstoul(setting,nullptr,10);
    if(GetEnvironmentVariableW(L"X3M_CAPTURE_FRAMES",setting,32)>0) capture_count=wcstoul(setting,nullptr,10);
    // 64: a plain frame counter (ctx.remaining); above 8 serves the raw TAA
    // debug dumps (about 40 MB per 1280x768 frame), see tools/manage.py.
    if(capture_count>64) capture_count=64;
    // X3M_CAPTURE_DELAY (frames, default 0 = immediate): frames between the F8
    // press and the start of the burst, so SETA can be re-engaged after the key
    // press cancels it. Malformed keeps 0; read once, used on the Present path.
    {   // A return of 32 or more means truncation: the buffer content is then
        // undefined, so the value is refused rather than parsed.
        const DWORD length=GetEnvironmentVariableW(L"X3M_CAPTURE_DELAY",setting,32);
        if(length>0&&length<32){
            wchar_t* stop=nullptr; const unsigned long v=wcstoul(setting,&stop,10);
            if(stop!=setting&&*stop==L'\0'&&v<=capture_arm::core::delay_max) capture_delay=unsigned(v);
        }
    }
    // X3M_FRAME_END_STRIDE (1..100000, default 300): frames between frame_end
    // lines. Out of range or malformed keeps the default; the line is written
    // only when the stride is not the default, so a default run is unchanged.
    if(GetEnvironmentVariableW(L"X3M_FRAME_END_STRIDE",setting,32)>0){
        wchar_t* stop=nullptr; const unsigned long v=wcstoul(setting,&stop,10);
        if(stop!=setting&&*stop==L'\0'&&v>=1&&v<=frame_end_stride_max)frame_end_stride=unsigned(v);
        if(frame_end_stride!=frame_end_stride_default)log("frame_end_stride_mode stride=%u",frame_end_stride);
    }
    // X3M_FPS_OVERLAY=1 (default off): the on-screen frame-rate line.
    fps_overlay_requested=GetEnvironmentVariableW(L"X3M_FPS_OVERLAY",setting,32)==1 && setting[0]==L'1';
    if(fps_overlay_requested)log("fps_overlay_mode requested=1 refresh_ms=250 window_ms=1000 key=ctrl_alt_f7");
    scene_depth_capture_requested=GetEnvironmentVariableW(L"X3M_SCENE_DEPTH_CAPTURE",setting,32)==1 && setting[0]==L'1';
    finite_positions_requested=GetEnvironmentVariableW(L"X3M_FINITE_POSITIONS",setting,32)==1 && setting[0]==L'1';
    motion_capture_requested=GetEnvironmentVariableW(L"X3M_MOTION_CAPTURE",setting,32)==1 && setting[0]==L'1' &&
        scene_depth_capture_requested && finite_positions_requested;
    log("motion_capture_mode requested=%u enabled=0 reason=write_exclusion_unavailable scope=private_rigid_diagnostic temporal_consumer=0",motion_capture_requested);
    motion_output_requested=GetEnvironmentVariableW(L"X3M_MOTION_OUTPUT",setting,32)==1 && setting[0]==L'1';
    // Per-draw jitter (off by default) with its Halton sample count, and the
    // cut detector bounds (median origin displacement at 1280 px width,
    // missing-key fraction); see docs/architecture/temporal-integration.md.
    motion_jitter_requested=GetEnvironmentVariableW(L"X3M_MOTION_JITTER",setting,32)==1 && setting[0]==L'1';
    if(GetEnvironmentVariableW(L"X3M_MOTION_JITTER_SAMPLES",setting,32)>0){const unsigned long n=wcstoul(setting,nullptr,10);if(n>=2&&n<=64)motion_jitter_samples=unsigned(n);}
    if(GetEnvironmentVariableW(L"X3M_MOTION_CUT_MEDIAN_PX",setting,32)>0)motion_cut_median_px=wcstof(setting,nullptr);
    if(GetEnvironmentVariableW(L"X3M_MOTION_CUT_MISSING",setting,32)>0)motion_cut_missing=wcstof(setting,nullptr);
    // The temporal resolve at the bloom copy (temporal step 3): requires the
    // route and implies the jitter; X3M_TAA_DEBUG=<n> (n > 0) writes the
    // resolved FP16 image and the pre-resolve color in capture frames.
    taa_requested=motion_output_requested && GetEnvironmentVariableW(L"X3M_TAA",setting,32)==1 && setting[0]==L'1';
    if(taa_requested)motion_jitter_requested=true;
    taa_debug_requested=taa_requested && GetEnvironmentVariableW(L"X3M_TAA_DEBUG",setting,32)>0 && wcstoul(setting,nullptr,10)>0;
    // X3M_TAA_K=<k> (0 <= k <= 65504): a fixed luminance-weighting constant for
    // the resolve on the FP16 scene (X3M_HDR=1); 0 is the unweighted resolve.
    // Unset, out of range or not a number: derived from the write-back's
    // exposure (0 is a valid override, so a failed conversion, which wcstof
    // reports as 0, must not be taken: the whole string has to be consumed).
    if(GetEnvironmentVariableW(L"X3M_TAA_K",setting,32)>0){wchar_t* end=nullptr;const float v=wcstof(setting,&end);if(end!=setting&&*end==L'\0'&&v>=0.f&&v<=65504.f)taa_k_override=v;}
    // X3M_TAA_MIP_BIAS=<bias> (-8 <= bias <= 8, whole string consumed; unset or
    // invalid: -0.5 with the TAA resolve on, otherwise off): requires the route
    // with the jitter (X3M_TAA=1 or X3M_MOTION_JITTER=1); the bias is applied
    // only while the jitter is active. An explicit 0 disables it (the launcher
    // always forwards a value in TAA mode, so this fallback covers a direct
    // WINEDLLOVERRIDES start).
    taa_mip_bias=taa_requested?-0.5f:0.f;
    if(motion_jitter_requested && GetEnvironmentVariableW(L"X3M_TAA_MIP_BIAS",setting,32)>0){wchar_t* end=nullptr;const float v=wcstof(setting,&end);if(end!=setting&&*end==L'\0'&&v>=-8.f&&v<=8.f)taa_mip_bias=v;}
    // X3M_TAA_SHARPEN=<s> (0 <= s <= 1): the post-resolve sharpen; the whole
    // string must parse (0 is off, so a failed conversion must not be taken).
    // Unset or invalid with the resolve on: 0.75; off entirely without TAA.
    taa_sharpen=taa_requested?0.75f:0.f;
    if(taa_requested&&GetEnvironmentVariableW(L"X3M_TAA_SHARPEN",setting,32)>0){wchar_t* end=nullptr;const float v=wcstof(setting,&end);if(end!=setting&&*end==L'\0'&&v>=0.f&&v<=1.f)taa_sharpen=v;}
    // X3M_TAA_CURRENT_FILTER=<A> (0 <= A <= 4; 0 or unset: off) and
    // X3M_TAA_HISTORY_WEIGHT=<w> (0.5 <= w <= 0.98; unset: 0.9): the resolve's
    // filtered current sample and history weight (docs/verification/
    // motion-output.md, "Run 139"). The whole string must parse; an invalid
    // value keeps the default.
    if(taa_requested&&GetEnvironmentVariableW(L"X3M_TAA_CURRENT_FILTER",setting,32)>0){wchar_t* end=nullptr;const float v=wcstof(setting,&end);if(end!=setting&&*end==L'\0'&&v>=0.f&&v<=4.f)taa_current_filter=v;}
    // X3M_TAA_LINE_FILTER=<A>[,<W>] (A 0..4; 0 or unset: off; W 1 or 2, default 1): the same filter on line-like pixels only
    // (docs/architecture/taa-lattice-crawl.md section 9); refused by the route when the global filter is on.
    if(taa_requested&&GetEnvironmentVariableW(L"X3M_TAA_LINE_FILTER",setting,32)>0){wchar_t* end=nullptr;const float v=wcstof(setting,&end);if(end!=setting&&v>=0.f&&v<=4.f){if(*end==L'\0')taa_line_filter=v;else if(*end==L','&&(end[1]==L'1'||end[1]==L'2')&&end[2]==L'\0'){taa_line_filter=v;taa_line_width=unsigned(end[1]-L'0');}}}
    // X3M_TAA_FAR_STABILISER=<W>[,<A>[,<F0>,<F1>[,<LO>,<HI>]]] (0 <= LO < HI <= 64 px/frame, the weight's speed gate) (docs/architecture/taa-distant-line-fade.md section 9; unset: off):
    // W 0 or 0.5..0.99 (checked against the history weight at attach), A 0..4, 0 < F0 < F1 <= 1e6 units per pixel.
    // The whole string must parse (1, 2, 4 or 6 fields); anything else keeps the option off.
    {wchar_t far_setting[64];const DWORD length=taa_requested?GetEnvironmentVariableW(L"X3M_TAA_FAR_STABILISER",far_setting,64):0;
        if(length>0&&length<64){float v[6]={0.f,0.f,80.f,130.f,.03f,.25f};unsigned count=0;wchar_t* cursor=far_setting;bool ok=true;
            while(ok&&count<6){wchar_t* end=nullptr;v[count]=wcstof(cursor,&end);ok=end!=cursor;++count;if(!ok||*end==L'\0')break;ok=*end==L',';cursor=end+1;if(count==6)ok=false;}
            ok=ok&&(count==1||count==2||count==4||count==6)&&v[4]>=0.f&&v[5]>v[4]&&v[5]<=64.f&&(v[0]==0.f||(v[0]>=.5f&&v[0]<=.99f))&&v[1]>=0.f&&v[1]<=4.f&&v[2]>0.f&&v[3]>v[2]&&v[3]<=1e6f;
            if(ok)for(unsigned i=0;i<6;++i)taa_far[i]=v[i];else log("taa_far_setting invalid=1");}
        else if(length>=64)log("taa_far_setting invalid=1 reason=too_long length=%lu",length);}
    // X3M_TAA_THIN_REGION=<W>[,<RELAX>[,<LO>,<HI>]] (docs/architecture/taa-lattice-crawl.md section 13; unset: off): W 0 or
    // 0.5..0.99, RELAX 0..1 (default 1: clip off on the region), 0 <= LO < HI <= 64 px/frame (given: replaces the far
    // stabiliser's gate, which the two share). 1, 2 or 4 fields; anything else keeps the option off.
    {wchar_t thin_setting[48];const DWORD length=taa_requested?GetEnvironmentVariableW(L"X3M_TAA_THIN_REGION",thin_setting,48):0;
        if(length>0&&length<48){float v[4]={0.f,1.f,.03f,.25f};unsigned count=0;wchar_t* cursor=thin_setting;bool ok=true;
            while(ok&&count<4){wchar_t* end=nullptr;v[count]=wcstof(cursor,&end);ok=end!=cursor;++count;if(!ok||*end==L'\0')break;ok=*end==L',';cursor=end+1;if(count==4)ok=false;}
            ok=ok&&(count==1||count==2||count==4)&&(v[0]==0.f||(v[0]>=.5f&&v[0]<=.99f))&&v[1]>=0.f&&v[1]<=1.f&&v[2]>=0.f&&v[3]>v[2]&&v[3]<=64.f;
            if(ok){for(unsigned i=0;i<4;++i)taa_thin_region[i]=v[i];taa_thin_gate_given=count==4;}else log("taa_thin_region_setting invalid=1");}
        else if(length>=48)log("taa_thin_region_setting invalid=1 reason=too_long length=%lu",length);}
    // X3M_TAA_THIN_REGION_GATE=screen|camera (section 32.1): "camera" gates the region on min(screen speed, camera-relative
    // speed) with the 7x7 box clip where the camera term alone opens it; "screen" is the screen-speed gate the thin region
    // originally had. Anything else keeps the screen gate and is logged. Meaningful only with X3M_TAA_THIN_REGION on.
    // Absent is the Run59-accepted default: camera whenever the thin region is on, except with the line filter, whose mask
    // channel carries the only gate it can have, where the default stays screen (the route refuses the pair as well).
    {wchar_t gate_setting[16];const DWORD length=taa_requested?GetEnvironmentVariableW(L"X3M_TAA_THIN_REGION_GATE",gate_setting,16):0;
        if(length>0&&length<16){if(wcscmp(gate_setting,L"camera")==0)taa_thin_camera_gate=true;else if(wcscmp(gate_setting,L"screen")!=0)log("taa_thin_region_gate_setting invalid=1");}
        else if(length>=16)log("taa_thin_region_gate_setting invalid=1 reason=too_long length=%lu",length);
        else if(taa_requested&&taa_thin_region[0]>0.f&&taa_line_filter<=0.f)taa_thin_camera_gate=true;}
    // X3M_TAA_SENTINEL_STABILISER=<S>[,<E>] (docs/architecture/temporal-integration.md "Distant unrouted stations under a
    // pan"): S 0..1, the thin-region strength of unrouted depth-sentinel pixels through the camera
    // gate (always box-clipped); E >= 0, the emitter bound in scene luma (default 1; 0 = none). 1 or 2 fields; anything else
    // keeps the option off. Meaningful only with the camera gate; the route turns it off otherwise and says so.
    // Absent is the Run61/Run62-accepted default: S = 0.7 with E = 1 whenever the TAA route runs with the thin-region
    // camera gate resolved above (that gate is the only path the stabiliser has); off otherwise, and "0" opts out.
    {wchar_t sentinel_setting[32];const DWORD length=taa_requested?GetEnvironmentVariableW(L"X3M_TAA_SENTINEL_STABILISER",sentinel_setting,32):0;
        if(length>0&&length<32){float v[2]={0.f,1.f};unsigned count=0;wchar_t* cursor=sentinel_setting;bool ok=true;
            while(ok&&count<2){wchar_t* end=nullptr;v[count]=wcstof(cursor,&end);ok=end!=cursor;++count;if(!ok||*end==L'\0')break;ok=*end==L',';cursor=end+1;if(count==2)ok=false;}
            ok=ok&&v[0]>=0.f&&v[0]<=1.f&&v[1]>=0.f&&v[1]<=65000.f;
            if(ok){taa_sentinel[0]=v[0];taa_sentinel[1]=v[1];}else log("taa_sentinel_stabiliser_setting invalid=1");}
        else if(length>=32)log("taa_sentinel_stabiliser_setting invalid=1 reason=too_long length=%lu",length);
        else if(taa_requested&&taa_thin_camera_gate)taa_sentinel[0]=.7f;} // run216/run221: 0.7 is the default with the camera gate ("0" opts out)
    if(taa_requested&&GetEnvironmentVariableW(L"X3M_TAA_HISTORY_WEIGHT",setting,32)>0){wchar_t* end=nullptr;const float v=wcstof(setting,&end);if(end!=setting&&*end==L'\0'&&v>=.5f&&v<=.98f)taa_history_weight=v;}
    // Flicker suppression (docs/architecture/taa-flicker-suppression.md), all
    // off when unset or invalid: X3M_TAA_THIN_CLIP=<S> (0..1),
    // X3M_TAA_ADAPTIVE_WEIGHT=<WMAX>[,<LO>,<HI>] (WMAX 0.5..0.99, 0 <= LO < HI
    // <= 64 px/frame; the route refuses it without the thin clip or below the
    // history weight) and X3M_TAA_ALPHA_HISTORY=1 (HDR route only).
    // GetEnvironmentVariableW returns the required size (>= 32) without writing
    // when the value does not fit, leaving the previous variable's text in the
    // buffer: such a value is invalid (option off, one log line), never parsed.
    auto flicker_setting=[&](const wchar_t* name,const char* label){const DWORD length=GetEnvironmentVariableW(name,setting,32);if(length>=32){log("taa_flicker_setting name=%s invalid=1 reason=too_long length=%lu",label,length);return false;}return length>0;};
    if(taa_requested&&flicker_setting(L"X3M_TAA_THIN_CLIP","X3M_TAA_THIN_CLIP")){wchar_t* end=nullptr;const float v=wcstof(setting,&end);if(end!=setting&&*end==L'\0'&&v>=0.f&&v<=1.f)taa_thin_clip=v;}
    if(taa_requested&&flicker_setting(L"X3M_TAA_ADAPTIVE_WEIGHT","X3M_TAA_ADAPTIVE_WEIGHT")){wchar_t* end=nullptr;const float wmax=wcstof(setting,&end);float lo=.1f,hi=.5f;bool okay=end!=setting&&wmax>=.5f&&wmax<=.99f;
        if(okay&&*end==L','){wchar_t* next=nullptr;lo=wcstof(end+1,&next);okay=next!=end+1&&*next==L',';if(okay){end=nullptr;hi=wcstof(next+1,&end);okay=end!=next+1&&*end==L'\0'&&lo>=0.f&&hi>lo&&hi<=64.f;}}
        else okay=okay&&*end==L'\0';
        if(okay){taa_adaptive_weight=wmax;taa_adaptive_lo=lo;taa_adaptive_hi=hi;}}
    taa_alpha_history=taa_requested&&flicker_setting(L"X3M_TAA_ALPHA_HISTORY","X3M_TAA_ALPHA_HISTORY")&&setting[0]==L'1'&&setting[1]==L'\0';
    // The FP16 HDR scene path (stage 1: redirect, identity write-back) needs
    // the route's hooks and selector.
    hdr_requested=motion_output_requested && GetEnvironmentVariableW(L"X3M_HDR",setting,32)==1 && setting[0]==L'1';
    hdr_config=x3m::renderer::HdrConfig{};
    // The selected production appearance is Auto capped at +1.3 EV. Keep
    // standalone component defaults independent; fixed EV0 remains available.
    hdr_config.exposure=x3m::renderer::ExposureMode::Auto;
    hdr_config.params.ev_max=1.3f;
    hdr_config.allow_auto_toggle=true;
    if(GetEnvironmentVariableW(L"X3M_HDR_TONEMAP",setting,32)>0 && (!wcscmp(setting,L"agx")||!wcscmp(setting,L"1")))hdr_config.tonemap=x3m::renderer::HdrTonemap::Agx;
    if(GetEnvironmentVariableW(L"X3M_HDR_DECODE",setting,32)>0){
        if(!wcscmp(setting,L"none"))hdr_config.decode=x3::temporal::AgxDecode::none;
        else if(!wcscmp(setting,L"srgb"))hdr_config.decode=x3::temporal::AgxDecode::srgb;
        else hdr_config.decode=x3::temporal::AgxDecode::gamma22; // gamma2.2 | pow22 | gamma
    }
    if(GetEnvironmentVariableW(L"X3M_HDR_LOOK",setting,32)>0){
        if(!wcscmp(setting,L"golden"))hdr_config.look=x3::temporal::AgxLook::golden;
        else if(!wcscmp(setting,L"punchy"))hdr_config.look=x3::temporal::AgxLook::punchy;
    }
    if(GetEnvironmentVariableW(L"X3M_HDR_CLAMP",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>0&&v<=65504.f)hdr_config.clamp_max=v;}
    const DWORD exposure_length=GetEnvironmentVariableW(L"X3M_HDR_EXPOSURE",setting,32);
    if(exposure_length>0)hdr_config.exposure=(exposure_length<32 && !wcscmp(setting,L"auto"))
        ? x3m::renderer::ExposureMode::Auto : x3m::renderer::ExposureMode::Manual;
    if(GetEnvironmentVariableW(L"X3M_HDR_EV_MANUAL",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>=-16.f&&v<=16.f){hdr_config.exposure=x3m::renderer::ExposureMode::Manual;hdr_config.ev_manual=v;}}
    if(GetEnvironmentVariableW(L"X3M_HDR_EV",setting,32)>0||GetEnvironmentVariableW(L"X3M_HDR_EV_OFFSET",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>=-16.f&&v<=16.f)hdr_config.params.ev_offset=v;}
    if(GetEnvironmentVariableW(L"X3M_HDR_KEY",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>0&&v<=64.f)hdr_config.params.key=v;}
    if(GetEnvironmentVariableW(L"X3M_HDR_EV_MIN",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>=-16.f&&v<=16.f)hdr_config.params.ev_min=v;}
    if(GetEnvironmentVariableW(L"X3M_HDR_EV_MAX",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>=-16.f&&v<=16.f)hdr_config.params.ev_max=v;}
    if(hdr_config.params.ev_min>hdr_config.params.ev_max)hdr_config.params.ev_min=hdr_config.params.ev_max;
    if(GetEnvironmentVariableW(L"X3M_HDR_ADAPT_UP",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>0&&v<=60.f)hdr_config.params.tau_up=v;}
    if(GetEnvironmentVariableW(L"X3M_HDR_ADAPT_DOWN",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>0&&v<=60.f)hdr_config.params.tau_down=v;}
    // Configuration only: reject truncation and malformed values before a
    // zero-valued control can silently disable a meter safeguard.
    const auto meter_parameter = [&](const wchar_t* name, float low, float high, float& output) {
        const DWORD length = GetEnvironmentVariableW(name, setting, 32);
        x3m::renderer::parse_meter_parameter(setting, length, low, high, output);
    };
    meter_parameter(L"X3M_HDR_METER_BG", 1e-4f, 64.f, hdr_config.params.meter_bg);
    meter_parameter(L"X3M_HDR_METER_MIN_LIT", 0.f, 1.f, hdr_config.params.meter_min_lit);
    meter_parameter(L"X3M_HDR_WHITE_TARGET", 0.f, 4.f, hdr_config.params.white_target);
    meter_parameter(L"X3M_HDR_KEY_PULL", 0.f, 1.f, hdr_config.params.key_pull);
    meter_parameter(L"X3M_HDR_EV_DEADBAND", 0.f, 8.f, hdr_config.params.ev_deadband);
    meter_parameter(L"X3M_HDR_METER_EDGE_WEIGHT", 0.f, 1.f, hdr_config.params.meter_edge_weight);
    if(GetEnvironmentVariableW(L"X3M_HDR_DT_MS",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>0&&v<=1000.f)hdr_config.fixed_dt=v/1000.f;}
    const bool material_requested=GetEnvironmentVariableW(L"X3M_LINEAR_MATERIALS",setting,32)==1 && setting[0]==L'1';
    linear_material_config=x3m::renderer::LinearMaterialConfig{};
    // Production material mode uses a small readability floor. Keep the
    // shared config's zero default for explicit K=0 byte-identical artifacts.
    linear_material_config.fill=0.05f;
    bool material_config_valid=true;
    const auto material_gain = [&](const wchar_t* name,float& output,float maximum=16.f) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length=GetEnvironmentVariableW(name,setting,32);
        if(!length && GetLastError()==ERROR_ENVVAR_NOT_FOUND)return;
        if(!length || length>=32){material_config_valid=false;return;}
        wchar_t* end=nullptr;
        const float value=wcstof(setting,&end);
        if(end==setting || *end || !std::isfinite(value) || value<0.f || value>maximum){material_config_valid=false;return;}
        output=value;
    };
    material_gain(L"X3M_MATERIAL_DIRECT_GAIN",linear_material_config.direct_gain);
    material_gain(L"X3M_MATERIAL_EMISSIVE_GAIN",linear_material_config.material_emissive_gain);
    material_gain(L"X3M_LIGHTMAP_EMISSIVE_GAIN",linear_material_config.lightmap_emissive_gain);
    // Constant hemispherical fill, 0..0.5. Explicit 0 is off and produces
    // byte-identical shader programs (docs/architecture/fill-light.md).
    material_gain(L"X3M_MATERIAL_FILL",linear_material_config.fill,0.5f);
    // Unlike the legacy decoder's permissive aliases, an explicit unknown or
    // truncated decode setting cannot authorize the material color contract.
    const DWORD material_decode_length=GetEnvironmentVariableW(L"X3M_HDR_DECODE",setting,32);
    const bool material_decode_valid=material_decode_length<32 && (!material_decode_length || !wcscmp(setting,L"gamma2.2") || !wcscmp(setting,L"pow22") || !wcscmp(setting,L"gamma"));
    const DWORD material_tonemap_length=GetEnvironmentVariableW(L"X3M_HDR_TONEMAP",setting,32);
    const bool material_tonemap_valid=material_tonemap_length>0 && material_tonemap_length<32 && (!wcscmp(setting,L"agx") || !wcscmp(setting,L"1"));
    linear_material_requested=material_requested && material_config_valid && material_decode_valid && material_tonemap_valid && motion_output_requested && hdr_requested
        && hdr_config.tonemap==x3m::renderer::HdrTonemap::Agx && hdr_config.decode==x3::temporal::AgxDecode::gamma22;
    if(material_requested)
        log("linear_material_mode requested=1 enabled=%u config_valid=%u decode_valid=%u tonemap_valid=%u direct_gain=%g material_emissive_gain=%g lightmap_emissive_gain=%g fill=%g",
            linear_material_requested,material_config_valid,material_decode_valid,material_tonemap_valid,double(linear_material_config.direct_gain),
            double(linear_material_config.material_emissive_gain),double(linear_material_config.lightmap_emissive_gain),double(linear_material_config.fill));
    const bool emission_requested=GetEnvironmentVariableW(L"X3M_LINEAR_EMISSIONS",setting,32)==1 && setting[0]==L'1';
    emission_gain=1.f;
    const bool saved_material_valid=material_config_valid;
    material_config_valid=true; material_gain(L"X3M_EMISSION_GAIN",emission_gain);
    const bool emission_config_valid=material_config_valid; material_config_valid=saved_material_valid;
    linear_emission_requested=emission_requested && emission_config_valid && material_decode_valid && material_tonemap_valid
        && motion_output_requested && taa_requested && hdr_requested
        && hdr_config.tonemap==x3m::renderer::HdrTonemap::Agx && hdr_config.decode==x3::temporal::AgxDecode::gamma22;
    if(emission_requested) log("linear_emission_mode requested=1 enabled=%u config_valid=%u gain=%g",linear_emission_requested,emission_config_valid,double(emission_gain));
    const bool fade_requested=GetEnvironmentVariableW(L"X3M_LINEAR_DISTANCE_FADE",setting,32)==1 && setting[0]==L'1';
    linear_distance_fade_requested=fade_requested && linear_material_requested && taa_requested;
    if(fade_requested)log("linear_distance_fade_mode requested=1 enabled=%u materials=%u taa=%u",linear_distance_fade_requested,linear_material_requested,taa_requested);
    // X3M_FADE_ROUTE=<permille>|off (default 500): the fade-band motion arm
    // threshold (docs/architecture/linear-distance-fade-region.md, "Fade-band
    // route"). The arm needs TAA and the FP16 HDR scene, not the material
    // route: since 2026-09-18 it runs under original shading too, so
    // linear_materials is reported beside the predicate, not part of it. The
    // route evaluates the device state at the draw. An unparsable value keeps
    // the default and is reported as source=default. The line is emitted at
    // startup whether or not the variable is set: the default-on arm leaves
    // startup evidence of its threshold.
    fade_route_threshold=500;
    bool fade_route_from_env=false;
    {const DWORD length=GetEnvironmentVariableW(L"X3M_FADE_ROUTE",setting,32);
     if(length>0&&length<32){
        if(!wcscmp(setting,L"off")){fade_route_threshold=x3m::fade_route::threshold_off;fade_route_from_env=true;}
        else{bool digits=true;for(DWORD i=0;i<length;++i)digits=digits&&setting[i]>=L'0'&&setting[i]<=L'9';
             const unsigned long n=digits?wcstoul(setting,nullptr,10):1001ul;
             if(digits&&n<=1000ul){fade_route_threshold=unsigned(n);fade_route_from_env=true;}}}}
    log("fade_route_mode threshold=%u hysteresis=%u enabled=%u taa=%u hdr=%u linear_materials=%u source=%s",
        fade_route_threshold,unsigned(x3m::fade_route::Hysteresis::band),
        unsigned(fade_route_threshold<=1000u&&taa_requested&&hdr_requested),
        unsigned(taa_requested),unsigned(hdr_requested),unsigned(linear_material_requested),
        fade_route_from_env?"env":"default");
    // X3M_SCREEN_EMISSION=1: the packed screen bracket (policy 8) for the
    // nine SM1 screen pairs; the same HDR/TAA prerequisites as the additive
    // emission route (the pass composes into the AgX FP16 scene, whose
    // encoding is the native one with or without linear hulls:
    // docs/architecture/linear-material-decoupling.md). Default off.
    {const bool asked=GetEnvironmentVariableW(L"X3M_SCREEN_EMISSION",setting,32)==1 && setting[0]==L'1';
     const bool screen_hdr=material_decode_valid && material_tonemap_valid && motion_output_requested && hdr_requested
        && hdr_config.tonemap==x3m::renderer::HdrTonemap::Agx && hdr_config.decode==x3::temporal::AgxDecode::gamma22;
     // The bound comes from the ownership Unlock scan (loader.cpp): without
     // X3M_OWNERSHIP=1 there is no locked prefix, so the option is refused
     // here like its other prerequisites instead of admitting nothing silently.
     const bool screen_ownership=GetEnvironmentVariableW(L"X3M_OWNERSHIP",setting,32)==1 && setting[0]==L'1';
     screen_emission_requested=asked && screen_hdr && taa_requested && screen_ownership;
     // X3M_SCREEN_EMISSION_GAIN: the step E gain g (default 1, native by
     // construction); unparsable or outside [0.5, 8] keeps 1 and logs.
     screen_emission_gain=1.f;bool gain_valid=true;
     SetLastError(ERROR_SUCCESS);
     const DWORD gain_length=GetEnvironmentVariableW(L"X3M_SCREEN_EMISSION_GAIN",setting,32);
     if(gain_length||GetLastError()!=ERROR_ENVVAR_NOT_FOUND){
         wchar_t* end=nullptr;const float value=gain_length&&gain_length<32?wcstof(setting,&end):0.f;
         if(gain_length&&gain_length<32&&end!=setting&&!*end&&std::isfinite(value)&&value>=.5f&&value<=8.f)screen_emission_gain=value;else gain_valid=false;}
     if(asked)log("screen_emission_mode requested=1 enabled=%u hdr=%u taa=%u ownership=%u materials=%u policy=8 gain=%g gain_valid=%u",screen_emission_requested,screen_hdr,taa_requested,screen_ownership,linear_material_requested,double(screen_emission_gain),unsigned(gain_valid));}
    // X3M_EMISSION_SOURCE_GAIN=<g>: source-only encoded gain of the twenty
    // PS2 emission pairs (engine glow, gate, impact, muzzle and explosion
    // sprites alike; docs/architecture/linear-emission-cost.md, "Implemented"
    // and "Screen substitution"): finite 1..8, 1 (the launcher default) is
    // off. Needs the FP16 scene (X3M_HDR=1, which itself needs
    // X3M_MOTION_OUTPUT=1: the shader registration and state shadow live
    // there); no linear-material, linear-emission, TAA or ownership
    // prerequisite. Unparsable or out of range keeps 1 and logs.
    {emission_source_gain=1.f;bool gain_valid=true;float value=1.f;
     SetLastError(ERROR_SUCCESS);
     const DWORD gain_length=GetEnvironmentVariableW(L"X3M_EMISSION_SOURCE_GAIN",setting,32);
     if(gain_length||GetLastError()!=ERROR_ENVVAR_NOT_FOUND){
         wchar_t* end=nullptr;value=gain_length&&gain_length<32?wcstof(setting,&end):0.f;
         if(gain_length&&gain_length<32&&end!=setting&&!*end&&std::isfinite(value)&&value>=1.f&&value<=8.f)emission_source_gain=value;else gain_valid=false;}
     // Exclusive with the linear emission route: its bracket carries its own
     // gain (X3M_EMISSION_GAIN) for the same pairs; the launcher rejects the
     // pair of options, the DLL refuses with the reason logged.
     const bool excluded=linear_emission_requested;
     if(!hdr_requested||excluded)emission_source_gain=1.f;
     if(!gain_valid||value!=1.f)log("emission_source_gain_mode requested=1 enabled=%u hdr=%u linear_emissions=%u gain=%g gain_valid=%u%s",emission_source_gain!=1.f,hdr_requested,unsigned(excluded),double(emission_source_gain),unsigned(gain_valid),excluded?" refused=linear_emissions":"");}
    // X3M_HULL_EMISSION_GAIN=<g>: a source gain over the twelve hull programs'
    // ADD ONE/ONE emitter draws (emitter plan phase 3; the launcher passes
    // --hull-emission-gain G, or the effects gain's value under
    // --hull-emitters): finite 1..8, 1 (the launcher default) is off. Needs
    // the FP16 scene only (X3M_HDR=1), independent of the effects gain so the
    // population can be bracketed alone, with its own key (Ctrl+Shift+F4);
    // the DLL refuses without HDR with the reason logged. Unparsable or out
    // of range keeps 1 and logs.
    {hull_emission_gain=1.f;bool gain_valid=true;float value=1.f;
     SetLastError(ERROR_SUCCESS);
     const DWORD gain_length=GetEnvironmentVariableW(L"X3M_HULL_EMISSION_GAIN",setting,32);
     if(gain_length||GetLastError()!=ERROR_ENVVAR_NOT_FOUND){
         wchar_t* end=nullptr;value=gain_length&&gain_length<32?wcstof(setting,&end):0.f;
         if(gain_length&&gain_length<32&&end!=setting&&!*end&&std::isfinite(value)&&value>=1.f&&value<=8.f)hull_emission_gain=value;else gain_valid=false;}
     const bool excluded=!hdr_requested;
     if(excluded)hull_emission_gain=1.f;
     if(!gain_valid||value!=1.f)log("hull_emission_gain_mode requested=1 enabled=%u source_gain=%g gain=%g gain_valid=%u%s",hull_emission_gain!=1.f,double(emission_source_gain),double(hull_emission_gain),unsigned(gain_valid),excluded?" refused=hdr":"");}
    // X3M_ORIGINAL_FILL=<k>: fill in linear light inside the ORIGINAL hull
    // pixel programs (docs/architecture/original-shading-critique.md 1a,
    // option C): finite 0..0.5, 0 (the launcher default) is off. The variant
    // is the ordinary motion/depth program plus the fill block, so it needs
    // the motion-output registry and the FP16 scene (X3M_HDR=1); no TAA,
    // ownership or tonemap prerequisite. Unparsable or out of range keeps 0
    // and logs.
    {original_fill=0.f;bool fill_valid=true;float value=0.f;
     SetLastError(ERROR_SUCCESS);
     const DWORD fill_length=GetEnvironmentVariableW(L"X3M_ORIGINAL_FILL",setting,32);
     if(fill_length||GetLastError()!=ERROR_ENVVAR_NOT_FOUND){
         wchar_t* end=nullptr;value=fill_length&&fill_length<32?wcstof(setting,&end):0.f;
         if(fill_length&&fill_length<32&&end!=setting&&!*end&&std::isfinite(value)&&value>=0.f&&value<=.5f)original_fill=value;else fill_valid=false;}
     // Exclusive with the linear-material route: its converted programs carry
     // their own fill (X3M_MATERIAL_FILL); the launcher rejects the pair of
     // options, the DLL refuses with the reason logged.
     const bool excluded=linear_material_requested;
     if(!hdr_requested||excluded)original_fill=0.f;
     if(!fill_valid||value!=0.f)log("original_fill_mode requested=1 enabled=%u hdr=%u linear_materials=%u fill=%g fill_valid=%u%s",original_fill!=0.f,hdr_requested,unsigned(excluded),double(original_fill),unsigned(fill_valid),excluded?" refused=linear_materials":"");}
    // X3M_HULL_LIGHTMAP_GAIN=<g>: a gain on the light-map (self-illumination)
    // term inside the ORIGINAL hull pixel programs (station windows and hull
    // lights; docs/reverse-engineering/hull-self-illumination.md 5): finite
    // 1..8, 1 (the launcher default) is off. The variant is the fill/motion
    // program plus one MUL, so it needs the motion-output registry and the
    // FP16 scene (X3M_HDR=1) only; exclusive with the linear-material route,
    // whose converted programs carry X3M_LIGHTMAP_EMISSIVE_GAIN. Ctrl+Shift+F4
    // switches this gain alone (the guide lights follow the effects gain on
    // Ctrl+Shift+F6). Unparsable or out of range keeps 1 and logs.
    {hull_lightmap_gain=1.f;bool gain_valid=true;float value=1.f;
     SetLastError(ERROR_SUCCESS);
     const DWORD gain_length=GetEnvironmentVariableW(L"X3M_HULL_LIGHTMAP_GAIN",setting,32);
     if(gain_length||GetLastError()!=ERROR_ENVVAR_NOT_FOUND){
         wchar_t* end=nullptr;value=gain_length&&gain_length<32?wcstof(setting,&end):0.f;
         if(gain_length&&gain_length<32&&end!=setting&&!*end&&std::isfinite(value)&&value>=1.f&&value<=8.f)hull_lightmap_gain=value;else gain_valid=false;}
     const bool excluded=linear_material_requested;
     if(!hdr_requested||excluded)hull_lightmap_gain=1.f;
     if(!gain_valid||value!=1.f)log("hull_lightmap_gain_mode requested=1 enabled=%u hdr=%u linear_materials=%u gain=%g gain_valid=%u%s",hull_lightmap_gain!=1.f,hdr_requested,unsigned(excluded),double(hull_lightmap_gain),unsigned(gain_valid),excluded?" refused=linear_materials":!hdr_requested?" refused=hdr":"");}
    // X3M_LIGHT_MAP_FAR_FADE=P0,P1[,G] (taa-distant-line-fade.md section 11):
    // the gain above fades to G (default 1, the game's own brightness; 0..gain)
    // as the draw's pixel footprint grows from P0 to P1 world units per pixel
    // (finite, 0 < P0 < P1). Unset or empty = off; malformed, out of range or
    // no gain to fade keeps it off and logs.
    {lightmap_far_fade_requested=false;lightmap_far_fade[0]=lightmap_far_fade[1]=0.f;lightmap_far_fade[2]=1.f;
     wchar_t fade_setting[96]{};
     const DWORD fade_length=GetEnvironmentVariableW(L"X3M_LIGHT_MAP_FAR_FADE",fade_setting,96);
     if(fade_length){
         float parsed[3]={0.f,0.f,1.f};unsigned count=0;bool valid=fade_length<96;
         const wchar_t* at=fade_setting;
         while(valid&&count<3){
             wchar_t* end=nullptr;parsed[count]=wcstof(at,&end);
             if(end==at||!std::isfinite(parsed[count])){valid=false;break;}
             ++count;if(!*end)break;
             if(*end!=L','||count==3){valid=false;break;}
             at=end+1;}
         valid=valid&&count>=2&&parsed[0]>0.f&&parsed[1]>parsed[0]&&parsed[1]<=1e6f&&parsed[2]>=0.f&&parsed[2]<=hull_lightmap_gain;
         const bool gained=hull_lightmap_gain!=1.f;
         lightmap_far_fade_requested=valid&&gained;
         if(lightmap_far_fade_requested){lightmap_far_fade[0]=parsed[0];lightmap_far_fade[1]=parsed[1];lightmap_far_fade[2]=parsed[2];
             camera_state::request_consumer();} // the footprint's P[0]: armed only for an accepted value
         log("light_map_far_fade_mode requested=1 enabled=%u valid=%u p0=%g p1=%g floor=%g gain=%g%s",unsigned(lightmap_far_fade_requested),unsigned(valid),
             double(parsed[0]),double(parsed[1]),double(parsed[2]),double(hull_lightmap_gain),valid&&!gained?" refused=no_gain":"");}}
    // X3M_HULL_EMISSIVE_WIDENING=K,Q0,Q1 (docs/architecture/hull-emissive-widening.md):
    // the gained hull variants' light-map fetch widens to a k x k pixel
    // footprint (texldd with the pixel's own gradients times k), k ramping
    // from 1 at footprint Q0 to K at Q1 world units per pixel (finite,
    // 1 < K <= 8, 0 <= Q0 < Q1 <= 1e6). Unset or empty = off; malformed, out of
    // range or no gain to widen keeps it off and logs.
    {hull_emissive_widening_requested=false;hull_emissive_widening[0]=1.f;hull_emissive_widening[1]=hull_emissive_widening[2]=0.f;
     wchar_t widen_setting[96]{};
     const DWORD widen_length=GetEnvironmentVariableW(L"X3M_HULL_EMISSIVE_WIDENING",widen_setting,96);
     if(widen_length){
         float parsed[3]={1.f,0.f,0.f};unsigned count=0;bool valid=widen_length<96;
         const wchar_t* at=widen_setting;
         while(valid&&count<3){
             wchar_t* end=nullptr;parsed[count]=wcstof(at,&end);
             if(end==at||!std::isfinite(parsed[count])){valid=false;break;}
             ++count;if(!*end)break;
             if(*end!=L','||count==3){valid=false;break;}
             at=end+1;}
         valid=valid&&count==3&&parsed[0]>1.f&&parsed[0]<=8.f&&parsed[1]>=0.f&&parsed[2]>parsed[1]&&parsed[2]<=1e6f;
         const bool gained=hull_lightmap_gain!=1.f;
         hull_emissive_widening_requested=valid&&gained;
         if(hull_emissive_widening_requested){hull_emissive_widening[0]=parsed[0];hull_emissive_widening[1]=parsed[1];hull_emissive_widening[2]=parsed[2];
             camera_state::request_consumer();} // the footprint's P[0]: armed only for an accepted value
         log("hull_emissive_widening_mode requested=1 enabled=%u valid=%u k=%g q0=%g q1=%g gain=%g%s",unsigned(hull_emissive_widening_requested),unsigned(valid),
             double(parsed[0]),double(parsed[1]),double(parsed[2]),double(hull_lightmap_gain),valid&&!gained?" refused=no_gain":"");}}
    // X3M_SCREEN_EMISSION_ADDITIVE=G (finite 1..8; unset, 0 or invalid = off):
    // the additive option of the same nine screen pairs, drawn in place with
    // DESTBLEND ONE and a colour gain G into the FP16 target. Needs the
    // motion-output hooks and X3M_HDR=1 only (no TAA, ownership or linear
    // materials: no bound, no bracket); exclusive with X3M_SCREEN_EMISSION=1,
    // which wins here as the launcher already refuses the combination.
    {screen_emission_additive_requested=false;screen_emission_additive_gain=1.f;
     screen_emission_additive_alpha_requested=false;screen_emission_additive_alpha=1.f;wchar_t alpha_setting[32]{};
     SetLastError(ERROR_SUCCESS);
     const DWORD length=GetEnvironmentVariableW(L"X3M_SCREEN_EMISSION_ADDITIVE",setting,32);
     wchar_t* end=nullptr;const float value=length&&length<32?wcstof(setting,&end):0.f;
     const bool parsed=length&&length<32&&end!=setting&&!*end;
     if(length&&length<32&&!(parsed&&value==0.f)){ // "0" / "0.0" is the explicit off value: silent
         const bool valid=parsed&&std::isfinite(value)&&value>=1.f&&value<=8.f;
         const bool conflict=screen_emission_requested;
         screen_emission_additive_requested=valid&&motion_output_requested&&hdr_requested&&!conflict;
         if(valid)screen_emission_additive_gain=value;
         // X3M_SCREEN_EMISSION_ADDITIVE_ALPHA=K (finite 0..1): the admitted
         // additive draw writes k*a + D.a to the scene alpha the bloom extract
         // weighs by, leaving the colour law G*q + D and every other emitter's
         // authored alpha alone (bloom-per-source-attenuation.md, option 1).
         // Absent or unparsable keeps the native law; it needs the option.
         SetLastError(ERROR_SUCCESS);
         wchar_t* alpha_end=nullptr;
         const DWORD alpha_length=GetEnvironmentVariableW(L"X3M_SCREEN_EMISSION_ADDITIVE_ALPHA",alpha_setting,32);
         const bool alpha_present=alpha_length||GetLastError()!=ERROR_ENVVAR_NOT_FOUND;
         const float alpha_value=alpha_length&&alpha_length<32?wcstof(alpha_setting,&alpha_end):-1.f;
         const bool alpha_valid=alpha_length&&alpha_length<32&&alpha_end!=alpha_setting&&!*alpha_end
             &&std::isfinite(alpha_value)&&alpha_value>=0.f&&alpha_value<=1.f;
         screen_emission_additive_alpha_requested=alpha_valid&&screen_emission_additive_requested;
         screen_emission_additive_alpha=screen_emission_additive_alpha_requested?alpha_value:1.f;
         char alpha_text[24];
         if(screen_emission_additive_alpha_requested)std::snprintf(alpha_text,sizeof alpha_text,"%g",double(screen_emission_additive_alpha));
         else std::snprintf(alpha_text,sizeof alpha_text,"native");
         log("screen_emission_additive_mode requested=1 enabled=%u gain=%g gain_valid=%u motion=%u hdr=%u packed_conflict=%u alpha=%s alpha_requested=%u alpha_valid=%u",
             screen_emission_additive_requested,double(screen_emission_additive_gain),unsigned(valid),motion_output_requested,hdr_requested,unsigned(conflict),
             alpha_text,unsigned(alpha_present),unsigned(alpha_valid));}}
    // X3M_SCREEN_EMISSION_TIMING=1: the option's opt-in per-frame timing
    // diagnostic (one screen_emission_frame line per Present). Needs the
    // enabled option; the option itself stays free of per-frame logging.
    {const bool asked=GetEnvironmentVariableW(L"X3M_SCREEN_EMISSION_TIMING",setting,32)==1 && setting[0]==L'1';
     screen_emission_timing_requested=asked && screen_emission_requested;
     if(asked)log("screen_emission_timing_mode requested=1 enabled=%u screen=%u",screen_emission_timing_requested,screen_emission_requested);}
    // X3M_FADE_WITNESS=<k> (1..100000): every k-th frame the fade-region
    // witness reads the M coverage target back once (default off; needs the
    // distance-fade route; docs/architecture/linear-distance-fade-region.md).
    fade_witness_frames=0;
    {const DWORD length=GetEnvironmentVariableW(L"X3M_FADE_WITNESS",setting,32);
     if(length>0&&length<32){bool digits=true;for(DWORD i=0;i<length;++i)digits=digits&&setting[i]>=L'0'&&setting[i]<=L'9';
        const unsigned long n=digits?wcstoul(setting,nullptr,10):0ul;if(digits&&n>=1&&n<=100000)fade_witness_frames=unsigned(n);
        log("fade_witness_mode requested=%lu digits=%u enabled=%u fade=%u screen=%u",n,digits,fade_witness_frames&&(linear_distance_fade_requested||screen_emission_requested),linear_distance_fade_requested,screen_emission_requested);}
     else if(length)log("fade_witness_mode requested=overlong enabled=0 fade=%u screen=%u",linear_distance_fade_requested,screen_emission_requested);}
    // X3M_SHIMMER_TRACE=1: per-frame distant-shimmer diagnostic (off by
    // default; needs the motion route and TAA; no other behaviour changes).
    {const bool asked=GetEnvironmentVariableW(L"X3M_SHIMMER_TRACE",setting,32)==1 && setting[0]==L'1';
     shimmer_trace_requested=asked && motion_output_requested && taa_requested;
     if(asked)log("shimmer_trace_mode requested=1 enabled=%u motion_output=%u taa=%u",shimmer_trace_requested,motion_output_requested,taa_requested);}
    bloom_requested=GetEnvironmentVariableW(L"X3M_HDR_BLOOM",setting,32)==1 && setting[0]==L'1';
    // X3M_BLOOM_SOURCE_CLAMP=C (finite, >0, at most 64): decoded-space ceiling
    // on the bloom extraction source only. Needs the bloom replacement; absent,
    // unparsable or out of range keeps today's unbounded feed.
    {bloom_source_clamp=x3::temporal::kAgxClampOff;
     SetLastError(ERROR_SUCCESS);
     const DWORD length=GetEnvironmentVariableW(L"X3M_BLOOM_SOURCE_CLAMP",setting,32);
     const bool present=length||GetLastError()!=ERROR_ENVVAR_NOT_FOUND;
     wchar_t* end=nullptr;
     const float value=length&&length<32?wcstof(setting,&end):0.f;
     const bool valid=length&&length<32&&end!=setting&&!*end&&std::isfinite(value)&&value>0.f&&value<=64.f;
     if(valid&&bloom_requested)bloom_source_clamp=value;
     // Always one line, so a log audit separates "variable absent" from "the
     // option was never built into this DLL".
     log("bloom_source_clamp_mode requested=%u enabled=%u clamp=%g clamp_valid=%u bloom=%u",
         unsigned(present),unsigned(bloom_source_clamp<x3::temporal::kAgxClampOff),
         double(value),unsigned(valid),unsigned(bloom_requested));}
    // X3M_AMBIENT_OCCLUSION=1: the AO chain at the scene end (needs the route
    // and the resolve, which integrates the rotated noise). The whole radius and
    // strength strings must parse; out of range keeps the default.
    // A value that does not fit the buffer (GetEnvironmentVariableW returns the
    // required size, >= 32) is invalid for every AO variable.
    {const auto ao_env=[&](const wchar_t* name){const DWORD n=GetEnvironmentVariableW(name,setting,32);return n>0&&n<32?n:0ul;};
     const bool asked=ao_env(L"X3M_AMBIENT_OCCLUSION")==1 && setting[0]==L'1';
     ambient_occlusion_requested=asked && motion_output_requested && taa_requested;
     ambient_occlusion_radius=2.f;ambient_occlusion_strength=.5f;
     if(ao_env(L"X3M_AO_RADIUS")){wchar_t* end=nullptr;const float v=wcstof(setting,&end);if(end!=setting&&*end==L'\0'&&v>=.1f&&v<=100.f)ambient_occlusion_radius=v;}
     if(ao_env(L"X3M_AO_STRENGTH")){wchar_t* end=nullptr;const float v=wcstof(setting,&end);if(end!=setting&&*end==L'\0'&&v>=0.f&&v<=1.f)ambient_occlusion_strength=v;}
     ambient_occlusion_debug=ambient_occlusion_requested && ao_env(L"X3M_AO_DEBUG")==1 && setting[0]==L'1';
     ambient_occlusion_timing=ambient_occlusion_requested && (ambient_occlusion_debug || (ao_env(L"X3M_AO_TIMING")==1 && setting[0]==L'1'));
     if(asked)log("ambient_occlusion_mode requested=1 enabled=%u motion_output=%u taa=%u radius_m=%g strength=%g debug=%u timing=%u",ambient_occlusion_requested,motion_output_requested,taa_requested,double(ambient_occlusion_radius),double(ambient_occlusion_strength),ambient_occlusion_debug,ambient_occlusion_timing);}
    sector_background_requested=GetEnvironmentVariableW(L"X3M_SECTOR_BACKGROUND",setting,32)==1 && setting[0]==L'1';
    // X3M_VOLUMETRIC_FOG=1: the sun-lit medium at the scene end (needs the route and
    // the resolve, which accumulates the jittered march). Whole strings must
    // parse; out of range keeps the default. Strength 0 is a detached pass.
    {const auto fog_env=[&](const wchar_t* name){const DWORD n=GetEnvironmentVariableW(name,setting,32);return n>0&&n<32?n:0ul;};
     const bool asked=fog_env(L"X3M_VOLUMETRIC_FOG")==1 && setting[0]==L'1';
     volumetric_fog_strength=renderer::fog_strength_default;volumetric_fog_anisotropy=renderer::fog_anisotropy_default;
     if(fog_env(L"X3M_VOLUMETRIC_FOG_STRENGTH")){wchar_t* end=nullptr;const float v=wcstof(setting,&end);if(end!=setting&&*end==L'\0'&&v>=renderer::fog_strength_min&&v<=renderer::fog_strength_max)volumetric_fog_strength=v;}
     if(fog_env(L"X3M_VOLUMETRIC_FOG_ANISOTROPY")){wchar_t* end=nullptr;const float v=wcstof(setting,&end);if(end!=setting&&*end==L'\0'&&v>=renderer::fog_anisotropy_min&&v<=renderer::fog_anisotropy_max)volumetric_fog_anisotropy=v;}
     // The launcher's prerequisites (tools/manage.py), so a hand-set environment cannot arm a pass that would
     // skip every frame: the FP16 scene path, the depth replay and a cascade list (validated per device later).
     const bool fog_replay=fog_env(L"X3M_SHADOW_REPLAY_DEPTH")==1 && setting[0]==L'1';
     wchar_t fog_cascades[4]{};const bool fog_cascade_list=GetEnvironmentVariableW(L"X3M_SHADOW_CASCADES",fog_cascades,4)>0;
     volumetric_fog_requested=asked && motion_output_requested && taa_requested && hdr_requested && fog_replay && fog_cascade_list && volumetric_fog_strength>0.f;
     volumetric_fog_everywhere=volumetric_fog_requested && fog_env(L"X3M_VOLUMETRIC_FOG_EVERYWHERE")==1 && setting[0]==L'1';
     volumetric_fog_timing=volumetric_fog_requested && fog_env(L"X3M_VOLUMETRIC_FOG_TIMING")==1 && setting[0]==L'1';
     volumetric_fog_cards_replace=volumetric_fog_requested && fog_env(L"X3M_VOLUMETRIC_FOG_CARDS")==7 && !wcscmp(setting,L"replace");
     volumetric_fog_range_stored=volumetric_fog_requested && fog_env(L"X3M_VOLUMETRIC_FOG_RANGE")==6 && !wcscmp(setting,L"stored");
     volumetric_fog_look=0;volumetric_fog_look_tuning={};
     if(volumetric_fog_range_stored){
        volumetric_fog_look=renderer::fog_look_default; // absent or malformed: the default look
        if(fog_env(L"X3M_VOLUMETRIC_FOG_LOOK")==1&&setting[0]>=L'0'&&setting[0]<L'0'+wchar_t(renderer::fog_look_count))volumetric_fog_look=unsigned(setting[0]-L'0');
        unsigned overrides=0;
        for(const auto& field:renderer::fog_look_fields){
            wchar_t name[48]{};std::swprintf(name,std::size(name),L"X3M_FOG_LOOK_%hs",field.name);
            if(!fog_env(name))continue;
            wchar_t* end=nullptr;const float v=wcstof(setting,&end);
            if(end!=setting&&*end==L'\0')overrides+=renderer::fog_look_set(volumetric_fog_look_tuning,field,v);
        }
        const auto colour=[&](const wchar_t* name,float out[3]){ // r,g,b each 0..4; anything else keeps the derived hue
            if(!fog_env(name))return;
            float rgb[3]{};wchar_t* at=setting;
            for(unsigned i=0;i<3;++i){wchar_t* end=nullptr;rgb[i]=wcstof(at,&end);if(end==at||!(rgb[i]>=0.f&&rgb[i]<=4.f)||*end!=(i<2?L',':L'\0'))return;at=end+1;}
            for(unsigned i=0;i<3;++i)out[i]=rgb[i];
            ++overrides;
        };
        colour(L"X3M_FOG_LOOK_AMBIENT_SUN",volumetric_fog_look_tuning.ambient_sun);colour(L"X3M_FOG_LOOK_AMBIENT_AWAY",volumetric_fog_look_tuning.ambient_away);
        char values[640]{};int used=0; // every tunable by its X3M_FOG_LOOK_<NAME>
        for(const auto& field:renderer::fog_look_fields){
            const int n=std::snprintf(values+used,sizeof values-std::size_t(used)," %s=%g",field.name,double(volumetric_fog_look_tuning.*field.field));
            if(n<0||std::size_t(used+n)>=sizeof values)break;
            used+=n;
        }
        log("volumetric_fog_look_mode look=%u overrides=%u key=ctrl_alt_f11%s",volumetric_fog_look,overrides,values);
     }
     if(asked)log("volumetric_fog_range mode=%s atlas_bytes=%u levels=2 cpu_bytes=%u upload_budget_bytes=%u upload_rects=%u ramp_frames=%u worker_threads=%u",volumetric_fog_range_stored?"stored":"legacy",
        volumetric_fog_range_stored?unsigned(fog::kAtlasBytes):0u,volumetric_fog_range_stored?unsigned(4*fog::kAtlasBytes):0u,volumetric_fog_range_stored?unsigned(fog::kDefaultUploadBudget):0u,
        volumetric_fog_range_stored?fog::kDefaultUploadRects:0u,volumetric_fog_range_stored?fog::kReadinessRampFrames:0u,unsigned(volumetric_fog_range_stored));
     if(asked)log("volumetric_fog_mode requested=1 enabled=%u motion_output=%u taa=%u hdr=%u shadow_replay_depth=%u shadow_cascades=%u strength=%g density_scale=%g anisotropy=%g everywhere=%u timing=%u cards=%s rule=current_engine_family keys=ctrl_alt_f9,ctrl_alt_f10",volumetric_fog_requested,motion_output_requested,taa_requested,hdr_requested,unsigned(fog_replay),unsigned(fog_cascade_list),double(volumetric_fog_strength),double(volumetric_fog_strength / .02f),double(volumetric_fog_anisotropy),volumetric_fog_everywhere,volumetric_fog_timing,volumetric_fog_cards_replace?"replace":"keep");}
    hdr_config.sharpen=taa_sharpen; // the HDR write-back sharpens the resolved image with the same setting
    motion_rt_lazy=GetEnvironmentVariableW(L"X3M_MOTION_RT_MODE",setting,32)>0 && !wcscmp(setting,L"lazy");
    if(GetEnvironmentVariableW(L"X3M_STATE_SHADOW",setting,32)>0){ // exactly "1" or "0"; anything else is auto, noted
        if(!wcscmp(setting,L"1"))motion_state_shadow=1;
        else if(!wcscmp(setting,L"0"))motion_state_shadow=0;
        else log("state_shadow_setting ignored=1 length=%u mode=auto",unsigned(wcslen(setting)));
    }
    const bool scene_hook_requested=scene_hook::wanted(); // default on with the route (X3M_SCENE_HOOK=0 turns it off)
    if(GetEnvironmentVariableW(L"X3M_MOTION_FRAME_LOG",setting,32)>0){const unsigned long n=wcstoul(setting,nullptr,10);if(n>=1&&n<=100000)motion_frame_log=unsigned(n);}
    if(GetEnvironmentVariableW(L"X3M_TAA_SENTINEL",setting,32)>0){
        if(!wcscmp(setting,L"1"))taa_sentinel_mode=x3m::renderer::SentinelMode::CurrentOnly;
        else if(!wcscmp(setting,L"2"))taa_sentinel_mode=x3m::renderer::SentinelMode::Camera;
        else taa_sentinel_mode=x3m::renderer::SentinelMode::Auto;
    }
    if(GetEnvironmentVariableW(L"X3M_TAA_UNMATCHED_STATIC",setting,32)>0){
        if(!wcscmp(setting,L"node"))taa_unmatched_static=1;
        else if(!wcscmp(setting,L"all"))taa_unmatched_static=2;
        else if(wcscmp(setting,L"0")!=0&&wcscmp(setting,L"off")!=0)log("taa_unmatched_static_setting invalid=1");
    }
    else if(taa_requested)taa_unmatched_static=1; // run212: node is the default with the TAA route (an explicit "off"/"0" opts out)
    if(GetEnvironmentVariableW(L"X3M_CAMERA_CUT_DEG",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>0&&v<=180)camera_cut_degrees=v;}
    if(GetEnvironmentVariableW(L"X3M_CAMERA_LOG",setting,32)>0){const unsigned long n=wcstoul(setting,nullptr,10);if(n>=1&&n<=1000000)camera_log_frames=unsigned(n);}
    log("motion_output_mode requested=%u scope=live_same_draw_diagnostic history_requires=object_trace,object_lifetime temporal_consumer=%u taa=%u taa_debug=%u jitter=%u jitter_samples=%u cut_median_px=%.3f cut_missing=%.3f rt_mode=%s frame_log=%u sentinel=%s unmatched_static=%u sentinel_stabiliser=%.3f sentinel_emitter=%.3f camera_cut_deg=%.2f camera_log=%u state_shadow=%s scene_hook=%u hdr=%u taa_k=%.5f mip_bias=%g taa_sharpen=%.3f taa_current_filter=%.3f taa_history_weight=%.3f",
        motion_output_requested,taa_requested,taa_requested,taa_debug_requested,motion_jitter_requested,motion_jitter_samples,motion_cut_median_px,motion_cut_missing,motion_rt_lazy?"lazy":"perdraw",motion_frame_log,
        taa_sentinel_mode==x3m::renderer::SentinelMode::CurrentOnly?"1":taa_sentinel_mode==x3m::renderer::SentinelMode::Camera?"2":"auto",taa_unmatched_static,double(taa_sentinel[0]),double(taa_sentinel[1]),camera_cut_degrees,camera_log_frames,motion_state_shadow<0?"auto":motion_state_shadow?"1":"0",scene_hook_requested,hdr_requested,taa_k_override,double(taa_mip_bias),taa_sharpen,double(taa_current_filter),double(taa_history_weight));
    log("x3-modern-renderer version=0.4 schema=2 capture_start=%u capture_frames=%u pointer_bits=32",capture_start,capture_count);
    telemetry::initialize([]{if(logfile)fflush(logfile);});
    game_phases::initialize(); // all 33 claims here, before the first Present
    frame_phases::initialize(); // X3M_FRAME_PHASES=1 only: ten render-routine stamps through the game-phase stub, same window
    pass_phases::initialize(); // X3M_PASS_PHASES=1 only: four effect-pass stamps through the lean stub, needs the frame group, same window
    residual_phases::initialize(); // X3M_RESIDUAL_PHASES=1 only: two residual stamps through the lean stub, needs the frame and pass groups, same window
    light_phases::initialize(); // opt-in R7 whole-call timer
    submit_phases::initialize(); // X3M_SUBMIT_PHASES=1 only: twenty-two view_submit candidate stamps through the context lean stub, needs the frame group, same window
    loop_phases::initialize(); // X3M_LOOP_PHASES=1 only: six per-sector update stamps through the lean stub, needs the frame group, same window
    media_cue::initialize(); // default ID2 skip plus optional trace/cache: one verified allocator gate
    if(const auto observer=media_cue::video_lock_observer()){ // trace on: the surface shell's lock witness (needs --ownership to see the game's surfaces)
        ownership::set_surface_lock_observer(observer);
        log("media_video_witness registered=1 blit_range=%08lx-%08lx interval=%u",static_cast<unsigned long>(media_cue::sites::kVideoBlitBegin),static_cast<unsigned long>(media_cue::sites::kVideoBlitEnd),media_cue::detail::video_blit_line_interval);
    }
    voice_dmo_fallback::initialize(); // X3M_VOICE_DMO_FALLBACK=1 only; one claim, same window
    frame_timing::initialize(); // X3M_FRAME_TIMING=1 only; one environment read, no allocation afterwards
    lod_scale::initialize(); // X3M_LOD_SCALE=<factor> only; same-length FMUL replacement, same window
    point_light_admission::initialize(); // X3M_POINT_LIGHT_ROOT_ADMISSION=1 only; six-byte JG site at 0x004c27af, same window
    collide_box_cull::initialize(); // X3M_COLLIDE_BOX_CULL=1 only; two box early-out trampolines on the sector collision pair tests (0x0045d58e, 0x0045cc7c), same window
    collide_narrow_census::initialize(); // X3M_COLLIDE_NARROW_CENSUS=1 only; narrow-phase census: two call redirects (0x0045d665, 0x0048a9a5) and one entry trampoline (0x004e2530), same window, disjoint from the box-cull claims
    collide_sat_sse2::initialize(); // X3M_COLLIDE_SAT_SSE2=1 only; the sole call of the OBB separating-axis test 0x004e3280 (at 0x004e25a3) redirected to an SSE2 reimplementation, same window, disjoint from every other claim
    collide_memo::initialize(); // X3M_COLLIDE_MEMO=1 only; the sole call of 0x004e29f0 (at 0x0047f329, inside the mesh-pair query) redirected to the no-contact memo's thunk, same window, after the census and the SAT so the bytes it hashes are settled
    // X3M_SUN_OCCLUSION=1 / X3M_SUN_OCCLUSION_LOG=1 only (docs/architecture/sun-partial-occlusion.md): the flare probe's call
    // 0x00471630 and the lens traversal's call 0x00472491, same window, disjoint from every other claim except
    // X3M_SUBMIT_PHASES' stamp at 0x00472490 (refused by name). The override needs the route's RT2 (X3M_MOTION_OUTPUT=1).
    {
        wchar_t value[32]{};
        if(GetEnvironmentVariableW(L"X3M_SUN_OCCLUSION_RADIUS",value,32)>0){wchar_t* end=nullptr;const float v=wcstof(value,&end);if(end!=value&&*end==L'\0'&&v>=sun_occlusion::core::radius_option_min_u&&v<=sun_occlusion::core::radius_option_max_u)sun_occlusion_radius=v;}
        if(GetEnvironmentVariableW(L"X3M_SUN_OCCLUSION_CURVE",value,32)>0){wchar_t* end=nullptr;const float v=wcstof(value,&end);if(end!=value&&*end==L'\0'&&v>=.25f&&v<=4.f)sun_occlusion_curve=v;}
        sun_occlusion_core_f=GetEnvironmentVariableW(L"X3M_SUN_OCCLUSION_CORE_F",value,32)==1&&value[0]==L'1';
        sun_occlusion::set_listener(&sun_lens_begin,&sun_lens_end);
        if(sun_occlusion::initialize())log("sun_occlusion_config override=%u log=%u route=%u radius_u=%.4f curve=%.3f core_f=%u",sun_occlusion::override_enabled()?1u:0u,sun_occlusion::logging()?1u:0u,motion_output_requested?1u:0u,double(sun_occlusion_radius),double(sun_occlusion_curve),sun_occlusion_core_f?1u:0u);
    }
    cull_census::initialize(); // X3M_CULL_CENSUS=1 only; two read-only trampolines on the cull/LOD pass (0x0047d258, 0x0047d528), same window
    cull_small_parts::initialize(); // X3M_CULL_SMALL_PARTS_PX only; one trampoline on the cull/LOD pass (0x0047d2a2), same window, disjoint from the census claims
    if(telemetry::enabled()||gz_buffer::requested()||crypt_cache::requested())loading_trace::initialize(); // X3M_GZ_BUFFER=1 / X3M_CRYPT_CACHE=1 patch their rows alone
    resource_reader::initialize(); // X3M_RESOURCE_READ=verify|fast, X3M_DAT_HANDLES=1; after the probes so its stub chains behind theirs
    sampling_profiler::initialize(); // X3M_PROFILE=1 only; outside loader lock, after the log exists
}
const wchar_t* capture_directory() { return directory.c_str(); }
// engine_memory phase=create|summary: the reader's counters from a guarded copy
// of its statistics (hits = validated reads answered from the region cache
// without a VirtualQuery). Validated direct reads are the only path since
// 2026-09-22. No per-draw work: called at device creation and by the telemetry
// summary.
void engine_memory_line(const char* phase,unsigned long long device,unsigned long long frame) {
    const auto s=engine_memory::stats();
    const unsigned long long reads=s.reads,queries=s.queries;
    log("engine_memory phase=%s device=%llu path=direct reads=%llu queries=%llu hits=%llu rejected=%llu frame=%llu",
        phase,static_cast<unsigned long long>(device),
        reads,queries,reads>=queries?reads-queries:0ull,static_cast<unsigned long long>(s.rejected),
        static_cast<unsigned long long>(frame?frame:s.frame));
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
// Fixture-only exports (verification/probe/motion_output_fixture.cpp). Absent
// from production builds; the seam DLL is linked by build_motion_output.sh.
namespace { MotionOutputFixtureConfig fixture_config{}; bool fixture_configured=false; unsigned fixture_hdr_fault_kind=0, fixture_hdr_fault_count=0; }
void fixture_apply(Device& ctx) {
    if(fixture_configured) {
        ctx.motion_output.fixture_configure(fixture_config);
        ctx.fixture_observe_native_wrap = fixture_config.observe_native_wrap != 0;
    }
    if(fixture_hdr_fault_count){ctx.motion_output.fixture_hdr_fault(fixture_hdr_fault_kind,fixture_hdr_fault_count);fixture_hdr_fault_count=0;}
}
#endif

// The engine scene-end signal: render thread, outside any device hook; every
// route sees it under the same mutex the hooks hold.
const X3mCompositorBinding* compositor_binding() noexcept {
    static const X3mCompositorBinding callbacks{nullptr,&compositor_pre,&compositor_post,&compositor_cleanup,nullptr};
    return bloom_requested && motion_output_requested && hdr_requested
        && hdr_config.tonemap==renderer::HdrTonemap::Agx && scene_hook::wanted() ? &callbacks : nullptr;
}
// DllMain DLL_PROCESS_DETACH, before the CRT destroys `devices` (whose ~MotionOutput would
// otherwise join a worker the OS already killed, under the loader lock). Deliberately without
// the capture mutex (a killed thread may hold it) and without logging.
void abandon_fog_density_workers() noexcept {
    for(auto& entry:devices)if(entry.second)entry.second->motion_output.abandon_volumetric_fog_worker();
}
void scene_end_signal() {
    CaptureLock lock;
    frame_timing::Scope timing(frame_timing::Bucket::Scene,"scene_end_signal"); // X3M_FRAME_TIMING only
    for(auto& entry:devices) entry.second->motion_output.scene_end_hook();
}
// Self-preserving: the CRT formatter is x87 code (%g/%f, the MinGW pformat),
// and log() is reachable from the light-envelope hooks (draw capture lines,
// the stream hooks' metadata-failure line). The save/restore is paid only when
// a line is written; call_preserved keeps the formatter out of the audited
// graph (cpu_state.h).
void log(const char* format,...) {
    CaptureLock lock;
    if(!logfile) return;
    va_list args; va_start(args,format);
    call_preserved([&]{vfprintf(logfile,format,args);fputc('\n',logfile);});
    va_end(args);
}
HANDLE log_handle() noexcept { return log_os_handle; }
void hook_direct3d(IDirect3D9* d) {
    CaptureLock lock;
    if(factories.count(d)) return;
    IDirect3D9Ex* ex=nullptr;
    const bool supports_ex=SUCCEEDED(d->QueryInterface(IID_IDirect3D9Ex,reinterpret_cast<void**>(&ex)))
        && static_cast<void*>(ex)==static_cast<void*>(d);
    if(ex) ex->Release();
    auto ctx=std::make_unique<Hooks>(d,supports_ex?22:17);
    ctx->set(2,release_factory); ctx->set(16,create_device);
    auto entry=factories.emplace(d,std::move(ctx));
    entry.first->second->install(d);
    D3DADAPTER_IDENTIFIER9 id{};
    if(SUCCEEDED(d->GetAdapterIdentifier(D3DADAPTER_DEFAULT,0,&id))) log("adapter description=%s driver=%s vendor=%08lx device=%08lx",id.Description,id.Driver,id.VendorId,id.DeviceId);
}
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
extern "C" __declspec(dllexport) void x3m_motion_output_fixture_configure(const x3m::MotionOutputFixtureConfig* config) {
    x3m::CaptureLock lock;
    if(!config||config->size!=sizeof(x3m::MotionOutputFixtureConfig)) return;
    x3m::fixture_config=*config; x3m::fixture_configured=true;
    for(auto& entry:x3m::devices) x3m::fixture_apply(*entry.second);
}
extern "C" __declspec(dllexport) HRESULT x3m_motion_output_fixture_wrap_snapshot(IDirect3DDevice9* device,x3m::MotionOutputFixtureWrapSnapshot* out) {
    x3m::CaptureLock lock;
    const auto it=x3m::devices.find(device);
    if(it==x3m::devices.end() || !out)return D3DERR_INVALIDCALL;
    *out=it->second->fixture_wrap;
    return S_OK;
}
extern "C" __declspec(dllexport) void x3m_linear_emission_fixture_fault(IDirect3DDevice9* device,unsigned kind,unsigned count) {
    x3m::CaptureLock lock;
    const auto it=x3m::devices.find(device);
    if(it!=x3m::devices.end())it->second->motion_output.fixture_emission_fault(kind,count);
}
extern "C" __declspec(dllexport) unsigned x3m_linear_emission_fixture_status(IDirect3DDevice9* device,unsigned key) {
    x3m::CaptureLock lock;
    const auto it=x3m::devices.find(device);
    if(it==x3m::devices.end())return 0;
    if(key==12)return it->second->fixture_emission_source_calls;
    if(key==49)return it->second->fixture_primitive_source_calls;
    return it->second->motion_output.fixture_emission_status(key);
}
extern "C" __declspec(dllexport) HRESULT x3m_motion_output_fixture_readback_target(IDirect3DDevice9* device,unsigned target,float* out,unsigned floats,unsigned* width,unsigned* height) {
    x3m::CaptureLock lock;
    const auto it=x3m::devices.find(device);
    if(it==x3m::devices.end()) return D3DERR_INVALIDCALL;
    return it->second->motion_output.fixture_readback(target,out,floats,width,height);
}
// Compatibility spellings: RT1 (motion, 4 floats per pixel) and RT2 (depth, 1 float per pixel).
extern "C" __declspec(dllexport) HRESULT x3m_motion_output_fixture_readback(IDirect3DDevice9* device,float* out,unsigned floats,unsigned* width,unsigned* height) {
    return x3m_motion_output_fixture_readback_target(device,1,out,floats,width,height);
}
// The fixture executable's own camera pointer slots stand in for the engine
// globals (camera_state::fixture_install); the identity gate is bypassed.
extern "C" __declspec(dllexport) void x3m_camera_state_fixture_install(const float* const* projection_slot,const float* const* view_slot) {
    x3m::CaptureLock lock;
    x3m::camera_state::fixture_install(projection_slot,view_slot);
}
// The fixture executable's own render-context slot stands in for 0x00608518
// (sun_light_poll::fixture_install); the identity gate is bypassed.
extern "C" __declspec(dllexport) void x3m_sun_light_poll_fixture_install(const std::uint32_t* context_slot) {
    x3m::CaptureLock lock;
    x3m::sun_light_poll::fixture_install(context_slot);
}
// Engine scene-end hook seam: the fixture executable's own E8 callsite and
// compositor stand in for 0x004721b1 / 0x004c4750; identity gate bypassed.
extern "C" __declspec(dllexport) int x3m_scene_hook_fixture_install(void* site,void* target) {
    x3m::CaptureLock lock;
    const bool result=x3m::scene_hook::fixture_install(site,target,&x3m::scene_end_signal);
    for(auto& entry:x3m::devices) entry.second->motion_output.configure_scene_hook(x3m::scene_hook::active());
    return result;
}
extern "C" __declspec(dllexport) int x3m_scene_hook_fixture_shutdown() {
    x3m::CaptureLock lock;
    const bool result=x3m::scene_hook::fixture_shutdown();
    for(auto& entry:x3m::devices) entry.second->motion_output.configure_scene_hook(x3m::scene_hook::active());
    return result;
}
// Synthetic owner only: renderer data, bridge, pin cleanup and native device
// hooks are real. The fixture uses its own original function and outer SEH catch.
extern "C" __declspec(dllexport) int x3m_bloom_lifetime_fixture_bind(void (*original)(),
        IDirect3DDevice9* device,IDirect3DTexture9* scene,IDirect3DSurface9* main,IDirect3DSurface9* depth) {
    x3m::CaptureLock lock;
    auto& f=x3m::bloom_lifetime_fixture;
    if(!original || !device || f.bound || x3m_compositor_bridge_active())return 0;
    if(x3m::devices.find(device)==x3m::devices.end()){
        // Production's separate Create9Ex factory currently passes through.
        // Adopt the fixture's genuine Ex device explicitly to exercise the
        // actual 134-slot ResetEx hook; this is not production Ex admission.
        D3DDEVICE_CREATION_PARAMETERS creation{};
        if(FAILED(device->GetCreationParameters(&creation)))return 0;
        x3m::hook_device(device,creation.hFocusWindow,creation.hFocusWindow);
    }
    f={}; f.device=device;f.scene=scene;f.main=main;f.depth=depth;
    f.binding={original,&x3m::bloom_fixture_pre,&x3m::bloom_fixture_post,&x3m::bloom_fixture_cleanup,nullptr};
    f.bound=x3m_compositor_bridge_bind(&f.binding)!=0;
    return f.bound;
}
extern "C" __declspec(dllexport) void* x3m_bloom_lifetime_fixture_entry() {
    return reinterpret_cast<void*>(&x3m_compositor_bridge_entry);
}
extern "C" __declspec(dllexport) int x3m_bloom_lifetime_fixture_unbind() {
    x3m::CaptureLock lock;
    auto& f=x3m::bloom_lifetime_fixture;
    if(!f.bound || x3m_compositor_bridge_active() || !x3m_compositor_bridge_unbind())return 0;
    f.bound=false;return 1;
}
extern "C" __declspec(dllexport) unsigned x3m_bloom_lifetime_fixture_query(unsigned key) {
    x3m::CaptureLock lock;
    auto& f=x3m::bloom_lifetime_fixture;
    if(key==8)return x3m::devices.find(f.device)==x3m::devices.end();
    if(key==9)return f.owner.expired();
    if(key==11)return x3m_compositor_bridge_active();
    return key<17?f.counts[key]:~0u;
}
extern "C" __declspec(dllexport) unsigned x3m_scene_hook_fixture_signals() { return unsigned(x3m::scene_hook::signals()); }
extern "C" __declspec(dllexport) const char* x3m_scene_hook_fixture_status() { return x3m::scene_hook::status(); }
extern "C" __declspec(dllexport) HRESULT x3m_motion_output_fixture_last_pixel_abi(IDirect3DDevice9* device,float* out,unsigned floats) {
    x3m::CaptureLock lock;
    const auto it=x3m::devices.find(device);
    if(it==x3m::devices.end()) return D3DERR_INVALIDCALL;
    return it->second->motion_output.fixture_last_pixel_abi(out,floats);
}
extern "C" __declspec(dllexport) HRESULT x3m_motion_output_fixture_readback_depth(IDirect3DDevice9* device,float* out,unsigned floats,unsigned* width,unsigned* height) {
    return x3m_motion_output_fixture_readback_target(device,2,out,floats,width,height);
}
// Depth replay seam: the private sun-space map as floats plus the last
// replayed frame's basis (16 floats; motion_output_shadow_replay_inc.h).
extern "C" __declspec(dllexport) HRESULT x3m_shadow_replay_fixture_readback(IDirect3DDevice9* device,float* out,unsigned floats,unsigned* width,unsigned* height,float* params,unsigned param_floats) {
    x3m::CaptureLock lock;
    const auto it=x3m::devices.find(device);
    if(it==x3m::devices.end()) return D3DERR_INVALIDCALL;
    return it->second->motion_output.fixture_shadow_replay_readback(out,floats,width,height,params,param_floats);
}
// The same per cascade (shadow-cascades.md): 19 params, the retained basis.
extern "C" __declspec(dllexport) HRESULT x3m_shadow_replay_fixture_cascade_readback(IDirect3DDevice9* device,unsigned cascade,float* out,unsigned floats,unsigned* width,unsigned* height,float* params,unsigned param_floats) {
    x3m::CaptureLock lock;
    const auto it=x3m::devices.find(device);
    if(it==x3m::devices.end()) return D3DERR_INVALIDCALL;
    return it->second->motion_output.fixture_shadow_replay_readback(out,floats,width,height,params,param_floats,cascade);
}
// The at-rest sun-shadow A/B (comparison-hotkeys.md, "Sun shadows at rest"):
// the fixture executable stands in for the Ctrl+Shift+F12 press, which the
// production key path delivers at the same frame boundary. Returns the new
// state (1 on / 0 off), or -1 for an unknown device.
extern "C" __declspec(dllexport) int x3m_sun_shadow_fixture_toggle(IDirect3DDevice9* device) {
    x3m::CaptureLock lock;
    const auto it=x3m::devices.find(device);
    return it==x3m::devices.end()?-1:it->second->motion_output.sun_shadow_toggle();
}
// Own-ship-adaptive cascade 0 seam (shadow-cascade-extents.md, section 5):
// the fixture executable's synthetic own-ship root node stands in for the
// registry walk of the verified executable; the scope nodes of its draws are
// compared against it. Pressed at a frame boundary, before the frame's draws.
extern "C" __declspec(dllexport) int x3m_shadow_own_ship_fixture_install(IDirect3DDevice9* device,std::uintptr_t node,std::uint32_t handle,std::uintptr_t part,std::uint32_t part_handle) {
    x3m::CaptureLock lock;
    const auto it=x3m::devices.find(device);
    if(it==x3m::devices.end())return -1;
    it->second->motion_output.fixture_own_ship(node,handle,part,part_handle);
    return 0;
}
// Caster retention seam (shadow-caster-retention.md): the store's levels and
// counters, and the synthetic lifetime observer (births, retirements, journal).
extern "C" __declspec(dllexport) unsigned x3m_shadow_retention_fixture_stats(IDirect3DDevice9* device,std::uint64_t* out,unsigned count) {
    x3m::CaptureLock lock;
    const auto it=x3m::devices.find(device);
    return it==x3m::devices.end()?0u:it->second->motion_output.fixture_shadow_retention_stats(out,count);
}
extern "C" __declspec(dllexport) void x3m_shadow_retention_fixture_lifetime(unsigned op,std::uint64_t a,std::uint64_t b) {
    x3m::CaptureLock lock;
    if(op==8){ for(auto& entry:x3m::devices) entry.second->motion_output.fixture_shadow_retention_device_lost(); return; } // 8: device loss on every hooked device
    x3m::shadow_retention_fixture_lifetime(op,a,b);
}
// HDR seam: fault injection (renderer::HdrFault kinds, `count` firings; a null
// device queues the fault for every hooked device and the next attach) and
// the FP16 target as floats.
extern "C" __declspec(dllexport) void x3m_hdr_fixture_fault(IDirect3DDevice9* device,unsigned kind,unsigned count) {
    x3m::CaptureLock lock;
    for(auto& entry:x3m::devices) if(!device||entry.first==device) entry.second->motion_output.fixture_hdr_fault(kind,count);
    if(!device){x3m::fixture_hdr_fault_kind=kind;x3m::fixture_hdr_fault_count=count;}
}
extern "C" __declspec(dllexport) HRESULT x3m_hdr_fixture_readback(IDirect3DDevice9* device,float* out,unsigned floats,unsigned* width,unsigned* height) {
    x3m::CaptureLock lock;
    const auto it=x3m::devices.find(device);
    if(it==x3m::devices.end()) return D3DERR_INVALIDCALL;
    return it->second->motion_output.fixture_hdr_readback(out,floats,width,height);
}
// Stage 2: the exposure state (ev, ev_adapted, ev_target, avg_log_l, dt, exposure, steps, k).
extern "C" __declspec(dllexport) HRESULT x3m_hdr_fixture_exposure(IDirect3DDevice9* device,float* out,unsigned floats) {
    x3m::CaptureLock lock;
    const auto it=x3m::devices.find(device);
    if(it==x3m::devices.end()) return D3DERR_INVALIDCALL;
    return it->second->motion_output.fixture_hdr_exposure(out,floats);
}
// The Ctrl+Shift+F11 action without the key: the same toggle the sampler calls.
extern "C" __declspec(dllexport) int x3m_ambient_occlusion_fixture_toggle(IDirect3DDevice9* device) {
    x3m::CaptureLock lock;
    const auto it=x3m::devices.find(device);
    if(it==x3m::devices.end()) return -2;
    return it->second->motion_output.ambient_occlusion_toggle();
}
// The two hull-family actions without their keys: the same toggle the sampler
// calls, lightmap!=0 for Ctrl+Shift+F4 (the light-map gain), 0 for the guide
// lights that Ctrl+Shift+F6 drives beside the effects gain.
extern "C" __declspec(dllexport) int x3m_hull_emission_fixture_toggle(IDirect3DDevice9* device,int lightmap) {
    x3m::CaptureLock lock;
    const auto it=x3m::devices.find(device);
    if(it==x3m::devices.end()) return -2;
    return it->second->motion_output.hull_emission_gain_toggle(lightmap!=0);
}
#endif
