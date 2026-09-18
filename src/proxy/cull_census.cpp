#include "cull_census.h"
#include "cull_census_core.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <atomic>
#include <cstring>

// Compiled with -mno-sse -mno-mmx -mfpmath=387 and -fno-exceptions (CMake
// source property; the fixture build compiles it the same way): the two
// handlers run on the engine's render thread in the middle of its cull pass
// with no CPU-state boundary, so nothing here may touch an XMM/MMX register,
// and they contain no floating-point arithmetic, so no x87 instruction either
// (verification/probe/check_no_x87.py walks them from their symbols).
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
using namespace x3m::cull_census::core;
namespace core = x3m::cull_census::core;
namespace engine_patch = x3m::engine_patch;
bool patched_ = false;
engine_patch::Site measure_site_{}, exit_site_{};
std::uintptr_t measure_stub_ = 0, exit_stub_ = 0;
const char* state_ = "disabled";
// The ring: committed once at install (never per node), never freed (the
// module is pinned and a render thread may be inside a handler at shutdown).
Entry* ring_ = nullptr;
// Written by the render thread inside the pass, read at Present on the same
// thread (the frame routine and Present share it); relaxed atomics keep the
// loads and stores plain words without inviting the compiler to cache them.
std::atomic<std::uint32_t> count_{0}, overflow_{0}, unmeasured_{0}, exited_{0};
std::uint32_t pending_node_ = 0, pending_index_ = no_index;
std::int32_t small_threshold_ = 0; // cull_small_parts' threshold for the frame being recorded (0 = none)

bool bytes_match(std::uintptr_t at, const unsigned char* expected, unsigned length) {
    unsigned char actual[measure_window_length]{};
    return length <= measure_window_length && engine_patch::read_code(at, actual, length) && !std::memcmp(actual, expected, length);
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN),
// so the stubs' CALLs and the enabled byte can never point into freed memory.
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
// stub start and the slot, or 0 when the arena is full.
template<unsigned Length, class Encode>
std::uintptr_t emit_stub(Encode&& encode, void*** slot_out) {
    engine_patch::Emitter e(Length + 8);
    if (!e.ok()) return 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    const std::uintptr_t slot = (at + Length + 3) & ~std::uintptr_t(3);
    unsigned char code[Length];
    encode(std::uint32_t(at), std::uint32_t(slot), code);
    e.bytes(code, Length);
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < slot) e.byte(0xcc);
    e.dword(0);
    if (!e.finish()) return 0;
    *slot_out = reinterpret_cast<void**>(slot);
    return at;
}
// Claims one site and chains a stub in front of its tail. On any failure the
// site is left as claim() left it (restored, or registered when rollback failed).
bool hook_site(engine_patch::Site& site, const engine_patch::SiteSpec& spec, std::uintptr_t stub, void** slot, const char** reason) {
    site = engine_patch::Site{};
    if (!engine_patch::claim(site, spec)) { *reason = site.status; return false; }
    if (!stub || !slot || !engine_patch::store_pointer(slot, *site.entry) || !engine_patch::push_front(site, reinterpret_cast<void*>(stub))) {
        engine_patch::restore(site);
        *reason = "chain_failed"; return false;
    }
    return true;
}
}

volatile unsigned char x3m_cull_census_enabled = 0; // declared extern "C" in the header

