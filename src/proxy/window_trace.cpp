#include "window_trace.h"
#include "cursor_reassert.h"
#include "cpu_state.h"
#include "log_tiers.h"
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}

extern "C" LRESULT CALLBACK x3m_window_hook_call(int code, WPARAM wparam, LPARAM lparam);
extern "C" LRESULT CALLBACK x3m_window_hook_ret(int code, WPARAM wparam, LPARAM lparam);
extern "C" LRESULT CALLBACK x3m_window_hook_get(int code, WPARAM wparam, LPARAM lparam);

namespace x3m::window_trace {
namespace {
bool requested_ = false, enabled_ = false;
CursorSource cursor_source_ = nullptr;
// Hook ownership. The hook procedures run on hook_thread_ only (thread hooks);
// present() touches the ring only on that thread too, so no lock is needed.
HHOOK call_hook_ = nullptr, ret_hook_ = nullptr, get_hook_ = nullptr;
DWORD hook_thread_ = 0;
// The device window. Thread hooks see every window of the thread (the Wine fixture
// saw a second one, not identified): rows are written for all of them, but only this window's messages
// count as transitions (flush, snapshot burst) and arm cursor_reassert.
HWND target_ = nullptr;
unsigned long long hook_device_ = 0;
// The final Release ran off the installing thread: the hooks stay (UnhookWindowsHookEx
// only from hook_thread_), the ring is left alone, and the next attach on hook_thread_
// or shutdown() removes them.
bool orphaned_ = false;
// Pinned for the process lifetime on the first successful attach (documented
// GET_MODULE_HANDLE_EX_FLAG_PIN, as cull_census.cpp does): a hook procedure can then
// never outlive this module's code, whatever happens to the hooks.
bool pinned_ = false;
bool thread_mismatch_logged_ = false;
Observed observed_;
unsigned depth_ = 0;
unsigned long long current_frame_ = 0;

enum class Kind : unsigned char { message, setcursor, frame, cursor_set, cursor_pos };
enum class Slot : unsigned char { empty, pending, written };
struct Entry {
    Slot slot = Slot::empty;
    Kind kind = Kind::message;
    std::uint32_t tick = 0, seq = 0, depth = 0, message = 0;
    std::uint32_t a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
    HWND hwnd = nullptr;
    WPARAM wparam = 0;
    LPARAM lparam = 0;
    LRESULT result = 0;
    unsigned long long frame = 0;
};
constexpr unsigned ring_capacity = 1024; // 4 s of per-frame entries at 240 fps plus the messages
Entry ring_[ring_capacity];
std::uint32_t ring_next_ = 0, overwritten_ = 0, message_seq_ = 0;
bool transition_pending_ = false;
// Set when the hooks are installed with the trace on: the device's first Present flushes
// the ring (the entries since device creation) and starts a snapshot burst, as a transition does.
bool first_present_pending_ = false;
// cursor_reassert's launch arm: once per process, at the first complete installation.
bool launch_armed_ = false;
core::SetCursorSummary setcursor_;
std::uint32_t frame_mousemove_ = 0, frame_ncmousemove_ = 0, frame_setcursor_ = 0;
std::uint32_t cursor_seen_ = 0;
unsigned burst_left_ = 0, burst_index_ = 0;

Entry& push(Kind kind) {
    Entry& e = ring_[ring_next_ % ring_capacity];
    if (e.slot == Slot::pending) ++overwritten_;
    e = Entry{};
    e.slot = Slot::pending;
    e.kind = kind;
    e.tick = GetTickCount();
    e.frame = current_frame_;
    ++ring_next_;
    return e;
}
// Hook side (window thread, LightCallBoundary): integer stores only.
void record_return(const CWPRETSTRUCT& m) {
    const std::uint32_t message = m.message;
    if (core::transition_message(message)) {
        Entry& e = push(Kind::message);
        e.seq = ++message_seq_;
        e.depth = depth_;
        e.message = message;
        e.hwnd = m.hwnd;
        e.wparam = m.wParam;
        e.lparam = m.lParam;
        e.result = m.lResult;
        if (m.hwnd == target_) transition_pending_ = true;
    } else if (message == core::wm_setcursor) {
        ++frame_setcursor_;
        const std::uint32_t hit = static_cast<std::uint32_t>(m.lParam) & 0xffffu,
                            trigger = (static_cast<std::uint32_t>(m.lParam) >> 16) & 0xffffu;
        const std::uint32_t suppressed = setcursor_.suppressed;
        if (core::setcursor_row(setcursor_, GetTickCount(), static_cast<std::uint32_t>(m.lResult), hit, trigger)) {
            Entry& e = push(Kind::setcursor);
            e.seq = ++message_seq_;
            e.depth = depth_;
            e.message = message;
            e.hwnd = m.hwnd;
            e.wparam = m.wParam;
            e.lparam = m.lParam;
            e.result = m.lResult;
            e.a = hit;
            e.b = trigger;
            e.c = suppressed;
            setcursor_.suppressed = 0;
        }
    }
}
void write(const Entry& e) {
    switch (e.kind) {
    case Kind::message:
        log("window_msg seq=%u depth=%u frame=%llu tick_ms=%lu hwnd=%p msg=%04x name=%s wparam=%08lx lparam=%08lx result=%08lx",
            e.seq, e.depth, e.frame, static_cast<unsigned long>(e.tick), static_cast<void*>(e.hwnd), e.message,
            core::message_name(e.message), static_cast<unsigned long>(e.wparam), static_cast<unsigned long>(e.lparam),
            static_cast<unsigned long>(e.result));
        break;
    case Kind::setcursor:
        log("window_msg seq=%u depth=%u frame=%llu tick_ms=%lu hwnd=%p msg=0020 name=WM_SETCURSOR wparam=%08lx hit=%u trigger=%04x result=%08lx suppressed=%u",
            e.seq, e.depth, e.frame, static_cast<unsigned long>(e.tick), static_cast<void*>(e.hwnd),
            static_cast<unsigned long>(e.wparam), e.a, e.b, static_cast<unsigned long>(e.result), e.c);
        break;
    case Kind::frame:
        log("window_msg_frame frame=%llu tick_ms=%lu mousemove=%u ncmousemove=%u setcursor=%u cursor_set=%u cursor_pos=%u cursor_dropped=%u",
            e.frame, static_cast<unsigned long>(e.tick), e.a, e.b, e.c, e.d, e.e, e.f);
        break;
    case Kind::cursor_set:
        log("cursor_call seq=%u frame=%llu tick_ms=%lu thread=%u op=set handle=%08x previous=%08x count=%u", e.seq,
            e.frame, static_cast<unsigned long>(e.tick), e.d, e.a, e.b, e.e);
        break;
    case Kind::cursor_pos:
        log("cursor_call seq=%u frame=%llu tick_ms=%lu thread=%u op=pos x=%d y=%d result=%u count=%u", e.seq, e.frame,
            static_cast<unsigned long>(e.tick), e.d, static_cast<int>(e.a), static_cast<int>(e.b), e.c, e.e);
        break;
    }
}
void flush(const char* reason, unsigned long long frame) {
    const std::uint32_t now = GetTickCount();
    unsigned written = 0, expired = 0;
    const std::uint32_t first = ring_next_ > ring_capacity ? ring_next_ - ring_capacity : 0;
    for (std::uint32_t i = first; i != ring_next_; ++i) {
        Entry& e = ring_[i % ring_capacity];
        if (e.slot != Slot::pending) continue;
        e.slot = Slot::written;
        if (now - e.tick > core::ring_ms) {
            ++expired;
            continue;
        }
        write(e);
        ++written;
    }
    log("window_trace_flush frame=%llu reason=%s written=%u expired=%u overwritten=%u", frame, reason, written, expired,
        overwritten_);
    overwritten_ = 0;
}
struct Snapshot {
    HWND foreground, focus, active, capture;
    BOOL visible, iconic, cursor_ok, clip_ok, monitor_ok;
    LONG style, exstyle;
    DWORD cursor_flags;
    HCURSOR cursor;
    POINT point, client_origin;
    RECT clip, window, client, monitor, work;
};
Snapshot last_snapshot_;
void snapshot(HWND window, unsigned long long device, unsigned long long frame) {
    Snapshot s;
    std::memset(&s, 0, sizeof s); // padding too: the change test is a memcmp
    s.foreground = GetForegroundWindow();
    s.focus = GetFocus();
    s.active = GetActiveWindow();
    s.capture = GetCapture();
    s.visible = IsWindowVisible(window);
    s.iconic = IsIconic(window);
    s.style = GetWindowLongW(window, GWL_STYLE);
    s.exstyle = GetWindowLongW(window, GWL_EXSTYLE);
    CURSORINFO info{};
    info.cbSize = sizeof info;
    s.cursor_ok = GetCursorInfo(&info);
    if (s.cursor_ok) {
        s.cursor_flags = info.flags;
        s.cursor = info.hCursor;
        s.point = info.ptScreenPos;
    }
    s.clip_ok = GetClipCursor(&s.clip);
    GetWindowRect(window, &s.window);
    GetClientRect(window, &s.client);
    ClientToScreen(window, &s.client_origin);
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    s.monitor_ok = GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &mi);
    if (s.monitor_ok) {
        s.monitor = mi.rcMonitor;
        s.work = mi.rcWork;
    }
    const bool first = burst_index_ == 0;
    ++burst_index_;
    if (!first && !std::memcmp(&s, &last_snapshot_, sizeof s)) return;
    last_snapshot_ = s;
    log("cursor_snapshot device=%llu frame=%llu burst=%u foreground=%p focus=%p active=%p capture=%p visible=%d iconic=%d style=%08lx exstyle=%08lx "
        "cursor_ok=%d cursor_flags=%lu cursor=%p x=%ld y=%ld clip_ok=%d clip=%ld,%ld,%ld,%ld window_rect=%ld,%ld,%ld,%ld client_origin=%ld,%ld client_size=%ld,%ld "
        "monitor_ok=%d monitor=%ld,%ld,%ld,%ld work=%ld,%ld,%ld,%ld",
        device, frame, burst_index_ - 1, static_cast<void*>(s.foreground), static_cast<void*>(s.focus),
        static_cast<void*>(s.active), static_cast<void*>(s.capture), s.visible, s.iconic,
        static_cast<unsigned long>(s.style), static_cast<unsigned long>(s.exstyle), s.cursor_ok,
        static_cast<unsigned long>(s.cursor_flags), static_cast<void*>(s.cursor), s.point.x, s.point.y, s.clip_ok,
        s.clip.left, s.clip.top, s.clip.right, s.clip.bottom, s.window.left, s.window.top, s.window.right,
        s.window.bottom, s.client_origin.x, s.client_origin.y, s.client.right, s.client.bottom, s.monitor_ok,
        s.monitor.left, s.monitor.top, s.monitor.right, s.monitor.bottom, s.work.left, s.work.top, s.work.right,
        s.work.bottom);
}
void drain_cursor_events(core::CursorCounts& counts) {
    if (!cursor_source_) return;
    core::CursorEvent events[core::cursor_event_capacity];
    std::uint32_t newest = cursor_seen_;
    const unsigned n = cursor_source_(cursor_seen_, events, core::cursor_event_capacity, &newest, &counts);
    for (unsigned i = 0; i < n; ++i) {
        const auto& c = events[i];
        Entry& e = push(c.op == 0 ? Kind::cursor_set : Kind::cursor_pos);
        e.tick = c.tick;
        e.seq = c.seq;
        e.a = c.a;
        e.b = c.b;
        e.c = c.result;
        e.d = c.thread;
        e.e = c.op == 0 ? counts.set : counts.pos;
    }
    cursor_seen_ = newest;
}
HHOOK take(HHOOK& slot) {
    return static_cast<HHOOK>(InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(&slot), nullptr));
}
}

