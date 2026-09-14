#pragma once
#include <cstdint>

// LOD threshold scale (X3M_LOD_SCALE=<factor>, 1..4; unset = vanilla, nothing
// patched): the one FMUL of the engine's LOD threshold loop at 0x0047d44b
// (`fmul dword [ecx+0x760]`, docs/reverse-engineering/lod-selection.md) is
// replaced by the same-length `fmul dword [mirror]`, an absolute-address
// operand in this DLL's data. The mirror holds game_value / factor while the
// game's own multiplier at *(0x606f34)+0x760 is in its known band, otherwise
// the game's bits unchanged (vanilla result). No register, EFLAGS or x87
// stack change: FMUL m32 for FMUL m32. Written on the backend-load path inside
// the engine_patch install window after the exact-executable and 17-byte window
// checks, with this module pinned (GetModuleHandleExW PIN) so the operand can
// never point into freed memory; VirtualProtect/FlushInstructionCache with
// rollback. Refreshed at each BeginScene, Present and Reset.
// docs/architecture/lod-scale.md.
namespace x3m::lod_scale {
bool initialize();  // backend-load path only; logs one lod_scale line when X3M_LOD_SCALE is set
void refresh();     // re-reads the game value; one aligned store when it changed
bool shutdown();    // restores the original bytes (dynamic-unload detach only; the pin makes that unreachable); true when nothing is installed
}
