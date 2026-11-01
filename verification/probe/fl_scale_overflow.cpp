// Native fixture of the FL scaler replacement (src/engine/fl/scale_overflow_fix_fl.cpp,
// docs/architecture/farnhams-legacy-scale-overflow.md). No launch, no tracked game bytes: argv[1] is a file with
// the helper's body bytes, written from the local X3FL.exe by tools/analysis/fl_scale_overflow_constants.py --body
// (verification/analysis/test_fl_scale_overflow.py). Built with -DX3M_EDITION_FL=1 -DX3M_FL_SCALE_FIX_FIXTURE.
//
// The body is copied to base + helper_rva of a synthetic image (executable, PAGE_EXECUTE_READ), with a fake caller
// whose `call rel32` sits at base + 0x22dd4 (the FL run 3 camera site). A thunk loads sentinels into EBX/ESI/EDI/
// EBP, XMM0-7, a non-default x87 control word with two stack values and a non-default MXCSR, sets ECX = a,
// EDX = &b, pushes &c and out, calls, and records EAX/ECX/EDX, the preserved registers, ESP, XMM0-7, the FNSAVE
// image and MXCSR. The original helper is run first (a vectored handler turns its c0000095 into a recorded trap),
// then the production gate and install patch the entry and the same thunk runs the replacement.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../../src/engine/fl/scale_overflow_fix_fl.h"
#include "../../src/proxy/engine_patch.h"
namespace layout {
#include "../../src/engine/fl/scale_overflow_fix_fl_inc.h"
}
namespace sf = x3m::fl::scale_fix;
unsigned checks = 0, failures = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}
char last_row[1024];
unsigned rows = 0;
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(last_row, sizeof last_row, format, args);
    va_end(args);
    ++rows;
    std::printf("LOG %s\n", last_row);
}
}
bool row_has(const char* text) {
    return std::strstr(last_row, text) != nullptr;
}
extern "C" {
uintptr_t g_fn = 0;
const int32_t *g_a = nullptr, *g_b = nullptr, *g_c = nullptr;
int32_t* g_out = nullptr;
uint32_t host_esp = 0, esp_before = 0, esp_after = 0, r_eax = 0, r_ecx = 0, r_edx = 0, r_ebx = 0, r_esi = 0, r_edi = 0,
         r_ebp = 0, trapped = 0;
uint32_t mxcsr_in = 0x5f80, mxcsr_after = 0, mxcsr_default = 0x1f80; // RC = round up, all masked
uint16_t fcw_in = 0x0e7f;                                           // RC = toward zero, 64-bit precision
alignas(16) uint32_t xmm_in[32], xmm_after[32];
alignas(16) unsigned char x87_before[108], x87_after[108];
void call_helper();
void helper_recover();
uint32_t __cdecl bench(uintptr_t fn, const int32_t* a, const int32_t* b, int32_t* out, const int32_t* c, uint32_t n);
}
asm(".text\n\t.globl _call_helper\n_call_helper:\n\t"
    "pushl %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
    "movl %esp,_host_esp\n\t"
    "fninit\n\tfldcw _fcw_in\n\tfldpi\n\tfld1\n\tfnsave _x87_before\n\tfrstor _x87_before\n\t"
    "ldmxcsr _mxcsr_in\n\t"
    "movups _xmm_in,%xmm0\n\tmovups _xmm_in+16,%xmm1\n\tmovups _xmm_in+32,%xmm2\n\tmovups _xmm_in+48,%xmm3\n\t"
    "movups _xmm_in+64,%xmm4\n\tmovups _xmm_in+80,%xmm5\n\tmovups _xmm_in+96,%xmm6\n\tmovups _xmm_in+112,%xmm7\n\t"
    "pushl _g_c\n\tpushl _g_out\n\t"
    "movl _g_a,%ecx\n\tmovl _g_b,%edx\n\t"
    "movl $0x5a5a3333,%ebx\n\tmovl $0x5a5a1111,%esi\n\tmovl $0x5a5a2222,%edi\n\tmovl $0x5a5a4444,%ebp\n\t"
    "movl %esp,_esp_before\n\t"
    "call *_g_fn\n\t"
    "movl %esp,_esp_after\n\t"
    "movl %eax,_r_eax\n\tmovl %ecx,_r_ecx\n\tmovl %edx,_r_edx\n\tmovl %ebx,_r_ebx\n\tmovl %esi,_r_esi\n\t"
    "movl %edi,_r_edi\n\tmovl %ebp,_r_ebp\n\t"
    "movups %xmm0,_xmm_after\n\tmovups %xmm1,_xmm_after+16\n\tmovups %xmm2,_xmm_after+32\n\tmovups %xmm3,_xmm_after+48\n\t"
    "movups %xmm4,_xmm_after+64\n\tmovups %xmm5,_xmm_after+80\n\tmovups %xmm6,_xmm_after+96\n\tmovups %xmm7,_xmm_after+112\n\t"
    "stmxcsr _mxcsr_after\n\tfnsave _x87_after\n\t"
    ".globl _helper_recover\n_helper_recover:\n\t"
    "ldmxcsr _mxcsr_default\n\tfninit\n\t"
    "movl _host_esp,%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t"
    // bench(fn, a, b, out, c, n): n calls with the engine's argument setup
    ".globl _bench\n_bench:\n\t"
    "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tmovl 28(%ebp),%ebx\n"
    "1:\n\tpushl 24(%ebp)\n\tpushl 20(%ebp)\n\tmovl 12(%ebp),%ecx\n\tmovl 16(%ebp),%edx\n\tcall *8(%ebp)\n\t"
    "addl $8,%esp\n\tdecl %ebx\n\tjnz 1b\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");

