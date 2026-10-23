#pragma once
#include "music_alert_core.h"
#include <cstdint>

// Combat-music trigger narrowed to real engagement (docs/architecture/music-combat-trigger.md; the engine facts in
// docs/reverse-engineering/music-script-driver.md sections 2, 5 and 9; ledger docs/verification/
// music-combat-trigger.md).
//
// X3M_MUSIC_COMBAT_TRIGGER=engaged (the schema default; `vanilla` patches nothing) claims one six-byte site inside
// the engine's alert routine 0x0042c890 at 0x0042c974 (`mov ecx,[esi+8]; mov eax,[esi+4]`, two whole instructions;
// ESI = R the cockpit registry, EDI = P the player ship or 0, [R+8] this frame's walk level, [R+4] the committed
// level; EAX/ECX/EDX and the flags are dead there). The stub calls the handler once per frame on the main thread and
// replays the two moves through the claim tail. The handler keeps the vanilla range as the precondition and lowers
// [R+8] to 0 unless the player is "engaged": within X3M_MUSIC_COMBAT_HOLD_S game seconds (1..600, default 20; the
// clock is cfg+0x718, so SETA scales the hold) of (a) the player's fire counters advancing while the selected target
// is hostile, (b) the player ship carrying the hit bit Flags2 0x80000 this frame, (c) a player-owned ship within the
// alert range (2,500,000 units, largest axis) carrying that bit or, for a class-7 ship, holding an attack order on a
// hostile, (d) the vanilla level being 2 (a hostile missile homing on the player), (e) the player's hit counters rising
// while a hostile carries the player-hit bit 0x40, or (f) a hostile ship's attack order targeting the player ship or an
// owned ship within that range. Rules (c) and (f) walk the sector's lists 5, 6, 7 and 10 (bounded) only while the
// vanilla level is nonzero, (a) and (e) only on frames where a counter rose; the rest is O(1) reads. The counters are
// re-baselined on the save-load marker, a changed player ship pointer or a clock that went backwards.
//
// Fail closed: the executable hash, every window of music_alert_core.h and the two call sites are compared before
// the one write; a refusal logs one music_alert row and leaves vanilla behaviour; a claim that fails after going live
// is rolled back. Reads go through engine_memory (VirtualQuery-validated); the handler calls no engine code, no
// script and allocates nothing. The stub is integer-only and preserves every register and EFLAGS; the handler runs
// under LightCallBoundary (MXCSR + LastError) and holds no x87 code (check_no_x87.py roots).
//
// Log rows (x3m.log): one `music_alert` install row; one per filtered-level edge (from, to, vanilla, reason a..f,
// age_ms, cause, now, step, seta, walk counts, walk= this frame's walk outcome or the first failed check, fire= rule
// (a)'s outcome on the last frame a fire counter rose); one `music_alert suppressed=start` row when vanilla would have raised
// the alert and the rule held it at 0, and one `suppressed=end frames=<n>` row when that episode ends. No per-frame
// rows; capped per session.
namespace x3m::music_alert {
bool initialize();   // backend-load path only (the engine_patch install window); logs one music_alert row
bool shutdown();     // restores the site (dynamic-unload detach only); true when nothing is installed
void loaded();       // Present path, on the save_load_complete marker: the next frame's counters are a new baseline
const char* state(); // ok / vanilla / <refusal>
}
extern "C" {
// Called by the stub: ESI = R (the registry), EDI = P (the player ship or 0). Writes [R+8] = 0 when not engaged.
void __cdecl x3m_music_alert_frame(std::uint32_t registry, std::uint32_t player);
}
