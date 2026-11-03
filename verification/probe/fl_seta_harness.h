#pragma once
// CPU-state harness shared by the two FL SETA native fixtures: h_enter() loads every GPR, EFLAGS (DF included) from
// h_in, ESP from h_in.esp (a private stack, every 4-byte skew mod 16), XMM0..7 (all 128 bits), an x87 stack of one
// value under a chosen control word and a chosen MXCSR, then jumps to h_site; h_record (the target of a `jmp` the
// fixture writes at a span's continuation, or of a reference copy's trailing jump) stores all of it back to h_out,
// with the x87 status word (TOP), and returns to h_enter's caller. LastError is compared by the caller around h_run().
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {
struct HCpu {
    std::uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp, flags, mxcsr;
    std::uint16_t fpucw, fpusw;
    std::uint32_t pad;
    double st0;
    alignas(16) unsigned char xmm[128];
};
HCpu h_in, h_out;
std::uint32_t h_site = 0, h_saved_esp = 0, h_host_mxcsr = 0, h_calls = 0;
void h_enter();
void h_record();
}
asm(".text\n\t"
    ".globl _h_enter\n_h_enter:\n\t"
    "pushal\n\tpushfl\n\tmovl %esp,_h_saved_esp\n\t"
    "stmxcsr _h_host_mxcsr\n\tfninit\n\tfldcw _h_in+40\n\tfldl _h_in+48\n\tldmxcsr _h_in+36\n\t"
    "movups _h_in+64,%xmm0\n\tmovups _h_in+80,%xmm1\n\tmovups _h_in+96,%xmm2\n\tmovups _h_in+112,%xmm3\n\t"
    "movups _h_in+128,%xmm4\n\tmovups _h_in+144,%xmm5\n\tmovups _h_in+160,%xmm6\n\tmovups _h_in+176,%xmm7\n\t"
    "pushl _h_in+32\n\tpopfl\n\t"
    "movl _h_in+28,%esp\n\t"
    "movl _h_in+0,%eax\n\tmovl _h_in+4,%ebx\n\tmovl _h_in+8,%ecx\n\tmovl _h_in+12,%edx\n\tmovl _h_in+16,%esi\n\t"
    "movl _h_in+20,%edi\n\tmovl _h_in+24,%ebp\n\t"
    "jmp *_h_site\n\t"
    ".globl _h_record\n_h_record:\n\t"
    "movl %eax,_h_out+0\n\tmovl %ebx,_h_out+4\n\tmovl %ecx,_h_out+8\n\tmovl %edx,_h_out+12\n\tmovl %esi,_h_out+16\n\t"
    "movl %edi,_h_out+20\n\tmovl %ebp,_h_out+24\n\tmovl %esp,_h_out+28\n\t"
    "pushfl\n\tpopl _h_out+32\n\t"
    "movups %xmm0,_h_out+64\n\tmovups %xmm1,_h_out+80\n\tmovups %xmm2,_h_out+96\n\tmovups %xmm3,_h_out+112\n\t"
    "movups %xmm4,_h_out+128\n\tmovups %xmm5,_h_out+144\n\tmovups %xmm6,_h_out+160\n\tmovups %xmm7,_h_out+176\n\t"
    "stmxcsr _h_out+36\n\tfnstcw _h_out+40\n\tfnstsw _h_out+42\n\tfstpl _h_out+48\n\t"
    "fninit\n\tldmxcsr _h_host_mxcsr\n\tcld\n\t"
    "movl _h_saved_esp,%esp\n\tpopfl\n\tpopal\n\tret\n\t");
static_assert(offsetof(HCpu, esp) == 28 && offsetof(HCpu, flags) == 32 && offsetof(HCpu, mxcsr) == 36 &&
                  offsetof(HCpu, fpucw) == 40 && offsetof(HCpu, fpusw) == 42 && offsetof(HCpu, st0) == 48 &&
                  offsetof(HCpu, xmm) == 64,
              "the asm offsets");

alignas(16) unsigned char h_stack[0x10000];
// One run at `site` with ESP = the private stack top minus `skew` (0, 4, 8, 12); LastError set to `error` first and
// returned through *error_after.
inline void h_run(std::uintptr_t site, unsigned skew, DWORD error, DWORD* error_after) {
    h_in.esp = std::uint32_t(reinterpret_cast<std::uintptr_t>(h_stack + sizeof h_stack - 0x400 - skew));
    h_site = std::uint32_t(site);
    std::memset(&h_out, 0xcd, sizeof h_out);
    SetLastError(error);
    h_enter();
    *error_after = GetLastError();
    ++h_calls;
}
// An executable copy of `n` original bytes followed by `jmp h_record`: the reference the hooked path is compared with.
inline void* h_reference(const unsigned char* bytes, unsigned n) {
    auto* p = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!p) return nullptr;
    std::memcpy(p, bytes, n);
    const std::int32_t rel = std::int32_t(reinterpret_cast<std::uintptr_t>(&h_record) - (reinterpret_cast<std::uintptr_t>(p) + n + 5));
    p[n] = 0xe9;
    std::memcpy(p + n + 1, &rel, 4);
    FlushInstructionCache(GetCurrentProcess(), p, n + 5);
    return p;
}
inline void h_sentinels(std::uint32_t seed, bool flags_set) {
    h_in.eax = 0xa0a00000u ^ seed;
    h_in.ebx = 0xb0b00000u ^ seed;
    h_in.ecx = 0xc0c00000u ^ seed;
    h_in.edx = 0xd0d00000u ^ seed;
    h_in.esi = 0x50500000u ^ seed;
    h_in.edi = 0xd1d10000u ^ seed;
    h_in.ebp = 0xbebe0000u ^ seed;
    h_in.flags = flags_set ? 0x00000ed7u : 0x00000202u; // CF|PF|AF|ZF|SF|DF|OF with IF, or IF alone (never TF)
    h_in.mxcsr = 0x1fa0;                                 // the precision flag set, default masks and rounding
    h_in.fpucw = 0x027f;                                 // 53-bit precision (not the default 0x037f)
    h_in.st0 = 1234.5;
    for (unsigned i = 0; i < 128; ++i) h_in.xmm[i] = static_cast<unsigned char>(0x11 * (i / 16 + 1) + i + seed);
}
