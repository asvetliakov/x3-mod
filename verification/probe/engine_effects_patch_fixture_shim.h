#pragma once
// Force-included (-include) into the unchanged production TU src/proxy/engine_effects_patch.cpp by
// build_engine_effects_patch.py, with -DX3M_ENGINE_EFFECTS_SHIM, and nowhere else. It routes the module's own
// read-back reads, its rollback VirtualProtect and its restore_call through pass-through wrappers defined in
// engine_effects_patch_fixture.cpp, which call the real function unless the fixture armed a one-shot fault for that
// call. engine_patch.cpp (claim_call's write) is not shimmed. The production source and build are untouched.
#include <windows.h>
#include <cstdint>
#include "../../src/proxy/engine_patch.h"
namespace x3m::engine_effects_fixture {
BOOL WINAPI virtual_protect(LPVOID address, SIZE_T size, DWORD protection, PDWORD previous);
}
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count);
bool fixture_restore_call(CallSite& site);
}
#ifdef X3M_ENGINE_EFFECTS_SHIM
#define VirtualProtect x3m::engine_effects_fixture::virtual_protect
#define read_code fixture_read_code
#define restore_call fixture_restore_call
#endif
