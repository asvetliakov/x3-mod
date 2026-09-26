// Reuse only the real-engine fixture's arena, model builders and parity helper.
// This executable has its own bounded checks and does not run the legacy suite.
#define main collide_memo_legacy_main
#include "collide_memo_fixture.cpp"
#undef main
#include "../../src/proxy/collide_query_phases.h"
namespace phases = x3m::collide_query_phases;
extern "C" {
extern std::uint32_t x3m_collide_query_descent_target;
void __cdecl query_abi_call(void* target, unsigned padding);
void query_abi_target();
std::uint32_t query_abi_regs[7], query_abi_args[5];
unsigned char query_abi_xmm[128], query_abi_fpu[108];
std::uint32_t query_abi_mxcsr, query_abi_round;
std::uint16_t query_abi_control = 0x037f;
float query_abi_three = 3.0f;
}
asm(R"(
.intel_syntax noprefix
.text
.globl _query_abi_call
_query_abi_call:
    push ebp
    mov ebp,esp
    push ebx
    push esi
    push edi
    sub esp,[ebp+12]
    push 0x55555555
    push 0x44444444
    push 0x33333333
    push 0x22222222
    push 0x11111111
    fninit
    fldcw [_query_abi_control]
    fld1
    fldz
    pcmpeqd xmm0,xmm0
    movdqa xmm1,xmm0
    movdqa xmm2,xmm0
    movdqa xmm3,xmm0
    movdqa xmm4,xmm0
    movdqa xmm5,xmm0
    movdqa xmm6,xmm0
    movdqa xmm7,xmm0
    mov eax,0x01010101
    mov ecx,0x02020202
    mov edx,0x03030303
    mov ebx,0x04040404
    mov esi,0x05050505
    mov edi,0x06060606
    call [ebp+8]
    mov [_query_abi_regs],eax
    mov [_query_abi_regs+4],ecx
    mov [_query_abi_regs+8],edx
    mov [_query_abi_regs+12],ebx
    mov [_query_abi_regs+16],esi
    mov [_query_abi_regs+20],edi
    mov [_query_abi_regs+24],ebp
    movdqu [_query_abi_xmm],xmm0
    movdqu [_query_abi_xmm+16],xmm1
    movdqu [_query_abi_xmm+32],xmm2
    movdqu [_query_abi_xmm+48],xmm3
    movdqu [_query_abi_xmm+64],xmm4
    movdqu [_query_abi_xmm+80],xmm5
    movdqu [_query_abi_xmm+96],xmm6
    movdqu [_query_abi_xmm+112],xmm7
    fnsave [_query_abi_fpu]
    stmxcsr [_query_abi_mxcsr]
    lea esp,[ebp-12]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
.globl _query_abi_target
_query_abi_target:
    // Observe every argument, then change volatile CPU outputs deliberately.
    push eax
    mov eax,[esp+8]
    mov [_query_abi_args],eax
    mov eax,[esp+12]
    mov [_query_abi_args+4],eax
    mov eax,[esp+16]
    mov [_query_abi_args+8],eax
    mov eax,[esp+20]
    mov [_query_abi_args+12],eax
    mov eax,[esp+24]
    mov [_query_abi_args+16],eax
    pop eax
    add eax,123
    add ecx,456
    add edx,789
    fchs
    fld1
    fdiv dword ptr [_query_abi_three]
    fstp dword ptr [_query_abi_round]
    psllq xmm0,1
    psllq xmm1,2
    psllq xmm2,3
    psllq xmm3,4
    psllq xmm4,5
    psllq xmm5,6
    psllq xmm6,7
    psllq xmm7,8
    ret
.att_syntax
)");
static std::uint64_t tick = 100, clock_calls = 0;
static Job* nested_job = nullptr;
static std::uint64_t nested_at = 0;
static bool fail_clock = false, reset_at_clock = false, reenter_at_clock = false, foreign_at_clock = false;
static std::uint64_t test_clock() {
    ++clock_calls;
    SetLastError(0xbad0);
    if (nested_job && clock_calls == nested_at) {
        Job& j = *nested_job;
        nested_job = nullptr;
        fx_call(core::caller_va, j.a->node, j.b->node, j.a->body, j.b->body, 2, 1, 0, 0, &j.state);
    }
    if (reset_at_clock) {
        reset_at_clock = false;
        phases::device_reset();
    }
    if (reenter_at_clock) {
        reenter_at_clock = false;
        phases::invalidate();
    }
    if (foreign_at_clock) {
        foreign_at_clock = false;
        phases::foreign();
    }
    return fail_clock ? 0 : ++tick;
}
static DWORD WINAPI mixed_first_thread(void*) {
    query_abi_control = 0x037f;
    _mm_setcsr(0x5f80);
    query_abi_call(reinterpret_cast<void*>(&x3m_collide_query_descent_thunk), 0);
    return 0;
}
static void abi() {
    x3m_collide_query_descent_target = addr(&query_abi_target);
    for (unsigned mode = 0; mode < 4; ++mode)
        for (unsigned padding : {0u, 4u, 8u, 12u}) {
            const std::uint16_t controls[] = {0x037f, 0x0b7f, 0x0f7f, 0x037f};
            query_abi_control = controls[mode];
            std::uint32_t registers[7], args[5], mxcsr;
            unsigned char xmm[128], fpu[108];
            _mm_setcsr(mode ? 0x5f80 : 0x1f80);
            SetLastError(0x12345678);
            query_abi_call(reinterpret_cast<void*>(&query_abi_target), padding);
            std::memcpy(registers, query_abi_regs, sizeof registers);
            std::memcpy(args, query_abi_args, sizeof args);
            std::memcpy(xmm, query_abi_xmm, sizeof xmm);
            std::memcpy(fpu, query_abi_fpu, sizeof fpu);
            mxcsr = query_abi_mxcsr;
            const auto rounded = query_abi_round;
            phases::begin();
            SetLastError(0x12345678);
            const auto clocks = clock_calls;
            query_abi_call(reinterpret_cast<void*>(&x3m_collide_query_descent_thunk), padding);
            const auto bracket_clocks = clock_calls - clocks;
            const DWORD error = GetLastError();
            phases::end();
            check(!std::memcmp(registers, query_abi_regs, sizeof registers),
                  "root wrapper GP registers at every 4-byte alignment");
            check(!std::memcmp(args, query_abi_args, sizeof args) && args[0] == 0x11111111 && args[4] == 0x55555555,
                  "root wrapper five args");
            check(!std::memcmp(xmm, query_abi_xmm, sizeof xmm), "root wrapper XMM0-7 outputs");
            check(!std::memcmp(fpu, query_abi_fpu, 10) && !std::memcmp(fpu + 28, query_abi_fpu + 28, 80),
                  "root wrapper live x87 CW/SW/tags/data");
            check(mxcsr == query_abi_mxcsr && error == 0x12345678, "root wrapper MXCSR/LastError");
            check(rounded == query_abi_round, "actual x87 division retains effective rounding");
            check(bracket_clocks == (mode < 2 ? 2u : 0u), "mixed rounding bypass has no QPC");
        }
    query_abi_control = 0x037f;
    _mm_setcsr(0x1f80);
    x3m_collide_query_descent_target = 0x004e2530;
}
static void unpatched_reference() {
    constexpr std::uint32_t dispatch = 0x4a0200, query_copy = 0x4a0400;
    std::memcpy(reinterpret_cast<void*>(dispatch), reinterpret_cast<void*>(0x4e29f0), 0x60);
    std::memcpy(reinterpret_cast<void*>(query_copy), reinterpret_cast<void*>(0x4e2780), 0x1e9);
    const auto fix = [](std::uint32_t call, std::uint32_t target) {
        engine<std::int32_t>(call + 1) = std::int32_t(target - call - 5);
    };
    fix(reference_va + core::memo_site_va - core::caller_va, dispatch);
    fix(dispatch + 0x12, 0x52b5d0);
    fix(dispatch + 0x56, query_copy);
    const std::uint32_t calls[][2] = {{0x4e27fc, 0x4e1ff0}, {0x4e281e, 0x4e20d0}, {0x4e2833, 0x4e1ff0},
                                      {0x4e2850, 0x4e20d0}, {0x4e2865, 0x4dfd80}, {0x4e28ae, 0x4e2130},
                                      {0x4e28c8, 0x4dfd80}, {0x4e290f, 0x4e2130}, {0x4e2956, 0x4e2530}};
    for (const auto& pair : calls) fix(query_copy + pair[0] - 0x4e2780, pair[1]);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
}
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (!map_engine()) return 1;
    unpatched_reference();
    unsigned char root[5];
    std::memcpy(root, reinterpret_cast<void*>(0x4e2956), 5);
    const auto pristine = [&] { return !std::memcmp(root, reinterpret_cast<void*>(0x4e2956), 5); };
    phases::fixture_clock(test_clock);
    SetEnvironmentVariableW(L"X3M_COLLIDE_QUERY_PHASES", nullptr);
    check(!phases::initialize(true) && pristine() && !clock_calls, "disabled no patch/no QPC");
    SetEnvironmentVariableW(L"X3M_COLLIDE_QUERY_PHASES", L"1");
    check(!phases::initialize(false) && pristine() && !clock_calls, "memo dependency fails closed");
    engine<unsigned char>(0x4e295b) ^= 1;
    check(!phases::initialize(true) && !clock_calls, "mismatched post bytes fail before clock");
    engine<unsigned char>(0x4e295b) ^= 1;
    // Root installed first, then a rejected memo call: transaction restores root.
    check(!memo::install_at({core::memo_site_va, core::query_va}, false) && pristine() && !phases::enabled,
          "memo claim failure rolls root back");
    phases::fixture_fail_restore(true);
    check(!memo::install_at({core::memo_site_va, core::query_va}, false) &&
              !std::strcmp(memo::state(), "rollback_failed") && !pristine(),
          "failed root rollback retained and reported");
    phases::fixture_fail_restore(false);
    check(memo::shutdown() && pristine(), "failed root rollback retries successfully");
    SetEnvironmentVariableW(L"X3M_COLLIDE_MEMO", L"1");
    check(memo::initialize() && phases::enabled && !pristine(), "both hooks installed");
    x3m_collide_query_descent_target = addr(&query_abi_target);
    HANDLE mixed = CreateThread(nullptr, 0, mixed_first_thread, nullptr, 0, nullptr);
    WaitForSingleObject(mixed, INFINITE);
    CloseHandle(mixed);
    x3m_collide_query_descent_target = 0x004e2530;
    phases::begin();
    phases::end();
    check(phases::fixture_sample().queries == 1 && phases::fixture_sample().cpu_mode == 1,
          "first mixed foreign thread cannot steal normal memo owner");
    abi();
    Object a{}, b{};
    double rotation_matrix[9];
    rotation(rotation_matrix, 0);
    set_rotation(a, rotation_matrix);
    set_rotation(b, rotation_matrix);
    Model *ma = make_model(64, 20), *mb = make_model(64, 20);
    place(a, ma, 0, 0, 0, 1);
    place(b, mb, 100000, 0, 0, 1);
    const Mode mode{2, 1, 0, false, 0};
    memo::device_reset();
    phases::fixture_clear();
    const auto calls_before = clock_calls;
    query(a, b, mode, "first");
    check(clock_calls - calls_before == 4, "miss exactly four clocks");
    const auto sampled = phases::fixture_sample();
    check(sampled.queries == 1 && sampled.descents == 1 && sampled.query == 3 && sampled.descent == 1 &&
              sampled.non_descent == 2,
          "same-sample raw query/descent/difference");
    const auto before_hit = clock_calls;
    query(a, b, mode, "hit");
    check(clock_calls - before_hit == 0 && phases::fixture_sample().queries == 1,
          "memo hit and untracked descent have no clocks");
    for (unsigned fault = 0; fault < 4; ++fault) {
        const auto before = phases::fixture_sample();
        phases::begin();
        if (fault == 0) fail_clock = true;
        if (fault == 1) reenter_at_clock = true;
        if (fault == 2) foreign_at_clock = true;
        if (fault == 3) reset_at_clock = true;
        phases::end();
        fail_clock = false;
        check(phases::fixture_sample().invalid == before.invalid + 1, "failed/poisoned clocks discard full query");
    }
    phases::begin();
    phases::present(1, 1);
    check(phases::fixture_sample().queries == 0, "abandoned query discarded at Present");
    phases::device_reset();
    phases::begin();
    phases::end();
    check(phases::fixture_sample().reset == 1 && phases::fixture_sample().queries == 1,
          "Reset discards partial frame and rearms");
    const auto before_foreign = clock_calls;
    Job job{&a, &b, {}};
    HANDLE thread = CreateThread(nullptr, 0, foreign_thread_main, &job, 0, nullptr);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    check(clock_calls == before_foreign, "foreign queries have no QPC");
    // Actual nested engine path while the outer memo return slot is live.
    place(b, mb, 200000, 0, 0, 1);
    Job nested{&a, &b, {}};
    const auto reentered = memo::counters().reentered;
    const auto invalid = phases::fixture_sample().invalid;
    nested_job = &nested;
    nested_at = clock_calls + 2;
    State outer{};
    fx_call(core::caller_va, a.node, b.node, a.body, b.body, 2, 1, 0, 0, &outer);
    check(!nested_job && memo::counters().reentered == reentered + 1 && outer.eax == 0 && nested.state.eax == 0 &&
              phases::fixture_sample().invalid == invalid + 1,
          "nested memo/root bypass preserves outer return and discards timing");
    phases::fixture_clock(nullptr);
    check(memo::shutdown() && memo::initialize(), "real clock calibration after fault-clock tests");
    memo::device_reset();
    phases::fixture_clear();
    for (unsigned frame = 1; frame <= 600; ++frame) {
        place(b, mb, 100000 + frame, 0, 0, 1);
        query(a, b, mode, "moving");
        memo::present(1, frame, false);
    }
    check(phases::fixture_window_size() == 0 && tally.differences == 0 && tally.register_differences == 0,
          "600 moving frames emit two windows; engine parity");
    // Real recursive/leaf/contact behavior, against the independent query copy.
    unsigned contacts = 0, deep = 0;
    for (int x = -35; x <= 35; ++x) {
        place(b, mb, x, 1, 0, 1);
        query(a, b, mode, "deep_contact");
        contacts += last_contact;
        deep += engine<std::uint32_t>(core::visits_va) > 1;
    }
    check(contacts > 0 && deep > 0 && tally.differences == 0,
          "recursive descent and contacts match truly unpatched reference");
    check(memo::shutdown() && memo::install_at({core::memo_site_va, core::memo_target_va}, true),
          "verify-mode diagnostic install");
    place(b, mb, 100000, 0, 0, 1);
    query(a, b, mode, "verify_first");
    query(a, b, mode, "verify_repeat");
    check(phases::fixture_sample().verifies == 1 && memo::counters().verify_mismatches == 0 && tally.differences == 0,
          "verify query counted separately with parity");
    // Same harness and moving root-separated pair: median aggregate cost on/off.
    double cost[2]{};
    for (unsigned enabled = 0; enabled < 2; ++enabled) {
        check(memo::shutdown() && pristine(), "restore before benchmark mode");
        SetEnvironmentVariableW(L"X3M_COLLIDE_QUERY_PHASES", enabled ? L"1" : nullptr);
        check(memo::initialize(), "benchmark install");
        double trials[7];
        State state{};
        for (unsigned trial = 0; trial < 7; ++trial) {
            const auto start = seconds();
            for (unsigned k = 0; k < 10000; ++k) {
                b.node[0xb0 / 4] = 100000 + trial * 10000 + k;
                fx_call(core::caller_va, a.node, b.node, a.body, b.body, 2, 1, 0, 0, &state);
            }
            trials[trial] = (seconds() - start) * 1e9 / 10000;
        }
        std::sort(trials, trials + 7);
        cost[enabled] = trials[3];
    }
    std::printf("COLLIDE QUERY BENCH off_ns=%.1f on_ns=%.1f overhead_ns=%.1f iterations=70000\n", cost[0], cost[1],
                cost[1] - cost[0]);
    check(memo::shutdown() && pristine(), "shutdown restores both hooks");
    engine_patch::close_install_window("query_fixture");
    check(!phases::initialize(true) && pristine(), "late claim refused");
    std::printf("COLLIDE QUERY CPU checks=%u failures=%u queries=%lu differences=%lu\n", checks, failures,
                tally.queries, tally.differences);
    return failures ? 1 : 0;
}
