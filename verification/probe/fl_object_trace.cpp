// Original synthetic fixture of the FL material-submission scope hook (src/engine/fl/object_trace_fl.cpp,
// docs/architecture/farnhams-legacy-object-identity.md): no game code, no game bytes, no launch.
// Built with -DX3M_EDITION_FL=1 -DX3M_OBJECT_TRACE_FIXTURE (verification/analysis/test_fl_object_trace.py).
//
// A fake caller mirrors the X3FL.exe convergence (material-submit.md section 1): ECX = descriptor,
// EDX = node, four pushed words (camera, flags, light1, light2), `call rel32` (the patched site),
// `add esp,0x10`. It loads sentinels into ESI/EDI/EBX and XMM0-7 before the call and stores ESP, EBP,
// ESI/EDI/EBX, ECX and XMM0-7 after it. The fake target starts with the gated bytes
// `push ebx; mov ebx,esp; sub esp,8`, realigns, records its entry registers, XMM0-7, the x87 control word
// and MXCSR, calls a C body with (ECX, EDX, [ebx+8..+20]) and returns EAX:EDX plus a sentinel ECX and
// XMM0-7 with a plain RET (caller cleanup).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <excpt.h>
#include <cstdarg>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../../src/proxy/object_trace.h"
#include "../../src/proxy/engine_memory.h"
#include "../../src/proxy/engine_patch.h"
namespace layout {
#include "../../src/engine/fl/object_trace_fl_inc.h"
}
namespace ot = x3m::object_trace;
unsigned checks = 0, failures = 0, calls = 0;
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
// Written by the fake caller / target (absolute addresses in the generated caller bytes and the asm).
uint32_t esp_before = 0, esp_after = 0, ebp_before = 0, ebp_after = 0, esi_after = 0, edi_after = 0, ebx_after = 0,
         ecx_after = 0;
uint32_t entry_esi = 0, entry_edi = 0, entry_ebx = 0;
uint16_t entry_cw = 0;
uint32_t entry_mxcsr = 0;
alignas(16) uint32_t xmm_in[32], xmm_at_target[32], xmm_out[32], xmm_after[32];
uint64_t __cdecl fl_fake_body(uintptr_t descriptor, uintptr_t node, uintptr_t camera, uint32_t flags, uintptr_t light1,
                              uintptr_t light2);
}
// The fake target: the six gated bytes, then a realigned C call; plain RET.
asm(".text\n\t.globl _fl_fake_target\n_fl_fake_target:\n\t"
    ".byte 0x53,0x8b,0xdc,0x83,0xec,0x08\n\t" // push ebx; mov ebx,esp; sub esp,8
    "andl $-16,%esp\n\t"
    "movups %xmm0,_xmm_at_target\n\tmovups %xmm1,_xmm_at_target+16\n\tmovups %xmm2,_xmm_at_target+32\n\t"
    "movups %xmm3,_xmm_at_target+48\n\tmovups %xmm4,_xmm_at_target+64\n\tmovups %xmm5,_xmm_at_target+80\n\t"
    "movups %xmm6,_xmm_at_target+96\n\tmovups %xmm7,_xmm_at_target+112\n\t"
    "fnstcw _entry_cw\n\tstmxcsr _entry_mxcsr\n\t"
    "movl %esi,_entry_esi\n\tmovl %edi,_entry_edi\n\tmovl (%ebx),%eax\n\tmovl %eax,_entry_ebx\n\t"
    "pushl 20(%ebx)\n\tpushl 16(%ebx)\n\tpushl 12(%ebx)\n\tpushl 8(%ebx)\n\tpushl %edx\n\tpushl %ecx\n\t"
    "call _fl_fake_body\n\t"
    "movups _xmm_out,%xmm0\n\tmovups _xmm_out+16,%xmm1\n\tmovups _xmm_out+32,%xmm2\n\tmovups _xmm_out+48,%xmm3\n\t"
    "movups _xmm_out+64,%xmm4\n\tmovups _xmm_out+80,%xmm5\n\tmovups _xmm_out+96,%xmm6\n\tmovups _xmm_out+112,%xmm7\n\t"
    "movl $0x0cc0ffee,%ecx\n\t"
    "movl %ebx,%esp\n\tpopl %ebx\n\tret\n\t"
    // A target with another prologue (the entry gate must refuse it).
    ".globl _fl_other_target\n_fl_other_target:\n\tpushl %ebp\n\tmovl %esp,%ebp\n\tpopl %ebp\n\tret\n\t");
