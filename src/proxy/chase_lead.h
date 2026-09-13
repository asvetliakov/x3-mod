#pragma once
#include <cstdint>
// Main-gun predictive marker in a currently applied chase view. Requires
// chase_transition's actual cockpit lifetime and update hooks. No mode writes.
namespace x3m::chase_lead {
bool initialize();
bool installed();
// Every pose-handler visit first invalidates its old witness, including errors
// and monitor visits. Only a successful camera write publishes a new witness.
void invalidate_pose();
void camera_context(std::uintptr_t cockpit, std::uintptr_t ship, std::uintptr_t camera, bool applied);
void report(std::uint64_t frame);
void shutdown(); // explicit quiescent fixture teardown only
} // namespace x3m::chase_lead
