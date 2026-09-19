// X3 CPU fixture of the sector-collide narrow-phase census
// (src/proxy/collide_narrow_census.cpp, sector-collide.md 11.7): layout-
// preserving synthetic copies of the three sites. Site 5: the engine's bytes
// 0x0045d65d..0x0045d671 (pre-window, call, `add esp,8; test; jle +0x5b`) with
// the contact / no-contact exits at the engine's offsets, calling a byte-exact
// replica of 0x0048ac80 (register arguments ECX/EAX, zeroing of
// [phys+0x180..0x190], plain ret) whose one rel32 targets a fixture stand-in
// for 0x0048a890. The stand-in does x87 work, then runs site 6's bytes
// 0x0048a993..0x0048a9b5 K times (its call targets a stand-in for 0x0047f1b0),
// which calls site 7's copy of the first 35 bytes of 0x004e2530 M times (the
// two abs32 operands point at fixture words: the only differing bytes).
// Checks: patched and unpatched runs agree on the exit, every register,
// EFLAGS, the x87 environment and stack, the engine locals, the callee's
// register and stack arguments, the contact scratch and LastError; counters
// and ring entries exact; ring overflow; memo annotation; capture rows and
// the window line; nested and foreign pass-through; a Present on a second
// thread loses nothing; refusals, partial-install rollback, exact restore,
// closed window. Diagnostic timings only; not game FPS. Never launches the game.
#include "../../src/proxy/collide_narrow_census.h"
#include "../../src/proxy/engine_patch.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cstdarg>
#include <string>
#include <vector>
static std::vector<std::string> window_lines, pair_lines;
namespace x3m { void log(const char* format, ...) {
    char text[1536]; std::va_list a; va_start(a, format); std::vsnprintf(text, sizeof text, format, a); va_end(a);
    if (!std::strncmp(text, "collide_narrow_pair ", 20)) { pair_lines.emplace_back(text); return; }
    if (!std::strncmp(text, "collide_narrow ", 15)) { window_lines.emplace_back(text); return; }
    static unsigned lines = 0; if (lines++ < 8) std::printf("%s\n", text);
} }
namespace x3m::object_trace { bool executable_verified() { return true; } }
namespace census = x3m::collide_narrow_census;
namespace core = x3m::collide_narrow_census::core;

extern "C" {
extern unsigned char synthetic_n5[], synthetic_callee5[], synthetic_n6[], synthetic_n7[];
struct Frame {
    std::uint32_t a, b;            // 0, 4: EBX / ESI, the pair
    std::uint32_t site_esp;        // 8
    std::uint32_t exit_esp;        // 12
    std::uint32_t exit_code;       // 16: 1 no contact (jle taken), 2 contact
    std::uint32_t out[9];          // 20: PUSHAD order edi,esi,ebp,esp,ebx,edx,ecx,eax then EFLAGS, at the exit
    std::uint32_t x87env[7];       // 56: fnstenv at the exit
    std::uint32_t locals_in[108];  // 84
    std::uint32_t locals_out[108]; // 516
    std::uint32_t pad;             // 948
    double st[2];                  // 952
    std::uint32_t entry_ebp;       // 968
    std::uint32_t control_word;    // 972
    std::uint32_t r_b, r_a;        // 976, 980: ECX / EAX at the pre-window (the two stack arguments)
};
std::uint32_t fx_exit_code = 0, fx_frame = 0, fx_saved_esp = 0;
// Stand-in inputs and observations.
std::uint32_t fx_result = 0, fx_mesh_count = 0, fx_node_count = 0, fx_mesh_result = 0, fx_hits = 0, fx_mode = 0;
std::uint32_t fx_loop = 0, fx_seen[6], fx6_seen[8], fx6_hits = 0, fx6_misses = 0, fx7_sum = 0, fx7_calls = 0;
double fx_x87_out = 0;
void fx_n5_run(Frame* frame);
std::uint32_t fx_call_stub(std::uint32_t stub, std::uint32_t phys_a, std::uint32_t phys_b, std::uint32_t r_a, std::uint32_t r_b);
}
static_assert(offsetof(Frame, out) == 20 && offsetof(Frame, x87env) == 56 && offsetof(Frame, locals_in) == 84 && offsetof(Frame, locals_out) == 516
              && offsetof(Frame, st) == 952 && offsetof(Frame, entry_ebp) == 968 && offsetof(Frame, control_word) == 972 && offsetof(Frame, r_a) == 980, "frame layout");
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _synthetic_n5
_synthetic_n5:
    .byte 0x51, 0x8b,0x4b,0x70, 0x50, 0x8b,0x46,0x70
    .byte 0xe8
    .long _synthetic_callee5 - (. + 4)
    .byte 0x83,0xc4,0x08, 0x85,0xc0, 0x7e,0x5b
    .byte 0xc7, 0x05
    .long _fx_exit_code
    .long 2
    .byte 0xe9
    .long _fx_exit_common - (. + 4)
    .skip 0x6f - (. - _synthetic_n5), 0xcc
    .byte 0xc7, 0x05
    .long _fx_exit_code
    .long 1
    .byte 0xe9
    .long _fx_exit_common - (. + 4)
    .p2align 4
    .globl _synthetic_callee5
