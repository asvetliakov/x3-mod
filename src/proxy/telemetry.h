#pragma once
#include <windows.h>
#include <array>
#include <cstdint>

// Optional CPU telemetry only. Call under capture.cpp's mutex. There are no GPU
// queries, heap-backed event queues, retained COM references, or process hooks.
namespace x3m::telemetry {
enum class Metric : unsigned {
    LockWait, CreateDevice, PresentNormal, PresentCapture, FrameNormal, FrameCapture,
    DrawBackend, CaptureCpu, Snapshot, ShaderVS, ShaderPS, ShaderInspect,
    ShaderGetFunction, ShaderHash, ShaderDump, Texture, CubeTexture, VolumeTexture,
    RenderTarget, DepthStencil, VertexBuffer, IndexBuffer, Reset, LogFlush,
    CursorProperties, CursorPosition, CursorShow,
    // Live motion route and temporal boundary (docs/verification/telemetry.md,
    // "Route and boundary cost"). All CPU-inclusive wall clock, never GPU time.
    StretchBackend,      // the application's own StretchRect backend call
    RouteGate,           // before_draw gate evaluation per scene draw (fill and apply excluded)
    RouteDraw,           // apply + undo around one routed draw (native draw and jitter writes excluded)
    RouteSetRenderTarget,// one route-issued SetRenderTarget (per-draw apply/undo or lazy flush)
    RouteJitter,         // one jitter constant write (the jittered rows or their restoration)
    RouteFill,           // the per-frame sentinel fill (save, quad, restore)
    RouteLazyFlush,      // one lazy-mode restoration of RT1/RT2 and their write masks
    RouteReadback,       // one capture-frame readback to disk (motion, depth, color or resolved)
    TaaRun,              // TemporalPass::run inclusive (nests the five below)
    TaaStateCapture,     // state block Capture plus the binding getters
    TaaCopyColor,        // 8-bit main target to FP16 scratch StretchRect
    TaaCopyDepth,        // R32F depth to depth history StretchRect
    TaaResolveDraw,      // normalize, scene bracket, resolve quad(s)
    TaaStateApply,       // binding restoration plus state block Apply
    TaaCopyBack,         // resolved FP16 to the 8-bit main target StretchRect
    // FP16 HDR scene path, stage 1 (docs/architecture/hdr-scene-path.md).
    HdrRedirect,         // the latching Clear's RT0 substitution (viewport/scissor getters, SetRenderTarget, setters)
    HdrWriteback,        // one write-back through the ladder, inclusive (nests the two below and hdr_bind)
    HdrWritebackDraw,    // the identity copy draw: state save, quad, state restore
    HdrWritebackStretch, // the emergency StretchRect rung (only when the draw failed)
    HdrBind,             // an explicit rebind of RT0 at the end of a write-back (unwind or nothing to write)
    HdrRecheck,          // the recovery self test at a latch while blocked
    // Stage 2 (AgX tonemap and exposure meter).
    HdrMeter,            // the meter chain's draws inside the write-back bracket (auto exposure)
    HdrMeterReadback,    // the lagged 1x1 readback lock and adaptation step at the latch
    Count
};
struct Counter {
    uint64_t count=0, failures=0, total=0, minimum=UINT64_MAX, maximum=0, bytes=0;
    // CPU durations: <=10us, <=100us, <=1ms, <=10ms, <=100ms, >100ms.
    std::array<uint64_t,6> buckets{};
};
struct State {
    std::array<Counter,static_cast<unsigned>(Metric::Count)> counters{};
    uint64_t last_present=0, last_summary=0, last_poll=0, last_position=0;
    uint64_t position_suppressed=0, cursor_changes_suppressed=0, last_cursor_change=0;
    uint64_t device=0, frame=0, resets=0;
    int logged_position_x=0,logged_position_y=0;
    DWORD logged_position_flags=0;
    HWND window=nullptr, focus_window=nullptr;
    HWND present_override=nullptr;
    bool present_override_known=false;
    HWND foreground=nullptr, thread_focus=nullptr;
    HWND gui_active=nullptr,gui_focus=nullptr,gui_capture=nullptr;
    BOOL gui_valid=FALSE,clip_valid=FALSE;
    RECT clip_rect{};
    bool marker_down=false;
    uint64_t markers=0;
    HCURSOR cursor=nullptr;
    DWORD cursor_flags=0;
    POINT point{};
    BOOL iconic=FALSE, visible=FALSE;
    LONG style=0, extended_style=0;
    RECT window_rect{}, client_rect{};
    bool polled=false, had_present=false, last_frame_capture=false;
    bool api_show_known=false; BOOL api_show=FALSE;
    bool properties_known=false; UINT cursor_x=0,cursor_y=0; void* cursor_surface=nullptr;
};
void initialize(void (*flush_log)()=nullptr);
bool enabled();
// X3M_TELEMETRY_DRAW=1 (with X3M_TELEMETRY=1; default off): the per-draw
// metrics draw_backend, route_gate, route_draw, route_set_rt, route_jitter and
// route_lazy_flush. Off, their samples are dropped and the route takes no QPC
// stamp per draw (under Wine each stamp is a syscall; route-cost-run1.md).
bool draw_enabled();
bool enabled(Metric); // enabled(), and draw_enabled() for the per-draw metrics
uint64_t now();
uint64_t frequency();
// Ticks to microseconds with the QPC frequency; 0 while telemetry is disabled.
double microseconds(uint64_t ticks);
void record(State&,Metric,uint64_t ticks,bool failed=false,uint64_t bytes=0);
State& process();
void summary(State&,const char* reason,uint64_t frame);
void present(State&,uint64_t frame,bool captured,uint64_t begin,uint64_t end,HRESULT result);
void poll_window(State&,uint64_t frame);
// Scope records wall-clock CPU-side elapsed time, including any nested work.
// Do not sum overlapping metrics such as ShaderInspect and ShaderHash.
class Scope {
    State* state_; Metric metric_; uint64_t begin_;
public:
    Scope(State& state,Metric metric):state_(enabled(metric)?&state:nullptr),metric_(metric),begin_(state_?now():0){}
    ~Scope(){if(state_)record(*state_,metric_,now()-begin_);}
};
}
