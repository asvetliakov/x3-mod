// Original synthetic fixture of the FL scene-end hook (src/engine/fl/scene_hook_fl.cpp) and the FL compositor
// owner read (src/engine/fl/compositor_owner_fl.cpp), docs/architecture/farnhams-legacy-scene.md: no game code,
// no game bytes beyond the generated window/anchor constants, no launch. Built with -DX3M_EDITION_FL=1
// -DX3M_SCENE_HOOK_FIXTURE and linked with the production compositor bridge (compositor_bridge.S and its clang SEH
// unit) by verification/analysis/test_fl_scene_hook.py.
//
// A fake complete pass mirrors X3FL.exe's (compositor.md section 1): done byte [ebp-0xd], `cmp byte
// [ebp-0xd],0; jne`, then the generated 24-byte boundary window (cmp dword [esi+0x29c],0x11; jle +9; call rel32;
// mov byte [ebp-0xd],1; mov ecx,[esi+0x270]) with the rel32 aimed at a fake compositor. It loads sentinels into
// ECX/EDX/EDI/EBX, ESI = camera, XMM0-7 and one x87 value, and stores every register, EFLAGS, XMM0-7, the x87
// value and the done byte after the window. The fake compositor starts with the 14 gated entry bytes (push ebp;
// lea ebp,[esp-0x38]; mov eax,fs:[0]; sub esp,0x38), records its entry state, calls a C body, returns EAX/EDX,
// XMM0-7 and EFLAGS sentinels with a plain RET. The listener and the bridge's pre callback clobber XMM0-7, the
// x87 stack/control word, MXCSR and LastError; the hook must hide all of it.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <excpt.h>
#include <cstdarg>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../../src/proxy/scene_hook.h"
#include "../../src/proxy/compositor_owner.h"
#include "../../src/proxy/engine_patch.h"
namespace layout {
#include "../../src/engine/fl/scene_hook_fl_inc.h"
}
namespace sh = x3m::scene_hook;
namespace co = x3m::compositor_owner;
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
extern volatile long x3m_compositor_bridge_binding_bits;
// Written by the fake caller / compositor (absolute addresses in the generated caller bytes and the asm).
uint32_t esp_before = 0, esp_after = 0, ebp_before = 0, ebp_after = 0, esi_after = 0, edi_after = 0, ebx_after = 0,
         ecx_after = 0, edx_after = 0, eax_after = 0, flags_after = 0;
uint32_t entry_ecx = 0, entry_edx = 0, entry_esi = 0, entry_edi = 0, entry_ebx = 0, entry_ebp = 0, entry_return = 0;
uint16_t entry_cw = 0, entry_sw = 0;
uint32_t entry_mxcsr = 0, out_flags = 0x2c3; // IF|CF|ZF|SF (reserved bit 1 set)
double x87_after = 0;
alignas(16) uint32_t xmm_in[32], xmm_at_target[32], xmm_out[32], xmm_after[32];
void __cdecl fl_fake_compositor_body();
}
asm(".text\n\t.globl _fl_fake_compositor\n_fl_fake_compositor:\n\t"
    ".byte 0x55,0x8d,0x6c,0x24,0xc8,0x64,0xa1,0x00,0x00,0x00,0x00,0x83,0xec,0x38\n\t"
    "movl %ecx,_entry_ecx\n\tmovl %edx,_entry_edx\n\tmovl %esi,_entry_esi\n\tmovl %edi,_entry_edi\n\t"
    "movl %ebx,_entry_ebx\n\tmovl 0x38(%ebp),%eax\n\tmovl %eax,_entry_ebp\n\tmovl 0x3c(%ebp),%eax\n\t"
    "movl %eax,_entry_return\n\t"
    "movups %xmm0,_xmm_at_target\n\tmovups %xmm1,_xmm_at_target+16\n\tmovups %xmm2,_xmm_at_target+32\n\t"
    "movups %xmm3,_xmm_at_target+48\n\tmovups %xmm4,_xmm_at_target+64\n\tmovups %xmm5,_xmm_at_target+80\n\t"
    "movups %xmm6,_xmm_at_target+96\n\tmovups %xmm7,_xmm_at_target+112\n\t"
    "fnstcw _entry_cw\n\tfnstsw _entry_sw\n\tstmxcsr _entry_mxcsr\n\t"
    "andl $-16,%esp\n\tcall _fl_fake_compositor_body\n\t"
    "movups _xmm_out,%xmm0\n\tmovups _xmm_out+16,%xmm1\n\tmovups _xmm_out+32,%xmm2\n\tmovups _xmm_out+48,%xmm3\n\t"
    "movups _xmm_out+64,%xmm4\n\tmovups _xmm_out+80,%xmm5\n\tmovups _xmm_out+96,%xmm6\n\tmovups _xmm_out+112,%xmm7\n\t"
    "movl $0x0dd0beef,%edx\n\tmovl $0x0cc0ffee,%ecx\n\tpushl _out_flags\n\tpopfl\n\tmovl $0x0aa0beef,%eax\n\t"
    "leal 0x38(%ebp),%esp\n\tpopl %ebp\n\tret\n\t"
    // A compositor with another prologue (the entry gate must refuse it).
    ".globl _fl_other_compositor\n_fl_other_compositor:\n\tpushl %ebp\n\tmovl %esp,%ebp\n\tpopl %ebp\n\tret\n\t");
