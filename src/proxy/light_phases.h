#pragma once
#include <atomic>
#include "engine_patch.h"
#include "light_phases_core.h"
// Default off. No engine pointer is retained; only a numerical native frame
// token and caller bucket survive a dispatch. Frame cadence is frame_phases'.
// Mixed x87/MXCSR rounding modes bypass before mode writes and invalidate the
// owner frame; read-only FNSTCW is the only x87 opcode in the timer.
namespace x3m::light_phases {
extern std::atomic<bool> active;
bool initialize();
namespace detail { void frame_impl(std::uint64_t frame,bool sampled) noexcept; }
inline void frame(std::uint64_t index,bool sampled) noexcept {
    if(active.load(std::memory_order_relaxed))detail::frame_impl(index,sampled);
}
#ifdef X3M_GAME_PHASE_FIXTURE
bool fixture_install(const engine_patch::SiteSpec*,const char**);
bool fixture_uninstall();
const detail::Accumulator* fixture_accumulator();
const detail::Gate* fixture_gate();
void fixture_returns(std::uint32_t cockpit,std::uint32_t traversal);
#endif
}
