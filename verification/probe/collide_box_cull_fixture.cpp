// X3 CPU fixture of the sector-collide bounding-box early-out: layout-preserving
// synthetic copies of the two square-root pair tests (P1 0x0045d58e of
// 0x0045d250, P2 0x0045cc7c of 0x0045cab0) whose site, compare, reject and
// continue windows are byte-exact at the engine's relative offsets (the two
// helpers 0x00412440 / 0x0052b5d0 are local replicas, so the four call rel32s
// are the only bytes that differ), drivers that enter each site with the
// engine's frame shape, the pair in the engine's registers and sentinels in
// the dead ones, and the production module (src/proxy/collide_box_cull.cpp,
// compiled separately with its production flags) patching that copy.
// Checks: for every synthetic pair the patched sites reach the same exit
// (continue / survivor / class-7) as the unpatched bytes with the same
// EBX/ESI/EDI/EBP/ESP, the same x87 stack (depth and values) and the same
// locals except the distance scratch the engine's own reject path also leaves
// dead; the box counters equal the host model pair by pair; disarmed frames
// count nothing and reject nothing; LastError preserved; exact rollback;
// option off untouched; changed window bytes and a null site refused; closed
// window refused; bench per 1,000 pairs native / disarmed / armed with and
// without the counters. Diagnostic timings only; not game FPS. Never launches
// the game.
#include "../../src/proxy/collide_box_cull.h"
#include "../../src/proxy/collide_box_cull_core.h"
#include "../../src/proxy/engine_patch.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cstdarg>
#include <string>
#include <vector>
static std::vector<std::string> census_lines, frame_lines;
namespace x3m {
void log(const char* format, ...) {
    char text[512];
    std::va_list a;
    va_start(a, format);
    std::vsnprintf(text, sizeof text, format, a);
    va_end(a);
    if (!std::strncmp(text, "collide_census_frame ", 21)) {
        frame_lines.emplace_back(text);
        return;
    }
    if (!std::strncmp(text, "collide_census ", 15)) {
        census_lines.emplace_back(text);
        return;
    }
    static unsigned lines = 0;
    if (lines++ < 8) std::printf("%s\n", text);
}
}
namespace x3m::object_trace {
bool executable_verified() {
    return true;
}
}
namespace cull = x3m::collide_box_cull;
namespace core = x3m::collide_box_cull::core;

// ---- the synthetic regions ----
// Every byte inside a verified window is the engine's; the exits are fixture
// code at the engine's own branch targets (survivor 0x0045d60c / 0x0045ccf8,
// class-7 0x0045d6e4, continue 0x0045df90 / 0x0045ce07). The layout keeps the
// engine's relative distances so the rel32 of `jg` and `jne` inside the
// windows are byte-exact. Exit codes: 1 continue, 2 survivor, 3 class-7.
extern "C" unsigned char synthetic_p1[], synthetic_p2[];
extern "C" std::uint32_t fx_exit_code, fx_frame, fx_saved_esp;
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _synthetic_sqrt
_synthetic_sqrt:
    .byte 0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0xd9, 0x45, 0x08, 0xd9, 0xfa, 0x8b, 0xe5, 0x5d, 0xc3
    .p2align 4
    .globl _synthetic_ftol
_synthetic_ftol:
    .byte 0x55, 0x8b, 0xec, 0x83, 0xec, 0x08, 0x83, 0xe4, 0xf8, 0xdd, 0x1c, 0x24, 0xf2, 0x0f, 0x2c, 0x04, 0x24, 0xc9, 0xc3
    .p2align 4
    .globl _synthetic_p1
_synthetic_p1:
    .byte 0x8b,0x4b,0x70, 0x8b,0x51,0x30, 0x8b,0x46,0x70, 0x2b,0x50,0x30, 0x83,0xc1,0x30, 0x83,0xc0,0x30, 0x89,0x54,0x24,0x28, 0xdb,0x44,0x24,0x28
    .byte 0x8b,0x51,0x04, 0x2b,0x50,0x04, 0x8b,0x49,0x08, 0x2b,0x48,0x08, 0x89,0x54,0x24,0x28, 0xdb,0x44,0x24,0x28, 0x89,0x4c,0x24,0x28, 0xdb,0x44,0x24,0x28
    .byte 0xd9,0xc2, 0x51, 0xde,0xcb, 0xd9,0xc1, 0xde,0xca, 0xd9,0xca, 0xde,0xc1, 0xd9,0xc1, 0xde,0xca, 0xde,0xc1, 0xd9,0x1c,0x24
    .byte 0xe8
    .long _synthetic_sqrt - (. + 4)
    .byte 0x83, 0xc4, 0x04
    .byte 0xe8
    .long _synthetic_ftol - (. + 4)
    .byte 0x8b,0xc8, 0x89,0x4c,0x24,0x1c, 0x8b,0x44,0x24,0x24, 0xba,0x8f,0x02,0x01,0x00, 0xf7,0xea, 0x05,0x00,0x80,0x00,0x00
    .byte 0x83,0xd2,0x00, 0x0f,0xac,0xd0,0x10, 0x3b,0xc8, 0x0f,0x8f,0xc0,0x00,0x00,0x00
    .byte 0xc7, 0x05
    .long _fx_exit_code
    .long 2
    .byte 0xe9
    .long _fx_exit_common - (. + 4)
    .skip 0x13e - (. - _synthetic_p1), 0xcc
    .byte 0x0f,0xb7,0x43,0x48, 0xbf,0x07,0x00,0x00,0x00, 0x66,0x3b,0xc7, 0x74,0x0a, 0x66,0x39,0x7e,0x48, 0x0f,0x85,0xac,0x08,0x00,0x00
    .byte 0xc7, 0x05
    .long _fx_exit_code
    .long 3
    .byte 0xe9
    .long _fx_exit_common - (. + 4)
    .skip 0xa02 - (. - _synthetic_p1), 0xcc
    .byte 0x8b,0x74,0x24,0x3c, 0x83,0x3e,0x00, 0x89,0x74,0x24,0x14
    .byte 0xc7, 0x05
    .long _fx_exit_code
    .long 1
    .byte 0xe9
    .long _fx_exit_common - (. + 4)
    .p2align 4
    .globl _synthetic_p2