extern "C" void x3m_cull_census_measure(std::uint32_t node, std::int32_t measure, std::int32_t s, std::int32_t d, std::uint32_t view) {
    const std::uint32_t index = count_.load(std::memory_order_relaxed);
    pending_node_ = node;
    if (!ring_ || index >= ring_size) { overflow_.fetch_add(1, std::memory_order_relaxed); pending_index_ = no_index; return; }
    // Every field below was dereferenced by the pass itself on this node
    // before the site (0x0047d08b radius, 0x0047d19b model, 0x0047d1af flags,
    // 0x0047d258/0x0047d2a7 thresholds) and the parent link is read at
    // 0x0047d2a2..0x0047d2af with the same null test.
    const auto* n = reinterpret_cast<const std::uint32_t*>(node);
    Entry& e = ring_[index];
    e.node = node; e.model = n[model_offset / 4]; e.view = view;
    e.s = s; e.measure = measure; e.d = d; e.radius = std::int32_t(n[radius_offset / 4]);
    e.thr_1dc = std::int32_t(n[threshold_1dc_offset / 4]); e.thr_1d8 = std::int32_t(n[threshold_1d8_offset / 4]);
    const std::uint32_t parent = n[parent_offset / 4];
    e.limit = size_limit(e.thr_1d8, parent != 0, parent ? std::int32_t(reinterpret_cast<const std::uint32_t*>(parent)[threshold_1d8_offset / 4]) : 0);
    e.flags_in = n[flags12c_offset / 4]; e.flags_out = 0; e.lod = 0; e.exited = 0;
    pending_index_ = index;
    count_.store(index + 1, std::memory_order_relaxed);
}
extern "C" void x3m_cull_census_exit(std::uint32_t node) {
    if (pending_node_ != node) { unmeasured_.fetch_add(1, std::memory_order_relaxed); return; }
    pending_node_ = 0;
    const std::uint32_t index = pending_index_;
    pending_index_ = no_index;
    if (index == no_index || !ring_) return; // dropped at the measure site: counted in overflow
    const auto* n = reinterpret_cast<const std::uint32_t*>(node);
    Entry& e = ring_[index];
    e.flags_out = n[flags12c_offset / 4]; e.lod = std::int32_t(n[lod_offset / 4]); e.exited = 1;
    exited_.fetch_add(1, std::memory_order_relaxed);
}

