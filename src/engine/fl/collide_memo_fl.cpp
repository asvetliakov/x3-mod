// FL edition of x3m::collide_memo (src/proxy/collide_memo.h; the ap target keeps src/proxy/collide_memo.cpp):
// X3M_COLLIDE_MEMO=1 (and X3M_COLLIDE_MEMO_VERIFY=1) on X3FL.exe. The sole call of the mesh-pair query base+0x11aae0,
// at base+0x09853a inside the transform caller, is redirected to x3m_collide_memo_fl_thunk. A query whose complete input
// (collide_memo_sites_fl.h: AP's 81-word key from FL's register/stack/literal inputs) equals, bit for bit, one that found
// no contact in this or the previous frame is not run: the counters and the root scratch it would write are written from
// the stored values, contacts = 0, and the call returns. Every other query runs in the engine on the engine's own stack;
// a contact is never stored. The table, hash, minimum relaxation, expiry and miss classes are AP's
// (src/proxy/collide_memo_core.h); FL adds two gates in front of them: the init flag must be set (the one-time writes
// stay native) and MXCSR must mask every SIMD exception, with a change of its control bits clearing the table
// (collision-memo.md, "Implemented FP context correction"). Facts: docs/reverse-engineering/fl/collision-memo.md
// ("Port (2026-11-03)"); verifier verification/probe/verify_collide_memo_sites_fl.py; fixture
// verification/probe/fl_collide_memo.cpp.
//
// Threads and clock (as AP): the first thread through the thunk owns the memo; any other thread and a re-entered call
// go straight to the engine. Expiry counts Present calls; the table is also dropped after a device Reset, after 100,000
// queries without a Present and when a Present on the owner thread finds a query still in flight (an unwind went past
// the thunk). Path cost per query: the thunk saves ECX/EDX and XMM0-7 around the C lookup (the query reads XMM2's low
// word; the rest is kept for parity), one STMXCSR, one 324-byte key build and a 4-way set compare; no lock, no
// allocation, no API call but GetCurrentThreadId, no FP arithmetic, no x87 instruction, MXCSR never written, LastError
// never touched. Install (backend-load path, inside the engine_patch install window; later: late_claim): X3FL.exe
// identity, all fourteen bodies, the module pinned, the call claimed (one lock cmpxchg8b) and read back. One row with
// edition=fl. The FL loader has no unload path; shutdown() is for the fixture.
#include "collide_memo_fl.h"
#include "../../proxy/config.h"
#include "../../proxy/engine_patch.h"
#include "../../proxy/log_tiers.h"
#include "executable_identity_fl.h"
#include <windows.h>
#include <xmmintrin.h>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
using namespace x3m::collide_memo::core;
namespace engine_patch = x3m::engine_patch;
namespace F = x3m::fl::collide_memo_fl;
namespace W = x3m::fl::collide_windows;
bool windows_ = false; // the 300-frame collide_memo window: X3M_TELEMETRY=1 or a logging group (log_tiers.h)
bool patched_ = false, verify_ = false;
const char* state_ = "not_initialized";
engine_patch::CallSite site_{};
std::uintptr_t base_ = 0;
Table table_;
Counters counters_{}, logged_{};
x3m::collide_memo::FpCounters fp_{}, fp_logged_{};
F::ControlContext controls_;
volatile std::uint32_t frame_ = 0;  // written by present() only
volatile LONG owner_thread_ = 0;    // the first thread through the thunk
volatile LONG clear_requested_ = 0; // device Reset / stuck query: consumed by the owner at its next query
volatile LONG foreign_thread_ = 0;  // counted by the foreign threads themselves
// Owner thread only from here on.
volatile bool busy_ = false; // between lookup() returning 0 and store()
std::uint32_t seen_frame_ = 0, queries_since_tick_ = 0;
Key pending_key_;
bool pending_eligible_ = false;
Entry* pending_verify_ = nullptr;
int pending_miss_ = -1;
#ifdef X3M_COLLIDE_MEMO_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
#endif