_synthetic_callee5:
    .byte 0x51, 0x33,0xd2, 0x56, 0x89,0x91,0x90,0x01,0x00,0x00, 0x89,0x90,0x90,0x01,0x00,0x00, 0x57, 0x8d,0xb1,0x90,0x01,0x00,0x00
    .byte 0x89,0x91,0x80,0x01,0x00,0x00, 0x89,0x91,0x84,0x01,0x00,0x00, 0x89,0x91,0x88,0x01,0x00,0x00, 0x89,0x91,0x8c,0x01,0x00,0x00
    .byte 0x8d,0xb8,0x90,0x01,0x00,0x00, 0x57, 0x89,0x90,0x80,0x01,0x00,0x00, 0x89,0x90,0x84,0x01,0x00,0x00, 0x89,0x90,0x88,0x01,0x00,0x00
    .byte 0x89,0x90,0x8c,0x01,0x00,0x00, 0x8b,0x54,0x24,0x18, 0x56, 0x52, 0x8b,0x54,0x24,0x1c, 0x52, 0x50, 0x51
    .byte 0xe8
    .long _fx_narrow_body - (. + 4)
    .byte 0x83,0xc4,0x18, 0x5f, 0x5e, 0x59, 0xc3
    .p2align 4
    .globl _synthetic_n6
_synthetic_n6:
    .byte 0xd9,0xee, 0x51, 0xd9,0x1c,0x24, 0x8b,0xce, 0x6a,0x01, 0x6a,0x02, 0x50, 0x57, 0x33,0xff, 0x8b,0xc3
    .byte 0xe8
    .long _fx_mesh_callee - (. + 4)
    .byte 0x83,0xc4,0x14, 0x85,0xc0, 0x0f,0x84,0x91,0x00,0x00,0x00
    .byte 0xe9
    .long _fx6_hit - (. + 4)
    .skip 0xb3 - (. - _synthetic_n6), 0xcc
    .byte 0xe9
    .long _fx6_miss - (. + 4)
    .p2align 4
    .globl _synthetic_n7
_synthetic_n7:
    .byte 0xa1
    .long _fx_hits
    .byte 0x83,0xec,0x40, 0x83,0x3d
    .long _fx_mode
    .byte 0x00, 0x53,0x55,0x56,0x57, 0x74,0x0e, 0x85,0xc0, 0x7e,0x0a, 0x33,0xc0, 0x5f,0x5e,0x5d,0x5b, 0x83,0xc4,0x40, 0xc3
    add eax, 0x1000
    pop edi
    pop esi
    pop ebp
    pop ebx
    add esp, 0x40
    ret

    .p2align 4
_fx_narrow_body:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    xor ecx, ecx
1:  mov eax, dword ptr [ebp+8+ecx*4]
    mov dword ptr [_fx_seen+ecx*4], eax
    inc ecx
    cmp ecx, 6
    jb 1b
    fldpi
    fsqrt
    fstp qword ptr [_fx_x87_out]
    mov eax, dword ptr [_fx_mesh_count]
    mov dword ptr [_fx_loop], eax
_fx6_next:
    cmp dword ptr [_fx_loop], 0
    je 2f
    dec dword ptr [_fx_loop]
    mov esi, dword ptr [ebp+8]
    mov ebx, dword ptr [ebp+12]
    mov edi, 0x0b0d1e5a
    mov eax, 0x0b0d1e5b
    mov ecx, 0xc1c1c1c1
    jmp _synthetic_n6
_fx6_hit:
    inc dword ptr [_fx6_hits]
    jmp _fx6_next
_fx6_miss:
    inc dword ptr [_fx6_misses]
    jmp _fx6_next
2:  mov eax, dword ptr [_fx_result]
    mov ecx, 0x1111c0de
    mov edx, 0x2222c0de
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
_fx_mesh_callee:
    mov dword ptr [_fx6_seen], ecx
    mov dword ptr [_fx6_seen+4], eax
    mov dword ptr [_fx6_seen+8], edi
    push ebx
    xor ecx, ecx
1:  mov eax, dword ptr [esp+8+ecx*4]
    mov dword ptr [_fx6_seen+12+ecx*4], eax
    inc ecx
    cmp ecx, 5
    jb 1b
    mov ebx, dword ptr [_fx_node_count]
3:  test ebx, ebx
    je 4f
    call _synthetic_n7
    add dword ptr [_fx7_sum], eax
    inc dword ptr [_fx7_calls]
    dec ebx
    jmp 3b
4:  mov eax, dword ptr [_fx_mesh_result]
    pop ebx
    ret

    .globl _fx_n5_run
_fx_n5_run:
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
    fldcw word ptr [eax+972]
    fld1
    fldpi
    mov ebx, dword ptr [eax]
    mov esi, dword ptr [eax+4]
    mov ecx, dword ptr [eax+976]
    mov edi, 0xd1d1d1d1
    mov edx, 0xdddddddd
    mov eax, dword ptr [eax+980]
    jmp _synthetic_n5
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
    .globl _fx_call_stub
