// Wine fixture of the engine-effects call redirects (src/proxy/engine_effects_patch.cpp through a force-included
// fault seam, and src/proxy/engine_patch.cpp unchanged). Never launches the game.
//
// The executable links its own image sections at the engine's pages (build_engine_effects_patch.py):
//   .x3meec 0x00414000  the fixture's caller, exit and callee bodies at +0x000; the engine's bytes 0x004147c4..
//                       0x00414832 (window A, the four-instruction trail test, window B, `jmp 0x0041487c`) at their
//                       VAs; at 0x0041487c a jump to the exit body; at 0x004148a0 the effect-instance callee's
//                       stand-in (cdecl, records registers, return address and its ten arguments);
//   .x3meeb 0x00412000  at 0x00412d70 the trail generator's stand-in (records, `ret 0x10`);
//   .x3meed 0x00606000  the VideoD3DFlags block pointer at 0x00606f34 -> a block with +0xfc = 0x40000000.
// So both windows' absolute operands and both rel32 calls reach the stand-ins exactly as in X3AP.exe. ee_enter builds
// the routine's frame (EBP frame with [ebp+8] = obj, k/eff/trail/pos at [esp+0x10..0x2c], ESP 16-aligned as after
// the routine's prologue), loads a hostile MXCSR, x87 control word, two x87 values and DF, sentinels in EBX/ESI/
// EDI/ECX/EDX, and jumps to 0x004147c4; the engine's bytes run both calls and reach the join 0x0041487c, where the
// exit body records the registers and the FPU state. Vanilla, patched (off and plumes) and restored passes for a
// ship (class 7), a missile (class 10) and a class word 0x0107 must give: ships skip both callees with the stack
// balanced, everything else reaches both callees with the vanilla register record (status flags aside), and EBX,
// EBP, ESI, EDI, DF, MXCSR, the x87 control word and stack, and LastError survive every path.
#include "../../src/proxy/engine_effects_patch.h"
#include "../../src/proxy/engine_effects_sites.h"
#include "../../src/proxy/engine_patch.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace eep = x3m::engine_effects_patch;
namespace sites = x3m::engine_effects_patch::sites;
namespace engine_patch = x3m::engine_patch;

// ---- production dependencies the module links against ----
namespace {
std::vector<std::string> log_lines;
bool executable_ok = true;
std::uintptr_t fault_read_at = 0, fault_restore_at = 0;
bool fault_protect = false;
unsigned faults_taken = 0;
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
}
namespace x3m::object_trace {
bool executable_verified() {
    return executable_ok;
}
}
// The seam (engine_effects_patch_fixture_shim.h): one-shot faults, otherwise the real calls.
namespace x3m::engine_effects_fixture {
BOOL WINAPI virtual_protect(LPVOID address, SIZE_T size, DWORD protection, PDWORD previous) {
    if (fault_protect) {
        fault_protect = false;
        ++faults_taken;
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return VirtualProtect(address, size, protection, previous);
}
}
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count) {
    const bool ok = read_code(address, out, count);
    if (ok && fault_read_at && address == fault_read_at && count == 5) { // the read-back after claim_call
        fault_read_at = 0;
        ++faults_taken;
        out[1] ^= 0xff;
    }
    return ok;
}
bool fixture_restore_call(CallSite& site) {
    if (fault_restore_at && site.address == fault_restore_at) { // as a refused VirtualProtect: still registered
        fault_restore_at = 0;
        ++faults_taken;
        site.status = "restore_protect_failed";
        return false;
    }
    return restore_call(site);
}
}