extern "C" void fl_fake_target();
extern "C" void fl_other_target();

using Caller = uint64_t(__cdecl*)(uintptr_t, uintptr_t, uintptr_t, uint32_t, uintptr_t, uintptr_t);
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
// Emits the fake caller at `at`; returns its site (the call rel32) through *site.
unsigned char* emit_caller(unsigned char* at, const void* target, unsigned cleanup, unsigned char** site) {
    Emit e{at};
    e.b({0x55, 0x89, 0xe5, 0x53, 0x56, 0x57}); // push ebp; mov ebp,esp; push ebx; push esi; push edi
    e.abs({0x89, 0x25}, &esp_before);          // mov [esp_before],esp
    e.abs({0x89, 0x2d}, &ebp_before);          // mov [ebp_before],ebp
    e.b({0xff, 0x75, 0x1c, 0xff, 0x75, 0x18, 0xff, 0x75, 0x14, 0xff, 0x75, 0x10}); // push l2, l1, flags, camera
    for (unsigned r = 0; r < 8; ++r) e.abs({0x0f, 0x10, 0x05 + 8 * r}, xmm_in + 4 * r); // movups xmmR,[xmm_in+16R]
    e.b({0x8b, 0x4d, 0x08, 0x8b, 0x55, 0x0c}); // mov ecx,[ebp+8]; mov edx,[ebp+0xc]
    e.b({0xbe});
    e.d(0x5a5a1111); // mov esi
    e.b({0xbf});
    e.d(0x5a5a2222); // mov edi
    e.b({0xbb});
    e.d(0x5a5a3333); // mov ebx
    *site = e.p;
    e.b({0xe8});
    e.d(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(e.p) + 4)));
    e.b({0x83, 0xc4, cleanup}); // add esp,cleanup
    e.abs({0x89, 0x25}, &esp_after);
    e.abs({0x89, 0x2d}, &ebp_after);
    e.abs({0x89, 0x35}, &esi_after);
    e.abs({0x89, 0x3d}, &edi_after);
    e.abs({0x89, 0x1d}, &ebx_after);
    e.abs({0x89, 0x0d}, &ecx_after);
    for (unsigned r = 0; r < 8; ++r) e.abs({0x0f, 0x11, 0x05 + 8 * r}, xmm_after + 4 * r); // movups [..],xmmR
    e.b({0x5f, 0x5e, 0x5b, 0x5d, 0xc3});
    return e.p;
}

