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
// checks; VirtualProtect/FlushInstructionCache with rollback; the six original
// bytes return at shutdown (DLL detach). Refreshed at each Present and Reset.
// docs/architecture/lod-scale.md.
namespace x3m::lod_scale {
bool wanted();      // X3M_LOD_SCALE parses to a finite factor
bool initialize();  // backend-load path only; logs one lod_scale line when requested
void refresh();     // re-reads the game value; one aligned store when it changed
bool patched();
const char* status();
bool shutdown();    // restores the original bytes; true when nothing is installed
}
