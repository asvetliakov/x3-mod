#pragma once
#include <cstdint>
#include "../../proxy/collide_sat_sse2.h"

// FL edition of x3m::collide_sat_sse2 (src/engine/fl/collide_sat_sse2_fl.cpp provides initialize(), shutdown() and
// state() of collide_sat_sse2.h for X3FL.exe; the ap target keeps src/proxy/collide_sat_sse2.cpp). FL's engine already
// performs the OBB separating-axis test in scalar SSE (docs/reverse-engineering/fl/collision-sat.md, "Port decision
// (2026-10-29)"): nothing is patched, install_at() and the AP thunk symbols are not provided. FL edition only.
namespace x3m::collide_sat_sse2 {
// True when the native SSE body and its sole call match collide_windows_fl_inc.h at the current base (the row's
// status=native_sse). False before initialize() and on any refusal.
bool native_sse();
#ifdef X3M_COLLIDE_SAT_FL_FIXTURE
void fixture_base(std::uintptr_t base); // initialize() uses `base` instead of fl::module_base() (0: the real one)
void fixture_rearm();                   // initialize() reports again (each fixture case is a fresh load)
#endif
}
