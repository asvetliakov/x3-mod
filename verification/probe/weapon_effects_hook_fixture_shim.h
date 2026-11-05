#pragma once
// Force-included (-include) into the unchanged production TU src/proxy/weapon_effects_patch.cpp by
// build_weapon_effects_hook.py with -DX3M_WEAPON_EFFECTS_SHIM, and nowhere else (engine_effects_patch_fixture_shim.h's
// pattern): the module's own read-back reads, its rollback VirtualProtect and its restore_call go through pass-through
// wrappers in weapon_effects_hook_fixture.cpp that call the real function unless the fixture armed a one-shot fault.
// engine_patch.cpp (claim_call and its write) is compiled unchanged.
#include <windows.h>
#include <cstdint>
#include "../../src/proxy/engine_patch.h"
namespace x3m::weapon_effects_fixture {
BOOL WINAPI virtual_protect(LPVOID address, SIZE_T size, DWORD protection, PDWORD previous);
}
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count);
bool fixture_restore_call(CallSite& site);
}
#ifdef X3M_WEAPON_EFFECTS_SHIM
#define VirtualProtect x3m::weapon_effects_fixture::virtual_protect
#define read_code fixture_read_code
#define restore_call fixture_restore_call
#endif
