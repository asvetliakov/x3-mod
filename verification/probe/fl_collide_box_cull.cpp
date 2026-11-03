// Native fixture of the FL collision box-cull (src/engine/fl/collide_box_cull_fl.cpp, collide_box_cull_sites_fl.h;
// docs/reverse-engineering/fl/collision-box.md, "Port (2026-11-03)"). It maps a private 64 KB-aligned copy of X3FL.exe
// with its relocations applied (fl_seta_image.h), so the two sites, the native distance, the sqrt helper and the
// continuations are the real relocated bytes a loaded X3FL.exe would run; synthetic objects and synthetic exit pads are
// the only non-engine parts. Built -DX3M_EDITION_FL=1 -DX3M_COLLIDE_BOX_CULL_FL_FIXTURE by
// verification/analysis/test_fl_collide_box_cull.py and run natively through WSL interop (no Wine). Not traversal,
// resource-lifetime or gameplay evidence.
//
// The harness fx_run sets ECX/EDX/ESI/EBP and a 128-byte local frame (the radius / sweep / count slots), loads MXCSR,
// an x87 state and XMM0-7, jumps to the site, and on return records the full CPU image the way the native continuation
// received it. Synthetic `push imm32; ret` pads at the continuations and the native keep/class exits record which way
// the site went (1 = reject to the continuation, 2 = native keep, 3 = P1 class-7 reject context). A disabled run
// (set_enabled false) runs the site purely native; an armed run runs it with the stub. For each pair the native verdict
// (dist > R) and the stub verdict are compared: a box reject must be a subset of the native rejects, and class 7 on
// either object must never be rejected by the box.
//
//   ./fl_collide_box_cull.exe <X3FL.exe>          the full run (map, install, claim/rollback/readback, CPU preservation,
//                                                 the differential over the randomised set, class-7 exemption, counters,
//                                                 shutdown, late_claim, a small per-pair timing)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "fl_seta_image.h"
#include "../../src/engine/fl/collide_box_cull_fl.h"
#include "../../src/engine/fl/collide_box_cull_sites_fl.h"
#include "../../src/proxy/engine_patch.h"

namespace box = x3m::collide_box_cull;
namespace F = x3m::fl::collide_box_cull_fl;
namespace ap = x3m::collide_box_cull::core;
namespace engine_patch = x3m::engine_patch;
using std::uint32_t;

static std::string census_window, census_frame;
namespace x3m {
void log(const char* format, ...) {
    char line[1024];
    std::va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (!std::strncmp(line, "collide_census_frame ", 21)) census_frame = line;
    else if (!std::strncmp(line, "collide_census ", 15)) census_window = line;
}
}
// FL fixtures: object_trace is AP-only and unused on FL, but engine_patch.cpp references it through nothing here; the FL
// modules call fl::module_base(), which the fixture overrides with fixture_base().
namespace x3m::fl {
std::uintptr_t module_base() {
    return 0;
} // never used: the fixture always sets collide_box_cull::fixture_base()
bool verified() {
    return false;
}
}

static unsigned checks = 0, failures = 0;
static void check(bool ok, const char* what, const char* detail = "") {
    ++checks;
    if (!ok) {
        ++failures;
        if (failures < 40) std::printf("FAIL %s %s\n", what, detail);
    }
}

