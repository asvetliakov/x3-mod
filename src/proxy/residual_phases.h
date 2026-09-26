#pragma once
#include <atomic>
#include <cstdint>
#include "engine_patch.h"
#include "residual_phases_core.h"

// Default-off residual attribution stamps (X3M_RESIDUAL_PHASES=1, launcher
// --residual-phases, which implies --frame-phases and --pass-phases and
// requires --telemetry). Two byte-verified sites (residual_phase_sites.h):
// the ID3DXEffect::Begin dispatch of the material submission routine
// 0x004c0150 pairs preparation and setup with retained pass clocks. Cumulative
// main-view submission ticks split both spans at every view boundary: prepare
// and setup are within submission; between_prepare and outside_setup are the
// complements of those spans. The particles stamp retains its original scope.
// Existing lean stubs preserve flags, registers, MXCSR and LastError; no new
// engine site or per-draw QPC is needed. Fixed counters and window arrays are
// reduced under the frame owner guard before the pass group's frame discard.
// Only enabled pass/residual diagnostics pay this bookkeeping; disabled groups
// install nothing. Dispatch self-cost uses the active-view CPU fixture
// two-site average; it is not exact flight self-cost.
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
    if (active.load(std::memory_order_relaxed))
        detail::frame_impl(frame_index, sampled, views_us, view_setup_us, view_submit_us, views);
}
#ifdef X3M_GAME_PHASE_FIXTURE
// The production install transaction on fixture spans (the fixture supplies
// its own addresses and the relocated disp32 of the view span; opcodes and
// lengths are real). The sibling clocks come from the production pass
// accumulator and frame tracker, so the fixture drives both groups.
bool fixture_install(const engine_patch::SiteSpec* specs, const char** status);
bool fixture_uninstall();
void* fixture_emit(unsigned index, void*** next); // the production lean stub
bool fixture_last_sample(detail::Sample* out);    // owner thread only
const detail::Accumulator* fixture_accumulator();
const detail::Gate* fixture_gate();
std::uint64_t fixture_dropped();
#endif
}
