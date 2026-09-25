#pragma once
// Logging tiers (docs/architecture/logging-tiers.md): X3M_DEBUG=1 and
// X3M_PERF=1 are expanded here, inside the DLL, so a bare proxy without the
// launcher logs the same rows as the launcher's --debug / --perf. Every
// individual switch stays readable and is ORed with its group; an explicitly
// set, valid cadence value wins over the group's (the read sites keep their
// own parsers and ask cadence_default() only when no valid value was given).
// Header-only: the fixture builds that compile single proxy sources against
// stubs link nothing new. Read once per site at initialisation, never on a
// render path; integers only (no x87, no allocation).
#include <windows.h>

namespace x3m::log_tier {
inline bool env_flag(const wchar_t* name) noexcept {
    wchar_t value[4]{};
    return GetEnvironmentVariableW(name, value, 4) == 1 && value[0] == L'1';
}
// --debug: every diagnostic whose only effects are log rows and reads on F8
// frames (family block and camera_state at stride 1, the censuses, traces).
inline bool debug() noexcept { return env_flag(L"X3M_DEBUG"); }
// --perf: the per-frame cost rows at stride 1, the frame-time and phase
// windows, the loading metrics and the FPS overlay.
inline bool perf() noexcept { return env_flag(L"X3M_PERF"); }
// X3M_TELEMETRY=1 or either group: the counters, the 1 Hz summaries, the
// loading-trace hook set and the family block's gate.
inline bool telemetry() noexcept { return env_flag(L"X3M_TELEMETRY") || perf() || debug(); }
// A boolean switch of a group (OR semantics: an explicit "0" does not defeat
// the group).
inline bool debug_flag(const wchar_t* name) noexcept { return env_flag(name) || debug(); }
inline bool perf_flag(const wchar_t* name) noexcept { return env_flag(name) || perf(); }
// The fallback of a cadence knob whose variable is unset or invalid: the
// group's value when the group is on, else the default.
inline unsigned cadence_default(bool group, unsigned group_value, unsigned fallback) noexcept {
    return group ? group_value : fallback;
}
}
