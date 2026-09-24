// Wine fixture of the field-of-view patch's real write path (src/proxy/fov.cpp,
// compiled unchanged; see fov_patch_fixture_shim.h for the pass-through fault
// seam).
//
// Memory under test: four image sections of this executable, linked at the
// engine's own pages (MEM_IMAGE, the loader's protections, as X3AP.exe):
//   .x3mfvc  0x0041c000  the registry constructor stub: the verified 28-byte
//            window (fov_sites.h expected_window) at page offset 0x9cc, so the
//            imm32 sits at 0x0041c9dc, offset 4 of its aligned 8-byte word, as
//            in the engine. cdecl int(void* registry): ESI = registry, EBX = 0,
//            EDI = 0xf, room for the window's [esp+0x2c]/[esp+0x30] stores; it
//            runs the window and returns [registry+0x24], i.e. the immediate
//            the constructor stored; plus, on the same page, the registry
//            serializer's verified 23-byte load tail 0x0041c8b4..0x0041c8ca
//            (fov_sites.h expected_load_window: two `call 0x004e9420`, the
//            claimed `mov [ebp+0x24],eax; pop esi; mov al,1` at 0x0041c8c1,
//            `pop ebp; ret 8`), entered from a cdecl void(void* registry) at
//            0x0041c800 that pushes the caller's EBP/ESI sentinels the tail
//            pops and two stack arguments for its `ret 8`, loads sentinels
//            into EBX/ECX/ESI/EDI, sets EBP = the registry, and records the
//            registers and the stack balance after the return;
//   .x3mfvf  0x0041f000  the load caller's verified 9 bytes at 0x0041f790
//            (`call 0x0041c6e0; test al,al; je`), compared, never executed;
//   .x3mfvr  0x00421000  the per-frame reader: `mov edx,[0x00608504]; mov
//            esi,[edx+0x24]` at 0x00421148 (the reader contract), returning ESI;
//   .x3mfvs  0x0042d000  INS_SetFocus: the verified 31-byte case body
//            0x0042dbed..0x0042dc0b (fov_sites.h expected_setfocus_case, the
//            claimed `mov edx,[0x00608504]` at 0x0042dbf8, the store at
//            0x0042dc04, `call 0x004a47f0`), entered from a cdecl
//            void(const unsigned char* cell, void* task) at 0x0042db00 that
//            builds the dispatcher's frame ([ebp+0x18] = the argument cell,
//            [ebp+0xc] = the task), loads sentinels into EBX/ESI/EDI/EDX and
//            records EBX/ESI/EDI and the stack balance after the call;
//   .x3mfvx  0x004a4000  the callee: the verified 29-byte prefix at
//            0x004a47f0 (its flag and ECX writes run for real), then the
//            fixture records the pushed VM pointer, the task (EAX at the
//            call), EDX (the registry the tail reloaded) and the pushed 0,
//            and returns with `ret 8`;
//   .x3mfvl  0x004e9000  the stream reader 0x004e9420 the load tail calls: it
//            returns the next of two queued values and clobbers EDX (the load
//            stub's scratch) as the engine's reader does;
//   .x3mfvd  0x00608000  writable data: the registry slot at 0x00608504, the
//            VM slot at 0x006085e4, the callee's record at 0x00608600, the
//            stream reader's queue at 0x00608620 and the load thunk's record
//            at 0x00608640.
// Plus a VirtualAlloc page (MEM_PRIVATE) for install_at(). initialize() runs at
// the production constants; every step checks the executed constructor's
// result, the imm32 read back (ReadProcessMemory), the rest of the page, the
// page protection, the call counts on the seam, the log rows, the registry
// field and LastError; the INS_SetFocus steps execute the case body (with the
// claimed jmp, the remap stub and the tail when installed) and check the
// stored base and the preserved registers; the load steps execute the load
// tail the same way (with the load claim's jmp, the exact-match stub and the
// tail when installed). Never launches the game.
#include "../../src/proxy/fov.h"
#include "../../src/proxy/fov_sites.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/engine_memory.h"
#include "fov_patch_fixture_shim.h"  // declarations only: X3M_FOV_SHIM is not defined here
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace fov = x3m::fov;
namespace sites = x3m::fov::sites;
namespace engine_patch = x3m::engine_patch;

// ---- the fault seam (declared in fov_patch_fixture_shim.h) ----
namespace {
struct Faults {
    unsigned protect_fail = 0;   // bit i: the i-th VirtualProtect since arm() fails (ERROR_ACCESS_DENIED)
    unsigned read_fail = 0;      // bit i: the i-th read_code fails
    unsigned write_drop = 0;     // bit i: the i-th write_code stores nothing (and reports the atomic path)
    unsigned write_corrupt = 0;  // bit i: the i-th write_code stores 71 34 00 00 instead of the requested bytes
    unsigned protects = 0, reads = 0, writes = 0, flushes = 0, atomic_writes = 0;
    DWORD first_previous = 0, first_during = 0;  // what the first VirtualProtect returned / VirtualQuery saw after it
    void (*on_read)(unsigned index) = nullptr;   // runs before the i-th read_code (a concurrent foreign store)
};
Faults g;
void arm(const Faults& f = Faults{}) { g = f; }
bool bit(unsigned mask, unsigned i) { return i < 32 && ((mask >> i) & 1u); }
}
namespace x3m::fov_fixture {
BOOL WINAPI virtual_protect(LPVOID address, SIZE_T size, DWORD protection, PDWORD previous) {
    const unsigned i = g.protects++;
    if (bit(g.protect_fail, i)) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    const BOOL ok = ::VirtualProtect(address, size, protection, previous);
    if (ok && i == 0) {
        g.first_previous = *previous;
        MEMORY_BASIC_INFORMATION m{};
        if (VirtualQuery(address, &m, sizeof m)) g.first_during = m.Protect;
    }
    return ok;
}
BOOL WINAPI flush_instruction_cache(HANDLE process, LPCVOID address, SIZE_T size) {
    ++g.flushes;
    return ::FlushInstructionCache(process, address, size);
}
}
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count) {
    const unsigned i = g.reads++;
    if (g.on_read) g.on_read(i);
    if (bit(g.read_fail, i)) return false;
    return read_code(address, out, count);
}
bool fixture_write_code(std::uintptr_t address, const unsigned char* bytes, unsigned n) {
    const unsigned i = g.writes++;
    if (bit(g.write_drop, i)) return true;
    unsigned char corrupt[8]{};
    if (bit(g.write_corrupt, i) && n == 4) { corrupt[0] = 0x71; corrupt[1] = 0x34; bytes = corrupt; }
    const bool atomic = write_code(address, bytes, n);
    if (atomic) ++g.atomic_writes;
    return atomic;
}
}

// ---- production dependencies the TU links against ----
namespace {
std::vector<std::string> log_lines;
HANDLE restore_log = INVALID_HANDLE_VALUE;
LONG restore_log_read = 0;
bool executable_ok = true;
}
namespace x3m {
void log(const char* format, ...) {
    char text[512];
    std::va_list a;
    va_start(a, format);
    std::vsnprintf(text, sizeof text, format, a);
    va_end(a);
    log_lines.emplace_back(text);
    std::printf("LOG %s\n", text);
}
HANDLE log_handle() noexcept { return restore_log; }
}
namespace x3m::object_trace { bool executable_verified() { return executable_ok; } }

