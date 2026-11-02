#pragma once
// Force-included (-include) into the unchanged production TU src/engine/fl/cull_small_parts_fl.cpp by
// verification/analysis/test_fl_cull_chain.py, with -DX3M_CULL_SMALL_PARTS_FL_SHIM, and nowhere else. It routes the
// module's own engine_patch::read_code (the window read and the jump's read-back), engine_patch::restore (its
// rollbacks and shutdown) and engine_patch::push_front through pass-through wrappers defined in
// verification/probe/fl_cull_chain.cpp, which call the real function unless the fixture armed a one-shot fault.
// engine_patch.cpp itself is not shimmed. The production source and build are untouched.
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include "../../src/proxy/engine_patch.h"
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count);
bool fixture_restore(Site& site);
void* fixture_push_front(Site& site, void* stub);
}
#ifdef X3M_CULL_SMALL_PARTS_FL_SHIM
#define read_code fixture_read_code
#define restore fixture_restore
#define push_front fixture_push_front
#endif
