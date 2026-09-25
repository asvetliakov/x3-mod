// Wine fixture of --cursor-reassert and the window-thread hooks it shares with
// --window-trace (src/proxy/cursor_reassert.cpp + cursor_reassert_core.h,
// src/proxy/window_trace.cpp + window_trace_core.h;
// docs/architecture/window-mode-and-cursor-fix.md sections 3.2, 3.3 and 4).
//  1. The sequence's order and counts with a recording stand-in (count -1, 0, -2).
//  2. The hooks on a probe window: WH_CALLWNDPROCRET sees the handler's magic
//     result, the message count matches, the handler's last error survives.
//  2a. Launch: the first complete attach arms once (armed_by=launch) and the
//     trace's first Present flushes the ring's first entries with one
//     cursor_snapshot; a launch arm with the window not foreground waits and is
//     refused after 120 frames without firing; a launch arm with the gates
//     holding fires once, balanced, armed_by=launch (the second launch arm is
//     driven directly: one per process in production).
//  3. Arming: WM_ACTIVATE inactive does not arm, active arms once (also over a
//     pending launch arm, with a fresh window), WM_ACTIVATEAPP while armed does not arm again.
//  4. The production present step with synthetic gates and the real Win32
//     sequence in the game's state (SetCursor(NULL), ShowCursor count -1): waits
//     while a gate fails, fires once when all pass with up = 0, down = -1 and
//     the GetCursorInfo flags/handle, cursor position and last error unchanged,
//     never again for the same activation; the count-0 variant (1, 0); a
//     120-frame refusal; a count of -2 disables the option for the process.
//  5. The trace ring: transition rows with results, the WM_SETCURSOR summary,
//     the posted mouse-move count, the flush and a cursor_snapshot.
//  6. Removal: UnhookWindowsHookEx TRUE for all three, nothing observed afterwards.
//  7. (runs after 1) The light SetCursor/SetCursorPos rows' change ring
//     (src/proxy/loading_trace_light.cpp) through the real wrappers: repeats not
//     recorded, drain order, the position event, more than 64 changes between
//     drains (dropped counter), the caller's last error kept.
//  8. (runs after the refusal in 4) The real present() gate collection with the
//     fixture's own window shown under the pointer and foreground: the gates, the
//     GetCursorInfo flags at show count -1 (a witness, recorded not asserted) and
//     an unchanged Win32 end state whatever present() decided.
// Built by CMake (target cursor_reassert_fixture); run through
// verification/probe/run_cursor_reassert.py under wine_lock.py. Never launches the game.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "../../src/proxy/cursor_reassert.h"
#include "../../src/proxy/window_trace.h"
#include "../../src/proxy/loading_trace_light.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace x3m {
std::vector<std::string> lines;
void log(const char* format, ...) {
    char buffer[2048];
    va_list args; va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    lines.emplace_back(buffer);
    std::printf("LOG %s\n", buffer);
    std::fflush(stdout);
}
}
namespace cr = x3m::cursor_reassert;
namespace wt = x3m::window_trace;
namespace light = x3m::loading_trace::light;
using x3m::window_trace::core::CursorEvent;
using x3m::window_trace::core::CursorCounts;

