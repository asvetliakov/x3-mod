#include "collide_query_phases.h"
#include "collide_query_phases_core.h"
#include "collide_memo.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "cpu_state.h"
#include "capture.h"
#include "log_tiers.h"
#include <windows.h>
#include <cstring>
#include <atomic>

static_assert(sizeof(void*) == 4, "x86 engine ABI");
namespace x3m::collide_query_phases {
bool enabled = false;
static bool verify_mode = false;
namespace {
constexpr std::uintptr_t site_va = 0x004e2956, target_va = 0x004e2530;
constexpr unsigned char pre[] = {0x33, 0xc0, 0xd9, 0x1c, 0x24, 0x51, 0x52, 0x53, 0x57, 0xa3, 0x44, 0x85,
                                 0x60, 0x00, 0xa3, 0x48, 0x85, 0x60, 0x00, 0xa3, 0x4c, 0x85, 0x60, 0x00};
constexpr unsigned char post[] = {0x83, 0xc4, 0x14, 0x5f, 0x5e, 0x5d, 0x5b, 0x81, 0xc4, 0x98, 0x00, 0x00, 0x00, 0xc3};
engine_patch::CallSite patch{};
core::Accumulator accumulator;
core::Window window;
bool descent_busy = false; // owner-only: guards the exact-stack root return slot
std::atomic<DWORD> owner{0};
volatile LONG reset_generation = 0, foreign_generation = 0, early_cpu_modes = 0;
LONG pending_generation = 0, seen_generation = 0, pending_foreign = 0;
std::uint64_t frequency = 0, clock_self_ns = 0, first_frame = 0;
struct ErrorGuard {
    DWORD error = GetLastError();
    ~ErrorGuard() { SetLastError(error); }
};
#ifdef X3M_COLLIDE_QUERY_FIXTURE
std::uint64_t (*clock_override)() = nullptr;
bool fail_restore = false;
#endif
std::uint64_t clock_tick() {
#ifdef X3M_COLLIDE_QUERY_FIXTURE
    if (clock_override) return clock_override();
#endif
    LARGE_INTEGER t{};
    return QueryPerformanceCounter(&t) && t.QuadPart > 0 ? std::uint64_t(t.QuadPart) : 0;
}
bool ours() {
    return owner.load(std::memory_order_relaxed) == GetCurrentThreadId();
}
std::uint64_t us(std::uint64_t ticks) {
    return ticks / frequency * 1000000 + (ticks % frequency) * 1000000 / frequency;
}
bool matches(std::uintptr_t address, const unsigned char* expected, unsigned size) {
    unsigned char actual[32]{};
    return engine_patch::read_code(address, actual, size) && !std::memcmp(actual, expected, size);
}
}
void detail::begin(core::Kind kind) {
    if (!owner.load(std::memory_order_relaxed)) owner.store(GetCurrentThreadId(), std::memory_order_relaxed);
    if (!ours()) return;
    const LONG generation = InterlockedCompareExchange(&reset_generation, 0, 0);
    if (generation != seen_generation) {
        accumulator.reset();
        seen_generation = generation;
    }
    const auto early = static_cast<std::uint32_t>(InterlockedExchange(&early_cpu_modes, 0));
    accumulator.frame.invalid += early;
    accumulator.frame.cpu_mode += early;
    pending_generation = generation;
    pending_foreign = InterlockedCompareExchange(&foreign_generation, 0, 0);
    accumulator.begin(clock_tick(), kind);
}
void detail::end() {
    if (!ours()) return;
    const auto now = clock_tick();
    const LONG generation = InterlockedCompareExchange(&reset_generation, 0, 0);
    if (pending_generation != generation) {
        accumulator.invalidate(core::Reset);
        seen_generation = generation;
    }
    if (pending_foreign != InterlockedCompareExchange(&foreign_generation, 0, 0)) accumulator.invalidate(core::Foreign);
    // Clock has stopped. Ineligible early returns leave stale engine counters.
    const bool eligible = accumulator.kind != core::Ineligible;
    const auto read = [](std::uintptr_t va) { return *reinterpret_cast<const std::uint32_t*>(va); };
    accumulator.end(now, eligible ? read(collide_memo::core::visits_va) : 0,
                    eligible ? read(collide_memo::core::triangles_va) : 0,
                    eligible ? read(collide_memo::core::contacts_va) : 0);
}
void detail::invalidate() {
    if (ours()) accumulator.invalidate();
}
void detail::foreign() {
    InterlockedIncrement(&foreign_generation);
}
void device_reset() {
    if (enabled) InterlockedIncrement(&reset_generation);
}
bool initialize(bool memo_ready, bool verifying) {
    ErrorGuard error;
    if (enabled) return true;
    if (patch.patched_in) return false; // retain a failed rollback for shutdown; never overwrite its claim
    if (!log_tier::debug_flag(L"X3M_COLLIDE_QUERY_PHASES")) return false; // X3M_COLLIDE_QUERY_PHASES=1 or X3M_DEBUG=1
    const char* reason = "memo_off";
    if (memo_ready) {
        LARGE_INTEGER f{};
        if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0)
            reason = "clock_unavailable";
        else if (!object_trace::executable_verified())
            reason = "executable_mismatch";
        else if (!matches(site_va - sizeof pre, pre, sizeof pre) || !matches(site_va + 5, post, sizeof post))
            reason = "bytes_mismatch";
        else {
            frequency = std::uint64_t(f.QuadPart);
            // Warm clocks, then median empty pair; reported raw, never subtracted from samples.
            std::uint64_t cost[129]{};
            for (auto& t : cost) {
                const auto a = clock_tick(), b = clock_tick();
                t = a && b >= a ? b - a : 0;
            }
            std::sort(cost, cost + 129);
            clock_self_ns = cost[64] * 1000000000 / frequency;
            if (engine_patch::claim_call(patch, site_va, target_va,
                                         reinterpret_cast<void*>(&x3m_collide_query_descent_thunk))) {
                accumulator = {};
                window = {};
                descent_busy = false;
                owner.store(0, std::memory_order_relaxed);
                seen_generation = InterlockedCompareExchange(&reset_generation, 0, 0);
                verify_mode = verifying;
                enabled = true;
                reason = "ok";
            } else {
                reason = patch.status;
                if (patch.patched_in && !engine_patch::restore_call(patch)) reason = "rollback_failed";
            }
        }
    }
    log("collide_query_phase_mode requested=1 enabled=%u reason=%s site=0x004e2956 target=0x004e2530 frames=300 clock_self_ns=%llu qpc_frequency=%llu engine_query=dispatch_root_setup_descent non_descent=same_sample_difference",
        unsigned(enabled), reason, clock_self_ns, frequency);
    return enabled;
}
bool shutdown() {
    ErrorGuard error;
    enabled = false;
#ifdef X3M_COLLIDE_QUERY_FIXTURE
    if (fail_restore && patch.patched_in) return false;
#endif
    return !patch.patched_in || engine_patch::restore_call(patch);
}
void present(unsigned long long device, unsigned long long frame) {
    if (!enabled || !ours()) return;
    x3m::PreserveCpuState cpu;
    const LONG generation = InterlockedCompareExchange(&reset_generation, 0, 0);
    if (generation != seen_generation) {
        accumulator.reset();
        seen_generation = generation;
    }
    if (!window.size) first_frame = frame;
    window.add(accumulator.take());
    descent_busy = false; // owner Present proves an open descent was abandoned
    if (window.size != core::window_frames) return;
    const auto totals = window.sum();
    const auto quantile = [&](std::uint64_t core::Sample::* field, unsigned p) {
        return us(window.quantile(field, p));
    };
    log("collide_query_phases qpc=%llu device=%llu frame=%llu first_frame=%llu frames=300 valid_frames=%u invalid_frames=%u verify=%u engine_query_p50_us=%llu engine_query_p95_us=%llu descent_p50_us=%llu descent_p95_us=%llu non_descent_p50_us=%llu non_descent_p95_us=%llu engine_query_sum_us=%llu descent_sum_us=%llu non_descent_sum_us=%llu queries=%llu descents=%llu misses=%llu verifies=%llu ineligible=%llu visits=%llu triangles=%llu contacts=%llu invalid=%llu discard_clock=%llu discard_reentry=%llu discard_foreign=%llu discard_unwind=%llu discard_reset=%llu discard_cpu_mode=%llu clock_self_ns=%llu",
        clock_tick(), device, frame, first_frame, window.valid_frames(), window.size - window.valid_frames(),
        unsigned(verify_mode), quantile(&core::Sample::query, 50), quantile(&core::Sample::query, 95),
        quantile(&core::Sample::descent, 50), quantile(&core::Sample::descent, 95),
        quantile(&core::Sample::non_descent, 50), quantile(&core::Sample::non_descent, 95), us(totals.query),
        us(totals.descent), us(totals.non_descent), totals.queries, totals.descents, totals.misses, totals.verifies,
        totals.ineligible, totals.visits, totals.triangles, totals.contacts, totals.invalid, totals.clock,
        totals.reentry, totals.foreign, totals.unwind, totals.reset, totals.cpu_mode, clock_self_ns);
    window = {};
}
#ifdef X3M_COLLIDE_QUERY_FIXTURE
void fixture_clock(std::uint64_t (*clock)()) {
    clock_override = clock;
}
core::Sample fixture_sample() {
    return accumulator.frame;
}
unsigned fixture_window_size() {
    return window.size;
}
void fixture_fail_restore(bool fail) {
    fail_restore = fail;
}
void fixture_clear() {
    accumulator = {};
    window = {};
    seen_generation = InterlockedCompareExchange(&reset_generation, 0, 0);
}
#endif
}
// The assembly envelope holds all CPU state. These handlers hold LastError,
// including across QPC failure. No allocation/log/FP arithmetic on this path.
extern "C" int __cdecl x3m_collide_query_lookup(std::uint32_t flags, std::uint32_t cap,
                                                const x3m_collide_memo_args* args) {
    const DWORD error = GetLastError();
    const int result = x3m_collide_memo_lookup(flags, cap, args);
    SetLastError(error);
    return result;
}
extern "C" void __cdecl x3m_collide_query_store() {
    const DWORD error = GetLastError();
    x3m_collide_memo_store();
    SetLastError(error);
}
extern "C" int __cdecl x3m_collide_query_enter() {
    using namespace x3m::collide_query_phases;
    if (!enabled || !ours() || !accumulator.pending) return 0;
    if (descent_busy) {
        accumulator.invalidate(core::Reentry);
        return 0;
    }
    descent_busy = true; // before QPC: even callback reentry must not claim the return slot
    const DWORD error = GetLastError();
    accumulator.enter(clock_tick());
    SetLastError(error);
    return 1;
}
extern "C" void __cdecl x3m_collide_query_leave() {
    using namespace x3m::collide_query_phases;
    const DWORD error = GetLastError();
    if (enabled && ours() && accumulator.pending) {
        accumulator.leave(clock_tick());
        descent_busy = false;
    }
    SetLastError(error);
}
extern "C" void __cdecl x3m_collide_query_mode_discard() {
    using namespace x3m::collide_query_phases;
    if (enabled) {
        // A mode bypass precedes memo's thread gate. Never adopt its thread or
        // touch owner-only state until normal begin has established that owner.
        const DWORD thread = owner.load(std::memory_order_relaxed);
        if (!thread)
            InterlockedIncrement(&early_cpu_modes);
        else if (thread == GetCurrentThreadId())
            accumulator.reject_mode();
        else
            detail::foreign();
    }
}
extern "C" void __cdecl x3m_collide_query_mode_abandon() {
    x3m_collide_query_mode_discard();
    x3m_collide_memo_abandon();
}
extern "C" void __cdecl x3m_collide_query_descent_abandon() {
    using namespace x3m::collide_query_phases;
    if (ours()) descent_busy = false;
}
extern "C" {
std::uint32_t x3m_collide_query_descent_target = 0x004e2530, x3m_collide_query_descent_return = 0;
}
// The sole external descent call has exactly five cdecl words. Its return slot
// is replaced only after the memo owner and descent_busy gate admit it. Calling
// the engine on its EXACT original stack also preserves volatile ECX/EDX values
// that can point at the descent's locals on a deep exit. Repushing arguments
// changed those outputs by 28 bytes in the rejected fixture (no verdict change).
// Foreign/untracked/reentered roots tail-forward without touching the slot.
// Four-byte incoming alignment; private CPU image explicitly 16-byte aligned.
// FNSAVE/FRSTOR is the project's qualified CrossOver/native x87 transport;
// mixed rounding modes bypass before any FP-control write.
asm(R"(
.intel_syntax noprefix
.text
.macro query_save refused
    pushfd
    pushad
    cld
    mov ebp,esp
    and esp,-16
    sub esp,240
    movdqu [esp],xmm0
    movdqu [esp+16],xmm1
    movdqu [esp+32],xmm2
    movdqu [esp+48],xmm3
    movdqu [esp+64],xmm4
    movdqu [esp+80],xmm5
    movdqu [esp+96],xmm6
    movdqu [esp+112],xmm7
    stmxcsr [esp+236]
    fnstcw [esp+232]
    mov eax,[esp+236]
    shr eax,13
    and eax,3
    movzx ecx,word ptr [esp+232]
    shr ecx,10
    and ecx,3
    cmp eax,ecx
    jne \refused
    fnsave [esp+128]
.endm
.macro query_restore
    frstor [esp+128]
    ldmxcsr [esp+236]
    query_restore_light
.endm
.macro query_restore_light
    movdqu xmm0,[esp]
    movdqu xmm1,[esp+16]
    movdqu xmm2,[esp+32]
    movdqu xmm3,[esp+48]
    movdqu xmm4,[esp+64]
    movdqu xmm5,[esp+80]
    movdqu xmm6,[esp+96]
    movdqu xmm7,[esp+112]
    mov esp,ebp
    popad
    popfd
.endm
.p2align 4
.globl _x3m_collide_query_descent_thunk
_x3m_collide_query_descent_thunk:
    query_save 5f
    call _x3m_collide_query_enter
    test eax,eax
    jz 7f
    query_restore
    pop dword ptr [_x3m_collide_query_descent_return]
    call dword ptr [_x3m_collide_query_descent_target]
    query_save 6f
    call _x3m_collide_query_leave
    query_restore
    jmp dword ptr [_x3m_collide_query_descent_return]
7:  query_restore
    jmp dword ptr [_x3m_collide_query_descent_target]
5:  call _x3m_collide_query_mode_discard
    query_restore_light
    jmp dword ptr [_x3m_collide_query_descent_target]
6:  call _x3m_collide_query_mode_discard
    call _x3m_collide_query_descent_abandon
    query_restore_light
    jmp dword ptr [_x3m_collide_query_descent_return]
.p2align 4
.globl _x3m_collide_query_memo_thunk
_x3m_collide_query_memo_thunk:
    query_save 3f
    lea edx,[ebp+40]
    push edx
    push dword ptr [ebp+28]
    push dword ptr [ebp+24]
    call _x3m_collide_query_lookup
    add esp,12
    cmp eax,1
    je 1f
    cmp eax,2
    je 2f
    query_restore
    pop dword ptr [_x3m_collide_memo_return]
    call dword ptr [_x3m_collide_memo_target]
    query_save 4f
    call _x3m_collide_query_store
    query_restore
    jmp dword ptr [_x3m_collide_memo_return]
1:  query_restore
    xor eax,eax
    ret
2:  query_restore
    jmp dword ptr [_x3m_collide_memo_target]
3:  call _x3m_collide_query_mode_discard
    query_restore_light
    jmp dword ptr [_x3m_collide_memo_target]
4:  call _x3m_collide_query_mode_abandon
    query_restore_light
    jmp dword ptr [_x3m_collide_memo_return]
.purgem query_restore_light
.purgem query_save
.purgem query_restore
.att_syntax
)");
