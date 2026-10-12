#pragma once
#include <cstdint>

// Smooth ship steering (X3M_SHIP_STEERING=1, default off): the three
// steering-leaf entries A1-A3 and the per-object loop continue B of the
// command pass claimed through engine_patch (docs/architecture/
// ship-steering-and-avoidance.md, step 1). initialize() runs on the
// backend-load path inside the install window; every refusal leaves every
// byte untouched and logs one ship_steering_refused row. frame_boundary()
// runs once per Present and drains the diagnostic rings under the logging
// tiers (nothing is formatted on the hot path).
namespace x3m::ship_steering {
bool initialize();
bool installed();
const char* state();
void frame_boundary(std::uint64_t frame);
// Restores the four sites (FreeLibrary path only; the stubs and tails stay callable).
bool shutdown();
}
