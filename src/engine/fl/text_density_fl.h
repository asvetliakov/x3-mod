#pragma once
#include <cstdint>
#include "../../proxy/text_density.h"

// FL edition of x3m::text_density (src/engine/fl/text_density_fl.cpp provides text_density.h's functions for X3FL.exe;
// the ap target keeps src/proxy/text_density.cpp). FL edition only.
namespace x3m::text_density {
#ifdef X3M_TEXT_DENSITY_FL_FIXTURE
void fixture_base(std::uintptr_t base); // initialize() uses `base` instead of fl::module_base() (0: the real one)
void fixture_reset();                   // forgets the one-shot state once nothing is registered (after shutdown())
#endif
}
