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
    CursorProperties, CursorPosition, CursorShow, Count
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
uint64_t now();
uint64_t frequency();
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
    Scope(State& state,Metric metric):state_(enabled()?&state:nullptr),metric_(metric),begin_(now()){}
    ~Scope(){if(state_)record(*state_,metric_,now()-begin_);}
};
}
