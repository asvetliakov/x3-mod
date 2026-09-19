#pragma once
#include "collide_sat_sse2_core.h"
#include <cstdint>

// SSE2 replacement of the engine's RAPID OBB-OBB separating-axis test
// (X3M_COLLIDE_SAT_SSE2=1; unset = vanilla, nothing patched), fix candidate (c)
// of docs/reverse-engineering/sector-collide.md 12.5, implemented in 12.8.
// The rel32 of the sole `call 0x004e3280` at 0x004e25a3 is redirected to
// x3m_collide_sat_thunk. The box test only prunes the BVH descent (contacts
// come from the leaf triangle test alone), and the replacement separates only
// where the engine's own compare separates with a 2^-20 relative margin to
// spare (and, like the engine, on an unordered compare), so it visits a superset of the engine's node pairs in the same order
// and finds the same contacts. Independent of X3M_COLLIDE_BOX_CULL and of
// X3M_COLLIDE_NARROW_CENSUS (site 7 of the census is the entry 0x004e2530, a
// different claim): any combination may be on.
//
// Hot path (about 2.3e5 calls per frame next to a station): no lock, no
// counter, no log, no API call. The thunk keeps ECX and EDX (the original
// never writes them), the callee-saved registers, MXCSR (read only when its
// control bits are the default, so only sticky flags can accumulate; otherwise
// saved, set to 0x1f80 for the body and restored bit-exact) and touches no
// x87/MMX state (the stack is empty at the site and stays so); LastError is
// never touched. XMM0-7 are clobbered: the engine's descent is x87-only code
// and holds nothing in them (verify_collide_sites.py, `sat_no_xmm_in_descent`).
namespace x3m::collide_sat_sse2 {
struct Addresses { std::uintptr_t site, target; };
bool initialize();  // backend-load path only; logs one collide_sat_sse2 line when the variable is set
bool shutdown();    // restores the call (dynamic-unload detach only); true when nothing is installed
// Verifies the windows before and after the call (offsets relative to the
// site, so the fixture passes a layout-preserving copy) and redirects it.
bool install_at(const Addresses& addresses);
const char* state();
bool installed();   // the engine's SAT call currently goes through the thunk
}
extern "C" {
// The redirected call's target: the engine's register/stack convention in, EAX out.
void x3m_collide_sat_thunk();
int __cdecl x3m_collide_sat_sse2(const float* R, const float* b_extents, const float* T, const float* a_extents);
extern const std::uint32_t x3m_collide_sat_mxcsr;
extern double x3m_collide_sat_min_gap;        // smallest t - (ra + rb) of the pruning tests since it was last reset; -1 once a NaN was seen
extern std::uint32_t x3m_collide_sat_prunes;   // pruning tests so far
}
