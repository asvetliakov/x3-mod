// Original synthetic fixture of the FL flare-probe hook and lens bracket (src/engine/fl/sun_occlusion_fl.cpp; the probe
// forwards every call since own sun step 2, docs/architecture/own-sun-pass.md section 14),
// docs/architecture/farnhams-legacy-sun-occlusion.md: no game code, no game bytes beyond the generated window/anchor
// constants, no launch. Built with -DX3M_EDITION_FL=1 -DX3M_SUN_OCCLUSION_FL_FIXTURE by
// verification/analysis/test_fl_sun_occlusion.py and run natively (WSL interop).
//
// A fake flare walk carries the generated 26-byte probe window (cmp [esi+0x30],0; je; mov edx,ebx; mov ecx,esi;
// call rel32; test eax,eax; je; mov dword [esi+0x30],0) with the call one qword-aligned word in (site % 8 = 2, as
// base+0x8af0a), EBX = view, ESI = record, EDI/EBP sentinels, XMM0-7 and one x87 value loaded and DF set; a fake lens
// pass carries `mov edx,esi; call rel32` with the call straddling a qword (site % 8 = 7, as base+0x8bcb7). The fake
// targets record their entry registers, EFLAGS, stack, XMM, x87 and LastError and return sentinels. The listeners
// clobber XMM0-7, the x87 stack/control word, MXCSR and LastError; the hooks must hide all of it.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <excpt.h>
#include <cstdarg>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../../src/proxy/own_sun_hook.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/engine/fl/sun_occlusion_fl_inc.h"
namespace so = x3m::own_sun_hook;
namespace L = x3m::fl::sun_occlusion_fl;
unsigned checks = 0, failures = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}
char last_row[4096];
unsigned rows = 0;
// Reached through a pointer, as production's log() reaches the formatter through call_preserved: the CPU-state audit
// stops at the indirect call.
void log_body(const char* format, va_list args) {
    std::vsnprintf(last_row, sizeof last_row, format, args);
    ++rows;
    std::printf("LOG %s\n", last_row);
}
void (*volatile log_sink)(const char*, va_list) = &log_body;
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_sink(format, args);
    va_end(args);
}
}
bool row_has(const char* text) {
    return std::strstr(last_row, text) != nullptr;
}
extern "C" {
// Written by the fake callers / targets (absolute addresses in the emitted caller bytes and the asm).
uint32_t esp_before = 0, esp_after = 0, ebp_after = 0, esi_after = 0, edi_after = 0, ebx_after = 0, ecx_after = 0,
         edx_after = 0, eax_after = 0, flags_before = 0, flags_after = 0;
uint32_t t_ecx = 0, t_edx = 0, t_esi = 0, t_edi = 0, t_ebx = 0, t_ebp = 0, t_esp = 0, t_ret = 0, t_ret2 = 0,
         t_flags = 0, t_mxcsr = 0, probe_answer = 0, lens_out_flags = 0x2c3; // IF|SF|ZF|CF
uint16_t t_cw = 0, t_sw = 0;
double x87_after = 0;
alignas(16) uint32_t xmm_in[32], xmm_at_target[32], xmm_out[32], xmm_after[32];
void __cdecl fl_fake_probe_body();
void __cdecl fl_fake_lens_body();
}
#define RECORD_ENTRY                                                                                                   \
    "movl %ecx,_t_ecx\n\tmovl %edx,_t_edx\n\tmovl %esi,_t_esi\n\tmovl %edi,_t_edi\n\tmovl %ebx,_t_ebx\n\t"             \
    "movl %ebp,_t_ebp\n\tmovl %esp,_t_esp\n\tmovl (%esp),%eax\n\tmovl %eax,_t_ret\n\tmovl 4(%esp),%eax\n\t"            \
    "movl %eax,_t_ret2\n\tpushfl\n\tpopl _t_flags\n\t"                                                                 \
    "movups %xmm0,_xmm_at_target\n\tmovups %xmm1,_xmm_at_target+16\n\tmovups %xmm2,_xmm_at_target+32\n\t"              \
    "movups %xmm3,_xmm_at_target+48\n\tmovups %xmm4,_xmm_at_target+64\n\tmovups %xmm5,_xmm_at_target+80\n\t"           \
    "movups %xmm6,_xmm_at_target+96\n\tmovups %xmm7,_xmm_at_target+112\n\t"                                            \
    "fnstcw _t_cw\n\tfnstsw _t_sw\n\tstmxcsr _t_mxcsr\n\tcld\n\t"
#define RETURN_XMM                                                                                                     \
    "movups _xmm_out,%xmm0\n\tmovups _xmm_out+16,%xmm1\n\tmovups _xmm_out+32,%xmm2\n\tmovups _xmm_out+48,%xmm3\n\t"   \
    "movups _xmm_out+64,%xmm4\n\tmovups _xmm_out+80,%xmm5\n\tmovups _xmm_out+96,%xmm6\n\tmovups _xmm_out+112,%xmm7\n\t"
