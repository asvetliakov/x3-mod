#pragma once
#include <atomic>
#include <cstdint>
#include "engine_patch.h"
#include "frame_phases_core.h"

// Default-off per-frame engine phase stamps (X3M_FRAME_PHASES=1, launcher
// --frame-phases, requires --telemetry). Ten byte-verified sites inside the
// render routine 0x00471f50 (frame_phase_sites.h) take one
// QueryPerformanceCounter each through the game-phase stub and CPU boundary
// (game_phases.cpp x3m_game_phase_enter, index >= game_phases::sites::Count);
// the Present hook closes the frame. Independent of X3M_GAME_PHASES: the two
// groups share the emitter and the boundary, nothing else. Per 300-frame
// window one `frame_phases` line plus up to four `frame_phases_slow`
// witnesses keyed by frame (docs/verification/sampling-profiler.md, "Frame
// phases"). Off, the per-frame cost is three relaxed loads of a process-global atomic bool.
namespace x3m::frame_phases {
bool initialize(); // capture initialize_log only, after game_phases::initialize, while the install window is open
extern std::atomic<bool> active;
// Engine stamp, called inside the game-phase CPU boundary (LastError and the
// computational state are already preserved there). index is sites::Index.
void stamp(unsigned index) noexcept;
namespace detail {
void present_begin_impl() noexcept;
void present_end_impl() noexcept;
void frame_impl(std::uint64_t frame) noexcept;
}
// Present hook: begin ahead of before_original, end after after_original
// (the same positions as frame_timing), frame at the per-frame sample site.
inline void present_begin() noexcept { if (active.load(std::memory_order_relaxed)) detail::present_begin_impl(); }
inline void present_end() noexcept { if (active.load(std::memory_order_relaxed)) detail::present_end_impl(); }
inline void frame(std::uint64_t frame_index) noexcept { if (active.load(std::memory_order_relaxed)) detail::frame_impl(frame_index); }
// The per-frame tracker, for the residual group's view_submit_end clock
// (residual_phases.cpp; owner thread only, never null).
const detail::Tracker* shared_tracker() noexcept;
#ifdef X3M_GAME_PHASE_FIXTURE
// The production install transaction on fixture spans: specs carry fixture
// addresses and bytes, length/rel32 metadata stays the real contract.
bool fixture_install(const engine_patch::SiteSpec* specs, const char** status);
bool fixture_uninstall();
bool fixture_last_sample(detail::Sample* out); // the most recent closed frame, owner thread only
const detail::Tracker* fixture_tracker();
#endif
}