void initialize(bool telemetry, CursorSource source) {
    requested_ = log_tier::debug_flag(L"X3M_WINDOW_TRACE"); // X3M_WINDOW_TRACE=1 or X3M_DEBUG=1
    enabled_ = requested_ && telemetry;
    cursor_source_ = enabled_ ? source : nullptr;
    if (requested_)
        log("window_trace_scope requested=1 enabled=%u telemetry=%u reason=%s hooks=callwndproc,callwndprocret,getmessage cursor_iat=%u snapshot_frames=%u ring=%u ring_ms=%u "
            "setcursor_ms=%u flush=first_present,transition excludes=dinput_user32,cocoa",
            unsigned(enabled_), unsigned(telemetry), enabled_ ? "ok" : "telemetry_off",
            unsigned(cursor_source_ != nullptr), core::snapshot_frames, ring_capacity, core::ring_ms,
            core::setcursor_interval_ms);
}
bool enabled() {
    return enabled_;
}
void attach(HWND window, unsigned long long device) {
    const bool reassert = cursor_reassert::enabled();
    if (!enabled_ && !reassert) return;
    const DWORD saved_error = GetLastError();
    const DWORD window_thread = window ? GetWindowThreadProcessId(window, nullptr) : 0;
    const DWORD thread = GetCurrentThreadId();
    if (observed_.installed && orphaned_ && thread == hook_thread_) {
        // Hooks left by an off-thread final Release: removed here, on their installing thread, then reinstalled below.
        const HHOOK get = take(get_hook_), ret = take(ret_hook_), call = take(call_hook_);
        const BOOL unhooked = (!get || UnhookWindowsHookEx(get)) && (!ret || UnhookWindowsHookEx(ret)) &&
                              (!call || UnhookWindowsHookEx(call));
        observed_.installed = false;
        orphaned_ = false;
        burst_left_ = 0;
        transition_pending_ = false;
        first_present_pending_ = false;
        log("window_trace_hooks device=%llu removed=1 reason=orphaned_by_foreign_release result=%d", device,
            int(unhooked));
    }
    if (observed_.installed) {
        // One installation per process: a second device keeps the first one's hooks (and its Present step).
        log("window_trace_hooks device=%llu window=%p installed=0 reason=already_hooked hooked_device=%llu hook_thread=%lu thread=%lu",
            device, static_cast<void*>(window), hook_device_, static_cast<unsigned long>(hook_thread_),
            static_cast<unsigned long>(thread));
        SetLastError(saved_error);
        return;
    }
    if (!window_thread || window_thread != thread) {
        log("window_trace_hooks device=%llu window=%p window_thread=%lu render_thread=%lu installed=0 reason=%s trace=%u reassert=%u",
            device, static_cast<void*>(window), static_cast<unsigned long>(window_thread),
            static_cast<unsigned long>(thread), window_thread ? "foreign_thread" : "no_window", unsigned(enabled_),
            unsigned(reassert));
        cursor_reassert::refuse(window_thread ? "foreign_thread" : "no_window");
        SetLastError(saved_error);
        return;
    }
    // Thread hooks for this process's own thread: hMod NULL (SetWindowsHookEx documentation).
    hook_thread_ = thread;
    target_ = window;
    DWORD error = 0;
    call_hook_ = SetWindowsHookExW(WH_CALLWNDPROC, x3m_window_hook_call, nullptr, thread);
    if (!call_hook_) error = GetLastError();
    if (enabled_ && call_hook_) {
        ret_hook_ = SetWindowsHookExW(WH_CALLWNDPROCRET, x3m_window_hook_ret, nullptr, thread);
        if (!ret_hook_) error = GetLastError();
    }
    if (enabled_ && ret_hook_) {
        get_hook_ = SetWindowsHookExW(WH_GETMESSAGE, x3m_window_hook_get, nullptr, thread);
        if (!get_hook_) error = GetLastError();
    }
    const bool complete = call_hook_ && (!enabled_ || (ret_hook_ && get_hook_));
    orphaned_ = false;
    if (!complete) { // all or none
        if (HHOOK h = take(get_hook_)) UnhookWindowsHookEx(h);
        if (HHOOK h = take(ret_hook_)) UnhookWindowsHookEx(h);
        if (HHOOK h = take(call_hook_)) UnhookWindowsHookEx(h);
        cursor_reassert::refuse("set_hook_failed");
        enabled_ = false;
        cursor_source_ = nullptr;
    }
    observed_.installed = complete;
    if (complete) hook_device_ = device;
    if (complete && enabled_) first_present_pending_ = true;
    const bool launch_arm = complete && reassert && !launch_armed_;
    if (launch_arm) {
        launch_armed_ = true;
        cursor_reassert::arm_launch();
    }
    if (complete && !pinned_) {
        HMODULE self = nullptr;
        pinned_ = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                     reinterpret_cast<LPCWSTR>(&x3m_window_hook_call), &self) != FALSE &&
                  self != nullptr;
    }
    log("window_trace_hooks device=%llu window=%p window_thread=%lu render_thread=%lu installed=%u reason=%s error=%lu trace=%u reassert=%u call=%u ret=%u get=%u pinned=%u "
        "launch_arm=%u",
        device, static_cast<void*>(window), static_cast<unsigned long>(window_thread),
        static_cast<unsigned long>(thread), unsigned(complete), complete ? "ok" : "set_hook_failed",
        static_cast<unsigned long>(error), unsigned(enabled_), unsigned(reassert && complete),
        unsigned(call_hook_ != nullptr), unsigned(ret_hook_ != nullptr), unsigned(get_hook_ != nullptr),
        unsigned(pinned_), unsigned(launch_arm));
    SetLastError(saved_error);
}
void detach(unsigned long long device) {
    if (!observed_.installed || device != hook_device_) return;
    const DWORD saved_error = GetLastError();
    if (GetCurrentThreadId() != hook_thread_) {
        // The hook procedures may be writing ring_ on the window thread right now: no flush,
        // and no UnhookWindowsHookEx off the installing thread. present() no longer matches
        // any device; the next attach on hook_thread_ or shutdown() removes the hooks.
        hook_device_ = 0;
        orphaned_ = true;
        log("window_trace_hooks device=%llu removed=0 reason=foreign_release release_thread=%lu hook_thread=%lu action=left_for_shutdown",
            device, static_cast<unsigned long>(GetCurrentThreadId()), static_cast<unsigned long>(hook_thread_));
        SetLastError(saved_error);
        return;
    }
    const HHOOK get = take(get_hook_), ret = take(ret_hook_), call = take(call_hook_);
    observed_.unhook_get = get ? UnhookWindowsHookEx(get) : FALSE;
    observed_.unhook_ret = ret ? UnhookWindowsHookEx(ret) : FALSE;
    observed_.unhook_call = call ? UnhookWindowsHookEx(call) : FALSE;
    observed_.installed = false;
    burst_left_ = 0;
    first_present_pending_ = false;
    if (enabled_) flush("device_destroy", current_frame_); // the last transitions before the device went
    log("window_trace_hooks device=%llu removed=1 call=%d ret=%d get=%d", device, int(observed_.unhook_call),
        int(observed_.unhook_ret), int(observed_.unhook_get));
    SetLastError(saved_error);
}
void shutdown() noexcept {
    // With the module pinned (the normal case) no FreeLibrary detach can reach here while a
    // hook exists, so the unhooks below are dead code; they stay only for a failed pin.
    if (pinned_) return;
    const DWORD saved_error = GetLastError();
    if (HHOOK h = take(get_hook_)) UnhookWindowsHookEx(h);
    if (HHOOK h = take(ret_hook_)) UnhookWindowsHookEx(h);
    if (HHOOK h = take(call_hook_)) UnhookWindowsHookEx(h);
    SetLastError(saved_error);
}
void present(HWND window, unsigned long long device, unsigned long long frame) {
    if (!observed_.installed || device != hook_device_)
        return; // options off, hooks refused or another device: two loads per Present
    const DWORD saved_error = GetLastError();
    if (GetCurrentThreadId() != hook_thread_) {
        if (!thread_mismatch_logged_) {
            thread_mismatch_logged_ = true;
            log("window_trace_thread_mismatch device=%llu frame=%llu present_thread=%lu hook_thread=%lu action=skip",
                device, frame, static_cast<unsigned long>(GetCurrentThreadId()),
                static_cast<unsigned long>(hook_thread_));
        }
        SetLastError(saved_error);
        return;
    }
    cursor_reassert::present(window, frame);
    if (enabled_) {
        core::CursorCounts counts;
        drain_cursor_events(counts);
        if (frame_mousemove_ || frame_ncmousemove_ || frame_setcursor_ || counts.set || counts.pos || counts.dropped) {
            Entry& e = push(Kind::frame);
            e.a = frame_mousemove_;
            e.b = frame_ncmousemove_;
            e.c = frame_setcursor_;
            e.d = counts.set;
            e.e = counts.pos;
            e.f = counts.dropped;
            frame_mousemove_ = frame_ncmousemove_ = frame_setcursor_ = 0;
        }
        if (first_present_pending_ || transition_pending_) {
            flush(first_present_pending_ ? "first_present" : "transition", frame);
            if (first_present_pending_ || transition_pending_) {
                burst_left_ = core::snapshot_frames;
                burst_index_ = 0;
            }
            first_present_pending_ = transition_pending_ = false;
        }
        if (burst_left_ && window) {
            snapshot(window, device, frame);
            --burst_left_;
        }
    }
    current_frame_ = frame;
    SetLastError(saved_error);
}
Observed observed() {
    return observed_;
}
}

