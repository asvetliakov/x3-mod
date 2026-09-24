#pragma once
#include <cstdint>

// Field of view (X3M_FOV=game|<vertical degrees 36..120>; unset or empty =
// game; docs/reverse-engineering/field-of-view.md). The engine's FOV is the
// binary angle at registry+0x24 (*0x00608504), 0x4000 by default, which every
// cockpit update divides by the zoom and copies into the sector cameras. A
// value sets the immediate of the registry constructor's
// `MOV dword [ESI+0x24],0x4000` at 0x0041c9d9 (fov_sites.h) to
// F = round(65536/pi * atan(tan(v/2) / 0.75)): the vertical FOV becomes v on
// every display at least as wide as 4:3 and the horizontal follows the aspect.
// Four immediate bytes, no stub, no pointer into this module, nothing per
// frame, saves unchanged. Written on the backend-load path inside the
// engine_patch install window (a late call is refused, late_claim) after the
// structural executable check, the reader-contract bytes and a 28-byte window
// compare (fail closed); VirtualProtect, one lock cmpxchg8b,
// FlushInstructionCache, read-back compare, rollback judged by a read-back of
// the original bytes. When the registry already exists at that point its +0x24
// is written once (validated reads, a committed writable page, one aligned
// InterlockedCompareExchange from the value read). The in-game FOV menu
// (INS_SetFocus, focus 70..100) still overrides the base for the running
// session; zoom scales it. shutdown() puts 00 40 00 00 back on a dynamic
// unload when the span still holds our value (one fov_restore row, written to
// the log handle without the capture lock); the live registry keeps its value.
namespace x3m::fov {
bool initialize();  // backend-load path only; logs one fov line
bool shutdown();    // dynamic-unload detach only; true when nothing stays registered
// Verifies the window at `window` (the engine's 0x0041c9cc, or a fixture's copy)
// and writes `focus` into the immediate. Returns whether the patch is live;
// state() carries the reason either way.
bool install_at(std::uintptr_t window, std::uint32_t focus);
const char* state();
const char* write_path();      // none|atomic|plain: which engine_patch::write_code path wrote the immediate
const char* registry_state();  // written|absent|skipped: the one-off registry+0x24 write at install
bool patched();
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
}
