#pragma once
#include <cstdint>
#include "../../proxy/cull_small_parts.h"

// FL edition of x3m::cull_small_parts (src/engine/fl/cull_small_parts_fl.cpp provides cull_small_parts.h's functions for
// X3FL.exe; the ap target keeps src/proxy/cull_small_parts.cpp). FL edition only.
namespace x3m::cull_small_parts {
// The window at base + 0x0961c5 (55 bytes, cull_small_parts_sites_fl.h), the stub emitted, then the five jump bytes at
// base + 0x0961d3 (one lock cmpxchg8b) and their read-back; nothing chained = nothing written. initialize() calls it with
// fl::module_base() and far_jets false (fenced on FL); the fixture with its own 64 KB-aligned image (far_jets true only
// there, to exercise the encoder's far block). LastError preserved.
bool install_base(std::uintptr_t base, bool exempt_projectiles, bool far_jets);
#ifdef X3M_CULL_SMALL_PARTS_FL_FIXTURE
void fixture_base(std::uintptr_t base); // initialize() uses `base` instead of fl::module_base() (0: the real one)
#endif
}
