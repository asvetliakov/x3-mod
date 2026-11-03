#pragma once
#include <cstdint>
#include "../../proxy/resource_reader.h"

// FL edition of the fast resource reader and the DAT handle pool (src/engine/fl/resource_reader_fl.cpp): it provides
// x3m::resource_reader::{initialize, mode, installed, status, report, pool_requested, pool_installed} (resource_reader.h)
// in place of the AP hook module src/proxy/resource_reader.cpp, over the shared core src/proxy/resource_reader_core.cpp.
// docs/reverse-engineering/fl/resource-reader.md, "Port (2026-11-03)"; sites in resource_reader_sites_fl.h. FL only.
namespace x3m::fl::resource_reader_fl {
// The pool's state (`disabled`, `ok` or the refusal) and the first refusing window of either transaction.
const char* pool_status();
const char* window();
#ifdef X3M_RESOURCE_READER_FL_FIXTURE
// The fixture's image base (production: x3m::fl::module_base()).
void fixture_bind(std::uintptr_t base);
// Puts every owned write back (the entry and the eighteen call sites, the chain left callable), drains the pool and
// clears the state so initialize() runs again. Fixture only: the FL loader has no unload path.
void fixture_reset();
// Rebinds the core's mode over the installed stub (fast / verify, the verify bridge loading ECX from EAX).
void fixture_mode(x3m::resource_reader::Mode mode);
// The stub's continuation word (the original entry's tail), to plant a probe on the fallback path.
void** fixture_next_slot();
// The Nth fault point from now fails (reader: claim, read-back, chain; pool: each of the eighteen claims' read-back).
inline unsigned fixture_fail_step = 0;
#endif
}
