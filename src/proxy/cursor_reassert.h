#pragma once
#include <windows.h>
#include "cursor_reassert_core.h"

// --cursor-reassert (X3M_CURSOR_REASSERT=1; docs/architecture/
// window-mode-and-cursor-fix.md section 3.3). Armed by the WH_CALLWNDPROC
// observer that window_trace.cpp installs on the window thread (with or
// without --window-trace), evaluated at each Present on that thread: once the
// window is foreground and visible, Win32 reports the cursor hidden and the
// pointer is inside the client (or the clip is the client), one balanced
// SetCursor(arrow) / ShowCursor(TRUE) / ShowCursor(FALSE) / SetCursor(previous)
// runs, so the Win32 end state equals the start and the display driver sees
// one unhide and one hide transition. Never a global hide, a loop, a
// foreground change or a pointer trap. Rows: cursor_reassert_mode,
// cursor_reassert_arm, cursor_reassert.
namespace x3m::cursor_reassert {
void initialize();
bool enabled();
// The observer hook's call (window thread, inside the hook's LightCallBoundary):
// integer work only, no logging.
void observe(UINT message, WPARAM wparam) noexcept;
// The hooks could not be installed (foreign thread, SetWindowsHookEx failure):
// one row, the option stays off for the process.
void refuse(const char* reason);
// Present, after the native call, on the render thread.
void present(HWND window, unsigned long long frame);
// The Win32 gate reads present() makes (the fixture records them with its own window
// foreground). Preserves the thread's last error.
core::Gates collect_gates(HWND window);
// present() with the gates given (the Wine fixture's synthetic states); the
// sequence itself is the real Win32 one. Preserves the thread's last error.
core::Decision present_with(HWND window, unsigned long long frame, const core::Gates& gates);
// The Win32 sequence alone (the fixture's direct check of the counts).
core::Sequence run_win32_sequence();
const core::Machine& machine();
}
