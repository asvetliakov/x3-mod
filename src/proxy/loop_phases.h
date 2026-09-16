#pragma once
#include <atomic>
#include <cstdint>
#include "engine_patch.h"
#include "loop_phases_core.h"

// Default-off per-sector update stamps (X3M_LOOP_PHASES=1, launcher
// --loop-phases, requires --telemetry and --frame-phases). Six byte-verified
// sites inside the main loop's per-sector update driver 0x0043a360
// (loop_phase_sites.h) split the `input_part=0` stall region of the main loop
// into its callees: collide, simulate, post (pass A) and economy+attach (pass
// B), accumulated per frame over every container the driver visits. The
// group uses the lean stub shared with pass phases (lean_stub.cpp: flags,
// EAX/ECX/EDX and XMM0-7 saved around a handler under LightCallBoundary,
// MXCSR + LastError, x87-free; verification/probe/check_no_x87.py walks
// x3m_loop_phase_enter) because the stamp rate is six per active sector plus
// two per skipped container per frame and the container count is unknown.
// The window reduction runs at the frame boundary from
// frame_phases::detail::frame_impl under its owner guard: one `loop_phases`
// line per 300-frame window and one `loop_phases_slow` line for each of the
// first 64 frames whose sum exceeds 50 ms (docs/verification/
// sampling-profiler.md, "Loop phases"). Off, nothing is installed and the
// per-frame cost is one relaxed load.
namespace x3m::loop_phases {
bool initialize(); // after frame_phases::initialize, while the install window is open
extern std::atomic<bool> active;
namespace detail {
void frame_impl(std::uint64_t frame, bool sampled, std::uint64_t dt_us, std::uint64_t pre_render_us) noexcept;
}
// Frame boundary, called by frame_phases::detail::frame_impl on its owner
// thread: `sampled` says whether that frame produced a frame-phase sample
// (its dt and pre_render are joined into the loop sample; the game-phase input
// phase replaces pre_render when X3M_GAME_PHASES is active); otherwise the
// frame's accumulation is discarded.
inline void frame(std::uint64_t frame_index, bool sampled, std::uint64_t dt_us, std::uint64_t pre_render_us) noexcept {
    if (active.load(std::memory_order_relaxed)) detail::frame_impl(frame_index, sampled, dt_us, pre_render_us);
}
#ifdef X3M_GAME_PHASE_FIXTURE
// The production install transaction on fixture spans (the fixture supplies
// its own addresses and relocated call fields; opcodes and lengths are real).
bool fixture_install(const engine_patch::SiteSpec* specs, const char** status);
bool fixture_uninstall();
void* fixture_emit(unsigned index, void*** next); // the production lean stub
bool fixture_last_sample(detail::Sample* out);   // owner thread only
const detail::Accumulator* fixture_accumulator();
const detail::Gate* fixture_gate();
std::uint64_t fixture_dropped();
#endif
}
