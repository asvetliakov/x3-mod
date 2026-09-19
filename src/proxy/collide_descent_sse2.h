#pragma once
#include "collide_descent_sse2_core.h"
#include <cstdint>

// SSE2 replacement of the engine's RAPID OBB-tree descent 0x004e2530
// (X3M_COLLIDE_DESCENT_SSE2=1; unset = vanilla, nothing patched),
// docs/reverse-engineering/sector-collide.md section 13. The rel32 of the sole
// external `call 0x004e2530` at 0x004e2956 is redirected to
// x3m_collide_descent_thunk; the engine's own body (and its four recursive
// calls) is then never entered. Same node pairs in the same order, the same
// head checks at every entry, the SAT of collide_sat_sse2_core.h, the engine's
// own leaf triangle test 0x004e2190 (ESI = a, EAX = b) and its counters.
// Independent of X3M_COLLIDE_SAT_SSE2 (whose call site sits inside the body
// this replaces and simply goes quiet), X3M_COLLIDE_BOX_CULL and
// X3M_COLLIDE_NARROW_CENSUS: the census's entry counter of 0x004e2530 (site 7)
// is fed from here, its leaf counter (site 8) still runs inside the engine's leaf.
//
// Hot path (about 2.3e5 node pairs per query next to a station): no lock, no
// log, no API call, no allocation; the pending stack is 64 entries on the
// frame and nests beyond that, never capped. The thunk keeps ECX/EDX, the C
// body keeps the callee-saved registers, MXCSR is read once per query (left
// alone when its control bits are the default, otherwise 0x1f80 for the body,
// the saved value around every leaf call and at the end), no x87/MMX
// instruction exists here and the x87 stack is empty at the site and at every
// leaf call, as in the engine. LastError is never touched.
namespace x3m::collide_descent_sse2 {
struct Addresses { std::uintptr_t site, target; };
bool initialize();  // backend-load path only; logs one collide_descent_sse2 line when the variable is set
bool shutdown();    // restores the call (dynamic-unload detach only); true when nothing is installed
// Verifies the windows before and after the call (offsets relative to the site,
// so the fixture passes a layout-preserving copy) and redirects it.
bool install_at(const Addresses& addresses);
const char* state();
}
extern "C" {
void x3m_collide_descent_thunk();   // the redirected call's target: cdecl in, EAX out, ECX/EDX kept
int __cdecl x3m_collide_descent_sse2(const x3m::collide_descent_sse2::core::Node* a, const x3m::collide_descent_sse2::core::Node* b, const float* R, const float* T, float s);
int __cdecl x3m_collide_descent_leaf(const x3m::collide_descent_sse2::core::Node* a, const x3m::collide_descent_sse2::core::Node* b);   // ESI = a, EAX = b, call 0x004e2190
}
