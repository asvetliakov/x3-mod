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
std::uint64_t generation(std::uintptr_t cockpit) noexcept;
// A nested/new updater permanently revokes the previous token on that thread.
// The caller still validates active registry, ship, camera and view identities.
Update current_update(std::uintptr_t cockpit) noexcept;
void report(std::uint64_t frame);
// Called only after game-thread quiescence; executable arena remains allocated.
void shutdown();
}
