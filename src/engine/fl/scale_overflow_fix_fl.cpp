// FL edition: saturating replacement of X3FL.exe's three-component fixed-point scaler
// (docs/architecture/farnhams-legacy-scale-overflow.md). The helper at module_base() + helper_rva computes
// out[i] = c ? (int64)a[i] * b / c : 0 with `imul edx; idiv ecx`; its 32-bit idiv raises
// STATUS_INTEGER_OVERFLOW when the quotient does not fit (FL run 3: the cockpit camera velocity
// Δ × 1000 / dt with dt = 2). The entry is claimed through engine_patch::claim (byte-checked, install window,
// one lock cmpxchg8b, rollback) and the chain head set to x3m_fl_scale_entry below, which reproduces the helper
// exactly whenever it does not trap and clamps the quotient to [INT32_MIN, INT32_MAX] otherwise.
//
// ABI (generator tools/analysis/fl_scale_overflow_constants.py, from the body): ECX = const int a[3],
// EDX = const int *b, [esp+4] = int out[3], [esp+8] = const int *c; plain RET (caller cleanup); EAX = out;
// EBX/ESI/EDI/EBP preserved. The helper leaves ECX = c and EDX = the remainder of component 2 (ECX = a[2],
// EDX = 0 when c == 0); the replacement returns the same so no caller can tell them apart. Memory is read and
// written in the helper's order (component 2's b is read before out[1] is stored), so aliased out/a/b behave
// alike. Built -fno-exceptions -mno-sse -mno-mmx -mfpmath=387: the per-call path is integer only. The QPC check
// (every 256 calls) holds an XmmSave and keeps LastError; the row runs log() under call_preserved as well.
#include "scale_overflow_fix_fl.h"
#include "../../proxy/config.h"
#include "../../proxy/cpu_state.h"
#include "../../proxy/engine_patch.h"
#include "executable_identity_fl.h"
#include <atomic>
#include <climits>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 entry patch only");
namespace {
namespace layout {
#include "scale_overflow_fix_fl_inc.h"
}
static_assert(sizeof layout::helper_entry == 16 && layout::displaced_length >= 5 &&
                  layout::displaced_length <= sizeof layout::helper_entry &&
                  layout::displaced_length <= x3m::engine_patch::max_prologue,
              "whole displaced instructions inside the gated entry");
x3m::engine_patch::Site site_;
uintptr_t install_base = 0, install_site = 0;
bool entry_ok = false, body_ok = false;
std::atomic<bool> live{false};
std::atomic<const char*> state{"disabled"};
// Plain counters: relaxed load + store (no lock prefix on the per-call path); concurrent calls may lose an
// increment, which only the diagnostic row sees.
std::atomic<unsigned long> call_count{0}, saturated_count{0};
std::atomic<uintptr_t> last_return{0};
std::atomic<bool> writing{false}, force{false};
long long period = 0, due = 0; // QPC ticks; period 0: rows off. Written under `writing` (or before install).
unsigned long logged_saturated = 0;
bool first_logged = false; // the first due period after install writes a row regardless of `saturated`
constexpr unsigned long qpc_stride = 256;

// XMM0-7 saved for the lifetime of the object (as object_trace_fl.cpp): QueryPerformanceCounter and log() may
// change volatile XMM state, and the engine's callers keep vectors live across the helper call.
struct XmmSave {
    alignas(16) unsigned char xmm[128];
    XmmSave() {
        asm volatile("movups %%xmm0,(%0)\n\tmovups %%xmm1,16(%0)\n\tmovups %%xmm2,32(%0)\n\tmovups %%xmm3,48(%0)\n\t"
                     "movups %%xmm4,64(%0)\n\tmovups %%xmm5,80(%0)\n\tmovups %%xmm6,96(%0)\n\tmovups %%xmm7,112(%0)" ::"r"(
                         xmm)
                     : "memory");
    }
    ~XmmSave() {
        asm volatile("movups (%0),%%xmm0\n\tmovups 16(%0),%%xmm1\n\tmovups 32(%0),%%xmm2\n\tmovups 48(%0),%%xmm3\n\t"
                     "movups 64(%0),%%xmm4\n\tmovups 80(%0),%%xmm5\n\tmovups 96(%0),%%xmm6\n\tmovups 112(%0),%%xmm7" ::"r"(
                         xmm)
                     : "memory");
    }
    XmmSave(const XmmSave&) = delete;
    XmmSave& operator=(const XmmSave&) = delete;
};
void log_status() {
    x3m::log("fl.scale_fix status=%s base=0x%08lx site=0x%08lx entry_ok=%u body_ok=%u write=%s", state.load(),
             static_cast<unsigned long>(install_base), static_cast<unsigned long>(install_site), unsigned(entry_ok),
             unsigned(body_ok), site_.patched_in ? (site_.atomic_write ? "atomic" : "plain") : "none");
}
// X3M_FL_SCALE_OVERFLOW_LOG (fl_scale_overflow_log): seconds between fl.scale_fix_sample checks, 0 off, unset 10, at
// most 3600. Digits only, else the default. Read once at install.
unsigned sample_seconds() {
    wchar_t text[8]{};
    const DWORD length = x3m::config::get(L"X3M_FL_SCALE_OVERFLOW_LOG", text, 8);
    if (length == 0 || length >= 8) return 10;
    unsigned value = 0;
    for (DWORD i = 0; i < length; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return 10;
        value = value * 10 + unsigned(text[i] - L'0');
    }
    return value > 3600 ? 3600 : value;
}
void arm_diagnostic() {
    period = due = 0;
    logged_saturated = 0;
    first_logged = false;
    call_count.store(0);
    saturated_count.store(0);
    last_return.store(0);
    force.store(false);
    const unsigned seconds = sample_seconds();
    LARGE_INTEGER frequency{}, now{};
    if (seconds && QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 && QueryPerformanceCounter(&now)) {
        period = frequency.QuadPart * static_cast<long long>(seconds);
        due = now.QuadPart + period;
    }
}
// Every qpc_stride calls (or when forced): one QPC; when the period is due, one row at the first due period after
// install (the call rate, even with saturated=0), then one only if `saturated` changed since the previous row. One
// thread at a time; a thread finding the flag taken skips this check.
void check_sample() {
    if (!period || writing.exchange(true, std::memory_order_acquire)) return;
    const DWORD error = GetLastError();
    {
        XmmSave xmm;
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const bool forced = force.exchange(false, std::memory_order_relaxed);
        if (forced || now.QuadPart >= due) {
            due = now.QuadPart + period;
            const unsigned long saturated = saturated_count.load(std::memory_order_relaxed);
            if (!first_logged || saturated != logged_saturated) {
                first_logged = true;
                logged_saturated = saturated;
                const unsigned long calls = call_count.load(std::memory_order_relaxed);
                const uintptr_t back = last_return.load(std::memory_order_relaxed);
                // The call instruction of the last clamped call, as an RVA (call rel32: return address - 5).
                const unsigned long site = back ? static_cast<unsigned long>(back - 5 - install_base) : 0;
                x3m::call_preserved([&]() {
                    x3m::log("fl.scale_fix_sample calls=%lu saturated=%lu last_site=0x%05lx", calls, saturated, site);
                });
            }
        }
    }
    SetLastError(error);
    writing.store(false, std::memory_order_release);
}
// a * b / c truncated toward zero, as `imul; idiv` computes it when the quotient fits; otherwise clamped (counted in
// *clamped). EDX of the idiv (the remainder) goes to *remainder (0 when clamped). c != 0.
int32_t scaled(int32_t a, int32_t b, int32_t c, uint32_t* remainder, unsigned* clamped) {
    const int64_t product = int64_t(a) * b;
    const bool negative = (product < 0) != (c < 0);
    const uint64_t magnitude = product < 0 ? 0 - uint64_t(product) : uint64_t(product);
    const uint32_t divisor = c < 0 ? 0u - uint32_t(c) : uint32_t(c);
    // |q| = floor(magnitude / divisor) fits iff magnitude < (limit + 1) * divisor, limit 2^31 (negative) or
    // 2^31 - 1; both bounds are below 2^63 (|product| <= 2^62).
    const uint64_t bound = negative ? uint64_t(divisor) * 0x80000001ull : uint64_t(divisor) << 31;
    if (magnitude >= bound) {
        *remainder = 0;
        ++*clamped;
        return negative ? INT32_MIN : INT32_MAX;
    }
    uint32_t low = uint32_t(uint64_t(product)), high = uint32_t(uint64_t(product) >> 32);
    asm("idivl %2" : "+a"(low), "+d"(high) : "rm"(c) : "cc");
    *remainder = high;
    return int32_t(low);
}
}
extern "C" {
// The body: cdecl, reached only through x3m_fl_scale_entry. regs[0..1] receive the helper's final ECX/EDX;
// return_address is the engine caller's (diagnostics only).
int32_t* __cdecl x3m_fl_scale_body(const int32_t* a, const int32_t* b, int32_t* out, const int32_t* c, uint32_t* regs,
                                   uintptr_t return_address) {
    const volatile int32_t* va = a;
    const volatile int32_t* vb = b;
    const volatile int32_t* vc = c;
    volatile int32_t* vout = out;
    uint32_t remainder = 0;
    unsigned clamped = 0;
    const int32_t c0 = *vc, b0 = *vb, a0 = va[0];
    vout[0] = c0 ? scaled(a0, b0, c0, &remainder, &clamped) : 0;
    const int32_t c1 = *vc, b1 = *vb, a1 = va[1];
    const int32_t q1 = c1 ? scaled(a1, b1, c1, &remainder, &clamped) : 0;
    const int32_t b2 = *vb; // read before out[1] is stored, as the helper does
    vout[1] = q1;
    const int32_t c2 = *vc, a2 = va[2];
    if (c2) {
        vout[2] = scaled(a2, b2, c2, &remainder, &clamped);
        regs[0] = uint32_t(c2);
        regs[1] = remainder;
    } else {
        vout[2] = 0;
        regs[0] = uint32_t(a2);
        regs[1] = 0;
    }
    const unsigned long n = call_count.load(std::memory_order_relaxed) + 1;
    call_count.store(n, std::memory_order_relaxed);
    if (clamped) {
        saturated_count.store(saturated_count.load(std::memory_order_relaxed) + clamped, std::memory_order_relaxed);
        last_return.store(return_address, std::memory_order_relaxed);
    }
    if (!(n % qpc_stride) || force.load(std::memory_order_relaxed)) check_sample();
    return out;
}
// The chain head at the helper's entry (engine_patch: entry JMP -> dispatcher `jmp [entry]` -> here). Entry:
// ECX = a, EDX = &b, [esp] = return into the caller, [esp+4] = out, [esp+8] = &c. The body is cdecl (EBX/ESI/EDI/
// EBP preserved); the final ECX/EDX come back through an 8-byte frame slot; EAX is the body's return (out).
// Plain RET: the caller removes its two words.
__attribute__((naked)) void x3m_fl_scale_entry() {
    __asm__ __volatile__(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tsubl $8,%esp\n\t"
        "leal -8(%ebp),%eax\n\t"
        "pushl 4(%ebp)\n\tpushl %eax\n\tpushl 12(%ebp)\n\tpushl 8(%ebp)\n\tpushl %edx\n\tpushl %ecx\n\t"
        "call _x3m_fl_scale_body\n\taddl $24,%esp\n\t"
        "movl -8(%ebp),%ecx\n\tmovl -4(%ebp),%edx\n\tleave\n\tret\n\t");
}
}
namespace {
uint32_t fnv1a(const unsigned char* p, unsigned n) {
    uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}
// The helper's 16 entry bytes and the FNV-1a of its whole body at base + helper_rva. nullptr when both match.
const char* gate(uintptr_t base) {
    const uintptr_t helper = base + layout::helper_rva;
    entry_ok = x3m::engine_patch::verify_bytes(helper, layout::helper_entry, sizeof layout::helper_entry);
    unsigned char body[layout::helper_length];
    body_ok =
        x3m::engine_patch::read_code(helper, body, sizeof body) && fnv1a(body, sizeof body) == layout::helper_fnv1a;
    if (!entry_ok) return "entry_mismatch";
    return body_ok ? nullptr : "body_mismatch";
}
// Pins this DLL for the process lifetime (documented GET_MODULE_HANDLE_EX_FLAG_PIN): the engine jumps into it.
bool pin_self() {
    HMODULE self = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&site_), &self) != FALSE &&
           self != nullptr;
}
// The gate passed: window, pin, claim (entry JMP to the dispatcher, whose head is still the original tail), then
// the head is pointed at the replacement. A failed head store restores the entry; if that fails too, the JMP
// still reaches the original through the tail.
bool install(uintptr_t base) {
    if (!x3m::engine_patch::install_window_open()) {
        state = "late_claim";
        return false;
    }
    if (!pin_self()) {
        state = "pin_failed";
        return false;
    }
    x3m::engine_patch::SiteSpec spec{};
    spec.name = "fl_scale_fix";
    spec.address = base + layout::helper_rva;
    std::memcpy(spec.expected, layout::helper_entry, layout::displaced_length);
    spec.length = layout::displaced_length;
    if (!x3m::engine_patch::claim(site_, spec)) {
        state = site_.status; // late_claim, bytes_mismatch, arena_full, emit_failed, protect_failed,
                              // patch_rolled_back, rollback_failed (static strings)
        return false;
    }
    arm_diagnostic();
    if (!x3m::engine_patch::push_front(site_, reinterpret_cast<void*>(&x3m_fl_scale_entry))) {
        state = x3m::engine_patch::restore(site_) ? "chain_failed" : "chain_failed_original_tail";
        return false;
    }
    live.store(true);
    state = "active";
    return true;
}
}
namespace x3m::fl::scale_fix {
// X3M_FL_SCALE_OVERFLOW_FIX (fl_scale_overflow_fix: DLL default on, only 0 turns it off), X3FL.exe identity, the
// entry bytes and the body hash. Inside the engine_patch install window only (load_backend); one fl.scale_fix row
// either way.
bool initialize() {
    const DWORD error = GetLastError();
    if (site_.claimed) {
        const bool enabled = live.load();
        SetLastError(error);
        return enabled;
    }
    wchar_t setting[4]{};
    bool result = false;
    if (x3m::config::get(L"X3M_FL_SCALE_OVERFLOW_FIX", setting, 4) == 1 && setting[0] == L'0')
        state = "disabled";
    else if (!x3m::fl::verified())
        state = "executable_mismatch";
    else {
        install_base = x3m::fl::module_base();
        install_site = install_base + layout::helper_rva;
        if (const char* refusal = gate(install_base))
            state = refusal;
        else
            result = install(install_base);
    }
    log_status();
    SetLastError(error);
    return result;
}
bool active() {
    return live.load();
}
const char* status() {
    return state.load();
}
unsigned long calls() {
    return call_count.load(std::memory_order_relaxed);
}
unsigned long saturated() {
    return saturated_count.load(std::memory_order_relaxed);
}
#ifdef X3M_FL_SCALE_FIX_FIXTURE
const char* fixture_gate(uintptr_t base) {
    return gate(base);
}
bool fixture_install(uintptr_t base) {
    if (site_.claimed) {
        state = "already_installed";
        return false;
    }
    install_base = base;
    install_site = base + layout::helper_rva;
    const char* refusal = gate(base);
    bool result = false;
    if (refusal)
        state = refusal;
    else
        result = install(base);
    log_status();
    return result;
}
bool fixture_restore() {
    live.store(false);
    if (!x3m::engine_patch::restore(site_)) return false;
    site_ = {};
    state = "restored";
    return true;
}
void* fixture_original() {
    return site_.tail;
}
void fixture_force_sample() {
    force.store(true);
}
#endif
}
