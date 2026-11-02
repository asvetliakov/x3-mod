#pragma once
// Force-included (-include) into the unchanged production TU src/engine/fl/fov_fl.cpp by
// verification/analysis/test_fl_fov.py, with -DX3M_FOV_FL_SHIM, and nowhere else. It routes the module's own
// engine_patch::read_code (the window reads, the literal's read-back and restore reads, the jumps' read-backs),
// engine_patch::restore (its rollbacks and shutdown) and engine_patch::push_front through pass-through wrappers defined
// in verification/probe/fl_fov.cpp, which call the real function unless the fixture armed a one-shot fault.
// fov::sites::restore keeps its name through a forwarding template declared before the macros. engine_patch.cpp itself
// is not shimmed. The production source and build are untouched.
#include <windows.h>
#include <cstdint>
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/fov_sites.h"
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count);
bool fixture_restore(Site& site);
void* fixture_push_front(Site& site, void* stub);
}
namespace x3m::fov::sites {
template <class Ops>
inline const char* fixture_restore(Ops& ops, std::uintptr_t at, std::uint32_t focus, std::uint32_t protection,
                                   unsigned char found[write_length], bool* found_read) {
    return restore(ops, at, focus, protection, found, found_read);
}
}
#ifdef X3M_FOV_FL_SHIM
#define read_code fixture_read_code
#define restore fixture_restore
#define push_front fixture_push_front
#endif
