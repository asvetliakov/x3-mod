#pragma once
#include <cstdint>
#include "../../proxy/fov.h"

// FL edition of x3m::fov (src/engine/fl/fov_fl.cpp provides fov.h's functions for X3FL.exe; the ap target keeps
// src/proxy/fov.cpp). FL edition only.
namespace x3m::fov {
// present() samples the admitted registry's +0x24 on its first call and then every this-many calls; current_focus()
// returns that sample (no engine read of its own).
constexpr unsigned fl_sample_interval = 64;
// Every window at base + its RVA (fov_sites_fl.h; fixups bound to base) before anything is emitted or written, then
// the two stubs emitted, the constructor literal (plain write), the INS_SetFocus claim and the load claim, all three or
// none. initialize() calls it with fl::module_base() and sites::constructor_focus(N); the fixture with its own 64 KB
// aligned image. Returns whether all three are live; state() carries the reason. LastError preserved.
bool install_base(std::uintptr_t base, std::uint32_t focus);
#ifdef X3M_FOV_FL_FIXTURE
void fixture_base(std::uintptr_t base);          // initialize() uses `base` instead of fl::module_base() (0: the real one)
void fixture_registry_slot(std::uintptr_t slot); // current_focus()/present() read this slot instead of the admitted one
#endif
}