_fx_call_stub:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    push dword ptr [ebp+24]
    push dword ptr [ebp+20]
    mov ecx, dword ptr [ebp+12]
    mov eax, dword ptr [ebp+16]
    call dword ptr [ebp+8]
    add esp, 8
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
    .att_syntax
)");

static unsigned checks = 0, failures = 0;
static void check(bool okay, const char* label) { ++checks; if (!okay) { ++failures; if (failures <= 40) std::printf("FAIL %s\n", label); } }
static std::uint32_t addr(const void* p) { return std::uint32_t(reinterpret_cast<std::uintptr_t>(p)); }
static void put(void* base, unsigned off, std::uint32_t v) { std::memcpy(static_cast<unsigned char*>(base) + off, &v, 4); }
static void put16(void* base, unsigned off, std::uint16_t v) { std::memcpy(static_cast<unsigned char*>(base) + off, &v, 2); }

struct alignas(16) Object { unsigned char bytes[0x100]; };
struct alignas(16) Physics { unsigned char bytes[0x200]; };
constexpr unsigned object_count = 16;
static Object objects[object_count]; static Physics physics[object_count];
static std::uint64_t rng = 0x9e3779b97f4a7c15ull;
static std::uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return std::uint32_t(rng >> 16); }
static void build_objects() {
    for (unsigned i = 0; i < object_count; ++i) {
        for (unsigned k = 0; k < sizeof objects[i].bytes; k += 4) put(objects[i].bytes, k, rnd());
        for (unsigned k = 0; k < sizeof physics[i].bytes; k += 4) put(physics[i].bytes, k, rnd());
        put16(objects[i].bytes, core::class_offset, std::uint16_t(5 + i % 9)); put16(objects[i].bytes, core::subtype_offset, std::uint16_t(100 + i));
        put(objects[i].bytes, core::physics_offset, addr(&physics[i])); put(objects[i].bytes, core::radius_offset, 1000 * (i + 1));
    }
}
struct Scenario { unsigned a, b; std::int32_t result; std::uint32_t mesh, nodes, mesh_result, hits, mode, control_word; };
struct Result {
    std::uint32_t code, regs[8], flags, env[7], esp_ok, ebp_ok, seen[6], seen6[8], hits6, misses6, sum7, calls7, scratch_nonzero, last_error; double st0, st1, x87_out; std::uint32_t locals[108];
};
static Frame frame_storage;
static Result run(const Scenario& s, unsigned serial) {
    Frame& f = frame_storage; std::memset(&f, 0, sizeof f);
    f.a = addr(&objects[s.a]); f.b = addr(&objects[s.b]); f.control_word = s.control_word; f.r_a = 0x00a00000u + serial; f.r_b = 0x00b00000u + serial;
    for (unsigned i = 0; i < 108; ++i) f.locals_in[i] = 0x10000000u + i;
    for (unsigned k = 0x180; k <= 0x190; k += 4) { put(physics[s.a].bytes, k, 0xfeedf00d); put(physics[s.b].bytes, k, 0xfeedf00d); }
    fx_result = std::uint32_t(s.result); fx_mesh_count = s.mesh; fx_node_count = s.nodes; fx_mesh_result = s.mesh_result; fx_hits = s.hits; fx_mode = s.mode;
    std::memset(fx_seen, 0, sizeof fx_seen); std::memset(fx6_seen, 0, sizeof fx6_seen); fx6_hits = fx6_misses = fx7_sum = fx7_calls = 0; fx_x87_out = 0;
    SetLastError(0x5150 + serial);
    fx_n5_run(&f);
    Result r{}; r.last_error = GetLastError();
    // AF is architecturally undefined after the `test eax,eax` of the engine's return window (the last flag writer before
    // the exit; FEX derives it lazily from earlier operands, so it differs once a popfd ran in between). Nothing reads it:
    // `jle` follows. Every other bit, DF and the system flags included, is compared.
    r.code = f.exit_code; std::memcpy(r.regs, f.out, sizeof r.regs); r.flags = f.out[8] & ~0x10u; std::memcpy(r.env, f.x87env, sizeof r.env);
    r.regs[3] = 0; r.esp_ok = f.exit_esp == f.site_esp; r.ebp_ok = f.entry_ebp == f.out[2]; r.regs[2] = 0;   // ESP/EBP depend on the C caller's depth: compared as relations
    std::memcpy(r.seen, fx_seen, sizeof r.seen); std::memcpy(r.seen6, fx6_seen, sizeof r.seen6); r.hits6 = fx6_hits; r.misses6 = fx6_misses; r.sum7 = fx7_sum; r.calls7 = fx7_calls;
    for (unsigned k = 0x180; k <= 0x190; k += 4) r.scratch_nonzero += core::load32(physics[s.a].bytes + k) != 0 || core::load32(physics[s.b].bytes + k) != 0;
    r.st0 = f.st[0]; r.st1 = f.st[1]; r.x87_out = fx_x87_out; std::memcpy(r.locals, f.locals_out, sizeof r.locals);
    return r;
}
static bool same(const Result& n, const Result& p, char* why) {
    why[0] = 0;
    if (n.code != p.code) std::sprintf(why, "exit %lu/%lu", (unsigned long)n.code, (unsigned long)p.code);
    else if (std::memcmp(n.regs, p.regs, sizeof n.regs)) std::sprintf(why, "registers eax=%08lx/%08lx ecx=%08lx/%08lx edx=%08lx/%08lx", (unsigned long)n.regs[7], (unsigned long)p.regs[7], (unsigned long)n.regs[6], (unsigned long)p.regs[6], (unsigned long)n.regs[5], (unsigned long)p.regs[5]);
    else if (n.flags != p.flags) std::sprintf(why, "eflags %08lx/%08lx", (unsigned long)n.flags, (unsigned long)p.flags);
    else if (!p.esp_ok || !p.ebp_ok || !n.esp_ok) std::sprintf(why, "esp/ebp");
    else if (std::memcmp(n.env, p.env, sizeof n.env)) std::sprintf(why, "x87 env cw=%04lx/%04lx sw=%04lx/%04lx tag=%04lx/%04lx fip=%08lx/%08lx", (unsigned long)n.env[0] & 0xffff, (unsigned long)p.env[0] & 0xffff, (unsigned long)n.env[1] & 0xffff, (unsigned long)p.env[1] & 0xffff, (unsigned long)n.env[2] & 0xffff, (unsigned long)p.env[2] & 0xffff, (unsigned long)n.env[3], (unsigned long)p.env[3]);
    else if (std::memcmp(&n.st0, &p.st0, 8) || std::memcmp(&n.st1, &p.st1, 8) || std::memcmp(&n.x87_out, &p.x87_out, 8)) std::sprintf(why, "x87 values");
    else if (std::memcmp(n.seen, p.seen, sizeof n.seen) || std::memcmp(n.seen6, p.seen6, sizeof n.seen6)) std::sprintf(why, "callee arguments");
    else if (n.hits6 != p.hits6 || n.misses6 != p.misses6 || n.sum7 != p.sum7 || n.calls7 != p.calls7) std::sprintf(why, "stand-in observations");
    else if (n.scratch_nonzero || p.scratch_nonzero) std::sprintf(why, "contact scratch not zeroed");
    else if (n.last_error != p.last_error) std::sprintf(why, "LastError %lx/%lx", (unsigned long)n.last_error, (unsigned long)p.last_error);
    else if (std::memcmp(n.locals, p.locals, sizeof n.locals)) std::sprintf(why, "engine locals");
    return !why[0];
}
static unsigned char original_n5[0x80], original_n6[0xc0], original_n7[48], original_callee[core::n5_callee_length];
static bool sites_original() {
    return !std::memcmp(synthetic_n5, original_n5, sizeof original_n5) && !std::memcmp(synthetic_n6, original_n6, sizeof original_n6)
        && !std::memcmp(synthetic_n7, original_n7, sizeof original_n7) && !std::memcmp(synthetic_callee5, original_callee, sizeof original_callee);
}
static census::Addresses addresses() {
    return census::Addresses{addr(synthetic_n5) + core::n5_pre_length, addr(synthetic_callee5), addr(synthetic_n6) + core::n6_pre_length,
                             addr(synthetic_n6) + core::n6_pre_length + 5 + std::uintptr_t(std::int32_t(core::load32(original_n6 + core::n6_pre_length + 1))), addr(synthetic_n7), addr(&fx_hits), addr(&fx_mode)};
}
static bool key_matches(const core::ObjectKey& k, unsigned index) {
    const core::ObjectKey e = core::read_key(addr(&objects[index]), objects[index].bytes, addr(&physics[index]), physics[index].bytes);
    return !std::memcmp(&k, &e, sizeof e);
}
static double seconds() { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return double(c.QuadPart) / double(f.QuadPart); }
static double bench_ns(const Scenario& s, unsigned loops, unsigned divisor) {
    const double t0 = seconds(); for (unsigned i = 0; i < loops; ++i) run(s, i); return (seconds() - t0) * 1e9 / (double(loops) * divisor);
}
static volatile LONG presenter_stop = 0; static std::uint64_t presenter_accepted = 0, presenter_nodes = 0, presenter_frames = 0;
static DWORD WINAPI presenter(void*) {
    unsigned long long frame = 1000000;
    while (!presenter_stop) {
        if (census::present(1, ++frame, false)) { const census::FrameView v = census::last_frame(); presenter_accepted += v.accepted; presenter_nodes += v.node_pairs; ++presenter_frames; }
        SwitchToThread();
    }
    return 0;
}

