#pragma once
#include <windows.h>
#include "window_mode_core.h"

// --window-monitor-rect (X3M_WINDOW_MONITOR_RECT=1; docs/architecture/
// window-mode-and-cursor-fix.md section 2.1). At create_before and
// reset_before, when window_mode_core.h's predicate holds, one
// SetWindowPos(SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER) moves the game's
// own device window from the work-area origin to its monitor rectangle, on
// the window's own thread. One window_mode row per call while the option is
// on; nothing (not even a Win32 read) while it is off. Documented Win32 only.
namespace x3m::window_mode {
// Reads X3M_WINDOW_MONITOR_RECT ("1" on, anything else off; unset = off) and
// X3M_WINDOW_MONITOR_RECT_DEFAULT (the launcher's marker); one
// window_mode_config row when the variable is present.
void initialize();
bool enabled();
struct Result {
    bool evaluated = false; // the option was on and the predicate ran
    core::Decision decision{core::Action::refused, "off"};
    BOOL set_result = FALSE; // SetWindowPos, when called
    DWORD set_error = 0;     // its GetLastError on failure (restored afterwards)
    core::Rect before, after;
};
// hwnd = device_window, or focus_window when device_window is NULL (D3D9's
// rule). Preserves the thread's last error.
Result apply(const char* phase, HWND focus_window, HWND device_window, bool windowed, UINT backbuffer_width,
             UINT backbuffer_height);
}