// ---- the engine pages ----
extern "C" unsigned char fov_ctor_page[], fov_reader_page[], fov_setfocus_page[], fov_callee_page[], fov_caller_page[], fov_stream_page[];
extern "C" std::uint32_t fov_registry_slot, fov_vm_slot, fov_seen[8], fov_load_values[4], fov_load_seen[8];
asm(R"(
    .section .x3mfvc,"xr"
    .balign 4096, 0xcc
    .globl _fov_ctor_page
_fov_ctor_page:
    .byte 0x55                                   # +000 push ebp
    .byte 0x8b,0xec                              # +001 mov ebp,esp
    .byte 0x53,0x56,0x57                         # +003 push ebx; push esi; push edi
    .byte 0x83,0xec,0x40                         # +006 sub esp,0x40               ; the window stores [esp+0x2c], [esp+0x30]
    .byte 0x8b,0x75,0x08                         # +009 mov esi,[ebp+8]            ; the registry
    .byte 0x33,0xdb                              # +00c xor ebx,ebx
    .byte 0xbf,0x0f,0x00,0x00,0x00               # +00e mov edi,0xf
    .byte 0xe9,0xb4,0x09,0x00,0x00               # +013 jmp +0x9cc
    .fill 0x800 - (. - _fov_ctor_page), 1, 0xcc
    .byte 0x53,0x56,0x57,0x55                    # +800 push ebx; push esi; push edi; push ebp   <- load entry, cdecl void(void* registry)
    .byte 0x8b,0x44,0x24,0x14                    # +804 mov eax,[esp+0x14]         ; the registry
    .byte 0x89,0x25,0x5c,0x86,0x60,0x00          # +808 mov [0x0060865c],esp       ; ESP before the frame
    .byte 0x6a,0x00,0x6a,0x00                    # +80e push 0; push 0             ; the two arguments `ret 8` pops
    .byte 0xe8,0x3d,0x00,0x00,0x00               # +812 call +854
    .byte 0x89,0x1d,0x40,0x86,0x60,0x00          # +817 mov [0x00608640],ebx
    .byte 0x89,0x0d,0x44,0x86,0x60,0x00          # +81d mov [0x00608644],ecx
    .byte 0x89,0x3d,0x48,0x86,0x60,0x00          # +823 mov [0x00608648],edi
    .byte 0x89,0x35,0x4c,0x86,0x60,0x00          # +829 mov [0x0060864c],esi       ; the ESI the tail popped
    .byte 0x89,0x2d,0x50,0x86,0x60,0x00          # +82f mov [0x00608650],ebp       ; the EBP the tail popped
    .byte 0xa3,0x54,0x86,0x60,0x00               # +835 mov [0x00608654],eax       ; AL = 1 from the tail
    .byte 0x89,0x25,0x58,0x86,0x60,0x00          # +83a mov [0x00608658],esp       ; equal to +808's when balanced
    .byte 0x5d,0x5f,0x5e,0x5b                    # +840 pop ebp; pop edi; pop esi; pop ebx
    .byte 0xc3                                   # +844 ret
    .fill 0x854 - (. - _fov_ctor_page), 1, 0xcc
    .byte 0x68,0x66,0x66,0x66,0x66               # +854 push 0x66666666            ; the caller's EBP, popped by the tail
    .byte 0x68,0x77,0x77,0x77,0x77               # +859 push 0x77777777            ; the caller's ESI, popped by the tail
    .byte 0x8b,0xe8                              # +85e mov ebp,eax                ; EBP = the registry
    .byte 0xbb,0x11,0x11,0x11,0x11               # +860 mov ebx,0x11111111
    .byte 0xb9,0x44,0x44,0x44,0x44               # +865 mov ecx,0x44444444
    .byte 0xbe,0x22,0x22,0x22,0x22               # +86a mov esi,0x22222222         ; (the stream in the engine)
    .byte 0xbf,0x33,0x33,0x33,0x33               # +86f mov edi,0x33333333
    .byte 0xe9,0x3b,0x00,0x00,0x00               # +874 jmp +8b4
    .fill 0x8b4 - (. - _fov_ctor_page), 1, 0xcc
    .byte 0xe8,0x67,0xcb,0x0c,0x00               # +8b4 call 0x004e9420            <- load window
    .byte 0x89,0x45,0x20                         # +8b9 mov [ebp+0x20],eax
    .byte 0xe8,0x5f,0xcb,0x0c,0x00               # +8bc call 0x004e9420            ; the saved focus
    .byte 0x89,0x45,0x24                         # +8c1 mov [ebp+0x24],eax         <- claimed (load store)
    .byte 0x5e                                   # +8c4 pop esi
    .byte 0xb0,0x01                              # +8c5 mov al,1
    .byte 0x5d                                   # +8c7 pop ebp
    .byte 0xc2,0x08,0x00                         # +8c8 ret 8
    .fill 0x9cc - (. - _fov_ctor_page), 1, 0xcc
    .byte 0x89,0x5e,0x20                         # +9cc mov [esi+0x20],ebx         <- window
    .byte 0xc6,0x46,0x19,0x01                    # +9cf mov byte [esi+0x19],1
    .byte 0x88,0x5e,0x1a                         # +9d3 mov [esi+0x1a],bl
    .byte 0x89,0x5e,0x1c                         # +9d6 mov [esi+0x1c],ebx
    .byte 0xc7,0x46,0x24,0x00,0x40,0x00,0x00     # +9d9 mov dword [esi+0x24],0x4000 <- site; imm32 at +9dc
    .byte 0x89,0x7c,0x24,0x30                    # +9e0 mov [esp+0x30],edi
    .byte 0x89,0x5c,0x24,0x2c                    # +9e4 mov [esp+0x2c],ebx
    .byte 0x8b,0x46,0x24                         # +9e8 mov eax,[esi+0x24]         ; the immediate as stored
    .byte 0x83,0xc4,0x40                         # +9eb add esp,0x40
    .byte 0x5f,0x5e,0x5b                         # +9ee pop edi; pop esi; pop ebx
    .byte 0x5d                                   # +9f1 pop ebp
    .byte 0xc3                                   # +9f2 ret
    .balign 4096, 0xcc
    .section .x3mfvr,"xr"
    .balign 4096, 0xcc
    .globl _fov_reader_page
_fov_reader_page:
    .fill 0x144, 1, 0xcc
    .byte 0x56,0x90,0x90,0x90                    # +144 push esi; nop x3
    .byte 0x8b,0x15,0x04,0x85,0x60,0x00          # +148 mov edx,[0x00608504]       <- reader contract
    .byte 0x8b,0x72,0x24                         # +14e mov esi,[edx+0x24]
    .byte 0x8b,0xc6                              # +151 mov eax,esi
    .byte 0x5e                                   # +153 pop esi
    .byte 0xc3                                   # +154 ret
    .balign 4096, 0xcc
    .section .x3mfvs,"xr"
    .balign 4096, 0xcc
    .globl _fov_setfocus_page
_fov_setfocus_page:
    .fill 0xb00, 1, 0xcc
    .byte 0x53,0x56,0x57,0x55                    # +b00 push ebx; push esi; push edi; push ebp
    .byte 0x83,0xec,0x20                         # +b04 sub esp,0x20
    .byte 0x8b,0x44,0x24,0x34                    # +b07 mov eax,[esp+0x34]         ; the argument cell
    .byte 0x8b,0x4c,0x24,0x38                    # +b0b mov ecx,[esp+0x38]         ; the task
    .byte 0x8b,0xec                              # +b0f mov ebp,esp
    .byte 0x89,0x45,0x18                         # +b11 mov [ebp+0x18],eax
    .byte 0x89,0x4d,0x0c                         # +b14 mov [ebp+0xc],ecx
    .byte 0xbb,0x11,0x11,0x11,0x11               # +b17 mov ebx,0x11111111
    .byte 0xbe,0x22,0x22,0x22,0x22               # +b1c mov esi,0x22222222
    .byte 0xbf,0x33,0x33,0x33,0x33               # +b21 mov edi,0x33333333
    .byte 0xba,0x44,0x44,0x44,0x44               # +b26 mov edx,0x44444444         ; dead at the site: the tail reloads it
    .byte 0xe9,0xbd,0x00,0x00,0x00               # +b2b jmp +bed
    .fill 0xbed - (. - _fov_setfocus_page), 1, 0xcc
    .byte 0x8b,0x45,0x18                         # +bed mov eax,[ebp+0x18]         <- case window
    .byte 0x8b,0x48,0x01                         # +bf0 mov ecx,[eax+1]            ; F from the script
    .byte 0xa1,0xe4,0x85,0x60,0x00               # +bf3 mov eax,[0x006085e4]       ; VM
    .byte 0x8b,0x15,0x04,0x85,0x60,0x00          # +bf8 mov edx,[0x00608504]       <- claimed
    .byte 0x6a,0x00                              # +bfe push 0
    .byte 0x50                                   # +c00 push eax
    .byte 0x8b,0x45,0x0c                         # +c01 mov eax,[ebp+0xc]          ; task
    .byte 0x89,0x4a,0x24                         # +c04 mov [edx+0x24],ecx         <- INS_SetFocus store
    .byte 0xe8,0xe4,0x6b,0x07,0x00               # +c07 call 0x004a47f0
    .byte 0x89,0x1d,0x10,0x86,0x60,0x00          # +c0c mov [0x00608610],ebx       ; (the engine's jmp 0x0042f04c is outside the window)
    .byte 0x89,0x35,0x14,0x86,0x60,0x00          # +c12 mov [0x00608614],esi
    .byte 0x89,0x3d,0x18,0x86,0x60,0x00          # +c18 mov [0x00608618],edi
    .byte 0x8b,0xc4                              # +c1e mov eax,esp
    .byte 0x2b,0xc5                              # +c20 sub eax,ebp                ; 0 when the stack is balanced
    .byte 0xa3,0x1c,0x86,0x60,0x00               # +c22 mov [0x0060861c],eax
    .byte 0x83,0xc4,0x20                         # +c27 add esp,0x20
    .byte 0x5d,0x5f,0x5e,0x5b                    # +c2a pop ebp; pop edi; pop esi; pop ebx
    .byte 0xc3                                   # +c2e ret
    .balign 4096, 0xcc
    .section .x3mfvx,"xr"
    .balign 4096, 0xcc
    .globl _fov_callee_page
_fov_callee_page:
    .fill 0x7f0, 1, 0xcc
    .byte 0x56                                   # +7f0 push esi                   <- verified callee prefix
    .byte 0x8b,0xf0                              # +7f1 mov esi,eax
    .byte 0x57                                   # +7f3 push edi
    .byte 0x8d,0x7e,0x28                         # +7f4 lea edi,[esi+0x28]
    .byte 0x66,0xc7,0x46,0x20,0x01,0x00          # +7f7 mov word [esi+0x20],1
    .byte 0x80,0x3f,0x08                         # +7fd cmp byte [edi],8
    .byte 0x72,0x07                              # +800 jb +809                    ; the task's byte is 0: taken
    .byte 0x8b,0xcf                              # +802 mov ecx,edi
    .byte 0xe8,0x37,0x3a,0x00,0x00               # +804 call 0x004a8240            ; (not reached)
    .byte 0x8b,0x4c,0x24,0x0c                    # +809 mov ecx,[esp+0xc]          ; the pushed VM pointer
    .byte 0x89,0x0d,0x00,0x86,0x60,0x00          # +80d mov [0x00608600],ecx
    .byte 0x89,0x35,0x04,0x86,0x60,0x00          # +813 mov [0x00608604],esi       ; the task (EAX at the call)
    .byte 0x89,0x15,0x08,0x86,0x60,0x00          # +819 mov [0x00608608],edx       ; the registry the tail reloaded
    .byte 0x8b,0x44,0x24,0x10                    # +81f mov eax,[esp+0x10]         ; the pushed 0
    .byte 0xa3,0x0c,0x86,0x60,0x00               # +823 mov [0x0060860c],eax
    .byte 0x5f,0x5e                              # +828 pop edi; pop esi
    .byte 0xc2,0x08,0x00                         # +82a ret 8
    .balign 4096, 0xcc
    .section .x3mfvf,"xr"
    .balign 4096, 0xcc
    .globl _fov_caller_page
_fov_caller_page:
    .fill 0x790, 1, 0xcc
    .byte 0xe8,0x4b,0xcf,0xff,0xff               # +790 call 0x0041c6e0            <- load caller (compared only)
    .byte 0x84,0xc0                              # +795 test al,al
    .byte 0x74,0xda                              # +797 je 0x0041f773
    .balign 4096, 0xcc
    .section .x3mfvl,"xr"
    .balign 4096, 0xcc
    .globl _fov_stream_page
_fov_stream_page:
    .fill 0x420, 1, 0xcc
    .byte 0x8b,0x15,0x28,0x86,0x60,0x00          # +420 mov edx,[0x00608628]       <- stream reader: the queue index
    .byte 0x8b,0x04,0x95,0x20,0x86,0x60,0x00     # +426 mov eax,[edx*4+0x00608620]
    .byte 0x42                                   # +42d inc edx
    .byte 0x83,0xe2,0x01                         # +42e and edx,1
    .byte 0x89,0x15,0x28,0x86,0x60,0x00          # +431 mov [0x00608628],edx
    .byte 0xba,0x55,0x55,0x55,0x55               # +437 mov edx,0x55555555         ; EDX clobbered, as the engine's reader sets it
    .byte 0xc3                                   # +43c ret
    .balign 4096, 0xcc
    .section .x3mfvd,"dw"
    .balign 4096, 0
    .fill 0x504, 1, 0
    .globl _fov_registry_slot
_fov_registry_slot:
    .long 0
    .fill 0x5e4 - 0x508, 1, 0
    .globl _fov_vm_slot
_fov_vm_slot:
    .long 0
    .fill 0x600 - 0x5e8, 1, 0
    .globl _fov_seen
_fov_seen:
    .fill 0x20, 1, 0
    .globl _fov_load_values
_fov_load_values:
    .fill 0x10, 1, 0
    .fill 0x640 - 0x630, 1, 0
    .globl _fov_load_seen
_fov_load_seen:
    .fill 0x20, 1, 0
    .balign 4096, 0
    .text
)");

