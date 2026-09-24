#pragma once
// Force-included (-include) into the unchanged production TU src/proxy/fov.cpp
// by build_fov_patch.py, with -DX3M_FOV_SHIM, and nowhere else. It routes the
// four calls the patch's CodeOps makes (VirtualProtect, FlushInstructionCache,
// engine_patch::read_code, engine_patch::write_code) through pass-through
// wrappers defined in fov_patch_fixture.cpp, which call the real function
// unless the fixture armed a one-shot fault for that call. The production
// source and the production build are untouched.
#include <windows.h>
#include <cstdint>
#include "../../src/proxy/engine_patch.h"
namespace x3m::fov_fixture {
BOOL WINAPI virtual_protect(LPVOID address, SIZE_T size, DWORD protection, PDWORD previous);
BOOL WINAPI flush_instruction_cache(HANDLE process, LPCVOID address, SIZE_T size);
}
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count);
bool fixture_write_code(std::uintptr_t address, const unsigned char* bytes, unsigned n);
}
#ifdef X3M_FOV_SHIM
#define VirtualProtect x3m::fov_fixture::virtual_protect
#define FlushInstructionCache x3m::fov_fixture::flush_instruction_cache
#define read_code fixture_read_code
#define write_code fixture_write_code
#endif
