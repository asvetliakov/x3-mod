// Wine fixture of the run-in-background patch (src/proxy/run_in_background.cpp and src/proxy/engine_patch.cpp,
// compiled unchanged). Never launches the game.
//
// The executable links its own image sections at the engine's pages (build_run_in_background_patch.py):
//   .x3mrbc 0x00403000  an entry stub at +0x000 and the 60-byte window (run_in_background_sites.h expected_window,
//                       the engine's bytes) at +0x392 = 0x00403392, followed at 0x004033ce by the stub's epilogue;
//   .x3mrbr 0x004b8000  a stand-in for the registry read 0x004b8510 (returns rib_registry_value, clobbers ECX/EDX);
//   .x3mrbt 0x004d2000  a stand-in for the callee 0x004d2580 that records every register, EFLAGS and its return
//                       address, and returns 0x00c0ffee;
//   .x3mrbd 0x00606000  writable data: the input block pointer at 0x00606f3c -> rib_flags.
// So the window's absolute operands and both rel32 calls reach the fixture's stand-ins exactly as in X3AP.exe.
// rib_enter(local) stores `local` at [esp+0x14] as the init routine's frame does, loads sentinels into
// EBX/ESI/EDI/EBP/EDX, sets DF and jumps into the window; the window writes the flags word from the local (or from
// the registry stand-in when the local is -1) and calls 0x004d2580. Each scenario runs vanilla first, then with the
// patch installed through initialize(): the callee's register record must be identical (the thunk is invisible),
// the flags word must be the vanilla word with bit 0x4000 set, LastError must survive, and the thunk writes once.
#include "../../src/proxy/run_in_background.h"
#include "../../src/proxy/run_in_background_sites.h"
#include "../../src/proxy/engine_patch.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace rib = x3m::run_in_background;
namespace sites = x3m::run_in_background::sites;
namespace engine_patch = x3m::engine_patch;

// ---- production dependencies the module links against ----
namespace {
std::vector<std::string> log_lines;
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
HANDLE log_handle() noexcept {
    return INVALID_HANDLE_VALUE;
}
}
namespace x3m::object_trace {
bool executable_verified() {
    return executable_ok;
}
}

