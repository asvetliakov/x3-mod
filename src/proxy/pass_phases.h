#pragma once
#include <atomic>
#include <cstdint>
#include "engine_patch.h"
#include "pass_phases_core.h"

// Default-off per-draw effect-pass stamps (X3M_PASS_PHASES=1, launcher
// --pass-phases, requires --telemetry and --frame-phases). Four byte-verified
// sites in the D3DX pass loop of the material submission routine 0x004c0150
// (pass_phase_sites.h) split every material draw into pass-apply (BeginPass),
// the device draw and EndPass. At ~4,024 dispatches per busy frame the shared
// game-phase stub (PreserveCpuState, FNSAVE/FRSTOR) is over budget, so the
// group has its own lean stub: flags, EAX/ECX/EDX and XMM0-7 saved around a
// handler under LightCallBoundary (MXCSR + LastError; the handler is x87-free,
// verification/probe/check_no_x87.py walks x3m_pass_phase_enter) that reads
// QueryPerformanceCounter once and accumulates. The window reduction runs at
// the frame boundary from frame_phases::detail::frame_impl under its owner
// guard: one `pass_phases` line per 300-frame window
// (docs/verification/sampling-profiler.md, "Pass phases"). Off, nothing is
// installed and the per-frame cost is one relaxed load.
namespace x3m::pass_phases {
bool initialize(); // after frame_phases::initialize, while the install window is open
extern std::atomic<bool> active;
namespace detail {
void frame_impl(std::uint64_t frame, bool sampled, std::uint64_t view_submit_us) noexcept;
}
// Frame boundary, called by frame_phases::detail::frame_impl on its owner
// thread: `sampled` says whether that frame produced a frame-phase sample
// (its view_submit sum is joined into the pass sample); otherwise the frame's
// pass accumulation is discarded.
inline void frame(std::uint64_t frame_index, bool sampled, std::uint64_t view_submit_us) noexcept {
    if (active.load(std::memory_order_relaxed)) detail::frame_impl(frame_index, sampled, view_submit_us);
}
// The per-frame accumulator, for the residual group's pairing clocks and
// pass count (residual_phases.cpp; owner thread only, never null).
detail::Accumulator* shared_accumulator() noexcept;
#ifdef X3M_GAME_PHASE_FIXTURE
// The production install transaction on fixture spans (plain copies: the
// fixture supplies its own addresses, the bytes are the real ones).
bool fixture_install(const engine_patch::SiteSpec* specs, const char** status);
bool fixture_uninstall();
void* fixture_emit(unsigned index, void*** next); // the production lean stub
bool fixture_last_sample(detail::Sample* out);    // owner thread only
const detail::Accumulator* fixture_accumulator();
const detail::Gate* fixture_gate();
std::uint64_t fixture_dropped();
#endif
}
