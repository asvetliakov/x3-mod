#pragma once
#include <cstdint>
#include "../../proxy/dust_leak_fix.h"

// FL edition of x3m::dust_leak_fix (src/engine/fl/dust_leak_fix_fl.cpp provides dust_leak_fix.h's functions for
// X3FL.exe; the ap target keeps src/proxy/dust_leak_fix.cpp). FL edition only.
namespace x3m::dust_leak_fix {
// Every window of dust_leak_fix_sites_fl.h at base + its RVA (fixups bound to base), then the stub emitted, the ten-
// byte loop tail claimed at base+0x020ac4 and read back, all or nothing. initialize() calls it with fl::module_base();
// the fixture with its own 64 KB-aligned image. Returns whether the patch is live; state() carries the reason.
// LastError preserved.
bool install_base(std::uintptr_t base);
#ifdef X3M_DUST_LEAK_FIX_FL_FIXTURE
void fixture_base(std::uintptr_t base); // initialize() uses `base` instead of fl::module_base() (0: the real one)
#endif
}
