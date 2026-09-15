#pragma once
#include <cstdint>

// Read-only native cockpit lifetime/update witnesses plus optional transition
// diagnostics. Install before the engine's first cockpit construction. Unseen,
// partial, destroyed and capacity-refused lifetimes fail closed (generation 0).
namespace x3m::chase_transition {
struct Update {
    std::uint64_t generation=0, serial=0;
    std::uint32_t thread=0;
};
bool initialize();
bool installed();
bool diagnostics_active();
// X3M_CHASE_VIEW_RESTORE=1: the one-use rear-chase restore ticket's seven
// sites are claimed (default off: nothing patched, no operand touched).
bool restore_installed();
std::uint64_t generation(std::uintptr_t cockpit) noexcept;
// A nested/new updater permanently revokes the previous token on that thread.
// The caller still validates active registry, ship, camera and view identities.
Update current_update(std::uintptr_t cockpit) noexcept;
// run78 pose gap: a complete lifetime whose registry active-control handle
// (*0x00608504+0x10, the walk of the native resolver 0x0041cd20) maps to this
// cockpit. The chase camera uses it only while the cockpit's ref view object
// (+0x10) is still 0 on a fresh generation; anything unseen, partial,
// destroyed or foreign fails closed. LastError preserved.
bool active_control_cockpit(std::uintptr_t cockpit) noexcept;
void report(std::uint64_t frame);
// Called only after game-thread quiescence; executable arena remains allocated.
void shutdown();
}
