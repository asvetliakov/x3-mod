#pragma once
#include <cstdint>

// --window-trace pure parts (docs/architecture/window-mode-and-cursor-fix.md
// section 3.2): which messages are transitions, the WM_SETCURSOR summary rule,
// message names and the cursor-call event record shared with the light IAT
// rows (loading_trace_light.cpp, compiled without SSE: plain 32-bit fields).
// No windows.h; the host test compiles it.
namespace x3m::window_trace::core {
constexpr unsigned snapshot_frames = 120;  // Presents with a cursor_snapshot after a transition
constexpr std::uint32_t ring_ms = 4000;    // entries older than this at a flush are dropped
constexpr std::uint32_t setcursor_interval_ms = 250;
constexpr std::uint32_t wm_move = 0x0003, wm_size = 0x0005, wm_activate = 0x0006, wm_setfocus = 0x0007, wm_killfocus = 0x0008,
    wm_activateapp = 0x001c, wm_cancelmode = 0x001f, wm_setcursor = 0x0020, wm_mouseactivate = 0x0021, wm_windowposchanged = 0x0047,
    wm_displaychange = 0x007e, wm_ncmousemove = 0x00a0, wm_syscommand = 0x0112, wm_mousemove = 0x0200, wm_capturechanged = 0x0215;

// The first group of section 3.2: each one is a transition (flushes the ring and starts a snapshot burst).
inline bool transition_message(std::uint32_t m) {
    switch (m) {
    case wm_activateapp: case wm_activate: case wm_setfocus: case wm_killfocus: case wm_mouseactivate: case wm_capturechanged:
    case wm_cancelmode: case wm_syscommand: case wm_size: case wm_move: case wm_windowposchanged: case wm_displaychange: return true;
    default: return false;
    }
}
inline const char* message_name(std::uint32_t m) {
    switch (m) {
    case wm_activateapp: return "WM_ACTIVATEAPP"; case wm_activate: return "WM_ACTIVATE"; case wm_setfocus: return "WM_SETFOCUS";
    case wm_killfocus: return "WM_KILLFOCUS"; case wm_mouseactivate: return "WM_MOUSEACTIVATE"; case wm_capturechanged: return "WM_CAPTURECHANGED";
    case wm_cancelmode: return "WM_CANCELMODE"; case wm_syscommand: return "WM_SYSCOMMAND"; case wm_size: return "WM_SIZE"; case wm_move: return "WM_MOVE";
    case wm_windowposchanged: return "WM_WINDOWPOSCHANGED"; case wm_displaychange: return "WM_DISPLAYCHANGE"; case wm_setcursor: return "WM_SETCURSOR";
    default: return "other";
    }
}
// WM_SETCURSOR summary: a row for the first message of each 250 ms window and whenever
// the handler's result, the hit-test or the trigger message changes; the rest are counted.
struct SetCursorSummary { bool any = false; std::uint32_t last_row_ms = 0, result = 0, hit = 0, trigger = 0, suppressed = 0; };
inline bool setcursor_row(SetCursorSummary& s, std::uint32_t now_ms, std::uint32_t result, std::uint32_t hit, std::uint32_t trigger) {
    const bool row = !s.any || now_ms - s.last_row_ms >= setcursor_interval_ms || result != s.result || hit != s.hit || trigger != s.trigger;
    if (row) { s.any = true; s.last_row_ms = now_ms; s.result = result; s.hit = hit; s.trigger = trigger; }
    else ++s.suppressed;
    return row;
}

// One Win32 cursor call by the game (the EXE's SetCursor / SetCursorPos imports),
// recorded by the light rows only when the value changes; the per-op counts
// cover every call. seq is written last (0 = empty slot).
struct CursorEvent {
    std::uint32_t seq = 0;
    std::uint32_t op = 0;       // 0 = SetCursor (a = handle, b = previous), 1 = SetCursorPos (a = x, b = y)
    std::uint32_t a = 0, b = 0;
    std::uint32_t result = 0;   // SetCursorPos BOOL
    std::uint32_t tick = 0;     // GetTickCount
    std::uint32_t thread = 0;
};
constexpr unsigned cursor_event_capacity = 64; // power of two
// set / pos: every call since the previous drain; dropped: changes the drain could not
// return (more than cursor_event_capacity since the previous drain, or overwritten while read).
struct CursorCounts { std::uint32_t set = 0, pos = 0, dropped = 0; };
}
