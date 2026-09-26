#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/light_phases.h"
#include "../../src/proxy/light_phase_sites.h"
#include "../../src/proxy/frame_phases.h"
#include "../../src/proxy/lean_stub.h"
#include "../../src/proxy/cpu_state.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
namespace patch = x3m::engine_patch;
namespace submit = x3m::light_phases;
namespace site = x3m::light_phases::sites;
namespace detail = x3m::light_phases::detail;
// The production unit's link-time neighbours, reduced to what it reads.
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::putchar('\n');
}
}
namespace x3m::telemetry {
bool enabled() {
    return true;
}
std::uint64_t frequency() {
    LARGE_INTEGER f{};
    return QueryPerformanceFrequency(&f) ? std::uint64_t(f.QuadPart) : 0;
}
}
namespace x3m::object_trace {
bool executable_verified() {
    return true;
}
}
namespace x3m::frame_phases {
std::atomic<bool> active{true};
}
extern "C" {
struct Snapshot {
    std::uint32_t regs[9];
    unsigned char xmm[128], x87[108];
    std::uint32_t mxcsr;
};
Snapshot* fixture_output = nullptr;
Snapshot body_input{};
std::uint32_t body_input_error = 0;
void fixture_snapshot_input();
std::uint32_t fixture_entry_esp = 0, fixture_exit_esp = 0;
alignas(16) unsigned char fixture_input_x87[108];
alignas(16) unsigned char fixture_xmm_seed[128];
std::uint16_t fixture_cw = 0x077f;
std::uint32_t fixture_mxcsr = 0x3f80;
std::uint32_t fixture_flags = 0x647;
void fixture_call(std::uint32_t target, std::uint32_t ebx, std::uint32_t ebp, std::uint32_t eax, std::uint32_t ecx,
                  std::uint32_t edx);
void fixture_nop_ret();
void fixture_pop_ret();
}
// Four-byte caller stack, no 16-byte alignment promise; hostile live state:
// two values on the x87 stack (the st(0) the engine may hold across a site),
// a non-default x87 control word and MXCSR, seeded XMM0-7, DF and the
// arithmetic flags set. The harness of game_phase_cpu_fixture.cpp.
asm(".text\n.globl _fixture_call\n_fixture_call:\n"
    "movl %esp,_fixture_entry_esp\n"
    "pushfl\n pushal\n subl $256,%esp\n"
    "fnsave 0(%esp)\n frstor 0(%esp)\n stmxcsr 108(%esp)\n"
    "movups %xmm0,112(%esp)\n movups %xmm1,128(%esp)\n movups %xmm2,144(%esp)\n movups %xmm3,160(%esp)\n"
    "movups %xmm4,176(%esp)\n movups %xmm5,192(%esp)\n movups %xmm6,208(%esp)\n movups %xmm7,224(%esp)\n"
    "movl 296(%esp),%eax\n movl %eax,240(%esp)\n"
    "fninit\n fld1\n fldpi\n fldcw _fixture_cw\n ldmxcsr _fixture_mxcsr\n"
    "movups _fixture_xmm_seed,%xmm0\n movups _fixture_xmm_seed+16,%xmm1\n movups _fixture_xmm_seed+32,%xmm2\n movups _fixture_xmm_seed+48,%xmm3\n"
    "movups _fixture_xmm_seed+64,%xmm4\n movups _fixture_xmm_seed+80,%xmm5\n movups _fixture_xmm_seed+96,%xmm6\n movups _fixture_xmm_seed+112,%xmm7\n"
    "movl 300(%esp),%ebx\n movl 304(%esp),%ebp\n movl 308(%esp),%eax\n movl 312(%esp),%ecx\n movl 316(%esp),%edx\n"
    "movl $0x12345678,%esi\n movl $0x98765432,%edi\n pushl _fixture_flags\n popfl\n fnsave _fixture_input_x87\n frstor _fixture_input_x87\n call *240(%esp)\n"
    "pushfl\n pushal\n movl %esp,%esi\n movl _fixture_output,%edi\n movl $9,%ecx\n cld\n rep movsl\n"
    "movl _fixture_output,%edi\n movups %xmm0,36(%edi)\n movups %xmm1,52(%edi)\n movups %xmm2,68(%edi)\n movups %xmm3,84(%edi)\n"
    "movups %xmm4,100(%edi)\n movups %xmm5,116(%edi)\n movups %xmm6,132(%edi)\n movups %xmm7,148(%edi)\n"
    "fnsave 164(%edi)\n frstor 164(%edi)\n stmxcsr 272(%edi)\n addl $36,%esp\n"
    "frstor 0(%esp)\n ldmxcsr 108(%esp)\n"
    "movups 112(%esp),%xmm0\n movups 128(%esp),%xmm1\n movups 144(%esp),%xmm2\n movups 160(%esp),%xmm3\n"
    "movups 176(%esp),%xmm4\n movups 192(%esp),%xmm5\n movups 208(%esp),%xmm6\n movups 224(%esp),%xmm7\n"
    "addl $256,%esp\n popal\n popfl\n movl %esp,_fixture_exit_esp\n ret\n"
    ".globl _fixture_snapshot_input\n_fixture_snapshot_input:\n"
    "pushfl\n pushal\n movl %esp,%esi\n movl $_body_input,%edi\n movl $9,%ecx\n cld\n rep movsl\n"
    "movl $_body_input,%edi\n movups %xmm0,36(%edi)\n movups %xmm1,52(%edi)\n movups %xmm2,68(%edi)\n movups %xmm3,84(%edi)\n"
    "movups %xmm4,100(%edi)\n movups %xmm5,116(%edi)\n movups %xmm6,132(%edi)\n movups %xmm7,148(%edi)\n"
    "fnsave 164(%edi)\n frstor 164(%edi)\n stmxcsr 272(%edi)\n popal\n popfl\n ret\n"
    // Stand-ins for the relocated call targets: D3DXMatrixInverse / _malloc (the
    // body balances their arguments itself) and the queue walk 0x0047e6e0 (one
    // pushed argument, popped here so the body stays linear).
    ".globl _fixture_nop_ret\n_fixture_nop_ret:\n ret\n"
    ".globl _fixture_pop_ret\n_fixture_pop_ret:\n ret $4\n");