namespace {
constexpr unsigned page_size = 4096, window_offset = 0x9cc, write_at = window_offset + sites::write_offset;
constexpr std::uintptr_t ctor_page = sites::window_va & ~std::uintptr_t(page_size - 1);
constexpr std::uintptr_t reader_entry = 0x00421144, setfocus_entry = 0x0042db00, load_entry = 0x0041c800;
constexpr unsigned load_site_offset = sites::load_site_va & (page_size - 1), load_window_offset = sites::load_window_va & (page_size - 1);
constexpr unsigned setfocus_case_offset = sites::setfocus_case_va & (page_size - 1), setfocus_site_offset = sites::setfocus_site_va & (page_size - 1);
static_assert(ctor_page + window_offset == sites::window_va, "the stub's window sits at the engine's page offset");
using Ctor = int (*)(void* registry);
using Reader = std::uint32_t (*)();
using SetFocusCall = void (*)(const unsigned char* cell, void* task);
using LoadCall = void (*)(void* registry);

unsigned checks = 0, failures = 0;
void check(bool ok, const char* name, const char* detail = "") {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s%s%s\n", ok ? "pass" : "FAIL", name, !ok && *detail ? " " : "", ok ? "" : detail);
}
alignas(16) unsigned char registry_a[0x40], registry_b[0x40];
std::uint32_t& focus_of(unsigned char* registry) { return *reinterpret_cast<std::uint32_t*>(registry + sites::registry_focus_offset); }
// One CTOR row per executed step: the immediate the constructor stored into a fresh registry.
std::uint32_t construct(const char* memory, const char* step, const unsigned char* page) {
    std::memset(registry_b, 0xa5, sizeof registry_b);
    const std::uint32_t stored = std::uint32_t(reinterpret_cast<Ctor>(reinterpret_cast<std::uintptr_t>(page))(registry_b));
    const bool fields = registry_b[0x19] == 1 && registry_b[0x1a] == 0 && *reinterpret_cast<std::uint32_t*>(registry_b + 0x1c) == 0 &&
                        *reinterpret_cast<std::uint32_t*>(registry_b + 0x20) == 0 && focus_of(registry_b) == stored;
    std::printf("CTOR memory=%s step=%s focus=0x%04lx fields=%u\n", memory, step, static_cast<unsigned long>(stored), fields ? 1u : 0u);
    return fields ? stored : 0xffffffffu;
}
std::uint32_t read_base() { return reinterpret_cast<Reader>(reader_entry)(); }
// One execution of the INS_SetFocus case body with F in the argument cell: the base it stored
// into registry_a (the slot must point at it). *preserved: the callee saw the VM pointer, the task,
// EDX = the registry and the pushed 0; EBX/ESI/EDI came back as loaded; the stack is balanced;
// LastError unchanged. One SETFOCUS row per call.
alignas(16) unsigned char task_buffer[0x40];
std::uint32_t run_setfocus(const char* step, std::uint32_t focus, bool* preserved) {
    unsigned char cell[5] = {0x01, static_cast<unsigned char>(focus), static_cast<unsigned char>(focus >> 8), static_cast<unsigned char>(focus >> 16),
                             static_cast<unsigned char>(focus >> 24)};
    std::memset(task_buffer, 0, sizeof task_buffer);
    std::memset(fov_seen, 0xee, sizeof fov_seen);
    fov_vm_slot = 0x0bad0f00u;
    focus_of(registry_a) = 0xa5a5a5a5u;
    SetLastError(0x2bad);
    reinterpret_cast<SetFocusCall>(setfocus_entry)(cell, task_buffer);
    const bool error_kept = GetLastError() == 0x2bad;
    const std::uint32_t stored = focus_of(registry_a);
    *preserved = error_kept && fov_seen[0] == 0x0bad0f00u && fov_seen[1] == static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(task_buffer)) &&
                 fov_seen[2] == fov_registry_slot && fov_seen[3] == 0 && fov_seen[4] == 0x11111111u && fov_seen[5] == 0x22222222u &&
                 fov_seen[6] == 0x33333333u && fov_seen[7] == 0 && task_buffer[0x20] == 1 && task_buffer[0x21] == 0;
    std::printf("SETFOCUS step=%s in=0x%04lx out=0x%04lx preserved=%u\n", step, static_cast<unsigned long>(focus), static_cast<unsigned long>(stored), *preserved ? 1u : 0u);
    return stored;
}
// One execution of the load tail with `focus` as the saved value (the stream reader's second result;
// the first, 0x5a5a1234, lands in +0x20): the value it stored into registry_a+0x24. *preserved: +0x20
// got the first value; EBX/ECX/EDI came back as loaded; ESI and EBP are the sentinels the tail popped
// (so ESP was the site's at the tail); AL = 1 with EAX's upper bytes the stored value's; the stack
// is balanced after `ret 8`; LastError unchanged. One LOAD row per call.
std::uint32_t run_load(const char* step, std::uint32_t focus, bool* preserved, bool quiet = false) {
    fov_load_values[0] = 0x5a5a1234u;
    fov_load_values[1] = focus;
    fov_load_values[2] = 0;
    std::memset(fov_load_seen, 0xee, 8 * sizeof(std::uint32_t));
    focus_of(registry_a) = 0xa5a5a5a5u;
    *reinterpret_cast<std::uint32_t*>(registry_a + 0x20) = 0xa5a5a5a5u;
    SetLastError(0x2bad);
    reinterpret_cast<LoadCall>(load_entry)(registry_a);
    const bool error_kept = GetLastError() == 0x2bad;
    const std::uint32_t stored = focus_of(registry_a);
    *preserved = error_kept && *reinterpret_cast<std::uint32_t*>(registry_a + 0x20) == 0x5a5a1234u && fov_load_values[2] == 0 &&
                 fov_load_seen[0] == 0x11111111u && fov_load_seen[1] == 0x44444444u && fov_load_seen[2] == 0x33333333u &&
                 fov_load_seen[3] == 0x77777777u && fov_load_seen[4] == 0x66666666u && fov_load_seen[5] == ((stored & 0xffffff00u) | 1u) &&
                 fov_load_seen[6] == fov_load_seen[7];
    if (!quiet)
        std::printf("LOAD step=%s in=0x%04lx out=0x%04lx preserved=%u\n", step, static_cast<unsigned long>(focus), static_cast<unsigned long>(stored), *preserved ? 1u : 0u);
    return stored;
}
// The load site's five bytes now (false when unreadable).
bool read_load_jmp(unsigned char out[5]) {
    SIZE_T n = 0;
    return ReadProcessMemory(GetCurrentProcess(), fov_ctor_page + load_site_offset, out, 5, &n) && n == 5;
}
// The whole .x3mfvs page against its reference, except the five jmp bytes at 0x0042dbf8 when `jmp` is given.
bool setfocus_page_is(const unsigned char* reference, const unsigned char* jmp) {
    unsigned char now[page_size];
    SIZE_T n = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), fov_setfocus_page, now, page_size, &n) || n != page_size) return false;
    const unsigned s = setfocus_site_offset;
    if (!jmp) return !std::memcmp(now, reference, page_size);
    return !std::memcmp(now, reference, s) && !std::memcmp(now + s, jmp, 5) && !std::memcmp(now + s + 5, reference + s + 5, page_size - s - 5);
}
DWORD protection(const void* at) {
    MEMORY_BASIC_INFORMATION m{};
    return VirtualQuery(at, &m, sizeof m) ? m.Protect : 0;
}
bool read_span(const unsigned char* page, unsigned char out[4]) {
    SIZE_T n = 0;
    return ReadProcessMemory(GetCurrentProcess(), page + write_at, out, 4, &n) && n == 4;
}
bool span_is(const unsigned char* page, const unsigned char want[4]) {
    unsigned char now[4]{};
    return read_span(page, now) && !std::memcmp(now, want, 4);
}
bool writable_image(DWORD protect) { return protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY; }
// The page equals `reference` except for the four imm32 bytes, which must be `span`, and, when `load`
// is given, the five load-site bytes at 0x0041c8c1, which must be `load` (the load claim's jmp).
bool page_is(const unsigned char* page, const unsigned char* reference, const unsigned char span[4], const unsigned char* load = nullptr) {
    unsigned char now[page_size], want[page_size];
    SIZE_T n = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), page, now, page_size, &n) || n != page_size) return false;
    std::memcpy(want, reference, page_size);
    std::memcpy(want + write_at, span, 4);
    if (load) std::memcpy(want + load_site_offset, load, 5);
    return !std::memcmp(now, want, page_size);
}
bool poke(unsigned char* at, const unsigned char* bytes, unsigned n) {
    DWORD old = 0, unused = 0;
    if (!::VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(at, bytes, n);
    const bool ok = ::VirtualProtect(at, n, old, &unused) != FALSE;
    ::FlushInstructionCache(GetCurrentProcess(), at, n);
    return ok;
}
// The on_read hook for the rollback_setfocus() failure: at the load jmp's read-back (seam read 9) a foreign
// jmp replaces ours at 0x0042dbf8, so taking the INS_SetFocus claim back finds bytes that are not ours.
unsigned char hook_saved_jmp[5]{};
bool hook_fired = false;
const unsigned char hook_foreign_jmp[5] = {0xe9, 0x11, 0x22, 0x33, 0x44};
void foreign_setfocus_on_load_readback(unsigned index) {
    if (index != 9 || hook_fired) return;
    SIZE_T n = 0;
    hook_fired = ReadProcessMemory(GetCurrentProcess(), fov_setfocus_page + setfocus_site_offset, hook_saved_jmp, 5, &n) && n == 5 &&
                 poke(fov_setfocus_page + setfocus_site_offset, hook_foreign_jmp, 5);
}
void set_fov(const wchar_t* value) { SetEnvironmentVariableW(L"X3M_FOV", value); }
void set_slot(const void* registry) { fov_registry_slot = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(registry)); x3m::engine_memory::reset(); }
std::string last_log() { return log_lines.empty() ? std::string() : log_lines.back(); }
std::string new_restore_rows() {
    std::string out;
    if (restore_log == INVALID_HANDLE_VALUE) return out;
    const LONG end = LONG(GetFileSize(restore_log, nullptr));
    if (end > restore_log_read) {
        std::vector<char> buffer(end - restore_log_read);
        DWORD got = 0;
        SetFilePointer(restore_log, restore_log_read, nullptr, FILE_BEGIN);
        if (ReadFile(restore_log, buffer.data(), DWORD(buffer.size()), &got, nullptr)) out.assign(buffer.data(), got);
        SetFilePointer(restore_log, 0, nullptr, FILE_END);
        restore_log_read = end;
    }
    std::size_t start = 0;
    for (std::size_t nl; (nl = out.find('\n', start)) != std::string::npos; start = nl + 1)
        std::printf("LOG %s\n", out.substr(start, nl - start).c_str());
    return out;
}
bool contains(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }
bool one_row(const std::string& rows, const char* needle) {
    std::size_t lines = 0;
    for (char c : rows) lines += c == '\n';
    return lines == 1 && contains(rows, needle);
}
std::string install_row(const char* status, const char* reason, std::uint32_t value, const char* setting, const char* write, const char* registry, const char* before,
                        const char* setfocus = "none", const char* setfocus_write = "none", const char* load = "none", const char* load_write = "none") {
    char text[340];
    std::snprintf(text, sizeof text, "fov site=%08lx status=%s reason=%s value=0x%04lx vertical_deg=%.2f setting=%s write=%s registry=%s registry_before=%s "
                  "setfocus=%s setfocus_write=%s load=%s load_write=%s",
                  static_cast<unsigned long>(sites::write_va), status, reason, static_cast<unsigned long>(value), sites::vertical_for_focus(value), setting, write, registry, before,
                  setfocus, setfocus_write, load, load_write);
    return text;
}
bool initialize_checked(const char* name) {
    SetLastError(0x2bad);
    const bool r = fov::initialize();
    char label[96];
    std::snprintf(label, sizeof label, "%s_lasterror_preserved", name);
    check(GetLastError() == 0x2bad, label);
    return r;
}
bool shutdown_checked(const char* name) {
    SetLastError(0x2bad);
    const bool r = fov::shutdown();
    char label[96];
    std::snprintf(label, sizeof label, "%s_lasterror_preserved", name);
    check(GetLastError() == 0x2bad, label);
    return r;
}
std::string shutdown_rows(const char* name) {
    shutdown_checked(name);
    return new_restore_rows();
}
double qpc_us(LARGE_INTEGER a, LARGE_INTEGER b) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return double(b.QuadPart - a.QuadPart) * 1e6 / double(f.QuadPart);
}
LONG WINAPI unhandled(EXCEPTION_POINTERS* e) {
    std::printf("CRASH code=%08lx address=%p checks=%u\n", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress, checks);
    std::printf("RESULT checks=%u failures=%u\n", checks + 1, failures + 1);
    std::fflush(stdout);
    ExitProcess(3);
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // the msvcrt treats _IOLBF as full buffering
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(unhandled);
    const unsigned char* const original = sites::expected_write;
    unsigned char ours[4], other[4];
    sites::encode(0x3470, ours);
    sites::encode(0x3471, other);
    const unsigned char corrupt[4] = {0x71, 0x34, 0x00, 0x00};
    char temp_dir[MAX_PATH]{}, path[MAX_PATH + 64]{}, detail[200];
    GetTempPathA(MAX_PATH, temp_dir);
    std::snprintf(path, sizeof path, "%sx3m-fov-patch-%lu.log", temp_dir, GetCurrentProcessId());
    restore_log = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    check(restore_log != INVALID_HANDLE_VALUE, "restore_log_opened");

    // ---- the pages ----
    unsigned char* const engine = fov_ctor_page;
    const bool placed = reinterpret_cast<std::uintptr_t>(engine) == ctor_page && reinterpret_cast<std::uintptr_t>(fov_reader_page) == 0x00421000 &&
                        reinterpret_cast<std::uintptr_t>(fov_setfocus_page) == 0x0042d000 && reinterpret_cast<std::uintptr_t>(fov_callee_page) == 0x004a4000 &&
                        reinterpret_cast<std::uintptr_t>(fov_caller_page) == 0x0041f000 && reinterpret_cast<std::uintptr_t>(fov_stream_page) == 0x004e9000 &&
                        reinterpret_cast<std::uintptr_t>(fov_load_values) == 0x00608620 && reinterpret_cast<std::uintptr_t>(fov_load_seen) == 0x00608640 &&
                        reinterpret_cast<std::uintptr_t>(&fov_registry_slot) == sites::registry_slot_va && reinterpret_cast<std::uintptr_t>(&fov_vm_slot) == 0x006085e4 &&
                        reinterpret_cast<std::uintptr_t>(fov_seen) == 0x00608600;
    check(placed, "pages_at_engine_vas");
    if (!placed) { std::printf("RESULT checks=%u failures=%u\n", checks, failures); return 1; }
    check(!std::memcmp(engine + window_offset, sites::expected_window, sites::window_length) &&
          !std::memcmp(fov_reader_page + 0x148, sites::expected_reader, sites::reader_length) && !std::memcmp(fov_setfocus_page + 0xc04, sites::expected_setfocus, sites::setfocus_length),
          "window_reader_setfocus_bytes_at_engine_vas");
    check(((reinterpret_cast<std::uintptr_t>(engine) + write_at) & 7u) == 4u && reinterpret_cast<std::uintptr_t>(engine) + write_at == sites::write_va,
          "engine_imm32_offset_4_of_its_qword");
    check(!std::memcmp(fov_setfocus_page + setfocus_case_offset, sites::expected_setfocus_case, sites::setfocus_case_length) &&
          !std::memcmp(fov_callee_page + (sites::setfocus_callee_va & (page_size - 1)), sites::expected_setfocus_callee, sites::setfocus_callee_length) &&
          (sites::setfocus_site_va & 7u) == 0u, "setfocus_case_and_callee_bytes_at_engine_vas_site_qword_aligned");
    check(!std::memcmp(engine + load_window_offset, sites::expected_load_window, sites::load_window_length) &&
          !std::memcmp(fov_caller_page + (sites::load_caller_va & (page_size - 1)), sites::expected_load_caller, sites::load_caller_length) &&
          reinterpret_cast<std::uintptr_t>(engine) + load_site_offset == sites::load_site_va && (sites::load_site_va & ~std::uintptr_t(7)) == ((sites::load_site_va + 4) & ~std::uintptr_t(7)),
          "load_window_and_caller_bytes_at_engine_vas_site_in_one_qword");
    unsigned char setfocus_reference[page_size];
    std::memcpy(setfocus_reference, fov_setfocus_page, page_size);
    unsigned char reference[page_size];
    std::memcpy(reference, engine, page_size);
    MEMORY_BASIC_INFORMATION mi{};
    VirtualQuery(engine, &mi, sizeof mi);
    std::printf("MEMORY memory=engine type=0x%lx protect=0x%lx allocation_protect=0x%lx\n", mi.Type, mi.Protect, mi.AllocationProtect);
    check(mi.Type == MEM_IMAGE && mi.Protect == PAGE_EXECUTE_READ, "engine_page_is_mem_image_execute_read");
    VirtualQuery(&fov_registry_slot, &mi, sizeof mi);
    check(mi.Type == MEM_IMAGE && (mi.Protect == PAGE_READWRITE || mi.Protect == PAGE_WRITECOPY), "registry_slot_is_writable_image_data");
    auto* const priv = static_cast<unsigned char*>(VirtualAlloc(nullptr, page_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    DWORD old = 0;
    check(priv && (std::memcpy(priv, reference, page_size), ::VirtualProtect(priv, page_size, PAGE_EXECUTE_READ, &old)) &&
          ::FlushInstructionCache(GetCurrentProcess(), priv, page_size), "private_page_execute_read");
    if (!priv) { std::printf("RESULT checks=%u failures=%u\n", checks, failures); return 1; }

    set_slot(nullptr);
    check(construct("engine", "before", engine) == 0x4000, "engine_before_ctor_stores_4000");
    check(construct("private", "before", priv) == 0x4000, "private_before_ctor_stores_4000");
    std::memset(registry_a, 0, sizeof registry_a);
    focus_of(registry_a) = 0x4000;
    set_slot(registry_a);
    check(read_base() == 0x4000, "reader_before_reads_registry_4000");
    bool kept = false;
    check(run_setfocus("before", 0x471c, &kept) == 0x471c && kept, "setfocus_before_install_stores_471c_registers_kept");
    check(run_load("before", 0x4000, &kept) == 0x4000 && kept, "load_before_install_stores_4000_registers_kept");
    set_slot(nullptr);

    // ---- off and refusals through initialize(): nothing protected, written or flushed ----
    struct Refusal { const wchar_t* setting; bool exe; const char* name; std::string row; };
    const Refusal refusals[] = {
        {nullptr, true, "default_unset_off", install_row("off", "game", 0x4000, "-", "none", "skipped", "-")},
        {L"game", true, "game_off", install_row("off", "game", 0x4000, "game", "none", "skipped", "-")},
        {L"69.9", true, "refuse_below_70", install_row("refused", "out_of_range", 0x4000, "69.9", "none", "skipped", "-")},
        {L"100.5", true, "refuse_above_100", install_row("refused", "out_of_range", 0x4000, "100.5", "none", "skipped", "-")},
        {L"58.7155", true, "refuse_old_vertical_value", install_row("refused", "out_of_range", 0x4000, "58.7155", "none", "skipped", "-")},
        {L"Game", true, "refuse_invalid_setting", install_row("refused", "invalid_setting", 0x4000, "Game", "none", "skipped", "-")},
        {L"90.000000000000000000000000000000", true, "refuse_too_long", install_row("refused", "too_long", 0x4000, "?", "none", "skipped", "-")},
        {L"90", false, "refuse_executable_mismatch", install_row("refused", "executable_mismatch", 0x4000, "90", "none", "skipped", "-")}};
    for (const Refusal& r : refusals) {
        set_fov(r.setting);
        executable_ok = r.exe;
        arm();
        const bool applied = initialize_checked(r.name);
        char name[96];
        std::snprintf(name, sizeof name, "%s_row_and_untouched", r.name);
        check(!applied && !fov::patched() && last_log() == r.row && g.protects == 0 && g.writes == 0 && g.flushes == 0 && fov::configured_focus() == 0x4000 &&
              page_is(engine, reference, original) && setfocus_page_is(setfocus_reference, nullptr), name, last_log().c_str());
    }
    set_fov(L"90");
    executable_ok = true;
    // The reader contract: a changed byte of the per-frame reader refuses the patch.
    const unsigned char reader_changed = 0x25, reader_back = 0x24;
    check(poke(fov_reader_page + 0x150, &reader_changed, 1), "setup_changed_reader_byte");
    arm();
    check(!initialize_checked("refuse_reader_mismatch") && fov::state() == std::string("reader_mismatch") && g.protects == 0 && g.writes == 0 &&
          page_is(engine, reference, original) && last_log() == install_row("refused", "reader_mismatch", 0x4000, "90", "none", "skipped", "-"),
          "refuse_reader_mismatch_untouched", last_log().c_str());
    check(poke(fov_reader_page + 0x150, &reader_back, 1), "setup_reader_byte_back");
    // The INS_SetFocus case window and the callee prefix: one changed byte in either refuses both sites.
    struct Mismatch { unsigned char* at; unsigned char changed, back; const char* name; };
    const Mismatch mismatches[] = {{fov_setfocus_page + setfocus_case_offset + 5, 0x02, 0x01, "refuse_setfocus_case_mismatch"},       // mov ecx,[eax+2]
                                   {fov_setfocus_page + setfocus_site_offset + 5, 0x01, 0x00, "refuse_setfocus_site_mismatch"},       // the claimed MOV's operand
                                   {fov_callee_page + 0x7ff, 0x09, 0x08, "refuse_setfocus_callee_mismatch"}};                         // cmp byte [edi],9
    for (const Mismatch& m : mismatches) {
        const bool poked = poke(m.at, &m.changed, 1);
        arm();
        const bool refused = !initialize_checked(m.name) && fov::state() == std::string("setfocus_mismatch") && g.protects == 0 && g.writes == 0 &&
                             page_is(engine, reference, original) && last_log() == install_row("refused", "setfocus_mismatch", 0x4000, "90", "none", "skipped", "-");
        const bool back = poke(m.at, &m.back, 1);
        char name[96];
        std::snprintf(name, sizeof name, "%s_untouched", m.name);
        check(poked && refused && back && setfocus_page_is(setfocus_reference, nullptr), name, last_log().c_str());
    }
    // The load window and the load caller: one changed byte in either refuses all three sites.
    const Mismatch load_mismatches[] = {{engine + load_site_offset + 1, 0x44, 0x45, "refuse_load_site_mismatch"},                 // mov [esp+..],eax
                                        {engine + load_window_offset + 1, 0x68, 0x67, "refuse_load_call_mismatch"},              // another call target
                                        {fov_caller_page + 0x795, 0x85, 0x84, "refuse_load_caller_mismatch"}};                   // test eax,eax
    for (const Mismatch& m : load_mismatches) {
        const bool poked = poke(m.at, &m.changed, 1);
        arm();
        const bool refused = !initialize_checked(m.name) && fov::state() == std::string("load_mismatch") && g.protects == 0 && g.writes == 0 &&
                             last_log() == install_row("refused", "load_mismatch", 0x4000, "90", "none", "skipped", "-");
        const bool back = poke(m.at, &m.back, 1);
        char name[96];
        std::snprintf(name, sizeof name, "%s_untouched", m.name);
        check(poked && refused && back && page_is(engine, reference, original) && setfocus_page_is(setfocus_reference, nullptr), name, last_log().c_str());
    }
    // A changed window byte and an already patched, unregistered window: bytes_mismatch.
    const unsigned char changed = 0x21, restored_byte = 0x20;
    check(poke(engine + window_offset + 2, &changed, 1), "setup_changed_window_byte");
    unsigned char changed_reference[page_size];
    std::memcpy(changed_reference, reference, page_size);
    changed_reference[window_offset + 2] = changed;
    arm();
    check(!initialize_checked("refuse_changed_window") && fov::state() == std::string("bytes_mismatch") && g.protects == 0 && g.writes == 0 &&
          page_is(engine, changed_reference, original) && last_log() == install_row("refused", "bytes_mismatch", 0x4000, "90", "none", "skipped", "-") &&
          setfocus_page_is(setfocus_reference, nullptr),
          "refuse_changed_window_untouched", last_log().c_str());
    check(poke(engine + window_offset + 2, &restored_byte, 1), "setup_restore_window_byte");
    check(poke(engine + write_at, other, 4), "setup_foreign_patch");
    arm();
    check(!initialize_checked("refuse_already_patched") && fov::state() == std::string("bytes_mismatch") && g.protects == 0 && g.writes == 0 &&
          page_is(engine, reference, other), "refuse_already_patched_window_untouched");
    check(poke(engine + write_at, original, 4) && page_is(engine, reference, original), "setup_foreign_patch_removed");
    Faults f;
    f.protect_fail = 1;
    arm(f);
    check(!initialize_checked("refuse_protect_failed") && !fov::patched() && g.writes == 0 && g.flushes == 0 && page_is(engine, reference, original) &&
          protection(engine) == PAGE_EXECUTE_READ && last_log() == install_row("refused", "protect_failed", 0x4000, "90", "none", "skipped", "-") &&
          setfocus_page_is(setfocus_reference, nullptr),
          "refuse_protect_failed_untouched", last_log().c_str());
    check(construct("engine", "after_protect_failed", engine) == 0x4000, "engine_after_protect_failed_ctor_4000");

    // ---- the production path at the engine's VA, registry absent: patch, read-back, execute, confirm, restore ----
    LARGE_INTEGER t0, t1, t2, t3;
    set_slot(nullptr);
    arm();
    QueryPerformanceCounter(&t0);
    const bool applied = initialize_checked("install");
    QueryPerformanceCounter(&t1);
    check(applied && fov::patched() && fov::state() == std::string("ok") && fov::write_path() == std::string("atomic") && fov::registry_state() == std::string("absent") &&
          fov::setfocus_state() == std::string("active") && fov::setfocus_patched() &&
          fov::load_state() == std::string("active") && fov::load_patched() &&
          last_log() == install_row("patched", "ok", 0x3470, "90", "atomic", "absent", "-", "active", "atomic", "active", "atomic"), "install_ok_atomic_registry_absent_row", last_log().c_str());
    // Reads through the seam: reader, store, case window, callee prefix, load window, load caller, window, imm32 read-back,
    // INS_SetFocus jmp read-back, load jmp read-back.
    std::snprintf(detail, sizeof detail, "protects=%u reads=%u writes=%u atomic=%u flushes=%u previous=0x%lx during=0x%lx", g.protects, g.reads,
                  g.writes, g.atomic_writes, g.flushes, g.first_previous, g.first_during);
    check(g.protects == 2 && g.reads == 10 && g.writes == 1 && g.atomic_writes == 1 && g.flushes == 1 && g.first_previous == PAGE_EXECUTE_READ &&
          writable_image(g.first_during), "install_sequence_counts", detail);
    // The claimed site: e9 rel32 to an address outside every image section (the arena), byte 0x0042dbfd and the rest of the page unchanged.
    unsigned char jmp[5]{};
    SIZE_T jmp_read = 0;
    const bool jmp_ok = ReadProcessMemory(GetCurrentProcess(), fov_setfocus_page + setfocus_site_offset, jmp, 5, &jmp_read) && jmp_read == 5 && jmp[0] == 0xe9;
    std::uint32_t rel = 0;
    std::memcpy(&rel, jmp + 1, 4);
    const std::uintptr_t dispatcher = sites::setfocus_site_va + 5 + rel;
    MEMORY_BASIC_INFORMATION di{};
    VirtualQuery(reinterpret_cast<const void*>(dispatcher), &di, sizeof di);
    std::snprintf(detail, sizeof detail, "jmp=%02x%02x%02x%02x%02x dispatcher=%08lx type=0x%lx protect=0x%lx", jmp[0], jmp[1], jmp[2], jmp[3], jmp[4],
                  static_cast<unsigned long>(dispatcher), di.Type, di.Protect);
    check(jmp_ok && setfocus_page_is(setfocus_reference, jmp) && di.Type == MEM_PRIVATE && di.Protect == PAGE_EXECUTE_READ && protection(fov_setfocus_page) == PAGE_EXECUTE_READ,
          "install_setfocus_jmp_to_arena_rest_of_page_unchanged", detail);
    std::printf("PROTECT memory=engine step=install previous=0x%lx during=0x%lx after=0x%lx\n", g.first_previous, g.first_during, protection(engine));
    std::printf("SEQUENCE step=install protects=%u reads=%u writes=%u atomic=%u flushes=%u\n", g.protects, g.reads, g.writes, g.atomic_writes, g.flushes);
    // The load site: e9 rel32 into the arena, byte 0x0041c8c6 (MOV AL,1's immediate) kept, the rest of the page as before.
    unsigned char load_jmp[5]{};
    const bool load_jmp_ok = read_load_jmp(load_jmp) && load_jmp[0] == 0xe9;
    std::uint32_t load_rel = 0;
    std::memcpy(&load_rel, load_jmp + 1, 4);
    const std::uintptr_t load_dispatcher = sites::load_site_va + 5 + load_rel;
    MEMORY_BASIC_INFORMATION li{};
    VirtualQuery(reinterpret_cast<const void*>(load_dispatcher), &li, sizeof li);
    std::snprintf(detail, sizeof detail, "jmp=%02x%02x%02x%02x%02x dispatcher=%08lx type=0x%lx protect=0x%lx sixth=%02x", load_jmp[0], load_jmp[1], load_jmp[2], load_jmp[3],
                  load_jmp[4], static_cast<unsigned long>(load_dispatcher), li.Type, li.Protect, engine[load_site_offset + 5]);
    check(load_jmp_ok && li.Type == MEM_PRIVATE && li.Protect == PAGE_EXECUTE_READ && engine[load_site_offset + 5] == 0x01 && load_dispatcher != dispatcher,
          "install_load_jmp_to_arena_sixth_byte_kept", detail);
    check(span_is(engine, ours) && page_is(engine, reference, ours, load_jmp), "install_readback_70340000_rest_of_page_unchanged");
    check(protection(engine) == PAGE_EXECUTE_READ, "install_protection_restored");
    check(construct("engine", "after_patch", engine) == 0x3470, "engine_after_patch_ctor_stores_3470");
    check(fov::configured_focus() == 0x3470 && fov::current_focus() == 0x3470, "configured_3470_current_falls_back_without_registry");
    // The first-Present confirmation: registry absent, then present, then silent.
    std::size_t rows_before = log_lines.size();
    SetLastError(0x2bad);
    fov::present(1);
    check(GetLastError() == 0x2bad && log_lines.size() == rows_before + 1 &&
          last_log() == "fov_confirm frame=1 registry=absent focus=- expected=0x3470 match=0 vertical_deg=- camera=skipped", "confirm_absent_row", last_log().c_str());
    fov::present(2);
    check(log_lines.size() == rows_before + 1, "confirm_absent_logged_once");
    construct("engine", "registry_created", engine);
    std::memcpy(registry_a, registry_b, sizeof registry_a);  // the registry the patched constructor built
    set_slot(registry_a);
    fov::present(3);
    std::snprintf(detail, sizeof detail, "fov_confirm frame=3 registry=%08lx focus=0x3470 expected=0x3470 match=1 vertical_deg=58.72 camera=skipped",
                  static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(registry_a)));
    check(log_lines.size() == rows_before + 2 && last_log() == detail, "confirm_registry_row_match", last_log().c_str());
    fov::present(4);
    check(log_lines.size() == rows_before + 2, "confirm_done_no_further_rows");
    check(read_base() == 0x3470 && fov::current_focus() == 0x3470, "reader_and_current_focus_3470");
    // The in-game FOV menu through the patched INS_SetFocus: the script's F for N is remapped to F'(N).
    struct Remap { const char* step; std::uint32_t in, out; };
    const Remap remaps[] = {{"n70", (70u << 16) / 360u, 0x2768}, {"n90", (90u << 16) / 360u, 0x3470}, {"n100", (100u << 16) / 360u, 0x3b6f},
                            {"n91", (91u << 16) / 360u, sites::remap_focus((91u << 16) / 360u)},
                            {"non_canonical_4001", 0x4001, 0x3470},                                   // nearest N = 90
                            {"n50_table_floor", (50u << 16) / 360u, 0x1b6a},
                            {"n130_table_ceiling", (130u << 16) / 360u, 0x52ab},
                            {"n49_passthrough", (49u << 16) / 360u, (49u << 16) / 360u},
                            {"n131_passthrough", (131u << 16) / 360u, (131u << 16) / 360u},
                            {"range_floor", sites::remap_focus_min, sites::remap_focus((50u << 16) / 360u)},
                            {"range_ceiling", sites::remap_focus_max, sites::remap_focus((130u << 16) / 360u)},
                            {"below_table_passthrough", sites::remap_focus_min - 1, sites::remap_focus_min - 1},
                            {"above_table_passthrough", sites::remap_focus_max + 1, sites::remap_focus_max + 1},
                            {"n45_passthrough", (45u << 16) / 360u, (45u << 16) / 360u},
                            {"n160_passthrough", (160u << 16) / 360u, (160u << 16) / 360u},
                            {"garbage_passthrough", 0xfffffff0u, 0xfffffff0u}};
    for (const Remap& r : remaps) {
        char step[64], name[96];
        std::snprintf(step, sizeof step, "patched_%s", r.step);
        const std::uint32_t out = run_setfocus(step, r.in, &kept);
        std::snprintf(name, sizeof name, "setfocus_%s_0x%04lx_to_0x%04lx", r.step, static_cast<unsigned long>(r.in), static_cast<unsigned long>(r.out));
        std::snprintf(detail, sizeof detail, "stored=0x%04lx kept=%u", static_cast<unsigned long>(out), kept ? 1u : 0u);
        check(out == r.out && kept, name, detail);
    }
    unsigned table_mismatches = 0;
    for (unsigned n = sites::remap_first; n < sites::remap_first + sites::remap_count; ++n) {
        bool quiet = false;
        unsigned char cell[5] = {0x01, static_cast<unsigned char>(((n << 16) / 360u) & 0xff), static_cast<unsigned char>(((n << 16) / 360u) >> 8), 0, 0};
        focus_of(registry_a) = 0;
        reinterpret_cast<SetFocusCall>(setfocus_entry)(cell, task_buffer);
        quiet = focus_of(registry_a) == sites::remap_focus((n << 16) / 360u);
        table_mismatches += quiet ? 0u : 1u;
    }
    std::snprintf(detail, sizeof detail, "mismatches=%u", table_mismatches);
    check(table_mismatches == 0, "setfocus_every_n_50_130_matches_the_formula", detail);
    // The menu's 100 through the patch: the base the per-frame reader sees, the constructor's immediate unchanged.
    run_setfocus("patched_override_100", 0x471c, &kept);
    std::printf("OVERRIDE setfocus=0x471c base=0x%04lx current=0x%04lx imm32=%s\n", static_cast<unsigned long>(read_base()),
                static_cast<unsigned long>(fov::current_focus()), span_is(engine, ours) ? "70340000" : "changed");
    check(read_base() == 0x3b6f && fov::current_focus() == 0x3b6f && span_is(engine, ours) && kept, "setfocus_menu_100_base_3b6f_patch_stays");
    // A savegame load through the patched store: the exact vanilla (N << 16) / 360 of N 50..130 becomes F'(N)
    // (N = 90 -> F'(90) whatever --fov is); remapped, off-table and non-canonical values are stored unchanged.
    struct Load { const char* step; std::uint32_t in, out; };
    const Load loads[] = {{"vanilla_90", 0x4000, 0x3470}, {"vanilla_70", 0x31c7, 0x2768}, {"vanilla_100", 0x471c, 0x3b6f},
                          {"remapped_90", 0x3470, 0x3470}, {"remapped_100", 0x3b6f, 0x3b6f}, {"old_constructor_34aa", 0x34aa, 0x34aa},
                          {"n45_2000", 0x2000, 0x2000}, {"n180_8000", 0x8000, 0x8000}, {"non_canonical_4001", 0x4001, 0x4001},
                          {"vanilla_n50_floor", (50u << 16) / 360u, 0x1b6a}, {"vanilla_n130_ceiling", (130u << 16) / 360u, 0x52ab},
                          {"vanilla_n49_passthrough", (49u << 16) / 360u, (49u << 16) / 360u},
                          {"vanilla_n131_passthrough", (131u << 16) / 360u, (131u << 16) / 360u},
                          {"below_table", sites::remap_focus_min - 1, sites::remap_focus_min - 1}, {"above_table", sites::remap_focus_max + 1, sites::remap_focus_max + 1},
                          {"garbage", 0xfffffff0u, 0xfffffff0u}};
    for (const Load& l : loads) {
        char step[64], name[96];
        std::snprintf(step, sizeof step, "patched_%s", l.step);
        const std::uint32_t out = run_load(step, l.in, &kept);
        std::snprintf(name, sizeof name, "load_%s_0x%04lx_to_0x%04lx", l.step, static_cast<unsigned long>(l.in), static_cast<unsigned long>(l.out));
        std::snprintf(detail, sizeof detail, "stored=0x%04lx kept=%u", static_cast<unsigned long>(out), kept ? 1u : 0u);
        check(out == l.out && kept, name, detail);
    }
    // Every F 0..0xffff and the top of the range against the model (fov_sites.h load_lookup): value and registers.
    std::uint16_t vanilla_model[sites::remap_count], table_model[sites::remap_count];
    sites::build_vanilla_table(vanilla_model);
    sites::build_remap_table(table_model);
    unsigned sweep = 0, sweep_mismatch = 0, sweep_changed = 0, sweep_lost = 0;
    for (std::uint32_t f = 0; f <= 0x10000u; ++f) {
        const std::uint32_t in = f == 0x10000u ? 0xffffffffu : f;
        const std::uint32_t out = run_load("sweep", in, &kept, true);
        ++sweep;
        sweep_mismatch += out != sites::load_lookup(in, vanilla_model, table_model) ? 1u : 0u;
        sweep_changed += out != in ? 1u : 0u;
        sweep_lost += kept ? 0u : 1u;
    }
    std::printf("LOADSWEEP inputs=%u mismatches=%u changed=%u registers_lost=%u\n", sweep, sweep_mismatch, sweep_changed, sweep_lost);
    std::snprintf(detail, sizeof detail, "inputs=%u mismatches=%u changed=%u lost=%u", sweep, sweep_mismatch, sweep_changed, sweep_lost);
    check(sweep == 0x10001u && sweep_mismatch == 0 && sweep_changed == sites::remap_count && sweep_lost == 0, "load_sweep_matches_model_81_changed_registers_kept", detail);
    // The fov_confirm row per save_load_complete: registry+0x24 as the load left it (not once-only, unlike present()).
    set_slot(registry_a);
    run_load("confirm_vanilla_90", 0x4000, &kept);
    rows_before = log_lines.size();
    SetLastError(0x2bad);
    fov::loaded(9);
    std::snprintf(detail, sizeof detail, "fov_confirm frame=9 registry=%08lx focus=0x3470 expected=0x3470 match=1 vertical_deg=58.72 camera=skipped after=save_load_complete",
                  static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(registry_a)));
    check(GetLastError() == 0x2bad && log_lines.size() == rows_before + 1 && last_log() == detail, "confirm_after_load_row_match", last_log().c_str());
    run_load("confirm_vanilla_100", 0x471c, &kept);
    fov::loaded(10);
    std::snprintf(detail, sizeof detail, "fov_confirm frame=10 registry=%08lx focus=0x3b6f expected=0x3470 match=0 vertical_deg=%.2f camera=skipped after=save_load_complete",
                  static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(registry_a)), sites::vertical_for_focus(0x3b6f));
    check(log_lines.size() == rows_before + 2 && last_log() == detail, "confirm_after_second_load_row_3b6f", last_log().c_str());
    set_slot(nullptr);
    fov::loaded(11);
    check(log_lines.size() == rows_before + 3 &&
          last_log() == "fov_confirm frame=11 registry=absent focus=- expected=0x3470 match=0 vertical_deg=- camera=skipped after=save_load_complete",
          "confirm_after_load_registry_absent_row", last_log().c_str());
    set_slot(registry_a);
    focus_of(registry_a) = 0x10;  // implausible: current_focus falls back to the configured value
    check(fov::current_focus() == 0x3470, "implausible_registry_value_falls_back");
    arm();
    check(!fov::install_at(sites::window_va, 0x3470) && fov::patched() && fov::state() == std::string("already_installed") && g.protects == 0 && g.writes == 0 &&
          span_is(engine, ours), "second_install_refused_already_installed");
    check(initialize_checked("initialize_again") && fov::patched() && g.writes == 0, "initialize_again_no_second_write");
    arm();
    QueryPerformanceCounter(&t2);
    const bool clean = shutdown_checked("restore");
    QueryPerformanceCounter(&t3);
    std::string rows = new_restore_rows();
    check(clean && !fov::patched() && !fov::setfocus_patched() && fov::state() == std::string("restored") && fov::configured_focus() == 0x4000 &&
          !fov::load_patched() && one_row(rows, "fov_restore site=0041c9dc status=restored found=70340000 registered=0 setfocus=restored load=restored"), "restore_row", rows.c_str());
    check(g.protects == 2 && g.writes == 1 && g.atomic_writes == 1 && g.flushes == 1 && page_is(engine, reference, original) && protection(engine) == PAGE_EXECUTE_READ,
          "restore_readback_00400000_protection");
    set_slot(registry_a);
    check(setfocus_page_is(setfocus_reference, nullptr) && protection(fov_setfocus_page) == PAGE_EXECUTE_READ && run_setfocus("after_restore", 0x471c, &kept) == 0x471c && kept,
          "restore_setfocus_bytes_back_vanilla_store_471c");
    check(page_is(engine, reference, original) && run_load("after_restore", 0x4000, &kept) == 0x4000 && kept, "restore_load_bytes_back_vanilla_store_4000");
    set_slot(nullptr);
    check(construct("engine", "after_restore", engine) == 0x4000, "engine_after_restore_ctor_4000");
    check(shutdown_checked("restore_again") && new_restore_rows().empty(), "restore_again_no_row");
    std::printf("TIMING install_us=%.1f restore_us=%.1f\n", qpc_us(t0, t1), qpc_us(t2, t3));

    // ---- the one-off registry write when the registry already exists ----
    struct RegistryCase { const char* name; std::uint32_t start; int kind; const char* state; const char* before; std::uint32_t after; };
    // kind 0: registry_a; 1: misaligned pointer; 2: a read-only page; 3: a reserved, uncommitted page
    auto* const readonly = static_cast<unsigned char*>(VirtualAlloc(nullptr, page_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    auto* const reserved = static_cast<unsigned char*>(VirtualAlloc(nullptr, page_size, MEM_RESERVE, PAGE_NOACCESS));
    check(readonly && reserved, "registry_case_pages_allocated");
    const RegistryCase cases[] = {{"registry_written", 0x4000, 0, "written", "0x4000", 0x3470},
                                  {"registry_menu_value_written", 0x3d34, 0, "written", "0x3d34", 0x3470},
                                  {"registry_implausible_skipped", 0x0000, 0, "skipped", "0x0000", 0x0000},
                                  {"registry_misaligned_skipped", 0x4000, 1, "skipped", "-", 0x4000},
                                  {"registry_readonly_skipped", 0x4000, 2, "skipped", "0x4000", 0x4000},
                                  {"registry_uncommitted_skipped", 0x4000, 3, "skipped", "-", 0x4000}};
    for (const RegistryCase& c : cases) {
        unsigned char* registry = registry_a;
        std::memset(registry_a, 0, sizeof registry_a);
        focus_of(registry_a) = c.start;
        if (c.kind == 1) registry = registry_a + 1;
        if (c.kind == 2 && readonly) {
            focus_of(readonly) = c.start;
            ::VirtualProtect(readonly, page_size, PAGE_READONLY, &old);
            registry = readonly;
        }
        if (c.kind == 3) registry = reserved;
        set_slot(registry);
        arm();
        const bool ok = initialize_checked(c.name);
        const std::uint32_t after = c.kind == 2 ? focus_of(readonly) : c.kind == 3 ? 0x4000 : focus_of(registry_a);
        char name[96];
        std::snprintf(name, sizeof name, "%s_row_and_field", c.name);
        check(ok && fov::registry_state() == std::string(c.state) && after == c.after && span_is(engine, ours) &&
              last_log() == install_row("patched", "ok", 0x3470, "90", "atomic", c.state, c.before, "active", "atomic", "active", "atomic"), name, last_log().c_str());
        std::snprintf(name, sizeof name, "%s_restore", c.name);
        rows = shutdown_rows(name);
        std::snprintf(name, sizeof name, "%s_restored", c.name);
        check(!fov::patched() && one_row(rows, "status=restored found=70340000 registered=0 setfocus=restored load=restored") && page_is(engine, reference, original) &&
              setfocus_page_is(setfocus_reference, nullptr), name, rows.c_str());
        if (c.kind == 2) ::VirtualProtect(readonly, page_size, PAGE_READWRITE, &old);
    }
    set_slot(nullptr);

    // ---- rollback paths (injected on the seam; every store and read is real) ----
    f = Faults{};
    f.read_fail = 1u << 7;  // reads: reader, store, case window, callee prefix, load window, load caller, window, read-back
    arm(f);
    check(!initialize_checked("rollback_readback") && !fov::patched() && fov::state() == std::string("patch_rolled_back") && fov::configured_focus() == 0x4000 &&
          last_log() == install_row("refused", "patch_rolled_back", 0x4000, "90", "atomic", "skipped", "-") && g.writes == 2 && g.atomic_writes == 2 &&
          setfocus_page_is(setfocus_reference, nullptr) &&
          page_is(engine, reference, original) && protection(engine) == PAGE_EXECUTE_READ, "rollback_readback_rolled_back", last_log().c_str());
    check(construct("engine", "after_rollback", engine) == 0x4000, "engine_after_rollback_ctor_4000");
    f = Faults{};
    f.write_drop = 1;
    arm(f);
    check(!initialize_checked("rollback_dropped_write") && !fov::patched() && fov::state() == std::string("patch_rolled_back") &&
          page_is(engine, reference, original) && protection(engine) == PAGE_EXECUTE_READ, "rollback_dropped_write_rolled_back");
    f = Faults{};
    f.protect_fail = 2 | 4;
    arm(f);
    const bool unprotected = !initialize_checked("rollback_unprotected") && !fov::patched() && fov::state() == std::string("rollback_unprotected") &&
                             page_is(engine, reference, original);
    const DWORD left = protection(engine);
    std::snprintf(detail, sizeof detail, "protect=0x%lx", left);
    check(unprotected && writable_image(left), "rollback_unprotected_original_bytes_page_writable", detail);
    check(construct("engine", "after_rollback_unprotected", engine) == 0x4000, "engine_after_rollback_unprotected_ctor_4000");
    check(::VirtualProtect(engine, page_size, PAGE_EXECUTE_READ, &old) != FALSE, "setup_reprotect_after_unprotected");
    // A wrong store that cannot be undone: rollback_failed stays registered and is reported as patched_unverified.
    f = Faults{};
    f.write_corrupt = 1;
    f.write_drop = 2;
    arm(f);
    check(!initialize_checked("rollback_failed") && fov::patched() && fov::state() == std::string("rollback_failed") &&
          last_log() == install_row("patched_unverified", "rollback_failed", 0x3471, "90", "atomic", "skipped", "-") && span_is(engine, corrupt) &&
          !fov::setfocus_patched() && setfocus_page_is(setfocus_reference, nullptr) &&
          protection(engine) == PAGE_EXECUTE_READ, "rollback_failed_registered", last_log().c_str());
    check(fov::configured_focus() == 0x3471, "rollback_failed_configured_from_readback_3471");
    check(construct("engine", "after_rollback_failed", engine) == 0x3471, "engine_after_rollback_failed_ctor_3471");
    arm();
    rows = shutdown_rows("restore_not_owned");
    check(fov::patched() && fov::state() == std::string("restore_not_owned") && one_row(rows, "status=restore_not_owned found=71340000 registered=1") &&
          g.writes == 0 && g.protects == 0 && g.flushes == 0 && span_is(engine, corrupt), "restore_not_owned_foreign_bytes_untouched_registered", rows.c_str());
    check(poke(engine + write_at, ours, 4), "setup_span_back_to_ours");
    f = Faults{};
    f.write_drop = 1;
    arm(f);
    rows = shutdown_rows("restore_dropped");
    check(fov::patched() && fov::state() == std::string("restore_failed") && one_row(rows, "status=restore_failed found=70340000 registered=1") &&
          span_is(engine, ours) && protection(engine) == PAGE_EXECUTE_READ, "restore_dropped_write_failed_registered", rows.c_str());
    f = Faults{};
    f.protect_fail = 1;
    arm(f);
    rows = shutdown_rows("restore_protect_failed");
    check(fov::patched() && one_row(rows, "status=restore_failed found=70340000 registered=1") && g.writes == 0, "restore_protect_failed_registered", rows.c_str());
    arm();
    rows = shutdown_rows("restore_after_failures");
    check(!fov::patched() && one_row(rows, "status=restored found=70340000 registered=0") && page_is(engine, reference, original) &&
          protection(engine) == PAGE_EXECUTE_READ, "restore_after_failures_00400000", rows.c_str());
    // Restore only over our value: a different focus written by someone else is left alone.
    arm();
    check(initialize_checked("install_2") && poke(engine + write_at, other, 4), "setup_install_then_foreign_value");
    arm();
    rows = shutdown_rows("restore_other_value");
    check(fov::patched() && !fov::setfocus_patched() && one_row(rows, "status=restore_not_owned found=71340000 registered=1 setfocus=restored") && g.writes == 0 &&
          g.protects == 0 && page_is(engine, reference, other) && setfocus_page_is(setfocus_reference, nullptr), "restore_other_value_not_owned_untouched", rows.c_str());
    check(poke(engine + write_at, original, 4), "setup_external_restore");
    arm();
    rows = shutdown_rows("restore_already_original");
    check(!fov::patched() && one_row(rows, "status=restored found=00400000 registered=0") && g.writes == 0 && g.protects == 0, "restore_already_original_no_write", rows.c_str());
    arm();
    check(initialize_checked("install_3"), "setup_install_3");
    f = Faults{};
    f.read_fail = 1;
    arm(f);
    rows = shutdown_rows("restore_unreadable");
    check(fov::patched() && one_row(rows, "status=restore_not_owned found=-- registered=1 setfocus=restored") && g.writes == 0 && g.protects == 0 &&
          page_is(engine, reference, ours), "restore_unreadable_found_unread_no_write", rows.c_str());
    arm();
    rows = shutdown_rows("restore_after_unreadable");
    check(!fov::patched() && one_row(rows, "status=restored found=70340000 registered=0 setfocus=none") && page_is(engine, reference, original), "restore_after_unreadable_00400000", rows.c_str());

    // ---- the INS_SetFocus site: partial-install rollback, restore only over our jmp, other values of N ----
    set_slot(nullptr);
    f = Faults{};
    f.read_fail = 1u << 8;  // the INS_SetFocus jmp read-back fails: the claim is undone and the immediate rolled back
    arm(f);
    check(!initialize_checked("setfocus_readback_rollback") && !fov::patched() && !fov::setfocus_patched() && fov::state() == std::string("setfocus_failed") &&
          fov::setfocus_state() == std::string("readback_failed") && fov::configured_focus() == 0x4000 &&
          last_log() == install_row("refused", "setfocus_failed", 0x4000, "90", "atomic", "skipped", "-", "readback_failed", "atomic") &&
          g.protects == 4 && g.writes == 2 && page_is(engine, reference, original) && protection(engine) == PAGE_EXECUTE_READ &&
          setfocus_page_is(setfocus_reference, nullptr) && protection(fov_setfocus_page) == PAGE_EXECUTE_READ, "setfocus_readback_both_sites_rolled_back", last_log().c_str());
    check(construct("engine", "after_setfocus_rollback", engine) == 0x4000, "engine_after_setfocus_rollback_ctor_4000");
    set_slot(registry_a);
    check(run_setfocus("after_setfocus_rollback", 0x471c, &kept) == 0x471c && kept, "setfocus_after_rollback_vanilla_store_471c");
    check(run_load("after_setfocus_rollback", 0x4000, &kept) == 0x4000 && kept && !fov::load_patched() && fov::load_state() == std::string("none"),
          "load_never_claimed_after_setfocus_failure");
    set_slot(nullptr);
    f = Faults{};
    f.read_fail = 1u << 8;
    f.write_drop = 2;       // and the immediate's rollback store does not land
    arm(f);
    check(!initialize_checked("setfocus_constructor_rollback_failed") && fov::patched() && !fov::setfocus_patched() && fov::state() == std::string("setfocus_failed") &&
          last_log() == install_row("patched_unverified", "setfocus_failed", 0x3470, "90", "atomic", "skipped", "-", "readback_failed", "atomic") &&
          span_is(engine, ours) && setfocus_page_is(setfocus_reference, nullptr), "setfocus_failed_constructor_rollback_failed_registered", last_log().c_str());
    arm();
    rows = shutdown_rows("setfocus_constructor_rollback_restore");
    check(!fov::patched() && one_row(rows, "status=restored found=70340000 registered=0 setfocus=none") && page_is(engine, reference, original),
          "setfocus_constructor_rollback_restored_at_shutdown", rows.c_str());
    // A foreign store over our jmp: the site is not restored and stays registered; the immediate is.
    arm();
    check(initialize_checked("install_4") && fov::setfocus_patched(), "setup_install_4");
    unsigned char our_jmp[5]{};
    check(ReadProcessMemory(GetCurrentProcess(), fov_setfocus_page + setfocus_site_offset, our_jmp, 5, &jmp_read) && jmp_read == 5 && our_jmp[0] == 0xe9,
          "setup_read_our_jmp");
    const unsigned char foreign_jmp[5] = {0xe9, 0x10, 0x20, 0x30, 0x40};
    check(poke(fov_setfocus_page + setfocus_site_offset, foreign_jmp, 5), "setup_foreign_jmp");
    arm();
    rows = shutdown_rows("setfocus_restore_not_owned");
    check(fov::patched() && fov::setfocus_patched() && fov::state() == std::string("restored") &&
          one_row(rows, "status=restored found=70340000 registered=1 setfocus=restore_not_owned") && setfocus_page_is(setfocus_reference, foreign_jmp) &&
          page_is(engine, reference, original), "setfocus_restore_not_owned_foreign_jmp_untouched_registered", rows.c_str());
    check(poke(fov_setfocus_page + setfocus_site_offset, our_jmp, 5), "setup_our_jmp_back");
    arm();
    rows = shutdown_rows("setfocus_restore_after_not_owned");
    check(!fov::patched() && !fov::setfocus_patched() && one_row(rows, "status=none found=-- registered=0 setfocus=restored") &&
          setfocus_page_is(setfocus_reference, nullptr) && protection(fov_setfocus_page) == PAGE_EXECUTE_READ, "setfocus_restore_after_not_owned_bytes_back", rows.c_str());
    // ---- the load site: all or none, restore only over our jmp ----
    // The load jmp read-back fails: the load claim is undone, the INS_SetFocus jmp restored, the immediate rolled back.
    set_slot(nullptr);
    f = Faults{};
    f.read_fail = 1u << 9;
    arm(f);
    check(!initialize_checked("load_readback_rollback") && !fov::patched() && !fov::setfocus_patched() && !fov::load_patched() &&
          fov::state() == std::string("load_failed") && fov::setfocus_state() == std::string("rolled_back") && fov::load_state() == std::string("readback_failed") &&
          fov::configured_focus() == 0x4000 &&
          last_log() == install_row("refused", "load_failed", 0x4000, "90", "atomic", "skipped", "-", "rolled_back", "atomic", "readback_failed", "atomic") &&
          g.protects == 4 && g.writes == 2 && page_is(engine, reference, original) && protection(engine) == PAGE_EXECUTE_READ &&
          setfocus_page_is(setfocus_reference, nullptr) && protection(fov_setfocus_page) == PAGE_EXECUTE_READ, "load_readback_all_three_sites_rolled_back", last_log().c_str());
    check(construct("engine", "after_load_rollback", engine) == 0x4000, "engine_after_load_rollback_ctor_4000");
    set_slot(registry_a);
    check(run_setfocus("after_load_rollback", 0x471c, &kept) == 0x471c && kept && run_load("after_load_rollback", 0x4000, &kept) == 0x4000 && kept,
          "after_load_rollback_vanilla_setfocus_471c_load_4000");
    set_slot(nullptr);
    f = Faults{};
    f.read_fail = 1u << 9;
    f.write_drop = 2;       // and the immediate's rollback store does not land
    arm(f);
    check(!initialize_checked("load_constructor_rollback_failed") && fov::patched() && !fov::setfocus_patched() && !fov::load_patched() &&
          fov::state() == std::string("load_failed") &&
          last_log() == install_row("patched_unverified", "load_failed", 0x3470, "90", "atomic", "skipped", "-", "rolled_back", "atomic", "readback_failed", "atomic") &&
          span_is(engine, ours) && page_is(engine, reference, ours) && setfocus_page_is(setfocus_reference, nullptr), "load_failed_constructor_rollback_failed_registered", last_log().c_str());
    arm();
    rows = shutdown_rows("load_constructor_rollback_restore");
    check(!fov::patched() && one_row(rows, "status=restored found=70340000 registered=0 setfocus=none load=none") && page_is(engine, reference, original),
          "load_constructor_rollback_restored_at_shutdown", rows.c_str());
    // A foreign store over our load jmp: that site is not restored and stays registered; the other two are.
    arm();
    check(initialize_checked("install_5") && fov::load_patched(), "setup_install_5");
    unsigned char our_load_jmp[5]{};
    check(read_load_jmp(our_load_jmp) && our_load_jmp[0] == 0xe9, "setup_read_our_load_jmp");
    const unsigned char foreign_load_jmp[5] = {0xe9, 0x50, 0x60, 0x70, 0x00};
    check(poke(engine + load_site_offset, foreign_load_jmp, 5), "setup_foreign_load_jmp");
    arm();
    rows = shutdown_rows("load_restore_not_owned");
    check(fov::patched() && fov::load_patched() && !fov::setfocus_patched() && fov::state() == std::string("restored") &&
          one_row(rows, "status=restored found=70340000 registered=1 setfocus=restored load=restore_not_owned") &&
          page_is(engine, reference, original, foreign_load_jmp) && setfocus_page_is(setfocus_reference, nullptr),
          "load_restore_not_owned_foreign_jmp_untouched_registered", rows.c_str());
    check(poke(engine + load_site_offset, our_load_jmp, 5), "setup_our_load_jmp_back");
    arm();
    rows = shutdown_rows("load_restore_after_not_owned");
    check(!fov::patched() && !fov::load_patched() && one_row(rows, "status=none found=-- registered=0 setfocus=none load=restored") &&
          page_is(engine, reference, original) && protection(engine) == PAGE_EXECUTE_READ, "load_restore_after_not_owned_bytes_back", rows.c_str());
    set_slot(registry_a);
    check(run_load("after_load_restore", 0x4000, &kept) == 0x4000 && kept, "load_after_restore_vanilla_store_4000");
    set_slot(nullptr);
    // rollback_setfocus() failing inside a load rollback: the load read-back fails while a foreign jmp lands on the
    // INS_SetFocus site; the load claim and the immediate are taken back, the INS_SetFocus site is not ours any more
    // and stays registered (patched_unverified, setfocus=rollback_failed).
    f = Faults{};
    f.read_fail = 1u << 9;
    f.on_read = foreign_setfocus_on_load_readback;
    hook_fired = false;
    arm(f);
    check(!initialize_checked("load_rollback_setfocus_not_owned") && hook_fired && fov::patched() && fov::setfocus_patched() && !fov::load_patched() &&
          fov::state() == std::string("load_failed") && fov::setfocus_state() == std::string("rollback_failed") && fov::load_state() == std::string("readback_failed") &&
          fov::configured_focus() == 0x4000 &&
          last_log() == install_row("patched_unverified", "load_failed", 0x4000, "90", "atomic", "skipped", "-", "rollback_failed", "atomic", "readback_failed", "atomic") &&
          page_is(engine, reference, original) && setfocus_page_is(setfocus_reference, hook_foreign_jmp),
          "load_rollback_setfocus_not_owned_registered_unverified", last_log().c_str());
    arm();
    rows = shutdown_rows("load_rollback_setfocus_not_owned_restore");
    check(fov::patched() && fov::setfocus_patched() && one_row(rows, "status=none found=-- registered=1 setfocus=restore_not_owned load=none") &&
          setfocus_page_is(setfocus_reference, hook_foreign_jmp) && g.writes == 0, "load_rollback_setfocus_foreign_jmp_untouched_at_shutdown", rows.c_str());
    check(poke(fov_setfocus_page + setfocus_site_offset, hook_saved_jmp, 5), "setup_setfocus_jmp_back_after_hook");
    arm();
    rows = shutdown_rows("load_rollback_setfocus_restore_after");
    check(!fov::patched() && one_row(rows, "status=none found=-- registered=0 setfocus=restored load=none") &&
          setfocus_page_is(setfocus_reference, nullptr) && protection(fov_setfocus_page) == PAGE_EXECUTE_READ, "load_rollback_setfocus_restored_after", rows.c_str());
    // Other launcher values: the constructor gets F'(N), the menu path the same table.
    struct Other { const wchar_t* setting; const char* text; std::uint32_t ctor; };
    // 78.5: F'(g) = 0x2ccc is vanilla N 63, so the constructor gets 0x2ccb (one unit towards the unrounded 0x2ccb.f9).
    const Other others[] = {{L"70", "70", 0x2768}, {L"100", "100", 0x3b6f}, {L"72.5", "72.5", sites::focus_for_degrees(72.5)}, {L"78.5", "78.5", 0x2ccb}};
    for (const Other& o : others) {
        set_fov(o.setting);
        arm();
        char name[96];
        std::snprintf(name, sizeof name, "install_setting_%s_ctor_0x%04lx", o.text, static_cast<unsigned long>(o.ctor));
        const bool installed = initialize_checked(name) && fov::configured_focus() == o.ctor &&
                               last_log() == install_row("patched", "ok", o.ctor, o.text, "atomic", "absent", "-", "active", "atomic", "active", "atomic");
        check(installed && construct("engine", name, engine) == o.ctor, name, last_log().c_str());
        set_slot(registry_a);
        std::snprintf(name, sizeof name, "install_setting_%s_menu_90_to_3470", o.text);
        check(run_setfocus(name, 0x4000, &kept) == 0x3470 && kept, name);
        std::snprintf(name, sizeof name, "install_setting_%s_load_90_to_3470", o.text);  // a save's 90 is F'(90) whatever --fov is
        check(run_load(name, 0x4000, &kept) == 0x3470 && kept, name);
        std::snprintf(name, sizeof name, "install_setting_%s_load_own_ctor_unchanged", o.text);  // a save of this session loads as saved
        check(run_load(name, o.ctor, &kept) == o.ctor && kept && !sites::vanilla_focus(o.ctor), name);
        set_slot(nullptr);
        std::snprintf(name, sizeof name, "install_setting_%s_restore", o.text);
        rows = shutdown_rows(name);
        std::snprintf(name, sizeof name, "install_setting_%s_restored", o.text);
        char want[96];
        unsigned char imm[4];
        sites::encode(o.ctor, imm);
        std::snprintf(want, sizeof want, "status=restored found=%02x%02x%02x%02x registered=0 setfocus=restored load=restored", imm[0], imm[1], imm[2], imm[3]);
        check(!fov::patched() && one_row(rows, want) && page_is(engine, reference, original) && setfocus_page_is(setfocus_reference, nullptr), name, rows.c_str());
    }
    set_fov(L"90");

    // ---- the private page (MEM_PRIVATE, PAGE_EXECUTE_READ): install_at + shutdown, another value ----
    arm();
    check(fov::install_at(reinterpret_cast<std::uintptr_t>(priv + window_offset), 0x5eb4) && fov::write_path() == std::string("atomic") &&
          construct("private", "after_patch", priv) == 0x5eb4, "private_install_ok_atomic_ctor_5eb4");
    check(g.first_previous == PAGE_EXECUTE_READ && g.first_during == PAGE_EXECUTE_READWRITE && protection(priv) == PAGE_EXECUTE_READ, "private_protection_restored");
    arm();
    rows = shutdown_rows("private_restore");
    check(!fov::patched() && one_row(rows, "status=restored found=b45e0000 registered=0") && page_is(priv, reference, original), "private_restore_00400000", rows.c_str());
    check(construct("private", "after_restore", priv) == 0x4000, "private_after_restore_ctor_4000");
    arm();
    check(!fov::install_at(reinterpret_cast<std::uintptr_t>(priv + window_offset), 0x4000) && fov::state() == std::string("invalid_value") && g.protects == 0,
          "install_at_engine_value_refused_invalid_value");

    // ---- arena_full at the load site: the INS_SetFocus block and claim fit, the load block does not ----
    set_slot(nullptr);
    f = Faults{};
    f.read_fail = 1u << 8;  // measures one INS_SetFocus block + claim: its read-back fails, the load site is not reached
    arm(f);
    const unsigned used_before = engine_patch::arena_used();
    const bool measured = !initialize_checked("arena_measure_setfocus") && fov::setfocus_state() == std::string("readback_failed") && !fov::patched();
    const unsigned setfocus_bytes = engine_patch::arena_used() - used_before;
    const unsigned leave = setfocus_bytes + 100;  // below the load block's reserve (53 + 3 + 4 + 162 = 222)
    const unsigned free_before = engine_patch::arena_capacity() - engine_patch::arena_used();
    bool filled = measured && free_before > leave + 4;
    if (filled) {
        const unsigned fill = (free_before - leave) & ~3u;
        engine_patch::Emitter e(fill);
        for (unsigned i = 0; i < fill; ++i) e.byte(0xcc);
        filled = e.finish() != nullptr;
    }
    const unsigned free_after = engine_patch::arena_capacity() - engine_patch::arena_used();
    std::printf("ARENA setfocus_bytes=%u free_before_fill=%u free_after_fill=%u\n", setfocus_bytes, free_before, free_after);
    arm();
    check(filled && free_after < setfocus_bytes + 222 && !initialize_checked("load_arena_full") && !fov::patched() && !fov::setfocus_patched() &&
          fov::state() == std::string("load_failed") && fov::load_state() == std::string("arena_full") && fov::setfocus_state() == std::string("rolled_back") &&
          last_log() == install_row("refused", "load_failed", 0x4000, "90", "atomic", "skipped", "-", "rolled_back", "atomic", "arena_full", "none") &&
          page_is(engine, reference, original) && setfocus_page_is(setfocus_reference, nullptr), "load_arena_full_all_three_rolled_back", last_log().c_str());
    set_slot(registry_a);
    check(run_setfocus("after_arena_full", 0x471c, &kept) == 0x471c && kept && run_load("after_arena_full", 0x4000, &kept) == 0x4000 && kept &&
          construct("engine", "after_arena_full", engine) == 0x4000, "after_arena_full_vanilla_setfocus_load_ctor");
    set_slot(nullptr);

    // ---- the closed install window: late_claim ----
    engine_patch::close_install_window("fixture");
    arm();
    check(!initialize_checked("late_initialize") && fov::state() == std::string("late_claim") &&
          last_log() == install_row("refused", "late_claim", 0x4000, "90", "none", "skipped", "-") && g.protects == 0 && g.writes == 0 &&
          page_is(engine, reference, original) && setfocus_page_is(setfocus_reference, nullptr), "late_initialize_refused", last_log().c_str());
    arm();
    check(!fov::install_at(sites::window_va, 0x3470) && fov::state() == std::string("late_claim") && g.writes == 0, "late_install_at_refused");
    check(construct("engine", "after_late", engine) == 0x4000, "engine_after_late_ctor_4000");
    set_slot(registry_a);
    check(run_setfocus("after_late", 0x471c, &kept) == 0x471c && kept, "setfocus_after_late_vanilla_store_471c");
    check(run_load("after_late", 0x4000, &kept) == 0x4000 && kept && !fov::load_patched(), "load_after_late_vanilla_store_4000");

    std::printf("RESULT checks=%u failures=%u\n", checks, failures);
    CloseHandle(restore_log);
    return failures ? 1 : 0;
}
