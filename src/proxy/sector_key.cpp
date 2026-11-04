// Sector key, the edition part (sector_key.h). The windows are code bytes read through engine_patch::read_code; the
// chain is read through engine_memory::read (VirtualQuery-validated spans). Decided once per process at the first
// sample; nothing is retained but the value cache.
#include "sector_key.h"
#include "engine_memory.h"
#include "engine_patch.h"
#if X3M_EDITION_FL
#include "../engine/fl/executable_identity_fl.h"
#else
#include "object_trace.h"
#endif
#include <windows.h>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "Verified x86 image layout only");
namespace x3m::sector_key {
namespace {
#if X3M_EDITION_FL
constexpr const core::Edition& running = core::fl;
#else
constexpr const core::Edition& running = core::ap;
#endif
bool decided = false, ready = false;
const char* state = "undecided";
core::Layout bound{};
core::Cache cache{};
struct EngineMemory {
    bool read(std::uint32_t at, void* out, unsigned n) { return engine_memory::read(at, out, n); }
};
bool read_code(std::uint32_t at, unsigned char* out, unsigned n) {
    return engine_patch::read_code(at, out, n);
}
void decide() {
    decided = true;
    std::uint32_t base = 0;
    const char* failed = "none";
#if X3M_EDITION_FL
    const bool identity = fl::verified();
    if (identity) base = std::uint32_t(fl::module_base());
#else
    const bool identity = object_trace::executable_verified();
    if (identity) base = core::preferred_base;
#endif
    if (!identity || !base) {
        state = "executable_mismatch";
    } else if (std::uint64_t(base) + 0x00400000u > 0xffffffffull) {
        state = "base_overflow";
    } else if (const char* bad = core::first_mismatch(read_code, running, base)) {
        state = "window_mismatch";
        failed = bad;
    } else {
        state = "ready";
        ready = true;
        bound = core::layout(running, base);
    }
    log("sky_stars_sector_check status=%s edition=%s base=0x%08lx windows=%u failed=%s vm_global=0x%08lx native_id_member=%u",
        state, running.name, static_cast<unsigned long>(base), running.window_count, failed,
        static_cast<unsigned long>(bound.vm_global), bound.native_id_member);
}
} // namespace

core::Result sample(std::uint32_t sector) noexcept {
    const DWORD error = GetLastError();
    if (!decided) decide();
    core::Result r;
    r.sector = sector;
    if (ready) {
        EngineMemory memory;
        r = core::read(memory, bound, sector, cache);
    }
    SetLastError(error);
    return r;
}
void invalidate() noexcept { cache.clear(); }
const char* edition() noexcept { return running.name; }
const char* status() noexcept { return state; }
} // namespace x3m::sector_key
