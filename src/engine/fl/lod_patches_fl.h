#pragma once
#include <cstdint>
#include "../../proxy/lod_occlusion.h"
#include "../../proxy/terran_station_lod.h"

// FL edition of x3m::terran_station_lod and x3m::lod_occlusion (src/engine/fl/terran_station_lod_fl.cpp and
// lod_occlusion_fl.cpp provide terran_station_lod.h / lod_occlusion.h for X3FL.exe; the ap target keeps
// src/proxy/terran_station_lod.cpp and lod_occlusion.cpp). One byte each (lod_patch_sites_fl.h). FL edition only.
namespace x3m::terran_station_lod {
// The 31-byte window at base + 0x095e82, then the ModRM byte at base + 0x095e98 (c1 -> c0), read back.
bool install_base(std::uintptr_t base);
#ifdef X3M_LOD_PATCHES_FL_FIXTURE
void fixture_base(std::uintptr_t base); // initialize() uses `base` instead of fl::module_base() (0: the real one)
#endif
}
namespace x3m::lod_occlusion {
// The 20-byte window at base + 0x0e56b1 (its fixup equal to base + 0x1a9014), then the rel8 at base + 0x0e56c1
// (6f -> 00), read back.
bool install_base(std::uintptr_t base);
#ifdef X3M_LOD_PATCHES_FL_FIXTURE
void fixture_base(std::uintptr_t base);
#endif
}