extern "C" void fl_fake_compositor();
extern "C" void fl_other_compositor();

using Caller = uint32_t(__cdecl*)(uint32_t* camera, uint32_t done);
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
};
// Emits the fake complete pass at `at`; *window receives the boundary window's address.
unsigned char* emit_caller(unsigned char* at, const void* target, unsigned char** window) {
    Emit e{at};
    e.b({0x55, 0x89, 0xe5, 0x83, 0xec, 0x10, 0x53, 0x56, 0x57}); // push ebp; mov ebp,esp; sub esp,16; push ebx/esi/edi
    e.b({0x8b, 0x45, 0x0c, 0x88, 0x45, 0xf3});                   // mov eax,[ebp+0xc]; mov [ebp-0xd],al
    e.abs({0x89, 0x25}, &esp_before);
    e.abs({0x89, 0x2d}, &ebp_before);
    for (unsigned r = 0; r < 8; ++r) e.abs({0x0f, 0x10, 0x05 + 8 * r}, xmm_in + 4 * r); // movups xmmR,[xmm_in+16R]
    e.b({0xd9, 0xe8});             // fld1
    e.b({0x8b, 0x75, 0x08});       // mov esi,[ebp+8] (camera)
    e.b({0xbf}), e.d(0x5a5a2222);  // mov edi
    e.b({0xbb}), e.d(0x5a5a3333);  // mov ebx
    e.b({0xb9}), e.d(0x5a5a1111);  // mov ecx
    e.b({0xba}), e.d(0x5a5a4444);  // mov edx
    e.b({0x80, 0x7d, 0xf3, 0x00}); // cmp byte [ebp-0xd],0
    e.b({0x75, 0x12});             // jne window+0x12 (the done byte skips the boundary test, as at RVA 0x8b91e)
    *window = e.p;
    std::memcpy(e.p, layout::window, sizeof layout::window);
    const uint32_t rel = uint32_t(reinterpret_cast<uintptr_t>(target) -
                                  (reinterpret_cast<uintptr_t>(e.p) + layout::window_site_offset + 5));
    std::memcpy(e.p + layout::window_site_offset + 1, &rel, 4);
    e.p += sizeof layout::window;
    e.b({0x9c});
    e.abs({0x8f, 0x05}, &flags_after); // pushfd; pop [flags_after]
    e.abs({0x89, 0x25}, &esp_after);
    e.abs({0x89, 0x2d}, &ebp_after);
    e.abs({0x89, 0x35}, &esi_after);
    e.abs({0x89, 0x3d}, &edi_after);
    e.abs({0x89, 0x1d}, &ebx_after);
    e.abs({0x89, 0x0d}, &ecx_after);
    e.abs({0x89, 0x15}, &edx_after);
    e.abs({0xa3}, &eax_after);
    for (unsigned r = 0; r < 8; ++r) e.abs({0x0f, 0x11, 0x05 + 8 * r}, xmm_after + 4 * r); // movups [..],xmmR
    e.abs({0xdd, 0x1d}, &x87_after);            // fstp qword [x87_after]
    e.b({0x0f, 0xb6, 0x45, 0xf3});              // movzx eax,byte [ebp-0xd]
    e.b({0x5f, 0x5e, 0x5b, 0xc9, 0xc3});        // pop edi/esi/ebx; leave; ret
    return e.p;
}

