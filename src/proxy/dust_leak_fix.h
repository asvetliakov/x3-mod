#pragma once
#include <cstdint>

// Dust-scene leak fix (X3M_DUST_LEAK_FIX=on|off; unset = on; the launcher
// sends on): the engine's dust-scene fill 0x0041efc0 allocates a scene node
// before it validates the dust body and drops the node when the body cannot be
// loaded, so every frame in a sector whose background names missing dust
// bodies leaks NumDustInstances nodes on the render manager's unattached list
// (docs/reverse-engineering/object-lifetimes.md, "Run383"). on claims the five
// bytes `SUB dword [ESP+0x20],1` at 0x0041f4d1 (the loop's common tail)
// through engine_patch and pushes a 45-byte stub in front of the tail
// (dust_leak_fix_sites.h): for a node that was never attached it calls the
// engine's own node release 0x00487be0 and makes the loop end for this frame;
// every other arrival is unchanged. The stub is emitted arena code with no
// pointer into this module (its hit counter lives in the arena too); it
// preserves every register but the dead flags. Claimed on the backend-load
// path inside the engine_patch install window (a late call is refused,
// late_claim) after the structural executable check and a 37-byte window
// compare (fail closed); the chain link and a read-back of the patched span
// are verified, any failure puts the original bytes back (or, when that
// fails, keeps the site registered). Per frame the proxy runs nothing; under
// --perf/--debug report() writes one `dust_leak_fix hits=` row per 300
// frames from the counter. Device Reset does not touch it. shutdown() puts
// the original bytes back on a dynamic unload only while the span still holds
// our jump (one dust_leak_fix_restore row, written to the log handle without
// the capture lock; foreign bytes are left alone, restore_not_owned).
namespace x3m::dust_leak_fix {
bool initialize(); // backend-load path only; logs one dust_leak_fix site= line
bool shutdown();   // dynamic-unload detach only; true when nothing stays registered
// Verifies the window at `window` (the engine's 0x0041f4b7, or a fixture's
// copy), claims the span at window + 0x1a and pushes the stub whose call
// reaches `release` (the engine's 0x00487be0, or a fixture's). Returns whether
// the patch is live; state() carries the reason either way.
bool install_at(std::uintptr_t window, std::uintptr_t release);
// One `dust_leak_fix hits=<since the previous row> total=<since install> frame=<frame>` row while the stub is
// live (the caller gates the tier and the 300-frame cadence).
void report(unsigned frame);
std::uint32_t hits(); // the stub's counter (0 unless live)
const char* state();
const char* write_path();      // none|atomic|plain: which engine_patch::write_code path wrote the jump
bool patched();                // registered (live, or a failed rollback still to be restored)
std::uintptr_t stub_address(); // 0 unless the stub is linked in
}