// ---- the engine pages ----
extern "C" {
extern unsigned char ee_code_page[], ee_window_a[], ee_window_b[], ee_join[], ee_callee_a[], ee_callee_b_page[],
    ee_callee_b[];
extern std::uint32_t ee_data_page[], ee_d3d_slot[];
// In: obj k eff trail pos[4] node mxcsr fcw df x87_a(2) x87_b(2).
std::uint32_t ee_in[16];
// Out at the join: eax ecx edx ebx esp ebp esi edi eflags mxcsr fcw st0(2) st1(2) window_esp frame_ebp.
std::uint32_t ee_out[20];
// Callee A: eax ecx edx ebx esp ebp esi edi eflags return args[10] calls. Callee B: the same with args[4].
std::uint32_t ee_ca[24], ee_cb[16];
std::uint32_t ee_saved_esp, ee_saved_mxcsr, ee_saved_fcw;
void __cdecl ee_enter();
}
asm(R"(
    .intel_syntax noprefix
    .section .x3meec,"xr"
    .balign 4096, 0xcc
    .globl _ee_code_page
    .globl _ee_enter
_ee_code_page:
_ee_enter:
    push ebp
    push ebx
    push esi
    push edi
    mov dword ptr [_ee_saved_esp], esp
    stmxcsr dword ptr [_ee_saved_mxcsr]
    fnstcw word ptr [_ee_saved_fcw]
    push dword ptr [_ee_in+0]                       # obj: [ebp+8] of the routine's frame
    push 0x0045ac82                                 # the routine's return address (0x0045ac7d + 5)
    push 0xe0e0e0e0                                 # its saved EBP
    mov ebp, esp
    and esp, -16
    sub esp, 0x90                                   # 0x84 of locals + push ebx/esi/edi
    mov ecx, dword ptr [_ee_in+4]
    mov dword ptr [esp+0x10], ecx                   # k
    mov ecx, dword ptr [_ee_in+8]
    mov dword ptr [esp+0x14], ecx                   # eff
    mov ecx, dword ptr [_ee_in+12]
    mov dword ptr [esp+0x18], ecx                   # trail
    mov ecx, dword ptr [_ee_in+16]
    mov dword ptr [esp+0x20], ecx                   # pos
    mov ecx, dword ptr [_ee_in+20]
    mov dword ptr [esp+0x24], ecx
    mov ecx, dword ptr [_ee_in+24]
    mov dword ptr [esp+0x28], ecx
    mov ecx, dword ptr [_ee_in+28]
    mov dword ptr [esp+0x2c], ecx
    mov dword ptr [_ee_out+60], esp
    mov dword ptr [_ee_out+64], ebp
    ldmxcsr dword ptr [_ee_in+36]
    fldcw word ptr [_ee_in+40]
    fld qword ptr [_ee_in+48]
    fld qword ptr [_ee_in+56]
    mov ebx, dword ptr [_ee_in+32]                  # the JET child node
    mov esi, 0x50505050
    mov edi, 0xd0d0d0d0
    mov ecx, 0xcccc0001
    mov edx, 0xdddd0001
    cmp dword ptr [_ee_in+44], 0
    je .Lee_df_clear
    std
.Lee_df_clear:
    mov eax, dword ptr [esp+0x14]                   # eff, as 0x004147bc loads it
    jmp _ee_window_a

_ee_exit_body:
    mov dword ptr [_ee_out+0], eax
    mov dword ptr [_ee_out+4], ecx
    mov dword ptr [_ee_out+8], edx
    mov dword ptr [_ee_out+12], ebx
    mov dword ptr [_ee_out+16], esp
    mov dword ptr [_ee_out+20], ebp
    mov dword ptr [_ee_out+24], esi
    mov dword ptr [_ee_out+28], edi
    pushfd
    pop dword ptr [_ee_out+32]
    cld
    stmxcsr dword ptr [_ee_out+36]
    fnstcw word ptr [_ee_out+40]
    fstp qword ptr [_ee_out+44]
    fstp qword ptr [_ee_out+52]
    ldmxcsr dword ptr [_ee_saved_mxcsr]
    fldcw word ptr [_ee_saved_fcw]
    mov esp, dword ptr [_ee_saved_esp]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

_ee_callee_a_body:                                  # 0x004148a0's stand-in: __cdecl, ten dwords
    mov dword ptr [_ee_ca+0], eax
    mov dword ptr [_ee_ca+4], ecx
    mov dword ptr [_ee_ca+8], edx
    mov dword ptr [_ee_ca+12], ebx
    mov dword ptr [_ee_ca+16], esp
    mov dword ptr [_ee_ca+20], ebp
    mov dword ptr [_ee_ca+24], esi
    mov dword ptr [_ee_ca+28], edi
    pushfd
    pop dword ptr [_ee_ca+32]
    mov eax, dword ptr [esp]
    mov dword ptr [_ee_ca+36], eax
    .irp i,1,2,3,4,5,6,7,8,9,10
    mov eax, dword ptr [esp+4*\i]
    mov dword ptr [_ee_ca+36+4*\i], eax
    .endr
    inc dword ptr [_ee_ca+80]
    mov eax, 0xa0a0a0a0
    mov ecx, 0xc1c1c1c1
    mov edx, 0xd1d1d1d1
    ret

_ee_callee_b_body:                                  # 0x00412d70's stand-in: __stdcall, four dwords
    mov dword ptr [_ee_cb+0], eax
    mov dword ptr [_ee_cb+4], ecx
    mov dword ptr [_ee_cb+8], edx
    mov dword ptr [_ee_cb+12], ebx
    mov dword ptr [_ee_cb+16], esp
    mov dword ptr [_ee_cb+20], ebp
    mov dword ptr [_ee_cb+24], esi
    mov dword ptr [_ee_cb+28], edi
    pushfd
    pop dword ptr [_ee_cb+32]
    mov eax, dword ptr [esp]
    mov dword ptr [_ee_cb+36], eax
    .irp i,1,2,3,4
    mov eax, dword ptr [esp+4*\i]
    mov dword ptr [_ee_cb+36+4*\i], eax
    .endr
    inc dword ptr [_ee_cb+56]
    mov eax, 0xb0b0b0b0
    mov ecx, 0xc2c2c2c2
    mov edx, 0xd2d2d2d2
    ret 0x10

    .fill 0x7c4 - (. - _ee_code_page), 1, 0xcc
    .globl _ee_window_a
_ee_window_a:
    .byte 0xf7,0x83,0x60,0x02,0x00,0x00,0x00,0x40,0x00,0x00  # 004147c4 test dword [ebx+0x260],0x4000
    .byte 0x75,0x23                                 # 004147ce jne 004147f3
    .byte 0x8b,0x55,0x08                            # 004147d0 mov edx,[ebp+8]
    .byte 0x6a,0x00,0x6a,0x00                       # 004147d3 push 0; push 0
    .byte 0x8d,0x4c,0x24,0x28                       # 004147d7 lea ecx,[esp+0x28]
    .byte 0x51,0x6a,0x00,0x6a,0x00,0x6a,0x00,0x52,0x50  # 004147db push ecx; push 0 x3; push edx; push eax
    .byte 0x8b,0x44,0x24,0x30                       # 004147e4 mov eax,[esp+0x30]
    .byte 0x50,0x6a,0x00                            # 004147e8 push eax; push 0
    .byte 0xe8,0xb0,0x00,0x00,0x00                  # 004147eb call 004148a0   <- site A
    .byte 0x83,0xc4,0x28                            # 004147f0 add esp,0x28
    .byte 0x8b,0x44,0x24,0x18,0x85,0xc0             # 004147f3 mov eax,[esp+0x18]; test eax,eax
    .byte 0x0f,0x8e,0x7d,0x00,0x00,0x00             # 004147f9 jle 0041487c
    .globl _ee_window_b
_ee_window_b:
    .byte 0xf7,0x83,0x60,0x02,0x00,0x00,0x00,0x20,0x00,0x00  # 004147ff test dword [ebx+0x260],0x2000
    .byte 0x75,0x71                                 # 00414809 jne 0041487c
    .byte 0x8b,0x0d,0x34,0x6f,0x60,0x00             # 0041480b mov ecx,[00606f34]
    .byte 0xf7,0x81,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,0x40  # 00414811 test dword [ecx+0xfc],0x40000000
    .byte 0x74,0x5f                                 # 0041481b je 0041487c
    .byte 0x8b,0x4d,0x08                            # 0041481d mov ecx,[ebp+8]
    .byte 0x8d,0x54,0x24,0x20,0x52                  # 00414820 lea edx,[esp+0x20]; push edx
    .byte 0x8b,0x54,0x24,0x14                       # 00414825 mov edx,[esp+0x14]
    .byte 0x51,0x50,0x52                            # 00414829 push ecx; push eax; push edx
    .byte 0xe8,0x3f,0xe5,0xff,0xff                  # 0041482c call 00412d70   <- site B
    .byte 0xeb,0x49                                 # 00414831 jmp 0041487c
    .fill 0x87c - (. - _ee_code_page), 1, 0xcc
    .globl _ee_join
_ee_join:
    jmp _ee_exit_body                               # 0041487c the fixture's exit
    .fill 0x8a0 - (. - _ee_code_page), 1, 0xcc
    .globl _ee_callee_a
_ee_callee_a:
    jmp _ee_callee_a_body                           # 004148a0
    .balign 4096, 0xcc

    .section .x3meeb,"xr"
    .balign 4096, 0xcc
    .globl _ee_callee_b_page
_ee_callee_b_page:
    .fill 0xd70, 1, 0xcc
    .globl _ee_callee_b
_ee_callee_b:
    jmp _ee_callee_b_body                           # 00412d70
    .balign 4096, 0xcc

    .section .x3meed,"dw"
    .balign 4096, 0
    .globl _ee_data_page
_ee_data_page:
    .fill 0xfc, 1, 0
    .long 0x40000000                                # VideoD3DFlags: bit 30, trails on
    .fill 0xf34 - (. - _ee_data_page), 1, 0
    .globl _ee_d3d_slot
_ee_d3d_slot:
    .long _ee_data_page                             # 00606f34
    .balign 4096, 0
    .text
    .att_syntax
)");

