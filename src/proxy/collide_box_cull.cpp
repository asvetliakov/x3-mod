#include "collide_box_cull.h"
#include "config.h"
#include "collide_box_cull_core.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include "log_tiers.h"
#include <windows.h>
#include <cstring>

// No per-pair C++ handler exists: the stubs are the emitted bytes of
// collide_box_cull_core.h (integer only, no call, no x87/SSE; the fixture build
// disassembles them). This module only installs them and reads the counters
// at Present.
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
using namespace x3m::collide_box_cull::core;
namespace core = x3m::collide_box_cull::core;
namespace engine_patch = x3m::engine_patch;
bool patched_ = false, counters_ = false;
engine_patch::Site p1_site_{}, p2_site_{};
std::uintptr_t p1_stub_ = 0, p2_stub_ = 0;
const char* state_ = "disabled";
Window window_;
unsigned frame_lines_ = 0;
bool windows_ = false; // the 300-frame collide_census window: X3M_TELEMETRY=1 or a logging group (log_tiers.h)

bool bytes_match(std::uintptr_t at, const unsigned char* expected, unsigned length) {
    unsigned char actual[64]{};
    return length <= sizeof actual && engine_patch::read_code(at, actual, length) && !std::memcmp(actual, expected, length);
}
// A `call rel32` at `at` must target `target`.
bool call_targets(std::uintptr_t at, std::uintptr_t target) {
    unsigned char code[5]{};
    if (!engine_patch::read_code(at, code, 5) || code[0] != 0xe8) return false;
    std::uint32_t rel = 0; std::memcpy(&rel, code + 1, 4);
    return at + 5 + rel == target;
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN),
// so the stubs' absolute operands (enabled byte, counters) can never point into freed memory.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE && module != nullptr;
}
engine_patch::SiteSpec spec_for(const char* name, std::uintptr_t address, const unsigned char* bytes) {
    engine_patch::SiteSpec spec{};
    spec.name = name; spec.address = address; spec.length = site_length; spec.ret_pop = ret_pop; spec.rel32_offset = 0;
    std::memcpy(spec.expected, bytes, site_length);
    return spec;
}
// Emits one stub followed by its 4-aligned continuation slot; returns the
// stub start and the slot, or 0 when the arena is full or the encoding failed.
template<class Encode>
std::uintptr_t emit_stub(Encode&& encode, void*** slot_out) {
    engine_patch::Emitter e(stub_capacity + 8);
    if (!e.ok()) return 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    unsigned char code[stub_capacity];
    // The continuation slot follows the code and its address is a dword
    // operand, so the length does not depend on it: encode once for the
    // length, then with the slot.
    const unsigned length = encode(std::uint32_t(at), 0u, code);
    if (!length) return 0;
    const std::uintptr_t slot = (at + length + 3) & ~std::uintptr_t(3);
    if (encode(std::uint32_t(at), std::uint32_t(slot), code) != length) return 0;
    e.bytes(code, length);
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < slot) e.byte(0xcc);
    e.dword(0);
    if (!e.finish()) return 0;
    *slot_out = reinterpret_cast<void**>(slot);
    return at;
}
bool hook_site(engine_patch::Site& site, const engine_patch::SiteSpec& spec, std::uintptr_t stub, void** slot, const char** reason) {
    site = engine_patch::Site{};
    if (!engine_patch::claim(site, spec)) { *reason = site.status; return false; }
    if (!stub || !slot || !engine_patch::store_pointer(slot, *site.entry) || !engine_patch::push_front(site, reinterpret_cast<void*>(stub))) {
        engine_patch::restore(site);
        *reason = "chain_failed"; return false;
    }
    return true;
}
bool windows_match(std::uintptr_t p1, std::uintptr_t p2) {
    return bytes_match(p1, p1_site_window, p1_site_window_length)
        && bytes_match(p1 + p1_compare_offset, p1_compare_window, p1_compare_window_length)
        && bytes_match(p1 + p1_reject_offset, p1_reject_window, p1_reject_window_length)
        && bytes_match(p1 + p1_continue_offset, p1_continue_window, p1_continue_window_length)
        && bytes_match(p2, p2_site_window, p2_site_window_length)
        && bytes_match(p2 + p2_compare_offset, p2_compare_window, p2_compare_window_length)
        && bytes_match(p2 + p2_continue_offset, p2_continue_window, p2_continue_window_length);
}
}

