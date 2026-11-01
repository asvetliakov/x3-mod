#pragma once
// FL edition of the sector-background reader (docs/architecture/farnhams-legacy-sector.md): the shared
// value-only sector_background::sample() over X3FL.exe's layout. FL edition only; capture.cpp's
// sector_background_context calls it where the AP build calls the 0x400000-based reader.
#include "../../proxy/sector_background.h"
#include <cstdint>

namespace x3m::fl::sector_background_fl {
// X3FL.exe's layout (fl::module_base() + the generated slot RVAs, config +0x758, NUL-stopped name) is admitted
// once fl::verified() holds and all anchors of sector_background_fl_inc.h match at the module base. Decided at
// the first call and kept for the process (the anchors are code bytes); one fl.sector row. LastError preserved.
bool admitted();
const char* status(); // undecided, ready, executable_mismatch, anchor_mismatch, base_overflow
// One frame's sample: false (out untouched) when admitted() refused; else engine_memory::revalidate(), the
// shared sample through engine_memory::read, and one fl.sector_sample row every X3M_FL_SECTOR_LOG seconds
// (unset 10, 0 off). No allocation, no engine call, nothing retained but value copies. LastError preserved.
bool sample(sector_background::Sample& out, unsigned walk);
#ifdef X3M_SECTOR_BACKGROUND_FIXTURE
// Fixture seam: forget the decision and admit `base` by the anchors alone (no X3FL.exe identity).
bool fixture_admit(std::uintptr_t base);
const char* fixture_failed_anchor(); // the first anchor that refused, or nullptr
void fixture_force_sample();         // the next sample() writes its row
#endif
} // namespace x3m::fl::sector_background_fl
