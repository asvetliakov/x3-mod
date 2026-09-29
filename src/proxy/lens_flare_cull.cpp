#include "lens_flare_cull.h"
#include "lens_flare_cull_core.h"
#include "cull_small_parts.h"
#include "engine_memory.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <cstring>

// Nothing here runs inside the engine's pass: the stub is straight-line integer
// code emitted from lens_flare_cull_core.h. initialize() runs on the
// backend-load path, begin_frame() and report() in the proxy's Present hook on
// the application's render thread (the one that runs the pass and the only
// writer of the bitmap and the flag), so no synchronisation.
static_assert(sizeof(void*) == 4, "x86 code patching only");
volatile std::uint32_t x3m_lens_flare_cull_enabled = 0; // declared extern "C" in the header
volatile std::uint32_t x3m_lens_flare_cull_culled = 0;
std::uint32_t x3m_lens_flare_cull_bitmap[x3m::lens_flare_cull::core::bitmap_words] = {};
static_assert(sizeof x3m_lens_flare_cull_bitmap == sizeof(x3m::lens_flare_cull::core::Bitmap), "the stub's bitmap");

namespace {
namespace core = x3m::lens_flare_cull::core;
namespace engine_patch = x3m::engine_patch;
#ifdef X3M_CULL_CENSUS_FIXTURE
std::uintptr_t body_global_ = core::census::body_global_va;
#else
constexpr std::uintptr_t body_global_ = core::census::body_global_va;
#endif
bool installed_ = false, gain_zero_ = false;
std::uintptr_t stub_ = 0;
const char* state_ = "off";
core::Table table_{};
bool found_[core::body_name_count] = {};
std::uint32_t bodies_ = 0, mapped_ = 0, scanned_ = 0, scanned_dynamic_ = 0, restarts_ = 0;
core::Mappings dynamic_{}; // the names resolved in dynamic slots, re-read every frame
std::uint32_t reported_ = 0, logged_mapped_ = 0xffffffffu;
unsigned body_rows_ = 0;
constexpr unsigned body_row_cap = 16; // lens_flare_cull_bodies rows per process (a game load re-binds the set)

bool bytes_match(std::uintptr_t at, const unsigned char* expected, unsigned length) {
    unsigned char actual[core::lens_walk_length]{};
    return length <= sizeof actual && engine_patch::read_code(at, actual, length) &&
           !std::memcmp(actual, expected, length);
}
core::Bitmap* bitmap() {
    return reinterpret_cast<core::Bitmap*>(x3m_lens_flare_cull_bitmap);
}
// Emits the stub followed by its 4-aligned continuation slot; 0 when the arena is full.
std::uintptr_t emit_stub(std::uint32_t cull_target, void*** slot_out) {
    engine_patch::Emitter e(core::stub_length + 8);
    if (!e.ok()) return 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    const std::uintptr_t slot = (at + core::stub_length + 3) & ~std::uintptr_t(3);
    unsigned char code[core::stub_length];
    core::encode_stub(std::uint32_t(at), std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_lens_flare_cull_enabled)),
                      std::uint32_t(reinterpret_cast<std::uintptr_t>(x3m_lens_flare_cull_bitmap)),
                      std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_lens_flare_cull_culled)), cull_target,
                      std::uint32_t(slot), code);
    e.bytes(code, core::stub_length);
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < slot) e.byte(0xcc);
    e.dword(0);
    if (!e.finish()) return 0;
    *slot_out = reinterpret_cast<void**>(slot);
    return at;
}
void publish() {
    x3m_lens_flare_cull_enabled = installed_ && gain_zero_ && mapped_ ? 1u : 0u;
}
// The body table now: a moved or shrunk table restarts the resolution, and so does any mapped dynamic slot whose
// name is no longer ours (a game load frees and refills the array, possibly at the same address with the same or a
// larger count, re-binding the dynamic ids: the at most 42 mapped dynamic slots are re-read every frame, in practice
// the four or five `v\010xx` names); a grown one scans only the new dynamic slots (and the newest one again: its
// name may have been mid-registration).
void refresh(bool log_rows, unsigned long long frame) {
    const core::Table t = core::read_table(&x3m::engine_memory::read, body_global_);
    if (!t.valid) return;
    const bool restart = !table_.valid || t.slots != table_.slots || t.fixed != table_.fixed ||
                         t.dynamic < table_.dynamic || !core::mappings_hold(&x3m::engine_memory::read, t, dynamic_);
    core::Resolution r;
    if (restart) {
        bitmap()->clear();
        std::memset(found_, 0, sizeof found_);
        dynamic_.clear();
        bodies_ = mapped_ = scanned_ = 0;
        scanned_dynamic_ = 0;
        if (table_.valid) ++restarts_;
        r = core::resolve(&x3m::engine_memory::read, t, bitmap(), found_, 0,
                          std::uint32_t(t.fixed) + std::uint32_t(t.dynamic), &dynamic_);
    } else if (std::uint32_t(t.dynamic) > scanned_dynamic_ || bodies_ < core::body_name_count) {
        r = core::resolve(&x3m::engine_memory::read, t, bitmap(), found_, std::uint32_t(t.fixed) + scanned_dynamic_,
                          std::uint32_t(t.fixed) + std::uint32_t(t.dynamic), &dynamic_);
    }
    bodies_ += r.resolved;
    mapped_ += r.mapped;
    scanned_ += r.scanned;
    scanned_dynamic_ = t.dynamic > 0 ? std::uint32_t(t.dynamic) - 1 : 0;
    table_ = t;
    publish();
    if (log_rows && mapped_ != logged_mapped_ && body_rows_ < body_row_cap) {
        ++body_rows_;
        logged_mapped_ = mapped_;
        x3m::log("lens_flare_cull_bodies bodies=%lu mapped=%lu scanned=%lu fixed=%ld dynamic=%ld enabled=%lu restarts=%lu frame=%llu",
            static_cast<unsigned long>(bodies_), static_cast<unsigned long>(mapped_),
            static_cast<unsigned long>(scanned_), static_cast<long>(t.fixed), static_cast<long>(t.dynamic),
            static_cast<unsigned long>(x3m_lens_flare_cull_enabled), static_cast<unsigned long>(restarts_), frame);
    }
}
}

