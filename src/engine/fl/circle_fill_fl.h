#pragma once
#include <cstdint>
#include "../../proxy/circle_fill.h"

// FL edition of x3m::circle_fill (src/engine/fl/circle_fill_fl.cpp provides circle_fill.h's functions for X3FL.exe;
// the ap target keeps src/proxy/circle_fill.cpp). FL edition only.
namespace x3m::circle_fill {
// Every window of circle_fill_sites_fl.h at base + its RVA (relocations bound to base), the module pinned, then the
// six-byte entry claimed (one lock cmpxchg8b), the stub chained in front and read back; all or none. initialize()
// calls it with fl::module_base(); the fixture with its own 64 KB-aligned image. Returns whether the claim is live and
// verified; state() carries the reason. LastError preserved.
bool install_base(std::uintptr_t base);
#ifdef X3M_CIRCLE_FILL_FL_FIXTURE
void fixture_base(std::uintptr_t base); // initialize() uses `base` instead of fl::module_base() (0: the real one)
#endif
}
extern "C" {
void x3m_circle_fill_fl_thunk();
extern std::uintptr_t x3m_circle_fill_fl_continue; // the engine_patch tail: the displaced prologue, then base+0x10c946
// The C side, entered from the stub with EFLAGS and every GPR saved (frame = the pushad/pushfd frame): 1 = handled
// (the stub returns to the caller), 0 = the stock routine runs from its first instruction.
std::uint32_t __cdecl x3m_circle_fill_fl_wide(std::uint32_t* frame);
}