static unsigned checks = 0, failures = 0;
static void check(bool okay, const char* label) {
    ++checks;
    if (!okay) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}
static inline double number(std::uint64_t value) {
    return double(std::uint32_t(value >> 32)) * 4294967296.0 + double(std::uint32_t(value));
}
static std::uintptr_t address(const void* p) {
    return reinterpret_cast<std::uintptr_t>(p);
}
static void invoke(void* target, Snapshot& out) {
    fixture_output = &out;
    SetLastError(0x13572468);
    fixture_call(std::uint32_t(address(target)), 0x23456789, 0x3456789a, 0x456789ab, 0x56789abc, 0x6789abcd);
    check(GetLastError() == 0x24681357, "native output LastError preserved");
    check(fixture_entry_esp == fixture_exit_esp, "four-byte incoming caller stack balanced");
    check(std::memcmp(fixture_input_x87, out.x87, 108) != 0, "native x87 output differs from entry image");
}
static void compare(const Snapshot& before, const Snapshot& after) {
    for (unsigned r = 0; r < 9; ++r)
        if (r != 3) check(before.regs[r] == after.regs[r], "GPR and EFLAGS match native baseline");
    check(!std::memcmp(before.xmm, after.xmm, 128), "all XMM registers match native baseline");
    check(before.mxcsr == after.mxcsr, "MXCSR matches native baseline");
    check(!std::memcmp(before.x87, after.x87, 108), "x87 image matches native baseline");
}