// ---- the CPU-state harness (adapted from the exit/object authority technique) ----
extern "C" {
struct alignas(16) Frame {
    uint32_t a, b, entry, mxcsr;             // 0..15
    uint32_t esp_in, esp_out, code, regs[9]; // 16..63  regs: PUSHAD edi,esi,ebp,esp,ebx,edx,ecx,eax, then eflags
    uint32_t locals[32], after[32];          // 64..319
    alignas(16) unsigned char fp_in[512], fp_out[512], fp_host[512]; // 320,832,1344
};
Frame* fx_frame = nullptr;
uint32_t fx_saved_esp = 0, fx_code = 0;
void fx_run(Frame*);
void fx_reject();
void fx_keep();
void fx_class();
}
asm(R"(
.intel_syntax noprefix
.text
.globl _fx_run
_fx_run:
 push ebp
 push ebx
 push esi
 push edi
 mov eax,[esp+20]
 mov [_fx_frame],eax
 mov [_fx_saved_esp],esp
 fxsave [eax+1344]
 fninit
 fld1
 fldz
 ldmxcsr [eax+12]
 pxor xmm0,xmm0
 pxor xmm1,xmm1
 pxor xmm2,xmm2
 pxor xmm3,xmm3
 pxor xmm4,xmm4
 pxor xmm5,xmm5
 pxor xmm6,xmm6
 pxor xmm7,xmm7
 fxsave [eax+320]
 sub esp,128
 mov [eax+16],esp
 lea esi,[eax+64]
 mov edi,esp
 mov ecx,32
 cld
 rep movsd
 mov ebx,[eax]
 mov edi,[eax+4]
 mov ebp,0xbebebebe
 mov esi,0x5e5e5e5e
 mov ecx,0xcccccccc
 mov edx,0xdddddddd
 jmp dword ptr [eax+8]
.globl _fx_reject
_fx_reject:
 mov dword ptr [_fx_code],1
 jmp _fx_exit
.globl _fx_keep
_fx_keep:
 mov dword ptr [_fx_code],2
 jmp _fx_exit
.globl _fx_class
_fx_class:
 mov dword ptr [_fx_code],3
_fx_exit:
 pushfd
 pushad
 mov ebx,[_fx_frame]
 lea edi,[ebx+28]
 mov esi,esp
 mov ecx,9
 cld
 rep movsd
 add esp,36
 mov [ebx+20],esp
 mov eax,[_fx_code]
 mov [ebx+24],eax
 fxsave [ebx+832]
 mov esi,esp
 lea edi,[ebx+192]
 mov ecx,32
 rep movsd
 fxrstor [ebx+1344]
 mov esp,[_fx_saved_esp]
 pop edi
 pop esi
 pop ebx
 pop ebp
 ret
.att_syntax
)");

