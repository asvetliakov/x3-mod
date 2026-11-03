// FL edition of x3m::collide_sat_sse2 (src/proxy/collide_sat_sse2.h; the ap target keeps src/proxy/collide_sat_sse2.cpp):
// X3M_COLLIDE_SAT_SSE2 on X3FL.exe. FL's OBB separating-axis test base+0x11c6d0 is already scalar SSE code (no x87, no
// helper call, no MXCSR write), the conversion the AP replacement makes; the AP thunk reads AP's ESI/EDI convention and
// AP's double core with its 2^-20 slack would change FL's verdicts. Port decision: keep the engine's native SAT
// (docs/reverse-engineering/fl/collision-sat.md, "Port decision (2026-10-29)", "Port (2026-11-03)"; sites:
// collide_sat_sse2_sites_fl.h). So nothing is patched, nothing is claimed, no DLL pin and no per-query work: on the
// backend-load path initialize() checks, once, that the native body and its sole call (the descent's call window) are
// the measured bytes at base + RVA with their two fixups bound to base, and writes one row
//   collide_sat_sse2 requested=0|1 patched=0 edition=fl status=native_sse reason=engine_already_sse ...
// whatever the setting says (setting_controls_native=0: the setting cannot turn the engine's SSE off), or status=refused
// with the first mismatching window when the bytes are not the measured ones. LastError preserved. The FL loader has no
// unload path; shutdown() has nothing to restore.
#include "collide_sat_sse2_fl.h"
#include "../../proxy/config.h"
#include "../../proxy/engine_patch.h"
#include "collide_sat_sse2_sites_fl.h"
#include "executable_identity_fl.h"
#include <windows.h>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
namespace {
namespace S = x3m::fl::collide_sat_fl;
namespace W = x3m::fl::collide_windows;
bool reported_ = false, native_ = false;
const char* state_ = "not_initialized";
#ifdef X3M_COLLIDE_SAT_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
#endif
std::uintptr_t current_base() {
#ifdef X3M_COLLIDE_SAT_FL_FIXTURE
    if (fixture_base_) return fixture_base_;
#endif
    return x3m::fl::module_base();
}
const char* mismatch_reason(const char* name) {
    if (!std::strcmp(name, "sat_body")) return "sat_body_mismatch";
    if (!std::strcmp(name, "sat_call")) return "sat_call_mismatch";
    return "window_mismatch";
}
}

namespace x3m::collide_sat_sse2 {
bool initialize() {
    const DWORD error = GetLastError();
    if (reported_) {
        SetLastError(error);
        return native_;
    }
    reported_ = true;
    wchar_t setting[4]{};
    const DWORD length = x3m::config::get(L"X3M_COLLIDE_SAT_SSE2", setting, 4);
    const bool requested = length == 1 && setting[0] == L'1';
    const std::uintptr_t base = current_base();
    if (!base)
        state_ = "executable_mismatch";
    else {
        static unsigned char buffer[W::max_window];
        const auto read = [](std::uintptr_t at, unsigned char* out, unsigned n) {
            return x3m::engine_patch::read_code(at, out, n);
        };
        const char* refused = W::first_mismatch(read, base, W::sat_windows, W::sat_fixups, buffer);
        state_ = refused ? mismatch_reason(refused) : "engine_already_sse";
        native_ = refused == nullptr;
    }
    log("collide_sat_sse2 requested=%u patched=0 edition=fl status=%s reason=%s site=%08lx rva=%06lx target=%08lx "
        "target_rva=%06lx implementation=%s setting_controls_native=0",
        requested ? 1u : 0u, native_ ? "native_sse" : "refused", state_,
        static_cast<unsigned long>(base ? base + S::call_rva : 0), static_cast<unsigned long>(S::call_rva),
        static_cast<unsigned long>(base ? base + S::sat_rva : 0), static_cast<unsigned long>(S::sat_rva),
        native_ ? "engine_native_sse" : "unknown");
    SetLastError(error);
    return native_;
}
bool shutdown() {
    return true; // nothing was patched
}
const char* state() {
    return state_;
}
bool native_sse() {
    return native_;
}
#ifdef X3M_COLLIDE_SAT_FL_FIXTURE
void fixture_base(std::uintptr_t base) {
    fixture_base_ = base;
}
void fixture_rearm() {
    reported_ = native_ = false;
    state_ = "not_initialized";
}
#endif
}