// ---- the engine pages ----
extern "C" {
extern unsigned char rib_code_page[], rib_window[], rib_registry_page[], rib_callee_page[];
extern std::uint32_t rib_data_page[], rib_slot[], rib_flags, rib_registry_value;
extern std::uint32_t rib_capture[12]; // eax ecx edx ebx esp ebp esi edi eflags return calls enter_esp
int __cdecl rib_enter(int local);
}
asm(R"(
    .intel_syntax noprefix
    .section .x3mrbc,"xr"
    .balign 4096, 0xcc
    .globl _rib_code_page
    .globl _rib_enter
_rib_code_page:
_rib_enter:
    push ebp
    push ebx
    push esi
    push edi
    mov dword ptr [_rib_capture+44], esp
    mov eax, dword ptr [esp+20]
    mov ebx, 0xb0b0b0b0
    mov esi, 0x50505050
    mov edi, 0xd0d0d0d0
    mov ebp, 0xe0e0e0e0
    mov edx, 0xdddd0001
    sub esp, 0x18
    mov dword ptr [esp+0x14], eax
    std
    jmp _rib_window
    .fill 0x392 - (. - _rib_code_page), 1, 0xcc
    .globl _rib_window
_rib_window:
    .byte 0x8b,0x0d,0x3c,0x6f,0x60,0x00             # 00403392 mov ecx,[00606f3c]
    .byte 0x8b,0x44,0x24,0x14                       # 00403398 mov eax,[esp+0x14]
    .byte 0x81,0x09,0x00,0x10,0x00,0x00             # 0040339c or dword [ecx],0x1000
    .byte 0x83,0xf8,0xff                            # 004033a2 cmp eax,-1
    .byte 0x75,0x10                                 # 004033a5 jne 004033b7
    .byte 0xbe,0xa8,0x56,0x55,0x00                  # 004033a7 mov esi,0x005556a8
    .byte 0xe8,0x5f,0x51,0x0b,0x00                  # 004033ac call 004b8510
    .byte 0x8b,0x0d,0x3c,0x6f,0x60,0x00             # 004033b1 mov ecx,[00606f3c]
    .byte 0x85,0xc0                                 # 004033b7 test eax,eax
    .byte 0x74,0x08                                 # 004033b9 je 004033c3
    .byte 0x81,0x09,0x00,0x40,0x00,0x00             # 004033bb or dword [ecx],0x4000
    .byte 0xeb,0x06                                 # 004033c1 jmp 004033c9
    .byte 0x81,0x21,0xff,0xbf,0xff,0xff             # 004033c3 and dword [ecx],0xffffbfff
    .byte 0xe8,0xb2,0xf1,0x0c,0x00                  # 004033c9 call 004d2580   <- site
    cld                                             # 004033ce the fixture's epilogue
    add esp, 0x18
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
    .balign 4096, 0xcc

    .section .x3mrbr,"xr"
    .balign 4096, 0xcc
    .globl _rib_registry_page
_rib_registry_page:
    .fill 0x510, 1, 0xcc
    mov eax, dword ptr [_rib_registry_value]        # 004b8510 the registry read's stand-in
    mov ecx, 0x0c0c0c0c
    mov edx, 0x0d0d0d0d
    ret
    .balign 4096, 0xcc

    .section .x3mrbt,"xr"
    .balign 4096, 0xcc
    .globl _rib_callee_page
_rib_callee_page:
    .fill 0x580, 1, 0xcc
    mov dword ptr [_rib_capture+0], eax             # 004d2580 the callee's stand-in: record, return 0x00c0ffee
    mov dword ptr [_rib_capture+4], ecx
    mov dword ptr [_rib_capture+8], edx
    mov dword ptr [_rib_capture+12], ebx
    mov dword ptr [_rib_capture+16], esp
    mov dword ptr [_rib_capture+20], ebp
    mov dword ptr [_rib_capture+24], esi
    mov dword ptr [_rib_capture+28], edi
    pushfd
    pop dword ptr [_rib_capture+32]
    mov eax, dword ptr [esp]
    mov dword ptr [_rib_capture+36], eax
    inc dword ptr [_rib_capture+40]
    mov eax, 0x00c0ffee
    ret
    .balign 4096, 0xcc

    .section .x3mrbd,"dw"
    .balign 4096, 0
    .globl _rib_data_page
    .globl _rib_flags
    .globl _rib_registry_value
    .globl _rib_capture
    .globl _rib_slot
_rib_data_page:
    .fill 0x100, 1, 0
_rib_flags:
    .long 0
_rib_registry_value:
    .long 0
    .fill 0x200 - (. - _rib_data_page), 1, 0
_rib_capture:
    .fill 48, 1, 0
    .fill 0xf3c - (. - _rib_data_page), 1, 0
_rib_slot:
    .long _rib_flags                                # 00606f3c the input block pointer
    .balign 4096, 0
    .text
    .att_syntax
)");