_synthetic_p2:
    .byte 0x8b,0x47,0x70, 0x8b,0x48,0x30, 0x8b,0x73,0x70, 0x2b,0x4e,0x30, 0x8b,0x50,0x34, 0x2b,0x56,0x34, 0x83,0xc0,0x30, 0x8b,0x40,0x08
    .byte 0x2b,0x46,0x38, 0x89,0x4c,0x24,0x30, 0xdb,0x44,0x24,0x30
    .byte 0x89,0x54,0x24,0x34, 0xdb,0x44,0x24,0x34, 0x89,0x44,0x24,0x38, 0xdb,0x44,0x24,0x38, 0xd9,0xc2, 0x89,0x54,0x24,0x64, 0x8b,0x54,0x24,0x3c
    .byte 0xde,0xcb, 0xd9,0xc1, 0x51, 0xde,0xca, 0x89,0x4c,0x24,0x64, 0xd9,0xca, 0x89,0x44,0x24,0x6c, 0x89,0x54,0x24,0x70, 0xde,0xc1, 0xd9,0xc1
    .byte 0xde,0xca, 0xde,0xc1, 0xd9,0x1c,0x24
    .byte 0xe8
    .long _synthetic_sqrt - (. + 4)
    .byte 0x83, 0xc4, 0x04
    .byte 0xe8
    .long _synthetic_ftol - (. + 4)
    .byte 0x8b,0x8f,0xa4,0x00,0x00,0x00, 0x03,0x4c,0x24,0x20, 0x3b,0xc1, 0x0f,0x8f,0x0f,0x01,0x00,0x00
    .byte 0xc7, 0x05
    .long _fx_exit_code
    .long 2
    .byte 0xe9
    .long _fx_exit_common - (. + 4)
    .skip 0x18b - (. - _synthetic_p2), 0xcc
    .byte 0x8b,0x3f, 0x83,0x3f,0x00, 0x89,0x7c,0x24,0x24
    .byte 0xc7, 0x05
    .long _fx_exit_code
    .long 1
    .byte 0xe9
    .long _fx_exit_common - (. + 4)
    .att_syntax
)");

// ---- harness: enters a site with the engine's frame shape and the pair in registers ----
extern "C" {
struct Frame {
    std::uint32_t a, b;            // 0, 4: P1 EBX/ESI (the pair); P2 EBX (swept object) / EDI (candidate)
    std::uint32_t site_esp;        // 8
    std::uint32_t exit_esp;        // 12
    std::uint32_t exit_code;       // 16
    std::uint32_t out[9];          // 20: PUSHAD order edi,esi,ebp,esp,ebx,edx,ecx,eax then EFLAGS, at the exit
    std::uint32_t x87env[7];       // 56: fnstenv at the exit (status: TOP; tag word)
    std::uint32_t locals_in[108];  // 84: the engine frame [ESP+0..0x1b0) at the site
    std::uint32_t locals_out[108]; // 516: the same at the exit
    std::uint32_t pad;             // 948
    double st[2];                  // 952: st(0), st(1) at the exit
    std::uint32_t entry_ebp;       // 968
};
std::uint32_t fx_exit_code = 0, fx_frame = 0, fx_saved_esp = 0;
void fx_p1_run(Frame* frame);
void fx_p2_run(Frame* frame);
}
static_assert(offsetof(Frame, out) == 20 && offsetof(Frame, x87env) == 56 && offsetof(Frame, locals_in) == 84 &&
                  offsetof(Frame, locals_out) == 516 && offsetof(Frame, st) == 952 && offsetof(Frame, entry_ebp) == 968,
              "frame layout");
// Engine frame: and esp,-16; sub esp,0x1a4 (0x0045cab0; 0x0045d250 uses 0x74,
// the slot offsets are the same); push ebx/esi/edi. Locals copied in from the
// Frame, the x87 stack loaded with two values (TOP = 6) so a depth change is
// visible, dead registers set to sentinels. The common exit records
// EFLAGS, PUSHAD, the x87 environment and st(0)/st(1), copies the locals out
// and unwinds through the saved C stack pointer.
asm(R"(
    .intel_syntax noprefix
    .text
    .globl _fx_p1_run
_fx_p1_run:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, dword ptr [ebp+8]
    mov dword ptr [_fx_frame], eax
    mov dword ptr [_fx_saved_esp], esp
    mov dword ptr [eax+968], ebp
    and esp, -16
    sub esp, 0x1a4
    push ebx
    push esi
    push edi
    mov dword ptr [eax+8], esp
    lea esi, [eax+84]
    mov edi, esp
    mov ecx, 108
    cld
    rep movsd
    fninit
    fld1
    fldz
    mov ebx, dword ptr [eax]
    mov esi, dword ptr [eax+4]
    mov edi, 0xd1d1d1d1
    mov ecx, 0xcccccccc
    mov edx, 0xdddddddd
    mov eax, 0xaaaaaaaa
    jmp _synthetic_p1
    .globl _fx_p2_run
_fx_p2_run:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, dword ptr [ebp+8]
    mov dword ptr [_fx_frame], eax
    mov dword ptr [_fx_saved_esp], esp
    mov dword ptr [eax+968], ebp
    and esp, -16
    sub esp, 0x1a4
    push ebx
    push esi
    push edi
    mov dword ptr [eax+8], esp
    lea esi, [eax+84]
    mov edi, esp
    mov ecx, 108
    cld
    rep movsd
    fninit
    fld1
    fldz
    mov ebx, dword ptr [eax]
    mov edi, dword ptr [eax+4]
    mov esi, 0x5e5e5e5e
    mov ecx, 0xcccccccc
    mov edx, 0xdddddddd
    mov eax, 0xaaaaaaaa
    jmp _synthetic_p2
    .globl _fx_exit_common
_fx_exit_common:
    pushfd
    pushad
    mov ebx, dword ptr [_fx_frame]
    fnstenv [ebx+56]
    mov esi, esp
    lea edi, [ebx+20]
    mov ecx, 9
    cld
    rep movsd
    add esp, 36
    mov dword ptr [ebx+12], esp
    mov esi, esp
    lea edi, [ebx+516]
    mov ecx, 108
    rep movsd
    mov eax, dword ptr [_fx_exit_code]
    mov dword ptr [ebx+16], eax
    fstp qword ptr [ebx+952]
    fstp qword ptr [ebx+960]
    fninit
    mov esp, dword ptr [_fx_saved_esp]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
    .att_syntax
)");