// The probe: register ABI (ECX record, EDX view), no stack argument, EAX the answer, bare RET; ECX/EDX clobbered.
// The lens draw: EDX view, ECX unread, bare RET; EAX/ECX/EDX and EFLAGS returned as sentinels.
asm(".text\n\t.globl _fl_fake_probe\n_fl_fake_probe:\n\t" RECORD_ENTRY
    "pushl %ebp\n\tmovl %esp,%ebp\n\tandl $-16,%esp\n\tcall _fl_fake_probe_body\n\tmovl %ebp,%esp\n\tpopl %ebp\n\t"
    RETURN_XMM "movl $0x0cc0ffee,%ecx\n\tmovl $0x0dd0beef,%edx\n\tmovl _probe_answer,%eax\n\tret\n\t"
    ".globl _fl_fake_lens\n_fl_fake_lens:\n\t" RECORD_ENTRY
    "pushl %ebp\n\tmovl %esp,%ebp\n\tandl $-16,%esp\n\tcall _fl_fake_lens_body\n\tmovl %ebp,%esp\n\tpopl %ebp\n\t"
    RETURN_XMM "movl $0x0cc0ffee,%ecx\n\tmovl $0x0dd0beef,%edx\n\tpushl _lens_out_flags\n\tpopfl\n\t"
    "movl $0x0aa0beef,%eax\n\tret\n\t"
    // Another callee (the claims must refuse a rel32 that names it).
    ".globl _fl_other_target\n_fl_other_target:\n\tret\n\t");
extern "C" void fl_fake_probe();
extern "C" void fl_fake_lens();
extern "C" void fl_other_target();

using Walk = uint32_t(__cdecl*)(uint32_t* record, uint32_t* view);
using Lens = uint32_t(__cdecl*)(uint32_t* view);
struct Emit {
    unsigned char* p;
    void b(std::initializer_list<unsigned> bytes) {
        for (unsigned x : bytes) *p++ = static_cast<unsigned char>(x);
    }
    void d(uint32_t v) {
        std::memcpy(p, &v, 4);
        p += 4;
    }
    void abs(std::initializer_list<unsigned> opcode, const void* address) {
        b(opcode);
        d(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(address)));
    }
    void pad_to(unsigned modulo) { // nops until p % 8 == modulo
        while ((reinterpret_cast<uintptr_t>(p) & 7) != modulo) b({0x90});
    }
    void rel(const void* target) { // the rel32 of the call just emitted
        d(uint32_t(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(p) + 4)));
    }
};
void emit_prologue(Emit& e) {
    for (unsigned r = 0; r < 8; ++r) e.abs({0x0f, 0x10, 0x05 + 8 * r}, xmm_in + 4 * r); // movups xmmR,[xmm_in+16R]
    e.b({0xd9, 0xe8});                                                                // fld1
    e.b({0xbf}), e.d(0x5a5a2222);                                                     // mov edi
    e.b({0xbd}), e.d(0x5a5a3333);                                                     // mov ebp (holder cursor)
    e.b({0xb9}), e.d(0x5a5a1111);                                                     // mov ecx
    e.b({0xba}), e.d(0x5a5a4444);                                                     // mov edx
    e.abs({0x89, 0x25}, &esp_before);
    e.b({0xfd});                         // std: DF is part of the site's EFLAGS
    e.b({0x9c}), e.abs({0x8f, 0x05}, &flags_before);
}
void emit_epilogue(Emit& e) {
    e.b({0x9c}), e.abs({0x8f, 0x05}, &flags_after); // pushfd; pop [flags_after]
    e.b({0xfc});                                    // cld
    e.abs({0x89, 0x25}, &esp_after);
    e.abs({0x89, 0x2d}, &ebp_after);
    e.abs({0x89, 0x35}, &esi_after);
    e.abs({0x89, 0x3d}, &edi_after);
    e.abs({0x89, 0x1d}, &ebx_after);
    e.abs({0x89, 0x0d}, &ecx_after);
    e.abs({0x89, 0x15}, &edx_after);
    e.abs({0xa3}, &eax_after);
    for (unsigned r = 0; r < 8; ++r) e.abs({0x0f, 0x11, 0x05 + 8 * r}, xmm_after + 4 * r);
    e.abs({0xdd, 0x1d}, &x87_after); // fstp qword [x87_after]
}
// The probe window: the generated walk bytes from 0x8af00 (26 bytes, both je's land on its end).
constexpr unsigned window_at = L::probe_site_rva - 10 - L::probe_walk_rva, window_length = 26;
static_assert(L::probe_walk_bytes[window_at] == 0x83 && L::probe_walk_bytes[window_at + 10] == 0xe8 &&
                  L::probe_walk_bytes[window_at + 5] == 0x14 && L::probe_walk_bytes[window_at + 18] == 0x07,
              "cmp [esi+0x30],0; je +0x14; ...; call; test; je +7; mov [esi+0x30],0");
unsigned char* emit_walk(unsigned char* at, const void* target, unsigned char** site) {
    Emit e{at};
    e.b({0x55, 0x53, 0x56, 0x57});       // push ebp/ebx/esi/edi
    e.b({0x8b, 0x74, 0x24, 0x14});       // mov esi,[esp+0x14] (record)
    e.b({0x8b, 0x5c, 0x24, 0x18});       // mov ebx,[esp+0x18] (view)
    emit_prologue(e);
    e.pad_to(0); // the call at window + 10 lands on % 8 == 2
    std::memcpy(e.p, L::probe_walk_bytes + window_at, window_length);
    *site = e.p + 10;
    e.p += 10 + 1;
    e.rel(target);
    e.p += window_length - 15;
    emit_epilogue(e);
    e.b({0x8b, 0x46, 0x30});             // mov eax,[esi+0x30]
    e.b({0x5f, 0x5e, 0x5b, 0x5d, 0xc3}); // pop edi/esi/ebx/ebp; ret
    return e.p;
}
unsigned char* emit_lens(unsigned char* at, const void* target, unsigned char** site) {
    Emit e{at};
    e.b({0x55, 0x53, 0x56, 0x57});
    e.b({0x8b, 0x74, 0x24, 0x14}); // mov esi,[esp+0x14] (lens view)
    e.b({0xbb}), e.d(0x5a5a6666);  // mov ebx
    emit_prologue(e);
    e.pad_to(5); // mov edx,esi at % 8 == 5: the call at % 8 == 7 straddles a qword
    e.b({0x8b, 0xd6, 0xe8});
    *site = e.p - 1;
    e.rel(target);
    emit_epilogue(e);
    e.b({0x5f, 0x5e, 0x5b, 0x5d, 0xc3});
    return e.p;
}