namespace {
constexpr std::uintptr_t code_page = 0x00403000, registry_page = 0x004b8000, callee_page = 0x004d2000,
                         data_page = 0x00606000;
constexpr DWORD sentinel_error = 0x1234abcd;
unsigned checks = 0, failures = 0;
void check(bool ok, const char* name, const char* detail = "") {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s%s%s\n", ok ? "pass" : "FAIL", name, !ok && *detail ? " " : "", ok ? "" : detail);
}
struct Capture {
    std::uint32_t r[12];
};
struct Run {
    int ret;
    std::uint32_t flags;
    DWORD error;
    Capture c;
};
// One pass through the window with the flags word cleared and the registry stand-in set.
__attribute__((noinline)) Run run(int local, std::uint32_t registry) {
    rib_flags = 0;
    rib_registry_value = registry;
    std::memset(rib_capture, 0, sizeof rib_capture);
    SetLastError(sentinel_error);
    Run out{};
    out.ret = rib_enter(local);
    out.error = GetLastError();
    out.flags = rib_flags;
    std::memcpy(out.c.r, rib_capture, sizeof out.c.r);
    return out;
}
// Registers at the callee, relative where the stack is concerned (ESP as a distance below rib_enter's entry ESP).
bool same_registers(const Capture& a, const Capture& b) {
    for (unsigned i = 0; i < 10; ++i) {
        if (i == 4) continue;
        if (a.r[i] != b.r[i]) return false;
    }
    return a.r[11] - a.r[4] == b.r[11] - b.r[4] && a.r[10] == 1 && b.r[10] == 1;
}
void print_run(const char* phase, const char* scenario, const Run& r) {
    std::printf(
        "RUN phase=%s scenario=%s ret=0x%08x flags=0x%08lx error=0x%08lx eax=%08lx ecx=%08lx edx=%08lx ebx=%08lx "
        "esi=%08lx edi=%08lx ebp=%08lx eflags=%08lx return=%08lx calls=%lu stack=%lu\n",
        phase, scenario, unsigned(r.ret), (unsigned long)r.flags, (unsigned long)r.error, (unsigned long)r.c.r[0],
        (unsigned long)r.c.r[1], (unsigned long)r.c.r[2], (unsigned long)r.c.r[3], (unsigned long)r.c.r[6],
        (unsigned long)r.c.r[7], (unsigned long)r.c.r[5], (unsigned long)r.c.r[8], (unsigned long)r.c.r[9],
        (unsigned long)r.c.r[10], (unsigned long)(r.c.r[11] - r.c.r[4]));
}
bool read_site(std::uintptr_t at, unsigned char out[5]) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(at), out, 5, &got) && got == 5;
}
bool site_is_original(std::uintptr_t window) {
    unsigned char now[5]{};
    return read_site(window + sites::site_offset, now) &&
           !std::memcmp(now, sites::expected_window + sites::site_offset, 5);
}
std::uintptr_t site_target(std::uintptr_t window) {
    unsigned char now[5]{};
    if (!read_site(window + sites::site_offset, now) || now[0] != 0xe8) return 0;
    std::int32_t rel = 0;
    std::memcpy(&rel, now + 1, 4);
    return window + sites::site_offset + 5 + std::uintptr_t(rel);
}
DWORD protection(const void* at) {
    MEMORY_BASIC_INFORMATION m{};
    return VirtualQuery(at, &m, sizeof m) ? m.Protect : 0;
}
void set_setting(const wchar_t* value) {
    SetEnvironmentVariableW(L"X3M_RUN_IN_BACKGROUND", value);
}
bool last_row_has(const char* needle) {
    return !log_lines.empty() && log_lines.back().find(needle) != std::string::npos;
}
// initialize() with a setting: one row, the expected status and reason, the site left as found.
void refusal(const char* name, const wchar_t* setting, bool executable, const char* row) {
    set_setting(setting);
    executable_ok = executable;
    const size_t before = log_lines.size();
    SetLastError(sentinel_error);
    const bool applied = rib::initialize();
    check(!applied && GetLastError() == sentinel_error && log_lines.size() == before + 1 && last_row_has(row) &&
              site_is_original(sites::window_va),
          name, log_lines.empty() ? "" : log_lines.back().c_str());
    executable_ok = true;
}

