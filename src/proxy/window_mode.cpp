#include "window_mode.h"

namespace x3m { void log(const char* format, ...); }

namespace x3m::window_mode {
namespace {
bool enabled_ = false;
core::Rect rect_of(const RECT& r) { core::Rect c; c.left = r.left; c.top = r.top; c.right = r.right; c.bottom = r.bottom; return c; }
}
void initialize() {
    wchar_t value[4]{};
    const DWORD length = GetEnvironmentVariableW(L"X3M_WINDOW_MONITOR_RECT", value, 4);
    enabled_ = length == 1 && value[0] == L'1';
    if (!length) return; // unset: the fixtures' and plain launches' default, no row
    wchar_t marker[4]{};
    const bool is_default = GetEnvironmentVariableW(L"X3M_WINDOW_MONITOR_RECT_DEFAULT", marker, 4) == 1 && marker[0] == L'1';
    log("window_mode_config requested=%u default=%u phases=create_before,reset_before flags=SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER",
        unsigned(enabled_), unsigned(is_default));
}
bool enabled() { return enabled_; }
Result apply(const char* phase, HWND focus_window, HWND device_window, bool windowed, UINT backbuffer_width, UINT backbuffer_height) {
    Result result;
    if (!enabled_) return result;
    const DWORD saved_error = GetLastError();
    result.evaluated = true;
    const HWND hwnd = device_window ? device_window : focus_window;
    core::Input in;
    in.windowed = windowed;
    in.single_window = !device_window || device_window == focus_window;
    in.window_valid = hwnd && IsWindow(hwnd);
    in.backbuffer_width = backbuffer_width; in.backbuffer_height = backbuffer_height;
    DWORD thread = 0;
    if (in.window_valid) {
        in.top_level = GetAncestor(hwnd, GA_PARENT) == GetDesktopWindow();
        thread = GetWindowThreadProcessId(hwnd, nullptr);
        in.same_thread = thread == GetCurrentThreadId();
        in.style = static_cast<std::uint32_t>(GetWindowLongW(hwnd, GWL_STYLE));
        in.exstyle = static_cast<std::uint32_t>(GetWindowLongW(hwnd, GWL_EXSTYLE));
        RECT wr{};
        if (GetWindowRect(hwnd, &wr)) in.window = rect_of(wr);
        MONITORINFO mi{}; mi.cbSize = sizeof mi;
        const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        in.monitor_known = monitor && GetMonitorInfoW(monitor, &mi);
        if (in.monitor_known) { in.monitor = rect_of(mi.rcMonitor); in.work = rect_of(mi.rcWork); }
    }
    result.decision = core::decide(in);
    result.before = result.after = in.window;
    const char* action = core::action_name(result.decision.action);
    if (result.decision.action == core::Action::move) {
        // HWND_TOP is ignored under SWP_NOZORDER; the z-order, activation and
        // owner order stay the game's. Same thread (condition 4): no cross-thread send.
        result.set_result = SetWindowPos(hwnd, HWND_TOP, in.monitor.left, in.monitor.top, int(core::width(in.monitor)), int(core::height(in.monitor)),
                                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        if (!result.set_result) { result.set_error = GetLastError(); action = "move_failed"; }
        RECT wr{};
        if (GetWindowRect(hwnd, &wr)) result.after = rect_of(wr);
        if (result.set_result && result.after != in.monitor) action = "moved_constrained";
    }
    log("window_mode phase=%s hwnd=%p style=%08lx exstyle=%08lx before=%ld,%ld,%ld,%ld after=%ld,%ld,%ld,%ld monitor=%ld,%ld,%ld,%ld work=%ld,%ld,%ld,%ld "
        "backbuffer=%ux%u windowed=%u single_window=%u top_level=%u window_thread=%lu thread=%lu action=%s reason=%s result=%d error=%lu",
        phase, static_cast<void*>(hwnd), static_cast<unsigned long>(in.style), static_cast<unsigned long>(in.exstyle),
        long(result.before.left), long(result.before.top), long(result.before.right), long(result.before.bottom),
        long(result.after.left), long(result.after.top), long(result.after.right), long(result.after.bottom),
        long(in.monitor.left), long(in.monitor.top), long(in.monitor.right), long(in.monitor.bottom),
        long(in.work.left), long(in.work.top), long(in.work.right), long(in.work.bottom),
        backbuffer_width, backbuffer_height, unsigned(windowed), unsigned(in.single_window), unsigned(in.top_level),
        static_cast<unsigned long>(thread), static_cast<unsigned long>(GetCurrentThreadId()), action, result.decision.reason,
        int(result.set_result), static_cast<unsigned long>(result.set_error));
    SetLastError(saved_error);
    return result;
}
}
