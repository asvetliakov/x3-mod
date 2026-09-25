#pragma once
#include <cstdint>

// --cursor-reassert state machine (docs/architecture/window-mode-and-cursor-fix.md
// section 3.3). Pure: no windows.h; the host test compiles it and compares it
// with its Python twin, the Wine fixture drives it with synthetic gates and the
// real Win32 sequence. cursor_reassert.cpp gathers the gates and runs the
// sequence on the window thread.
//
// idle --(WM_ACTIVATE active/click-active, or WM_ACTIVATEAPP wParam != 0)--> armed
// idle --(launch: once per process, when the hooks are installed at device creation)--> armed
// armed by launch --(an arming message)--> armed by that message, a fresh window (the launch arm
//                                           is replaced, it never fired)
// armed, at each Present: all gates pass -> fire once -> idle;
//                         a gate fails   -> wait (window_frames Presents at most),
//                                           then refuse (reason = the last failing gate) -> idle
// fired with an unexpected ShowCursor pair or a changed Win32 end state -> disabled (process)
namespace x3m::cursor_reassert::core {
constexpr unsigned window_frames = 120;
constexpr std::uint32_t wm_activate = 0x0006, wm_activateapp = 0x001c;
constexpr std::uint32_t cursor_showing = 0x00000001u; // CURSORINFO.flags CURSOR_SHOWING
// Machine::armed_by for the launch arm: not a window message (message numbers are below 0x10000).
constexpr std::uint32_t arm_launch_source = 0x00010000u;

enum class State : unsigned char { idle, armed, disabled };
struct Machine {
    State state = State::idle;
    unsigned frames_left = 0;
    std::uint32_t armed_by = 0;         // the arming message, or arm_launch_source
    unsigned arms = 0, fires = 0, refusals = 0, mismatches = 0;
};
// The arming messages: WM_ACTIVATE with LOWORD(wParam) != WA_INACTIVE, WM_ACTIVATEAPP with wParam != 0.
inline bool arming_message(std::uint32_t message, std::uint32_t wparam) {
    return (message == wm_activate && (wparam & 0xffffu) != 0) || (message == wm_activateapp && wparam != 0);
}
// Observer step (the WH_CALLWNDPROC hook, window thread). True when this message armed an idle
// machine or replaced a pending launch arm.
inline bool observe(Machine& m, std::uint32_t message, std::uint32_t wparam) {
    if (!arming_message(message, wparam)) return false;
    if (m.state != State::idle && !(m.state == State::armed && m.armed_by == arm_launch_source)) return false;
    m.state = State::armed; m.frames_left = window_frames; m.armed_by = message; ++m.arms;
    return true;
}
// Launch step (device creation, window thread; the caller keeps it to once per process).
// True when it armed an idle machine.
inline bool arm_launch(Machine& m) {
    if (m.state != State::idle) return false;
    m.state = State::armed; m.frames_left = window_frames; m.armed_by = arm_launch_source; ++m.arms;
    return true;
}
inline const char* arm_source(std::uint32_t armed_by) { return armed_by == arm_launch_source ? "launch" : "activate"; }
inline const char* arm_message(std::uint32_t armed_by) {
    return armed_by == wm_activate ? "WM_ACTIVATE" : armed_by == wm_activateapp ? "WM_ACTIVATEAPP" : "none";
}
struct Gates {
    bool same_thread = false;       // the Present runs on the window's thread
    bool foreground = false;        // GetForegroundWindow() == window
    bool visible = false;           // IsWindowVisible and not IsIconic
    bool cursor_hidden = false;     // GetCursorInfo succeeded and CURSOR_SHOWING is clear
    bool pointer_in_client = false; // GetCursorPos inside the client rectangle (screen coordinates)
    bool clip_is_client = false;    // GetClipCursor equals the client rectangle
};
enum class Step : unsigned char { none, wait, fire, refuse };
struct Decision { Step step; const char* reason; };
inline const char* failing_gate(const Gates& g) {
    if (!g.same_thread) return "thread";
    if (!g.foreground) return "foreground";
    if (!g.visible) return "not_visible";
    if (!g.cursor_hidden) return "cursor_visible";
    if (!g.pointer_in_client && !g.clip_is_client) return "pointer_outside";
    return nullptr;
}
// Present step. fire leaves the machine idle (one firing per activation).
inline Decision present(Machine& m, const Gates& g) {
    if (m.state != State::armed) return {Step::none, nullptr};
    if (const char* reason = failing_gate(g)) {
        if (m.frames_left) --m.frames_left;
        if (m.frames_left) return {Step::wait, reason};
        m.state = State::idle; ++m.refusals;
        return {Step::refuse, reason};
    }
    m.state = State::idle; m.frames_left = 0; ++m.fires;
    return {Step::fire, "gates_passed"};
}
// The balanced sequence. Api: void* arrow(); void* set_cursor(void*); int show_cursor(bool).
struct Sequence { void* arrow = nullptr; void* previous = nullptr; int up = 0, down = 0; };
template<class Api> Sequence run_sequence(Api& api) {
    Sequence s;
    s.arrow = api.arrow();
    s.previous = api.set_cursor(s.arrow); // handle change while hidden: no driver call
    s.up = api.show_cursor(true);         // count -1 -> 0: the driver unhides
    s.down = api.show_cursor(false);      // count 0 -> -1: the driver runs a hide transition
    api.set_cursor(s.previous);           // the game's handle back
    return s;
}
// Expected pairs: started hidden by a count of -1 (dinput exclusive acquire) -> (0, -1);
// started at 0 (no acquire) -> (1, 0). Anything else, or a Win32 end state that differs
// from the start, is a mismatch and disables the option for the process.
inline bool expected_counts(int up, int down) { return (up == 0 && down == -1) || (up == 1 && down == 0); }
inline bool balanced(const Sequence& s, std::uint32_t flags_before, std::uint32_t flags_after, const void* cursor_before, const void* cursor_after) {
    return expected_counts(s.up, s.down) && flags_before == flags_after && cursor_before == cursor_after;
}
inline void after_fire(Machine& m, bool was_balanced) {
    if (!was_balanced) { m.state = State::disabled; ++m.mismatches; }
}
inline const char* step_name(Step s) { return s == Step::fire ? "fired" : s == Step::refuse ? "refused" : s == Step::wait ? "wait" : "none"; }
}