// ---- the engine-side objects ----------------------------------------------------------------------------------
alignas(16) uint32_t record[0x70 / 4], view[0x380 / 4], other_view[0x380 / 4], owner[0x2a0 / 4], config[0x110 / 4];
uint32_t config_slot = 0, main_view_word = 0;
Walk walk = nullptr;
Lens lens = nullptr;
unsigned char *probe_site = nullptr, *lens_site = nullptr;
unsigned probe_calls = 0, lens_calls = 0, begin_calls = 0, end_calls = 0;
uint32_t native_probe_flags = 0; // the original's entry EFLAGS without the hook (the window's cmp defines them)
unsigned target_mode = 0;     // 0 checked, 2 raise (lens), 3 timing (no checks)
bool bracket_at_target = false, clobber = true, check_probe_error = true;
void make_objects() {
    std::memset(record, 0, sizeof record);
    std::memset(view, 0, sizeof view);
    std::memset(other_view, 0, sizeof other_view);
    std::memset(owner, 0, sizeof owner);
    std::memset(config, 0, sizeof config);
    record[L::record_owner / 4] = uint32_t(reinterpret_cast<uintptr_t>(owner));
    record[L::record_accumulator / 4] = 200;
    record[L::record_size / 4] = 0x1234;
    record[L::record_group / 4] = 5;
    for (uint32_t* v : {view, other_view}) {
        v[L::view_flags / 4] = 0x1;
        v[L::view_rect_top / 4] = 0;
        v[L::view_rect_bottom / 4] = 0x10000;
        v[L::view_rect_left / 4] = 0;
        v[L::view_rect_right / 4] = 0x10000;
        v[L::view_fov / 4] = 0x4000;
        v[L::view_scale_x / 4] = 0x8000;
        v[L::view_layer / 4] = 20;
    }
    owner[L::view_flags / 4] = 0x00400135; // background regime
    owner[L::view_layer / 4] = 15;
    config[L::config_video_flags / 4] = L::video_flag_flares;
    config_slot = uint32_t(reinterpret_cast<uintptr_t>(config));
    main_view_word = uint32_t(reinterpret_cast<uintptr_t>(view));
}

extern "C" void __cdecl fl_fake_probe_body() {
    ++probe_calls;
    if (target_mode == 3) return;
    if (check_probe_error) check(GetLastError() == 0x145, "probe original entered with the caller's LastError");
    SetLastError(0x246);
}
std::jmp_buf outer_jump;
extern "C" void __cdecl fl_fake_lens_body() {
    ++lens_calls;
    if (target_mode == 3) return;
    bracket_at_target = so::bracket_open();
    check(GetLastError() == 0x145, "lens original entered with the caller's LastError");
    if (target_mode == 2) RaiseException(0xe3450001, 0, 0, nullptr);
    SetLastError(0x246);
}
// Every volatile piece of CPU state the hooks must hide from the engine.
void clobber_cpu() {
    if (!clobber) return;
    const uint16_t cw = 0x0c7f;
    const uint32_t mxcsr = 0x9fc0;
    asm volatile("pcmpeqd %%xmm0,%%xmm0\n\tpcmpeqd %%xmm1,%%xmm1\n\tpxor %%xmm2,%%xmm2\n\tpcmpeqd %%xmm3,%%xmm3\n\t"
                 "pxor %%xmm4,%%xmm4\n\tpcmpeqd %%xmm5,%%xmm5\n\tpcmpeqd %%xmm6,%%xmm6\n\tpxor %%xmm7,%%xmm7\n\t"
                 "fldpi\n\tfldpi\n\tfldcw %0\n\tldmxcsr %1" ::"m"(cw),
                 "m"(mxcsr)
                 : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "st", "st(1)");
    SetLastError(0x999);
}
uint16_t listener_sw = 0xffff, listener_cw = 0;
void listener_check() {
    asm volatile("fnstsw %0\n\tfnstcw %1" : "=m"(listener_sw), "=m"(listener_cw));
    if (target_mode != 3) check(listener_sw == 0 && listener_cw == 0x037f, "listener runs on an initialised x87");
}
void begin_listener() {
    ++begin_calls;
    listener_check();
    clobber_cpu();
}
void end_listener() {
    ++end_calls;
    listener_check();
    clobber_cpu();
}

