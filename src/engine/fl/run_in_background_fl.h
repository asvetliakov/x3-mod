#pragma once
#include <cstdint>
#include "../../proxy/run_in_background.h"

// FL edition of x3m::run_in_background (src/engine/fl/run_in_background_fl.cpp provides run_in_background.h's
// functions for X3FL.exe; the ap target keeps src/proxy/run_in_background.cpp). No thunk: the x3m_run_in_background_*
// symbols run_in_background.h declares are AP only and are not defined in the fl target. FL edition only.
namespace x3m::run_in_background {
// Both windows at base + RVA (run_in_background_sites_fl.h; fixups bound to base), then the startup decision's JE at
// base+0x0776ec replaced by two NOPs (one lock cmpxchg8b), read back, rolled back on failure. initialize() calls it with
// fl::module_base(); install_at(a) with a.window - 0x0776c2; the fixture with its own 64 KB aligned image. Returns
// whether the patch is live; state() carries the reason. LastError preserved.
bool install_base(std::uintptr_t base);
#ifdef X3M_RUN_IN_BACKGROUND_FL_FIXTURE
void fixture_base(std::uintptr_t base); // initialize() uses `base` instead of fl::module_base() (0: the real one)
#endif
}