constexpr uintptr_t descriptor_value = 0x11223344, light1_value = 0x55667788, light2_value = 0x99aabbcc;
constexpr uint64_t result_value = 0x0123456789abcdefull;
uint32_t node[0x270 / 4]{}, camera[0x790 / 4]{}, engine[4]{};
uintptr_t engine_pointer = 0;
Caller invoke = nullptr;
bool expect_scope = true;
uint32_t expect_depth = 1;
uintptr_t seen[6]{};
// flags carries the mode: 0 plain, 1 nested outer, 2 nested inner, 3 raise, 4 null node/camera, 5 TLS enter
// failure armed by the caller, 6 TLS restore failure armed here, 7 timing (no checks), 8 timing with current(),
// 9 unreadable camera pointer.
extern "C" uint64_t __cdecl fl_fake_body(uintptr_t descriptor, uintptr_t n, uintptr_t c, uint32_t flags, uintptr_t l1,
                                         uintptr_t l2) {
    ++calls;
    const DWORD entry_error = GetLastError();
    if (flags == 7) return result_value;
    ot::Snapshot snap{};
    if (flags == 8) {
        ot::current(&snap, false);
        return result_value;
    }
    seen[0] = descriptor, seen[1] = n, seen[2] = c, seen[3] = flags, seen[4] = l1, seen[5] = l2;
    check(entry_error == 0x145, "entry LastError preserved");
    check(descriptor == descriptor_value && l1 == light1_value && l2 == light2_value, "ECX and light words forwarded");
    const bool scoped = ot::current(&snap);
    check(scoped == expect_scope, "scope availability inside the target");
    check(GetLastError() == 0x145, "current() preserves LastError");
    uintptr_t d = 0, nd = 0;
    uint32_t depth = 0;
    check(ot::scope_node(&d, &nd) == expect_scope && ot::scope_descriptor(&d, &depth) == expect_scope,
          "scope_node/scope_descriptor availability");
    if (expect_scope) {
        check(snap.mesh == descriptor && snap.node == n && snap.camera == c, "scope args exact (ECX, EDX, [esp+4])");
        check(d == descriptor && nd == n && depth == expect_depth, "scope_node/scope_descriptor values");
        check(snap.scope_depth == expect_depth, "scope depth");
        if (flags == 4) {
            check(!(snap.valid & (ot::Node | ot::Camera)) && (snap.valid & ot::Registry), "null node/camera not read");
        } else if (flags == 9) {
            check(snap.valid == (ot::Node | ot::Registry), "unreadable camera: Camera bit clear, node still read");
        } else {
            check(snap.valid == (ot::Node | ot::Camera | ot::Registry), "node, camera, registry valid; no matrix bits");
            check(snap.node_handle == 42 && snap.camera_handle == 7 && snap.registry == 0x13579, "handles, registry");
            check(snap.model == 123 && snap.lod == 2, "FL model +0x144 / LOD +0x150 (not AP +0x140/+0x14c)");
            check(snap.parent == 0x600d && snap.flags12c == 0x12c0 && snap.flags130 == 0x1300, "parent, flags");
            check(snap.position[1] == 22 && snap.scale[0] == 0x700 && snap.scale[3] == 0x880 && snap.basis[4] == 0xd4,
                  "position, scale, basis rows");
            check(!snap.alpha13c && !(snap.valid & (ot::World | ot::WorldBasis | ot::View | ot::Projection)),
                  "no AP matrix or alpha reads");
        }
    }
    if (flags == 1) {
        // The nested call rewrites the caller's witness words: keep the outer call's.
        const uint32_t outer_esp = esp_before, outer_ebp = ebp_before;
        uintptr_t outer_seen[6];
        std::memcpy(outer_seen, seen, sizeof seen);
        expect_depth = 2;
        SetLastError(0x145);
        const uint64_t nested = invoke(descriptor, n, c, 2, l1, l2);
        expect_depth = 1;
        esp_before = outer_esp;
        ebp_before = outer_ebp;
        std::memcpy(seen, outer_seen, sizeof seen);
        check(nested == result_value, "nested EAX:EDX");
        ot::Snapshot after{};
        check(ot::current(&after) && after.scope_depth == 1 && after.node == n, "outer scope restored after nesting");
    }
    if (flags == 3) RaiseException(0xe3450001, 0, 0, nullptr);
    if (flags == 6) ot::fixture_fail_next_tls_set();
    SetLastError(0x246);
    return result_value;
}
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
// An outer original SEH frame beyond the patched call: the exception unwinds through the dispatcher's own
// registration (foreign SEH unwinding, not C++ stack destruction).
extern "C" __attribute__((naked)) uint64_t __cdecl catch_outer(uintptr_t, uintptr_t, uintptr_t, uint32_t, uintptr_t,
                                                               uintptr_t) {
    __asm__ __volatile__(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tsubl $8,%esp\n\t"
        "movl %fs:0,%eax\n\tmovl %eax,-8(%ebp)\n\tmovl $_fl_fixture_handler,-4(%ebp)\n\t"
        "leal -8(%ebp),%eax\n\tmovl %eax,%fs:0\n\t"
        "pushl 28(%ebp)\n\tpushl 24(%ebp)\n\tpushl 20(%ebp)\n\tpushl 16(%ebp)\n\tpushl 12(%ebp)\n\tpushl 8(%ebp)\n\t"
        "call *_fl_fixture_invoke\n\taddl $24,%esp\n\t"
        "movl -8(%ebp),%edx\n\tmovl %edx,%fs:0\n\tleave\n\tret\n\t");
}
uintptr_t n_ptr() {
    return reinterpret_cast<uintptr_t>(node);
}
uintptr_t c_ptr() {
    return reinterpret_cast<uintptr_t>(camera);
}
// One call with the full register/XMM/x87/MXCSR witness.
uint64_t witnessed_call(uintptr_t n, uintptr_t c, uint32_t mode) {
    for (unsigned i = 0; i < 32; ++i) {
        xmm_in[i] = 0xa5000000u + i + mode * 64;
        xmm_out[i] = 0x5a000000u + i + mode * 64;
    }
    uint16_t cw = 0, test_cw = 0x027f; // 53-bit precision instead of the default 64
    uint32_t mxcsr = 0, test_mxcsr = 0;
    asm volatile("fnstcw %0\n\tstmxcsr %1" : "=m"(cw), "=m"(mxcsr));
    test_mxcsr = (mxcsr & ~0x6000u) | 0x6000u; // round toward zero
    SetLastError(0x145);
    asm volatile("fldcw %0\n\tldmxcsr %1" ::"m"(test_cw), "m"(test_mxcsr));
    const uint64_t result = invoke(descriptor_value, n, c, mode, light1_value, light2_value);
    asm volatile("fldcw %0\n\tldmxcsr %1" ::"m"(cw), "m"(mxcsr));
    check(GetLastError() == 0x246, "target LastError returned");
    check(result == result_value, "EAX:EDX returned");
    check(ecx_after == 0x0cc0ffee, "returned ECX kept");
    check(esp_after == esp_before && ebp_after == ebp_before, "stack balanced (one 0x10 cleanup each), EBP kept");
    check(esi_after == 0x5a5a1111 && edi_after == 0x5a5a2222 && ebx_after == 0x5a5a3333, "ESI/EDI/EBX kept");
    check(entry_esi == 0x5a5a1111 && entry_edi == 0x5a5a2222 && entry_ebx == 0x5a5a3333,
          "ESI/EDI/EBX reach the target");
    check(!std::memcmp(xmm_at_target, xmm_in, sizeof xmm_in), "XMM0-7 reach the target");
    check(!std::memcmp(xmm_after, xmm_out, sizeof xmm_out), "XMM0-7 of the return reach the caller");
    check(entry_cw == test_cw && entry_mxcsr == test_mxcsr, "x87 control word and MXCSR reach the target");
    check(seen[1] == n && seen[2] == c && seen[3] == mode, "EDX and the camera/flags words forwarded");
    return result;
}
// The production gate (gate(..., exact=true) and engine_anchor_matches) against a synthetic image at the base
// VirtualAlloc picks: only the anchor pages are committed, holding the generated bytes with the engine operand
// relocated to base + slot. It must pass, and refuse the same bytes at another base, a foreign operand and every
// single-bit flip outside the operand.
void anchor_cases() {
    const SIZE_T span = (layout::engine_slot_rva + 0x1000) & ~SIZE_T(0xfff);
    auto* base = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE, PAGE_NOACCESS));
    check(base != nullptr, "synthetic image reserved");
    if (!base) return;
    const uintptr_t b = reinterpret_cast<uintptr_t>(base);
    bool committed = true;
    for (uint32_t rva : {layout::site_rva, layout::target_rva, layout::engine_anchor_rva})
        committed = committed && VirtualAlloc(base + (rva & ~0xfffu), 0x2000, MEM_COMMIT, PAGE_READWRITE);
    check(committed, "anchor pages committed");
    struct Span {
        uint32_t rva;
        const uint8_t* bytes;
        unsigned length;
    } spans[] = {{layout::site_rva, layout::site_bytes, sizeof layout::site_bytes},
                 {layout::target_rva, layout::target_entry, sizeof layout::target_entry},
                 {layout::engine_anchor_rva, layout::engine_anchor, sizeof layout::engine_anchor}};
    auto write = [&](uint32_t operand_delta) {
        for (const Span& s : spans) std::memcpy(base + s.rva, s.bytes, s.length);
        const uint32_t operand = uint32_t(b + layout::engine_slot_rva + operand_delta);
        std::memcpy(base + layout::engine_anchor_rva + layout::engine_operand_offset, &operand, 4);
    };
    write(0);
    check(ot::fixture_gate(b) == nullptr, "relocated synthetic image accepted at its base");
    check(ot::fixture_gate(b + 0x10000) != nullptr, "the same bytes checked at another base refused");
    write(0x10000);
    check(ot::fixture_gate(b) && !std::strcmp(ot::fixture_gate(b), "engine_anchor_mismatch"),
          "engine operand naming another slot refused");
    unsigned flips = 0, refused = 0;
    for (const Span& s : spans)
        for (unsigned i = 0; i < s.length; ++i) {
            if (s.rva == layout::engine_anchor_rva && i >= layout::engine_operand_offset &&
                i < layout::engine_operand_offset + 4)
                continue; // the operand is covered by the two cases above
            write(0);
            base[s.rva + i] ^= 0x01;
            ++flips;
            refused += ot::fixture_gate(b) != nullptr;
        }
    check(flips == 8 + 6 + 7 && refused == flips, "every single-byte flip of site, entry and anchor refused");
    std::printf("ANCHOR flips=%u refused=%u\n", flips, refused);
    check(VirtualFree(base, 0, MEM_RELEASE) != 0, "synthetic image released");
}
// The reason the dispatcher's rare paths hold an XmmSave: XMM0-7 loaded with sentinels, one VirtualQuery, XMM0-7
// stored. Prints how many of the eight registers came back changed and how many zero (an observation, not a
// check: it is Windows behaviour, recorded by the host test).
extern "C" {
alignas(16) uint32_t probe_in[32], probe_out[32];
MEMORY_BASIC_INFORMATION probe_info;
SIZE_T(WINAPI* probe_query)(LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T) = nullptr;
}
asm(".text\n\t.globl _xmm_query_probe\n_xmm_query_probe:\n\t"
    "movups _probe_in,%xmm0\n\tmovups _probe_in+16,%xmm1\n\tmovups _probe_in+32,%xmm2\n\tmovups _probe_in+48,%xmm3\n\t"
    "movups _probe_in+64,%xmm4\n\tmovups _probe_in+80,%xmm5\n\tmovups _probe_in+96,%xmm6\n\tmovups _probe_in+112,%xmm7\n\t"
    "pushl $28\n\tpushl $_probe_info\n\tpushl $_probe_info\n\tcall *_probe_query\n\t" // stdcall: callee pops
    "movups %xmm0,_probe_out\n\tmovups %xmm1,_probe_out+16\n\tmovups %xmm2,_probe_out+32\n\tmovups %xmm3,_probe_out+48\n\t"
    "movups %xmm4,_probe_out+64\n\tmovups %xmm5,_probe_out+80\n\tmovups %xmm6,_probe_out+96\n\tmovups %xmm7,_probe_out+112\n\t"
    "ret\n\t");