inline std::uint32_t& word(std::uint32_t rva) {
    return *reinterpret_cast<std::uint32_t*>(base_ + rva);
}
std::uintptr_t current_base() {
#ifdef X3M_COLLIDE_MEMO_FL_FIXTURE
    if (fixture_base_) return fixture_base_;
#endif
    return x3m::fl::module_base();
}
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
    } reasons[] = {{"caller", "caller_mismatch"},
                   {"query", "query_mismatch"},
                   {"descent", "descent_mismatch"},
                   {"leaf", "leaf_mismatch"},
                   {"triangle", "triangle_mismatch"},
                   {"projection", "projection_mismatch"},
                   {"sat", "sat_mismatch"},
                   {"transpose_product", "transpose_product_mismatch"},
                   {"matrix_product", "matrix_product_mismatch"},
                   {"reset_model", "reset_model_mismatch"},
                   {"insert_triangle", "insert_triangle_mismatch"},
                   {"finish_model", "finish_model_mismatch"},
                   {"destroy_model", "destroy_model_mismatch"},
                   {"construct_model", "construct_model_mismatch"}};
    for (const auto& r : reasons)
        if (!std::strcmp(r.name, name)) return r.reason;
    return "window_mismatch";
}
}

extern "C" {
std::uint32_t x3m_collide_memo_fl_target = 0, x3m_collide_memo_fl_return = 0;

int __cdecl x3m_collide_memo_fl_lookup(const F::Regs* regs, const F::Args* args) {
    // Single-thread gate before any memo state is read (AP's rule).
    const LONG thread = static_cast<LONG>(GetCurrentThreadId()); // no LastError, no FP state
    LONG owner = owner_thread_;
    if (owner == 0) owner = InterlockedCompareExchange(&owner_thread_, thread, 0) == 0 ? thread : owner_thread_;
    if (owner != thread) {
        InterlockedIncrement(&foreign_thread_);
        return 2;
    }
    if (busy_) {
        ++counters_.reentered;
        return 2;
    }
    busy_ = true;
    const std::uint32_t frame = frame_;
    if (frame != seen_frame_) {
        seen_frame_ = frame;
        queries_since_tick_ = 0;
    }
    if (InterlockedExchange(&clear_requested_, 0) != 0 || ++queries_since_tick_ > queries_without_tick_limit) {
        table_.clear();
        queries_since_tick_ = 0;
        ++counters_.clears;
    }
    pending_verify_ = nullptr;
    pending_miss_ = -1;
    pending_eligible_ = false;
    // STMXCSR only: no control or status bit changes. Unmasked exceptions run the engine (its traps stay native).
    switch (controls_.judge(_mm_getcsr())) {
    case F::ControlContext::bypass:
        ++fp_.unmasked;
        ++counters_.ineligible;
        return 0;
    case F::ControlContext::accept_cleared:
        table_.clear();
        ++fp_.context_clears;
        break;
    case F::ControlContext::accept:
        break;
    }
    const F::Mode mode{word(F::flags_rva), word(F::cap_rva), word(F::tolerance_rva), word(F::minimum_rva),
                       word(F::init_rva)};
    pending_eligible_ = F::build_key(pending_key_, *regs, *args, mode);
    if (!pending_eligible_) {
        ++counters_.ineligible;
        return 0;
    }
    Entry* const entry = table_.find(pending_key_, frame);
    if (entry == nullptr) {
        ++counters_.misses;
        pending_miss_ = table_.classify(pending_key_);
        ++counters_.miss_count[pending_miss_];
        return 0;
    }
    if (verify_) {
        pending_verify_ = entry;
        return 0;
    } // the engine runs too; store() compares
    entry->frame = frame;
    F::replay_outputs(entry->outputs, [](std::uint32_t rva) -> std::uint32_t& { return word(rva); });
    ++counters_.hits;
    if (entry->key.words[minimum_value_word] != pending_key_.words[minimum_value_word]) ++counters_.min_relaxed_hits;
    counters_.skipped_visits += entry->outputs.visits;
    counters_.skipped_triangles += entry->outputs.triangles;
    busy_ = false;
    return 1;
}
void __cdecl x3m_collide_memo_fl_store() { // owner thread only: reached on lookup() == 0 alone
    busy_ = false;
    if (!pending_eligible_) return;
    pending_eligible_ = false;
    Outputs now;
    F::read_outputs(now, [](std::uint32_t rva) -> std::uint32_t& { return word(rva); });
    const bool contact = word(F::contacts_rva) != 0;
    if (pending_miss_ >= 0) counters_.miss_visits[pending_miss_] += now.visits;
    if (Entry* const expected = pending_verify_) {
        pending_verify_ = nullptr;
        if (!contact && !std::memcmp(&now, &expected->outputs, sizeof now)) {
            ++counters_.verified;
            expected->frame = frame_;
        } else {
            ++counters_.verify_mismatches;
            expected->valid = false;
        }
        return;
    }
    if (contact) {
        ++counters_.contacts;
        return;
    } // never stored: a contact is always computed
    ++counters_.stored;
    if (table_.store(pending_key_, now, frame_)) ++counters_.evictions;
}
}
// In: ECX = &R1, EDX = &T1, XMM2 low = s1, [ESP] = the caller's return address, [ESP+4..+0x18] = F::Args.
// ECX, EDX and XMM0-7 are saved around the C lookup and restored before any engine code runs. lookup() = 1: answered,
// EAX = 0 and a plain return (the caller drops the six words). 2: not this thread's or re-entered, straight to the query
// with every register as it arrived. 0: the return address is taken off, the query is called with the engine's exact
// stack and registers, then store() runs with EAX/ECX/EDX and XMM0-7 kept and control returns to the caller.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_collide_memo_fl_thunk
_x3m_collide_memo_fl_thunk:
    lea esp, [esp-128]
    movdqu [esp], xmm0
    movdqu [esp+16], xmm1
    movdqu [esp+32], xmm2
    movdqu [esp+48], xmm3
    movdqu [esp+64], xmm4
    movdqu [esp+80], xmm5
    movdqu [esp+96], xmm6
    movdqu [esp+112], xmm7
    lea esp, [esp-4]
    movd dword ptr [esp], xmm2
    push edx
    push ecx
    mov eax, esp
    lea edx, [esp+144]
    push edx
    push eax
    call _x3m_collide_memo_fl_lookup
    lea esp, [esp+8]
    pop ecx
    pop edx
    lea esp, [esp+4]
    movdqu xmm0, [esp]
    movdqu xmm1, [esp+16]
    movdqu xmm2, [esp+32]
    movdqu xmm3, [esp+48]
    movdqu xmm4, [esp+64]
    movdqu xmm5, [esp+80]
    movdqu xmm6, [esp+96]
    movdqu xmm7, [esp+112]
    lea esp, [esp+128]
    cmp eax, 1
    je 1f
    cmp eax, 2
    je 2f
    pop dword ptr [_x3m_collide_memo_fl_return]
    call dword ptr [_x3m_collide_memo_fl_target]
    push eax
    push ecx
    push edx
    lea esp, [esp-128]
    movdqu [esp], xmm0
    movdqu [esp+16], xmm1
    movdqu [esp+32], xmm2
    movdqu [esp+48], xmm3
    movdqu [esp+64], xmm4
    movdqu [esp+80], xmm5
    movdqu [esp+96], xmm6
    movdqu [esp+112], xmm7
    call _x3m_collide_memo_fl_store
    movdqu xmm0, [esp]
    movdqu xmm1, [esp+16]
    movdqu xmm2, [esp+32]
    movdqu xmm3, [esp+48]
    movdqu xmm4, [esp+64]
    movdqu xmm5, [esp+80]
    movdqu xmm6, [esp+96]
    movdqu xmm7, [esp+112]
    lea esp, [esp+128]
    pop edx
    pop ecx
    pop eax
    jmp dword ptr [_x3m_collide_memo_fl_return]
