#pragma once
#include <windows.h>
#include "window_trace_core.h"

// --window-trace (X3M_WINDOW_TRACE=1, requires X3M_TELEMETRY=1;
// docs/architecture/window-mode-and-cursor-fix.md section 3.2) and the owner of
// the window-thread message hooks that --cursor-reassert also uses.
//
// Hooks: SetWindowsHookExW thread hooks on the device window's thread, only
// when that thread is the one creating the device (the render thread):
// WH_CALLWNDPROC (arming for cursor_reassert, nesting depth), with the trace
// also WH_CALLWNDPROCRET (the original handler's result, no subclassing) and
// WH_GETMESSAGE (posted WM_MOUSEMOVE / WM_NCMOUSEMOVE counts). The hook
// procedures keep the thread's last error and MXCSR (LightCallBoundary), make
// no Win32 call besides CallNextHookEx and GetTickCount, and never log: they
// write a bounded ring (1024 entries) that the Present flushes at the device's
// first Present (reason=first_present), on a transition message to the device
// window (entries older than 4 s are dropped); messages to the thread's other windows (the
// fixture saw one, not identified) are rows, not transitions, and never arm cursor_reassert.
// Removed at device destruction and at DLL detach.
//
// Rows: window_trace_scope (once), window_trace_hooks (install / remove),
// window_msg, window_msg_frame, cursor_call (the EXE's SetCursor/SetCursorPos
// imports through the light IAT rows), window_trace_flush, and cursor_snapshot
// (change-only, every Present for 120 frames after the first Present and after
// each transition). The first complete installation per process also arms
// cursor_reassert once (armed_by=launch). dinput's own
// ShowCursor/ClipCursor and the Cocoa cursor are not instrumented (labelled in
// window_trace_scope). The trace never shows, hides, warps or captures.
namespace x3m::window_trace {
using CursorSource = unsigned (*)(std::uint32_t after, core::CursorEvent* out, unsigned capacity, std::uint32_t* newest,
                                  core::CursorCounts* counts) noexcept;
// Reads X3M_WINDOW_TRACE; the trace is on only with telemetry. source drains
// the light rows' cursor events (nullptr: no cursor_call rows).
void initialize(bool telemetry, CursorSource source);
bool enabled();
// hook_device: installs the hooks the trace or cursor_reassert need.
void attach(HWND window, unsigned long long device);
// A second device while hooks exist logs one reason=already_hooked row and keeps them.
// Device destruction: removes the hooks this device installed (one row). A final
// Release off the installing thread leaves them (one row, no flush): the next attach
// on that thread or shutdown() removes them.
void detach(unsigned long long device);
// DllMain DLL_PROCESS_DETACH on FreeLibrary only (loader.cpp): removes any hook, no logging,
// idempotent. The first successful attach pins the module, so this acts only if the pin failed.
void shutdown() noexcept;
// Present, after the native call, for the device that installed the hooks only:
// cursor_reassert's step, then the trace's drain, flush and snapshot.
void present(HWND window, unsigned long long device, unsigned long long frame);
// Hook observation counters (the Wine fixture's assertions).
struct Observed {
    unsigned calls = 0, rets = 0, gets = 0;
    UINT last_message = 0;
    LRESULT last_result = 0;
    bool installed = false;
    BOOL unhook_call = FALSE, unhook_ret = FALSE, unhook_get = FALSE; // the last removal's results
};
Observed observed();
}