extern "C" void xmm_query_probe();
void xmm_after_virtualquery() {
    for (unsigned i = 0; i < 32; ++i) probe_in[i] = 0xc3000000u + i;
    probe_query = &VirtualQuery;
    xmm_query_probe();
    unsigned changed = 0, zero = 0;
    for (unsigned r = 0; r < 8; ++r) {
        changed += std::memcmp(probe_out + 4 * r, probe_in + 4 * r, 16) != 0;
        zero += !(probe_out[4 * r] | probe_out[4 * r + 1] | probe_out[4 * r + 2] | probe_out[4 * r + 3]);
    }
    check(probe_info.RegionSize != 0, "probe VirtualQuery answered");
    std::printf("XMM_AFTER_VIRTUALQUERY changed=%u zero=%u of=8\n", changed, zero);
}
DWORD WINAPI foreign_thread(void*) {
    SetLastError(0x145);
    const uint64_t r = invoke(descriptor_value, n_ptr(), c_ptr(), 0, light1_value, light2_value);
    return r == result_value ? 0 : 1;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    node[0x18 / 4] = 0x600d;
    node[0x28 / 4] = 42;
    node[0x70 / 4] = 0x700;
    node[0x88 / 4] = 0x880;
    node[0xb4 / 4] = 22;
    node[0xd4 / 4] = 0xd4; // basis row 1, word 1 (rows are 16 bytes apart)
    node[0x12c / 4] = 0x12c0;
    node[0x130 / 4] = 0x1300;
    node[0x140 / 4] = 999; // AP's model offset: must not be read as the model
    node[0x144 / 4] = 123;
    node[0x14c / 4] = 998; // AP's LOD offset
    node[0x150 / 4] = 2;
    camera[0x28 / 4] = 7;
    engine[3] = 0x13579;
    engine_pointer = reinterpret_cast<uintptr_t>(engine);
    const uintptr_t engine_slot = reinterpret_cast<uintptr_t>(&engine_pointer);
    auto* page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!page) return 2;
    unsigned char *site = nullptr, *bad_cleanup_site = nullptr, *other_site = nullptr;
    unsigned char* next = emit_caller(page, reinterpret_cast<void*>(&fl_fake_target), 0x10, &site);
    unsigned char* bad_cleanup = next = page + ((next - page + 63) & ~63);
    next = emit_caller(bad_cleanup, reinterpret_cast<void*>(&fl_fake_target), 0x0c, &bad_cleanup_site);
    unsigned char* other = page + ((next - page + 63) & ~63);
    emit_caller(other, reinterpret_cast<void*>(&fl_other_target), 0x10, &other_site);
    DWORD old = 0;
    VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), page, 4096);
    invoke = reinterpret_cast<Caller>(page);
    fl_fixture_invoke = invoke;
    unsigned char original[5];
    std::memcpy(original, site, 5);
    const auto protection_kept = [&]() {
        MEMORY_BASIC_INFORMATION info{};
        VirtualQuery(page, &info, sizeof info);
        return info.Protect == PAGE_EXECUTE_READ;
    };
    // Unpatched baseline through the same caller: the witness itself holds without the hook.
    expect_scope = false;
    witnessed_call(n_ptr(), c_ptr(), 0);
    // Production gate in a non-X3FL process.
    SetEnvironmentVariableW(L"X3M_OBJECT_TRACE", nullptr);
    check(!ot::initialize() && !ot::active() && row_has("fl.trace status=disabled"), "unset X3M_OBJECT_TRACE: disabled");
    SetEnvironmentVariableW(L"X3M_OBJECT_TRACE", L"1");
    check(!ot::initialize() && !ot::active() && row_has("fl.trace status=executable_mismatch"),
          "not X3FL.exe: executable_mismatch");
    // Site gate refusals: nothing written.
    check(!ot::fixture_install(site, reinterpret_cast<void*>(&fl_other_target), engine_slot) &&
              !std::strcmp(ot::status(), "target_mismatch"),
          "rel32 to another target refused");
    check(!ot::fixture_install(bad_cleanup_site, reinterpret_cast<void*>(&fl_fake_target), engine_slot) &&
              !std::strcmp(ot::status(), "cleanup_mismatch"),
          "caller without add esp,0x10 refused");
    check(!ot::fixture_install(other_site, reinterpret_cast<void*>(&fl_other_target), engine_slot) &&
              !std::strcmp(ot::status(), "entry_mismatch"),
          "target without the gated prologue refused");
    check(!ot::fixture_install(site + 1, reinterpret_cast<void*>(&fl_fake_target), engine_slot) &&
              (!std::strcmp(ot::status(), "site_mismatch") || !std::strcmp(ot::status(), "target_mismatch")),
          "mid-instruction site refused");
    check(!std::memcmp(site, original, 5) && protection_kept() && !ot::recovery_required(), "refusals wrote nothing");
    // Install.
    check(ot::fixture_install(site, reinterpret_cast<void*>(&fl_fake_target), engine_slot) && ot::active() &&
              !std::strcmp(ot::status(), "active"),
          "verified fixture site installed");
    check(std::memcmp(site, original, 5) && site[0] == 0xe8 && protection_kept(), "redirect written, protection kept");
    expect_scope = true;
    for (uint32_t mode : {0u, 1u}) {
        witnessed_call(n_ptr(), c_ptr(), mode);
        ot::Snapshot after{};
        check(!ot::current(&after), "scope cleared after a normal return");
    }
    witnessed_call(0, 0, 4);
    witnessed_call(n_ptr(), 0x10, 9); // a camera pointer into the null page
    // Diagnostic row: forced on the next entry of the owning (first) thread; one entry from another thread.
    HANDLE thread = CreateThread(nullptr, 0, foreign_thread, nullptr, 0, nullptr);
    DWORD thread_exit = 1;
    check(thread && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0 && GetExitCodeThread(thread, &thread_exit) &&
              thread_exit == 0,
          "call from a second thread");
    if (thread) CloseHandle(thread);
    const unsigned rows_before = rows;
    ot::fixture_force_sample();
    witnessed_call(n_ptr(), c_ptr(), 0); // the row is written inside the call, before the target: witnessed
    check(rows == rows_before + 1 && row_has("fl.trace_sample row=1 status=active "), "one sample row");
    // Entries on the owning thread since install: mode 0, mode 1 outer + inner, mode 4, mode 9, this call = 6.
    check(row_has(" entries=6 "), "entries counted");
    check(row_has(" depth_max=2 ") && row_has(" null_node=1 ") && row_has(" null_camera=1 "), "depth, null counts");
    check(row_has(" nodes=1 ") && row_has(" cameras=2 camera_unreadable=1 ") && row_has(" fields=1 field_failed=0 "),
          "distinct sets, the unreadable camera counted once");
    check(row_has(" handle_min=42 handle_max=42 lod_min=2 lod_max=2 model_min=123 model_max=123 "),
          "first-sighting fields at the FL offsets");
    check(row_has(" last_handle=42 ") && row_has(" last_camera_handle=7 ") && row_has(" registry=0x00013579 ") &&
              row_has(" registry_changed=0 "),
          "last node, camera, registry");
    check(row_has(" foreign_entries=1"), "the second thread's entry counted, not sampled");
    // Foreign SEH unwind through the dispatcher's registration.
    SetLastError(0x145);
    const int caught = setjmp(outer_jump);
    if (!caught) catch_outer(descriptor_value, n_ptr(), c_ptr(), 3, light1_value, light2_value);
    check(caught == 1, "SEH reached the outer handler");
    ot::Snapshot after{};
    check(ot::active() && !ot::current(&after), "scope cleared by the unwind, observation kept");
    witnessed_call(n_ptr(), c_ptr(), 0);
    // Restore, reinstall.
    check(ot::shutdown() && !std::memcmp(site, original, 5) && protection_kept() && !ot::active(), "exact restore");
    expect_scope = false;
    witnessed_call(n_ptr(), c_ptr(), 0);
    check(ot::fixture_install(site, reinterpret_cast<void*>(&fl_fake_target), engine_slot), "reinstall");
    expect_scope = true;
    // TLS publication failure at entry: observation off, the call still forwarded exactly; the redirect stays
    // (recovery_required) and forwards unobserved until restored.
    ot::fixture_fail_next_tls_set();
    expect_scope = false;
    witnessed_call(n_ptr(), c_ptr(), 5);
    check(!ot::active() && ot::recovery_required() && !std::strcmp(ot::status(), "tls_enter_failed") &&
              row_has("fl.trace status=tls_enter_failed"),
          "TLS enter failure disables observation, logged");
    witnessed_call(n_ptr(), c_ptr(), 0);
    check(ot::shutdown() && !std::memcmp(site, original, 5), "restore after TLS failure");
    // TLS restore failure at exit.
    check(ot::fixture_install(site, reinterpret_cast<void*>(&fl_fake_target), engine_slot), "reinstall 2");
    expect_scope = true;
    witnessed_call(n_ptr(), c_ptr(), 6);
    check(!ot::active() && ot::recovery_required() && !std::strcmp(ot::status(), "tls_restore_failed"),
          "TLS restore failure disables observation");
    check(ot::shutdown() && !std::memcmp(site, original, 5), "restore after TLS restore failure");
    // X3M_FL_TRACE_LOG=0: no sample rows, observation unchanged.
    SetEnvironmentVariableW(L"X3M_FL_TRACE_LOG", L"0");
    check(ot::fixture_install(site, reinterpret_cast<void*>(&fl_fake_target), engine_slot), "reinstall with rows off");
    {
        const unsigned before = rows;
        ot::fixture_force_sample();
        witnessed_call(n_ptr(), c_ptr(), 0);
        check(rows == before && ot::active(), "X3M_FL_TRACE_LOG=0 writes no sample row");
    }
    check(ot::shutdown(), "restore after rows off");
    SetEnvironmentVariableW(L"X3M_FL_TRACE_LOG", nullptr);
    anchor_cases();
    xmm_after_virtualquery();
    // Cost: the same caller unpatched, patched (enter, TLS, diagnostics, leave), patched plus current().
    {
        LARGE_INTEGER frequency{}, begin{}, end{};
        QueryPerformanceFrequency(&frequency);
        const unsigned iterations = 200000;
        double ns[3]{};
        for (unsigned pass = 0; pass < 3; ++pass) {
            if (pass == 1) check(ot::fixture_install(site, reinterpret_cast<void*>(&fl_fake_target), engine_slot), "reinstall 3");
            const uint32_t mode = pass == 2 ? 8u : 7u;
            QueryPerformanceCounter(&begin);
            for (unsigned i = 0; i < iterations; ++i) {
                if ((i & 1023) == 0) x3m::engine_memory::next_frame();
                invoke(descriptor_value, n_ptr(), c_ptr(), mode, light1_value, light2_value);
            }
            QueryPerformanceCounter(&end);
            ns[pass] = double(end.QuadPart - begin.QuadPart) * 1e9 / double(frequency.QuadPart) / iterations;
        }
        std::printf("TIMING native_ns=%.1f hooked_ns=%.1f hooked_current_ns=%.1f hook_ns=%.1f current_ns=%.1f\n", ns[0],
                    ns[1], ns[2], ns[1] - ns[0], ns[2] - ns[1]);
        check(ot::shutdown(), "restore after timing");
    }
    // Late window: after the first Present closes the engine_patch window, a claim is refused and writes nothing.
    x3m::engine_patch::close_install_window("fixture_first_present");
    check(!ot::fixture_install(site, reinterpret_cast<void*>(&fl_fake_target), engine_slot) &&
              !std::strcmp(ot::status(), "late_claim") && !std::memcmp(site, original, 5) && !ot::recovery_required(),
          "late claim refused");
    VirtualFree(page, 0, MEM_RELEASE);
    std::printf("RESULT %s checks=%u failures=%u target_calls=%u\n", failures ? "FAIL" : "PASS", checks, failures, calls);
    return failures ? 1 : 0;
}
