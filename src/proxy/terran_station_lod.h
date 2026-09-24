#pragma once
#include <cstdint>

// Terran-station LOD (X3M_TERRAN_STATION_LOD=size|distance; unset = size):
// Terran TDocks/TFactories roots carry bit 31 of node+0x12c, which sends their
// whole subtree to the engine's fixed-distance LOD branch; that branch never
// reaches a merged-LOD overlay record (docs/architecture/merged-lod-feasibility.md,
// "Slot 06 LOD switch"). size rewrites the reader's `je` at 0x0047d01c into a
// `jmp` to the same target (terran_lod_sites.h), so these stations select by
// screen size like every other object; distance leaves the engine's bytes.
// Two instruction bytes, no stub, no pointer into this module, nothing per
// frame, saves unchanged. Written on the backend-load path inside the
// engine_patch install window (a late call is refused, late_claim) after the
// structural executable check and a 17-byte window compare (fail closed);
// VirtualProtect, one lock cmpxchg8b, FlushInstructionCache, read-back compare,
// rollback judged by a read-back of the original bytes. Device Reset does not
// touch it. shutdown() puts the original bytes back on a dynamic unload (one
// terran_station_lod_restore row, written to the log handle without the capture
// lock; a failed restore stays registered); the
// instruction boundaries are the same in both states, so a thread inside the
// pass sees either jump.
namespace x3m::terran_station_lod {
bool initialize();  // backend-load path only; logs one terran_station_lod line
bool shutdown();    // writes 74 05 back whatever the site holds (dynamic-unload detach only) and logs one terran_station_lod_restore row; true when nothing stays registered
// Verifies the window at `window` (the engine's 0x0047d012, or a fixture's
// synthetic copy) and writes the site. Returns whether the patch is live;
// state() carries the reason either way.
bool install_at(std::uintptr_t window);
const char* state();
const char* write_path();  // none|atomic|plain: which engine_patch::write_code path wrote the site
bool patched();
}
