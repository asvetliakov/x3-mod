#pragma once
// Force-included (-include) into the unchanged production TU
// src/proxy/sun_flare_fix.cpp by build_sun_flare_fix.py, with
// -DX3M_SUN_FLARE_FIX_SHIM, and nowhere else. It routes the module's three
// engine_patch calls that the rollback paths depend on (read_code,
// store_pointer, restore) through pass-through wrappers defined in
// sun_flare_fix_fixture.cpp, which call the real function unless the fixture
// armed a one-shot fault for that call. engine_patch.cpp (claim, the real
// lock cmpxchg8b write, VirtualProtect, FlushInstructionCache) is compiled
// unchanged. The production source and the production build are untouched.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../../src/proxy/engine_patch.h"
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count);
bool fixture_store_pointer(void** slot, void* value);
bool fixture_restore(Site& site);
}
#ifdef X3M_SUN_FLARE_FIX_SHIM
#define read_code fixture_read_code
#define store_pointer fixture_store_pointer
#define restore fixture_restore
#endif