int main() {
    DWORD old = 0;
    const std::uintptr_t page = reinterpret_cast<std::uintptr_t>(synthetic_n5) & ~std::uintptr_t(0xfff);
    check(VirtualProtect(reinterpret_cast<void*>(page), 0x2000, PAGE_EXECUTE_READWRITE, &old) != FALSE, "synthetic code writable");
    std::memcpy(original_n5, synthetic_n5, sizeof original_n5); std::memcpy(original_n6, synthetic_n6, sizeof original_n6);
    std::memcpy(original_n7, synthetic_n7, sizeof original_n7); std::memcpy(original_callee, synthetic_callee5, sizeof original_callee);
    // The copies are the engine's bytes at the engine's offsets; only rel32 / abs32 operands differ.
    unsigned char n7[core::n7_window_length]; core::n7_expected(addr(&fx_hits), addr(&fx_mode), n7);
    bool exact = !std::memcmp(synthetic_n5, core::n5_pre_window, core::n5_pre_length) && synthetic_n5[core::n5_pre_length] == 0xe8
        && !std::memcmp(synthetic_n5 + core::n5_pre_length + 5, core::n5_post_window, core::n5_post_length)
        && !std::memcmp(synthetic_n6, core::n6_pre_window, core::n6_pre_length) && synthetic_n6[core::n6_pre_length] == 0xe8
        && !std::memcmp(synthetic_n6 + core::n6_pre_length + 5, core::n6_post_window, core::n6_post_length) && !std::memcmp(synthetic_n7, n7, sizeof n7)
        && !std::memcmp(synthetic_callee5, core::n5_callee, core::n5_callee_rel32)
        && !std::memcmp(synthetic_callee5 + core::n5_callee_rel32 + 4, core::n5_callee + core::n5_callee_rel32 + 4, core::n5_callee_length - core::n5_callee_rel32 - 4);
    check(exact, "synthetic regions carry the engine windows and the 0x0048ac80 body byte-exact (rel32/abs32 operands aside)");
    if (!exact) { std::printf("COLLIDE NARROW CENSUS CPU checks=%u failures=%u\n", checks, failures); return 1; }
    build_objects();

    // ---- scenarios and the native replay ----
    std::vector<Scenario> scenarios;
    const std::int32_t results[] = {0, 1, -1, 7, 0, 0};
    const std::uint32_t words[] = {0x037f, 0x027f, 0x007f, 0x0f7f};   // 64-bit, 53-bit, 24-bit precision; round toward zero
    for (unsigned i = 0; i < 1500; ++i) {
        Scenario s{rnd() % object_count, rnd() % object_count, results[i % 6], rnd() % 4, rnd() % 6, rnd() & 1, (i % 5 == 0) ? 0u : rnd() % 3, (i % 7 == 0) ? 1u : 0u, words[i % 4]};
        if (s.a == s.b) s.b = (s.a + 1) % object_count;
        scenarios.push_back(s);
    }
    std::vector<Result> native; native.reserve(scenarios.size());
    unsigned exits[3] = {0, 0, 0}, early7 = 0;
    for (unsigned i = 0; i < scenarios.size(); ++i) { native.push_back(run(scenarios[i], i)); ++exits[native.back().code < 3 ? native.back().code : 0]; early7 += scenarios[i].mode && scenarios[i].hits; }
    check(exits[1] > 100 && exits[2] > 100 && exits[0] == 0 && early7 > 20, "native replay reaches both exits and both paths of the 0x004e2530 head");
    bool model = true;
    for (unsigned i = 0; i < scenarios.size(); ++i) {
        const Scenario& s = scenarios[i]; const Result& r = native[i];
        const std::uint32_t per = s.mode && std::int32_t(s.hits) > 0 ? 0u : s.hits + 0x1000u;
        model = model && r.code == (s.result > 0 ? 2u : 1u) && r.calls7 == s.mesh * s.nodes && r.sum7 == per * s.mesh * s.nodes && r.hits6 + r.misses6 == s.mesh
            && r.seen[0] == addr(&physics[s.a]) && r.seen[1] == addr(&physics[s.b]) && r.seen[2] == 0x00a00000u + i && r.seen[3] == 0x00b00000u + i
            && r.seen[4] == r.seen[0] + 0x190 && r.seen[5] == r.seen[1] + 0x190 && r.regs[7] == std::uint32_t(s.result) && r.regs[6] == r.seen[0] && r.regs[5] == 0x2222c0deu
            && (s.mesh == 0 || (r.seen6[0] == r.seen[0] && r.seen6[1] == r.seen[1] && r.seen6[2] == 0 && r.seen6[3] == 0x0b0d1e5au && r.seen6[4] == 0x0b0d1e5bu && r.seen6[5] == 2 && r.seen6[6] == 1 && r.seen6[7] == 0));
    }
    check(model, "native replay: the engine bytes pass the register and stack arguments, the node-pair head and the result as decoded");

    // ---- option off / refusals ----
    SetEnvironmentVariableW(L"X3M_COLLIDE_NARROW_CENSUS", nullptr);
    check(!census::initialize() && !std::strcmp(census::state(), "disabled") && sites_original(), "variable unset: nothing patched");
    SetEnvironmentVariableW(L"X3M_COLLIDE_NARROW_CENSUS", L"0");
    check(!census::initialize() && !std::strcmp(census::state(), "disabled") && sites_original(), "value 0: nothing patched");
    SetEnvironmentVariableW(L"X3M_COLLIDE_NARROW_CENSUS", L"1");
    check(!census::initialize() && sites_original() && (!std::strcmp(census::state(), "callee_mismatch") || !std::strcmp(census::state(), "bytes_mismatch")), "value 1 without the engine image: callees absent, nothing patched");
    SetEnvironmentVariableW(L"X3M_COLLIDE_NARROW_CENSUS", nullptr);
    census::Addresses bad = addresses(); bad.n6_site = 0;
    check(!census::install_at(bad) && !std::strcmp(census::state(), "invalid_site") && sites_original(), "null site refused");
    synthetic_n5[core::n5_pre_length + 5 + 2] ^= 0x04;   // add esp,8 -> add esp,0xc
    check(!census::install_at(addresses()) && !std::strcmp(census::state(), "bytes_mismatch") && !std::memcmp(synthetic_n6, original_n6, sizeof original_n6) && !std::memcmp(synthetic_n7, original_n7, sizeof original_n7), "changed site-5 return window refused");
    synthetic_n5[core::n5_pre_length + 5 + 2] ^= 0x04;
    synthetic_n6[7] ^= 0x08;                             // mov ecx,esi -> another register
    check(!census::install_at(addresses()) && !std::strcmp(census::state(), "bytes_mismatch"), "changed site-6 register argument refused");
    synthetic_n6[7] ^= 0x08;
    synthetic_n7[20] ^= 0x01;                            // je +0x0e -> another target
    check(!census::install_at(addresses()) && !std::strcmp(census::state(), "bytes_mismatch"), "changed 0x004e2530 head refused");
    synthetic_n7[20] ^= 0x01;
    bad = addresses(); bad.n7_hits += 4;
    check(!census::install_at(bad) && !std::strcmp(census::state(), "bytes_mismatch") && sites_original(), "site 7 reading another global refused");

    // ---- partial install: sites 7 and 6 claimed, site 5 refused -> both rolled back ----
    char why[256];
    bad = addresses(); bad.n5_target += 1;
    const std::uint32_t before_counters[2] = {x3m_collide_narrow_counters[0], x3m_collide_narrow_counters[1]};
    check(!census::install_at(bad) && !std::strcmp(census::state(), "target_mismatch"), "site 5 with another callee refused by the call-site claim (after sites 7 and 6 were patched)");
    check(sites_original() && !census::stub_address(5) && !census::stub_address(7), "partial install rolled back byte-exact");
    { const Result r = run(scenarios[3], 3); check(same(native[3], r, why) && x3m_collide_narrow_counters[0] == before_counters[0] && x3m_collide_narrow_counters[1] == before_counters[1], "after the rollback the sites run natively and count nothing"); }
    bad = addresses(); bad.n6_target += 1;
    check(!census::install_at(bad) && !std::strcmp(census::state(), "target_mismatch") && sites_original(), "site 6 with another callee refused, site 7 rolled back");

    // ---- install ----
    SetLastError(0x1234);
    check(census::install_at(addresses()), "install on the synthetic regions");
    check(GetLastError() == 0x1234, "install preserves LastError");
    check(!std::strcmp(census::state(), "ok") && census::stub_address(5) && census::stub_address(6) && census::stub_address(7), "state ok, three stubs live");
    check(synthetic_n5[core::n5_pre_length] == 0xe8 && std::memcmp(synthetic_n5, original_n5, sizeof original_n5) && synthetic_n6[core::n6_pre_length] == 0xe8 && synthetic_n7[0] == 0xe9
          && !std::memcmp(synthetic_n7 + 5, original_n7 + 5, sizeof original_n7 - 5) && !std::memcmp(synthetic_callee5, original_callee, sizeof original_callee), "sites 5/6 keep their call opcode, site 7 carries the jump, nothing else changed");
    check(!census::install_at(addresses()) && !std::strcmp(census::state(), "already_installed"), "second install refused");

    // ---- patched replay: identical machine state, exact counters and ring entries ----
    census::present(1, 1, false);
    unsigned mismatches = 0, counter_mismatch = 0, entry_mismatch = 0;
    for (unsigned i = 0; i < scenarios.size(); ++i) {
        const Scenario& s = scenarios[i];
        const std::uint32_t c0 = x3m_collide_narrow_counters[0], c1 = x3m_collide_narrow_counters[1];
        const Result r = run(s, i);
        if (!same(native[i], r, why) && ++mismatches <= 8) std::printf("DETAIL scenario %u: %s\n", i, why);
        if (x3m_collide_narrow_counters[0] - c0 != s.mesh || x3m_collide_narrow_counters[1] - c1 != s.mesh * s.nodes || x3m_collide_narrow_busy) ++counter_mismatch;
        SetLastError(0x4321);
        census::present(1, 2 + i, false);
        if (GetLastError() != 0x4321) ++counter_mismatch;
        const census::FrameView v = census::last_frame();
        if (v.count != 1 || v.accepted != 1 || v.overflow || v.mesh_pairs != s.mesh || v.node_pairs != s.mesh * s.nodes || !v.entries || !key_matches(v.entries[0].a, s.a) || !key_matches(v.entries[0].b, s.b)
            || v.entries[0].result != s.result || v.entries[0].visits != s.mesh * s.nodes || v.entries[0].mesh_pairs != s.mesh) { if (++entry_mismatch <= 4) std::printf("DETAIL entry %u count=%u accepted=%lu\n", i, v.count, (unsigned long)v.accepted); }
    }
    check(mismatches == 0, "patched: identical exit, registers, EFLAGS, x87 environment and stack, callee arguments, contact scratch, locals and LastError on every pair");
    check(counter_mismatch == 0, "counters: mesh-pair tests and node-pair visits exact per pair, busy flag released, present preserves LastError");
    check(entry_mismatch == 0, "ring entry: objects, class/subtype/flags/radius, positions, model, hashes, result and attributed visits exact");
    check(x3m_collide_narrow_counters[2] == 0 && x3m_collide_narrow_counters[3] == 0 && census::dropped_total() == 0, "no nested, foreign or dropped pair on the plain path");

    // ---- ring overflow ----
    Scenario quiet{1, 2, 0, 1, 2, 0, 0, 0, 0x027f};
    for (unsigned i = 0; i < 300; ++i) run(quiet, i);
    census::present(1, 5000, false);
    { const census::FrameView v = census::last_frame(); check(v.count == core::ring_capacity && v.accepted == 300 && v.overflow == 44 && v.mesh_pairs == 300 && v.node_pairs == 600, "ring overflow: 256 recorded, 44 counted as overflow, totals complete"); }

    // ---- memo annotation across frames ----
    auto frame_of = [&](unsigned long long frame, const std::int32_t* result, const std::uint32_t* nodes, bool captured) {
        for (unsigned i = 0; i < 10; ++i) { Scenario s{i, i + 1, result[i], 1, nodes[i], 0, 0, 0, 0x027f}; run(s, i); }
        census::present(1, frame, captured); return census::last_frame();
    };
    std::int32_t res[10] = {0, 0, 0, 0, 0, 1, 0, 0, 0, -1}; std::uint32_t nodes[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    frame_of(6000, res, nodes, false);
    census::FrameView v = frame_of(6001, res, nodes, false);
    check(v.count == 10 && v.memo.with_previous == 10 && v.memo.unchanged == 10 && v.memo.memo_hits == 9 && v.memo.memo_hit_visits == 1 + 2 + 3 + 4 + 5 + 7 + 8 + 9 + 10 && v.memo.memo_unsafe == 0 && v.memo.visits_differ == 0,
          "identical frame: every pair unchanged, the nine no-contact pairs are memo hits carrying their visits");
    put(physics[2].bytes, core::position_offset + 4, core::load32(physics[2].bytes + core::position_offset + 4) + 1);   // pairs 1 and 2 move
    put(physics[5].bytes, core::xform_offset + 8, 0x12345678);                                                             // pairs 4 and 5 rotate
    put(physics[8].bytes, core::saved_offset, 0x0badf00d);                                                                 // pairs 7 and 8: saved position
    res[0] = 1; nodes[9] = 99;                                                                                             // pair 0: same key, now a contact; pair 9: same key, other visit count
    pair_lines.clear();
    v = frame_of(6002, res, nodes, true);
    check(v.memo.with_previous == 10 && v.memo.unchanged == 4 && v.memo.changed_position == 2 && v.memo.changed_xform == 2 && v.memo.changed_saved == 2 && v.memo.memo_hits == 3
          && v.memo.memo_unsafe == 1 && v.memo.visits_differ == 1 && (v.entries[0].flags & core::memo_unsafe) && (v.entries[9].flags & core::visits_differ) && !(v.entries[1].flags & core::same_position)
          && (v.entries[1].flags & core::same_xform) && !(v.entries[4].flags & core::same_xform) && !(v.entries[7].flags & core::same_saved), "changed position / transform / saved position / result / visit count each classified");
    check(pair_lines.size() == 10 && pair_lines[0].find(" rank=0 of=10 ") != std::string::npos && pair_lines[0].find(" visits=99 ") != std::string::npos && pair_lines[0].find(" visits_differ=1") != std::string::npos
          && pair_lines[9].find(" visits=1 ") != std::string::npos && pair_lines[9].find(" memo_unsafe=1 ") != std::string::npos && pair_lines[9].find(" contact=1 ") != std::string::npos, "capture frame: one row per entry, ordered by visits");
    if (!pair_lines.empty()) std::printf("ROW %s\n", pair_lines[0].c_str());
    { Scenario s{12, 13, 0, 0, 0, 0, 0, 0, 0x027f}; run(s, 0); census::present(1, 6003, false); v = census::last_frame(); check(v.count == 1 && v.memo.with_previous == 0 && v.entries[0].flags == 0, "a pair absent from the previous frame has no memo verdict"); }

    // ---- nested and foreign pass-through ----
    x3m_collide_narrow_busy = 1;
    { const Result r = run(scenarios[11], 11); x3m_collide_narrow_busy = 0; census::present(1, 6004, false); v = census::last_frame();
      check(same(native[11], r, why) && x3m_collide_narrow_counters[2] == 1 && v.accepted == 0 && v.count == 0 && v.node_pairs == scenarios[11].mesh * scenarios[11].nodes, "re-entered narrow phase passes through uncounted, sites 6/7 still count"); }
    fx_result = 5; fx_mesh_count = 0;
    const std::uint32_t direct = fx_call_stub(std::uint32_t(census::stub_address(5)), addr(&physics[1]), addr(&physics[2]), 11, 22);
    census::present(1, 6005, false); v = census::last_frame();
    check(direct == 5 && fx_seen[0] == addr(&physics[1]) && fx_seen[1] == addr(&physics[2]) && fx_seen[2] == 11 && fx_seen[3] == 22 && x3m_collide_narrow_counters[3] == 1 && v.accepted == 0 && !x3m_collide_narrow_busy, "a caller other than the patched site passes through unbracketed");

    // ---- window line ----
    window_lines.clear();
    for (unsigned f = 0; f < 320 && window_lines.empty(); ++f) { for (unsigned i = 0; i < (f & 3); ++i) run(quiet, i); census::present(1, 7000 + f, false); }
    check(window_lines.size() == 1, "one window line per 300 frames");
    if (window_lines.size() == 1) {
        unsigned long long frame = 0, p50 = 0, mx = 0, sum = 0, m50 = 0, mmax = 0, msum = 0, n50 = 0, nmax = 0, nsum = 0; unsigned frames = 0;
        const int n = std::sscanf(window_lines[0].c_str(), "collide_narrow frame=%llu frames=%u accepted_p50=%llu accepted_max=%llu accepted_sum=%llu mesh_pairs_p50=%llu mesh_pairs_max=%llu mesh_pairs_sum=%llu node_pairs_p50=%llu node_pairs_max=%llu node_pairs_sum=%llu",
                                  &frame, &frames, &p50, &mx, &sum, &m50, &mmax, &msum, &n50, &nmax, &nsum);
        // The window holds the last 8 frames above (scenario 1499, the overflow frame, three memo frames, the absent pair,
        // the nested and the foreign pass) and 292 frames of (f & 3) quiet pairs: 73 each of 0, 1, 2, 3.
        const Scenario& last = scenarios.back(); const Scenario& nest = scenarios[11];
        const unsigned long long want_mesh = last.mesh + 300 + 30 + nest.mesh + 438, want_nodes = last.mesh * last.nodes + 600 + 55 + 55 + 144 + nest.mesh * nest.nodes + 876;
        check(n == 11 && frame == 7291 && frames == 300 && p50 == 2 && mx == 300 && sum == 770 && msum == want_mesh && nsum == want_nodes && m50 <= mmax && n50 <= nmax, "window line: frame, frames, p50, max and sums of the three counters");
        check(window_lines[0].find(" recorded_sum=726 ") != std::string::npos && window_lines[0].find(" ring_overflow=44 ") != std::string::npos && window_lines[0].find(" nested=1 foreign=1 cross_thread_frames=0") != std::string::npos
              && window_lines[0].find(" dropped=0 deferred=0 ") != std::string::npos, "window line: sums, the overflow, the nested and foreign passes, no cross-thread frame");
        std::printf("WINDOW %s\n", window_lines[0].c_str());
    }

    // ---- Present on another thread: nothing lost ----
    census::present(1, 8000, false);
    const std::uint32_t dropped_before = census::dropped_total();
    HANDLE thread = CreateThread(nullptr, 0, presenter, nullptr, 0, nullptr);
    const unsigned concurrent = 20000;
    for (unsigned i = 0; i < concurrent; ++i) run(quiet, i);
    InterlockedExchange(&presenter_stop, 1); WaitForSingleObject(thread, INFINITE); CloseHandle(thread);
    census::present(1, 9000000, false); v = census::last_frame();
    const std::uint64_t accounted = presenter_accepted + v.accepted + (census::dropped_total() - dropped_before);
    check(thread && presenter_frames > 10 && accounted == concurrent && presenter_nodes + v.node_pairs == 2ull * concurrent, "cross-thread Present: accepted + dropped and the node-pair deltas account for every pair");
    std::printf("cross-thread frames=%llu accepted=%llu dropped=%lu\n", presenter_frames, presenter_accepted + v.accepted, (unsigned long)(census::dropped_total() - dropped_before));

    // ---- bench (harness-inclusive) and restore ----
    Scenario bare{1, 2, 0, 0, 0, 0, 0, 0, 0x027f}, deep{1, 2, 0, 1, 20000, 0, 0, 0, 0x027f};
    const double patched_pair = bench_ns(bare, 20000, 1), patched_visit = bench_ns(deep, 40, 20000);
    check(census::shutdown() && sites_original() && !census::stub_address(5), "shutdown restores the three sites exactly");
    const double native_pair = bench_ns(bare, 20000, 1), native_visit = bench_ns(deep, 40, 20000);
    mismatches = 0;
    for (unsigned i = 0; i < scenarios.size(); i += 3) { const Result r = run(scenarios[i], i); if (!same(native[i], r, why)) ++mismatches; }
    check(mismatches == 0, "restored bytes replay natively");
    check(census::install_at(addresses()) && census::shutdown() && sites_original(), "reinstall and second shutdown");
    std::printf("COLLIDE NARROW CENSUS BENCH ns_per_pair native=%.1f patched=%.1f ns_per_node_visit native=%.2f patched=%.2f\n", native_pair, patched_pair, native_visit, patched_visit);

    // ---- closed window ----
    x3m::engine_patch::close_install_window("fixture");
    check(!census::install_at(addresses()) && !std::strcmp(census::state(), "late_claim") && sites_original(), "closed install window refused");
    std::printf("COLLIDE NARROW CENSUS CPU checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
