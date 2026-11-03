#pragma once
// Shared harness of the FL music family's native fixtures (fl_media_cue.cpp, fl_music_keep.cpp, fl_music_alert.cpp;
// built and run by verification/analysis/test_fl_{media_cue,music_keep,music_alert}.py, MinGW i686, natively through
// WSL interop, no Wine). It maps the user's installed X3FL.exe (argv[1], never copied into the repository) into a
// 64 KB-aligned private image the way the loader does (headers, sections, HIGHLOW relocations to the new base; imports
// left unbound), so the production modules install over the real relocated bytes, and it enters engine code with every
// register, DF, XMM7, the x87 control word, MXCSR and LastError set, capturing them at a planted return.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace fx {
inline unsigned checks = 0, failures = 0;
inline void check(bool value, const char* label, const char* detail = "") {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s %s\n", label, detail);
    }
}
inline int result() {
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
inline std::uintptr_t base = 0;
inline unsigned char* image = nullptr;
inline std::uint32_t image_size = 0;
inline std::vector<unsigned char> pristine;
inline std::uint32_t u32(const void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
inline std::uint32_t va(std::uint32_t rva) {
    return std::uint32_t(base + rva);
}
inline std::uint32_t get32(std::uint32_t rva) {
    std::uint32_t v;
    std::memcpy(&v, image + rva, 4);
    return v;
}
inline void put32(std::uint32_t rva, std::uint32_t v) {
    std::memcpy(image + rva, &v, 4);
}
inline void flush() {
    FlushInstructionCache(GetCurrentProcess(), image, image_size);
}
// The image's pages stay PAGE_EXECUTE_READWRITE (engine_patch's VirtualProtect calls then restore that).
inline bool map_exe(const char* path) {
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    const DWORD size = GetFileSize(f, nullptr);
    std::vector<unsigned char> file(size);
    DWORD got = 0;
    const bool read = ReadFile(f, file.data(), size, &got, nullptr) && got == size;
    CloseHandle(f);
    if (!read || size < 0x400) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + dos->e_lfanew);
    image_size = nt->OptionalHeader.SizeOfImage;
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!image) return false;
    base = reinterpret_cast<std::uintptr_t>(image);
    std::memcpy(image, file.data(), nt->OptionalHeader.SizeOfHeaders);
    const auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        std::memcpy(image + sec[i].VirtualAddress, file.data() + sec[i].PointerToRawData,
                    sec[i].SizeOfRawData < sec[i].Misc.VirtualSize ? sec[i].SizeOfRawData : sec[i].Misc.VirtualSize);
    const std::uint32_t delta = std::uint32_t(base - nt->OptionalHeader.ImageBase);
    const auto& rd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    for (std::uint32_t at = rd.VirtualAddress; at < rd.VirtualAddress + rd.Size;) {
        const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(image + at);
        if (block->SizeOfBlock < 8) break;
        const auto* entries = reinterpret_cast<const std::uint16_t*>(block + 1);
        for (unsigned k = 0; k < (block->SizeOfBlock - 8) / 2; ++k)
            if (entries[k] >> 12 == IMAGE_REL_BASED_HIGHLOW) {
                const std::uint32_t rva = block->VirtualAddress + (entries[k] & 0xfff);
                put32(rva, get32(rva) + delta);
            }
        at += block->SizeOfBlock;
    }
    pristine.assign(image, image + image_size);
    flush();
    return !(base & 0xffff);
}
// `push imm32; ret` at rva (6 bytes): a planted exit back into the harness.
inline void jump_at(std::uint32_t rva, const void* to) {
    image[rva] = 0x68;
    put32(rva + 1, u32(to));
    image[rva + 5] = 0xc3;
    FlushInstructionCache(GetCurrentProcess(), image + rva, 6);
}
inline void restore_bytes(std::uint32_t rva, unsigned n) {
    std::memcpy(image + rva, pristine.data() + rva, n);
    FlushInstructionCache(GetCurrentProcess(), image + rva, n);
}
inline bool same_as_pristine(std::uint32_t rva, unsigned n) {
    return !std::memcmp(image + rva, pristine.data() + rva, n);
}
// Log rows of the production modules (x3m::log / session_log::vlog stubs in each fixture append here).
inline char last_row[2048];
inline unsigned rows = 0;
inline void row(const char* format, va_list args) {
    std::vsnprintf(last_row, sizeof last_row, format, args);
    std::size_t n = std::strlen(last_row);
    while (n && last_row[n - 1] == '\n') last_row[--n] = 0;
    ++rows;
    std::printf("LOG %s\n", last_row);
}
}

