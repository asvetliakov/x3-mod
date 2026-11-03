#pragma once
// Force-included (-include) into the unchanged production TU src/engine/fl/dust_leak_fix_fl.cpp by
// verification/analysis/test_fl_dust_leak_fix.py, with -DX3M_DUST_LEAK_FIX_FL_SHIM, and nowhere else. It routes the
// module's engine_patch::read_code (window reads, the read-back, shutdown's reads), store_pointer (the slot),
// push_front (the chain link) and restore (rollbacks and shutdown) through pass-through wrappers defined in
// verification/probe/fl_dust_leak_fix.cpp, which call the real function unless the fixture armed a one-shot fault.
// engine_patch.cpp itself (claim, the plain write, VirtualProtect, FlushInstructionCache) is not shimmed. The
// production source and build are untouched.
#include <windows.h>
#include <cstdint>
#include "../../src/proxy/engine_patch.h"
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count);
bool fixture_store_pointer(void** slot, void* value);
void* fixture_push_front(Site& site, void* stub);
bool fixture_restore(Site& site);
}
#ifdef X3M_DUST_LEAK_FIX_FL_SHIM
#define read_code fixture_read_code
#define store_pointer fixture_store_pointer
#define push_front fixture_push_front
#define restore fixture_restore
#endif