static unsigned checks = 0, failures = 0;
static void check(bool okay, const char* label) {
    ++checks;
    if (!okay) {
        ++failures;
        if (failures <= 40) std::printf("FAIL %s\n", label);
    }
}

// ---- synthetic objects ----
struct alignas(16) Object {
    unsigned char bytes[0x100];
};
struct alignas(16) Physics {
    unsigned char bytes[0x40];
};
alignas(16) static std::uint32_t sentinel[4] = {0, 0, 0, 0};
static std::uint32_t addr(const void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
static void put(void* base, unsigned off, std::uint32_t v) {
    std::memcpy(static_cast<unsigned char*>(base) + off, &v, 4);
}
static void put16(void* base, unsigned off, std::uint16_t v) {
    std::memcpy(static_cast<unsigned char*>(base) + off, &v, 2);
}
static Object A, B;
static Physics PA, PB;
static void set_object(Object& o, Physics& p, std::uint16_t cls, std::int32_t radius, std::int32_t x, std::int32_t y,
                       std::int32_t z) {
    std::memset(o.bytes, 0x5a, sizeof o.bytes);
    std::memset(p.bytes, 0x3c, sizeof p.bytes);
    put(o.bytes, 0, addr(sentinel));
    put16(o.bytes, core::class_offset, cls);
    put(o.bytes, core::physics_offset, addr(&p));
    put(o.bytes, core::radius_offset, std::uint32_t(radius));
    put(p.bytes, core::position_offset, std::uint32_t(x));
    put(p.bytes, core::position_offset + 4, std::uint32_t(y));
    put(p.bytes, core::position_offset + 8, std::uint32_t(z));
}
struct Pair {
    std::uint16_t class_a, class_b;
    std::int32_t ax, ay, az, bx, by, bz, radius_a, radius_b, sweep;
};
struct Result {
    std::uint32_t code, ebx, esi, edi, ebp, esp, eax, ecx, edx, flags, top, tag;
    double st0, st1;
    std::uint32_t locals[108];
};
static Frame frame_storage;
static Result run(bool p1, const Pair& pr) {
    set_object(A, PA, pr.class_a, pr.radius_a, pr.ax, pr.ay, pr.az);
    set_object(B, PB, pr.class_b, pr.radius_b, pr.bx, pr.by, pr.bz);
    Frame& f = frame_storage;
    std::memset(&f, 0, sizeof f);
    f.a = addr(&A);
    f.b = addr(&B);
    for (unsigned i = 0; i < 108; ++i) f.locals_in[i] = 0x10000000u + i; // sentinel pattern: a stray write shows
    if (p1) {
        f.locals_in[core::p1_radius_sum_slot / 4] = std::uint32_t(pr.radius_a) + std::uint32_t(pr.radius_b); // the
                                                                                                             // engine's
                                                                                                             // own
                                                                                                             // 32-bit
                                                                                                             // sum
        f.locals_in[0x3c / 4] = addr(sentinel); // next object: the continue label's list walk ends
        fx_p1_run(&f);
    } else {
        f.locals_in[core::p2_sweep_slot / 4] = std::uint32_t(pr.sweep);
        f.locals_in[0x24 / 4] = addr(&B);
        fx_p2_run(&f);
    }
    Result r{};
    r.code = f.exit_code;
    r.edi = f.out[0];
    r.esi = f.out[1];
    r.ebp = f.out[2];
    r.ebx = f.out[4];
    r.edx = f.out[5];
    r.ecx = f.out[6];
    r.eax = f.out[7];
    r.flags = f.out[8] & 0x8d5;
    r.esp = f.exit_esp;
    r.top = (f.x87env[1] >> 11) & 7;
    r.tag = f.x87env[2] & 0xffff;
    r.st0 = f.st[0];
    r.st1 = f.st[1];
    std::memcpy(r.locals, f.locals_out, sizeof r.locals);
    if (f.exit_esp != f.site_esp || f.entry_ebp != r.ebp) {
        std::printf("DETAIL esp=%08lx/%08lx ebp=%08lx/%08lx\n", (unsigned long)f.exit_esp, (unsigned long)f.site_esp,
                    (unsigned long)r.ebp, (unsigned long)f.entry_ebp);
    }
    r.esp = f.exit_esp == f.site_esp ? 1 : 0; // 1 = ESP back at the site value
    return r;
}
// Slots the engine's own reject path writes before the compare and never reads
// afterwards (liveness in sector-collide.md section 10): the stub's reject
// path leaves them untouched, so they are excluded when the pair was rejected.
static bool dead_slot_p1(unsigned slot) {
    return slot == 0x1c || slot == 0x28;
}
static bool dead_slot_p2(unsigned slot) {
    return slot == 0x30 || slot == 0x34 || slot == 0x38 || slot == 0x60 || slot == 0x64 || slot == 0x68 || slot == 0x6c;
}
static bool same(bool p1, const Result& n, const Result& p, bool rejected_by_stub, char* why) {
    if (n.code != p.code) {
        std::sprintf(why, "exit %lu vs %lu", (unsigned long)n.code, (unsigned long)p.code);
        return false;
    }
    // EDI is the P1 stub's scratch and is dead at the site (on every path out of 0x0045d58e the first touch of EDI
    // is a write: 0x0045d6d0 on both reject paths, a reload on every survivor path; sector-collide.md section 10).
    // It is compared wherever the engine itself defines it: the continue and class-7 exits (EDI = 7) and all of P2.
    const bool edi_defined = !p1 || n.code != 2;
    if (n.ebx != p.ebx || n.esi != p.esi || (edi_defined && n.edi != p.edi) || n.ebp != p.ebp || n.esp != 1 ||
        p.esp != 1) {
        std::sprintf(why, "callee-saved/esp");
        return false;
    }
    if (n.top != p.top || n.tag != p.tag || n.st0 != p.st0 || n.st1 != p.st1 || n.top != 6 || n.tag != 0x0fff ||
        n.st0 != 0.0 || n.st1 != 1.0) {
        std::sprintf(why, "x87 top=%lu/%lu tag=%04lx/%04lx", (unsigned long)n.top, (unsigned long)p.top,
                     (unsigned long)n.tag, (unsigned long)p.tag);
        return false;
    }
    for (unsigned i = 0; i < 108; ++i) {
        if (n.locals[i] == p.locals[i]) continue;
        if (rejected_by_stub && (p1 ? dead_slot_p1(i * 4) : dead_slot_p2(i * 4))) continue;
        std::sprintf(why, "local [esp+0x%x] %08lx vs %08lx", i * 4, (unsigned long)n.locals[i],
                     (unsigned long)p.locals[i]);
        return false;
    }
    if (!rejected_by_stub && (n.eax != p.eax || n.ecx != p.ecx || n.edx != p.edx || n.flags != p.flags)) {
        std::sprintf(why, "eax/ecx/edx/flags");
        return false;
    }
    return true;
}
static std::uint32_t counters_[4];
static void take() {
    cull::take_counters(counters_);
}
static std::uint32_t seed = 0x2545f491u;
static std::uint32_t rnd() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}
static std::int32_t rnd_signed(std::uint32_t magnitude) {
    const std::int32_t v = std::int32_t(magnitude);
    return (rnd() & 1) ? v : std::int32_t(0u - std::uint32_t(v));
}

