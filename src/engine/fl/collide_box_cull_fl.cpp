// FL edition of x3m::collide_box_cull (src/proxy/collide_box_cull.h; the ap target keeps src/proxy/collide_box_cull.cpp):
// X3M_COLLIDE_BOX_CULL=1 on X3FL.exe. Two engine_patch trampolines on the sector collision pass's square-root pair
// tests: P1 base+0x06723f (the all-pairs loop) and P2 base+0x066859 (the swept scan), each a six-byte span of two whole
// MOVs. The stubs (collide_box_cull_sites_fl.h) run AP's integer box test with FL's margin 256 and, on a prospective
// reject, one MXCSR read (precision masked and already sticky, so skipping the inexact SSE distance changes no observable
// FP state); a reject jumps to the engine's own continuation (P1 base+0x067e7e with ESI kept, P2 base+0x066a1b with ESI
// = the count [ESP+0x14], as the native path leaves it), every other arrival runs the displaced MOVs and the native
// distance. Class 7 on either object always takes the native path at P1 (its distance is reused); P2 has no class-7 case.
// Facts and proof: docs/reverse-engineering/fl/collision-box.md ("Port (2026-11-03)"); verifier
// verification/probe/verify_collide_box_cull_sites_fl.py; fixture verification/probe/fl_collide_box_cull.cpp.
//
// Install (backend-load path, inside the engine_patch install window; later: late_claim): X3FL.exe identity
// (fl::module_base() != 0), all nine windows at base + RVA before anything is emitted; the module pinned (the stubs name
// its enabled byte and counters); both stubs and their slots in the never-freed arena; P1 claimed, chained, read back,
// then P2; any failure takes back what is live (a rollback that cannot complete keeps the site registered). One row with
// edition=fl. Per Present: the four counters read and zeroed, the AP collide_census rows with edition=fl. The FL loader
// has no unload path (cull_small_parts_fl.cpp); shutdown() is for the fixture. LastError preserved throughout.
#include "collide_box_cull_fl.h"
#include "../../proxy/config.h"
#include "../../proxy/engine_patch.h"
#include "../../proxy/log_tiers.h"
#include "collide_box_cull_sites_fl.h"
#include "executable_identity_fl.h"
#include <windows.h>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 code patching only");

volatile unsigned char x3m_collide_box_cull_enabled = 0; // declared extern "C" in collide_box_cull.h
volatile std::uint32_t x3m_collide_box_cull_counters[4] = {0, 0, 0, 0};