extern "C" {
std::uint32_t body_path = 0, observed_residue = 0, native_mxcsr = 0x5f80, nesting = 0;
std::uint16_t native_cw = 0x0b7f;
void __cdecl native_error() {
    body_input_error = GetLastError();
    SetLastError(0x24681357);
}
}
struct Body {
    void* body = nullptr;
    void* spans[2]{};
    void* alternate = nullptr;
    unsigned length = 0;
};
static Body make_body(bool bench = false) {
    patch::Emitter e(512);
    Body b;
    b.body = e.here();
    const auto span = [&](unsigned i) {
        b.spans[i] = e.here();
        e.bytes(site::kSites[i].expected, site::kSites[i].length);
    };
    span(0);
    e.byte(0xe8);
    e.rel32(reinterpret_cast<void*>(&fixture_snapshot_input));
    e.bytes("\x83\xec\x34\x53\x56\x57", 6); // native allocation and nonvolatile saves
    e.bytes("\x8d\x45\x04\x83\xe0\x0f\xa3", 7);
    e.dword(std::uint32_t(address(&observed_residue)));
    e.byte(0xfc);
    e.byte(0xe8);
    e.rel32(reinterpret_cast<void*>(&native_error)); // C++ needs DF clear
    e.byte(0xd9);
    e.byte(bench ? 0xd0 : 0xee);
    e.bytes("\xd9\x2d", 2);
    e.dword(std::uint32_t(address(&native_cw))); // fldz; fldcw, distinct output
    e.bytes("\x0f\xae\x15", 3);
    e.dword(std::uint32_t(address(&native_mxcsr)));
    e.bytes("\x66\x0f\xef\xc0", 4); // pxor xmm0,xmm0: native output differs
    // Native nesting is outside the proved chain but model an asynchronous
    // callback once, with the real prologue/epilogue/token of the same body.
    e.bytes("\x83\x3d", 2);
    e.dword(std::uint32_t(address(&nesting)));
    e.byte(0);
    e.bytes("\x74\x14\xc7\x05", 4);
    e.dword(std::uint32_t(address(&nesting)));
    e.dword(0);
    e.bytes("\x6a\x00\xe8", 3);
    e.rel32(b.body);
    e.bytes("\x90\x90\x90", 3);
    // Four native-style gates to the shared exit, or populated fall-through.
    unsigned char* branches[5]{};
    for (unsigned i = 0; i < 5; ++i) {
        e.bytes("\x83\x3d", 2);
        e.dword(std::uint32_t(address(&body_path)));
        e.byte(static_cast<unsigned char>(i + 1));
        e.bytes("\x0f\x84", 2);
        branches[i] = static_cast<unsigned char*>(e.here());
        e.dword(0);
    }
    e.bytes("\xb8\x11\x22\x33\x44\xf9", 6);
    e.byte(bench ? 0xfc : 0xfd); // populated path: eax; stc; std
    for (unsigned i = 0; i < 4; ++i) {
        const auto rel = std::int32_t(static_cast<unsigned char*>(e.here()) - branches[i] - 4);
        std::memcpy(branches[i], &rel, 4);
    }
    span(1);
    e.bytes("\x5d\xc2\x04\x00", 4);
    b.alternate = e.here(); // controlled exit-bypass witness (no exception catch)
    const auto rel = std::int32_t(static_cast<unsigned char*>(b.alternate) - branches[4] - 4);
    std::memcpy(branches[4], &rel, 4);
    e.bytes(site::kSites[1].expected, 5);
    e.bytes("\x5d\xc2\x04\x00", 4);
    b.length = unsigned(static_cast<unsigned char*>(e.here()) - static_cast<unsigned char*>(b.body));
    if (!e.finish()) b.body = nullptr;
    return b;
}
struct Caller {
    void* code;
    std::uint32_t ret;
};
static Caller make_caller(const Body& b, unsigned padding) {
    patch::Emitter e(64);
    Caller c{e.here(), 0};
    e.bytes("\x83\xec", 2);
    e.byte(static_cast<unsigned char>(padding));
    e.bytes("\x6a\x00\xe8", 3);
    e.rel32(b.body);
    c.ret = std::uint32_t(address(e.here()));
    e.bytes("\x8d\x64\x24", 3);
    e.byte(static_cast<unsigned char>(padding));
    e.byte(0xc3);
    if (!e.finish()) c.code = nullptr;
    return c;
}
static void specs_for(const Body& b, patch::SiteSpec* specs) {
    for (unsigned i = 0; i < 2; ++i) {
        specs[i] = site::kSites[i];
        specs[i].address = address(b.spans[i]);
    }
}
static DWORD WINAPI foreign(LPVOID p) {
    reinterpret_cast<void (*)()>(p)();
    return 0;
}
static void replay() {
    Body b = make_body();
    check(b.body != nullptr, "body emitted");
    if (!b.body) return;
    Caller callers[4];
    for (unsigned i = 0; i < 4; ++i) callers[i] = make_caller(b, i * 4);
    submit::fixture_returns(callers[0].ret, callers[1].ret);
    Snapshot baseline[5][4]{}, inputs[5][4]{}, hooked{};
    bool residues[4]{};
    for (unsigned path = 0; path < 5; ++path)
        for (unsigned r = 0; r < 4; ++r) {
            body_path = path;
            invoke(callers[r].code, baseline[path][r]);
            inputs[path][r] = body_input;
            check(body_input_error == 0x13572468, "baseline native input LastError");
            check(observed_residue % 4 == 0, "incoming stack four-byte aligned");
            residues[observed_residue / 4] = true;
        }
    check(residues[0] && residues[1] && residues[2] && residues[3], "all four incoming ESP residues exercised");
    patch::SiteSpec specs[2];
    specs_for(b, specs);
    unsigned char original[512];
    std::memcpy(original, b.body, b.length);
    const char* status = nullptr;
    check(submit::fixture_install(specs, &status), "group installed");
    body_path = 0;
    invoke(callers[0].code, hooked);
    compare(baseline[0][0], hooked);
    check(submit::fixture_gate()->early == 2 && submit::fixture_accumulator()->pending.stamps == 0,
          "early stamps rejected");
    submit::frame(1, false);
    for (unsigned path = 0; path < 5; ++path)
        for (unsigned r = 0; r < 4; ++r) {
            body_path = path;
            invoke(callers[r].code, hooked);
            compare(baseline[path][r], hooked);
            compare(inputs[path][r], body_input);
            check(body_input_error == 0x13572468, "entry LastError preserved before native work");
        }
    const auto* a = submit::fixture_accumulator();
    check(a->pending.calls[0] == 5 && a->pending.calls[1] == 5 && a->pending.calls[2] == 10,
          "cockpit traversal unknown buckets and all exits counted");
    check(!a->depth && !a->poisoned, "every normal token paired");
    HANDLE t = CreateThread(nullptr, 0, &foreign, callers[0].code, 0, nullptr);
    check(t != nullptr, "foreign thread created");
    if (t) {
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
    }
    check(submit::fixture_gate()->foreign == 2 && a->pending.stamps == 40,
          "foreign stamps leave owner state unchanged");
    submit::frame(2, true);
    body_path = 5;
    invoke(callers[0].code, hooked);
    check(a->depth == 1, "controlled native bypass leaves an open record");
    submit::frame(3, false);
    check(a->depth == 0 && a->errors.unmatched == 1, "discard invalidates abandoned call");
    body_path = 0;
    nesting = 1;
    invoke(callers[0].code, hooked);
    check(a->errors.nested == 1 && a->pending.calls[0] == 0 && a->depth == 0,
          "nested native path poisons both calls without double counting");
    submit::frame(4, false);
    invoke(callers[0].code, hooked);
    check(a->pending.calls[0] == 1 && !a->poisoned, "boundary recovers after nested call");
    submit::active.store(false);
    invoke(callers[0].code, hooked);
    compare(baseline[0][0], hooked);
    check(a->pending.calls[0] == 1, "disabled patched stub forwards without sampling");
    check(submit::fixture_uninstall(), "uninstall succeeds");
    check(!std::memcmp(original, b.body, b.length), "rollback restores exact bytes");
    specs[1].expected[0] ^= 1;
    check(!submit::fixture_install(specs, &status), "last span mismatch refuses preflight");
    check(!std::memcmp(original, b.body, b.length), "preflight leaves bytes untouched");
    submit::fixture_uninstall();
    specs[1].expected[0] ^= 1;
    specs[1] = specs[0];
    check(!submit::fixture_install(specs, &status), "duplicate span causes partial install refusal");
    check(!std::memcmp(original, b.body, b.length), "partial install rollback restores both spans");
    submit::fixture_uninstall();
}
// Unlike the image fixture, no FNSAVE/FRSTOR occurs between the final mode
// setter and either native arithmetic witness. Exercise all 16 RC pairs,
// both setter orders and all four caller stack residues.
extern "C" {
std::uint16_t arithmetic_cw = 0x037f;
std::uint32_t arithmetic_mxcsr = 0x1f80, arithmetic_order = 0, arithmetic_target = 0;
std::int32_t arithmetic_three = 3;
std::uint64_t arithmetic_inside = 0, arithmetic_after = 0;
void arithmetic_call();
void arithmetic_ratio_inside();
void arithmetic_ratio_after();
}
asm(".text\n.globl _arithmetic_call\n_arithmetic_call:\n"
    "pushfl\n pushal\n subl $256,%esp\n"
    "fnsave 0(%esp)\n stmxcsr 108(%esp)\n"
    "movups %xmm0,112(%esp)\n movups %xmm1,128(%esp)\n movups %xmm2,144(%esp)\n movups %xmm3,160(%esp)\n"
    "movups %xmm4,176(%esp)\n movups %xmm5,192(%esp)\n movups %xmm6,208(%esp)\n movups %xmm7,224(%esp)\n"
    "fninit\n cmpl $0,_arithmetic_order\n jne 1f\n"
    "fldcw _arithmetic_cw\n ldmxcsr _arithmetic_mxcsr\n jmp 2f\n"
    "1: ldmxcsr _arithmetic_mxcsr\n fldcw _arithmetic_cw\n"
    "2: call *_arithmetic_target\n"
    "frstor 0(%esp)\n ldmxcsr 108(%esp)\n"
    "movups 112(%esp),%xmm0\n movups 128(%esp),%xmm1\n movups 144(%esp),%xmm2\n movups 160(%esp),%xmm3\n"
    "movups 176(%esp),%xmm4\n movups 192(%esp),%xmm5\n movups 208(%esp),%xmm6\n movups 224(%esp),%xmm7\n"
    "addl $256,%esp\n popal\n popfl\n ret\n"
    ".globl _arithmetic_ratio_inside\n_arithmetic_ratio_inside:\n"
    "fld1\n fildl _arithmetic_three\n fdivrp %st,%st(1)\n fstpl _arithmetic_inside\n ret\n"
    ".globl _arithmetic_ratio_after\n_arithmetic_ratio_after:\n"
    "fld1\n fildl _arithmetic_three\n fdivrp %st,%st(1)\n fstpl _arithmetic_after\n ret\n");
