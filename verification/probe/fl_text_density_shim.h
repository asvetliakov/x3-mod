#pragma once
// Force-included (-include) into the unchanged production TU src/engine/fl/text_density_fl.cpp by
// verification/analysis/test_fl_text_density.py, with -DX3M_TEXT_DENSITY_FL_SHIM, and nowhere else. It routes the
// module's own engine_patch::read_code (window reads, every read-back), write_code (the single-byte edits, recorded for
// the order witness), restore and push_front through pass-through wrappers defined in verification/probe/
// fl_text_density.cpp, which call the real function unless the fixture armed a one-shot fault. engine_patch.cpp itself
// is not shimmed; the production source and build are untouched.
#include <windows.h>
#include <cstdint>
#include "../../src/proxy/engine_patch.h"
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count);
bool fixture_write_code(std::uintptr_t address, const unsigned char* bytes, unsigned n);
bool fixture_restore(Site& site);
void* fixture_push_front(Site& site, void* stub);
}
#ifdef X3M_TEXT_DENSITY_FL_SHIM
#define read_code fixture_read_code
#define write_code fixture_write_code
#define restore fixture_restore
#define push_front fixture_push_front
#endif