namespace {
namespace engine_patch = x3m::engine_patch;
namespace F = x3m::fl::collide_box_cull_fl;
namespace W = x3m::fl::collide_windows;
namespace ap = x3m::collide_box_cull::core;
bool patched_ = false, counters_ = false, windows_ = false;
engine_patch::Site p1_site_{}, p2_site_{};
std::uintptr_t p1_stub_ = 0, p2_stub_ = 0;
const char* state_ = "not_initialized";
ap::Window window_;
#ifdef X3M_COLLIDE_BOX_CULL_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
#endif

std::uintptr_t current_base() {
#ifdef X3M_COLLIDE_BOX_CULL_FL_FIXTURE
    if (fixture_base_) return fixture_base_;
#endif
    return x3m::fl::module_base();
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the stubs' absolute operands (the
// enabled byte, the counters) can never point into freed memory.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE &&
           module != nullptr;
}
const char* window_reason(const char* name) {
    static const struct {
        const char* name;
        const char* reason;
    } reasons[] = {{"radius", "radius_mismatch"},
                   {"class7_entry", "class7_entry_mismatch"},
                   {"p1_distance", "p1_distance_mismatch"},
                   {"p1_class7_reject", "p1_class7_reject_mismatch"},
                   {"p1_continue", "p1_continue_mismatch"},
                   {"p2_inbound", "p2_inbound_mismatch"},
                   {"p2_distance", "p2_distance_mismatch"},
                   {"p2_continue", "p2_continue_mismatch"},
                   {"sqrt_helper", "sqrt_helper_mismatch"}};
    for (const auto& r : reasons)
        if (!std::strcmp(r.name, name)) return r.reason;
    return "window_mismatch";
}
// One stub followed by its 4-aligned continuation slot; 0 when the arena is full or the encoding failed.
template <class Encode> std::uintptr_t emit_stub(Encode&& encode, void*** slot_out) {
    engine_patch::Emitter e(F::stub_capacity + 8);
    if (!e.ok()) return 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    unsigned char code[F::stub_capacity];
    // The slot is a dword operand, so the length does not depend on it: encode once for the length, then with the slot.
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
engine_patch::SiteSpec spec_for(const char* name, std::uintptr_t address, const unsigned char* bytes) {
    engine_patch::SiteSpec spec{};
    spec.name = name;
    spec.address = address;
    spec.length = F::site_length;
    spec.ret_pop = F::ret_pop; // an interior site: no return address on the stack, FL's plain RET
    spec.rel32_offset = 0;
    std::memcpy(spec.expected, bytes, F::site_length);
    return spec;
}
// The original bytes back over a site we claimed; false (the site stays registered) when they cannot be confirmed.
bool take_back(engine_patch::Site& site, const unsigned char* bytes) {
    engine_patch::restore(site);
    unsigned char now[F::site_length]{};
    if (engine_patch::read_code(site.spec.address, now, F::site_length) && !std::memcmp(now, bytes, F::site_length)) {
        site = engine_patch::Site{};
        return true;
    }
    site.patched_in = true; // our jump may still be there: shutdown() tries again
    return false;
}
// Claims one site, links the stub in front of the tail and reads the span and the chain head back. nullptr on success;
// else the reason, with the site taken back (or still registered: *live).
const char* hook_site(engine_patch::Site& site, const engine_patch::SiteSpec& spec, std::uintptr_t stub, void** slot,
                      bool* live) {
    *live = false;
    site = engine_patch::Site{};
    if (!engine_patch::claim(site, spec)) {
        *live = site.patched_in;
        return site.status;
    }
    const char* reason = nullptr;
    unsigned char now[F::site_length]{};
    if (!engine_patch::store_pointer(slot, *site.entry) ||
        !engine_patch::push_front(site, reinterpret_cast<void*>(stub)))
        reason = "chain_failed";
    else if (!engine_patch::read_code(spec.address, now, F::site_length) ||
             !F::claimed_span(now, site.patched, spec.expected) || *site.entry != reinterpret_cast<void*>(stub))
        reason = "readback_mismatch";
    if (!reason) return nullptr;
    if (!take_back(site, spec.expected)) {
        *live = true;
        return "rollback_failed";
    }
    return reason;
}
}

namespace x3m::collide_box_cull {
bool install_base(std::uintptr_t base, bool counters) {
    const DWORD error = GetLastError();
    const auto done = [&](const char* reason) {
        state_ = reason;
        SetLastError(error);
        return patched_ && !std::strcmp(reason, "ok");
    };
    if (patched_) return done("already_installed");
    if (!base || (base & 0xffff)) return done("invalid_site");
    if (!engine_patch::install_window_open()) return done("late_claim");
    {
        static unsigned char buffer[W::max_window];
        const auto read = [](std::uintptr_t at, unsigned char* out, unsigned n) {
            return engine_patch::read_code(at, out, n);
        };
        if (const char* refused = W::first_mismatch(read, base, W::box_windows, W::box_fixups, buffer))
            return done(window_reason(refused));
    }
    if (!pin_self()) return done("pin_failed");
    const std::uint32_t enabled = std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_collide_box_cull_enabled));
    const auto slot_address = [](unsigned i) {
        return std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_collide_box_cull_counters[i]));
    };
    const ap::Counters c1 = counters ? ap::Counters{slot_address(0), slot_address(1)} : ap::Counters{0, 0};
    const ap::Counters c2 = counters ? ap::Counters{slot_address(2), slot_address(3)} : ap::Counters{0, 0};
    void** p1_slot = nullptr;
    void** p2_slot = nullptr;
    const std::uintptr_t p1_stub = emit_stub(
        [&](std::uint32_t at, std::uint32_t slot, unsigned char* out) {
            return F::encode_p1_stub(at, enabled, c1, slot, std::uint32_t(base + F::p1_continue_rva), out);
        },
        &p1_slot);
    const std::uintptr_t p2_stub = emit_stub(
        [&](std::uint32_t at, std::uint32_t slot, unsigned char* out) {
            return F::encode_p2_stub(at, enabled, c2, slot, std::uint32_t(base + F::p2_continue_rva), out);
        },
        &p2_slot);
    if (!p1_stub || !p2_stub) return done("arena_full");
    x3m_collide_box_cull_enabled = 0; // the stubs are reachable only after the claims; armed once both are in
    for (auto& c : x3m_collide_box_cull_counters) c = 0;
    bool live = false;
    if (const char* reason = hook_site(p1_site_, spec_for("fl_collide_box_cull_p1", base + F::p1_site_rva, F::p1_site),
                                       p1_stub, p1_slot, &live)) {
        if (live) {
            patched_ = true; // registered for shutdown()
            return done("p1_rollback_failed");
        }
        return done(reason);
    }
    if (const char* reason = hook_site(p2_site_, spec_for("fl_collide_box_cull_p2", base + F::p2_site_rva, F::p2_site),
                                       p2_stub, p2_slot, &live)) {
        // Partial install: P1 goes back too; anything that cannot be confirmed back stays registered.
        if (live) {
            patched_ = true;
            return done("p2_rollback_failed");
        }
        if (!take_back(p1_site_, F::p1_site)) {
            patched_ = true;
            return done("p1_rollback_failed");
        }
        return done(reason);
    }
    patched_ = true;
    counters_ = counters;
    p1_stub_ = p1_stub;
    p2_stub_ = p2_stub;
    window_.reset();
    x3m_collide_box_cull_enabled = 1;
    return done("ok");
}
bool install_at(std::uintptr_t p1, std::uintptr_t p2, bool counters) {
    const std::uintptr_t base = p1 - F::p1_site_rva;
    if (p1 < F::p1_site_rva || p2 != base + F::p2_site_rva || (base & 0xffff)) {
        if (!patched_) state_ = "invalid_site";
        return false;
    }
    return install_base(base, counters);
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) {
        SetLastError(error);
        return p1_stub_ != 0;
    }
    windows_ = x3m::log_tier::telemetry();
    wchar_t setting[4]{};
    const DWORD length = x3m::config::get(L"X3M_COLLIDE_BOX_CULL", setting, 4);
    if (length == 0) {
        state_ = "disabled";
        SetLastError(error);
        return false;
    }
    const bool requested = length == 1 && setting[0] == L'1';
    const std::uintptr_t base = current_base();
    bool applied = false;
    if (!requested)
        state_ = "disabled";
    else if (!base)
        state_ = "executable_mismatch";
    else
        applied = install_base(base, true);
    log("collide_box_cull requested=%u patched=%u edition=fl reason=%s p1_site=0x%08lx p1_rva=%06lx p2_site=0x%08lx "
        "p2_rva=%06lx write_p1=%s write_p2=%s stub_p1=0x%08lx stub_p2=0x%08lx counters=%u enabled=%u margin=%d",
        requested ? 1u : 0u, patched_ ? 1u : 0u, state_,
        static_cast<unsigned long>(base ? base + F::p1_site_rva : 0), static_cast<unsigned long>(F::p1_site_rva),
        static_cast<unsigned long>(base ? base + F::p2_site_rva : 0), static_cast<unsigned long>(F::p2_site_rva),
        p1_site_.patched_in ? (p1_site_.atomic_write ? "atomic" : "plain") : "none",
        p2_site_.patched_in ? (p2_site_.atomic_write ? "atomic" : "plain") : "none",
        static_cast<unsigned long>(p1_stub_), static_cast<unsigned long>(p2_stub_), counters_ ? 1u : 0u,
        x3m_collide_box_cull_enabled ? 1u : 0u, int(F::margin));
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    x3m_collide_box_cull_enabled = 0; // a thread inside a stub takes the native path from here on
    const bool p2_ok = !p2_site_.patched_in || take_back(p2_site_, F::p2_site);
    const bool p1_ok = !p1_site_.patched_in || take_back(p1_site_, F::p1_site);
    patched_ = !(p1_ok && p2_ok);
    if (!patched_) {
        counters_ = false;
        p1_stub_ = p2_stub_ = 0; // the stubs stay in the arena (a thread may still be inside them)
    }
    state_ = p1_ok && p2_ok ? "restored" : "restore_failed";
    SetLastError(error);
    return !patched_;
}
const char* state() {
    return state_;
}
std::uintptr_t p1_stub_address() {
    return patched_ ? p1_stub_ : 0;
}
std::uintptr_t p2_stub_address() {
    return patched_ ? p2_stub_ : 0;
}
void set_enabled(bool enabled) {
    x3m_collide_box_cull_enabled = patched_ && p1_stub_ && enabled ? 1 : 0;
}
bool enabled() {
    return x3m_collide_box_cull_enabled != 0;
}
void take_counters(std::uint32_t out[4]) {
    for (unsigned i = 0; i < 4; ++i)
        out[i] = static_cast<std::uint32_t>(
            InterlockedExchange(reinterpret_cast<volatile LONG*>(&x3m_collide_box_cull_counters[i]), 0));
}
void present(unsigned long long device, unsigned long long frame, bool captured) {
    if (!patched_ || !p1_stub_) return;
    const DWORD error = GetLastError();
    std::uint32_t values[ap::counter_count]{};
    take_counters(values);
    window_.add(frame, values);
    if (captured)
        log("collide_census_frame device=%llu frame=%llu edition=fl p1_pairs=%lu p1_rejected=%lu p2_cands=%lu "
            "p2_rejected=%lu counters=%u enabled=%u",
            device, frame, (unsigned long)values[0], (unsigned long)values[1], (unsigned long)values[2],
            (unsigned long)values[3], counters_ ? 1u : 0u, x3m_collide_box_cull_enabled ? 1u : 0u);
    if (window_.full()) {
        ap::WindowSummary s;
        if (window_.close(s) && windows_)
            log("collide_census frame=%llu frames=%u edition=fl p1_pairs_p50=%llu p1_pairs_max=%llu p1_pairs_sum=%llu "
                "p1_rejected_p50=%llu p1_rejected_max=%llu p1_rejected_sum=%llu p2_cands_p50=%llu p2_cands_max=%llu "
                "p2_cands_sum=%llu p2_rejected_p50=%llu p2_rejected_max=%llu p2_rejected_sum=%llu counters=%u enabled=%u",
                s.frame, s.frames, s.p50[0], s.max[0], s.sum[0], s.p50[1], s.max[1], s.sum[1], s.p50[2], s.max[2],
                s.sum[2], s.p50[3], s.max[3], s.sum[3], counters_ ? 1u : 0u, x3m_collide_box_cull_enabled ? 1u : 0u);
    }
    SetLastError(error);
}
#ifdef X3M_COLLIDE_BOX_CULL_FL_FIXTURE
void fixture_base(std::uintptr_t base) {
    fixture_base_ = base;
}
#endif
}