static const unsigned char* site_p1() {
    return synthetic_p1;
}
static const unsigned char* site_p2() {
    return synthetic_p2;
}
static bool sites_original() {
    return !std::memcmp(site_p1(), core::p1_site, core::site_length) &&
           !std::memcmp(site_p2(), core::p2_site, core::site_length);
}
static bool windows_original() {
    return !std::memcmp(synthetic_p1, core::p1_site_window, core::p1_site_window_length) &&
           !std::memcmp(synthetic_p1 + core::p1_compare_offset, core::p1_compare_window,
                        core::p1_compare_window_length) &&
           !std::memcmp(synthetic_p1 + core::p1_reject_offset, core::p1_reject_window, core::p1_reject_window_length) &&
           !std::memcmp(synthetic_p1 + core::p1_continue_offset, core::p1_continue_window,
                        core::p1_continue_window_length) &&
           !std::memcmp(synthetic_p2, core::p2_site_window, core::p2_site_window_length) &&
           !std::memcmp(synthetic_p2 + core::p2_compare_offset, core::p2_compare_window,
                        core::p2_compare_window_length) &&
           !std::memcmp(synthetic_p2 + core::p2_continue_offset, core::p2_continue_window,
                        core::p2_continue_window_length);
}

// The pair sets: class combinations the P1 site sees (everything but 0, 4 and
// the class-20/0x5c dispatch, including 7 on either side), magnitudes on the
// boundary band of R and T, the cap, the wrap and random positions.
static std::vector<Pair> pairs_p1, pairs_p2;
static void build_pairs() {
    const std::uint16_t class_pairs[][2] = {{1, 1},  {1, 2},   {5, 6},   {7, 1},   {1, 7}, {7, 7},
                                            {8, 10}, {12, 17}, {18, 20}, {22, 28}, {2, 5}, {6, 6}};
    const std::int32_t radii[] = {0, 1, 63, 64, 1000, 250000, 1000000, 100000000, 0x7c000000, -1, 0x7fffffff};
    for (unsigned c = 0; c < sizeof class_pairs / sizeof class_pairs[0]; ++c) {
        for (std::int32_t r : radii) {
            const std::int32_t sum = std::int32_t(std::uint32_t(r) + std::uint32_t(r / 3));
            const std::int32_t R = core::p1_engine_threshold(sum);
            std::int32_t T = 0;
            const bool has_t = core::p1_threshold(sum, &T);
            std::vector<std::uint32_t> mags = {0,
                                               1,
                                               2,
                                               100,
                                               std::uint32_t(R) - 1,
                                               std::uint32_t(R),
                                               std::uint32_t(R) + 1,
                                               0x20000000u,
                                               0x3fffffffu,
                                               0x40000000u,
                                               0x40000001u,
                                               0x7fffffffu,
                                               0x80000000u};
            if (has_t) {
                mags.push_back(std::uint32_t(T) - 1);
                mags.push_back(std::uint32_t(T));
                mags.push_back(std::uint32_t(T) + 1);
                mags.push_back(std::uint32_t(T) + 40);
                mags.push_back(std::uint32_t(T) + 1000);
            }
            const bool full = c < 6;
            const unsigned samples = full ? 0 : 400;
            if (full) {
                for (std::uint32_t mx : mags)
                    for (std::uint32_t my : mags)
                        for (std::uint32_t mz : mags) {
                            Pair p{};
                            p.class_a = class_pairs[c][0];
                            p.class_b = class_pairs[c][1];
                            p.radius_a = r;
                            p.radius_b = r / 3;
                            p.ax = rnd_signed(mx);
                            p.ay = rnd_signed(my);
                            p.az = rnd_signed(mz);
                            p.bx = 0;
                            p.by = 0;
                            p.bz = 0;
                            // Shift both by a random offset so the positions, not the differences, are arbitrary.
                            const std::int32_t ox = std::int32_t(rnd()), oy = std::int32_t(rnd()),
                                               oz = std::int32_t(rnd());
                            p.ax = std::int32_t(std::uint32_t(p.ax) + std::uint32_t(ox));
                            p.bx = ox;
                            p.ay = std::int32_t(std::uint32_t(p.ay) + std::uint32_t(oy));
                            p.by = oy;
                            p.az = std::int32_t(std::uint32_t(p.az) + std::uint32_t(oz));
                            p.bz = oz;
                            pairs_p1.push_back(p);
                        }
            } else
                for (unsigned s = 0; s < samples; ++s) {
                    Pair p{};
                    p.class_a = class_pairs[c][0];
                    p.class_b = class_pairs[c][1];
                    p.radius_a = r;
                    p.radius_b = r / 3;
                    p.ax = rnd_signed(mags[rnd() % mags.size()]);
                    p.ay = rnd_signed(mags[rnd() % mags.size()]);
                    p.az = rnd_signed(mags[rnd() % mags.size()]);
                    pairs_p1.push_back(p);
                }
        }
    }
    for (unsigned s = 0; s < 100000; ++s) { // random positions across the whole int32 range and small radii
        Pair p{};
        p.class_a = std::uint16_t(1 + rnd() % 30);
        p.class_b = std::uint16_t(1 + rnd() % 30);
        p.radius_a = std::int32_t(rnd() % 2000000);
        p.radius_b = std::int32_t(rnd() % 2000000);
        const std::uint32_t span = (s & 1) ? 0xffffffffu : (rnd() & 3 ? 0x1fffffffu : 0x7fffffffu);
        p.ax = std::int32_t(rnd() & span);
        p.ay = std::int32_t(rnd() & span);
        p.az = std::int32_t(rnd() & span);
        p.bx = std::int32_t(rnd() & span);
        p.by = std::int32_t(rnd() & span);
        p.bz = std::int32_t(rnd() & span);
        pairs_p1.push_back(p);
    }
    const std::int32_t cand_radii[] = {0, 1, 500, 250000, 100000000, -5, 0x7fffffff};
    const std::int32_t sweeps[] = {0, 1, 64, 100000, 0x7ffffff0, -100};
    for (std::int32_t r : cand_radii)
        for (std::int32_t sw : sweeps) {
            const std::int32_t R = core::wrapped_difference(r, core::wrapped_difference(0, sw));
            std::int32_t T = 0;
            const bool has_t = core::p2_threshold(r, sw, &T);
            std::vector<std::uint32_t> mags = {
                0,           1,           100,         std::uint32_t(R) - 1, std::uint32_t(R), std::uint32_t(R) + 1,
                0x3fffffffu, 0x40000000u, 0x7fffffffu, 0x80000000u};
            if (has_t) {
                mags.push_back(std::uint32_t(T) - 1);
                mags.push_back(std::uint32_t(T));
                mags.push_back(std::uint32_t(T) + 1);
                mags.push_back(std::uint32_t(T) + 40);
            }
            for (std::uint32_t mx : mags)
                for (std::uint32_t my : mags)
                    for (std::uint32_t mz : mags) {
                        Pair p{};
                        p.class_a = 0;
                        p.class_b = std::uint16_t(rnd() % 30);
                        p.radius_b = r;
                        p.radius_a = 7;
                        p.sweep = sw;
                        const std::int32_t ox = std::int32_t(rnd()), oy = std::int32_t(rnd()), oz = std::int32_t(rnd());
                        p.bx = std::int32_t(std::uint32_t(rnd_signed(mx)) + std::uint32_t(ox));
                        p.ax = ox;
                        p.by = std::int32_t(std::uint32_t(rnd_signed(my)) + std::uint32_t(oy));
                        p.ay = oy;
                        p.bz = std::int32_t(std::uint32_t(rnd_signed(mz)) + std::uint32_t(oz));
                        p.az = oz;
                        pairs_p2.push_back(p);
                    }
        }
    for (unsigned s = 0; s < 50000; ++s) {
        Pair p{};
        p.class_a = 0;
        p.class_b = std::uint16_t(rnd() % 30);
        p.radius_b = std::int32_t(rnd() % 2000000);
        p.radius_a = 7;
        p.sweep = std::int32_t(rnd() % 4000000);
        const std::uint32_t span = (s & 1) ? 0xffffffffu : 0x1fffffffu;
        p.ax = std::int32_t(rnd() & span);
        p.ay = std::int32_t(rnd() & span);
        p.az = std::int32_t(rnd() & span);
        p.bx = std::int32_t(rnd() & span);
        p.by = std::int32_t(rnd() & span);
        p.bz = std::int32_t(rnd() & span);
        pairs_p2.push_back(p);
    }
}
static bool model_reject(bool p1, const Pair& p) {
    if (p1)
        return core::p1_box_reject(p.class_a, p.class_b, core::wrapped_difference(p.ax, p.bx),
                                   core::wrapped_difference(p.ay, p.by), core::wrapped_difference(p.az, p.bz),
                                   std::int32_t(std::uint32_t(p.radius_a) + std::uint32_t(p.radius_b)));
    // P2: the candidate is B (EDI), the swept object A (EBX): dx = B - A.
    return core::p2_box_reject(core::wrapped_difference(p.bx, p.ax), core::wrapped_difference(p.by, p.ay),
                               core::wrapped_difference(p.bz, p.az), p.radius_b, p.sweep);
}
// Native results, taken before the patch.
static std::vector<Result> native_p1, native_p2;

