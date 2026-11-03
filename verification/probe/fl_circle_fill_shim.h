#pragma once
// Force-included (-include) into the unchanged production TU src/engine/fl/circle_fill_fl.cpp by
// verification/analysis/test_fl_circle_fill.py, with -DX3M_CIRCLE_FILL_FL_SHIM, and nowhere else. It routes the
// module's own engine_patch::read_code (window reads, the read-back, shutdown's reads), engine_patch::restore (its
// rollback and shutdown) and engine_patch::push_front through pass-through wrappers defined in
// verification/probe/fl_circle_fill.cpp, which call the real function unless the fixture armed a one-shot fault.
// engine_patch.cpp itself is not shimmed. The production source and build are untouched.
#include <windows.h>
#include <cstdint>
#include "../../src/proxy/engine_patch.h"
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count);
bool fixture_restore(Site& site);
void* fixture_push_front(Site& site, void* stub);
}
#ifdef X3M_CIRCLE_FILL_FL_SHIM
#define read_code fixture_read_code
#define restore fixture_restore
#define push_front fixture_push_front
#endif
