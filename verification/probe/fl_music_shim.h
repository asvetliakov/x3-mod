#pragma once
// Force-included (-include) into the unchanged production TUs src/engine/fl/{media_cue,music_keep,music_alert}_fl.cpp by
// verification/analysis/test_fl_{media_cue,music_keep,music_alert}.py, with -DX3M_FL_MUSIC_SHIM, and nowhere else. It
// routes the modules' own engine_patch::read_code (window reads and jump read-backs) and engine_patch::push_front
// through pass-through wrappers defined in verification/probe/fl_music_fixture_seam.cpp, which call the real function
// unless the fixture armed a one-shot fault. engine_patch.cpp itself is not shimmed; production is untouched.
#include <windows.h>
#include <cstdint>
#include "../../src/proxy/engine_patch.h"
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count);
void* fixture_push_front(Site& site, void* stub);
}
#ifdef X3M_FL_MUSIC_SHIM
#define read_code fixture_read_code
#define push_front fixture_push_front
#endif
