#pragma once
#include "seta_restore_core.h"
#include <cstdint>

// SETA restore after a gate transit (docs/architecture/seta-restore.md; ledger docs/verification/seta-restore.md).
// X3M_SETA_RESTORE=1 (schema default 1, launcher --seta-restore): a record-only trampoline at the TI_SetTimeWarpFactor
// case entry 0x004b148c arms a ticket when the story's 150::StartGateWarp stops SETA (old factor > 1.0, new 1.0, task
// IP and return offset detected at run time in the loaded object, story_detect.h: Mayhem 1558d/15ef8, stock AP
// 157a6/160d1; an object where they are not found never arms); once the destination sector is Ready with the player ship in
// it and two frames are presented, the chase restore's injection site 0x00410100 (shared, claimed once for both
// consumers: chase_transition::inject_attach) calls the script's own `0x0049f4c0(0, 0, 0x96, "StartFastForward", 1, 0)`.
// Any later factor write, a player-ship change, VM clear/load, the EH epoch and the expiry cancel the ticket. Fails
// closed: executable identity, every byte window and the install window are checked before any write; a failed step
// rolls the earlier ones back and logs one `seta_restore refused reason=...` row.
namespace x3m::seta_restore {
bool initialize();   // backend-load path only (the engine_patch install window); one row when the variable is set
bool shutdown();     // dynamic unload only: restores the hook and detaches from the injection site
const char* state(); // ok / disabled / <refusal>
// Stores the Present frame counter (one relaxed store; no other per-frame work).
void present(unsigned long long frame);
// The SETA key stop latch of X3M_SETA_MANUAL_FLIGHT >= 1 (seta_key_latch_core.h, docs/architecture/seta-manual-flight.md
// "Key stop latch"), run in this module's two sites: the factor hook arms it on 150::ToggleFastForward's stop, the
// injection consumer calls `0x0049f4c0(0, 0, 0x96, "StopFastForward", 1, 0)` while it is armed. Called once by
// seta_manual_flight::initialize after its install; `debug` (X3M_DEBUG=1) adds the seta_factor / seta_key /
// seta_key_latch rows and keeps the injection prefilter bit set. Returns "ok", or seta_restore_disabled /
// seta_restore_refused when this module is not armed (the latch then stays off: vanilla key behaviour).
const char* key_latch_enable(bool debug);
}
extern "C" {
// Called by the integer stub at 0x004b148c: EBX = argument cells, EBP = task.
void __cdecl x3m_seta_restore_factor_write(std::uint32_t args, std::uint32_t task);
}
