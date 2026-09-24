#pragma once
#include <cstdint>

// Lens-flare collector fix (X3M_SUN_FLARE_FIX=on|off; unset = off; the
// launcher sends on): the collector's horizontal off-screen test keeps only
// the low 32 bits of W*tan(F/2)*z/2, so on wide displays at wide F a far sun
// near the view centre is declared off-screen and its lens chain vanishes
// (docs/reverse-engineering/field-of-view.md sections 9 and 9.1). on claims
// the six bytes `SHRD EAX,EDX,16; CMP ECX,EAX` at 0x0047e391 through
// engine_patch and pushes a 20-byte saturating stub in front of the tail
// (sun_flare_fix_sites.h): an overflowing bound becomes 0x7fffffff, every
// other input is unchanged. The stub is emitted arena code with no pointer
// into this module; it touches EAX/EDX and dead flags only. Claimed on the
// backend-load path inside the engine_patch install window (a late call is
// refused, late_claim) after the structural executable check and a 56-byte
// window compare (fail closed); the chain link and a read-back of the patched
// span are verified, any failure puts the original bytes back (or, when that
// fails, keeps the site registered). Nothing runs per frame on the proxy side;
// Device Reset does not touch it. shutdown() puts the original bytes back on a
// dynamic unload only while the span still holds our jump (one
// sun_flare_fix_restore row, written to the log handle without the capture
// lock; foreign bytes are left alone, restore_not_owned).
namespace x3m::sun_flare_fix {
bool initialize();  // backend-load path only; logs one sun_flare_fix line
bool shutdown();    // dynamic-unload detach only; true when nothing stays registered
// Verifies the window at `window` (the engine's 0x0047e365, or a fixture's
// copy), claims the span at window + 0x2c and pushes the stub. Returns whether
// the patch is live; state() carries the reason either way.
bool install_at(std::uintptr_t window);
const char* state();
const char* write_path();          // none|atomic|plain: which engine_patch::write_code path wrote the jump
bool patched();                    // registered (live, or a failed rollback still to be restored)
std::uintptr_t stub_address();     // 0 unless the stub is linked in
}
