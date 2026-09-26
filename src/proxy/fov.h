#pragma once
#include <cstdint>

// Field of view (X3M_FOV=game|N, N the game's own degrees 70..100; unset or
// empty = game; docs/reverse-engineering/field-of-view.md sections 5 and 7.3).
// The engine's FOV is the binary angle at registry+0x24 (*0x00608504), 0x4000
// by default, which every cockpit update divides by the zoom and copies into
// the sector cameras. The game's number N (script default 90, in-game menu
// 70..100, F = (N << 16) / 360) is reinterpreted as "N degrees horizontal on
// 16:9": the engine gets F' with tan(F'/2) = 0.75 * tan(F/2) (90 -> 0x3470,
// 70 -> 0x2768, 100 -> 0x3b6f). Three sites (fov_sites.h), all or none:
//  - the registry constructor's `MOV dword [ESI+0x24],0x4000` immediate at
//    0x0041c9dc becomes F'(N) (four bytes, one lock cmpxchg8b, read back);
//  - INS_SetFocus's `MOV EDX,[0x00608504]` at 0x0042dbf8 is claimed through
//    engine_patch (five jmp bytes in one aligned qword, read back) with a
//    generated stub in front that remaps ECX, the menu's incoming F, through
//    an 81-entry table (N 50..130; other values pass through) before the
//    displaced MOV runs and the store at 0x0042dc04 writes the base;
//  - the registry serializer's load store `MOV [EBP+0x24],EAX; POP ESI; MOV
//    AL,1` at 0x0041c8c1 is claimed the same way with a stub that replaces a
//    savegame's exact vanilla-unit (N << 16) / 360, N 50..130, with the same
//    table's F'(N) (a save's 90 is F'(90) whatever --fov is: the launcher
//    value seeds a new game, the savegame's own value wins after a load);
//    remapped and other values are stored unchanged. The constructor's F'
//    is moved by one unit when a decimal --fov lands on a vanilla value
//    (sites::constructor_focus), so such a session's saves load as saved.
// Nothing per frame; saves are written unchanged (in the session's units).
// Written on the backend-load path inside the engine_patch install window (a
// late call is refused, late_claim) after the structural executable check,
// the reader contract, the INS_SetFocus case window and callee prefix, the
// load window and its caller, and the constructor's 28-byte window compare
// (fail closed). A failed later site rolls the earlier ones back; a rollback
// that cannot be completed keeps the site registered (patched_unverified). When the
// registry already exists at install its +0x24 is written once with F'(N)
// (validated reads, a committed writable page, one aligned
// InterlockedCompareExchange from the value read). shutdown() on a dynamic
// unload restores the INS_SetFocus and load bytes (only over our jmp) and 00 40 00 00
// (only over our value), one fov_restore row written to the log handle
// without the capture lock; the live registry keeps its value, and the stub
// and tables stay in the never-freed arena.
namespace x3m::fov {
bool initialize(); // backend-load path only; logs one fov line
bool shutdown();   // dynamic-unload detach only; true when nothing stays registered
// Verifies the window at `window` (the engine's 0x0041c9cc, or a fixture's copy)
// and writes `focus` into the immediate. Returns whether the patch is live;
// state() carries the reason either way.
bool install_at(std::uintptr_t window, std::uint32_t focus);
const char* state();
const char* write_path();     // none|atomic|plain: which engine_patch::write_code path wrote the immediate
const char* registry_state(); // written|absent|skipped: the one-off registry+0x24 write at install
const char* setfocus_state(); // none (not attempted or restored), active, rolled_back (after a load failure) or the
                              // INS_SetFocus failure reason
const char* load_state();     // none (not attempted or restored), active, or the load-store failure reason
bool patched();               // any site registered (the constructor's immediate, the INS_SetFocus or the load claim)
bool setfocus_patched();      // the INS_SetFocus claim is registered
bool load_patched();          // the load-store claim is registered
// The binary angle the option put in place (0x4000 when off, refused or before initialize()); after
// rollback_failed the immediate read back, or 0 (unknown) when unreadable or implausible.
std::uint32_t configured_focus();
// The engine's live base: registry+0x24 when the executable was verified at
// initialize() and the value is readable and plausible (0x106..0x8000), else
// configured_focus(). Two validated engine reads; any thread.
std::uint32_t current_focus();
// Per Present: one fov_confirm row at the first Present (registry+0x24 against
// the configured value); while the registry does not exist yet, one more row
// when it first appears. After that a single flag test.
void present(unsigned long long frame);
// One more fov_confirm row (after=save_load_complete) for each save_load_complete
// marker: the base the savegame left in registry+0x24. Present path only. The
// Present-cadence marker fires once per process (the first load after the menu),
// so a second load in the same session gets no row.
void loaded(unsigned long long frame);
}
