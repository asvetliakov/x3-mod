#pragma once
#include "engine_patch.h"
#include <windows.h>
#include <cstdint>

// Entry-counting trampolines on the engine's loading functions
// (X3M_TELEMETRY=1 plus X3M_LOADING_PROBES=1; docs/reverse-engineering/loading-probes.md
// lists every site with its verified bytes). Each site's entry is redirected
// through engine_patch to a generated stub that calls the light handler
// loading_trace::light::x3m_probe_enter and, for timed sites, hijacks the return
// address so x3m_probe_exit records the inclusive time. Count and cumulative
// time only: no per-call log line, no behaviour change. loading_trace::report
// prints one loading_probe line per site per report window (the same windows
// the loading_metric rows use, so the analysis attributes them to gaps).
// Exact-executable only: every site is byte-verified and fails closed alone.
namespace x3m::loading_probes {
constexpr unsigned site_count = 12;
struct SiteReport {
    const char* name;
    uintptr_t address;
    unsigned length;
    bool timed;
    const char* status;
    const char* extra[4];
};
bool initialize(); // reads X3M_LOADING_PROBES; requires object_trace::executable_verified()
bool requested();
unsigned installed_sites();
const SiteReport& site(unsigned index);
void report();   // loading_probe / loading_probe_caller / loading_probe_path lines (deltas)
void shutdown(); // restores the patched bytes
// The resource reader chains its stub in front of the resource_read site through this.
engine_patch::Site* resource_read_site();
#ifdef X3M_LOADING_PROBES_FIXTURE
// Fixture seam: install the probe stubs on fixture-provided sites (no
// executable verification), with the production kinds by index.
bool fixture_install(const engine_patch::SiteSpec* specs, unsigned count, const unsigned* kinds,
                     const uint32_t* size_global);
void fixture_shutdown();
#endif
}