static unsigned checks = 0, failures = 0;
static bool require(const char* label, bool value) { ++checks; if (!value) ++failures; std::printf("CHECK %s %s\n", label, value ? "PASS" : "FAIL"); std::fflush(stdout); return value; }
// The newest log line starting with `prefix` at or after index `from` that contains every needle.
static bool logged(size_t from, const char* prefix, std::initializer_list<const char*> needles) {
    for (size_t i = x3m::lines.size(); i-- > from;) {
        const auto& line = x3m::lines[i];
        if (line.compare(0, std::strlen(prefix), prefix)) continue;
        bool all = true;
        for (const char* n : needles) all = all && line.find(n) != std::string::npos;
        if (all) return true;
    }
    return false;
}
struct Recorder {
    std::string order; int count; void* handle;
    void* arrow() { order += 'L'; return reinterpret_cast<void*>(0xa0); }
    void* set_cursor(void* h) { order += 'S'; void* p = handle; handle = h; return p; }
    int show_cursor(bool show) { order += show ? '+' : '-'; count += show ? 1 : -1; return count; }
};
constexpr UINT kProbe = WM_APP + 7;
constexpr LRESULT kMagic = 0x5a5a1234;
static LRESULT CALLBACK probe_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == kProbe) { SetLastError(0x7777); return kMagic; }
    if (m == WM_ACTIVATE || m == WM_ACTIVATEAPP) return 0; // the game returns 0 without DefWindowProc (0x4d3620)
    if (m == WM_SETCURSOR) { SetCursor(nullptr); return 1; }
    return DefWindowProcA(h, m, w, l);
}
static void pump() { MSG m; while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); } }
static cr::core::Gates all_pass() { cr::core::Gates g; g.same_thread = g.foreground = g.visible = g.cursor_hidden = g.pointer_in_client = true; return g; }
struct CursorState { BOOL ok; DWORD flags; HCURSOR cursor; POINT pos; };
static CursorState cursor_state() { CURSORINFO ci{}; ci.cbSize = sizeof ci; CursorState s{}; s.ok = GetCursorInfo(&ci); s.flags = ci.flags; s.cursor = ci.hCursor; GetCursorPos(&s.pos); return s; }
static bool same(const CursorState& a, const CursorState& b) { return a.ok && b.ok && a.flags == b.flags && a.cursor == b.cursor && a.pos.x == b.pos.x && a.pos.y == b.pos.y; }
// The thread's show count, read with a balanced pair (+1 then -1).
static int show_count() { const int up = ShowCursor(TRUE); ShowCursor(FALSE); return up - 1; }
static bool ascending(const CursorEvent* e, unsigned n, std::uint32_t last) {
    for (unsigned i = 0; i < n; ++i) if (e[i].seq != last - (n - 1) + i) return false;
    return true;
}
static void cursor_ring_cases() {
    light::initialize();
    light::set_original(static_cast<unsigned>(x3m::loading_trace::Operation::CursorSet), reinterpret_cast<PVOID>(&SetCursor));
    light::set_original(static_cast<unsigned>(x3m::loading_trace::Operation::CursorPosition), reinterpret_cast<PVOID>(&SetCursorPos));
    light::cursor_observe(true);
    CursorEvent events[x3m::window_trace::core::cursor_event_capacity];
    CursorCounts counts;
    std::uint32_t seen = 0, newest = 0;
    const HCURSOR arrow = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    light::cursor_drain(seen, events, 64, &seen, &counts); // baseline
    // 10 changes (alternating arrow / NULL) and 3 repeats that must not be recorded.
    for (int i = 0; i < 10; ++i) { light::cursor_set(i & 1 ? nullptr : arrow); if (i == 4) { light::cursor_set(arrow); light::cursor_set(arrow); light::cursor_set(arrow); } }
    unsigned n = light::cursor_drain(seen, events, 64, &newest, &counts);
    bool alternating = true;
    for (unsigned i = 0; i < n; ++i) alternating = alternating && events[i].op == 0 && events[i].a == (i & 1 ? 0u : std::uint32_t(reinterpret_cast<uintptr_t>(arrow)));
    std::printf("DRAIN case=ten n=%u dropped=%u set=%u newest=%u\n", n, counts.dropped, counts.set, newest);
    require("drain_ten_in_order", n == 10 && counts.dropped == 0 && counts.set == 13 && newest == seen + 10 && ascending(events, n, newest) && alternating);
    seen = newest;
    // One position event at the current pointer position (no visible warp), then an identical repeat.
    POINT p{}; GetCursorPos(&p);
    const BOOL moved = light::cursor_position(p.x, p.y); light::cursor_position(p.x, p.y);
    n = light::cursor_drain(seen, events, 64, &newest, &counts);
    require("drain_position_event", moved && n == 1 && counts.pos == 2 && counts.set == 0 && events[0].op == 1 && int(events[0].a) == p.x && int(events[0].b) == p.y
            && events[0].result == 1 && events[0].seq == newest);
    seen = newest;
    // 100 changes between two drains: the newest 64 in order, 36 dropped.
    for (int i = 0; i < 100; ++i) light::cursor_set(i & 1 ? nullptr : arrow); // the previous step left NULL: 100 changes, ending on NULL
    n = light::cursor_drain(seen, events, 64, &newest, &counts);
    std::printf("DRAIN case=past_capacity n=%u dropped=%u set=%u first=%u newest=%u\n", n, counts.dropped, counts.set, n ? events[0].seq : 0, newest);
    require("drain_past_capacity_dropped", n == 64 && counts.dropped == 36 && counts.set == 100 && newest == seen + 100 && ascending(events, n, newest));
    seen = newest;
    SetLastError(0x2468);
    light::cursor_set(arrow);
    const DWORD error = GetLastError();
    n = light::cursor_drain(seen, events, 64, &newest, &counts);
    require("light_row_last_error_kept", error == 0x2468 && n == 1);
    seen = newest;
    n = light::cursor_drain(seen, events, 64, &newest, &counts);
    require("drain_empty_after", n == 0 && counts.dropped == 0 && counts.set == 0 && newest == seen);
    light::cursor_observe(false);
    SetCursor(nullptr);
}

