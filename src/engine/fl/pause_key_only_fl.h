#pragma once
#include <cstdint>
#include "../../proxy/pause_key_only.h"

// FL edition of x3m::pause_key_only (src/engine/fl/pause_key_only_fl.cpp provides pause_key_only.h's functions for
// X3FL.exe; the ap target keeps src/proxy/pause_key_only.cpp). FL edition only.
namespace x3m::pause_key_only {
// Both windows at base + RVA (pause_key_only_sites_fl.h; fixups bound to base), then the 13-byte site at base+0x07880c
// rewritten for `key` (plain write: the span crosses qwords), read back, rolled back on failure. initialize() calls it
// with fl::module_base(); install_at(window, key) with window - 0x0787ee; the fixture with its own 64 KB aligned image.
// Returns whether the patch is live; state() carries the reason. LastError preserved.
bool install_base(std::uintptr_t base, std::uint32_t key);
#ifdef X3M_PAUSE_KEY_ONLY_FL_FIXTURE
void fixture_base(std::uintptr_t base); // initialize() uses `base` instead of fl::module_base() (0: the real one)
#endif
}
