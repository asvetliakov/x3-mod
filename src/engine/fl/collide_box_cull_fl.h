#pragma once
#include <cstdint>
#include "../../proxy/collide_box_cull.h"

// FL edition of x3m::collide_box_cull (src/engine/fl/collide_box_cull_fl.cpp provides collide_box_cull.h's functions for
// X3FL.exe; the ap target keeps src/proxy/collide_box_cull.cpp). FL edition only.
namespace x3m::collide_box_cull {
// Every box window of collide_windows_fl_inc.h at base + its RVA, both stubs emitted, P1 base+0x06723f and P2
// base+0x066859 claimed, chained and read back, all or none. initialize() calls it with fl::module_base(); the fixture
// with its own 64 KB-aligned image. install_at(p1, p2, counters) is the same with base = p1 - P1's RVA (p2 must be base
// + P2's RVA). LastError preserved.
bool install_base(std::uintptr_t base, bool counters);
#ifdef X3M_COLLIDE_BOX_CULL_FL_FIXTURE
void fixture_base(std::uintptr_t base); // initialize() uses `base` instead of fl::module_base() (0: the real one)
#endif
}