static double bench_ns(bool p1, const std::vector<Pair>& set, unsigned loops) {
    LARGE_INTEGER f{}, s{}, e{};
    QueryPerformanceFrequency(&f);
    for (unsigned i = 0; i < 2; ++i)
        for (const Pair& p : set) run(p1, p);
    QueryPerformanceCounter(&s);
    for (unsigned i = 0; i < loops; ++i)
        for (const Pair& p : set) run(p1, p);
    QueryPerformanceCounter(&e);
    return double(e.QuadPart - s.QuadPart) * 1e9 / double(f.QuadPart) / double(loops) / double(set.size());
}

int main() {
    DWORD old = 0;
    const std::uintptr_t page = reinterpret_cast<std::uintptr_t>(synthetic_p1) & ~std::uintptr_t(0xfff);
    check(VirtualProtect(reinterpret_cast<void*>(page), 0x3000, PAGE_EXECUTE_READWRITE, &old) != FALSE,
          "synthetic code writable");
    check(windows_original(), "synthetic regions carry the engine windows byte-exact at the engine's offsets");
    check(reinterpret_cast<std::uintptr_t>(synthetic_p2) - reinterpret_cast<std::uintptr_t>(synthetic_p1) >= 0xa10,
          "P2 region follows the P1 continue label");
    if (!windows_original()) {
        std::printf("COLLIDE BOX CULL CPU checks=%u failures=%u\n", checks, failures);
        return 1;
    }
    build_pairs();
    std::printf("pairs p1=%u p2=%u\n", unsigned(pairs_p1.size()), unsigned(pairs_p2.size()));

    // ---- native replay ----
    native_p1.reserve(pairs_p1.size());
    native_p2.reserve(pairs_p2.size());
    unsigned native_exit[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    for (const Pair& p : pairs_p1) {
        native_p1.push_back(run(true, p));
        ++native_exit[0][native_p1.back().code & 3];
    }
    for (const Pair& p : pairs_p2) {
        native_p2.push_back(run(false, p));
        ++native_exit[1][native_p2.back().code & 3];
    }
    check(native_exit[0][1] && native_exit[0][2] && native_exit[0][3] && native_exit[1][1] && native_exit[1][2],
          "native replay reaches every exit (continue, survivor, class-7)");
    std::printf("native exits p1 continue=%u survivor=%u class7=%u  p2 continue=%u survivor=%u\n", native_exit[0][1],
                native_exit[0][2], native_exit[0][3], native_exit[1][1], native_exit[1][2]);
    // The host model of the engine compare agrees with the engine's own bytes on every pair.
    unsigned model_mismatch = 0;
    for (unsigned i = 0; i < pairs_p1.size(); ++i) {
        const Pair& p = pairs_p1[i];
        const bool engine = core::p1_engine_reject(
            core::wrapped_difference(p.ax, p.bx), core::wrapped_difference(p.ay, p.by),
            core::wrapped_difference(p.az, p.bz), std::int32_t(std::uint32_t(p.radius_a) + std::uint32_t(p.radius_b)));
        const bool class7 = p.class_a == 7 || p.class_b == 7;
        const std::uint32_t expected = engine ? (class7 ? 3u : 1u) : 2u;
        if (native_p1[i].code != expected) ++model_mismatch;
    }
    for (unsigned i = 0; i < pairs_p2.size(); ++i) {
        const Pair& p = pairs_p2[i];
        const bool engine = core::p2_engine_reject(core::wrapped_difference(p.bx, p.ax),
                                                   core::wrapped_difference(p.by, p.ay),
                                                   core::wrapped_difference(p.bz, p.az), p.radius_b, p.sweep);
        if (native_p2[i].code != (engine ? 1u : 2u)) ++model_mismatch;
    }
    check(model_mismatch == 0, "host model of the engine compare matches the engine bytes on every pair");
    if (model_mismatch) std::printf("DETAIL model mismatches=%u\n", model_mismatch);

    // ---- option off / null site / changed bytes ----
    SetEnvironmentVariableW(L"X3M_COLLIDE_BOX_CULL", nullptr);
    check(!cull::initialize() && !std::strcmp(cull::state(), "disabled") && sites_original(),
          "variable unset: nothing patched");
    SetEnvironmentVariableW(L"X3M_COLLIDE_BOX_CULL", L"0");
    check(!cull::initialize() && !std::strcmp(cull::state(), "disabled") && sites_original(),
          "value 0: nothing patched");
    SetEnvironmentVariableW(L"X3M_COLLIDE_BOX_CULL", L"1");
    check(!cull::initialize() && sites_original() &&
              (!std::strcmp(cull::state(), "helper_mismatch") || !std::strcmp(cull::state(), "bytes_mismatch")),
          "value 1 without the engine image: helpers/sites absent, nothing patched");
    SetEnvironmentVariableW(L"X3M_COLLIDE_BOX_CULL", nullptr);
    check(!cull::install_at(0, addr(synthetic_p2), true) && !std::strcmp(cull::state(), "invalid_site") &&
              sites_original(),
          "null site refused");
    synthetic_p1[core::p1_compare_offset + 12] ^= 0x01; // 0x1028f -> 0x1028e
    check(!cull::install_at(addr(synthetic_p1), addr(synthetic_p2), true) &&
              !std::strcmp(cull::state(), "bytes_mismatch") && sites_original(),
          "changed P1 compare constant refused");
    synthetic_p1[core::p1_compare_offset + 12] ^= 0x01;
    synthetic_p2[core::p2_continue_offset + 1] ^= 0x01;
    check(!cull::install_at(addr(synthetic_p1), addr(synthetic_p2), true) &&
              !std::strcmp(cull::state(), "bytes_mismatch") && sites_original(),
          "changed P2 continue label refused");
    synthetic_p2[core::p2_continue_offset + 1] ^= 0x01;
    check(windows_original(), "windows restored after the refusal checks");

    // ---- install, armed, with counters ----
    SetLastError(0x1234);
    check(cull::install_at(addr(synthetic_p1), addr(synthetic_p2), true), "install on the synthetic regions");
    check(GetLastError() == 0x1234, "install preserves LastError");
    check(!std::strcmp(cull::state(), "ok") && cull::enabled() && cull::p1_stub_address() && cull::p2_stub_address(),
          "state ok, armed, both stubs live");
    check(synthetic_p1[0] == 0xe9 && synthetic_p2[0] == 0xe9, "both sites carry the patch jump");
    take();
    unsigned mismatches = 0, stub_rejects[2] = {0, 0}, counter_mismatch = 0;
    char why[128];
    for (unsigned i = 0; i < pairs_p1.size(); ++i) {
        const Result r = run(true, pairs_p1[i]);
        take();
        const bool model = model_reject(true, pairs_p1[i]);
        if (counters_[0] != 1 || counters_[1] != (model ? 1u : 0u) || counters_[2] || counters_[3]) ++counter_mismatch;
        if (model) ++stub_rejects[0];
        if (!same(true, native_p1[i], r, model, why)) {
            if (++mismatches <= 8)
                std::printf("DETAIL p1 pair %u: %s (classes %u/%u d=%ld,%ld,%ld r=%ld)\n", i, why, pairs_p1[i].class_a,
                            pairs_p1[i].class_b, (long)core::wrapped_difference(pairs_p1[i].ax, pairs_p1[i].bx),
                            (long)core::wrapped_difference(pairs_p1[i].ay, pairs_p1[i].by),
                            (long)core::wrapped_difference(pairs_p1[i].az, pairs_p1[i].bz),
                            (long)(std::uint32_t(pairs_p1[i].radius_a) + std::uint32_t(pairs_p1[i].radius_b)));
        }
        if (model && r.code != 1) {
            ++mismatches;
        }
    }
    check(mismatches == 0,
          "P1 patched: identical exit, callee-saved registers, x87 stack and live locals on every pair");
    check(counter_mismatch == 0,
          "P1 counters: entered once per pair, rejected exactly when the host box model rejects");
    check(stub_rejects[0] > 1000, "P1 box rejects a substantial share of the far pairs");
    mismatches = counter_mismatch = 0;
    for (unsigned i = 0; i < pairs_p2.size(); ++i) {
        const Result r = run(false, pairs_p2[i]);
        take();
        const bool model = model_reject(false, pairs_p2[i]);
        if (counters_[2] != 1 || counters_[3] != (model ? 1u : 0u) || counters_[0] || counters_[1]) ++counter_mismatch;
        if (model) ++stub_rejects[1];
        if (!same(false, native_p2[i], r, model, why)) {
            if (++mismatches <= 8) std::printf("DETAIL p2 pair %u: %s\n", i, why);
        }
        if (model && r.code != 1) ++mismatches;
    }
    check(mismatches == 0,
          "P2 patched: identical exit, callee-saved registers, x87 stack and live locals on every pair");
    check(counter_mismatch == 0,
          "P2 counters: entered once per candidate, rejected exactly when the host box model rejects");
    check(stub_rejects[1] > 1000, "P2 box rejects a substantial share of the far candidates");
    std::printf("stub rejects p1=%u/%u p2=%u/%u\n", stub_rejects[0], unsigned(pairs_p1.size()), stub_rejects[1],
                unsigned(pairs_p2.size()));
    // The rejected set is inside the engine's reject set: every stub reject reached the continue exit natively.
    unsigned outside = 0;
    for (unsigned i = 0; i < pairs_p1.size(); ++i)
        if (model_reject(true, pairs_p1[i]) && native_p1[i].code != 1) ++outside;
    for (unsigned i = 0; i < pairs_p2.size(); ++i)
        if (model_reject(false, pairs_p2[i]) && native_p2[i].code != 1) ++outside;
    check(outside == 0, "every box-rejected pair is one the engine's own compare sent to the continue label");
    // Class-7 pairs never counted as rejected.
    unsigned class7_rejected = 0;
    for (unsigned i = 0; i < pairs_p1.size(); ++i)
        if ((pairs_p1[i].class_a == 7 || pairs_p1[i].class_b == 7) && model_reject(true, pairs_p1[i]))
            ++class7_rejected;
    check(class7_rejected == 0, "class-7 pairs always take the engine path");

    // ---- disarmed: native behaviour, no counts ----
    cull::set_enabled(false);
    take();
    mismatches = 0;
    for (unsigned i = 0; i < pairs_p1.size(); i += 7) {
        const Result r = run(true, pairs_p1[i]);
        if (!same(true, native_p1[i], r, false, why)) ++mismatches;
    }
    for (unsigned i = 0; i < pairs_p2.size(); i += 7) {
        const Result r = run(false, pairs_p2[i]);
        if (!same(false, native_p2[i], r, false, why)) ++mismatches;
    }
    take();
    check(mismatches == 0 && !counters_[0] && !counters_[1] && !counters_[2] && !counters_[3],
          "disarmed: every pair identical to native including the distance scratch, nothing counted");
    cull::set_enabled(true);

    // ---- present(): frame and window lines, LastError ----
    take();
    census_lines.clear();
    frame_lines.clear();
    for (unsigned i = 0; i < 50; ++i) run(true, pairs_p1[i]);
    for (unsigned i = 0; i < 20; ++i) run(false, pairs_p2[i]);
    unsigned expect_r1 = 0, expect_r2 = 0;
    for (unsigned i = 0; i < 50; ++i) expect_r1 += model_reject(true, pairs_p1[i]);
    for (unsigned i = 0; i < 20; ++i) expect_r2 += model_reject(false, pairs_p2[i]);
    SetLastError(0x4321);
    cull::present(1, 100, true);
    check(GetLastError() == 0x4321, "present preserves LastError");
    char expected[160];
    std::snprintf(
        expected, sizeof expected,
        "collide_census_frame device=1 frame=100 p1_pairs=50 p1_rejected=%u p2_cands=20 p2_rejected=%u counters=1 enabled=1",
        expect_r1, expect_r2);
    check(frame_lines.size() == 1 && frame_lines[0] == expected,
          "capture frame line carries the frame's four counters");
    if (frame_lines.size() == 1 && frame_lines[0] != expected) std::printf("DETAIL %s\n", frame_lines[0].c_str());
    take();
    for (unsigned f = 101; f < 400; ++f) {
        for (unsigned i = 0; i < (f & 3); ++i) run(true, pairs_p1[i]);
        cull::present(1, f, false);
    }
    check(frame_lines.size() == 1, "uncaptured frames emit no frame line");
    check(census_lines.size() == 1, "one window line after 300 frames");
    if (census_lines.size() == 1) {
        unsigned long long frame = 0, p50 = 0, mx = 0, sum = 0;
        unsigned frames = 0;
        const int n = std::sscanf(
            census_lines[0].c_str(),
            "collide_census frame=%llu frames=%u p1_pairs_p50=%llu p1_pairs_max=%llu p1_pairs_sum=%llu", &frame,
            &frames, &p50, &mx, &sum);
        // frames 101..399 contribute (f & 3) P1 pairs each: 0,1,2,3 repeating (75 each of 1,2,3 and 74 zeros) plus
        // frame 100's 50.
        check(n == 5 && frame == 399 && frames == 300 && p50 == 2 && mx == 50 && sum == 50 + 75 * (1 + 2 + 3),
              "window line: frame, frames, p50, max and sum of p1_pairs");
        if (n != 5 || frame != 399 || frames != 300 || p50 != 2 || mx != 50)
            std::printf("DETAIL %s\n", census_lines[0].c_str());
    }

    // ---- bench per pair: harness-inclusive (frame copy, x87 load, exit record) ----
    std::vector<Pair> far_set, near_set;
    for (unsigned i = 0; i < pairs_p1.size() && (far_set.size() < 1000 || near_set.size() < 1000); ++i) {
        if (pairs_p1[i].class_a == 7 || pairs_p1[i].class_b == 7) continue;
        if (model_reject(true, pairs_p1[i])) {
            if (far_set.size() < 1000) far_set.push_back(pairs_p1[i]);
        } else if (native_p1[i].code == 1 && near_set.size() < 1000)
            near_set.push_back(pairs_p1[i]); // engine rejects, box keeps: the stub's pure overhead case
    }
    check(far_set.size() == 1000 && near_set.size() == 1000,
          "bench sets: 1,000 box-rejected and 1,000 engine-only-rejected pairs");
    const unsigned loops = 40;
    const double armed_far = bench_ns(true, far_set, loops), armed_near = bench_ns(true, near_set, loops);
    cull::set_enabled(false);
    const double disarmed_far = bench_ns(true, far_set, loops), disarmed_near = bench_ns(true, near_set, loops);
    check(cull::shutdown() && sites_original() && windows_original() && !cull::enabled(),
          "shutdown restores both sites exactly");
    const double native_far = bench_ns(true, far_set, loops), native_near = bench_ns(true, near_set, loops);
    // Native again after the restore: identical to the first replay.
    mismatches = 0;
    for (unsigned i = 0; i < pairs_p1.size(); i += 5) {
        const Result r = run(true, pairs_p1[i]);
        if (!same(true, native_p1[i], r, false, why)) ++mismatches;
    }
    for (unsigned i = 0; i < pairs_p2.size(); i += 5) {
        const Result r = run(false, pairs_p2[i]);
        if (!same(false, native_p2[i], r, false, why)) ++mismatches;
    }
    check(mismatches == 0, "restored bytes replay natively");
    check(cull::install_at(addr(synthetic_p1), addr(synthetic_p2), false), "reinstall without the counters");
    take();
    for (unsigned i = 0; i < 200; ++i) run(true, pairs_p1[i]);
    take();
    check(!counters_[0] && !counters_[1], "no-counter stubs count nothing");
    const double plain_far = bench_ns(true, far_set, loops), plain_near = bench_ns(true, near_set, loops);
    mismatches = 0;
    for (unsigned i = 0; i < pairs_p1.size(); i += 3) {
        const Result r = run(true, pairs_p1[i]);
        if (!same(true, native_p1[i], r, model_reject(true, pairs_p1[i]), why)) ++mismatches;
    }
    check(mismatches == 0, "no-counter stubs: identical outcomes");
    check(cull::shutdown() && sites_original(), "second shutdown restores");
    std::printf(
        "COLLIDE BOX CULL BENCH ns_per_pair far: native=%.1f disarmed=%.1f armed_counted=%.1f armed_plain=%.1f near: native=%.1f disarmed=%.1f armed_counted=%.1f armed_plain=%.1f\n",
        native_far, disarmed_far, armed_far, plain_far, native_near, disarmed_near, armed_near, plain_near);

    // ---- closed window ----
    x3m::engine_patch::close_install_window("fixture");
    check(!cull::install_at(addr(synthetic_p1), addr(synthetic_p2), true) &&
              !std::strcmp(cull::state(), "late_claim") && sites_original(),
          "closed install window refused");
    std::printf("COLLIDE BOX CULL CPU checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