static unsigned char* image_base = nullptr;
static FlImage img;
static uint32_t address(const void* p) {
    return uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
static unsigned char* mem(uint32_t rva) {
    return image_base + rva;
}
static void jump(uint32_t rva, void (*target)()) {
    unsigned char* p = mem(rva);
    p[0] = 0xe9;
    const uint32_t rel = address(reinterpret_cast<const void*>(target)) - (address(p) + 5);
    std::memcpy(p + 1, &rel, 4);
}
// The synthetic exit pads: P1 reject/class continuations and both compare fall-throughs; P2 reject continuation and its
// keep fall-through. Native keep begins at the compare address (P1 0x0672af, P2 0x0668cb).
static void exits() {
    jump(0x067e7e, &fx_reject); // P1 reject (stub) / neither-class-7 native path
    jump(0x0673ae, &fx_class);  // P1 native distance rejected: the class-7 context
    jump(0x0672af, &fx_keep);   // P1 native kept
    jump(0x066a1b, &fx_reject); // P2 reject (stub) / native continuation
    jump(0x0668cb, &fx_keep);   // P2 native kept
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
}
// A precision-only exception during the native inexact distance is masked and resumed (the integer stub never faults):
// the native path may run with precision unmasked, which the proof allows; the stub's FP gate refuses to skip then.
static LONG CALLBACK handler(EXCEPTION_POINTERS* e) {
    uint32_t mxcsr = 0;
    std::memcpy(&mxcsr, e->ContextRecord->ExtendedRegisters + 24, 4);
    const uint32_t pending = (mxcsr & 0x3fu) & ~(mxcsr >> 7);
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    const bool precision = pending == 0x20 && (code == EXCEPTION_FLT_INEXACT_RESULT ||
                                               code == 0xC00002B4u /*MULTIPLE_FAULTS*/ || code == 0xC00002B5u);
    if (!precision) return EXCEPTION_CONTINUE_SEARCH;
    mxcsr |= 0x1000;
    std::memcpy(e->ContextRecord->ExtendedRegisters + 24, &mxcsr, 4);
    return EXCEPTION_CONTINUE_EXECUTION;
}

struct Object {
    unsigned char data[0xb0]{};
};
struct Physics {
    unsigned char data[0x40]{};
};
static Object obj_a, obj_b;
static Physics phys_a, phys_b;
static uint32_t sentinel = 0;
static void put(void* p, unsigned off, uint32_t v) {
    std::memcpy(static_cast<unsigned char*>(p) + off, &v, 4);
}

struct Pair {
    std::int32_t x, y, z, r, sweep;
    unsigned ca = 1, cb = 2;
};
static uint32_t csr_out(const Frame& f) {
    uint32_t v;
    std::memcpy(&v, f.fp_out + 24, 4);
    return v;
}
static Frame run(unsigned site, const Pair& p, uint32_t csr) {
    put(&obj_a, 0, address(&sentinel));
    put(&obj_b, 0, address(&sentinel));
    put(&obj_a, 0x70, address(&phys_a));
    put(&obj_b, 0x70, address(&phys_b));
    put(&obj_a, 0x48, p.ca);
    put(&obj_b, 0x48, p.cb);
    put(&obj_b, 0xa4, uint32_t(p.r));
    put(&phys_a, 0x30, 0);
    put(&phys_a, 0x34, 0);
    put(&phys_a, 0x38, 0);
    put(&phys_b, 0x30, uint32_t(p.x));
    put(&phys_b, 0x34, uint32_t(p.y));
    put(&phys_b, 0x38, uint32_t(p.z));
    Frame f{};
    // P1: EDI = A, EBX = B (the loop reads A+0x70 into ECX, B+0x70 into EDX). P2: EBX = candidate, EDI = reference.
    f.a = site == 1 ? address(&obj_b) : address(&obj_b); // EBX
    f.b = site == 1 ? address(&obj_a) : address(&obj_a); // EDI
    f.entry = address(mem(site == 1 ? F::p1_site_rva : F::p2_site_rva));
    f.mxcsr = csr;
    for (unsigned i = 0; i < 32; ++i) f.locals[i] = 0xabab0000 + i;
    f.locals[0x14 / 4] = 13;              // P2 count
    f.locals[0x34 / 4] = uint32_t(p.r);   // P1 radius sum slot
    f.locals[0x2c / 4] = uint32_t(p.sweep); // P2 sweep slot
    f.locals[0x44 / 4] = address(&sentinel);
    f.locals[0x24 / 4] = address(&obj_a);
    SetLastError(0xface1234);
    fx_run(&f);
    check(GetLastError() == 0xface1234, "pair preserves LastError");
    return f;
}
static bool fp_preserved(const Frame& f, bool skipped) {
    // full x87 environment + data (fxsave 0..160) and, on a skip, MXCSR and XMM0-7 (fxsave 160..288).
    const bool env = !std::memcmp(f.fp_in, f.fp_out, 160);
    const bool xmm = !std::memcmp(f.fp_in + 160, f.fp_out + 160, 128);
    return env && (!skipped || xmm);
}
// Full CPU-image equivalence of two runs: the verdict code, the ESP delta, the preserved GPRs (PUSHAD, skipping the
// stack-pointer slot 3 whose absolute value differs between calls) and EFLAGS, the live local slots, and the whole x87
// environment, MXCSR and XMM image. Dead volatile outputs the native overwrites are compared too (both ran native).
static bool frames_equal(const Frame& a, const Frame& b, unsigned site) {
    (void)site;
    bool same = a.code == b.code && (a.esp_out - a.esp_in) == (b.esp_out - b.esp_in);
    for (unsigned i = 0; i < 9; ++i)
        if (i != 3) same = same && a.regs[i] == b.regs[i];
    for (unsigned i = 0; i < 32; ++i) same = same && a.after[i] == b.after[i];
    same = same && !std::memcmp(a.fp_out, b.fp_out, 288);
    return same;
}

static std::vector<Pair> deterministic() {
    return {{1024, 0, 0, 0, 0},          {65537, 0, 0, 0, 0},    {10, 5, -3, 100, 5},
            {257, 0, 0, 0, 0},           {1024, 0, 0, 0, 0, 7, 1}, {1024, 0, 0, 0, 0, 1, 7},
            {0x40000000, 0, 0, 0, 0},    {INT32_MIN, 0, 0, 0, 0}, {1024, 0, 0, -1, 0},
            {1024, 0, 0, INT32_MAX, 1},  {1024, 0, 0, INT32_MAX - 128, 0},
            {1073741823, 0, 0, 1073741758, 0}, {0, 0, 0, -1000, 0}, {300, 0, 0, 200, 0}, {300, 0, 0, 20, 0}};
}
// A full-avalanche 32-bit generator (splitmix32 finaliser over a Weyl sequence): every draw below uses its high bits,
// so the coordinate scale, the radius scale and the class draws are independent (an LCG's low bits repeat every 8).
static std::uint32_t rng_state = 0x1234abcdu;
static std::uint32_t next32() {
    std::uint32_t z = (rng_state += 0x9e3779b9u);
    z = (z ^ (z >> 16)) * 0x85ebca6bu;
    z = (z ^ (z >> 13)) * 0xc2b2ae35u;
    return z ^ (z >> 16);
}
// Coordinates on four scales (1 Ki, 1 Mi, 2^29, just past the 2^30 cap), radii on four scales (2^8 .. 2^29, one in 16
// negative), sweep +-32 Ki, class 7 on each object with probability 1/8 (about 23% of pairs carry a 7).
static std::int32_t coord() {
    static const std::int64_t scale[4] = {1 << 10, 1 << 20, 1 << 29, (std::int64_t(1) << 30) + 4096};
    const std::int64_t m = scale[next32() >> 30];
    return std::int32_t(std::int64_t(next32() % std::uint32_t(2 * m)) - m);
}
static std::int32_t radius() {
    static const std::uint32_t scale[4] = {1u << 8, 1u << 16, 1u << 24, 1u << 29};
    const std::int32_t r = std::int32_t(next32() % scale[next32() >> 30]);
    return (next32() >> 28) == 0 ? -r : r;
}
static unsigned class_word() {
    return (next32() >> 29) == 0 ? 7u : 1u + (next32() >> 30);
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 2) {
        std::printf("FAIL arguments argc=%d\n", argc);
        return 2;
    }
    if (!map_fl_image(argv[1], img)) {
        std::printf("SKIP image\n");
        return 0;
    }
    image_base = img.base;
    const uint32_t base = address(img.base);
    std::printf("IMAGE size=%u relocated=%u\n", img.size, img.relocated);
    AddVectoredExceptionHandler(1, handler);
    box::fixture_base(base);
    SetEnvironmentVariableW(L"X3M_COLLIDE_BOX_CULL", L"1");

    // ---- install: claim both sites, read back, CPU preservation is per-pair below ----
    const unsigned arena_before = engine_patch::arena_used();
    SetLastError(0xaabbccdd);
    const bool installed = box::initialize();
    check(installed && !std::strcmp(box::state(), "ok"), "install ok", box::state());
    check(GetLastError() == 0xaabbccdd, "install preserves LastError");
    check(box::p1_stub_address() && box::p2_stub_address(), "both stubs emitted");
    // the five jump bytes are in place at both sites
    check(mem(F::p1_site_rva)[0] == 0xe9 && mem(F::p2_site_rva)[0] == 0xe9, "sites redirected");
    exits();

    // ---- the differential over the deterministic and randomised sets, under every MXCSR mode that matters ----
    // Admitting modes (precision masked and sticky): each rounding control, DAZ+FZ, all at once. Refused modes:
    // precision sticky clear (0x1f80) and precision unmasked (0x0fa0: the native inexact distance traps and the handler
    // resumes it masked, identically in the disarmed and the armed run).
    struct Mode {
        uint32_t csr;
        bool admits;
    };
    static const Mode modes[] = {{0x1fa0, true}, {0x3fa0, true}, {0x5fa0, true}, {0x7fa0, true},
                                 {0x9fe0, true}, {0xffe0, true}, {0x1f80, false}, {0x0fa0, false}};
    const uint32_t gate_csr = 0x1fa0; // the census and timing below use the admitting nearest mode
    std::vector<Pair> set = deterministic();
    for (unsigned i = 0; i < 4000; ++i) {
        Pair p{coord(), coord(), coord(), radius(), std::int32_t(next32() % 0x10000u) - 0x8000};
        p.ca = class_word();
        p.cb = class_word();
        set.push_back(p);
    }
    unsigned total_over = 0, total_culled[2]{}, total_class7_culled = 0, refused_mode_culls = 0;
    for (const Mode& mode : modes) {
        for (unsigned site = 1; site <= 2; ++site) {
            unsigned pairs = 0, native_rej = 0, culled = 0, over = 0, c7_pairs = 0, c7_culled = 0;
            for (const Pair& p : set) {
                ++pairs;
                const std::int32_t dx = ap::wrapped_difference(p.x, 0), dy = ap::wrapped_difference(p.y, 0),
                                   dz = ap::wrapped_difference(p.z, 0);
                const bool stub_reject = site == 1 ? F::p1_box_reject(p.ca, p.cb, dx, dy, dz, p.r, mode.csr)
                                                   : F::p2_box_reject(dx, dy, dz, p.r, p.sweep, mode.csr);
                // disarmed first: the site runs purely native; its verdict (P1: class context 3 = reject, keep 2;
                // P2: reject 1, keep 2) is the ground truth, its CPU image what a kept armed run must match.
                box::set_enabled(false);
                const Frame bare = run(site, p, mode.csr);
                const bool native_hw_reject = site == 1 ? bare.code == 3 : bare.code == 1;
                box::set_enabled(true);
                uint32_t discard[4];
                box::take_counters(discard);
                const Frame armed = run(site, p, mode.csr);
                uint32_t counters[4];
                box::take_counters(counters);
                const bool hw_reject = counters[(site - 1) * 2 + 1] != 0;
                check(hw_reject == stub_reject, "hardware verdict matches the stub model");
                if (hw_reject) {
                    check(fp_preserved(armed, true), "skip preserves x87/MXCSR/XMM");
                    check(armed.code == 1 && csr_out(armed) == mode.csr, "reject reaches the continuation, MXCSR intact");
                } else {
                    check(frames_equal(armed, bare, site), "kept run equals the disarmed native run");
                }
                native_rej += native_hw_reject;
                culled += hw_reject;
                over += hw_reject && !native_hw_reject; // a cull of a pair the native distance keeps
                if (mode.csr == 0x1fa0) {
                    const bool model = site == 1 ? F::p1_engine_reject(dx, dy, dz, p.r)
                                                 : F::p2_engine_reject(dx, dy, dz, p.r, p.sweep);
                    check(native_hw_reject == model, "native hardware verdict matches the host model (nearest)");
                }
                if (site == 1 && (p.ca == 7 || p.cb == 7)) {
                    ++c7_pairs;
                    c7_culled += hw_reject;
                }
            }
            check(over == 0, "box cull subset of the native reject (no over-cull)");
            if (!mode.admits) refused_mode_culls += culled;
            total_over += over;
            total_culled[site - 1] += culled;
            if (site == 1) total_class7_culled += c7_culled;
            std::printf("DIFFERENTIAL site=%u mxcsr=%04x pairs=%u native_rejected=%u box_culled=%u over_culled=%u",
                        site, mode.csr, pairs, native_rej, culled, over);
            if (site == 1) std::printf(" class7_pairs=%u class7_culled=%u", c7_pairs, c7_culled);
            std::printf("\n");
        }
    }
    check(total_class7_culled == 0, "class 7 on either object is never culled at P1");
    check(refused_mode_culls == 0, "a refused FP gate (sticky clear or precision unmasked) never culls");
    check(total_culled[0] > 100 && total_culled[1] > 100, "both sites cull a meaningful share");
    std::printf("DIFFERENTIAL_TOTAL modes=%u over_culled=%u p1_culled=%u p2_culled=%u class7_culled=%u "
                "refused_mode_culls=%u\n",
                unsigned(sizeof modes / sizeof modes[0]), total_over, total_culled[0], total_culled[1],
                total_class7_culled, refused_mode_culls);

    // ---- counters and the census rows (telemetry on) ----
    SetEnvironmentVariableW(L"X3M_TELEMETRY", L"1");
    const Pair hit{65537, 0, 0, 0, 0};
    for (unsigned frame = 1; frame <= 300; ++frame) {
        run(1, hit, gate_csr);
        run(2, hit, gate_csr);
        SetLastError(0xface1234);
        box::present(7, frame, frame == 300);
        check(GetLastError() == 0xface1234, "present preserves LastError");
    }
    check(census_frame.find("edition=fl") != std::string::npos && census_frame.find("p1_rejected=1 ") != std::string::npos,
          "capture census frame row");

    // ---- a small per-pair timing (performance pass; diagnostic, not game FPS) ----
    // Every fx_run carries the harness's FXSAVE/FXRSTOR and 128-byte frame copies; `harness` enters a pad directly (no
    // engine, no stub), so the stub's own cost is the difference to the disarmed (native) run of the same pair.
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    const auto time_ns = [&](Frame& f) {
        for (unsigned i = 0; i < 2000; ++i) fx_run(&f);
        QueryPerformanceCounter(&t0);
        for (unsigned i = 0; i < 200000; ++i) fx_run(&f);
        QueryPerformanceCounter(&t1);
        return double(t1.QuadPart - t0.QuadPart) * 1e9 / double(freq.QuadPart) / 200000;
    };
    const Pair bench_reject{65537, 0, 0, 0, 0}, bench_keep{5, 3, 2, 100, 0};
    Frame fh = run(1, bench_keep, gate_csr);
    fh.entry = address(reinterpret_cast<const void*>(&fx_keep));
    const double harness_ns = time_ns(fh);
    box::set_enabled(false);
    Frame fn = run(1, bench_reject, gate_csr), fkn = run(1, bench_keep, gate_csr);
    const double native_reject_ns = time_ns(fn), native_keep_ns = time_ns(fkn);
    box::set_enabled(true);
    Frame fr = run(1, bench_reject, gate_csr), fk = run(1, bench_keep, gate_csr);
    const double reject_ns = time_ns(fr), keep_ns = time_ns(fk);
    std::printf("BENCH harness_ns=%.3f native_reject_ns=%.3f stub_reject_ns=%.3f native_keep_ns=%.3f "
                "stub_keep_ns=%.3f reject_saving_ns=%.3f keep_overhead_ns=%.3f\n",
                harness_ns, native_reject_ns, reject_ns, native_keep_ns, keep_ns, native_reject_ns - reject_ns,
                keep_ns - native_keep_ns);

    // ---- shutdown restores both sites, then late_claim ----
    SetLastError(0x5a5a5a5a);
    check(box::shutdown() && !std::strcmp(box::state(), "restored"), "shutdown restores", box::state());
    check(GetLastError() == 0x5a5a5a5a, "shutdown preserves LastError");
    check(!std::memcmp(mem(F::p1_site_rva), F::p1_site, 6) && !std::memcmp(mem(F::p2_site_rva), F::p2_site, 6),
          "sites back to the engine's bytes");
    (void)arena_before;
    engine_patch::close_install_window("fixture_late");
    check(!box::install_base(base, true) && !std::strcmp(box::state(), "late_claim"), "late_claim after the window",
          box::state());

    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