1:  xor eax, eax
    ret
2:  jmp dword ptr [_x3m_collide_memo_fl_target]
    .att_syntax
)");

namespace x3m::collide_memo {
bool install_base(std::uintptr_t base, bool verify_mode) {
    const DWORD error = GetLastError();
    const auto done = [&](const char* reason, bool ok) {
        state_ = reason;
        SetLastError(error);
        return ok;
    };
    if (patched_) return done("already_installed", false);
    if (!base || (base & 0xffff)) return done("invalid_site", false);
    if (!engine_patch::install_window_open()) return done("late_claim", false);
    {
        static unsigned char buffer[W::max_window];
        const auto read = [](std::uintptr_t at, unsigned char* out, unsigned n) {
            return engine_patch::read_code(at, out, n);
        };
        if (const char* refused = W::first_mismatch(read, base, W::memo_windows, W::memo_fixups, buffer))
            return done(window_reason(refused), false);
    }
    if (!pin_self()) return done("pin_failed", false);
    base_ = base;
    table_.clear();
    controls_.reset();
    verify_ = verify_mode;
    x3m_collide_memo_fl_target = std::uint32_t(base + F::query_rva);
    busy_ = false;
    owner_thread_ = 0;
    clear_requested_ = 0;
    seen_frame_ = frame_;
    queries_since_tick_ = 0;
    site_ = engine_patch::CallSite{};
    if (!engine_patch::claim_call(site_, base + F::site_rva, base + F::query_rva,
                                  reinterpret_cast<void*>(&x3m_collide_memo_fl_thunk))) {
        if (site_.patched_in) {
            patched_ = true; // registered: shutdown() tries again
            return done("rollback_failed", false);
        }
        return done(site_.status, false);
    }
    unsigned char now[F::call_length]{};
    if (!engine_patch::read_code(base + F::site_rva, now, F::call_length) ||
        std::memcmp(now, site_.patched, F::call_length)) {
        if (!engine_patch::restore_call(site_)) {
            patched_ = true;
            return done("rollback_failed", false);
        }
        return done("readback_mismatch", false);
    }
    patched_ = true;
    return done("ok", true);
}
bool install_at(const Addresses& a, bool verify_mode) {
    const std::uintptr_t base = a.site - F::site_rva;
    if (a.site < F::site_rva || a.target != base + F::query_rva) {
        if (!patched_) state_ = "invalid_site";
        return false;
    }
    return install_base(base, verify_mode);
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) {
        SetLastError(error);
        return true;
    }
    windows_ = x3m::log_tier::telemetry();
    wchar_t setting[4]{}, verify[4]{};
    const DWORD length = x3m::config::get(L"X3M_COLLIDE_MEMO", setting, 4);
    if (length == 0) {
        state_ = "disabled";
        SetLastError(error);
        return false;
    }
    const bool verify_mode = x3m::config::get(L"X3M_COLLIDE_MEMO_VERIFY", verify, 4) == 1 && verify[0] == L'1';
    const bool requested = length == 1 && setting[0] == L'1';
    const std::uintptr_t base = current_base();
    bool applied = false;
    if (!requested)
        state_ = "disabled";
    else if (!base)
        state_ = "executable_mismatch";
    else
        applied = install_base(base, verify_mode);
    log("collide_memo requested=%u patched=%u verify=%u edition=fl reason=%s site=0x%08lx rva=%06lx target=0x%08lx "
        "target_rva=%06lx write=%s handler=0x%08lx entries=%u",
        requested ? 1u : 0u, patched_ ? 1u : 0u, patched_ && verify_ ? 1u : 0u, state_,
        static_cast<unsigned long>(base ? base + F::site_rva : 0), static_cast<unsigned long>(F::site_rva),
        static_cast<unsigned long>(base ? base + F::query_rva : 0), static_cast<unsigned long>(F::query_rva),
        site_.patched_in ? (site_.atomic_write ? "atomic" : "plain") : "none",
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_collide_memo_fl_thunk)), ways * sets);
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    const bool back = engine_patch::restore_call(site_);
    patched_ = site_.patched_in; // a failed restoration stays registered for a later retry
    state_ = back ? "restored" : "restore_failed";
    SetLastError(error);
    return back;
}
const char* state() {
    return state_;
}
core::Counters counters() {
    core::Counters c = counters_;
    c.foreign_thread = static_cast<std::uint32_t>(foreign_thread_);
    return c;
}
FpCounters fp_counters() {
    return fp_;
}
void device_reset() {
    if (patched_) InterlockedExchange(&clear_requested_, 1);
}
void present(unsigned long long device, unsigned long long frame, bool) {
    if (!patched_) return;
    const std::uint32_t now = frame_ + 1;
    frame_ = now;
    // A query cannot be in flight on the thread that is presenting: busy here is what an unwind past the thunk left.
    if (busy_ && static_cast<LONG>(GetCurrentThreadId()) == owner_thread_) {
        busy_ = false;
        ++counters_.stuck_busy;
        InterlockedExchange(&clear_requested_, 1);
    }
    if (now % 300u != 0 || !windows_) return;
    const DWORD error = GetLastError();
    const Counters c = counters();
    const FpCounters fp = fp_;
    const auto d = [](std::uint32_t a, std::uint32_t b) { return static_cast<unsigned long>(a - b); };
    const std::uint32_t queries = (c.hits - logged_.hits) + (c.misses - logged_.misses) +
                                  (c.ineligible - logged_.ineligible) + (c.verified - logged_.verified) +
                                  (c.verify_mismatches - logged_.verify_mismatches);
    log("collide_memo device=%llu frame=%llu frames=300 edition=fl verify=%u queries=%lu hits=%lu misses=%lu stored=%lu contacts=%lu ineligible=%lu evictions=%lu skipped_visits=%lu "
        "skipped_triangles=%lu verified=%lu verify_mismatches=%lu foreign_thread=%lu reentered=%lu clears=%lu stuck_busy=%lu min_relaxed_hits=%lu "
        "fp_unmasked=%lu fp_context_clears=%lu "
        "miss_none_found=%lu miss_none_found_visits=%lu miss_xform_a=%lu miss_xform_a_visits=%lu miss_xform_b=%lu miss_xform_b_visits=%lu miss_scale=%lu miss_scale_visits=%lu miss_mode=%lu miss_mode_visits=%lu miss_models=%lu miss_models_visits=%lu miss_min_value=%lu miss_min_value_visits=%lu miss_expired=%lu miss_expired_visits=%lu",
        device, frame, verify_ ? 1u : 0u, static_cast<unsigned long>(queries), d(c.hits, logged_.hits),
        d(c.misses, logged_.misses), d(c.stored, logged_.stored), d(c.contacts, logged_.contacts),
        d(c.ineligible, logged_.ineligible), d(c.evictions, logged_.evictions),
        d(c.skipped_visits, logged_.skipped_visits), d(c.skipped_triangles, logged_.skipped_triangles),
        d(c.verified, logged_.verified), d(c.verify_mismatches, logged_.verify_mismatches),
        d(c.foreign_thread, logged_.foreign_thread), d(c.reentered, logged_.reentered), d(c.clears, logged_.clears),
        d(c.stuck_busy, logged_.stuck_busy), d(c.min_relaxed_hits, logged_.min_relaxed_hits),
        d(fp.unmasked, fp_logged_.unmasked), d(fp.context_clears, fp_logged_.context_clears),
        d(c.miss_count[0], logged_.miss_count[0]), d(c.miss_visits[0], logged_.miss_visits[0]),
        d(c.miss_count[1], logged_.miss_count[1]), d(c.miss_visits[1], logged_.miss_visits[1]),
        d(c.miss_count[2], logged_.miss_count[2]), d(c.miss_visits[2], logged_.miss_visits[2]),
        d(c.miss_count[3], logged_.miss_count[3]), d(c.miss_visits[3], logged_.miss_visits[3]),
        d(c.miss_count[4], logged_.miss_count[4]), d(c.miss_visits[4], logged_.miss_visits[4]),
        d(c.miss_count[5], logged_.miss_count[5]), d(c.miss_visits[5], logged_.miss_visits[5]),
        d(c.miss_count[6], logged_.miss_count[6]), d(c.miss_visits[6], logged_.miss_visits[6]),
        d(c.miss_count[7], logged_.miss_count[7]), d(c.miss_visits[7], logged_.miss_visits[7]));
    SetLastError(error);
    logged_ = c;
    fp_logged_ = fp;
}
#ifdef X3M_COLLIDE_MEMO_FL_FIXTURE
void fixture_base(std::uintptr_t base) {
    fixture_base_ = base;
}
#endif
}