// ---- witnessed calls ------------------------------------------------------------------------------------------
struct Cpu {
    uint16_t cw;
    uint32_t mxcsr;
};
Cpu enter_test_state() {
    for (unsigned i = 0; i < 32; ++i) {
        xmm_in[i] = 0xa5000000u + i * 0x01010101u;
        xmm_out[i] = 0x5a000000u + i * 0x00010203u;
    }
    Cpu saved{};
    asm volatile("fnstcw %0\n\tstmxcsr %1" : "=m"(saved.cw), "=m"(saved.mxcsr));
    const uint16_t test_cw = 0x027f;
    const uint32_t test_mxcsr = (saved.mxcsr & ~0x6000u) | 0x6000u;
    t_sw = 0xffff;
    SetLastError(0x145);
    asm volatile("fldcw %0\n\tldmxcsr %1" ::"m"(test_cw), "m"(test_mxcsr));
    return saved;
}
struct After {
    uint16_t cw;
    uint32_t mxcsr;
    DWORD error;
};
After leave_test_state(const Cpu& saved) {
    After a{};
    asm volatile("fnstcw %0\n\tstmxcsr %1" : "=m"(a.cw), "=m"(a.mxcsr));
    a.error = GetLastError();
    asm volatile("fldcw %0\n\tldmxcsr %1" ::"m"(saved.cw), "m"(saved.mxcsr));
    return a;
}
constexpr uint16_t test_cw = 0x027f;
uint32_t test_mxcsr() {
    uint32_t m = 0;
    asm volatile("stmxcsr %0" : "=m"(m));
    return (m & ~0x6000u) | 0x6000u;
}
// One probe through the fake walk. expect: 0 / 1 answered by the hook, 2 the original ran (answer = probe_answer).
void probe(uint32_t* v, int expect, const char* label) {
    char text[200];
    auto c = [&](bool value, const char* what) {
        std::snprintf(text, sizeof text, "%s: %s", label, what);
        check(value, text);
    };
    const uint32_t mx = test_mxcsr();
    record[L::record_visible / 4] = 1;
    const unsigned calls = probe_calls;
    const Cpu saved = enter_test_state();
    const uint32_t visible = walk(record, v);
    const After a = leave_test_state(saved);
    const uint32_t answer = expect == 2 ? probe_answer : uint32_t(expect);
    c(esp_after == esp_before && ebp_after == 0x5a5a3333, "stack balanced, EBP (holder cursor) kept");
    c(esi_after == uint32_t(reinterpret_cast<uintptr_t>(record)) && ebx_after == uint32_t(reinterpret_cast<uintptr_t>(v)) &&
          edi_after == 0x5a5a2222,
      "ESI record, EBX view, EDI owner kept");
    c(visible == (answer ? 0u : 1u), "record+0x30 cleared exactly on a nonzero answer");
    c(((flags_after >> 6) & 1) == (answer == 0), "the walk's test eax,eax saw the answer");
    c(x87_after == 1.0 && a.cw == test_cw && a.mxcsr == mx, "x87 value, control word and MXCSR kept");
    if (expect != 2) {
        c(probe_calls == calls && eax_after == answer, "answered without the original, EAX 0/1");
        c(ecx_after == uint32_t(reinterpret_cast<uintptr_t>(record)) && edx_after == uint32_t(reinterpret_cast<uintptr_t>(v)),
          "answer path: ECX/EDX as at the call");
        c((flags_after & 0x400) != 0, "answer path: DF returned as the site had it (popfd)");
        c(a.error == 0x145, "answer path: LastError unchanged");
        c(!std::memcmp(xmm_after, xmm_in, sizeof xmm_in), "answer path: XMM0-7 unchanged");
        return;
    }
    c(probe_calls == calls + 1 && eax_after == probe_answer, "the original ran once and its EAX returned");
    c(t_ecx == uint32_t(reinterpret_cast<uintptr_t>(record)) && t_edx == uint32_t(reinterpret_cast<uintptr_t>(v)),
      "original entered with ECX record, EDX view");
    c(t_esi == esi_after && t_ebx == ebx_after && t_edi == 0x5a5a2222 && t_ebp == 0x5a5a3333,
      "original entered with ESI/EBX/EDI/EBP");
    c(t_esp == esp_before - 4 && t_ret == uint32_t(reinterpret_cast<uintptr_t>(probe_site)) + 5,
      "popfd; jmp: ESP at the return address, [ESP] = site + 5");
    if (!native_probe_flags) native_probe_flags = t_flags; // the unpatched baseline
    c(t_flags == native_probe_flags && (t_flags & 0x400), "original entered with the site's EFLAGS (DF set)");
    c(!std::memcmp(xmm_at_target, xmm_in, sizeof xmm_in), "XMM0-7 reach the original");
    c(t_cw == test_cw && t_mxcsr == mx && ((t_sw >> 11) & 7) == 7, "x87 CW, MXCSR and the one value reach the original");
    c(a.error == 0x246 && !std::memcmp(xmm_after, xmm_out, sizeof xmm_out), "the original's LastError and XMM returned");
}
// One lens call through the fake pass. hooked: the bracket opens around the original.
void lens_call(uint32_t* v, bool hooked, const char* label) {
    char text[200];
    auto c = [&](bool value, const char* what) {
        std::snprintf(text, sizeof text, "%s: %s", label, what);
        check(value, text);
    };
    const uint32_t mx = test_mxcsr();
    const unsigned calls = lens_calls, begins = begin_calls, ends = end_calls;
    const Cpu saved = enter_test_state();
    lens(v);
    const After a = leave_test_state(saved);
    c(lens_calls == calls + 1, "the original ran once");
    c(begin_calls == begins + (hooked ? 1u : 0u) && end_calls == ends + (hooked ? 1u : 0u), "begin/end once each");
    c(bracket_at_target == hooked && !so::bracket_open(), "bracket open around the original only");
    c(esp_after == esp_before && ebp_after == 0x5a5a3333 && edi_after == 0x5a5a2222 && ebx_after == 0x5a5a6666 &&
          esi_after == uint32_t(reinterpret_cast<uintptr_t>(v)),
      "stack balanced, EBP/EDI/EBX/ESI kept");
    c(t_edx == uint32_t(reinterpret_cast<uintptr_t>(v)) && t_ecx == 0x5a5a1111, "original entered with EDX view, ECX padding");
    c(t_esi == esi_after && t_edi == 0x5a5a2222 && t_ebx == 0x5a5a6666 && t_ebp == 0x5a5a3333,
      "original entered with ESI/EDI/EBX/EBP");
    c(t_flags == flags_before, "original entered with the site's EFLAGS");
    c((hooked ? t_ret2 : t_ret) == uint32_t(reinterpret_cast<uintptr_t>(lens_site)) + 5, "returns to site + 5");
    c(!std::memcmp(xmm_at_target, xmm_in, sizeof xmm_in), "XMM0-7 reach the original");
    c(t_cw == test_cw && t_mxcsr == mx && ((t_sw >> 11) & 7) == 7, "x87 CW, MXCSR and the one value reach the original");
    c(eax_after == 0x0aa0beef && ecx_after == 0x0cc0ffee && edx_after == 0x0dd0beef &&
          (flags_after & 0x8d5) == (lens_out_flags & 0x8d5),
      "the original's EAX/ECX/EDX and flags returned");
    c(a.error == 0x246 && !std::memcmp(xmm_after, xmm_out, sizeof xmm_out), "the original's LastError and XMM returned");
    c(x87_after == 1.0 && a.cw == test_cw && a.mxcsr == mx, "x87 value, control word and MXCSR kept");
}
// A frame: probe(s), the lens bracket, Present.
void frame(int expect, const char* label, uint32_t* v = view) {
    probe(v, expect, label);
    lens_call(view, true, label);
    so::present(0);
}