constexpr uint32_t camera_flags = 0x000f1a95;
uint32_t camera_high[0x2a0 / 4]{}, camera_low[0x2a0 / 4]{}, camera_edge[0x2a0 / 4]{};
Caller invoke = nullptr;
unsigned char* site = nullptr;
unsigned target_calls = 0, listener_calls = 0, pre_calls = 0, post_calls = 0, cleanup_calls = 0, abnormal_calls = 0;
unsigned target_mode = 0; // 0 plain, 1 nested complete pass, 2 raise, 3 timing (no checks)
bool bridge_path = false, clobber = true;
uint32_t expected_return = 0;
extern "C" void __cdecl fl_fake_compositor_body() {
    ++target_calls;
    if (target_mode == 3) return;
    check(GetLastError() == 0x145, "compositor entry LastError preserved");
    if (target_mode == 1) {
        // A nested complete pass inside the compositor: its witness words are the outer call's.
        const uint32_t outer[] = {esp_before, ebp_before, entry_ecx, entry_edx, entry_esi, entry_edi, entry_ebx,
                                  entry_ebp, entry_return};
        const uint16_t cw = entry_cw, sw = entry_sw;
        const uint32_t mxcsr = entry_mxcsr;
        uint32_t at_target[32];
        std::memcpy(at_target, xmm_at_target, sizeof at_target);
        target_mode = 0;
        SetLastError(0x145);
        const uint32_t done = invoke(camera_high, 0);
        target_mode = 1;
        check(done == 1, "nested pass reached its compositor call");
        esp_before = outer[0], ebp_before = outer[1], entry_ecx = outer[2], entry_edx = outer[3], entry_esi = outer[4];
        entry_edi = outer[5], entry_ebx = outer[6], entry_ebp = outer[7], entry_return = outer[8];
        entry_cw = cw, entry_sw = sw, entry_mxcsr = mxcsr;
        std::memcpy(xmm_at_target, at_target, sizeof at_target);
    }
    if (target_mode == 2) RaiseException(0xe3450001, 0, 0, nullptr);
    SetLastError(0x246);
}
// Every volatile piece of CPU state the hook must hide from the engine.
void clobber_cpu() {
    if (!clobber) return;
    const uint16_t cw = 0x0c7f;
    const uint32_t mxcsr = 0x9fc0; // FTZ, DAZ, round toward zero, all masked
    asm volatile("pcmpeqd %%xmm0,%%xmm0\n\tpcmpeqd %%xmm1,%%xmm1\n\tpxor %%xmm2,%%xmm2\n\tpcmpeqd %%xmm3,%%xmm3\n\t"
                 "pxor %%xmm4,%%xmm4\n\tpcmpeqd %%xmm5,%%xmm5\n\tpcmpeqd %%xmm6,%%xmm6\n\tpxor %%xmm7,%%xmm7\n\t"
                 "fldpi\n\tfldpi\n\tfldcw %0\n\tldmxcsr %1" ::"m"(cw),
                 "m"(mxcsr)
                 : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "st", "st(1)");
    SetLastError(0x999);
}
uint16_t listener_sw = 0xffff, listener_cw = 0;
void listener() {
    ++listener_calls;
    asm volatile("fnstsw %0\n\tfnstcw %1" : "=m"(listener_sw), "=m"(listener_cw));
    if (target_mode != 3) {
        check(listener_sw == 0 && listener_cw == 0x037f, "listener runs on an initialised x87 (empty stack, default CW)");
        check(sh::signals() >= 1, "signal counted before the listener");
    }
    clobber_cpu();
}
void pre(const X3mCompositorFrame* frame, void*, void*) {
    ++pre_calls;
    if (target_mode != 3) {
        check(frame->caller_pc == sh::compositor_caller_pc() &&
                  frame->caller_pc == reinterpret_cast<uintptr_t>(site) + 5,
              "bridge caller_pc is the site + 5");
        check(frame->input.regs[1] == reinterpret_cast<uintptr_t>(camera_high), "bridge input ESI (camera)");
        check(frame->input.regs[0] == 0x5a5a2222 && frame->input.regs[4] == 0x5a5a3333 &&
                  frame->input.regs[5] == 0x5a5a4444 && frame->input.regs[6] == 0x5a5a1111,
              "bridge input EDI/EBX/EDX/ECX");
        check(!sh::shutdown() && sh::active(), "shutdown refused while the bridge is active");
    }
    clobber_cpu();
}
void post(const X3mCompositorFrame* frame, void*, void*) {
    ++post_calls;
    if (target_mode != 3)
        check((frame->flags & X3M_CB_ORIGINAL_RETURNED) && frame->output.regs[7] == 0x0aa0beef &&
                  frame->output.regs[5] == 0x0dd0beef,
              "bridge post sees the compositor's EAX/EDX");
    clobber_cpu();
}
void cleanup(const X3mCompositorFrame*, void*, void*, int abnormal) {
    ++cleanup_calls;
    abnormal_calls += abnormal != 0;
}
const X3mCompositorBinding callbacks{nullptr, &pre, &post, &cleanup, nullptr};
const X3mCompositorBinding broken_callbacks{nullptr, &pre, &post, nullptr, nullptr};

std::jmp_buf outer_jump;
extern "C" __attribute__((force_align_arg_pointer)) EXCEPTION_DISPOSITION __cdecl fl_fixture_handler(
    EXCEPTION_RECORD* record, void* frame, CONTEXT*, void*) {
    if (!(record->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) &&
        record->ExceptionCode == 0xe3450001) {
        RtlUnwind(frame, nullptr, nullptr, nullptr);
        void* previous = *static_cast<void**>(frame);
        __asm__ __volatile__("movl %0,%%fs:0" ::"r"(previous));
        std::longjmp(outer_jump, 1);
    }
    return ExceptionContinueSearch;
}
extern "C" Caller fl_fixture_invoke;
Caller fl_fixture_invoke = nullptr;
// An outer SEH frame beyond the fake complete pass: the raise unwinds through the bridge's own registration
// (bridge path) or through no frame of the hook at all (signal path: the trampoline tail-jumps).
extern "C" __attribute__((naked)) uint32_t __cdecl catch_outer(uint32_t*, uint32_t) {
    __asm__ __volatile__("pushl %ebp\n\tmovl %esp,%ebp\n\tsubl $8,%esp\n\t"
                         "movl %fs:0,%eax\n\tmovl %eax,-8(%ebp)\n\tmovl $_fl_fixture_handler,-4(%ebp)\n\t"
                         "leal -8(%ebp),%eax\n\tmovl %eax,%fs:0\n\t"
                         "pushl 12(%ebp)\n\tpushl 8(%ebp)\n\tcall *_fl_fixture_invoke\n\taddl $8,%esp\n\t"
                         "movl -8(%ebp),%edx\n\tmovl %edx,%fs:0\n\tleave\n\tret\n\t");
}