// ---- The register harness (one translation unit per fixture includes this header: plain definitions, which the asm
// below references by name): enter `fx_target` with fx_in[] registers and fx_words[] pushed under a return address
// (fx_ret, or the harness label when 0), DF set, XMM7 / x87 CW / MXCSR loaded; capture everything at fx_exit. ----
extern "C" {
std::uint32_t fx_in[7]; // eax ecx edx ebx esi edi ebp
std::uint32_t fx_out[7], fx_out_flags = 0, fx_out_esp = 0, fx_esp_before = 0, fx_saved_esp = 0;
std::uint32_t fx_target = 0, fx_ret = 0, fx_words[16], fx_word_count = 0;
alignas(16) unsigned char fx_in_xmm[16], fx_out_xmm[16];
std::uint16_t fx_in_fcw = 0x027f, fx_out_fcw = 0, fx_def_fcw = 0;
std::uint32_t fx_in_mxcsr = 0x1fa0, fx_out_mxcsr = 0, fx_def_mxcsr = 0, fx_df = 1;
void fx_enter();
void fx_exit();
}
asm(".text\n\t"
    ".globl _fx_enter\n_fx_enter:\n\t"
    "pushal\n\tpushfl\n\tmovl %esp,_fx_saved_esp\n\tfnstcw _fx_def_fcw\n\tstmxcsr _fx_def_mxcsr\n\t"
    "movdqu _fx_in_xmm,%xmm7\n\tfldcw _fx_in_fcw\n\tldmxcsr _fx_in_mxcsr\n\t"
    "movl _fx_word_count,%ecx\n"
    "1:\n\ttestl %ecx,%ecx\n\tjz 2f\n\tpushl _fx_words-4(,%ecx,4)\n\tdecl %ecx\n\tjmp 1b\n"
    "2:\n\tmovl _fx_ret,%eax\n\ttestl %eax,%eax\n\tjnz 3f\n\tmovl $_fx_exit,%eax\n"
    "3:\n\tpushl %eax\n\tmovl %esp,_fx_esp_before\n\t"
    "movl _fx_in,%eax\n\tmovl _fx_in+4,%ecx\n\tmovl _fx_in+8,%edx\n\tmovl _fx_in+12,%ebx\n\tmovl _fx_in+16,%esi\n\t"
    "movl _fx_in+20,%edi\n\tmovl _fx_in+24,%ebp\n\t"
    "cmpl $0,_fx_df\n\tje 4f\n\tstd\n"
    "4:\n\tjmp *_fx_target\n\t"
    ".globl _fx_exit\n_fx_exit:\n\t"
    "pushfl\n\tpopl _fx_out_flags\n\tcld\n\tmovl %eax,_fx_out\n\tmovl %ecx,_fx_out+4\n\tmovl %edx,_fx_out+8\n\t"
    "movl %ebx,_fx_out+12\n\tmovl %esi,_fx_out+16\n\tmovl %edi,_fx_out+20\n\tmovl %ebp,_fx_out+24\n\t"
    "movl %esp,_fx_out_esp\n\tmovdqu %xmm7,_fx_out_xmm\n\tfnstcw _fx_out_fcw\n\tstmxcsr _fx_out_mxcsr\n\t"
    "fldcw _fx_def_fcw\n\tldmxcsr _fx_def_mxcsr\n\tmovl _fx_saved_esp,%esp\n\tpopfl\n\tpopal\n\tret\n\t");

namespace fx {
enum Reg { eax, ecx, edx, ebx, esi, edi, ebp };
constexpr std::uint32_t sentinel_error = 0x2a2a1357;
struct Call {
    std::uint32_t error = 0;
};
// One entry: registers, stack words (words[0] at [esp+4]), the return address (0: the harness), DF set by default.
inline Call enter(std::uint32_t target, std::initializer_list<std::uint32_t> regs, std::initializer_list<std::uint32_t> words,
                  std::uint32_t ret = 0) {
    unsigned i = 0;
    for (std::uint32_t r : regs) fx_in[i++] = r;
    i = 0;
    for (std::uint32_t w : words) fx_words[i++] = w;
    fx_word_count = i;
    fx_target = target;
    fx_ret = ret;
    for (unsigned k = 0; k < 16; ++k) fx_in_xmm[k] = static_cast<unsigned char>(0xa0 + k);
    SetLastError(sentinel_error);
    fx_enter();
    Call c;
    c.error = GetLastError();
    return c;
}
// XMM7, the x87 control word, MXCSR and DF as loaded (DF set by the harness).
inline bool cpu_kept() {
    return !std::memcmp(fx_in_xmm, fx_out_xmm, 16) && fx_out_fcw == fx_in_fcw && fx_out_mxcsr == fx_in_mxcsr &&
           (fx_out_flags & 0x400);
}
inline bool kept(std::initializer_list<Reg> regs) {
    for (Reg r : regs)
        if (fx_out[r] != fx_in[r]) return false;
    return true;
}
}