extern "C" __attribute__((force_align_arg_pointer)) EXCEPTION_DISPOSITION __cdecl fl_fixture_handler(
    EXCEPTION_RECORD* rec, void* frame_, CONTEXT*, void*) {
    if (!(rec->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) && rec->ExceptionCode == 0xe3450001) {
        RtlUnwind(frame_, nullptr, nullptr, nullptr);
        void* previous = *static_cast<void**>(frame_);
        __asm__ __volatile__("movl %0,%%fs:0" ::"r"(previous));
        std::longjmp(outer_jump, 1);
    }
    return ExceptionContinueSearch;
}
extern "C" Lens fl_fixture_lens;
Lens fl_fixture_lens = nullptr;
// An outer SEH frame beyond the fake lens pass: the raise in the original unwinds through the thunk.
extern "C" __attribute__((naked)) uint32_t __cdecl catch_outer(uint32_t*) {
    __asm__ __volatile__("pushl %ebp\n\tmovl %esp,%ebp\n\tsubl $8,%esp\n\t"
                         "movl %fs:0,%eax\n\tmovl %eax,-8(%ebp)\n\tmovl $_fl_fixture_handler,-4(%ebp)\n\t"
                         "leal -8(%ebp),%eax\n\tmovl %eax,%fs:0\n\t"
                         "pushl 8(%ebp)\n\tcall *_fl_fixture_lens\n\taddl $4,%esp\n\t"
                         "movl -8(%ebp),%edx\n\tmovl %edx,%fs:0\n\tleave\n\tret\n\t");
}
DWORD WINAPI foreign_thread(void*) {
    record[L::record_visible / 4] = 1;
    SetLastError(0x145);
    walk(record, view);
    return 0;
}

