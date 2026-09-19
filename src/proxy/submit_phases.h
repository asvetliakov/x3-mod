#pragma once
#include <atomic>
#include <cstdint>
#include "engine_patch.h"
#include "submit_phases_core.h"

// Default-off view_submit candidate stamps (X3M_SUBMIT_PHASES=1, launcher
// --submit-phases, which requires --telemetry and --frame-phases).
// Twenty-two byte-verified sites (submit_phase_sites.h) bracket the candidates
// R1-R8 of docs/reverse-engineering/view-submit-hot-path.md, which the
// sampling profiler cannot see under FEX (section 8 of that note): the
// draw-queue sort 0x0047e620 (time, calls, queue length), the per-node cache
// walk (time, lookups, misses, sampled iterations), the SetTechnique and End
// dispatches, the Begin -> pass-loop-guard block of 0x004c0150, the two
// D3DXMatrixInverse calls, 0x004c0150 and 0x004bdee0 whole. The group is
// independent of the pass and residual groups (its sites are disjoint from
// theirs) and needs the frame group only for the frame boundary and the owner
// thread. It uses the context variant of the shared lean stub (lean_stub.h
// emit_context: flags and all eight general registers saved with pushad,
// XMM0-7 saved, x87 never touched; the handler runs under LightCallBoundary,
// MXCSR + LastError, and is x87-free: verification/probe/check_no_x87.py
// walks x3m_submit_phase_enter). The handler reads engine memory at two
// points only, both behind pointers the engine itself dereferences in the
// same block: the draw queue at the sort entry and the node cache list on one
// lookup in sixteen. The window reduction runs at the frame boundary from
// frame_phases::detail::frame_impl under its owner guard: one `submit_phases`
// line per 300-frame window (docs/verification/sampling-profiler.md, "Submit
// phases"). Off, nothing is installed and the cost is one relaxed load per
// frame.
namespace x3m::submit_phases {
bool initialize(); // after frame_phases::initialize, while the install window is open
extern std::atomic<bool> active;
namespace detail {
void frame_impl(std::uint64_t frame, bool sampled) noexcept;
}
// Frame boundary, called by frame_phases::detail::frame_impl on its owner
// thread: `sampled` says whether that frame produced a frame-phase sample;
// otherwise the frame's accumulation is discarded.
inline void frame(std::uint64_t frame_index, bool sampled) noexcept {
    if (active.load(std::memory_order_relaxed)) detail::frame_impl(frame_index, sampled);
}
#ifdef X3M_GAME_PHASE_FIXTURE
// The production install transaction on fixture spans (the fixture supplies
// its own addresses and relocated rel32 fields; opcodes and lengths are real)
// and the address of the word that stands for the engine's view-root global.
bool fixture_install(const engine_patch::SiteSpec* specs, const char** status, const std::uint32_t* view_root_global);
bool fixture_uninstall();
void* fixture_emit(unsigned index, void*** next); // the production context stub
bool fixture_last_sample(detail::Sample* out);   // owner thread only
const detail::Accumulator* fixture_accumulator();
const detail::Gate* fixture_gate();
std::uint64_t fixture_dropped();
#endif
}
