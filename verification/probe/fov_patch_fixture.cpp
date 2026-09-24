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
//            the constructor stored;
//   .x3mfvr  0x00421000  the per-frame reader: `mov edx,[0x00608504]; mov
//            esi,[edx+0x24]` at 0x00421148 (the reader contract), returning ESI;
//   .x3mfvs  0x0042d000  INS_SetFocus's store `mov [edx+0x24],ecx` at
//            0x0042dc04: cdecl void(void* registry, uint32 focus);
//   .x3mfvd  0x00608000  writable data: the registry slot at 0x00608504.
// Plus a VirtualAlloc page (MEM_PRIVATE) for install_at(). initialize() runs at
// the production constants; every step checks the executed constructor's
// result, the imm32 read back (ReadProcessMemory), the rest of the page, the
// page protection, the call counts on the seam, the log rows, the registry
// field and LastError. Never launches the game.
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
    if (bit(g.read_fail, g.reads++)) return false;
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
extern "C" unsigned char fov_ctor_page[], fov_reader_page[], fov_setfocus_page[];
extern "C" std::uint32_t fov_registry_slot;
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
    .fill 0xbfc, 1, 0xcc
    .byte 0x8b,0x54,0x24,0x04                    # +bfc mov edx,[esp+4]            ; registry
    .byte 0x8b,0x4c,0x24,0x08                    # +c00 mov ecx,[esp+8]            ; focus
    .byte 0x89,0x4a,0x24                         # +c04 mov [edx+0x24],ecx         <- INS_SetFocus store
    .byte 0xc3                                   # +c07 ret
    .balign 4096, 0xcc
    .section .x3mfvd,"dw"
    .balign 4096, 0
    .fill 0x504, 1, 0
    .globl _fov_registry_slot
_fov_registry_slot:
    .long 0
    .balign 4096, 0
    .text
)");

