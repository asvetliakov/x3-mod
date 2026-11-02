#pragma once
#include <cstdint>
#include "../../proxy/engine_effects_patch.h"

// FL edition of x3m::engine_effects_patch (src/engine/fl/engine_effects_fl.cpp provides engine_effects_patch.h's
// functions for X3FL.exe; install_at is AP only). FL edition only.
namespace x3m::engine_effects_patch {
// Both windows at base + their RVAs (engine_effects_sites_fl.h; B's fixup must equal base + 0x1a83b0), then B (one
// lock cmpxchg8b) and A (plain copy, install window only), both or none. initialize() calls it with
// fl::module_base(); the fixture with its own 64 KB-aligned image. LastError preserved.
bool install_base(std::uintptr_t base);
#ifdef X3M_ENGINE_EFFECTS_FL_FIXTURE
void fixture_base(std::uintptr_t base); // initialize() uses `base` instead of fl::module_base() (0: the real one)
#endif
}