namespace x3m::cull_census {
bool install_at(std::uintptr_t measure_site, std::uintptr_t exit_site) {
    if (patched_) { state_ = "already_installed"; return false; }
    const char* reason = nullptr;
    if (!measure_site || !exit_site || measure_site < measure_site_offset || exit_site < exit_site_offset) reason = "invalid_site";
    else if (!engine_patch::install_window_open()) reason = "late_claim";
    else if (!bytes_match(measure_site - measure_site_offset, measure_window, measure_window_length)
             || !bytes_match(exit_site - exit_site_offset, exit_window, exit_window_length)) reason = "bytes_mismatch";
    else if (!pin_self()) reason = "pin_failed";
    else if (!ring_ && !(ring_ = static_cast<Entry*>(VirtualAlloc(nullptr, sizeof(Entry) * ring_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)))) reason = "ring_alloc_failed";
    std::uintptr_t measure_stub = 0, exit_stub = 0; void** measure_slot = nullptr; void** exit_slot = nullptr;
    if (!reason) {
        const std::uint32_t enabled = std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_cull_census_enabled));
        measure_stub = emit_stub<measure_stub_length>([&](std::uint32_t at, std::uint32_t slot, unsigned char* out) {
            encode_measure_stub(at, enabled, std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_cull_census_measure)), slot, out); }, &measure_slot);
        exit_stub = emit_stub<exit_stub_length>([&](std::uint32_t at, std::uint32_t slot, unsigned char* out) {
            encode_exit_stub(at, enabled, std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_cull_census_exit)), slot, out); }, &exit_slot);
        if (!measure_stub || !exit_stub) reason = "arena_full";
    }
    if (!reason) {
        x3m_cull_census_enabled = 0;
        if (!hook_site(measure_site_, spec_for("cull_census_measure", measure_site, core::measure_site), measure_stub, measure_slot, &reason)) {
            if (measure_site_.patched_in) { patched_ = true; state_ = "measure_rollback_failed"; return false; } // registered for shutdown()
        } else if (!hook_site(exit_site_, spec_for("cull_census_exit", exit_site, core::exit_site), exit_stub, exit_slot, &reason)) {
            // Partial install: the measure site goes back; a live but unrestorable
            // exit site keeps both registered so shutdown() tries again.
            if (exit_site_.patched_in) { patched_ = true; state_ = "exit_rollback_failed"; return false; }
            if (!engine_patch::restore(measure_site_)) { patched_ = true; state_ = "measure_rollback_failed"; return false; }
            state_ = reason; return false;
        } else {
            patched_ = true; measure_stub_ = measure_stub; exit_stub_ = exit_stub; reason = "ok";
        }
    }
    state_ = reason;
    return patched_ && !std::strcmp(reason, "ok");
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) { SetLastError(error); return true; }
    wchar_t setting[4]{};
    const DWORD length = GetEnvironmentVariableW(L"X3M_CULL_CENSUS", setting, 4);
    if (length == 0) { state_ = "disabled"; SetLastError(error); return false; }
    bool applied = false;
    const bool requested = length == 1 && setting[0] == L'1';
    if (!requested) state_ = "disabled";
    else if (!object_trace::executable_verified()) state_ = "executable_mismatch";
    else applied = install_at(measure_site_va, exit_site_va);
    log("cull_census requested=%u patched=%u reason=%s measure_site=0x%08lx exit_site=0x%08lx write_measure=%s write_exit=%s stub_measure=0x%08lx stub_exit=0x%08lx ring=%u",
        requested ? 1u : 0u, patched_ ? 1u : 0u, state_, static_cast<unsigned long>(measure_site_va), static_cast<unsigned long>(exit_site_va),
        measure_site_.patched_in ? (measure_site_.atomic_write ? "atomic" : "plain") : "none", exit_site_.patched_in ? (exit_site_.atomic_write ? "atomic" : "plain") : "none",
        static_cast<unsigned long>(measure_stub_), static_cast<unsigned long>(exit_stub_), ring_size);
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    x3m_cull_census_enabled = 0;
    const bool exit_ok = engine_patch::restore(exit_site_), measure_ok = engine_patch::restore(measure_site_);
    patched_ = false; measure_stub_ = exit_stub_ = 0; // the stubs stay in the arena (a thread may still be inside them)
    state_ = exit_ok && measure_ok ? "restored" : "restore_failed";
    SetLastError(error);
    return exit_ok && measure_ok;
}
const char* state() { return state_; }
std::uintptr_t measure_stub_address() { return patched_ ? measure_stub_ : 0; }
std::uintptr_t exit_stub_address() { return patched_ ? exit_stub_ : 0; }
void begin_frame(bool capture) {
    if (!patched_) return;
    x3m_cull_census_enabled = 0;
    count_.store(0, std::memory_order_relaxed); overflow_.store(0, std::memory_order_relaxed);
    unmeasured_.store(0, std::memory_order_relaxed); exited_.store(0, std::memory_order_relaxed);
    pending_node_ = 0; pending_index_ = no_index;
    x3m_cull_census_enabled = capture ? 1 : 0;
}
void note_small_threshold(std::int32_t threshold) { small_threshold_ = threshold; }
Stats stats() {
    Stats s{};
    s.entries = count_.load(std::memory_order_relaxed); s.overflow = overflow_.load(std::memory_order_relaxed);
    s.unmeasured = unmeasured_.load(std::memory_order_relaxed); s.exited = exited_.load(std::memory_order_relaxed);
    s.armed = x3m_cull_census_enabled != 0;
    return s;
}
void present(unsigned long long device, unsigned long long frame, bool captured) {
    if (!patched_) return;
    const DWORD error = GetLastError();
    x3m_cull_census_enabled = 0; // the ring is read below on the same thread that fills it
    if (captured) {
        const Stats s = stats();
        const std::uint32_t entries = s.entries < ring_size ? s.entries : ring_size;
        log("cull_census_frame device=%llu frame=%llu entries=%lu overflow=%lu unmeasured=%lu exited=%lu ring=%u",
            device, frame, (unsigned long)entries, (unsigned long)s.overflow, (unsigned long)s.unmeasured, (unsigned long)s.exited, ring_size);
        for (std::uint32_t i = 0; i < entries && ring_; ++i) {
            const Entry& e = ring_[i];
            log("cull_census device=%llu frame=%llu view=%08lx node=%08lx model=%08lx s=%ld measure=%ld d=%ld radius=%ld thr_1dc=%ld thr_1d8=%ld limit=%ld flags_in=%08lx flags_out=%08lx lod=%ld verdict=%s",
                device, frame, (unsigned long)e.view, (unsigned long)e.node, (unsigned long)e.model, (long)e.s, (long)e.measure, (long)e.d, (long)e.radius,
                (long)e.thr_1dc, (long)e.thr_1d8, (long)e.limit, (unsigned long)e.flags_in, (unsigned long)e.flags_out, (long)e.lod, verdict_name(classify(e, small_threshold_)));
        }
    }
    count_.store(0, std::memory_order_relaxed); overflow_.store(0, std::memory_order_relaxed);
    unmeasured_.store(0, std::memory_order_relaxed); exited_.store(0, std::memory_order_relaxed);
    pending_node_ = 0; pending_index_ = no_index;
    SetLastError(error);
}
}
