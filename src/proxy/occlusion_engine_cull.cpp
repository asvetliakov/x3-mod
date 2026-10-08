#include "occlusion_engine_cull.h"
#include "cull_small_parts.h"
#include "cull_small_parts_core.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <cstring>

// Nothing here runs inside the engine's pass: the stub is straight-line integer code emitted from
// occlusion_engine_core.h. initialize() runs on the backend-load path; publish() runs in the proxy's Clear hook at the
// sector view's Clear, take() in the Present hook, both on the application's render thread (the one that runs the
// pass and the only reader of the table), so no synchronisation (occlusion_engine_core.h, "Single-thread assumption").
static_assert(sizeof(void*) == 4, "x86 code patching only");
volatile std::uint32_t x3m_occlusion_engine_armed = 0; // declared extern "C" in the header
volatile std::uint32_t x3m_occlusion_engine_view = 0;
volatile std::uint32_t x3m_occlusion_engine_stamp = 0;
volatile std::uint32_t x3m_occlusion_engine_visits = 0;
volatile std::uint32_t x3m_occlusion_engine_skipped_parts = 0;
volatile std::uint32_t x3m_occlusion_engine_skipped_draws = 0;
volatile std::uint32_t x3m_occlusion_engine_rejected_model = 0;
volatile std::uint32_t x3m_occlusion_engine_rejected_stamp = 0;
volatile std::uint32_t x3m_occlusion_engine_rejected_position = 0;

namespace {
namespace core = x3m::occlusion_cull::engine;
namespace small = x3m::cull_small_parts::core;
namespace engine_patch = x3m::engine_patch;
core::Table table_{}; // the stub's table (24.8 KB, this module's data: pinned with it)
bool installed_ = false;
std::uintptr_t stub_ = 0;
const char* state_ = "off";

// Emits the stub followed by its 4-aligned continuation slot; 0 when the arena is full.
std::uintptr_t emit_stub(std::uint32_t cull_target, void*** slot_out) {
    engine_patch::Emitter e(core::stub_length + 8);
    if (!e.ok()) return 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    const std::uintptr_t slot = (at + core::stub_length + 3) & ~std::uintptr_t(3);
    unsigned char code[core::stub_length];
    const core::StubWords words{std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_occlusion_engine_armed)),
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_occlusion_engine_view)),
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_occlusion_engine_stamp)),
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(table_.entries)),
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_occlusion_engine_visits)),
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_occlusion_engine_skipped_parts)),
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_occlusion_engine_skipped_draws)),
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_occlusion_engine_rejected_model)),
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_occlusion_engine_rejected_stamp)),
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_occlusion_engine_rejected_position))};
    core::encode_stub(std::uint32_t(at), words, cull_target, std::uint32_t(slot), code);
    e.bytes(code, core::stub_length);
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < slot) e.byte(0xcc);
    e.dword(0);
    if (!e.finish()) return 0;
    *slot_out = reinterpret_cast<void**>(slot);
    return at;
}
void clear_counters() {
    x3m_occlusion_engine_visits = 0;
    x3m_occlusion_engine_skipped_parts = 0;
    x3m_occlusion_engine_skipped_draws = 0;
    x3m_occlusion_engine_rejected_model = 0;
    x3m_occlusion_engine_rejected_stamp = 0;
    x3m_occlusion_engine_rejected_position = 0;
}
}

namespace x3m::occlusion_engine_cull {
bool install_at(std::uintptr_t site, std::uintptr_t cull_target) {
    if (installed_) {
        state_ = "already_installed";
        return false;
    }
    const char* reason = nullptr;
    std::uintptr_t stub = 0;
    void** slot = nullptr;
    // The site is checked (window, bytes, cull target, the install window) before a stub is emitted, so a refusal
    // leaves the arena untouched; the chain re-checks (the window may close in between: late_claim).
    if (cull_small_parts::site_chainable(site, cull_target, &reason)) {
        reason = nullptr; // "ok": the chain below sets the final state
        if (!(stub = emit_stub(std::uint32_t(cull_target), &slot))) reason = "arena_full";
    }
    if (!reason) {
        x3m_occlusion_engine_armed = 0;
        x3m_occlusion_engine_view = 0;
        x3m_occlusion_engine_stamp = 0;
        clear_counters();
        table_.clear();
        if (!cull_small_parts::chain_stub(site, cull_target, reinterpret_cast<void*>(stub), slot, &reason)) {
            stub_ = 0; // the stub stays in the arena unreachable
        } else {
            installed_ = true;
            stub_ = stub;
            reason = "ok";
        }
    }
    state_ = reason;
    return installed_;
}
bool initialize(bool engine) {
    const DWORD error = GetLastError();
    if (installed_) {
        SetLastError(error);
        return true;
    }
    bool applied = false;
    const char* status = "off";
    if (!engine)
        state_ = "mode"; // on or off: the draw-level cull alone, nothing patched
    else if (!object_trace::executable_verified()) {
        state_ = "executable_mismatch";
        status = "refused";
    } else {
        applied = install_at(small::site_va, small::cull_va);
        status = applied ? "patched" : "refused";
    }
    log("occlusion_engine_cull status=%s reason=%s site=0x%08lx cull=0x%08lx stub=0x%08lx stub_length=%u table_slots=%u "
        "probe=%u window_shift=%u write=%s",
        status, state_, static_cast<unsigned long>(small::site_va), static_cast<unsigned long>(small::cull_va),
        static_cast<unsigned long>(stub_), core::stub_length, core::table_slots, core::table_probe, core::window_shift,
        cull_small_parts::site_write());
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!installed_) return true;
    x3m_occlusion_engine_armed = 0;
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
unsigned publish(const core::Ledger& ledger, std::uint32_t stamp, unsigned retest, core::PublishStats* stats) {
    if (!installed_ || !cull_small_parts::site_claimed()) return 0;
    // Disarmed while the table is rewritten: the pass runs after this Clear on the same thread, so the order is not
    // observable by the stub; the disarm only keeps a late reader (none exists) off a half-built table.
    x3m_occlusion_engine_armed = 0;
    const unsigned published = table_.publish(ledger, stamp, retest, stats);
    x3m_occlusion_engine_view = std::uint32_t(ledger.view);
    x3m_occlusion_engine_stamp = stamp;
    if (published && ledger.view) x3m_occlusion_engine_armed = 1;
    return published;
}
void disarm() {
    x3m_occlusion_engine_armed = 0;
}
Counters take() {
    Counters c{x3m_occlusion_engine_visits,         x3m_occlusion_engine_skipped_parts,
               x3m_occlusion_engine_skipped_draws,  x3m_occlusion_engine_rejected_model,
               x3m_occlusion_engine_rejected_stamp, x3m_occlusion_engine_rejected_position};
    x3m_occlusion_engine_armed = 0;
    clear_counters();
    return c;
}
const core::Table& table() {
    return table_;
}
}