int main() {
    std::printf("FIXTURE cursor_reassert\n");
    // 1. Sequence order and counts with the recording stand-in.
    {
        Recorder r{"", -1, nullptr};
        const auto s = cr::core::run_sequence(r);
        require("sequence_order", r.order == "LS+-S" && s.previous == nullptr && r.handle == nullptr && s.arrow == reinterpret_cast<void*>(0xa0));
        require("sequence_counts_hidden", s.up == 0 && s.down == -1 && r.count == -1 && cr::core::expected_counts(s.up, s.down));
        Recorder z{"", 0, reinterpret_cast<void*>(0x55)};
        const auto t = cr::core::run_sequence(z);
        require("sequence_counts_shown", t.up == 1 && t.down == 0 && z.count == 0 && z.handle == reinterpret_cast<void*>(0x55) && cr::core::expected_counts(t.up, t.down));
        Recorder d{"", -2, nullptr};
        const auto u = cr::core::run_sequence(d);
        require("sequence_counts_deep_rejected", u.up == -1 && u.down == -2 && !cr::core::expected_counts(u.up, u.down));
    }
    cursor_ring_cases();
    SetEnvironmentVariableW(L"X3M_CURSOR_REASSERT", L"1");
    SetEnvironmentVariableW(L"X3M_WINDOW_TRACE", L"1");
    cr::initialize();
    wt::initialize(true, nullptr);
    require("options_enabled", cr::enabled() && wt::enabled() && cr::machine().state == cr::core::State::idle);

    WNDCLASSA wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = probe_proc; wc.hInstance = GetModuleHandleA(nullptr); wc.lpszClassName = "X3MCursorFixture";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, wc.lpszClassName, WS_POPUP, 0, 0, 320, 200, nullptr, nullptr, wc.hInstance, nullptr);
    require("probe_window", hwnd != nullptr);
    if (!hwnd) { std::printf("RESULT checks=%u failed=%u FAIL\n", checks, failures); return 1; }

    // 2. Hooks.
    wt::attach(hwnd, 1);
    require("hooks_installed", wt::observed().installed && logged(0, "window_trace_hooks", {"installed=1", "trace=1", "reassert=1", "call=1 ret=1 get=1", "pinned=1"}));
    {
        const size_t before_second = x3m::lines.size();
        wt::attach(hwnd, 2);
        require("second_device_logged_already_hooked", wt::observed().installed
                && logged(before_second, "window_trace_hooks", {"device=2", "installed=0", "reason=already_hooked", "hooked_device=1"}));
    }
    const unsigned rets0 = wt::observed().rets, calls0 = wt::observed().calls;
    SetLastError(0);
    const LRESULT magic = SendMessageA(hwnd, kProbe, 0, 0);
    const DWORD handler_error = GetLastError();
    const auto seen = wt::observed();
    require("callwndprocret_sees_result", magic == kMagic && seen.rets == rets0 + 1 && seen.calls == calls0 + 1 && seen.last_message == kProbe && seen.last_result == kMagic);
    require("handler_last_error_kept", handler_error == 0x7777);
    for (int i = 0; i < 10; ++i) SendMessageA(hwnd, kProbe, 0, 0);
    require("message_count_matches", wt::observed().rets == rets0 + 11 && wt::observed().calls == calls0 + 11);

    // 2a. Launch arm and the first-Present flush.
    require("launch_armed_at_attach", cr::machine().arms == 1 && cr::machine().state == cr::core::State::armed
            && cr::machine().armed_by == cr::core::arm_launch_source && logged(0, "window_trace_hooks", {"device=1", "installed=1", "launch_arm=1"})
            && logged(0, "window_trace_hooks", {"device=2", "reason=already_hooked"}));
    // The game's cursor state on this thread: handle NULL, show count -1 (dinput's exclusive acquire).
    SetCursor(nullptr);
    const int initial = ShowCursor(FALSE);
    std::printf("COUNT initial_after_hide=%d\n", initial);
    require("game_state_count_minus_one", initial == -1);
    unsigned long long frame = 100;
    // One ring entry before the first Present (hit 2: a later hit-1 row is a change, not suppressed).
    SendMessageA(hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(hwnd), MAKELPARAM(HTCAPTION, WM_MOUSEMOVE));
    const size_t before_first = x3m::lines.size();
    wt::present(hwnd, 1, ++frame, 0); // the real gates: the probe window is hidden, so the launch arm waits
    require("first_present_flush_and_snapshot", logged(before_first, "window_trace_flush", {"reason=first_present"})
            && logged(before_first, "window_msg ", {"name=WM_SETCURSOR", "hit=2"}) && logged(before_first, "cursor_snapshot", {"burst=0"})
            && logged(before_first, "cursor_reassert_arm", {"armed_by=launch", "message=none", "arms=1"}) && cr::machine().state == cr::core::State::armed);
    cr::core::Gates waiting = all_pass(); waiting.foreground = false;
    {
        const size_t before = x3m::lines.size();
        unsigned waits = 0; cr::core::Step last = cr::core::Step::none;
        for (unsigned i = 0; i < cr::core::window_frames && cr::machine().state == cr::core::State::armed; ++i) {
            last = cr::present_with(hwnd, ++frame, waiting).step; waits += last == cr::core::Step::wait;
        }
        require("launch_not_foreground_refused", last == cr::core::Step::refuse && waits == cr::core::window_frames - 2 && cr::machine().fires == 0
                && cr::machine().refusals == 1 && cr::machine().state == cr::core::State::idle
                && logged(before, "cursor_reassert frame=", {"action=refused", "reason=foreground", "armed_by=launch"}));
        cr::arm_launch(); // a second process's launch arm, driven directly
        const CursorState start = cursor_state();
        SetLastError(0x2468);
        const size_t before_fire = x3m::lines.size();
        const auto fired = cr::present_with(hwnd, ++frame, all_pass());
        const DWORD error = GetLastError();
        const CursorState end = cursor_state();
        require("launch_fires_once_balanced", fired.step == cr::core::Step::fire && cr::machine().arms == 2 && cr::machine().fires == 1
                && cr::machine().state == cr::core::State::idle && same(start, end) && error == 0x2468
                && logged(before_fire, "cursor_reassert_arm", {"armed_by=launch", "arms=2"})
                && logged(before_fire, "cursor_reassert frame=", {"action=fired", "armed_by=launch", " up=0 down=-1 ", "balanced=1", "disabled=0", "message=none"}));
        require("launch_no_second_fire", cr::present_with(hwnd, ++frame, all_pass()).step == cr::core::Step::none && cr::machine().fires == 1);
    }

    // 3. Arming.
    const unsigned arms_base = cr::machine().arms;
    SendMessageA(hwnd, WM_ACTIVATE, WA_INACTIVE, 0);
    require("inactive_does_not_arm", cr::machine().arms == arms_base && cr::machine().state == cr::core::State::idle);
    cr::arm_launch();
    cr::present_with(hwnd, ++frame, waiting); // the pending launch arm has used one frame
    const unsigned left_launch = cr::machine().frames_left;
    SendMessageA(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
    require("activate_replaces_pending_launch_arm", left_launch == cr::core::window_frames - 1 && cr::machine().arms == arms_base + 2
            && cr::machine().state == cr::core::State::armed && cr::machine().armed_by == WM_ACTIVATE && cr::machine().frames_left == cr::core::window_frames);
    SendMessageA(hwnd, WM_ACTIVATEAPP, TRUE, 0);
    require("activate_arms_once", cr::machine().arms == arms_base + 2 && cr::machine().state == cr::core::State::armed && cr::machine().armed_by == WM_ACTIVATE);

    // 4. The present step in the game's cursor state (set in 2a).
    char arms_needle[32];
    std::snprintf(arms_needle, sizeof arms_needle, "arms=%u", arms_base + 2);
    const size_t before_wait = x3m::lines.size();
    bool waited = true;
    for (int i = 0; i < 5; ++i) waited = waited && cr::present_with(hwnd, ++frame, waiting).step == cr::core::Step::wait;
    require("waits_while_gate_fails", waited && cr::machine().fires == 1 && cr::machine().state == cr::core::State::armed
            && logged(before_wait, "cursor_reassert_arm", {"armed_by=activate", "message=WM_ACTIVATE", arms_needle}));
    const CursorState start = cursor_state();
    SetLastError(0x1357);
    const size_t before_fire = x3m::lines.size();
    const auto fired = cr::present_with(hwnd, ++frame, all_pass());
    const DWORD fire_error = GetLastError();
    const CursorState end = cursor_state();
    require("fires_once_balanced", fired.step == cr::core::Step::fire && cr::machine().fires == 2 && cr::machine().state == cr::core::State::idle
            && logged(before_fire, "cursor_reassert frame=", {"action=fired", "armed_by=activate", " up=0 down=-1 ", "balanced=1", "disabled=0", "message=WM_ACTIVATE"}));
    require("win32_end_state_equals_start", same(start, end) && fire_error == 0x1357);
    require("no_second_fire", cr::present_with(hwnd, ++frame, all_pass()).step == cr::core::Step::none && cr::machine().fires == 2);
    std::printf("CURSOR start_flags=%lu start_cursor=%p end_flags=%lu end_cursor=%p x=%ld y=%ld\n", static_cast<unsigned long>(start.flags),
                static_cast<void*>(start.cursor), static_cast<unsigned long>(end.flags), static_cast<void*>(end.cursor), start.pos.x, start.pos.y);
    // The real sequence alone, both starting counts.
    {
        const CursorState a = cursor_state();
        const auto s = cr::run_win32_sequence();
        const CursorState b = cursor_state();
        require("real_sequence_hidden", s.up == 0 && s.down == -1 && same(a, b));
        const int shown = ShowCursor(TRUE);
        const CursorState c = cursor_state();
        const auto t = cr::run_win32_sequence();
        const CursorState d = cursor_state();
        require("real_sequence_count_zero", shown == 0 && t.up == 1 && t.down == 0 && same(c, d));
        ShowCursor(FALSE); // back to -1
    }
    // Refusal after 120 frames.
    SendMessageA(hwnd, WM_ACTIVATE, WA_CLICKACTIVE, 0);
    const size_t before_refuse = x3m::lines.size();
    unsigned waits = 0; cr::core::Step last = cr::core::Step::none;
    for (unsigned i = 0; i < cr::core::window_frames; ++i) { last = cr::present_with(hwnd, ++frame, waiting).step; waits += last == cr::core::Step::wait; }
    require("refuses_after_window", cr::machine().arms == arms_base + 3 && waits == cr::core::window_frames - 1 && last == cr::core::Step::refuse && cr::machine().refusals == 2
            && cr::machine().state == cr::core::State::idle && logged(before_refuse, "cursor_reassert frame=", {"action=refused", "reason=foreground", "armed_by=activate"}));
    // 8. The real gate collection with this window shown under the pointer and brought to the foreground.
    {
        const unsigned arms_before = cr::machine().arms, fires_before = cr::machine().fires;
        POINT p{}; GetCursorPos(&p);
        SetWindowPos(hwnd, HWND_TOP, p.x - 160, p.y - 100, 320, 200, SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
        const DWORD start = GetTickCount();
        while (GetTickCount() - start < 500) { pump(); Sleep(10); }
        if (cr::machine().state != cr::core::State::armed) SendMessageA(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
        const int count = show_count();
        CURSORINFO ci{}; ci.cbSize = sizeof ci; const BOOL ci_ok = GetCursorInfo(&ci);
        const auto gates = cr::collect_gates(hwnd);
        const CursorState before = cursor_state();
        cr::present(hwnd, ++frame);
        const CursorState after = cursor_state();
        const int count_after = show_count();
        const bool fired = cr::machine().fires != fires_before;
        std::printf("REAL_GATES same_thread=%d foreground=%d visible=%d cursor_ok=%d cursor_flags=%lu show_count=%d cursor_hidden=%d pointer_in_client=%d clip_is_client=%d "
                    "armed_by_show=%u step=%s state=%s\n", int(gates.same_thread), int(gates.foreground), int(gates.visible), int(ci_ok), static_cast<unsigned long>(ci.flags),
                    count, int(gates.cursor_hidden), int(gates.pointer_in_client), int(gates.clip_is_client), cr::machine().arms - arms_before, fired ? "fired" : "not_fired",
                    cr::machine().state == cr::core::State::armed ? "armed" : cr::machine().state == cr::core::State::idle ? "idle" : "disabled");
        require("real_gates_collected", gates.same_thread && count == -1);
        require("real_present_end_state_unchanged", same(before, after) && count_after == count && cr::machine().state != cr::core::State::disabled);
        ShowWindow(hwnd, SW_HIDE);
        pump();
    }
    // An unexpected count (-2) disables the option for the process.
    const unsigned arms_before_mismatch = cr::machine().arms;
    SendMessageA(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
    const int deeper = ShowCursor(FALSE);
    const size_t before_mismatch = x3m::lines.size();
    const auto mismatch = cr::present_with(hwnd, ++frame, all_pass());
    require("mismatch_disables", deeper == -2 && mismatch.step == cr::core::Step::fire && cr::machine().state == cr::core::State::disabled && !cr::enabled()
            && logged(before_mismatch, "cursor_reassert frame=", {" up=-1 down=-2 ", "balanced=0", "disabled=1"}));
    const unsigned arms_disabled = cr::machine().arms;
    SendMessageA(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
    require("disabled_never_rearms", arms_disabled <= arms_before_mismatch + 1 && cr::machine().arms == arms_disabled && cr::machine().state == cr::core::State::disabled);
    ShowCursor(TRUE); ShowCursor(TRUE); // back to 0

    // 5. The trace ring (cursor_reassert is disabled now, so window_trace::present runs the trace alone).
    const size_t before_flush = x3m::lines.size();
    wt::present(hwnd, 1, ++frame, 0);
    require("flush_on_transition", logged(before_flush, "window_trace_flush", {"reason=transition"}) && logged(before_flush, "window_msg ", {"name=WM_ACTIVATE", "result=00000000"})
            && logged(before_flush, "window_msg ", {"name=WM_ACTIVATEAPP"}) && logged(before_flush, "cursor_snapshot", {"burst=0"}));
    for (int i = 0; i < 3; ++i) PostMessageA(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(10 + i, 10));
    pump();
    for (int i = 0; i < 5; ++i) SendMessageA(hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(hwnd), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    const size_t before_quiet = x3m::lines.size();
    wt::present(hwnd, 1, ++frame, 0);
    const bool quiet = !logged(before_quiet, "window_trace_flush", {}) && !logged(before_quiet, "window_msg", {});
    SendMessageA(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(320, 200));
    const size_t before_second = x3m::lines.size();
    wt::present(hwnd, 1, ++frame, 0);
    require("no_flush_without_transition", quiet);
    require("setcursor_summary_and_frame_counts", logged(before_second, "window_msg ", {"name=WM_SETCURSOR", "hit=1", "trigger=0200", "result=00000001"})
            && logged(before_second, "window_msg_frame", {"mousemove=3", "setcursor=5"}) && logged(before_second, "window_msg ", {"name=WM_SIZE"})
            && logged(before_second, "window_trace_flush", {"reason=transition"}));
    const size_t before_marker = x3m::lines.size();
    wt::present(hwnd, 1, ++frame, 1);
    require("flush_on_marker", logged(before_marker, "window_trace_flush", {"reason=marker"}));

    // 6. Removal.
    wt::detach(1);
    const auto removed = wt::observed();
    require("unhook_all_true", !removed.installed && removed.unhook_call && removed.unhook_ret && removed.unhook_get);
    const unsigned rets_after = removed.rets;
    SendMessageA(hwnd, kProbe, 0, 0);
    require("nothing_observed_after_removal", wt::observed().rets == rets_after);
    wt::shutdown(); // idempotent
    DestroyWindow(hwnd);
    std::printf("RESULT checks=%u failed=%u %s\n", checks, failures, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
