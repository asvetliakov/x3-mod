#pragma once
// Logging tiers (docs/architecture/logging-tiers.md): X3M_DEBUG=1 and
// X3M_PERF=1 are expanded here, inside the DLL, so a bare proxy without the
// launcher logs the same rows as the launcher's --debug / --perf. Every
// individual switch stays readable and is ORed with its group; an explicitly
// set, valid cadence value wins over the group's (the read sites keep their
// own parsers and ask cadence_default() only when no valid value was given).
// Header-only: the fixture builds that compile single proxy sources against
// stubs link nothing new. Read once per site at initialisation, never on a
// render path; integers only (no x87, no allocation). A render-path check
// reads the flags cached once by init() (initialize_log), never the environment.
#include <windows.h>
#include "config.h"

namespace x3m::log_tier {
inline bool env_flag(const wchar_t* name) noexcept {
    wchar_t value[4]{};
    return x3m::config::get(name, value, 4) == 1 && value[0] == L'1';
}
// --debug: every diagnostic whose only effects are log rows and reads on F8
// frames (family block and camera_state at stride 1, the censuses, traces),
// with no engine patch beyond the frame boundary (the ten frame-phase stamps).
inline bool debug() noexcept { return env_flag(L"X3M_DEBUG"); }
// --perf: the per-frame cost rows at stride 1, the frame-time and phase
// windows, the loading metrics and the FPS overlay (the cheap tier: the stand
// command carries it on every flight).
inline bool perf() noexcept { return env_flag(L"X3M_PERF"); }
// --draw-trace (2026-09-26): the heavy attribution, off in every group: the
// per-draw route cost fields (X3M_TELEMETRY_DRAW, two QPC reads per routed
// draw) and the engine-stamp families X3M_GAME_PHASES (threshold at its default
// unless set), X3M_PASS_PHASES, X3M_RESIDUAL_PHASES, X3M_LIGHT_PHASES,
// X3M_LOOP_PHASES, with the frame phases they pair with. It needs telemetry and
// the frame boundary from --perf or --debug (the launcher refuses it alone).
// X3M_SUBMIT_PHASES is in no group: it claims the lens traversal call the
// sun-occlusion default patches; it stays a fixture-only read.
inline bool draw_trace() noexcept { return env_flag(L"X3M_DRAW_TRACE"); }
// The three group flags, read once by init() at initialize_log (before any
// device exists) for the checks that run on the render path (the F8 guard on
// every Present): plain bools, no environment read per frame.
inline bool cached_debug = false, cached_perf = false, cached_draw_trace = false;
inline void init() noexcept { cached_debug = debug(); cached_perf = perf(); cached_draw_trace = draw_trace(); }
// X3M_TELEMETRY=1 or either group: the counters, the 1 Hz summaries, the
// loading-trace hook set and the family block's gate.
inline bool telemetry() noexcept { return env_flag(L"X3M_TELEMETRY") || perf() || debug(); }
// A boolean switch of a group (OR semantics: an explicit "0" does not defeat
// the group).
inline bool debug_flag(const wchar_t* name) noexcept { return env_flag(name) || debug(); }
inline bool perf_flag(const wchar_t* name) noexcept { return env_flag(name) || perf(); }
inline bool draw_trace_flag(const wchar_t* name) noexcept { return env_flag(name) || draw_trace(); }
// The fallback of a cadence knob whose variable is unset or invalid: the
// group's value when the group is on, else the default.
inline unsigned cadence_default(bool group, unsigned group_value, unsigned fallback) noexcept {
    return group ? group_value : fallback;
}
}
