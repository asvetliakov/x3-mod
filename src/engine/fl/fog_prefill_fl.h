#pragma once
// FL edition of the fog transit prefill walk (docs/architecture/farnhams-legacy-prefill.md): the shared value-only
// fog_prefill::walk() over X3FL.exe's layout. FL edition only; capture.cpp's fog_prefill_poll calls it where the AP
// build calls the 0x400000-based walk.
#include "../../proxy/fog_prefill.h"
#include <cstdint>

namespace x3m::fl::fog_prefill_fl {
// X3FL.exe's layout (fl::module_base() + the generated slot RVAs, the recovery guard read first, NUL-stopped name)
// is admitted once fl::verified() holds, the sector reader is admitted (sector_background_fl anchors the count slot)
// and all anchors of fog_prefill_fl_inc.h match at the module base. Decided at the first call and kept for the
// process (the anchors are code bytes); one fl.prefill row. LastError preserved.
bool admitted();
const char* status(); // undecided, ready, executable_mismatch, sector_refused, anchor_mismatch, base_overflow
// One poll's walk: false (out untouched) when admitted() refused; else engine_memory::revalidate(), the shared walk
// through engine_memory::read, and one fl.prefill_sample row every X3M_FL_PREFILL_LOG seconds (unset 10, 0 off;
// the first poll writes one). No allocation, no engine call, nothing retained but counters. LastError preserved.
bool walk(fog_prefill::Result& out, std::uint32_t last_ready_id);
#ifdef X3M_FOG_PREFILL_FIXTURE
// Fixture seam: forget the decision and admit `base` by the anchors alone (no X3FL.exe identity, no sector reader).
bool fixture_admit(std::uintptr_t base);
const char* fixture_failed_anchor(); // the first anchor that refused, or nullptr
void fixture_force_row();            // the next walk() writes its row
#endif
} // namespace x3m::fl::fog_prefill_fl