static Body make_arithmetic_body() {
    patch::Emitter e(128);
    Body b;
    b.body = e.here();
    b.spans[0] = e.here();
    e.bytes(site::kSites[0].expected, 6);
    e.bytes("\x83\xec\x34\x53\x56\x57", 6);
    e.byte(0xe8);
    e.rel32(reinterpret_cast<void*>(&arithmetic_ratio_inside));
    b.spans[1] = e.here();
    e.bytes(site::kSites[1].expected, 5);
    e.bytes("\x5d\xc2\x04\x00", 4);
    b.length = unsigned(static_cast<unsigned char*>(e.here()) - static_cast<unsigned char*>(b.body));
    if (!e.finish()) b.body = nullptr;
    return b;
}
static Caller make_arithmetic_caller(const Body& b, unsigned padding) {
    patch::Emitter e(64);
    Caller c{e.here(), 0};
    e.bytes("\x83\xec", 2);
    e.byte(static_cast<unsigned char>(padding));
    e.bytes("\x6a\x00\xe8", 3);
    e.rel32(b.body);
    c.ret = std::uint32_t(address(e.here()));
    e.byte(0xe8);
    e.rel32(reinterpret_cast<void*>(&arithmetic_ratio_after));
    e.bytes("\x8d\x64\x24", 3);
    e.byte(static_cast<unsigned char>(padding));
    e.byte(0xc3);
    if (!e.finish()) c.code = nullptr;
    return c;
}
static DWORD WINAPI arithmetic_foreign(LPVOID) {
    arithmetic_call();
    return 0;
}
static void arithmetic_checks() {
    Body b = make_arithmetic_body();
    check(b.body != nullptr, "arithmetic body emitted");
    if (!b.body) return;
    Caller callers[4];
    for (unsigned r = 0; r < 4; ++r) callers[r] = make_arithmetic_caller(b, r * 4);
    struct Result {
        std::uint64_t inside, after;
    } baseline[4][4][2][4]{};
    for (unsigned cw = 0; cw < 4; ++cw)
        for (unsigned mx = 0; mx < 4; ++mx)
            for (unsigned order = 0; order < 2; ++order)
                for (unsigned r = 0; r < 4; ++r) {
                    arithmetic_cw = std::uint16_t(0x037f | (cw << 10));
                    arithmetic_mxcsr = 0x1f80 | (mx << 13);
                    arithmetic_order = order;
                    arithmetic_target = std::uint32_t(address(callers[r].code));
                    arithmetic_call();
                    baseline[cw][mx][order][r] = {arithmetic_inside, arithmetic_after};
                    check(arithmetic_inside >= 0x3fd5555555555555ull && arithmetic_inside <= 0x3fd5555555555556ull,
                          "arithmetic witness computes one third");
                }
    patch::SiteSpec specs[2];
    specs_for(b, specs);
    const char* status = nullptr;
    check(submit::fixture_install(specs, &status), "arithmetic group installed");
    submit::fixture_returns(callers[0].ret, callers[1].ret);
    for (unsigned admission = 0; admission < 4; ++admission) {
        if (admission == 1) submit::frame(100, false);
        if (admission == 3) submit::active.store(false);
        for (unsigned cw = 0; cw < 4; ++cw)
            for (unsigned mx = 0; mx < 4; ++mx)
                for (unsigned order = 0; order < 2; ++order)
                    for (unsigned r = 0; r < 4; ++r) {
                        arithmetic_cw = std::uint16_t(0x037f | (cw << 10));
                        arithmetic_mxcsr = 0x1f80 | (mx << 13);
                        arithmetic_order = order;
                        arithmetic_target = std::uint32_t(address(callers[r].code));
                        if (admission == 2) {
                            HANDLE t = CreateThread(nullptr, 0, &arithmetic_foreign, nullptr, 0, nullptr);
                            check(t != nullptr, "arithmetic foreign thread");
                            if (t) {
                                WaitForSingleObject(t, INFINITE);
                                CloseHandle(t);
                            }
                        } else
                            arithmetic_call();
                        const auto expected = baseline[cw][mx][order][r];
                        check(arithmetic_inside == expected.inside,
                              "native in-body arithmetic equals unhooked across RC/order/residue/admission");
                        check(arithmetic_after == expected.after,
                              "native post-exit arithmetic equals unhooked across RC/order/residue/admission");
                        if (admission == 1) {
                            const auto* a = submit::fixture_accumulator();
                            check((cw == mx) ? (a->pending.stamps == 2 && !a->poisoned)
                                             : (a->pending.stamps == 0 && a->poisoned),
                                  "equal modes sampled, mixed modes bypass before clocks");
                            submit::frame(101, false);
                        }
                    }
    }
    check(submit::fixture_accumulator()->errors.mode_refused == 192,
          "all 96 owner mixed-mode calls refused at both boundaries");
    check(submit::fixture_uninstall(), "arithmetic rollback");
}

