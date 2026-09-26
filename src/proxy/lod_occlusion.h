#pragma once
#include <cstdint>

// LOD occlusion (X3M_LOD_OCCLUSION=record0|all; unset = record0): the
// material submission 0x004c0150 binds t_OcclusionTexture from the material
// only for LOD record 0 and the NONE_OCCL_DECAL placeholder for every other
// record (docs/reverse-engineering/texture-lookup.md section 12), so merged-LOD
// coarse records and vanilla lower records draw without occlusion. all sets the
// rel32 of the gate's `jne` at 0x004c34f7 to 0 (lod_occlusion_sites.h): the
// branch lands on the next instruction, every record takes the LOD-0 bind path;
// record0 leaves the engine's bytes. Four instruction bytes, no stub, no
// pointer into this module, nothing per frame, saves unchanged. Written on the
// backend-load path inside the engine_patch install window (a late call is
// refused, late_claim) after the structural executable check and a 31-byte
// window compare (fail closed); VirtualProtect, one lock cmpxchg8b,
// FlushInstructionCache, read-back compare, rollback judged by a read-back of
// the original bytes. Device Reset does not touch it. shutdown() puts the
// original bytes back on a dynamic unload when the span still holds the patch
// (one lod_occlusion_restore row, written to the log handle without the capture
// lock; foreign bytes are left alone, restore_not_owned, and like a failed
// restore keep the site registered); the opcode and the instruction boundaries are the same in both
// states, so a thread inside the submission decodes either jne.
namespace x3m::lod_occlusion {
bool initialize(); // backend-load path only; logs one lod_occlusion line
bool shutdown(); // writes c9 00 00 00 back only over the patched 00 00 00 00 (dynamic-unload detach only; other bytes:
                 // restore_not_owned, no write) and logs one lod_occlusion_restore row; true when nothing stays
                 // registered
// Verifies the window at `window` (the engine's 0x004c34e7, or a fixture's
// copy) and writes the rel32. Returns whether the patch is live; state()
// carries the reason either way.
bool install_at(std::uintptr_t window);
const char* state();
const char* write_path(); // none|atomic|plain: which engine_patch::write_code path wrote the rel32
bool patched();
}