volatile unsigned char x3m_collide_box_cull_enabled = 0;          // declared extern "C" in the header
volatile std::uint32_t x3m_collide_box_cull_counters[4] = {0, 0, 0, 0};

namespace x3m::collide_box_cull {
bool install_at(std::uintptr_t p1, std::uintptr_t p2, bool counters) {
    if (patched_) { state_ = "already_installed"; return false; }
    const char* reason = nullptr;
    if (!p1 || !p2) reason = "invalid_site";
    else if (!engine_patch::install_window_open()) reason = "late_claim";
    else if (!windows_match(p1, p2)) reason = "bytes_mismatch";
    else if (!pin_self()) reason = "pin_failed";
    std::uintptr_t p1_stub = 0, p2_stub = 0; void** p1_slot = nullptr; void** p2_slot = nullptr;
    if (!reason) {
        const std::uint32_t enabled = std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_collide_box_cull_enabled));
        const auto slot_address = [](unsigned i) { return std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_collide_box_cull_counters[i])); };
        const Counters c1 = counters ? Counters{slot_address(0), slot_address(1)} : Counters{0, 0};
        const Counters c2 = counters ? Counters{slot_address(2), slot_address(3)} : Counters{0, 0};
        p1_stub = emit_stub([&](std::uint32_t at, std::uint32_t slot, unsigned char* out) {
            return encode_p1_stub(at, enabled, c1, slot, std::uint32_t(p1 + p1_continue_offset), out); }, &p1_slot);
        p2_stub = emit_stub([&](std::uint32_t at, std::uint32_t slot, unsigned char* out) {
            return encode_p2_stub(at, enabled, c2, slot, std::uint32_t(p2 + p2_continue_offset), out); }, &p2_slot);
        if (!p1_stub || !p2_stub) reason = "arena_full";
    }
    if (!reason) {
        x3m_collide_box_cull_enabled = 0;
        for (auto& c : x3m_collide_box_cull_counters) c = 0;
        if (!hook_site(p1_site_, spec_for("collide_box_cull_p1", p1, core::p1_site), p1_stub, p1_slot, &reason)) {
            if (p1_site_.patched_in) { patched_ = true; state_ = "p1_rollback_failed"; return false; } // registered for shutdown()
        } else if (!hook_site(p2_site_, spec_for("collide_box_cull_p2", p2, core::p2_site), p2_stub, p2_slot, &reason)) {
            // Partial install: P1 goes back; a live but unrestorable P2 keeps both registered so shutdown() tries again.
            if (p2_site_.patched_in) { patched_ = true; state_ = "p2_rollback_failed"; return false; }
            if (!engine_patch::restore(p1_site_)) { patched_ = true; state_ = "p1_rollback_failed"; return false; }
            state_ = reason; return false;
        } else {
            patched_ = true; counters_ = counters; p1_stub_ = p1_stub; p2_stub_ = p2_stub; reason = "ok";
            window_.reset(); frame_lines_ = 0;
            x3m_collide_box_cull_enabled = 1;
        }
    }
    state_ = reason;
    return patched_ && !std::strcmp(reason, "ok");
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) { SetLastError(error); return true; }
    windows_ = x3m::log_tier::telemetry();
    wchar_t setting[4]{};
    const DWORD length = x3m::config::get(L"X3M_COLLIDE_BOX_CULL", setting, 4);
    if (length == 0) { state_ = "disabled"; SetLastError(error); return false; }
    bool applied = false;
    const bool requested = length == 1 && setting[0] == L'1';
    if (!requested) state_ = "disabled";
    else if (!object_trace::executable_verified()) state_ = "executable_mismatch";
    // The two helpers and the four call sites are outside the windows the
    // fixture can replicate: checked here, on the real image only.
    else if (!bytes_match(sqrt_helper_va, sqrt_helper, sqrt_helper_length) || !bytes_match(ftol_helper_va, ftol_helper, ftol_helper_length)
             || !call_targets(p1_sqrt_call_va, sqrt_helper_va) || !call_targets(p1_ftol_call_va, ftol_helper_va)
             || !call_targets(p2_sqrt_call_va, sqrt_helper_va) || !call_targets(p2_ftol_call_va, ftol_helper_va)) state_ = "helper_mismatch";
    else applied = install_at(p1_site_va, p2_site_va, true);
    log("collide_box_cull requested=%u patched=%u reason=%s p1_site=0x%08lx p2_site=0x%08lx write_p1=%s write_p2=%s stub_p1=0x%08lx stub_p2=0x%08lx counters=%u enabled=%u",
        requested ? 1u : 0u, patched_ ? 1u : 0u, state_, static_cast<unsigned long>(p1_site_va), static_cast<unsigned long>(p2_site_va),
        p1_site_.patched_in ? (p1_site_.atomic_write ? "atomic" : "plain") : "none", p2_site_.patched_in ? (p2_site_.atomic_write ? "atomic" : "plain") : "none",
        static_cast<unsigned long>(p1_stub_), static_cast<unsigned long>(p2_stub_), counters_ ? 1u : 0u, x3m_collide_box_cull_enabled ? 1u : 0u);
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    x3m_collide_box_cull_enabled = 0;
    const bool p2_ok = engine_patch::restore(p2_site_), p1_ok = engine_patch::restore(p1_site_);
    patched_ = false; counters_ = false; p1_stub_ = p2_stub_ = 0; // the stubs stay in the arena (a thread may still be inside them)
    state_ = p2_ok && p1_ok ? "restored" : "restore_failed";
    SetLastError(error);
    return p2_ok && p1_ok;
}
const char* state() { return state_; }
std::uintptr_t p1_stub_address() { return patched_ ? p1_stub_ : 0; }
std::uintptr_t p2_stub_address() { return patched_ ? p2_stub_ : 0; }
void set_enabled(bool enabled) { x3m_collide_box_cull_enabled = patched_ && enabled ? 1 : 0; }
bool enabled() { return x3m_collide_box_cull_enabled != 0; }
void take_counters(std::uint32_t out[4]) {
    for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<std::uint32_t>(InterlockedExchange(reinterpret_cast<volatile LONG*>(&x3m_collide_box_cull_counters[i]), 0));
}
void present(unsigned long long device, unsigned long long frame, bool captured) {
    if (!patched_) return;
    const DWORD error = GetLastError();
    std::uint32_t values[counter_count]{};
    take_counters(values);
    window_.add(frame, values);
    if (captured)
        log("collide_census_frame device=%llu frame=%llu p1_pairs=%lu p1_rejected=%lu p2_cands=%lu p2_rejected=%lu counters=%u enabled=%u",
            device, frame, (unsigned long)values[0], (unsigned long)values[1], (unsigned long)values[2], (unsigned long)values[3], counters_ ? 1u : 0u, x3m_collide_box_cull_enabled ? 1u : 0u);
    if (window_.full()) {
        WindowSummary s;
        if (window_.close(s) && windows_)
            log("collide_census frame=%llu frames=%u p1_pairs_p50=%llu p1_pairs_max=%llu p1_pairs_sum=%llu p1_rejected_p50=%llu p1_rejected_max=%llu p1_rejected_sum=%llu "
                "p2_cands_p50=%llu p2_cands_max=%llu p2_cands_sum=%llu p2_rejected_p50=%llu p2_rejected_max=%llu p2_rejected_sum=%llu counters=%u enabled=%u",
                s.frame, s.frames, s.p50[0], s.max[0], s.sum[0], s.p50[1], s.max[1], s.sum[1], s.p50[2], s.max[2], s.sum[2], s.p50[3], s.max[3], s.sum[3],
                counters_ ? 1u : 0u, x3m_collide_box_cull_enabled ? 1u : 0u);
    }
    SetLastError(error);
}
}
