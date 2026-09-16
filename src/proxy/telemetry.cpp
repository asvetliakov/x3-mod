#include "telemetry.h"
#include "capture.h"
#include "loading_trace.h"
#include "game_phases.h"
#include "cpu_state.h"
#include <algorithm>
#include <cstring>

namespace x3m::telemetry {
namespace {
bool active=false, draw_active=false, reporting=false;
void (*flush_output)()=nullptr;
uint64_t clock_frequency=0, startup=0;
// Histogram edges (10 us, 100 us, ... 100 ms) in QPC ticks, computed once at
// initialize: record() runs inside the light-envelope draw hooks
// (cpu_state.h), so the bucketing is integer work, not a double division.
uint64_t bucket_ticks[5]{};
State global;
constexpr const char* names[]={"lock_wait","create_device","present_normal","present_capture","frame_normal","frame_capture","draw_backend","capture_cpu","snapshot","shader_vs_backend","shader_ps_backend","shader_inspect","shader_getfunction","shader_hash","shader_dump","texture","cube_texture","volume_texture","render_target","depth_stencil","vertex_buffer","index_buffer","reset","log_flush","cursor_properties","cursor_position","cursor_show","stretch_backend","route_gate","route_draw","route_set_rt","route_jitter","route_fill","route_lazy_flush","route_readback","taa_run","taa_state_capture","taa_copy_color","taa_copy_depth","taa_resolve_draw","taa_state_apply","taa_copy_back","hdr_redirect","hdr_writeback","hdr_writeback_draw","hdr_writeback_stretch","hdr_bind","hdr_recheck","hdr_meter","hdr_meter_readback"};
static_assert(sizeof(names)/sizeof(*names)==static_cast<unsigned>(Metric::Count));
double us(uint64_t ticks){return clock_frequency?double(ticks)*1000000.0/double(clock_frequency):0;}
bool per_draw(Metric metric){
    switch(metric){
    case Metric::DrawBackend: case Metric::RouteGate: case Metric::RouteDraw: case Metric::RouteSetRenderTarget:
    case Metric::RouteJitter: case Metric::RouteLazyFlush: return true;
    default: return false;
    }
}
}
void initialize(void (*flush_log)()){
    flush_output=flush_log;
    wchar_t value[8]{};
    active=GetEnvironmentVariableW(L"X3M_TELEMETRY",value,8)==1 && value[0]==L'1';
    if(!active)return;
    draw_active=GetEnvironmentVariableW(L"X3M_TELEMETRY_DRAW",value,8)==1 && value[0]==L'1';
    LARGE_INTEGER f{}; if(!QueryPerformanceFrequency(&f)||f.QuadPart<=0){active=false;return;}
    clock_frequency=uint64_t(f.QuadPart); startup=now();global.last_summary=startup;
    for(unsigned i=0,limit_us=10;i<5;++i,limit_us*=10)bucket_ticks[i]=clock_frequency*limit_us/1000000u;
    log("telemetry_start schema=1 qpc_frequency=%llu qpc=%llu anchor=proxy_initialize cpu_only=1 per_draw=%u",clock_frequency,startup,draw_active);
}
bool enabled(){return active;}
bool draw_enabled(){return draw_active;}
bool enabled(Metric metric){return active && (draw_active || !per_draw(metric));}
uint64_t now(){if(!active)return 0;LARGE_INTEGER t{};QueryPerformanceCounter(&t);return uint64_t(t.QuadPart);}
uint64_t frequency(){return clock_frequency;}
double microseconds(uint64_t ticks){return us(ticks);}
State& process(){return global;}
void record(State& state,Metric metric,uint64_t ticks,bool failed,uint64_t bytes){
    if(!enabled(metric))return;
    auto& c=state.counters[static_cast<unsigned>(metric)];
    ++c.count;c.failures+=failed;c.total+=ticks;c.minimum=std::min(c.minimum,ticks);c.maximum=std::max(c.maximum,ticks);c.bytes+=bytes;
    unsigned bucket=0;while(bucket<5 && ticks>bucket_ticks[bucket])++bucket;
    ++c.buckets[bucket];
    // Check the reporting deadline from every observed operation, including
    // resource creation during loading when no Present calls arrive. The
    // summary formats microseconds and calls the loading reporters (x87 code
    // in the CRT formatter): it runs under its own CPU-state envelope so the
    // light draw hooks that record per-draw metrics stay transparent.
    const auto stamp=now();
    if(!state.last_summary)state.last_summary=stamp;
    else if(!reporting && stamp-state.last_summary>=clock_frequency)call_preserved([&]{summary(state,"interval",state.frame);});
}
void summary(State& state,const char* reason,uint64_t frame){
    if(!active)return;
    const auto stamp=now();
    const bool was_reporting=reporting;reporting=true;
    log("telemetry_summary device=%llu frame=%llu reason=%s qpc=%llu since_start_us=%.3f interval_us=%.3f position_suppressed=%llu cursor_changes_suppressed=%llu",state.device,frame,reason,stamp,us(stamp-startup),state.last_summary?us(stamp-state.last_summary):0,state.position_suppressed,state.cursor_changes_suppressed);
    for(unsigned i=0;i<unsigned(Metric::Count);++i){
        auto& c=state.counters[i];if(!c.count)continue;
        log("telemetry_metric device=%llu name=%s count=%llu failures=%llu total_us=%.3f min_us=%.3f max_us=%.3f bytes=%llu buckets=%llu,%llu,%llu,%llu,%llu,%llu",state.device,names[i],c.count,c.failures,us(c.total),us(c.minimum),us(c.maximum),c.bytes,c.buckets[0],c.buckets[1],c.buckets[2],c.buckets[3],c.buckets[4],c.buckets[5]);
        c={};
    }
    state.last_summary=stamp;state.position_suppressed=0;state.cursor_changes_suppressed=0;
    engine_memory_line("summary",state.device,frame);
    if(state.device)game_phases::report(frame); // prior finalized records only
    if(state.device==0)loading_trace::report();
    if(flush_output){const auto begin=now();flush_output();record(state,Metric::LogFlush,now()-begin);}
    reporting=was_reporting;
}
void present(State& state,uint64_t frame,bool captured,uint64_t begin,uint64_t end,HRESULT result){
    if(!active)return;
    state.frame=frame;
    record(state,captured?Metric::PresentCapture:Metric::PresentNormal,end-begin,FAILED(result));
    // Present-to-Present intervals containing either adjacent capture are kept
    // out of the normal distribution; snapshot CPU can contaminate both edges.
    if(state.had_present)record(state,(captured||state.last_frame_capture)?Metric::FrameCapture:Metric::FrameNormal,end-state.last_present);
    else log("telemetry_first_present device=%llu frame=%llu qpc=%llu since_start_us=%.3f capture=%u reset_count=%llu",state.device,frame,end,us(end-startup),captured,state.resets);
    state.had_present=true;state.last_present=end;state.last_frame_capture=captured;
    const bool marker_down=(GetAsyncKeyState(VK_F7)&0x8000)!=0 && (GetAsyncKeyState(VK_CONTROL)&0x8000)!=0 && (GetAsyncKeyState(VK_SHIFT)&0x8000)!=0;
    if(marker_down&&!state.marker_down && GetAncestor(GetForegroundWindow(),GA_ROOT)==GetAncestor(state.window,GA_ROOT))log("telemetry_phase_marker device=%llu frame=%llu marker=%llu qpc=%llu since_start_us=%.3f coverage=present_poll",state.device,frame,++state.markers,end,us(end-startup));
    state.marker_down=marker_down;
    poll_window(state,frame);
    if(!state.last_summary)state.last_summary=end;
    if(end-state.last_summary>=clock_frequency && frame%60==0) {
        summary(state,"periodic",frame);summary(global,"periodic",frame);
    }
}
void poll_window(State& state,uint64_t frame){
    if(!active)return;
    const auto stamp=now();if(state.polled && stamp-state.last_poll<clock_frequency/4)return;state.last_poll=stamp;
    const HWND foreground=GetForegroundWindow(),focus=GetFocus();
    const BOOL iconic=IsIconic(state.window),visible=IsWindowVisible(state.window);
    const LONG style=GetWindowLongW(state.window,GWL_STYLE),extended=GetWindowLongW(state.window,GWL_EXSTYLE);
    RECT wr{},cr{};GetWindowRect(state.window,&wr);GetClientRect(state.window,&cr);
    CURSORINFO info{};info.cbSize=sizeof info;const BOOL ok=GetCursorInfo(&info);const DWORD cursor_error=ok?0:GetLastError();
    const DWORD window_thread=GetWindowThreadProcessId(state.window,nullptr);
    GUITHREADINFO gui{};gui.cbSize=sizeof gui;const BOOL gui_ok=GetGUIThreadInfo(window_thread,&gui);
    RECT clip{};const BOOL clip_ok=GetClipCursor(&clip);
    bool changed=!state.polled || foreground!=state.foreground || focus!=state.thread_focus || iconic!=state.iconic || visible!=state.visible || style!=state.style || extended!=state.extended_style || std::memcmp(&wr,&state.window_rect,sizeof wr)||std::memcmp(&cr,&state.client_rect,sizeof cr);
    changed=changed || gui_ok!=state.gui_valid || gui.hwndActive!=state.gui_active || gui.hwndFocus!=state.gui_focus || gui.hwndCapture!=state.gui_capture || clip_ok!=state.clip_valid || std::memcmp(&clip,&state.clip_rect,sizeof clip);
    if(changed){
        const HMONITOR monitor=MonitorFromWindow(state.window,MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};mi.cbSize=sizeof mi;const BOOL monitor_ok=GetMonitorInfoW(monitor,&mi);
        log("telemetry_window_context device=%llu frame=%llu render_thread=%lu window_thread=%lu gui_ok=%d gui_active=%p gui_focus=%p gui_capture=%p clip_ok=%d clip=%ld,%ld,%ld,%ld monitor_ok=%d monitor=%ld,%ld,%ld,%ld work=%ld,%ld,%ld,%ld",state.device,frame,GetCurrentThreadId(),window_thread,gui_ok,gui.hwndActive,gui.hwndFocus,gui.hwndCapture,clip_ok,clip.left,clip.top,clip.right,clip.bottom,monitor_ok,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right,mi.rcMonitor.bottom,mi.rcWork.left,mi.rcWork.top,mi.rcWork.right,mi.rcWork.bottom);
    }
    if(changed)log("telemetry_window device=%llu frame=%llu qpc=%llu window=%p foreground=%p thread_focus=%p iconic=%d visible=%d style=%08lx exstyle=%08lx window_rect=%ld,%ld,%ld,%ld client_rect=%ld,%ld,%ld,%ld",state.device,frame,stamp,state.window,foreground,focus,iconic,visible,style,extended,wr.left,wr.top,wr.right,wr.bottom,cr.left,cr.top,cr.right,cr.bottom);
    if(ok && (!state.polled || info.hCursor!=state.cursor || info.flags!=state.cursor_flags || info.ptScreenPos.x!=state.point.x || info.ptScreenPos.y!=state.point.y))
        log("telemetry_cursor_poll device=%llu frame=%llu qpc=%llu flags=%lu cursor=%p x=%ld y=%ld",state.device,frame,stamp,info.flags,info.hCursor,info.ptScreenPos.x,info.ptScreenPos.y);
    else if(!ok && !state.polled)log("telemetry_cursor_poll device=%llu frame=%llu available=0 error=%lu",state.device,frame,cursor_error);
    state.gui_valid=gui_ok;state.gui_active=gui.hwndActive;state.gui_focus=gui.hwndFocus;state.gui_capture=gui.hwndCapture;state.clip_valid=clip_ok;state.clip_rect=clip;
    state.foreground=foreground;state.thread_focus=focus;state.iconic=iconic;state.visible=visible;state.style=style;state.extended_style=extended;state.window_rect=wr;state.client_rect=cr;
    if(ok){state.cursor=info.hCursor;state.cursor_flags=info.flags;state.point=info.ptScreenPos;}
    state.polled=true;
}
}
