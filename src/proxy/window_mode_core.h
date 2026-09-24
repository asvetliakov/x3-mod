#pragma once
#include <cstdint>

// --window-monitor-rect predicate (docs/architecture/window-mode-and-cursor-fix.md
// section 2.1): whether the proxy moves the game's device window from the
// monitor's work-area origin to the monitor rectangle at create_before /
// reset_before. Pure: no windows.h, so the host test compiles it as is; the
// Win32 reads and the one SetWindowPos live in window_mode.cpp.
//
// Moved only when every condition holds, checked in this order (the first
// failing one is the reason):
//  1. windowed device (Windowed == TRUE)                         else fullscreen
//  2. one window: hDeviceWindow NULL or equal to the focus window else foreign_window
//     and the window exists                                      else no_window
//  3. top-level (GetAncestor(GA_PARENT) is the desktop)           else child_window
//  4. the window belongs to the calling thread                    else foreign_thread
//  5. WS_POPUP without WS_CAPTION / WS_THICKFRAME and without the
//     border extended styles (the game's borderless style)        else not_popup / decorated
//  6. the window's monitor is known                               else no_monitor
//  7. back buffer == monitor size                                 else backbuffer_mismatch
//  8. window rect == monitor rect                                 -> noop (at_monitor_rect)
//  9. window rect == the game's own placement, the monitor-sized
//     rectangle at the work-area origin (0x4daf1b..0x4daf4e)       -> move (work_origin)
//     else                                                        refused (not_work_origin)
// On native Windows with a bottom taskbar the work area starts at the
// monitor origin, so condition 8 answers noop and nothing moves.
namespace x3m::window_mode::core {
struct Rect { std::int32_t left = 0, top = 0, right = 0, bottom = 0; };
inline bool operator==(const Rect& a, const Rect& b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }
inline bool operator!=(const Rect& a, const Rect& b) { return !(a == b); }
inline std::int64_t width(const Rect& r) { return std::int64_t(r.right) - r.left; }
inline std::int64_t height(const Rect& r) { return std::int64_t(r.bottom) - r.top; }

// Win32 style bits (winuser.h values; duplicated so this header stays windows.h-free).
constexpr std::uint32_t ws_popup = 0x80000000u, ws_caption = 0x00c00000u, ws_thickframe = 0x00040000u;
constexpr std::uint32_t ws_ex_dlgmodalframe = 0x00000001u, ws_ex_windowedge = 0x00000100u, ws_ex_clientedge = 0x00000200u, ws_ex_staticedge = 0x00020000u;
constexpr std::uint32_t decorated_style = ws_caption | ws_thickframe;
constexpr std::uint32_t decorated_exstyle = ws_ex_dlgmodalframe | ws_ex_windowedge | ws_ex_clientedge | ws_ex_staticedge;

struct Input {
    bool windowed = false;
    bool single_window = false;   // hDeviceWindow NULL or == focus window
    bool window_valid = false;    // IsWindow
    bool top_level = false;
    bool same_thread = false;
    std::uint32_t style = 0, exstyle = 0;
    bool monitor_known = false;
    Rect window, monitor, work;
    std::uint32_t backbuffer_width = 0, backbuffer_height = 0;
};
enum class Action : unsigned char { move, noop, refused };
struct Decision { Action action; const char* reason; };

// The rectangle 0x4dac90 gives the window: the work-area origin, the monitor's extents.
inline Rect game_placement(const Input& in) {
    Rect r;
    r.left = in.work.left; r.top = in.work.top;
    r.right = std::int32_t(std::int64_t(in.work.left) + width(in.monitor));
    r.bottom = std::int32_t(std::int64_t(in.work.top) + height(in.monitor));
    return r;
}
inline Decision decide(const Input& in) {
    if (!in.windowed) return {Action::refused, "fullscreen"};
    if (!in.single_window) return {Action::refused, "foreign_window"};
    if (!in.window_valid) return {Action::refused, "no_window"};
    if (!in.top_level) return {Action::refused, "child_window"};
    if (!in.same_thread) return {Action::refused, "foreign_thread"};
    if (!(in.style & ws_popup)) return {Action::refused, "not_popup"};
    if ((in.style & decorated_style) || (in.exstyle & decorated_exstyle)) return {Action::refused, "decorated"};
    if (!in.monitor_known || width(in.monitor) <= 0 || height(in.monitor) <= 0) return {Action::refused, "no_monitor"};
    if (std::int64_t(in.backbuffer_width) != width(in.monitor) || std::int64_t(in.backbuffer_height) != height(in.monitor))
        return {Action::refused, "backbuffer_mismatch"};
    if (in.window == in.monitor) return {Action::noop, "at_monitor_rect"};
    if (in.window == game_placement(in)) return {Action::move, "work_origin"};
    return {Action::refused, "not_work_origin"};
}
inline const char* action_name(Action a) { return a == Action::move ? "moved" : a == Action::noop ? "noop" : "refused"; }
}