static void benchmark() {
    x3m::PreserveCpuState cpu;
    Body b = make_body(true);
    Caller c = make_caller(b, 0);
    submit::fixture_returns(c.ret, 0);
    body_path = 0;
    constexpr unsigned loops = 20000, trials = 7;
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    const auto timed = [&]() {
        std::uint64_t best = ~std::uint64_t(0);
        for (unsigned t = 0; t < trials; ++t) {
            LARGE_INTEGER s{}, e{};
            QueryPerformanceCounter(&s);
            for (unsigned i = 0; i < loops; ++i) reinterpret_cast<void (*)()>(c.code)();
            QueryPerformanceCounter(&e);
            auto v = std::uint64_t(e.QuadPart - s.QuadPart);
            if (v < best) best = v;
        }
        return best;
    };
    // Keep the repeated synthetic workload x87-neutral and DF clear; the
    // benchmark boundary restores its caller CPU state after both trials.
    const auto before = timed();
    patch::SiteSpec specs[2];
    specs_for(b, specs);
    const char* status = nullptr;
    check(submit::fixture_install(specs, &status), "benchmark install");
    submit::frame(1, false);
    const auto after = timed();
    check(submit::fixture_accumulator()->pending.calls[0] == loops * trials, "benchmark counted every call");
    submit::fixture_uninstall();
    const double dispatch = (number(after) - number(before)) * 1e9 / number(std::uint64_t(f.QuadPart)) / loops / 2;
    std::printf("LIGHT PHASE BENCH loops=%u trials=%u dispatch_ns=%.1f documented_dispatch_ns=%llu\n", loops, trials,
                dispatch, static_cast<unsigned long long>(detail::dispatch_cost_ns));
    check(dispatch > 0, "positive dispatch overhead");
    if (detail::dispatch_cost_ns)
        check(number(detail::dispatch_cost_ns) > dispatch / 2 && number(detail::dispatch_cost_ns) < dispatch * 2,
              "calibrated cost within 2x");
}
int main() {
    for (unsigned i = 0; i < 128; ++i) fixture_xmm_seed[i] = static_cast<unsigned char>(0xa5 ^ i * 7);
    replay();
    arithmetic_checks();
    benchmark();
    Body b = make_body();
    patch::SiteSpec specs[2];
    specs_for(b, specs);
    const char* status = nullptr;
    patch::close_install_window("fixture");
    check(!submit::fixture_install(specs, &status), "late install refused");
    std::printf("LIGHT PHASE CPU sites=2 checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