so::Addresses addresses(bool fixture_main_view = true) {
    so::Addresses a{};
    a.probe_site = reinterpret_cast<uintptr_t>(probe_site);
    a.probe_target = reinterpret_cast<uintptr_t>(&fl_fake_probe);
    a.lens_site = reinterpret_cast<uintptr_t>(lens_site);
    a.lens_target = reinterpret_cast<uintptr_t>(&fl_fake_lens);
    a.main_view = fixture_main_view ? reinterpret_cast<const uintptr_t*>(&main_view_word) : nullptr;
    return a;
}
// The production gate (fixture_gate: the 17 anchors) against a synthetic image at the base VirtualAlloc picks.
void anchor_cases() {
    const SIZE_T span = (L::root_slot_rva + 0x2000) & ~SIZE_T(0xfff);
    auto* base = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE, PAGE_NOACCESS));
    check(base != nullptr, "synthetic image reserved");
    if (!base) return;
    const uintptr_t b = reinterpret_cast<uintptr_t>(base);
    bool committed = true;
    for (unsigned i = 0; i < L::anchor_count; ++i) {
        const L::Anchor& a = L::anchors[i];
        for (uint32_t page = a.rva & ~0xfffu; page < a.rva + a.length; page += 0x1000)
            committed = committed && VirtualAlloc(base + page, 0x1000, MEM_COMMIT, PAGE_READWRITE);
    }
    check(committed, "anchor pages committed");
    auto write = [&](uint32_t operand_delta) {
        for (unsigned i = 0; i < L::anchor_count; ++i) {
            const L::Anchor& a = L::anchors[i];
            std::memcpy(base + a.rva, a.bytes, a.length);
            for (unsigned k = 0; k < a.operands; ++k) {
                const uint32_t operand = uint32_t(b + a.operand_rva[k] + operand_delta);
                std::memcpy(base + a.rva + a.operand_offset[k], &operand, 4);
            }
        }
    };
    write(0);
    check(so::fixture_gate(b) == nullptr, "relocated synthetic image accepted at its base");
    check(so::fixture_gate(b + 0x10000) != nullptr, "the same bytes checked at another base refused");
    write(4);
    const char* foreign = so::fixture_gate(b);
    check(foreign && !std::strcmp(foreign, "probe_walk"), "an operand naming another slot refused");
    unsigned flips = 0, refused = 0, operand_flips = 0, operand_refused = 0;
    for (unsigned i = 0; i < L::anchor_count; ++i) {
        const L::Anchor& a = L::anchors[i];
        for (unsigned j = 0; j < a.length; ++j) {
            bool operand = false;
            for (unsigned k = 0; k < a.operands; ++k) operand = operand || (j >= a.operand_offset[k] && j < a.operand_offset[k] + 4);
            write(0);
            base[a.rva + j] ^= 0x01;
            const char* r = so::fixture_gate(b);
            if (operand) {
                ++operand_flips;
                operand_refused += r && !std::strcmp(r, a.name);
            } else {
                ++flips;
                refused += r && !std::strcmp(r, a.name);
            }
        }
    }
    write(0);
    check(flips == refused && operand_flips == operand_refused, "every single-bit flip refused, naming its anchor");
    std::printf("ANCHOR anchors=%u flips=%u refused=%u operand_flips=%u operand_refused=%u\n", L::anchor_count, flips,
                refused, operand_flips, operand_refused);
    VirtualFree(base + (L::anchors[2].rva & ~0xfffu), 0x1000, MEM_DECOMMIT);
    const char* unreadable = so::fixture_gate(b);
    check(unreadable && !std::strcmp(unreadable, "probe_gates"), "an unreadable anchor refused");
    VirtualFree(base, 0, MEM_RELEASE);
}
// The FL registry walk (cockpit registry -> active handle -> bucket chain -> cockpit +0x58 -> sector camera).
uint32_t registry_obj[0x20 / 4], map_header[2], buckets[4], link_a[3], link_b[3], cockpit[0x60 / 4], registry_slot;
void main_view_cases() {
    const auto u = [](const void* p) { return uint32_t(reinterpret_cast<uintptr_t>(p)); };
    registry_slot = u(registry_obj);
    registry_obj[0] = u(map_header);
    registry_obj[0x10 / 4] = 0x1235; // the active handle
    map_header[0] = u(buckets);
    map_header[1] = 4;
    buckets[(4 - 1) & 0x1235] = u(link_a);
    link_a[0] = u(link_b), link_a[1] = 0x9999, link_a[2] = 0; // another key first
    link_b[0] = 0, link_b[1] = 0x1235, link_b[2] = u(cockpit);
    cockpit[0x58 / 4] = u(view);
    view[L::view_flags / 4] = 0x10001; // sector camera
    so::fixture_slots(u(&registry_slot));
    check(so::fixture_main_view() == reinterpret_cast<uintptr_t>(view), "registry walk finds the cockpit camera");
    view[L::view_flags / 4] = 0x1;
    check(so::fixture_main_view() == 0, "no sector-camera bit: no main view");
    view[L::view_flags / 4] = 0x10001;
    map_header[1] = 3;
    check(so::fixture_main_view() == 0, "a capacity that is not a power of two: no main view");
    map_header[1] = 4;
    link_b[1] = 0x1236;
    check(so::fixture_main_view() == 0, "handle not in the chain: no main view");
    link_b[1] = 0x1235;
    so::fixture_slots(0);
    check(so::fixture_main_view() == 0, "no admitted registry slot: no main view");
    so::fixture_slots(u(&registry_slot));
}

