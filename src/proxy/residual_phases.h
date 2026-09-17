#pragma once
#include <atomic>
#include <cstdint>
#include "engine_patch.h"
#include "residual_phases_core.h"

// Default-off residual attribution stamps (X3M_RESIDUAL_PHASES=1, launcher
// --residual-phases, which implies --frame-phases and --pass-phases and
// requires --telemetry). Two byte-verified sites (residual_phase_sites.h):
// the ID3DXEffect::Begin dispatch of the material submission routine
// 0x004c0150 splits the engine work between passes into the engine's
// per-object preparation (last pass_end -> Begin) and the D3DX setup (Begin
// -> first pass_begin); the particles-call return of the frame routine's
// per-view loop splits what the frame group leaves between view_submit_end
// and the next view into the particles pass and the rest (scene-end
// composite, env-map pass, fixups), computed at the frame boundary as views -
// view_setup - view_submit - particles. Both stamps pair with clocks the
// sibling groups retain on the owner thread (pass_phases_core.h end_clock /
// begin_clock, frame_phases_core.h submit_end); neither sibling's handler
// gains more than one store. The group uses the shared lean stub
// (lean_stub.cpp: flags, EAX/ECX/EDX and XMM0-7 saved around a handler under
// LightCallBoundary, MXCSR + LastError, x87-free;
// verification/probe/check_no_x87.py walks x3m_residual_phase_enter). The
// window reduction runs at the frame boundary from
// frame_phases::detail::frame_impl under its owner guard, ahead of the pass
// group's own reduction so the pass accumulator is still open: one
// `residual_phases` line per 300-frame window
// (docs/verification/sampling-profiler.md, "Residual phases"). Off, nothing
// is installed; the cost that remains is one relaxed load per frame here
// plus what the siblings pay unconditionally for the retained clocks: one
// store per pass_end and one load+predicted branch per pass_begin in the
// pass handler, one store per view_submit_end in the frame tracker.
namespace x3m::residual_phases {
bool initialize(); // after pass_phases::initialize, while the install window is open
extern std::atomic<bool> active;
namespace detail {
void frame_impl(std::uint64_t frame, bool sampled, std::uint64_t views_us, std::uint64_t view_setup_us,
                std::uint64_t view_submit_us, std::uint32_t views) noexcept;
}
// Frame boundary, called by frame_phases::detail::frame_impl on its owner
// thread before pass_phases::frame: `sampled` says whether that frame produced
// a frame-phase sample (its views phase, view sums and view count are joined
// and the pass accumulator's count is read); otherwise the frame's
// accumulation is discarded.
inline void frame(std::uint64_t frame_index, bool sampled, std::uint64_t views_us, std::uint64_t view_setup_us,
                  std::uint64_t view_submit_us, std::uint32_t views) noexcept {
    if (active.load(std::memory_order_relaxed)) detail::frame_impl(frame_index, sampled, views_us, view_setup_us, view_submit_us, views);
}
#ifdef X3M_GAME_PHASE_FIXTURE
// The production install transaction on fixture spans (the fixture supplies
// its own addresses and the relocated disp32 of the view span; opcodes and
// lengths are real). The sibling clocks come from the production pass
// accumulator and frame tracker, so the fixture drives both groups.
bool fixture_install(const engine_patch::SiteSpec* specs, const char** status);
bool fixture_uninstall();
void* fixture_emit(unsigned index, void*** next); // the production lean stub
bool fixture_last_sample(detail::Sample* out);   // owner thread only
const detail::Accumulator* fixture_accumulator();
const detail::Gate* fixture_gate();
std::uint64_t fixture_dropped();
#endif
}