namespace x3m::lens_flare_cull {
bool install_at(std::uintptr_t site, std::uintptr_t cull_target, bool gain_zero) {
    if (installed_) {
        state_ = "already_installed";
        return false;
    }
    const char* reason = nullptr;
    std::uintptr_t stub = 0;
    void** slot = nullptr;
    if (!engine_patch::install_window_open())
        reason = "late_claim";
    else if (!(stub = emit_stub(std::uint32_t(cull_target), &slot)))
        reason = "arena_full";
    if (!reason) {
        x3m_lens_flare_cull_enabled = 0;
        x3m_lens_flare_cull_culled = 0;
        if (!cull_small_parts::chain_stub(site, cull_target, reinterpret_cast<void*>(stub), slot, &reason)) {
            stub_ = 0; // the stub stays in the arena unreachable
        } else {
            installed_ = true;
            stub_ = stub;
            gain_zero_ = gain_zero;
            reason = "ok";
        }
    }
    state_ = reason;
    return installed_;
}
bool initialize(bool gain_zero) {
    const DWORD error = GetLastError();
    if (installed_) {
        SetLastError(error);
        return true;
    }
    bool applied = false;
    const char* status = "off";
    if (!gain_zero)
        state_ = "gain"; // G > 0: the blend law or the proxy skip, nothing patched
    else if (!object_trace::executable_verified()) {
        state_ = "executable_mismatch";
        status = "refused";
    } else if (!(bytes_match(core::lens_walk_va, core::lens_walk, core::lens_walk_length) &&
                 bytes_match(core::walker_call_va, core::walker_call, core::walker_call_length) &&
                 bytes_match(core::model_read_va, core::model_read, core::model_read_length))) {
        state_ = "lens_bytes_mismatch"; // the lens block does not walk the pass as documented: nothing patched
        status = "refused";
    } else {
        applied = install_at(core::site_va, core::cull_va, true);
        status = applied ? "patched" : "refused";
        if (applied) refresh(false, 0); // the table may not be up yet: begin_frame() keeps resolving
    }
    log("lens_flare_cull status=%s reason=%s bodies=%lu mapped=%lu site=0x%08lx cull=0x%08lx stub=0x%08lx write=%s",
        status, state_, static_cast<unsigned long>(bodies_), static_cast<unsigned long>(mapped_),
        static_cast<unsigned long>(core::site_va), static_cast<unsigned long>(core::cull_va),
        static_cast<unsigned long>(stub_), cull_small_parts::site_write());
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!installed_) return true;
    x3m_lens_flare_cull_enabled = 0;
    installed_ = false;
    stub_ = 0; // the stub stays in the arena (a thread may still be inside it); the site is the owner's to restore
    state_ = "disarmed";
    return true;
}
const char* state() {
    return state_;
}
bool installed() {
    return installed_;
}
std::uintptr_t stub_address() {
    return installed_ ? stub_ : 0;
}
void begin_frame(unsigned long long frame) {
    if (!installed_ || !cull_small_parts::site_claimed()) {
        x3m_lens_flare_cull_enabled = 0;
        return;
    }
    const DWORD error = GetLastError();
    refresh(true, frame);
    SetLastError(error);
}
void report(unsigned long long frame) {
    if (!installed_) return;
    const DWORD error = GetLastError();
    const std::uint32_t total = x3m_lens_flare_cull_culled;
    log("lens_flare_cull culled=%lu total=%lu enabled=%lu bodies=%lu mapped=%lu frame=%llu",
        static_cast<unsigned long>(total - reported_), static_cast<unsigned long>(total),
        static_cast<unsigned long>(x3m_lens_flare_cull_enabled), static_cast<unsigned long>(bodies_),
        static_cast<unsigned long>(mapped_), frame);
    reported_ = total;
    SetLastError(error);
}
Stats stats() {
    Stats s{};
    s.culled = x3m_lens_flare_cull_culled - reported_;
    s.total = x3m_lens_flare_cull_culled;
    s.bodies = bodies_;
    s.mapped = mapped_;
    s.scanned = scanned_;
    s.dynamic = std::uint32_t(table_.dynamic);
    s.restarts = restarts_;
    s.enabled = x3m_lens_flare_cull_enabled != 0;
    return s;
}
#ifdef X3M_CULL_CENSUS_FIXTURE
void set_body_table_global(std::uintptr_t va) {
    body_global_ = va;
    table_ = core::Table{};
}
#endif
}
