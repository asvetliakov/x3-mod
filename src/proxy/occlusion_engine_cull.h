#pragma once
#include <cstdint>
#include "occlusion_engine_core.h"

// Engine-side occlusion skip (X3M_OCCLUSION_CULL=engine; occlusion_engine_core.h;
// docs/architecture/occlusion-cull.md "Engine-side skip";
// docs/reverse-engineering/engine-side-occlusion-cull.md). A third stub on the
// small-parts claim of the per-node cull/LOD pass 0x0047cfe0 at 0x0047d2a2
// (cull_small_parts.h chain_stub: one engine_patch claim, all three stubs,
// restored once by cull_small_parts::shutdown()): a ship part node listed in
// the table the motion route publishes at the sector view's Clear takes the
// engine's own size-cull instruction at 0x0047d2c3 for that view and frame.
// Installed on the backend-load path only with the mode `engine`, after the
// exact-executable check, with this module pinned; with the mode off or on
// nothing is patched and no byte of the engine changes. The stub is
// straight-line integer code emitted from the core header: no handler runs
// inside the pass. Fail closed: the table is armed only between a publish and
// the next Present; a refused claim leaves the draw-level cull alone.
namespace x3m::occlusion_engine_cull {
// Backend-load path only, after cull_small_parts::initialize() and lens_flare_cull::initialize(); engine = the mode
// parsed as `engine` (and the route requested). Logs one `occlusion_engine_cull status=patched|off|refused reason=` row.
bool initialize(bool engine);
bool shutdown(); // disarms; the shared site's bytes go back in cull_small_parts::shutdown()
// Chains the stub on the shared claim of the given site (the fixture passes a synthetic copy of the engine window);
// cull_target is the address of the engine's `and [edi+0x12c],~2` (window offset 47).
bool install_at(std::uintptr_t site, std::uintptr_t cull_target);
const char* state();
bool installed();
std::uintptr_t stub_address();
// Render thread, at the sector view's Clear: rebuilds the table from the previous frame's ledger (core::Table::publish),
// stores the view pointer and stamp and arms the stub. Returns the entries published (0 = disarmed).
unsigned publish(const occlusion_cull::engine::Ledger& ledger, std::uint32_t stamp, unsigned retest,
                 occlusion_cull::engine::PublishStats* stats);
void disarm(); // the stub sees armed = 0 at its first compare
// Render thread, at Present: the stub's counters since the previous take (cleared), and the stub is disarmed.
struct Counters {
    std::uint32_t visits; // sector-view pass visits while armed (0 with entries published: the view pointer differs)
    std::uint32_t skipped_parts, skipped_draws, rejected_model, rejected_stamp, rejected_position;
};
Counters take();
const occlusion_cull::engine::Table& table(); // diagnostics (the published entries)
}
// The words the stub reads and writes (render thread only): armed (1 between a publish and the next Present), the
// sector view pointer, this frame's stamp, and the five counters.
extern "C" volatile std::uint32_t x3m_occlusion_engine_armed;
extern "C" volatile std::uint32_t x3m_occlusion_engine_view;
extern "C" volatile std::uint32_t x3m_occlusion_engine_stamp;
extern "C" volatile std::uint32_t x3m_occlusion_engine_visits;
extern "C" volatile std::uint32_t x3m_occlusion_engine_skipped_parts;
extern "C" volatile std::uint32_t x3m_occlusion_engine_skipped_draws;
extern "C" volatile std::uint32_t x3m_occlusion_engine_rejected_model;
extern "C" volatile std::uint32_t x3m_occlusion_engine_rejected_stamp;
extern "C" volatile std::uint32_t x3m_occlusion_engine_rejected_position;