unsigned char* image = nullptr; // the synthetic base
uintptr_t base = 0, helper = 0, caller = 0;
constexpr uint32_t image_size = 0x30000, caller_site_rva = 0x22dd4, caller_rva = caller_site_rva - 8;
constexpr uint32_t trap_code = 0xc0000095; // STATUS_INTEGER_OVERFLOW
uint32_t trap_offset = 0, trap_seen = 0;
LONG CALLBACK on_exception(EXCEPTION_POINTERS* info) {
    const uintptr_t eip = info->ContextRecord->Eip;
    if (info->ExceptionRecord->ExceptionCode != trap_code || eip < helper || eip >= helper + layout::helper_length)
        return EXCEPTION_CONTINUE_SEARCH;
    trap_seen = info->ExceptionRecord->ExceptionCode;
    trap_offset = uint32_t(eip - helper);
    trapped = 1;
    info->ContextRecord->Eip = reinterpret_cast<DWORD>(&helper_recover);
    return EXCEPTION_CONTINUE_EXECUTION;
}
bool set_protection(DWORD protection) {
    DWORD old = 0;
    return VirtualProtect(image, image_size, protection, &old) != FALSE;
}
DWORD protection_at(uintptr_t address) {
    MEMORY_BASIC_INFORMATION info{};
    return VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof info) ? info.Protect : 0;
}

