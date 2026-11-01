#pragma once
// FL edition: the saturating replacement of X3FL.exe's three-component fixed-point scaler
// (docs/architecture/farnhams-legacy-scale-overflow.md). FL only; the ap target does not link it.
#include <windows.h>
#include <cstdint>

namespace x3m::fl::scale_fix {
// X3M_FL_SCALE_OVERFLOW_FIX (DLL default on, 0 off), fl::verified(), the helper's entry bytes and body hash; inside the
// engine_patch install window only (load_backend). One fl.scale_fix row either way.
bool initialize();
bool active();
const char* status();
unsigned long calls();     // replacement calls since install (relaxed, may miss increments across threads)
unsigned long saturated(); // components clamped since install
#ifdef X3M_FL_SCALE_FIX_FIXTURE
// The production gate and install at a synthetic image base (no fl::verified()).
const char* fixture_gate(uintptr_t base);
bool fixture_install(uintptr_t base);
// Restores the entry bytes and forgets the site (a later install claims afresh).
bool fixture_restore();
// The engine_patch tail: the displaced prologue plus a jump back, i.e. the original helper, still callable.
void* fixture_original();
// The next call checks QPC and writes the row if `saturated` changed, regardless of the period.
void fixture_force_sample();
#endif
}