struct Counts {
    unsigned target, listener, pre, post, cleanup;
    unsigned long long signals;
};
Counts counts() {
    return {target_calls, listener_calls, pre_calls, post_calls, cleanup_calls, sh::signals()};
}
// One pass through the fake complete pass with the full register/EFLAGS/XMM/x87/MXCSR/LastError witness.
// calls: compositor calls expected (0: layer <= 17 or the done byte set; 2: the compositor runs a nested pass).
// hooked: each call signals.
void witnessed(uint32_t* camera, uint32_t done, unsigned calls, bool hooked, const char* label) {
    const bool expect_call = calls != 0;
    for (unsigned i = 0; i < 32; ++i) {
        xmm_in[i] = 0xa5000000u + i + camera[0x29c / 4] * 64;
        xmm_out[i] = 0x5a000000u + i + camera[0x29c / 4] * 64;
    }
    uint16_t cw = 0, test_cw = 0x027f; // 53-bit precision instead of the default 64
    uint32_t mxcsr = 0, test_mxcsr = 0;
    asm volatile("fnstcw %0\n\tstmxcsr %1" : "=m"(cw), "=m"(mxcsr));
    test_mxcsr = (mxcsr & ~0x6000u) | 0x6000u; // round toward zero
    const Counts before = counts();
    entry_sw = 0xffff;
    SetLastError(0x145);
    asm volatile("fldcw %0\n\tldmxcsr %1" ::"m"(test_cw), "m"(test_mxcsr));
    const uint32_t result = invoke(camera, done);
    uint16_t cw_after = 0;
    uint32_t mxcsr_after = 0;
    asm volatile("fnstcw %0\n\tstmxcsr %1" : "=m"(cw_after), "=m"(mxcsr_after));
    const DWORD error_after = GetLastError();
    asm volatile("fldcw %0\n\tldmxcsr %1" ::"m"(cw), "m"(mxcsr));
    const Counts after = counts();
    char text[160];
    auto c = [&](bool value, const char* what) {
        std::snprintf(text, sizeof text, "%s: %s", label, what);
        check(value, text);
    };
    c(esp_after == esp_before && ebp_after == ebp_before, "stack balanced, EBP kept");
    c(esi_after == reinterpret_cast<uintptr_t>(camera) && edi_after == 0x5a5a2222 && ebx_after == 0x5a5a3333,
      "ESI/EDI/EBX kept");
    c(ecx_after == camera_flags, "the window's flags reload ran (ECX = camera+0x270)");
    c(x87_after == 1.0, "x87 stack value kept");
    c(cw_after == test_cw && mxcsr_after == test_mxcsr, "x87 control word and MXCSR returned");
    if (!expect_call) {
        c(result == done && after.target == before.target && after.signals == before.signals &&
              after.listener == before.listener && after.pre == before.pre,
          "no compositor call, no signal");
        c(error_after == 0x145 && edx_after == 0x5a5a4444 && !std::memcmp(xmm_after, xmm_in, sizeof xmm_in),
          "skipped call: LastError, EDX, XMM untouched");
        return;
    }
    c(result == 1, "done byte set after the call");
    c(after.target == before.target + calls, "compositor called once per pass");
    c(after.signals == before.signals + (hooked ? calls : 0u), "one signal per call");
    if (hooked && !bridge_path) c(after.listener == before.listener + calls, "listener ran once per call");
    if (hooked && bridge_path)
        c(after.pre == before.pre + calls && after.post == before.post + calls &&
              after.cleanup == before.cleanup + calls,
          "bridge pre/post/cleanup once per call");
    c(error_after == 0x246, "compositor LastError returned");
    c(eax_after == 0x0aa0beef && edx_after == 0x0dd0beef, "compositor EAX/EDX returned");
    c((flags_after & 0x8d5) == (out_flags & 0x8d5), "compositor arithmetic flags returned");
    c(entry_ecx == 0x5a5a1111 && entry_edx == 0x5a5a4444 && entry_esi == reinterpret_cast<uintptr_t>(camera) &&
          entry_edi == 0x5a5a2222 && entry_ebx == 0x5a5a3333 && entry_ebp == ebp_before,
      "ECX/EDX/ESI/EDI/EBX/EBP reach the compositor");
    if (!bridge_path) c(entry_return == reinterpret_cast<uintptr_t>(site) + 5, "signal path: same stack, returns to site+5");
    c(!std::memcmp(xmm_at_target, xmm_in, sizeof xmm_in), "XMM0-7 reach the compositor");
    c(!std::memcmp(xmm_after, xmm_out, sizeof xmm_out), "XMM0-7 of the return reach the caller");
    c(entry_cw == test_cw && entry_mxcsr == test_mxcsr, "x87 control word and MXCSR reach the compositor");
    c(((entry_sw >> 11) & 7) == 7, "the caller's x87 stack (one value) reaches the compositor");
}
uint32_t* make_camera(uint32_t* camera, uint32_t layer) {
    camera[0x29c / 4] = layer;
    camera[0x270 / 4] = camera_flags;
    return camera;
}
void present() {
    sh::fl_sample_row();
}
// The raise inside the compositor reaches an outer handler; the hook leaves no scope behind.
void unwind_case(const char* label) {
    const Counts before = counts();
    target_mode = 2;
    SetLastError(0x145);
    const int caught = setjmp(outer_jump);
    if (!caught) catch_outer(camera_high, 0);
    target_mode = 0;
    asm volatile("fninit"); // the caller's fld1 was never popped
    const Counts after = counts();
    char text[160];
    std::snprintf(text, sizeof text, "%s: SEH reached the outer handler, compositor ran once", label);
    check(caught == 1 && after.target == before.target + 1, text);
    if (bridge_path) {
        check(after.cleanup == before.cleanup + 1 && after.post == before.post && abnormal_calls >= 1 &&
                  x3m_compositor_bridge_active() == 0,
              "bridge unwind: cleanup once (abnormal), no post, active count back to 0");
    }
    check(sh::active(), "hook still active after the unwind");
    witnessed(camera_high, 0, 1, true, "after unwind");
}
// The production gate (fixture_gate: window, entry and the three anchors, exact) against a synthetic image at the
// base VirtualAlloc picks: only the anchor pages are committed, holding the generated bytes with each disp32
// relocated to base + slot. It must pass, refuse the same bytes at another base, a foreign operand and every
// single-byte flip outside the operands. The same image then carries the owner/glow chain for the owner reads.
void anchor_and_owner_cases() {
    const SIZE_T span = (layout::renderer_slot_rva + 0x2000) & ~SIZE_T(0xfff);
    auto* base = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE, PAGE_NOACCESS));
    check(base != nullptr, "synthetic image reserved");
    if (!base) return;
    const uintptr_t b = reinterpret_cast<uintptr_t>(base);
    bool committed = true;
    for (uint32_t rva : {layout::window_rva, layout::target_rva, layout::config_slot_rva, layout::renderer_slot_rva})
        committed = committed && VirtualAlloc(base + (rva & ~0xfffu), 0x1000, MEM_COMMIT, PAGE_READWRITE);
    check(committed, "anchor pages committed");
    struct Span {
        uint32_t rva;
        const uint8_t* bytes;
        unsigned length, operand; // operand: offset of the relocated disp32, ~0u none
        uint32_t slot;
    } spans[] = {{layout::window_rva, layout::window, sizeof layout::window, ~0u, 0},
                 {layout::target_rva, layout::target_entry, sizeof layout::target_entry, ~0u, 0},
                 {layout::glow_anchor_rva, layout::glow_anchor, sizeof layout::glow_anchor, layout::glow_operand_offset,
                  layout::config_slot_rva},
                 {layout::owner_anchor_rva, layout::owner_anchor, sizeof layout::owner_anchor,
                  layout::owner_operand_offset, layout::renderer_slot_rva},
                 {layout::device_anchor_rva, layout::device_anchor, sizeof layout::device_anchor, ~0u, 0}};
    auto write = [&](uint32_t operand_delta) {
        for (const Span& s : spans) {
            std::memcpy(base + s.rva, s.bytes, s.length);
            if (s.operand != ~0u) {
                const uint32_t operand = uint32_t(b + s.slot + operand_delta);
                std::memcpy(base + s.rva + s.operand, &operand, 4);
            }
        }
    };
    write(0);
    check(sh::fixture_gate(b) == nullptr, "relocated synthetic image accepted at its base");
    check(sh::fixture_gate(b + 0x10000) != nullptr, "the same bytes checked at another base refused");
    write(0x10000);
    const char* foreign = sh::fixture_gate(b);
    check(foreign && !std::strcmp(foreign, "glow_anchor_mismatch"), "anchor operand naming another slot refused");
    unsigned flips = 0, refused = 0;
    for (const Span& s : spans)
        for (unsigned i = 0; i < s.length; ++i) {
            if (s.operand != ~0u && i >= s.operand && i < s.operand + 4) continue; // covered above
            write(0);
            base[s.rva + i] ^= 0x01;
            ++flips;
            refused += sh::fixture_gate(b) != nullptr;
        }
    write(0);
    check(flips == 24 + 14 + 14 + 5 + 4 && refused == flips, "every single-byte flip of window, entry, anchors refused");
    std::printf("ANCHOR flips=%u refused=%u\n", flips, refused);

    // Owner chain: renderer = *(base + renderer_slot_rva), record = [renderer + 0x18], device = [record].
    co::take_read_counts();
    alignas(16) static uint32_t renderer[0x70 / 4], record[0x1a8 / 4], config[0x110 / 4];
    co::Snapshot snap{};
    auto put = [&](uint32_t rva, const void* value) {
        const uint32_t word = uint32_t(reinterpret_cast<uintptr_t>(value));
        std::memcpy(base + rva, &word, 4);
    };
    SetLastError(0x145);
    check(co::read(b, snap) == co::Result::InvalidAddress && !snap.device, "read refused before admission");
    check(!co::glow_enabled(b), "glow refused before admission");
    co::admit(b);
    put(layout::renderer_slot_rva, nullptr);
    check(co::read(b, snap) == co::Result::InvalidAddress, "null renderer: invalid_address");
    put(layout::renderer_slot_rva, renderer);
    renderer[0x18 / 4] = 0;
    check(co::read(b, snap) == co::Result::InvalidAddress, "null record: invalid_address");
    renderer[0x18 / 4] = uint32_t(reinterpret_cast<uintptr_t>(record));
    record[0] = 0;
    check(co::read(b, snap) == co::Result::OwnerMismatch && !snap.record, "record without device: no_device");
    record[0] = 0x0badf00d;
    check(co::read(b, snap) == co::Result::Ok && snap.renderer == uint32_t(reinterpret_cast<uintptr_t>(renderer)) &&
              snap.record == uint32_t(reinterpret_cast<uintptr_t>(record)) && snap.device == 0x0badf00d &&
              !snap.manager && !snap.manager_device,
          "renderer -> record+0x18 -> device[0], no manager fields");
    co::Snapshot again{};
    check(co::read(b, again) == co::Result::Ok && co::same(snap, again), "same() over two equal reads");
    record[0] = 0x0badf00e;
    check(co::read(b, again) == co::Result::Ok && !co::same(snap, again), "same() rejects a changed device");
    renderer[0x18 / 4] = uint32_t(b + 0x1000); // reserved, not committed
    check(co::read(b, snap) == co::Result::Unreadable && !snap.renderer, "unreadable record: unreadable, output empty");
    check(co::read(b + 0x10000, snap) == co::Result::InvalidAddress, "another base refused");
    check(GetLastError() == 0x145, "reads preserve LastError");
    const auto read_counts = co::take_read_counts();
    check(read_counts.ok == 3 && read_counts.failed == 6 && read_counts.last_failure == co::Result::InvalidAddress,
          "read counters (3 ok, 6 failed, last invalid_address)");
    // Glow: config = *(base + config_slot_rva), byte +0xf0 bit 0x80 (AP's +0x100 is not FL's flag).
    put(layout::config_slot_rva, config);
    reinterpret_cast<unsigned char*>(config)[0x100] = 0x80;
    check(!co::glow_enabled(b), "AP's config+0x100 bit does not count");
    reinterpret_cast<unsigned char*>(config)[layout::glow_flag_offset] = 0x80;
    check(co::glow_enabled(b) && !co::glow_enabled(b + 0x10000), "config+0xf0 bit 0x80: glow on, admitted base only");
    reinterpret_cast<unsigned char*>(config)[layout::glow_flag_offset] = 0x7f;
    check(!co::glow_enabled(b), "bit clear: glow off");
    put(layout::config_slot_rva, nullptr);
    check(!co::glow_enabled(b) && GetLastError() == 0x145, "null config: off, LastError kept");
    co::admit(0);
    check(VirtualFree(base, 0, MEM_RELEASE) != 0, "synthetic image released");
}
double time_calls(unsigned iterations) {
    LARGE_INTEGER frequency{}, begin{}, end{};
    QueryPerformanceFrequency(&frequency);
    target_mode = 3;
    QueryPerformanceCounter(&begin);
    for (unsigned i = 0; i < iterations; ++i) {
        invoke(camera_high, 0);
    }
    QueryPerformanceCounter(&end);
    target_mode = 0;
    return double(end.QuadPart - begin.QuadPart) * 1e9 / double(frequency.QuadPart) / iterations;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    make_camera(camera_high, 0x12);
    make_camera(camera_low, 0x05);
    make_camera(camera_edge, 0x11); // layer 17: jle taken, no call
    auto* page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!page) return 2;
    unsigned char *window = nullptr, *other_window = nullptr;
    unsigned char* next = emit_caller(page, reinterpret_cast<void*>(&fl_fake_compositor), &window);
    unsigned char* other = page + ((next - page + 63) & ~63);
    next = emit_caller(other, reinterpret_cast<void*>(&fl_other_compositor), &other_window);
    unsigned char* mutated = page + ((next - page + 63) & ~63);
    unsigned char* mutated_window = nullptr;
    emit_caller(mutated, reinterpret_cast<void*>(&fl_fake_compositor), &mutated_window);
    DWORD old = 0;
    VirtualProtect(page, 4096, PAGE_EXECUTE_READWRITE, &old);
    site = window + layout::window_site_offset;
    invoke = reinterpret_cast<Caller>(page);
    fl_fixture_invoke = invoke;
    unsigned char original[sizeof layout::window];
    std::memcpy(original, window, sizeof original);
    const auto unchanged = [&]() { return !std::memcmp(window, original, sizeof original); };
    // Unpatched baseline through the same pass: the witness holds without the hook, both ways of the boundary.
    witnessed(camera_high, 0, 1, false, "unpatched layer 18");
    witnessed(camera_edge, 0, 0, false, "unpatched layer 17");
    witnessed(camera_high, 1, 0, false, "unpatched done byte set");
    // Production initialize in a non-X3FL process.
    SetEnvironmentVariableW(L"X3M_SCENE_HOOK", L"0");
    check(!sh::initialize(&listener, nullptr) && !sh::active() && row_has("fl.scene status=disabled"),
          "X3M_SCENE_HOOK=0: disabled");
    SetEnvironmentVariableW(L"X3M_SCENE_HOOK", nullptr);
    SetEnvironmentVariableW(L"X3M_MOTION_OUTPUT", nullptr);
    check(!sh::wanted() && !sh::initialize(&listener, nullptr) && row_has("fl.scene status=disabled"),
          "unset switch without the route: disabled");
    SetEnvironmentVariableW(L"X3M_MOTION_OUTPUT", L"1");
    check(sh::wanted(), "unset switch follows X3M_MOTION_OUTPUT=1");
    SetEnvironmentVariableW(L"X3M_MOTION_OUTPUT", nullptr);
    SetEnvironmentVariableW(L"X3M_SCENE_HOOK", L"1");
    check(!sh::initialize(&listener, &callbacks) && !sh::active() && sh::requested() &&
              row_has("fl.scene status=executable_mismatch") && row_has("bridge_requested=1") &&
              x3m_compositor_bridge_binding_bits == 0,
          "not X3FL.exe: executable_mismatch, nothing bound");
    // Window/entry gate refusals: nothing written, nothing bound.
    check(!sh::fixture_install(other_window, reinterpret_cast<void*>(&fl_other_compositor), &listener, &callbacks) &&
              !std::strcmp(sh::status(), "entry_mismatch"),
          "compositor without the gated prologue refused");
    check(!sh::fixture_install(window, reinterpret_cast<void*>(&fl_other_compositor), &listener, nullptr) &&
              !std::strcmp(sh::status(), "target_mismatch"),
          "rel32 to another target refused");
    check(!sh::fixture_install(window + 1, reinterpret_cast<void*>(&fl_fake_compositor), &listener, nullptr) &&
              (!std::strcmp(sh::status(), "callsite_mismatch") || !std::strcmp(sh::status(), "target_mismatch")),
          "window one byte off refused");
    for (unsigned index : {6u, 8u, 14u, 17u, 23u}) { // threshold, jle distance, done-byte store, its value, reload
        mutated_window[index] ^= 0x01;
        check(!sh::fixture_install(mutated_window, reinterpret_cast<void*>(&fl_fake_compositor), &listener, nullptr) &&
                  !std::strcmp(sh::status(), "boundary_mismatch"),
              "mutated window byte refused (boundary_mismatch)");
        mutated_window[index] ^= 0x01;
    }
    check(!sh::fixture_install(window, reinterpret_cast<void*>(&fl_fake_compositor), &listener, &broken_callbacks) &&
              !std::strcmp(sh::status(), "bridge_bind_failed") && x3m_compositor_bridge_binding_bits == 0,
          "incomplete bridge callbacks refused before any write");
    check(unchanged() && !sh::installed() && sh::shutdown(), "refusals wrote nothing; shutdown without install");

    // ---- signal path --------------------------------------------------------------------------------------------
    check(sh::fixture_install(window, reinterpret_cast<void*>(&fl_fake_compositor), &listener, nullptr) &&
              sh::active() && !sh::compositor_active() && !sh::compositor_caller_pc() &&
              row_has("fl.scene status=active") && row_has(" path=signal "),
          "signal path installed");
    check(!unchanged() && window[layout::window_site_offset] == 0xe8 &&
              !std::memcmp(window, original, layout::window_site_offset) &&
              !std::memcmp(window + layout::window_site_offset + 5, original + layout::window_site_offset + 5,
                           sizeof original - layout::window_site_offset - 5),
          "only the rel32 rewritten");
    bridge_path = false;
    witnessed(camera_high, 0, 1, true, "signal layer 18");
    present();
    witnessed(camera_edge, 0, 0, true, "signal layer 17");
    present();
    witnessed(camera_high, 1, 0, true, "signal done byte set");
    present();
    target_mode = 1;
    witnessed(camera_high, 0, 2, true, "signal nested outer");
    target_mode = 0;
    check(listener_calls >= 3, "nested pass signalled too");
    present();
    unwind_case("signal");
    present();
    {
        const unsigned before = rows;
        sh::fixture_force_sample();
        present(); // frame 6: no call since the unwind case's witnessed call was counted in frame 5
        check(rows == before + 1 && row_has("fl.scene_sample row=1 status=active path=signal "), "one sample row");
        // Frames: 1 call, none (layer 17), none (done byte), 2 (nested), 2 (unwind + witnessed), none = 6 frames.
        check(row_has(" frames=6 entries=5 frames_without_call=3 frames_multi_call=2 nested=0 compositor_active=0 "),
              "frame accounting: entries, frames without a call, multi-call frames");
        check(row_has(" owner_reads=0 owner_failed=0 owner_last_failure=none glow=0 "), "no owner reads on this path");
    }
    check(sh::shutdown() && unchanged() && !sh::active() && !std::strcmp(sh::status(), "restored"), "exact restore");
    witnessed(camera_high, 0, 1, false, "after restore");

    // ---- bridge path --------------------------------------------------------------------------------------------
    check(sh::fixture_install(window, reinterpret_cast<void*>(&fl_fake_compositor), &listener, &callbacks) &&
              sh::active() && sh::compositor_active() &&
              sh::compositor_caller_pc() == reinterpret_cast<uintptr_t>(site) + 5 && row_has(" path=bridge "),
          "bridge path installed");
    bridge_path = true;
    const unsigned listener_before = listener_calls;
    witnessed(camera_high, 0, 1, true, "bridge layer 18");
    present();
    witnessed(camera_edge, 0, 0, true, "bridge layer 17");
    present();
    target_mode = 1;
    witnessed(camera_high, 0, 2, true, "bridge nested outer");
    target_mode = 0;
    check(pre_calls >= 3 && listener_calls == listener_before, "nested bridge entries; the listener is not used");
    present();
    unwind_case("bridge");
    {
        sh::fixture_force_sample();
        present();
        check(row_has(" path=bridge frames=4 entries=5 frames_without_call=1 frames_multi_call=2 nested=1 "
                      "compositor_active=1 "),
              "bridge frame accounting and one nested entry");
    }
    check(sh::shutdown() && unchanged() && !sh::compositor_active() && x3m_compositor_bridge_binding_bits == 0,
          "bridge restore and unbind");
    bridge_path = false;

    // X3M_FL_SCENE_LOG=0: no sample rows, the hook unchanged.
    SetEnvironmentVariableW(L"X3M_FL_SCENE_LOG", L"0");
    check(sh::fixture_install(window, reinterpret_cast<void*>(&fl_fake_compositor), &listener, nullptr),
          "reinstall with rows off");
    {
        const unsigned before = rows;
        sh::fixture_force_sample();
        witnessed(camera_high, 0, 1, true, "rows off");
        present();
        check(rows == before && sh::active(), "X3M_FL_SCENE_LOG=0 writes no sample row");
    }
    check(sh::shutdown(), "restore after rows off");
    SetEnvironmentVariableW(L"X3M_FL_SCENE_LOG", nullptr);

    anchor_and_owner_cases();

    // Cost per compositor call: unpatched, signal path (trivial listener), bridge path (trivial callbacks).
    {
        clobber = false;
        const unsigned iterations = 200000;
        const double native = time_calls(iterations);
        check(sh::fixture_install(window, reinterpret_cast<void*>(&fl_fake_compositor), &listener, nullptr), "timing 1");
        const double signal = time_calls(iterations);
        check(sh::shutdown(), "timing restore 1");
        check(sh::fixture_install(window, reinterpret_cast<void*>(&fl_fake_compositor), &listener, &callbacks), "timing 2");
        bridge_path = true;
        const double bridge = time_calls(iterations);
        check(sh::shutdown(), "timing restore 2");
        bridge_path = false;
        clobber = true;
        std::printf("TIMING native_ns=%.1f signal_ns=%.1f bridge_ns=%.1f signal_hook_ns=%.1f bridge_hook_ns=%.1f\n",
                    native, signal, bridge, signal - native, bridge - native);
    }
    // Late window: after the first Present closes the engine_patch window, a claim is refused and writes nothing.
    x3m::engine_patch::close_install_window("fixture_first_present");
    check(!sh::fixture_install(window, reinterpret_cast<void*>(&fl_fake_compositor), &listener, &callbacks) &&
              !std::strcmp(sh::status(), "late_claim") && unchanged() && !sh::installed() &&
              x3m_compositor_bridge_binding_bits == 0,
          "late claim refused, nothing bound");
    VirtualFree(page, 0, MEM_RELEASE);
    std::printf("RESULT %s checks=%u failures=%u target_calls=%u\n", failures ? "FAIL" : "PASS", checks, failures,
                target_calls);
    return failures ? 1 : 0;
}
