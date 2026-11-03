#pragma once
#include <cstdint>
#include "../../proxy/ui_scale.h"

// FL edition of x3m::ui_scale (src/engine/fl/ui_scale_fl.cpp provides ui_scale.h's functions for X3FL.exe; the ap
// target keeps src/proxy/ui_scale.cpp). FL edition only.
namespace x3m::ui_scale {
// The excluded camera present() published (0 = none) and the projection stub's two counters since the last report.
std::uint32_t excluded_camera();
unsigned scaled_count();
unsigned excluded_count();
#ifdef X3M_UI_SCALE_FL_FIXTURE
void fixture_base(std::uintptr_t base); // initialize() uses `base` instead of fl::module_base() (0: the real one)
void fixture_reset();                   // forgets the one-shot state once nothing is registered (after shutdown())
#endif
}
