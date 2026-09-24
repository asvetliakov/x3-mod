#include "cursor_reassert.h"

namespace x3m { void log(const char* format, ...); }

namespace x3m::cursor_reassert {
namespace {
bool enabled_ = false;
core::Machine machine_;
// Set by observe() on the window thread, read and cleared at the next Present on the same thread.
bool arm_pending_ = false;
DWORD armed_tick_ = 0;
struct Win32Api {
    void* arrow() { return LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); } // IDC_ARROW
    void* set_cursor(void* handle) { return SetCursor(static_cast<HCURSOR>(handle)); }
    int show_cursor(bool show) { return ShowCursor(show ? TRUE : FALSE); }
};
bool same_rect(const RECT& a, const RECT& b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }
// The client rectangle in screen coordinates (MapWindowPoints: documented, mirroring-aware).
bool client_on_screen(HWND window, RECT* out) {
    RECT r{};
    if (!GetClientRect(window, &r)) return false;
    SetLastError(ERROR_SUCCESS);
    if (!MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&r), 2) && GetLastError() != ERROR_SUCCESS) return false;
    *out = r;
    return true;
}
}
void initialize() {
    wchar_t value[4]{};
    enabled_ = GetEnvironmentVariableW(L"X3M_CURSOR_REASSERT", value, 4) == 1 && value[0] == L'1';
    if (enabled_)
        log("cursor_reassert_mode requested=1 window_frames=%u arm=WM_ACTIVATE_active,WM_ACTIVATEAPP_on gates=thread,foreground,visible,cursor_hidden,pointer_in_client_or_clip "
            "sequence=set_arrow,show_true,show_false,set_previous disable_on_mismatch=1", core::window_frames);
}
bool enabled() { return enabled_ && machine_.state != core::State::disabled; }
void observe(UINT message, WPARAM wparam) noexcept {
    if (!enabled_) return;
    if (core::observe(machine_, message, static_cast<std::uint32_t>(wparam))) { arm_pending_ = true; armed_tick_ = GetTickCount(); }
}
void refuse(const char* reason) {
    if (!enabled_) return;
    machine_.state = core::State::disabled;
    log("cursor_reassert action=refused reason=%s scope=process", reason);
}
core::Sequence run_win32_sequence() {
    Win32Api api;
    return core::run_sequence(api);
}
const core::Machine& machine() { return machine_; }
core::Decision present_with(HWND window, unsigned long long frame, const core::Gates& gates) {
    const DWORD saved_error = GetLastError();
    if (arm_pending_) {
        arm_pending_ = false;
        log("cursor_reassert_arm frame=%llu hwnd=%p message=%s arms=%u tick_ms=%lu", frame, static_cast<void*>(window),
            machine_.armed_by == core::wm_activate ? "WM_ACTIVATE" : "WM_ACTIVATEAPP", machine_.arms, static_cast<unsigned long>(armed_tick_));
    }
    const auto decision = core::present(machine_, gates);
    if (decision.step == core::Step::refuse)
        log("cursor_reassert frame=%llu hwnd=%p action=refused reason=%s window_frames=%u refusals=%u", frame, static_cast<void*>(window), decision.reason,
            core::window_frames, machine_.refusals);
    else if (decision.step == core::Step::fire) {
        CURSORINFO before{}; before.cbSize = sizeof before;
        const BOOL before_ok = GetCursorInfo(&before);
        RECT clip{}; const BOOL clip_ok = GetClipCursor(&clip);
        const auto sequence = run_win32_sequence();
        CURSORINFO after{}; after.cbSize = sizeof after;
        const BOOL after_ok = GetCursorInfo(&after);
        const bool balanced = before_ok && after_ok && core::balanced(sequence, before.flags, after.flags, before.hCursor, after.hCursor);
        core::after_fire(machine_, balanced);
        log("cursor_reassert frame=%llu hwnd=%p action=fired armed_by=%s before_flags=%lu before_cursor=%p previous=%p arrow=%p up=%d down=%d after_flags=%lu after_cursor=%p "
            "clip_ok=%d clip=%ld,%ld,%ld,%ld pointer_in_client=%u clip_is_client=%u balanced=%u disabled=%u fires=%u",
            frame, static_cast<void*>(window), machine_.armed_by == core::wm_activate ? "WM_ACTIVATE" : "WM_ACTIVATEAPP",
            static_cast<unsigned long>(before.flags), static_cast<void*>(before.hCursor), sequence.previous, sequence.arrow, sequence.up, sequence.down,
            static_cast<unsigned long>(after.flags), static_cast<void*>(after.hCursor), int(clip_ok), clip.left, clip.top, clip.right, clip.bottom,
            unsigned(gates.pointer_in_client), unsigned(gates.clip_is_client), unsigned(balanced), unsigned(machine_.state == core::State::disabled), machine_.fires);
    }
    SetLastError(saved_error);
    return decision;
}
core::Gates collect_gates(HWND window) {
    const DWORD saved_error = GetLastError();
    core::Gates gates;
    gates.same_thread = window && GetWindowThreadProcessId(window, nullptr) == GetCurrentThreadId();
    if (gates.same_thread) {
        gates.foreground = GetForegroundWindow() == window;
        gates.visible = IsWindowVisible(window) && !IsIconic(window);
        CURSORINFO info{}; info.cbSize = sizeof info;
        gates.cursor_hidden = GetCursorInfo(&info) && !(info.flags & core::cursor_showing);
        RECT client{};
        if (client_on_screen(window, &client)) {
            POINT point{};
            gates.pointer_in_client = GetCursorPos(&point) && PtInRect(&client, point);
            RECT clip{};
            gates.clip_is_client = GetClipCursor(&clip) && same_rect(clip, client);
        }
    }
    SetLastError(saved_error);
    return gates;
}
void present(HWND window, unsigned long long frame) {
    if (!enabled_ || machine_.state != core::State::armed) return; // the per-frame cost while idle: two loads
    present_with(window, frame, collect_gates(window));
}
}