struct Scenario {
    const char* name;
    int local;
    std::uint32_t registry;
    bool vanilla_bit;
    const char* outcome;
};
const Scenario scenarios[] = {
    {"no_argument_registry_0", -1, 0, false, "patched"}, // the CrossOver shortcut: the case the setting exists for
    {"no_argument_registry_1", -1, 1, true, "already"},  // RunInBackground=1 in the registry
    {"noruninbg", 0, 7, false, "patched"},               // -noruninbg: the setting overrides it (documented)
    {"runinbg", 1, 0, true, "already"},                  // -runinbg (the developer launcher): nothing written
};
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    // Layout: the stand-ins sit at the engine's addresses and the window is the header's.
    check(reinterpret_cast<std::uintptr_t>(rib_code_page) == code_page &&
              reinterpret_cast<std::uintptr_t>(rib_window) == sites::window_va &&
              reinterpret_cast<std::uintptr_t>(rib_registry_page) == registry_page &&
              reinterpret_cast<std::uintptr_t>(rib_callee_page) == callee_page &&
              reinterpret_cast<std::uintptr_t>(rib_data_page) == data_page &&
              reinterpret_cast<std::uintptr_t>(rib_slot) == sites::input_block_slot_va &&
              *reinterpret_cast<std::uint32_t* const*>(sites::input_block_slot_va) == &rib_flags,
          "layout_at_engine_addresses");
    check(!std::memcmp(rib_window, sites::expected_window, sites::window_length), "window_is_header_window");
    check(site_target(sites::window_va) == sites::target_va, "site_calls_callee");
    const DWORD code_protect = protection(rib_code_page);
    check(code_protect == PAGE_EXECUTE_READ, "code_page_execute_read");

    // Vanilla: the engine's semantics of the local and the registry value.
    Run vanilla[4];
    for (unsigned i = 0; i < 4; ++i) {
        const Scenario& s = scenarios[i];
        vanilla[i] = run(s.local, s.registry);
        print_run("vanilla", s.name, vanilla[i]);
        const std::uint32_t want = 0x1000u | (s.vanilla_bit ? sites::run_in_background_bit : 0u);
        check(vanilla[i].ret == 0x00c0ffee && vanilla[i].flags == want && vanilla[i].error == sentinel_error &&
                  vanilla[i].c.r[9] == sites::return_va && vanilla[i].c.r[10] == 1 && (vanilla[i].c.r[8] & 0x400u),
              "vanilla_semantics");
    }

    // Refusals and off: one row each, nothing written.
    refusal("unset_is_off", nullptr, true, "status=off reason=unset setting=-");
    refusal("zero_is_off", L"0", true, "status=off reason=setting_off setting=0");
    refusal("word_refused", L"on", true, "status=refused reason=invalid_setting setting=?");
    refusal("long_refused", L"1111", true, "status=refused reason=invalid_setting setting=?");
    refusal("executable_mismatch_refused", L"1", false, "status=refused reason=executable_mismatch setting=1");

    // install_at on copies: a changed window byte, a foreign call target, a read-only view.
    auto* copy = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    check(copy != nullptr, "private_page");
    if (copy) {
        std::memcpy(copy, rib_code_page, 4096);
        DWORD old = 0;
        VirtualProtect(copy, 4096, PAGE_EXECUTE_READ, &old);
        const std::uintptr_t window = reinterpret_cast<std::uintptr_t>(copy) + 0x392;
        check(!rib::install_at({window, sites::target_va, sites::input_block_slot_va}) &&
                  !std::strcmp(rib::state(), "target_mismatch") && site_is_original(window),
              "foreign_target_refused", rib::state());
        VirtualProtect(copy, 4096, PAGE_EXECUTE_READWRITE, &old);
        copy[0x392 + 0x29 + 3] = 0x41; // the OR's immediate 0x4000 -> 0x4100
        VirtualProtect(copy, 4096, PAGE_EXECUTE_READ, &old);
        check(!rib::install_at({window, sites::target_va, sites::input_block_slot_va}) &&
                  !std::strcmp(rib::state(), "bytes_mismatch") && site_is_original(window),
              "changed_window_refused", rib::state());
        VirtualFree(copy, 0, MEM_RELEASE);
    }
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, 0, 4096, nullptr);
    auto* writer = static_cast<unsigned char*>(section ? MapViewOfFile(section, FILE_MAP_WRITE, 0, 0, 4096) : nullptr);
    auto* roview = static_cast<unsigned char*>(
        section ? MapViewOfFile(section, FILE_MAP_READ | FILE_MAP_EXECUTE, 0, 0, 4096) : nullptr);
    check(writer && roview, "roview_mapped");
    if (writer && roview) {
        std::memcpy(writer, rib_code_page, 4096);
        const std::uintptr_t window = reinterpret_cast<std::uintptr_t>(roview) + 0x392;
        const bool installed = rib::install_at({window, site_target(window), sites::input_block_slot_va});
        check(!installed && !std::strcmp(rib::state(), "protect_failed") && site_is_original(window) &&
                  !std::memcmp(roview, rib_code_page, 4096),
              "readonly_view_refused", rib::state());
        UnmapViewOfFile(roview);
        UnmapViewOfFile(writer);
    }
    if (section) CloseHandle(section);

    // Installed: each scenario once through initialize(), the one write, the second pass disarmed, the restore.
    for (unsigned i = 0; i < 4; ++i) {
        const Scenario& s = scenarios[i];
        set_setting(L"1");
        const size_t rows = log_lines.size();
        SetLastError(sentinel_error);
        const bool armed = rib::initialize();
        const bool armed_ok = armed && GetLastError() == sentinel_error && log_lines.size() == rows + 1 &&
                              last_row_has("status=armed reason=ok setting=1") && last_row_has("write=atomic") &&
                              site_target(sites::window_va) ==
                                  reinterpret_cast<std::uintptr_t>(&x3m_run_in_background_thunk) &&
                              protection(rib_code_page) == code_protect;
        check(armed_ok, "armed", log_lines.empty() ? "" : log_lines.back().c_str());
        const Run patched = run(s.local, s.registry);
        print_run("patched", s.name, patched);
        char row[160];
        const std::uint32_t before = vanilla[i].flags, after = before | sites::run_in_background_bit;
        std::snprintf(row, sizeof row,
                      "status=%s reason=ok setting=1 value_before=%u value_after=1 flags_before=0x%08lx "
                      "flags_after=0x%08lx",
                      s.outcome, (before & sites::run_in_background_bit) ? 1u : 0u, (unsigned long)before,
                      (unsigned long)after);
        check(patched.ret == 0x00c0ffee && same_registers(patched.c, vanilla[i].c), "thunk_invisible_to_callee");
        check(patched.flags == after && patched.error == sentinel_error, "flags_set_lasterror_kept");
        check(rib::outcome() && !std::strcmp(rib::outcome(), s.outcome) && rib::flags_before() == before &&
                  rib::flags_after() == after && log_lines.size() == rows + 2 && last_row_has(row),
              "one_site_row", log_lines.empty() ? "" : log_lines.back().c_str());
        // The site again (a second init would do this): the thunk is disarmed, the engine's word stays vanilla.
        const Run again = run(0, 0);
        check(again.flags == 0x1000u && again.error == sentinel_error && log_lines.size() == rows + 2 &&
                  same_registers(again.c, vanilla[2].c), // local 0: the registry stand-in is not called either way
              "second_pass_writes_nothing");
        check(rib::shutdown() && site_is_original(sites::window_va) && protection(rib_code_page) == code_protect &&
                  !std::strcmp(rib::state(), "restored"),
              "restored");
        const Run restored = run(s.local, s.registry);
        check(restored.flags == vanilla[i].flags && same_registers(restored.c, vanilla[i].c), "vanilla_after_restore");
    }

    // Late: after the first Present the install window is closed.
    engine_patch::close_install_window("fixture");
    refusal("late_claim_refused", L"1", true, "status=refused reason=late_claim setting=1");

    std::printf("RESULT checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