namespace {
constexpr unsigned page_size = 4096, window_offset = 0x9cc, write_at = window_offset + sites::write_offset;
constexpr std::uintptr_t ctor_page = sites::window_va & ~std::uintptr_t(page_size - 1);
constexpr std::uintptr_t reader_entry = 0x00421144, setfocus_entry = 0x0042dbfc;
static_assert(ctor_page + window_offset == sites::window_va, "the stub's window sits at the engine's page offset");
using Ctor = int (*)(void* registry);
using Reader = std::uint32_t (*)();
using SetFocusStore = void (*)(void* registry, std::uint32_t focus);

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
// The page equals `reference` except for the four imm32 bytes, which must be `span`.
bool page_is(const unsigned char* page, const unsigned char* reference, const unsigned char span[4]) {
    unsigned char now[page_size];
    SIZE_T n = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), page, now, page_size, &n) || n != page_size) return false;
    const unsigned s = write_at;
    return !std::memcmp(now, reference, s) && !std::memcmp(now + s, span, 4) && !std::memcmp(now + s + 4, reference + s + 4, page_size - s - 4);
}
bool poke(unsigned char* at, const unsigned char* bytes, unsigned n) {
    DWORD old = 0, unused = 0;
    if (!::VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(at, bytes, n);
    const bool ok = ::VirtualProtect(at, n, old, &unused) != FALSE;
    ::FlushInstructionCache(GetCurrentProcess(), at, n);
    return ok;
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
std::string install_row(const char* status, const char* reason, std::uint32_t value, const char* setting, const char* write, const char* registry, const char* before) {
    char text[240];
    std::snprintf(text, sizeof text, "fov site=%08lx status=%s reason=%s value=0x%04lx vertical_deg=%.2f setting=%s write=%s registry=%s registry_before=%s",
                  static_cast<unsigned long>(sites::write_va), status, reason, static_cast<unsigned long>(value), sites::vertical_for_focus(value), setting, write, registry, before);
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
                        reinterpret_cast<std::uintptr_t>(fov_setfocus_page) == 0x0042d000 && reinterpret_cast<std::uintptr_t>(&fov_registry_slot) == sites::registry_slot_va;
    check(placed, "pages_at_engine_vas");
    if (!placed) { std::printf("RESULT checks=%u failures=%u\n", checks, failures); return 1; }
    check(!std::memcmp(engine + window_offset, sites::expected_window, sites::window_length) &&
          !std::memcmp(fov_reader_page + 0x148, sites::expected_reader, sites::reader_length) && !std::memcmp(fov_setfocus_page + 0xc04, sites::expected_setfocus, sites::setfocus_length),
          "window_reader_setfocus_bytes_at_engine_vas");
    check(((reinterpret_cast<std::uintptr_t>(engine) + write_at) & 7u) == 4u && reinterpret_cast<std::uintptr_t>(engine) + write_at == sites::write_va,
          "engine_imm32_offset_4_of_its_qword");
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
    set_slot(nullptr);

    // ---- off and refusals through initialize(): nothing protected, written or flushed ----
    struct Refusal { const wchar_t* setting; bool exe; const char* name; std::string row; };
    const Refusal refusals[] = {
        {nullptr, true, "default_unset_off", install_row("off", "game", 0x4000, "-", "none", "skipped", "-")},
        {L"game", true, "game_off", install_row("off", "game", 0x4000, "game", "none", "skipped", "-")},
        {L"73.74", true, "engine_value_off", install_row("off", "engine_value", 0x4000, "73.74", "none", "skipped", "-")},
        {L"35.9", true, "refuse_below_36", install_row("refused", "out_of_range", 0x4000, "35.9", "none", "skipped", "-")},
        {L"120.5", true, "refuse_above_120", install_row("refused", "out_of_range", 0x4000, "120.5", "none", "skipped", "-")},
        {L"Game", true, "refuse_invalid_setting", install_row("refused", "invalid_setting", 0x4000, "Game", "none", "skipped", "-")},
        {L"58.71550000000000000000000000000000", true, "refuse_too_long", install_row("refused", "too_long", 0x4000, "?", "none", "skipped", "-")},
        {L"58.7155", false, "refuse_executable_mismatch", install_row("refused", "executable_mismatch", 0x4000, "58.7155", "none", "skipped", "-")}};
    for (const Refusal& r : refusals) {
        set_fov(r.setting);
        executable_ok = r.exe;
        arm();
        const bool applied = initialize_checked(r.name);
        char name[96];
        std::snprintf(name, sizeof name, "%s_row_and_untouched", r.name);
        check(!applied && !fov::patched() && last_log() == r.row && g.protects == 0 && g.writes == 0 && g.flushes == 0 && fov::configured_focus() == 0x4000 &&
              page_is(engine, reference, original), name, last_log().c_str());
    }
    set_fov(L"58.7155");
    executable_ok = true;
    // The reader contract: a changed byte of the per-frame reader refuses the patch.
    const unsigned char reader_changed = 0x25, reader_back = 0x24;
    check(poke(fov_reader_page + 0x150, &reader_changed, 1), "setup_changed_reader_byte");
    arm();
    check(!initialize_checked("refuse_reader_mismatch") && fov::state() == std::string("reader_mismatch") && g.protects == 0 && g.writes == 0 &&
          page_is(engine, reference, original) && last_log() == install_row("refused", "reader_mismatch", 0x4000, "58.7155", "none", "skipped", "-"),
          "refuse_reader_mismatch_untouched", last_log().c_str());
    check(poke(fov_reader_page + 0x150, &reader_back, 1), "setup_reader_byte_back");
    // A changed window byte and an already patched, unregistered window: bytes_mismatch.
    const unsigned char changed = 0x21, restored_byte = 0x20;
    check(poke(engine + window_offset + 2, &changed, 1), "setup_changed_window_byte");
    unsigned char changed_reference[page_size];
    std::memcpy(changed_reference, reference, page_size);
    changed_reference[window_offset + 2] = changed;
    arm();
    check(!initialize_checked("refuse_changed_window") && fov::state() == std::string("bytes_mismatch") && g.protects == 0 && g.writes == 0 &&
          page_is(engine, changed_reference, original) && last_log() == install_row("refused", "bytes_mismatch", 0x4000, "58.7155", "none", "skipped", "-"),
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
          protection(engine) == PAGE_EXECUTE_READ && last_log() == install_row("refused", "protect_failed", 0x4000, "58.7155", "none", "skipped", "-"),
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
          last_log() == install_row("patched", "ok", 0x3470, "58.7155", "atomic", "absent", "-"), "install_ok_atomic_registry_absent_row", last_log().c_str());
    std::snprintf(detail, sizeof detail, "protects=%u reads=%u writes=%u atomic=%u flushes=%u previous=0x%lx during=0x%lx", g.protects, g.reads,
                  g.writes, g.atomic_writes, g.flushes, g.first_previous, g.first_during);
    check(g.protects == 2 && g.reads == 4 && g.writes == 1 && g.atomic_writes == 1 && g.flushes == 1 && g.first_previous == PAGE_EXECUTE_READ &&
          writable_image(g.first_during), "install_sequence_counts", detail);
    std::printf("PROTECT memory=engine step=install previous=0x%lx during=0x%lx after=0x%lx\n", g.first_previous, g.first_during, protection(engine));
    std::printf("SEQUENCE step=install protects=%u reads=%u writes=%u atomic=%u flushes=%u\n", g.protects, g.reads, g.writes, g.atomic_writes, g.flushes);
    check(span_is(engine, ours) && page_is(engine, reference, ours), "install_readback_70340000_rest_of_page_unchanged");
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
    // The in-game FOV menu (INS_SetFocus) still overrides the base for the running session.
    reinterpret_cast<SetFocusStore>(setfocus_entry)(registry_a, 0x471c);
    std::printf("OVERRIDE setfocus=0x471c base=0x%04lx current=0x%04lx imm32=%s\n", static_cast<unsigned long>(read_base()),
                static_cast<unsigned long>(fov::current_focus()), span_is(engine, ours) ? "70340000" : "changed");
    check(read_base() == 0x471c && fov::current_focus() == 0x471c && span_is(engine, ours), "setfocus_overrides_base_patch_stays");
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
    check(clean && !fov::patched() && fov::state() == std::string("restored") && fov::configured_focus() == 0x4000 &&
          one_row(rows, "fov_restore site=0041c9dc status=restored found=70340000 registered=0"), "restore_row", rows.c_str());
    check(g.protects == 2 && g.writes == 1 && g.atomic_writes == 1 && g.flushes == 1 && page_is(engine, reference, original) && protection(engine) == PAGE_EXECUTE_READ,
          "restore_readback_00400000_protection");
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
              last_log() == install_row("patched", "ok", 0x3470, "58.7155", "atomic", c.state, c.before), name, last_log().c_str());
        std::snprintf(name, sizeof name, "%s_restore", c.name);
        rows = shutdown_rows(name);
        std::snprintf(name, sizeof name, "%s_restored", c.name);
        check(!fov::patched() && one_row(rows, "status=restored found=70340000 registered=0") && page_is(engine, reference, original), name, rows.c_str());
        if (c.kind == 2) ::VirtualProtect(readonly, page_size, PAGE_READWRITE, &old);
    }
    set_slot(nullptr);

    // ---- rollback paths (injected on the seam; every store and read is real) ----
    f = Faults{};
    f.read_fail = 1u << 3;  // reads: reader, setfocus, window, read-back
    arm(f);
    check(!initialize_checked("rollback_readback") && !fov::patched() && fov::state() == std::string("patch_rolled_back") && fov::configured_focus() == 0x4000 &&
          last_log() == install_row("refused", "patch_rolled_back", 0x4000, "58.7155", "atomic", "skipped", "-") && g.writes == 2 && g.atomic_writes == 2 &&
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
          last_log() == install_row("patched_unverified", "rollback_failed", 0x3471, "58.7155", "atomic", "skipped", "-") && span_is(engine, corrupt) &&
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
    check(fov::patched() && one_row(rows, "status=restore_not_owned found=71340000 registered=1") && g.writes == 0 && g.protects == 0 &&
          page_is(engine, reference, other), "restore_other_value_not_owned_untouched", rows.c_str());
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
    check(fov::patched() && one_row(rows, "status=restore_not_owned found=-- registered=1") && g.writes == 0 && g.protects == 0 &&
          page_is(engine, reference, ours), "restore_unreadable_found_unread_no_write", rows.c_str());
    arm();
    rows = shutdown_rows("restore_after_unreadable");
    check(!fov::patched() && one_row(rows, "status=restored found=70340000 registered=0") && page_is(engine, reference, original), "restore_after_unreadable_00400000", rows.c_str());

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

    // ---- the closed install window: late_claim ----
    engine_patch::close_install_window("fixture");
    arm();
    check(!initialize_checked("late_initialize") && fov::state() == std::string("late_claim") &&
          last_log() == install_row("refused", "late_claim", 0x4000, "58.7155", "none", "skipped", "-") && g.protects == 0 && g.writes == 0 &&
          page_is(engine, reference, original), "late_initialize_refused", last_log().c_str());
    arm();
    check(!fov::install_at(sites::window_va, 0x3470) && fov::state() == std::string("late_claim") && g.writes == 0, "late_install_at_refused");
    check(construct("engine", "after_late", engine) == 0x4000, "engine_after_late_ctor_4000");

    std::printf("RESULT checks=%u failures=%u\n", checks, failures);
    CloseHandle(restore_log);
    return failures ? 1 : 0;
}