double time_probes(uint32_t* v, unsigned iterations) {
    LARGE_INTEGER frequency{}, begin{}, end{};
    QueryPerformanceFrequency(&frequency);
    target_mode = 3;
    QueryPerformanceCounter(&begin);
    for (unsigned i = 0; i < iterations; ++i) {
        record[L::record_visible / 4] = 1;
        walk(record, v);
    }
    QueryPerformanceCounter(&end);
    target_mode = 0;
    asm volatile("fninit");
    return double(end.QuadPart - begin.QuadPart) * 1e9 / double(frequency.QuadPart) / iterations;
}
double time_lens(unsigned iterations) {
    LARGE_INTEGER frequency{}, begin{}, end{};
    QueryPerformanceFrequency(&frequency);
    target_mode = 3;
    QueryPerformanceCounter(&begin);
    for (unsigned i = 0; i < iterations; ++i) lens(view);
    QueryPerformanceCounter(&end);
    target_mode = 0;
    asm volatile("fninit");
    return double(end.QuadPart - begin.QuadPart) * 1e9 / double(frequency.QuadPart) / iterations;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    make_objects();
    auto* page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!page) return 2;
    unsigned char* next = emit_walk(page, reinterpret_cast<void*>(&fl_fake_probe), &probe_site);
    unsigned char* lens_page = page + ((next - page + 63) & ~63);
    emit_lens(lens_page, reinterpret_cast<void*>(&fl_fake_lens), &lens_site);
    DWORD old = 0;
    VirtualProtect(page, 4096, PAGE_EXECUTE_READWRITE, &old);
    walk = reinterpret_cast<Walk>(page);
    lens = reinterpret_cast<Lens>(lens_page);
    fl_fixture_lens = lens;
    check((reinterpret_cast<uintptr_t>(probe_site) & 7) == 2 && (reinterpret_cast<uintptr_t>(lens_site) & 7) == 7,
          "probe call one word (as base+0x8af0a), lens call straddling a qword (as base+0x8bcb7)");
    unsigned char probe_original[5], lens_original[5];
    std::memcpy(probe_original, probe_site, 5);
    std::memcpy(lens_original, lens_site, 5);
    const auto unchanged = [&]() {
        return !std::memcmp(probe_site, probe_original, 5) && !std::memcmp(lens_site, lens_original, 5);
    };
    so::set_listener(&begin_listener, &end_listener);
    // Unpatched baseline through the same fake walk and pass.
    probe_answer = 1;
    probe(view, 2, "unpatched probe hide");
    probe_answer = 0;
    probe(view, 2, "unpatched probe show");
    lens_call(view, false, "unpatched lens");

    // Production initialize in a non-X3FL process. Only the bracket (the lens-flare gain or the fog) installs on FL: the
    // own sun is inert here and its log alone installs nothing.
    check(!so::initialize(false) && !std::strcmp(so::state(), "disabled"), "nothing requested: disabled, no row");
    SetEnvironmentVariableW(L"X3M_OWN_SUN_LOG", L"1");
    const unsigned rows_before = rows;
    check(!so::initialize(false) && !std::strcmp(so::state(), "disabled") && rows == rows_before,
          "the log alone: disabled, no row");
    check(!so::initialize(true) && row_has("fl.sun_occlusion status=executable_mismatch ") && row_has(" log=1 bracket=1 "),
          "bracket for the fog/gain: one row, executable_mismatch");
    SetEnvironmentVariableW(L"X3M_OWN_SUN_LOG", nullptr);
    check(unchanged() && !so::installed(), "refusals wrote nothing");

    // install_at refusals and the rollback of the first claim.
    {
        so::Addresses a = addresses();
        a.probe_target = reinterpret_cast<uintptr_t>(&fl_other_target);
        check(!so::install_at(a, false) && !std::strcmp(so::state(), "target_mismatch") && unchanged() &&
                  !so::installed(),
              "probe claim refused after the lens claim: the lens call rolled back byte for byte");
        a = addresses();
        a.lens_target = reinterpret_cast<uintptr_t>(&fl_other_target);
        check(!so::install_at(a, false) && !std::strcmp(so::state(), "target_mismatch") && unchanged(),
              "lens claim refused, nothing written");
        a = addresses();
        a.lens_site += 1;
        check(!so::install_at(a, false) && unchanged(), "lens site one byte off refused");
        a = addresses();
        a.probe_site = 0;
        check(!so::install_at(a, false) && !std::strcmp(so::state(), "invalid_site") && unchanged(), "no probe site");
    }

    // ---- installed: every probe forwards, the bracket opens around the lens pass ----------------------------------
    main_view_cases();
    check(so::install_at(addresses(), false) && so::installed() && !so::logging() && !std::strcmp(so::state(), "active"),
          "both calls claimed");
    so::fixture_log_status();
    check(row_has("fl.sun_occlusion status=active ") && row_has(" probe=base+0x8af0a lens=base+0x8bcb7 ") &&
              row_has(" probe_write=atomic lens_write=plain ") && row_has(" log=0 bracket=0 ") && row_has(" anchors=17 "),
          "status row: sites, write modes, anchors");
    check(probe_site[0] == 0xe8 && lens_site[0] == 0xe8 && !unchanged(), "only the rel32s rewritten");
    probe_answer = 1;
    frame(2, "eligible probe forwards (hide)");
    probe_answer = 0;
    frame(2, "eligible probe forwards (show)");
    // The flare option off or the view's hide flag: still the original's answer (no gate is replicated on FL).
    config[L::config_video_flags / 4] = 0;
    view[L::view_flags / 4] |= L::view_flag_probe_hidden;
    frame(2, "flags never read: the original decides");
    config[L::config_video_flags / 4] = L::video_flag_flares;
    view[L::view_flags / 4] &= ~L::view_flag_probe_hidden;
    // Ineligible probes forward too: another view, a record the main view owns, an owner without the background bit.
    probe_answer = 1;
    probe(other_view, 2, "another view's probe forwards");
    record[L::record_owner / 4] = uint32_t(reinterpret_cast<uintptr_t>(view));
    probe(view, 2, "a record the main view owns forwards");
    record[L::record_owner / 4] = uint32_t(reinterpret_cast<uintptr_t>(owner));
    owner[L::view_flags / 4] = 0x135;
    probe(view, 2, "an owner without the background bit forwards");
    owner[L::view_flags / 4] = 0x00400135;
    lens_call(view, true, "bracket after the probes");
    so::present(0);

    // ---- unwind through the bracket ------------------------------------------------------------------------------
    {
        probe_answer = 0;
        probe(view, 2, "probe before the raise");
        const unsigned ends = end_calls, calls = lens_calls;
        target_mode = 2;
        SetLastError(0x145);
        const int caught = setjmp(outer_jump);
        if (!caught) catch_outer(view);
        target_mode = 0;
        asm volatile("fninit\n\tcld");
        check(caught == 1 && lens_calls == calls + 1 && end_calls == ends, "SEH reached the outer handler; no end");
        check(so::bracket_open(), "an unwind leaves the bracket open until Present");
        so::present(0);
        check(!so::bracket_open(), "Present closes the bracket");
        frame(2, "after the unwind the probe forwards");
    }

    // ---- a probe from another thread runs the original ----------------------------------------------------------
    {
        const unsigned calls = probe_calls;
        HANDLE thread = CreateThread(nullptr, 0, &foreign_thread, nullptr, 0, nullptr);
        check(thread && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0, "foreign thread ran");
        if (thread) CloseHandle(thread);
        check(probe_calls == calls + 1 && so::counters().foreign_thread == 1, "foreign probe: the original, counted");
        so::present(0);
        check(row_has("own_sun_hook_foreign_thread ") && row_has(" calls=1 "), "foreign thread row");
    }

    // ---- the periodic row -----------------------------------------------------------------------------------------
    {
        frame(2, "after the probe-less frame the probe forwards");
        check(so::fixture_arm_sample() == 10, "X3M_FL_SUN_OCCLUSION_LOG unset: 10 s");
        frame(2, "sample frame 1");
        probe(other_view, 2, "sample frame 2 ineligible");
        lens_call(view, true, "sample frame 2 bracket");
        so::fixture_force_sample();
        so::present(0);
        check(row_has("fl.sun_occlusion_sample row=1 status=active frames=2 probes=2 own=0 hidden=0 original=2 "
                      "foreign=1 brackets=2 main_view=0x00000000"),
              "fl.sun_occlusion_sample counts (no main-view walk without the log)");
        SetEnvironmentVariableW(L"X3M_FL_SUN_OCCLUSION_LOG", L"0");
        check(so::fixture_arm_sample() == 0, "X3M_FL_SUN_OCCLUSION_LOG=0: off");
        const unsigned before = rows;
        so::fixture_force_sample();
        so::present(0);
        check(rows == before, "no row when off");
        SetEnvironmentVariableW(L"X3M_FL_SUN_OCCLUSION_LOG", nullptr);
    }
    check(so::shutdown() && unchanged() && !so::installed() && !std::strcmp(so::state(), "restored"), "exact restore");
    lens_call(view, false, "lens after restore");
    probe_answer = 0;
    probe(view, 2, "probe after restore");

    // ---- the registry main view and the log mode (the original via the register ABI) -------------------------------
    check(so::install_at(addresses(false), true) && so::logging(), "install with the registry walk and the log");
    check_probe_error = false; // the diagnostic calls the original from inside the handler
    // The diagnostic runs the original itself (ECX record, EDX view, as fastcall) and returns its answer: the thunk
    // never forwards.
    for (unsigned k = 0; k < 2; ++k) {
        probe_answer = k == 0 ? 1 : 0;
        const unsigned calls = probe_calls;
        record[L::record_visible / 4] = 1;
        const Cpu saved = enter_test_state();
        const uint32_t visible = walk(record, view);
        const After a = leave_test_state(saved);
        check(probe_calls == calls + 1 && t_ecx == uint32_t(reinterpret_cast<uintptr_t>(record)) &&
                  t_edx == uint32_t(reinterpret_cast<uintptr_t>(view)),
              "log mode: the original ran once through the register ABI");
        check(eax_after == probe_answer && visible == (probe_answer ? 0u : 1u) && a.error == 0x145 &&
                  !std::memcmp(xmm_after, xmm_in, sizeof xmm_in) && esp_after == esp_before,
              "log mode: answered by the thunk, LastError and XMM kept");
        check(row_has("sun_probe ") && row_has(" owner_layer=15 ") && row_has(" eligible=1 ") &&
                  row_has(k == 0 ? " vanilla=1 answer=1" : " vanilla=0 answer=0"),
              "sun_probe row: the registry main view, the owner's layer, the original's answer");
        lens_call(view, true, "log mode bracket");
        so::present(0);
    }
    check_probe_error = true;
    check(so::shutdown() && unchanged(), "restore after the log mode");

    anchor_cases();

    // Cost per call: unpatched, forwarded (another view's probe and the main view's eligible one); lens unpatched and
    // bracketed (trivial listeners).
    {
        clobber = false;
        const unsigned iterations = 200000;
        const double native_probe = time_probes(view, iterations), native_lens = time_lens(iterations);
        check(so::install_at(addresses(), false), "timing install");
        probe_answer = 0;
        frame(2, "timing warm");
        const double forward = time_probes(other_view, iterations);
        const double eligible = time_probes(view, iterations);
        const double bracket = time_lens(iterations);
        so::present(0);
        check(so::shutdown() && unchanged(), "timing restore");
        clobber = true;
        std::printf("TIMING native_probe_ns=%.1f forward_ns=%.1f eligible_forward_ns=%.1f native_lens_ns=%.1f bracket_ns=%.1f "
                    "forward_hook_ns=%.1f bracket_hook_ns=%.1f\n",
                    native_probe, forward, eligible, native_lens, bracket, forward - native_probe, bracket - native_lens);
    }
    // Late window: after the first Present closes the engine_patch window, both claims are refused, nothing written
    // (the lens call's plain write is never attempted outside the window).
    x3m::engine_patch::close_install_window("fixture_first_present");
    check(!so::install_at(addresses(), false) && !std::strcmp(so::state(), "late_claim") && unchanged() &&
              !so::installed(),
          "late claim refused, nothing written");
    {
        x3m::engine_patch::CallSite site{};
        check(!x3m::engine_patch::claim_call(site, reinterpret_cast<uintptr_t>(lens_site),
                                             reinterpret_cast<uintptr_t>(&fl_fake_lens),
                                             reinterpret_cast<void*>(&fl_other_target)) &&
                  !std::strcmp(site.status, "late_claim") && unchanged(),
              "engine_patch refuses the straddling call after the window too");
    }
    VirtualFree(page, 0, MEM_RELEASE);
    std::printf("RESULT %s checks=%u failures=%u probe_calls=%u lens_calls=%u\n", failures ? "FAIL" : "PASS", checks,
                failures, probe_calls, lens_calls);
    return failures ? 1 : 0;
}