// The hook procedures: the thread's last error and MXCSR are kept around our
// part and the rest of the chain's result is passed through unchanged
// (check_no_x87.py walks them: no logging, no floating point).
using namespace x3m::window_trace;
extern "C" LRESULT CALLBACK x3m_window_hook_call(int code, WPARAM wparam, LPARAM lparam) {
    x3m::LightCallBoundary cpu;
    if (code == HC_ACTION && lparam) {
        const auto& m = *reinterpret_cast<const CWPSTRUCT*>(lparam);
        ++observed_.calls;
        ++depth_;
        if (m.hwnd == target_) x3m::cursor_reassert::observe(m.message, m.wParam);
    }
    cpu.before_original();
    const LRESULT result = CallNextHookEx(nullptr, code, wparam, lparam);
    cpu.after_original();
    return result;
}
extern "C" LRESULT CALLBACK x3m_window_hook_ret(int code, WPARAM wparam, LPARAM lparam) {
    x3m::LightCallBoundary cpu;
    if (code == HC_ACTION && lparam) {
        const auto& m = *reinterpret_cast<const CWPRETSTRUCT*>(lparam);
        ++observed_.rets;
        observed_.last_message = m.message;
        observed_.last_result = m.lResult;
        if (depth_) --depth_;
        if (enabled_) record_return(m);
    }
    cpu.before_original();
    const LRESULT result = CallNextHookEx(nullptr, code, wparam, lparam);
    cpu.after_original();
    return result;
}
extern "C" LRESULT CALLBACK x3m_window_hook_get(int code, WPARAM wparam, LPARAM lparam) {
    x3m::LightCallBoundary cpu;
    if (code == HC_ACTION && wparam == PM_REMOVE && lparam) {
        const auto& m = *reinterpret_cast<const MSG*>(lparam);
        ++observed_.gets;
        if (m.message == x3m::window_trace::core::wm_mousemove)
            ++frame_mousemove_;
        else if (m.message == x3m::window_trace::core::wm_ncmousemove)
            ++frame_ncmousemove_;
    }
    cpu.before_original();
    const LRESULT result = CallNextHookEx(nullptr, code, wparam, lparam);
    cpu.after_original();
    return result;
}