struct Case {
    int32_t a[3], b, c;
};
enum Alias { distinct, out_is_a, out_is_b, out_is_c, b_is_out1, c_is_out0, alias_modes };
struct Run {
    int32_t out[4];
    uint32_t eax, ecx, edx;
    bool trapped, cpu_ok;
    uint32_t trap_offset;
};
constexpr int32_t out_sentinel = 0x0badf00d;
// One call through fn with the engine's argument setup. cpu_ok: ESP balanced, EBX/ESI/EDI/EBP, XMM0-7, the x87
// image (control, status, tag, registers; the FPU instruction/operand pointers are left out, see below), MXCSR and
// LastError unchanged. eax is reported relative to the out buffer (0 = the out pointer).
Run run(uintptr_t fn, const Case& k, Alias alias = distinct) {
    static int32_t a[4], b, c, out[4];
    std::memcpy(a, k.a, 12);
    a[3] = 0x77777777;
    b = k.b;
    c = k.c;
    for (int32_t& v : out) v = out_sentinel;
    g_a = a;
    g_b = &b;
    g_c = &c;
    g_out = out;
    if (alias == out_is_a) {
        g_out = a;
    } else if (alias == out_is_b) {
        out[0] = k.b;
        g_b = out;
    } else if (alias == out_is_c) {
        out[1] = k.c;
        g_c = out + 1;
    } else if (alias == b_is_out1) { // b read from out[1]: component 2's b is read before out[1] is stored
        out[1] = k.b;
        g_b = out + 1;
    } else if (alias == c_is_out0) { // c read from out[0]: components 1 and 2 see out[0]
        out[0] = k.c;
        g_c = out;
    }
    g_fn = fn;
    trapped = 0;
    trap_offset = 0;
    SetLastError(0x2a4);
    call_helper();
    const DWORD error = GetLastError();
    Run r{};
    std::memcpy(r.out, g_out, 16);
    r.eax = r_eax - uint32_t(reinterpret_cast<uintptr_t>(g_out));
    r.ecx = r_ecx;
    r.edx = r_edx;
    r.trapped = trapped != 0;
    r.trap_offset = trap_offset;
    // x87 pointers (FIP/FCS/FOP/FOO/FOS, bytes 12..27) are not compared: CPUs that save them only after an
    // unmasked exception (FDP_EXCPTN_ONLY, AMD's FXSAVE) can zero them across a context switch.
    r.cpu_ok = r.trapped || (esp_after == esp_before && r_ebx == 0x5a5a3333 && r_esi == 0x5a5a1111 &&
                             r_edi == 0x5a5a2222 && r_ebp == 0x5a5a4444 && !std::memcmp(xmm_in, xmm_after, 128) &&
                             !std::memcmp(x87_before, x87_after, 12) && !std::memcmp(x87_before + 28, x87_after + 28, 80) &&
                             mxcsr_after == mxcsr_in && error == 0x2a4);
    return r;
}
int32_t reference(int32_t a, int32_t b, int32_t c, bool* clamped) {
    if (!c) return 0;
    const int64_t q = int64_t(a) * b / c; // C++ truncates toward zero, as idiv does
    if (q > INT32_MAX || q < INT32_MIN) {
        *clamped = true;
        return q > 0 ? INT32_MAX : INT32_MIN;
    }
    return int32_t(q);
}
bool fits(const Case& k) {
    bool clamped = false;
    for (int i = 0; i < 3; ++i) reference(k.a[i], k.b, k.c, &clamped);
    return !clamped;
}
bool matches_reference(const Run& r, const Case& k) {
    bool clamped = false;
    for (int i = 0; i < 3; ++i)
        if (r.out[i] != reference(k.a[i], k.b, k.c, &clamped)) return false;
    return r.out[3] == out_sentinel && r.eax == 0 && !r.trapped;
}
bool same(const Run& x, const Run& y) {
    return !std::memcmp(x.out, y.out, 16) && x.eax == y.eax && x.ecx == y.ecx && x.edx == y.edx && !x.trapped && !y.trapped;
}
uint32_t rng = 0x12345678;
uint32_t next() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
int32_t random_value() {
    const uint32_t shift = next() % 32, raw = next() >> shift;
    switch (next() % 16) {
    case 0: return INT32_MIN;
    case 1: return INT32_MAX;
    case 2: return 0;
    default: return (next() & 1) ? -int32_t(raw >> 1) : int32_t(raw >> 1);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: fl_scale_overflow.exe <helper body file>\n");
        return 2;
    }
    unsigned char body[layout::helper_length]{};
    FILE* f = std::fopen(argv[1], "rb");
    const size_t got = f ? std::fread(body, 1, sizeof body, f) : 0;
    if (f) std::fclose(f);
    if (got != sizeof body) {
        std::printf("FAIL body file %s: %u bytes\n", argv[1], unsigned(got));
        return 2;
    }
    for (unsigned i = 0; i < 32; ++i) xmm_in[i] = 0x3c000000u + i * 0x01010101u;
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    base = reinterpret_cast<uintptr_t>(image);
    helper = base + layout::helper_rva;
    caller = base + caller_rva;
    std::memset(image, 0xcc, image_size);
    std::memcpy(image + layout::helper_rva, body, sizeof body);
    {
        // push dword [esp+8] (c); push dword [esp+8] (out); call rel32 helper at caller_site_rva; add esp,8; ret
        unsigned char* p = image + caller_rva;
        const unsigned char head[] = {0xff, 0x74, 0x24, 0x08, 0xff, 0x74, 0x24, 0x08, 0xe8};
        std::memcpy(p, head, sizeof head);
        const uint32_t rel = uint32_t(helper - (base + caller_site_rva + 5));
        std::memcpy(p + 9, &rel, 4);
        const unsigned char tail[] = {0x83, 0xc4, 0x08, 0xc3};
        std::memcpy(p + 13, tail, sizeof tail);
    }
    check(set_protection(PAGE_EXECUTE_READ), "synthetic image executable");
    check(base % 0x10000 == 0 && layout::helper_rva % 8 + 5 <= 8, "entry inside one qword: atomic write");
    AddVectoredExceptionHandler(1, on_exception);

    // 1. Settings and identity, before any patch.
    SetEnvironmentVariableW(L"X3M_FL_SCALE_OVERFLOW_FIX", L"0");
    check(!sf::initialize() && row_has("fl.scale_fix status=disabled "), "X3M_FL_SCALE_OVERFLOW_FIX=0: disabled");
    SetEnvironmentVariableW(L"X3M_FL_SCALE_OVERFLOW_FIX", L"1");
    check(!sf::initialize() && row_has("fl.scale_fix status=executable_mismatch "), "not X3FL.exe: executable_mismatch");
    SetEnvironmentVariableW(L"X3M_FL_SCALE_OVERFLOW_FIX", nullptr); // unset: the DLL default (on)
    check(!sf::initialize() && row_has("fl.scale_fix status=executable_mismatch "), "unset: on, then the identity gate");

    // 2. The production gate on the synthetic image: every single-byte flip of the body is refused.
    check(sf::fixture_gate(base) == nullptr, "gate accepts the copied body");
    unsigned flips = 0, refused = 0, entry_refusals = 0, body_refusals = 0;
    for (unsigned i = 0; i < layout::helper_length; ++i) {
        set_protection(PAGE_READWRITE);
        image[layout::helper_rva + i] ^= 0x01;
        set_protection(PAGE_EXECUTE_READ);
        const char* why = sf::fixture_gate(base);
        ++flips;
        if (why) ++refused;
        if (why && !std::strcmp(why, "entry_mismatch")) ++entry_refusals;
        if (why && !std::strcmp(why, "body_mismatch")) ++body_refusals;
        set_protection(PAGE_READWRITE);
        image[layout::helper_rva + i] ^= 0x01;
        set_protection(PAGE_EXECUTE_READ);
    }
    std::printf("GATE flips=%u refused=%u entry=%u body=%u\n", flips, refused, entry_refusals, body_refusals);
    check(flips == refused && entry_refusals == sizeof layout::helper_entry, "every flip refused");
    check(sf::fixture_gate(base) == nullptr && sf::fixture_gate(base + 0x10000) != nullptr, "gate is base relative");

    // 3. The original helper (pre-patch): the dump vector traps at component 2's idiv; fitting cases are recorded.
    const Case dump{{1366700, -692544, 4460464}, 1000, 2}; // FL run 3: Δ × 1000 / dt
    {
        const Run r = run(helper, dump);
        std::printf("ORIGINAL_DUMP trapped=%u code=0x%08x offset=0x%x out=%ld,%ld\n", unsigned(r.trapped),
                    unsigned(trap_seen), unsigned(r.trap_offset), long(r.out[0]), long(r.out[1]));
        check(r.trapped && trap_seen == trap_code && r.trap_offset == 0xaf, "original traps at RVA 0x12dbf (idiv ecx)");
        check(r.out[0] == 683350000 && r.out[1] == -346272000, "components 0 and 1 stored before the trap");
    }
    const Case fixed[] = {
        dump,
        {{5, -7, 123}, 1000, 0},                                // c == 0: zeros, ECX = a[2], EDX = 0
        {{INT32_MAX, INT32_MIN, 0}, 1, 1},                      // exact boundaries that fit
        {{1073741824, 0, 0}, 2, 1},                             // 2^31: over by one
        {{0, -715827883, 0}, 3, 1},                             // -2^31 - 1: under by one
        {{INT32_MIN, 0, 0}, 1, -1},                             // INT32_MIN / -1
        {{0, 0, INT32_MIN}, -1, 1},                             // -INT32_MIN in component 2
        {{-6700417, 0, 0}, 641, 2},                             // -2^31 - 0.5 truncates to INT32_MIN: fits
        {{6700417, 0, 0}, 641, 2},                              // 2^31 + 0.5 truncates to 2^31: over
        {{65535, 0, 0}, 65537, 2},                              // 2^31 - 0.5 truncates to INT32_MAX: fits
        {{INT32_MIN, INT32_MAX, 1}, INT32_MIN, INT32_MIN},      // c = INT32_MIN, all fit
        {{INT32_MAX, INT32_MIN, INT32_MIN}, INT32_MAX, 1},      // 2^62-scale products: three clamps
        {{INT32_MAX, INT32_MIN, 12345}, INT32_MAX, INT32_MAX},  // large products that fit
        {{-1366700, 692544, -4460464}, 1000, 2},                // the dump vector negated
    };
    Run original[sizeof fixed / sizeof fixed[0]];
    for (unsigned i = 0; i < sizeof fixed / sizeof fixed[0]; ++i) {
        original[i] = run(helper, fixed[i]);
        check(original[i].cpu_ok, "original: CPU state");
        check(original[i].trapped == !fits(fixed[i]), "original traps exactly when a quotient does not fit");
        if (!original[i].trapped) check(matches_reference(original[i], fixed[i]), "reference model = original");
    }
    // Every sign combination, inexact quotients (truncation toward zero), through the original and the model.
    Case signs[8];
    Run signs_original[8];
    for (unsigned s = 0; s < 8; ++s) {
        const int32_t sa = (s & 1) ? -1 : 1, sb = (s & 2) ? -1 : 1, sc = (s & 4) ? -1 : 1;
        signs[s] = Case{{sa * 7, sa * 1000001, sa * INT32_MAX}, sb * 3, sc * 4};
        signs_original[s] = run(helper, signs[s]);
        check(signs_original[s].cpu_ok && matches_reference(signs_original[s], signs[s]), "signs: original = model");
    }
    // Aliased out/a/b/c: the original's read/write order.
    const Case alias_case{{1000, -2000, 3000}, 7, 3};
    Run alias_original[alias_modes];
    for (int m = 0; m < alias_modes; ++m) {
        alias_original[m] = run(helper, alias_case, Alias(m));
        check(alias_original[m].cpu_ok && !alias_original[m].trapped, "aliased buffers: original");
    }
    // Random sweep through the original (fitting cases only; recorded for the replacement below).
    constexpr unsigned sweep = 20000;
    static Case sweep_cases[sweep];
    static Run sweep_original[sweep];
    unsigned sweep_fit = 0, sweep_c0 = 0;
    for (unsigned i = 0; i < sweep; ++i) {
        Case k{{random_value(), random_value(), random_value()}, random_value(), random_value()};
        if (next() % 8 == 0) k.c = int32_t(next() % 7) - 3;
        sweep_cases[i] = k;
        if (!k.c) ++sweep_c0;
        if (fits(k)) {
            ++sweep_fit;
            sweep_original[i] = run(helper, k);
            if (!sweep_original[i].cpu_ok || !matches_reference(sweep_original[i], k)) check(false, "sweep: original = model");
        }
    }

    // 4. Install through the production gate and engine_patch claim.
    unsigned char before[16];
    std::memcpy(before, image + layout::helper_rva, 16);
    check(sf::fixture_install(base) && sf::active() && !std::strcmp(sf::status(), "active"), "installed");
    check(row_has("fl.scale_fix status=active ") && row_has(" entry_ok=1 body_ok=1 write=atomic"), "status row");
    check(image[layout::helper_rva] == 0xe9 && !std::memcmp(image + layout::helper_rva + 5, before + 5, 11) &&
              protection_at(helper) == PAGE_EXECUTE_READ,
          "entry JMP written, the rest unchanged, protection kept");
    check(sf::fixture_install(base) == false && !std::strcmp(sf::status(), "already_installed"), "second install refused");
    const unsigned long calls0 = sf::calls();
    // 5. The replacement through the patched entry.
    {
        const unsigned rows_before = rows;
        sf::fixture_force_sample();
        const Run r = run(helper, Case{{1, 2, 3}, 4, 5});
        check(r.cpu_ok && rows == rows_before + 1 && row_has("fl.scale_fix_sample calls=") &&
                  row_has(" saturated=0 last_site=0x00000"),
              "first due period after install: one row with saturated=0");
        sf::fixture_force_sample();
        run(helper, Case{{1, 2, 3}, 4, 5});
        check(rows == rows_before + 1, "later periods without a saturation write no row");
    }
    {
        const Run r = run(caller, dump); // through the fake caller at RVA 0x22dd4
        std::printf("REPLACEMENT_DUMP out=%ld,%ld,%ld ecx=%lu edx=%lu saturated=%lu\n", long(r.out[0]), long(r.out[1]),
                    long(r.out[2]), static_cast<unsigned long>(r.ecx), static_cast<unsigned long>(r.edx), sf::saturated());
        check(r.cpu_ok && !r.trapped && r.out[0] == 683350000 && r.out[1] == -346272000 && r.out[2] == INT32_MAX &&
                  r.out[3] == out_sentinel && r.eax == 0,
              "dump vector: z clamped to 2147483647, x/y exact");
        check(r.ecx == 2 && r.edx == 0 && sf::saturated() == 1, "dump vector: one saturation, ECX = c");
    }
    {
        const unsigned rows_before = rows;
        sf::fixture_force_sample();
        const Run r = run(helper, Case{{1, 2, 3}, 4, 5}); // the row is written inside this call: CPU state witnessed
        check(r.cpu_ok && rows == rows_before + 1, "one sample row after a saturation, CPU state kept");
        check(row_has("fl.scale_fix_sample calls=") && row_has(" saturated=1 last_site=0x22dd4"), "sample row fields");
        sf::fixture_force_sample();
        run(helper, Case{{1, 2, 3}, 4, 5});
        check(rows == rows_before + 1, "no row while the saturation count is unchanged");
    }
    unsigned long expected_saturations = 1;
    for (unsigned i = 0; i < sizeof fixed / sizeof fixed[0]; ++i) {
        const Run r = run(helper, fixed[i]);
        bool clamped = false;
        unsigned n = 0;
        for (int j = 0; j < 3; ++j) {
            bool one = false;
            reference(fixed[i].a[j], fixed[i].b, fixed[i].c, &one);
            n += one;
            clamped |= one;
        }
        expected_saturations += n;
        check(r.cpu_ok && matches_reference(r, fixed[i]), "fixed case: replacement = model");
        if (!original[i].trapped) check(same(r, original[i]), "fixed case: replacement = original (out, EAX, ECX, EDX)");
        if (clamped && fixed[i].c) check(r.ecx == uint32_t(fixed[i].c), "clamped: ECX = c");
    }
    check(sf::saturated() == expected_saturations, "saturations counted per component");
    for (unsigned s = 0; s < 8; ++s) {
        const Run r = run(helper, signs[s]);
        check(r.cpu_ok && same(r, signs_original[s]), "signs: replacement = original");
    }
    for (int m = 0; m < alias_modes; ++m) {
        const Run r = run(helper, alias_case, Alias(m));
        check(r.cpu_ok && same(r, alias_original[m]), "aliased buffers: replacement = original");
    }
    unsigned sweep_bad = 0;
    for (unsigned i = 0; i < sweep; ++i) {
        const Run r = run(helper, sweep_cases[i]);
        bool ok = r.cpu_ok && matches_reference(r, sweep_cases[i]);
        if (fits(sweep_cases[i])) ok = ok && same(r, sweep_original[i]);
        if (!ok) ++sweep_bad;
    }
    std::printf("SWEEP cases=%u fit=%u c0=%u clamped=%u mismatches=%u\n", sweep, sweep_fit, sweep_c0, sweep - sweep_fit,
                sweep_bad);
    check(sweep_bad == 0 && sweep_fit > sweep / 4 && sweep - sweep_fit > sweep / 8, "sweep: replacement = model/original");
    // The claim's tail (displaced prologue + jump back) is the original helper, still callable.
    {
        const Run r = run(reinterpret_cast<uintptr_t>(sf::fixture_original()), signs[5]);
        check(r.cpu_ok && same(r, signs_original[5]), "tail runs the original");
    }
    check(sf::calls() > calls0, "calls counted");
    // Cost per call with the engine's argument setup (fits; no saturation): original tail vs replacement.
    {
        LARGE_INTEGER frequency{}, t0{}, t1{}, t2{};
        QueryPerformanceFrequency(&frequency);
        static int32_t a[4] = {1366700, -692544, 446046, 0}, b = 1000, c = 2, out[4];
        const uint32_t n = 2000000;
        QueryPerformanceCounter(&t0);
        bench(reinterpret_cast<uintptr_t>(sf::fixture_original()), a, &b, out, &c, n);
        QueryPerformanceCounter(&t1);
        bench(helper, a, &b, out, &c, n);
        QueryPerformanceCounter(&t2);
        const double scale = 1e9 / double(frequency.QuadPart) / n;
        std::printf("TIMING original_ns=%.2f replacement_ns=%.2f\n", double(t1.QuadPart - t0.QuadPart) * scale,
                    double(t2.QuadPart - t1.QuadPart) * scale);
    }
    // 6. Restore; rows off on reinstall; restore again.
    check(sf::fixture_restore() && !std::memcmp(image + layout::helper_rva, before, 16) &&
              protection_at(helper) == PAGE_EXECUTE_READ,
          "exact restore");
    check(run(helper, dump).trapped, "restored: the original traps again");
    SetEnvironmentVariableW(L"X3M_FL_SCALE_OVERFLOW_LOG", L"0");
    check(sf::fixture_install(base), "reinstall with rows off");
    {
        const unsigned rows_before = rows;
        run(helper, dump);
        sf::fixture_force_sample();
        run(helper, dump);
        check(rows == rows_before && sf::saturated() == 2, "X3M_FL_SCALE_OVERFLOW_LOG=0: counted, no row");
    }
    SetEnvironmentVariableW(L"X3M_FL_SCALE_OVERFLOW_LOG", nullptr);
    check(sf::fixture_restore() && !std::memcmp(image + layout::helper_rva, before, 16), "restore 2");
    // 7. Late window: after the first Present closes it, the install is refused and writes nothing.
    x3m::engine_patch::close_install_window("fixture_first_present");
    check(!sf::fixture_install(base) && !std::strcmp(sf::status(), "late_claim") &&
              row_has("fl.scale_fix status=late_claim ") && !std::memcmp(image + layout::helper_rva, before, 16) &&
              !sf::active(),
          "late claim refused");
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