namespace {
constexpr std::uintptr_t code_page = 0x00414000, callee_b_page = 0x00412000, data_page = 0x00606000;
constexpr DWORD sentinel_error = 0x1234abcd;
constexpr std::uint32_t hostile_mxcsr = 0x7f80, hostile_fcw = 0x0c7f; // round toward zero; x87 single, truncate
constexpr double x87_a = 1.25, x87_b = -3.5e10;
constexpr std::uint32_t status_flags = 0x8d5; // CF PF AF ZF SF OF
unsigned checks = 0, failures = 0;
void check(bool ok, const char* name, const char* detail = "") {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s%s%s\n", ok ? "pass" : "FAIL", name, !ok && *detail ? " " : "", ok ? "" : detail);
}
struct alignas(16) Object {
    unsigned char bytes[0x100];
};
Object ship, missile, wide_class;
alignas(16) std::uint32_t node[0x100]; // +0x260 = 0: neither 0x4000 nor 0x2000, both calls reached
constexpr std::uint32_t k_value = 3, eff_value = 712, trail_value = 21;
constexpr std::uint32_t pos_value[4] = {100, 0xffffff38u, 300, 0xdeadbeefu};

struct Run {
    std::uint32_t out[20], ca[24], cb[16];
    DWORD error;
};
std::uint32_t u32(const void* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p));
}
__attribute__((noinline)) Run run(const Object& obj) {
    std::memset(ee_in, 0, sizeof ee_in);
    std::memset(ee_out, 0, sizeof ee_out);
    std::memset(ee_ca, 0, sizeof ee_ca);
    std::memset(ee_cb, 0, sizeof ee_cb);
    ee_in[0] = u32(&obj);
    ee_in[1] = k_value;
    ee_in[2] = eff_value;
    ee_in[3] = trail_value;
    std::memcpy(ee_in + 4, pos_value, sizeof pos_value);
    ee_in[8] = u32(node);
    ee_in[9] = hostile_mxcsr;
    ee_in[10] = hostile_fcw;
    ee_in[11] = 1; // DF set
    std::memcpy(ee_in + 12, &x87_a, 8);
    std::memcpy(ee_in + 14, &x87_b, 8);
    SetLastError(sentinel_error);
    ee_enter();
    Run r{};
    r.error = GetLastError();
    std::memcpy(r.out, ee_out, sizeof r.out);
    std::memcpy(r.ca, ee_ca, sizeof r.ca);
    std::memcpy(r.cb, ee_cb, sizeof r.cb);
    return r;
}
void print_run(const char* phase, const char* object, const Run& r) {
    std::printf("RUN phase=%s object=%s calls_a=%lu calls_b=%lu exit_esp_delta=%ld ebx=%08lx ebp=%08lx esi=%08lx "
                "edi=%08lx eax=%08lx eflags=%08lx mxcsr=%08lx fcw=%04lx error=%08lx a_esp_delta=%ld a_return=%08lx "
                "b_esp_delta=%ld b_return=%08lx\n",
                phase, object, (unsigned long)r.ca[20], (unsigned long)r.cb[14],
                long(std::int32_t(r.out[4] - r.out[15])), (unsigned long)r.out[3], (unsigned long)r.out[5],
                (unsigned long)r.out[6], (unsigned long)r.out[7], (unsigned long)r.out[0], (unsigned long)r.out[8],
                (unsigned long)r.out[9], (unsigned long)(r.out[10] & 0xffff), (unsigned long)r.error,
                r.ca[20] ? long(std::int32_t(r.ca[4] - r.out[15])) : 0L, (unsigned long)r.ca[9],
                r.cb[14] ? long(std::int32_t(r.cb[4] - r.out[15])) : 0L, (unsigned long)r.cb[9]);
}
// Everything the routine relies on after the window, plus the hostile state, whatever path was taken.
bool frame_kept(const Run& r) {
    double st0 = 0, st1 = 0;
    std::memcpy(&st0, r.out + 11, 8);
    std::memcpy(&st1, r.out + 13, 8);
    return r.out[4] == r.out[15] /* stack balanced at the join */ && r.out[3] == u32(node) && r.out[5] == r.out[16] &&
           r.out[6] == 0x50505050u && r.out[7] == 0xd0d0d0d0u && (r.out[8] & 0x400u) /* DF */ &&
           r.out[9] == hostile_mxcsr && (r.out[10] & 0xffff) == hostile_fcw && st0 == x87_b && st1 == x87_a &&
           r.error == sentinel_error;
}
// The callees saw exactly what the engine passes (vanilla contract).
bool callee_contract(const Run& r, const Object& obj) {
    const std::uint32_t window_esp = r.out[15], pos = window_esp + 0x20;
    const std::uint32_t a_args[10] = {0, k_value, eff_value, u32(&obj), 0, 0, 0, pos, 0, 0};
    const std::uint32_t b_args[4] = {k_value, trail_value, u32(&obj), pos};
    return r.ca[20] == 1 && r.cb[14] == 1 && r.ca[4] == window_esp - 0x2c && r.ca[9] == sites::return_a_va &&
           !std::memcmp(r.ca + 10, a_args, sizeof a_args) && r.cb[4] == window_esp - 0x14 &&
           r.cb[9] == sites::return_b_va && !std::memcmp(r.cb + 10, b_args, sizeof b_args) && r.ca[3] == u32(node) &&
           r.cb[3] == u32(node) && r.ca[5] == r.out[16] && r.cb[5] == r.out[16] && (r.ca[8] & 0x400u) &&
           (r.cb[8] & 0x400u);
}
// Forwarded calls: the callee records equal the vanilla ones (every register, ESP, return, arguments), EFLAGS
// compared without the status bits the stub's class test writes.
bool same_callee_records(const Run& a, const Run& b) {
    for (unsigned i = 0; i < 21; ++i)
        if (i == 8 ? ((a.ca[i] ^ b.ca[i]) & ~status_flags) : a.ca[i] != b.ca[i]) return false;
    for (unsigned i = 0; i < 15; ++i)
        if (i == 8 ? ((a.cb[i] ^ b.cb[i]) & ~status_flags) : a.cb[i] != b.cb[i]) return false;
    return true;
}
bool read5(std::uintptr_t at, unsigned char out[5]) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(at), out, 5, &got) && got == 5;
}
std::uintptr_t call_target(std::uintptr_t site) {
    unsigned char now[5]{};
    if (!read5(site, now) || now[0] != 0xe8) return 0;
    std::int32_t rel = 0;
    std::memcpy(&rel, now + 1, 4);
    return site + 5 + std::uintptr_t(rel);
}
bool engine_windows_original() {
    return !std::memcmp(ee_window_a, sites::expected_window_a, sites::window_a_length) &&
           !std::memcmp(ee_window_b, sites::expected_window_b, sites::window_b_length);
}
bool redirected() {
    return call_target(sites::a_site_va) == u32(reinterpret_cast<void*>(&x3m_engine_effects_stub_a)) &&
           call_target(sites::b_site_va) == u32(reinterpret_cast<void*>(&x3m_engine_effects_stub_b));
}
DWORD protection(std::uintptr_t at) {
    MEMORY_BASIC_INFORMATION m{};
    return VirtualQuery(reinterpret_cast<const void*>(at), &m, sizeof m) ? m.Protect : 0;
}
void set_setting(const wchar_t* value) {
    SetEnvironmentVariableW(L"X3M_ENGINE_EFFECTS", value);
}
// The two rows initialize() just wrote, each containing its needle.
bool rows_have(size_t before, const char* a_needle, const char* b_needle) {
    return log_lines.size() == before + 2 && log_lines[before].find("engine_effects_patch site=A va=004147eb ") == 0 &&
           log_lines[before + 1].find("engine_effects_patch site=B va=0041482c ") == 0 &&
           log_lines[before].find(a_needle) != std::string::npos &&
           log_lines[before + 1].find(b_needle) != std::string::npos;
}
const char* last_row() {
    return log_lines.empty() ? "" : log_lines.back().c_str();
}
// initialize() with a setting that patches nothing: false, two rows, LastError kept, windows untouched.
void refusal(const char* name, const wchar_t* setting, bool executable, const char* row) {
    set_setting(setting);
    executable_ok = executable;
    const size_t before = log_lines.size();
    SetLastError(sentinel_error);
    const bool applied = eep::initialize();
    check(!applied && GetLastError() == sentinel_error && rows_have(before, row, row) && engine_windows_original() &&
              !eep::installed(),
          name, last_row());
    executable_ok = true;
}
eep::Addresses engine_addresses() {
    return eep::Addresses{sites::window_a_va, sites::target_a_va, sites::window_b_va, sites::target_b_va};
}
// install_at with the given addresses: refused or rolled back, with the expected states and the engine untouched.
void install_refused(const char* name, const eep::Addresses& a, const char* reason, const char* a_state,
                     const char* b_state) {
    SetLastError(sentinel_error);
    const bool ok = eep::install_at(a);
    char detail[200];
    std::snprintf(detail, sizeof detail, "state=%s a=%s b=%s", eep::state(), eep::site_state(0), eep::site_state(1));
    check(!ok && GetLastError() == sentinel_error && !std::strcmp(eep::state(), reason) &&
              !std::strcmp(eep::site_state(0), a_state) && !std::strcmp(eep::site_state(1), b_state) &&
              !eep::installed() && engine_windows_original() && protection(code_page) == PAGE_EXECUTE_READ,
          name, detail);
}
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    ship.bytes[sites::class_offset] = 7; // class word 0x0007, subtype 0x0301: only the 16-bit word decides
    ship.bytes[0x4a] = 0x01;
    ship.bytes[0x4b] = 0x03;
    missile.bytes[sites::class_offset] = 10;
    wide_class.bytes[sites::class_offset] = 7; // class word 0x0107: not a ship
    wide_class.bytes[sites::class_offset + 1] = 1;

    // Layout: the stand-ins sit at the engine's addresses and the windows are the header's.
    check(u32(ee_code_page) == code_page && u32(ee_window_a) == sites::window_a_va &&
              u32(ee_window_b) == sites::window_b_va && u32(ee_join) == sites::join_b_va &&
              u32(ee_callee_a) == sites::target_a_va && u32(ee_callee_b_page) == callee_b_page &&
              u32(ee_callee_b) == sites::target_b_va && u32(ee_data_page) == data_page &&
              u32(ee_d3d_slot) == sites::d3d_flags_slot_va,
          "layout_at_engine_addresses");
    check(engine_windows_original(), "windows_are_header_windows");
    check(call_target(sites::a_site_va) == sites::target_a_va && call_target(sites::b_site_va) == sites::target_b_va,
          "sites_call_callees");
    const DWORD code_protect = protection(code_page);
    check(code_protect == PAGE_EXECUTE_READ, "code_page_execute_read");

    // Vanilla: both callees reached for every class, with the engine's arguments.
    const Object* objects[3] = {&ship, &missile, &wide_class};
    const char* names[3] = {"ship", "missile", "class_0107"};
    Run vanilla[3];
    for (unsigned i = 0; i < 3; ++i) {
        vanilla[i] = run(*objects[i]);
        print_run("vanilla", names[i], vanilla[i]);
        check(callee_contract(vanilla[i], *objects[i]) && frame_kept(vanilla[i]), "vanilla_contract");
    }

    // Settings that patch nothing: two rows each.
    refusal("native_is_native", L"native", true, "state=native reason=native mode=native setting=native write=none");
    refusal("word_refused", L"on", true, "state=invalid_setting reason=invalid_setting mode=- setting=on write=none");
    refusal("case_refused", L"Off", true, "state=invalid_setting reason=invalid_setting mode=- setting=Off write=none");
    refusal("long_refused", L"plumesplumesplumes", true, "state=too_long reason=too_long mode=- setting=? write=none");
    refusal("executable_mismatch_refused", L"off", false,
            "state=executable_mismatch reason=executable_mismatch mode=off setting=off write=none");

    // Changed windows on copies of the page: refused before any write, the other window untouched.
    auto* copy = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    check(copy != nullptr, "private_page");
    if (copy) {
        std::memcpy(copy, ee_code_page, 4096);
        const std::uintptr_t base = u32(copy);
        const eep::Addresses on_copy{base + 0x7c4, call_target(base + 0x7eb), base + 0x7ff, call_target(base + 0x82c)};
        copy[0x7c4 + 7] = 0x41; // A's guard immediate 0x4000 -> 0x4100
        install_refused("changed_window_a_refused", on_copy, "bytes_mismatch_a", "bytes_mismatch", "not_attempted");
        copy[0x7c4 + 7] = 0x40;
        copy[0x7ff + 0x2e] ^= 0x01; // B's rel32 low byte
        install_refused("changed_window_b_refused",
                        {sites::window_a_va, sites::target_a_va, base + 0x7ff, on_copy.target_b}, "bytes_mismatch_b",
                        "not_attempted", "bytes_mismatch");
        check(!std::memcmp(copy + 0x7c4, sites::expected_window_a, sites::window_a_length), "copy_a_unwritten");
        VirtualFree(copy, 0, MEM_RELEASE);
    }
    install_refused("invalid_site_refused", {0, sites::target_a_va, sites::window_b_va, sites::target_b_va},
                    "invalid_site", "invalid_site", "invalid_site");

    // Partial installs: A claimed, B fails -> A restored (both or none).
    install_refused("b_target_mismatch_rolls_back_a",
                    {sites::window_a_va, sites::target_a_va, sites::window_b_va, sites::target_b_va + 1},
                    "target_mismatch", "rolled_back", "target_mismatch");
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, 0, 4096, nullptr);
    auto* writer = static_cast<unsigned char*>(section ? MapViewOfFile(section, FILE_MAP_WRITE, 0, 0, 4096) : nullptr);
    auto* roview = static_cast<unsigned char*>(
        section ? MapViewOfFile(section, FILE_MAP_READ | FILE_MAP_EXECUTE, 0, 0, 4096) : nullptr);
    check(writer && roview, "roview_mapped");
    if (writer && roview) {
        std::memcpy(writer, ee_code_page, 4096);
        const std::uintptr_t view = u32(roview);
        install_refused("b_protect_failed_rolls_back_a",
                        {sites::window_a_va, sites::target_a_va, view + 0x7ff, call_target(view + 0x82c)},
                        "protect_failed", "rolled_back", "protect_failed");
        check(!std::memcmp(roview, ee_code_page, 4096), "roview_unwritten");
        UnmapViewOfFile(roview);
        UnmapViewOfFile(writer);
    }
    if (section) CloseHandle(section);
    // Read-back faults (seam): the module's own rollback, judged by a second read-back.
    fault_read_at = sites::a_site_va;
    install_refused("a_readback_rolled_back", engine_addresses(), "readback_rolled_back", "readback_rolled_back",
                    "not_attempted");
    fault_read_at = sites::b_site_va;
    install_refused("b_readback_rolls_back_both", engine_addresses(), "readback_rolled_back", "rolled_back",
                    "readback_rolled_back");
    check(faults_taken == 2 && !fault_read_at, "readback_faults_taken");
    // A's read-back rollback refused: the redirect stays live and registered, nothing else installs, shutdown restores.
    fault_read_at = sites::a_site_va;
    fault_protect = true;
    SetLastError(sentinel_error);
    bool ok = eep::install_at(engine_addresses());
    check(!ok && GetLastError() == sentinel_error && !std::strcmp(eep::state(), "rollback_failed") &&
              !std::strcmp(eep::site_state(0), "rollback_failed") && !eep::installed() &&
              call_target(sites::a_site_va) == u32(reinterpret_cast<void*>(&x3m_engine_effects_stub_a)) &&
              call_target(sites::b_site_va) == sites::target_b_va && faults_taken == 4,
          "a_rollback_failed_registered", eep::state());
    check(!eep::install_at(engine_addresses()) && !std::strcmp(eep::state(), "already_installed"),
          "registered_refuses_reinstall");
    {
        const Run r = run(ship); // the registered A redirect alone: no instance, the trail still created
        check(r.ca[20] == 0 && r.cb[14] == 1 && frame_kept(r), "a_alone_skips_only_a");
    }
    SetLastError(sentinel_error);
    check(eep::shutdown() && GetLastError() == sentinel_error && engine_windows_original() &&
              !std::strcmp(eep::state(), "restored") && !std::strcmp(eep::site_state(0), "restored"),
          "registered_a_restored", eep::state());
    // B fails and A's restore is refused (seam): rollback_failed, A registered; shutdown restores it.
    fault_restore_at = sites::a_site_va;
    SetLastError(sentinel_error);
    ok = eep::install_at({sites::window_a_va, sites::target_a_va, sites::window_b_va, sites::target_b_va + 1});
    check(!ok && GetLastError() == sentinel_error && !std::strcmp(eep::state(), "rollback_failed") &&
              !std::strcmp(eep::site_state(0), "rollback_failed") &&
              !std::strcmp(eep::site_state(1), "target_mismatch") && !eep::installed() &&
              call_target(sites::b_site_va) == sites::target_b_va && faults_taken == 5,
          "a_restore_refused_registered", eep::state());
    check(eep::shutdown() && engine_windows_original() && !std::strcmp(eep::state(), "restored"),
          "refused_a_restored_at_shutdown", eep::state());

    // Installed through initialize(): off and plumes patch the same two calls; unset is the default, plumes.
    const wchar_t* modes[3] = {L"off", L"plumes", nullptr};
    const char* mode_names[3] = {"off", "plumes", "plumes"};
    const char* settings[3] = {"off", "plumes", "-"};
    const char* phases[3] = {"off", "plumes", "unset"};
    for (unsigned m = 0; m < 3; ++m) {
        set_setting(modes[m]);
        const size_t before = log_lines.size();
        SetLastError(sentinel_error);
        const bool armed = eep::initialize();
        char a_row[160], b_row[160];
        std::snprintf(a_row, sizeof a_row, "state=active reason=ok mode=%s setting=%s write=atomic", mode_names[m],
                      settings[m]);
        std::snprintf(b_row, sizeof b_row, "state=active reason=ok mode=%s setting=%s write=plain", mode_names[m],
                      settings[m]);
        check(armed && GetLastError() == sentinel_error && rows_have(before, a_row, b_row) && eep::installed() &&
                  redirected() && protection(code_page) == code_protect && !std::strcmp(eep::write_path(0), "atomic") &&
                  !std::strcmp(eep::write_path(1), "plain") && x3m_engine_effects_continue_a == sites::target_a_va &&
                  x3m_engine_effects_continue_b == sites::target_b_va,
              "installed_a_atomic_b_plain", last_row());
        SetLastError(sentinel_error);
        check(eep::initialize() && GetLastError() == sentinel_error && log_lines.size() == before + 2,
              "second_initialize_silent");
        for (unsigned i = 0; i < 3; ++i) {
            const Run r = run(*objects[i]);
            print_run(phases[m], names[i], r);
            if (objects[i] == &ship) {
                check(r.ca[20] == 0 && r.cb[14] == 0 && r.out[0] == 0 && frame_kept(r), "ship_skips_both");
            } else {
                check(callee_contract(r, *objects[i]) && same_callee_records(r, vanilla[i]) && frame_kept(r),
                      "non_ship_forwarded_unchanged");
            }
        }
        SetLastError(sentinel_error);
        check(eep::shutdown() && GetLastError() == sentinel_error && engine_windows_original() &&
                  protection(code_page) == code_protect && !std::strcmp(eep::state(), "restored") &&
                  !std::strcmp(eep::site_state(0), "restored") && !std::strcmp(eep::site_state(1), "restored") &&
                  !eep::installed(),
              "restored", eep::state());
        for (unsigned i = 0; i < 3; ++i) {
            const Run r = run(*objects[i]);
            check(callee_contract(r, *objects[i]) && same_callee_records(r, vanilla[i]) && frame_kept(r),
                  "vanilla_after_restore");
        }
    }

    // A foreign call over site A at shutdown: restore_not_owned, nothing written there, B still restored.
    set_setting(L"off");
    check(eep::initialize(), "reinstalled");
    {
        unsigned char foreign[5] = {0xe8, 0x00, 0x00, 0x00, 0x00};
        const std::uint32_t rel = sites::target_a_va + 0x10 - sites::return_a_va;
        std::memcpy(foreign + 1, &rel, 4);
        DWORD old = 0, unused = 0;
        VirtualProtect(reinterpret_cast<void*>(sites::a_site_va), 5, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(reinterpret_cast<void*>(sites::a_site_va), foreign, 5);
        VirtualProtect(reinterpret_cast<void*>(sites::a_site_va), 5, old, &unused);
        SetLastError(sentinel_error);
        const bool back = eep::shutdown();
        unsigned char now[5]{};
        read5(sites::a_site_va, now);
        check(!back && GetLastError() == sentinel_error && !std::memcmp(now, foreign, 5) &&
                  call_target(sites::b_site_va) == sites::target_b_va &&
                  !std::strcmp(eep::site_state(0), "restore_not_owned") &&
                  !std::strcmp(eep::site_state(1), "restored") && !std::strcmp(eep::state(), "restore_failed"),
              "foreign_bytes_not_restored", eep::site_state(0));
        check(!eep::install_at(engine_addresses()) && !std::strcmp(eep::state(), "already_installed"),
              "not_owned_stays_registered");
        // The module's own call back over A: the retried detach restores it.
        unsigned char ours[5] = {0xe8, 0, 0, 0, 0};
        const std::uint32_t to_stub = u32(reinterpret_cast<void*>(&x3m_engine_effects_stub_a)) - sites::return_a_va;
        std::memcpy(ours + 1, &to_stub, 4);
        VirtualProtect(reinterpret_cast<void*>(sites::a_site_va), 5, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(reinterpret_cast<void*>(sites::a_site_va), ours, 5);
        VirtualProtect(reinterpret_cast<void*>(sites::a_site_va), 5, old, &unused);
        check(eep::shutdown() && engine_windows_original() && !std::strcmp(eep::state(), "restored") &&
                  protection(code_page) == code_protect,
              "retried_detach_restores", eep::state());
    }

    // Late: after the first Present the install window is closed.
    engine_patch::close_install_window("fixture");
    refusal("late_claim_refused", L"off", true, "state=late_claim reason=late_claim mode=off setting=off write=none");

    std::printf("RESULT checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
