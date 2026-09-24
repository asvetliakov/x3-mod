// Wine fixture of the LOD occlusion patch's real write path
// (src/proxy/lod_occlusion.cpp, compiled unchanged; see
// lod_occlusion_patch_fixture_shim.h for the pass-through fault seam).
//
// Memory under test, each holding the same executable stub with the verified
// 31-byte window (lod_occlusion_sites.h expected_window) at page offset 0x4e7,
// so the jne sits at page offset 0x4f7 and its rel32 at 0x4f9, offset 1 of its
// aligned 8-byte word, exactly as 0x004c34f9 does:
//   engine   the image section .x3mocc of this executable, linked at the
//            engine's own page 0x004c3000 (MEM_IMAGE, the loader's
//            PAGE_EXECUTE_READ, as X3AP.exe's .text), driven through
//            initialize(), which installs at the production constant window_va;
//   private  a VirtualAlloc page (MEM_PRIVATE, PAGE_EXECUTE_READ), driven
//            through install_at();
//   roview   an executable read-only view of a pagefile section, whose
//            protection cannot be raised: a real VirtualProtect failure.
// The window's `mov edx,[00606f74]` reads the image section .x3mocd, linked at
// the engine's data page 0x00606000, which holds a marker at +0xf74.
// The stub is cdecl int(const void* mesh, const void* node) with an EBP frame
// like 0x004c0150: it runs the window (`mov ecx,[ebp+0xc]; cmp [ecx+0x14c],0;
// mov edx,[00606f74]; jne; cmp [esp+0x74],0; mov [esp+0x18],edx`) and returns
// 1 on the LOD-0 path (with the placeholder stored at [esp+0x18]; 3 if not)
// and 2 at the placeholder-bind offset 0x5c6, the jne's original target. A node
// with LOD index 1 returns 2 while the engine's rel32 is in place and 1 once it
// is 0; a node with LOD index 0 returns 1 either way. Every step checks the
// returned path, the rel32 read back (ReadProcessMemory), the rest of the page,
// the page protection, the call counts on the seam, the log rows and LastError.
// Never launches the game.
#include "../../src/proxy/lod_occlusion.h"
#include "../../src/proxy/lod_occlusion_sites.h"
#include "../../src/proxy/engine_patch.h"
#include "lod_occlusion_patch_fixture_shim.h"  // declarations only: X3M_LOD_OCCLUSION_SHIM is not defined here
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace occl = x3m::lod_occlusion;
namespace sites = x3m::lod_occlusion::sites;
namespace engine_patch = x3m::engine_patch;

// ---- the fault seam (declared in lod_occlusion_patch_fixture_shim.h) ----
namespace {
struct Faults {
    unsigned protect_fail = 0;   // bit i: the i-th VirtualProtect since arm() fails (ERROR_ACCESS_DENIED)
    unsigned read_fail = 0;      // bit i: the i-th read_code fails
    unsigned write_drop = 0;     // bit i: the i-th write_code stores nothing (and reports the atomic path)
    unsigned write_corrupt = 0;  // bit i: the i-th write_code stores c8 00 00 00 instead of the requested bytes
    bool skip_flush = false;     // FlushInstructionCache returns TRUE without flushing
    unsigned protects = 0, reads = 0, writes = 0, flushes = 0, atomic_writes = 0;
    DWORD first_previous = 0, first_during = 0;  // what the first VirtualProtect returned / VirtualQuery saw after it
};
Faults g;
void arm(const Faults& f = Faults{}) { g = f; }
bool bit(unsigned mask, unsigned i) { return i < 32 && ((mask >> i) & 1u); }
}
namespace x3m::lod_occlusion_fixture {
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
    if (g.skip_flush) return TRUE;
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
    if (bit(g.write_corrupt, i) && n == 4) { corrupt[0] = 0xc8; bytes = corrupt; }
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

// ---- the stub: its own image section .x3mocc, linked at 0x004c3000 (MEM_IMAGE), window at +0x4e7 = 0x004c34e7;
// the placeholder global in .x3mocd, linked at 0x00606000, marker at +0xf74 ----
extern "C" unsigned char occl_engine_page[];
extern "C" const std::uint32_t occl_placeholder_global;
asm(R"(
    .section .x3mocc,"xr"
    .balign 4096, 0xcc
    .globl _occl_engine_page
_occl_engine_page:
    .byte 0x55                                   # +000 push ebp
    .byte 0x8b,0xec                              # +001 mov ebp,esp
    .byte 0x83,0xec,0x7c                         # +003 sub esp,0x7c
    .byte 0xc7,0x44,0x24,0x74,0x00,0x00,0x00,0x00  # +006 mov dword [esp+0x74],0   ; the occlusion id slot, as the engine's frame
    .byte 0xc7,0x44,0x24,0x18,0x00,0x00,0x00,0x00  # +00e mov dword [esp+0x18],0   ; the placeholder slot
    .byte 0xe9,0xcc,0x04,0x00,0x00               # +016 jmp +0x4e7
    .fill 0x4e7 - (. - _occl_engine_page), 1, 0xcc
    .byte 0x8b,0x4d,0x0c                         # +4e7 mov ecx,[ebp+0xc]            <- window
    .byte 0x83,0xb9,0x4c,0x01,0x00,0x00,0x00     # +4ea cmp dword [ecx+0x14c],0
    .byte 0x8b,0x15,0x74,0x6f,0x60,0x00          # +4f1 mov edx,[00606f74]
    .byte 0x0f,0x85,0xc9,0x00,0x00,0x00          # +4f7 jne +0xc9 (-> +5c6)   <- site; rel32 00 00 00 00 once patched
    .byte 0x83,0x7c,0x24,0x74,0x00               # +4fd cmp dword [esp+0x74],0
    .byte 0x89,0x54,0x24,0x18                    # +502 mov [esp+0x18],edx
    .byte 0x8b,0x44,0x24,0x18                    # +506 mov eax,[esp+0x18]         ; the LOD-0 path: placeholder kept?
    .byte 0x3b,0xc2                              # +50a cmp eax,edx
    .byte 0xb8,0x01,0x00,0x00,0x00               # +50c mov eax,1
    .byte 0x74,0x05                              # +511 je +5
    .byte 0xb8,0x03,0x00,0x00,0x00               # +513 mov eax,3
    .byte 0x8b,0xe5                              # +518 mov esp,ebp
    .byte 0x5d                                   # +51a pop ebp
    .byte 0xc3                                   # +51b ret
    .fill 0x5c6 - (. - _occl_engine_page), 1, 0xcc
    .byte 0xb8,0x02,0x00,0x00,0x00               # +5c6 mov eax,2                  ; the placeholder bind
    .byte 0x8b,0xe5                              # +5cb mov esp,ebp
    .byte 0x5d                                   # +5cd pop ebp
    .byte 0xc3                                   # +5ce ret
    .balign 4096, 0xcc
    .section .x3mocd,"dr"
    .balign 4096, 0
    .fill 0xf74, 1, 0
    .globl _occl_placeholder_global
_occl_placeholder_global:
    .long 0x5a5a1234
    .balign 4096, 0
    .text
)");

namespace {
constexpr unsigned page_size = 4096, window_offset = 0x4e7, write_at = window_offset + sites::write_offset;
constexpr std::uintptr_t engine_page = sites::window_va & ~std::uintptr_t(page_size - 1);
constexpr std::uintptr_t placeholder_global_va = 0x00606000u + 0xf74u;  // the window's mov edx operand
static_assert(engine_page + window_offset == sites::window_va, "the stub's window sits at the engine's page offset");
static_assert(sites::placeholder_va - engine_page == 0x5c6, "the stub's placeholder bind sits at the engine's page offset");
using Stub = int (*)(const void* mesh, const void* node);

unsigned checks = 0, failures = 0;
void check(bool ok, const char* name, const char* detail = "") {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s%s%s\n", ok ? "pass" : "FAIL", name, !ok && *detail ? " " : "", ok ? "" : detail);
}
alignas(16) unsigned char node_lod0[0x200], node_lod1[0x200];
int run(const unsigned char* page, bool lod1) {
    return reinterpret_cast<Stub>(reinterpret_cast<std::uintptr_t>(page))(nullptr, lod1 ? node_lod1 : node_lod0);
}
// One BRANCH row per executed step: the path a LOD-0 and a LOD-1 node take (1 = LOD-0 bind path, 2 = placeholder bind).
struct Branch { int lod0, lod1; };
Branch branch(const char* memory, const char* step, const unsigned char* page) {
    Branch b{run(page, false), run(page, true)};
    std::printf("BRANCH memory=%s step=%s lod0=%d lod1=%d\n", memory, step, b.lod0, b.lod1);
    return b;
}
bool vanilla(Branch b) { return b.lod0 == 1 && b.lod1 == 2; }
bool patched_path(Branch b) { return b.lod0 == 1 && b.lod1 == 1; }
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
// The protection a PAGE_EXECUTE_READWRITE request leaves on an image page: this Wine reports
// PAGE_EXECUTE_WRITECOPY before and after the store (measured for the Terran fixture); on Windows the
// page is expected (inferred, not verified) to read PAGE_EXECUTE_READWRITE once the store has made a private copy.
bool writable_image(DWORD protect) { return protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY; }
// The page equals `reference` except for the four rel32 bytes, which must be `span`.
bool page_is(const unsigned char* page, const unsigned char* reference, const unsigned char span[4]) {
    unsigned char now[page_size];
    SIZE_T n = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), page, now, page_size, &n) || n != page_size) return false;
    const unsigned s = write_at;
    return !std::memcmp(now, reference, s) && !std::memcmp(now + s, span, 4) && !std::memcmp(now + s + 4, reference + s + 4, page_size - s - 4);
}
// Fixture setup writes (not the patch): raw Win32, bypassing the seam.
bool poke(unsigned char* at, const unsigned char* bytes, unsigned n) {
    DWORD old = 0, unused = 0;
    if (!::VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(at, bytes, n);
    const bool ok = ::VirtualProtect(at, n, old, &unused) != FALSE;
    ::FlushInstructionCache(GetCurrentProcess(), at, n);
    return ok;
}
void set_mode(const wchar_t* value) { SetEnvironmentVariableW(L"X3M_LOD_OCCLUSION", value); }
// The last install row, and the restore rows written to the log handle since the previous call.
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
// default=1 only where a case sets X3M_LOD_OCCLUSION_DEFAULT=1 with a value (the launcher's default marker).
std::string install_row(const char* status, const char* reason, const char* mode, const char* setting, const char* write, bool defaulted = false) {
    char text[200];
    std::snprintf(text, sizeof text, "lod_occlusion site=%08lx status=%s reason=%s mode=%s setting=%s write=%s default=%u",
                  static_cast<unsigned long>(sites::site_va), status, reason, mode, setting, write, defaulted ? 1u : 0u);
    return text;
}
double qpc_us(LARGE_INTEGER a, LARGE_INTEGER b) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return double(b.QuadPart - a.QuadPart) * 1e6 / double(f.QuadPart);
}
// initialize() with LastError preserved; returns its result.
bool initialize_checked(const char* name) {
    SetLastError(0x2bad);
    const bool r = occl::initialize();
    char label[96];
    std::snprintf(label, sizeof label, "%s_lasterror_preserved", name);
    check(GetLastError() == 0x2bad, label);
    return r;
}
bool shutdown_checked(const char* name) {
    SetLastError(0x2bad);
    const bool r = occl::shutdown();
    char label[96];
    std::snprintf(label, sizeof label, "%s_lasterror_preserved", name);
    check(GetLastError() == 0x2bad, label);
    return r;
}
// shutdown() and the restore rows it wrote.
std::string shutdown_rows(const char* name) {
    shutdown_checked(name);
    return new_restore_rows();
}

// A fault anywhere ends the run with one row instead of the Wine debugger.
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
    reinterpret_cast<std::uint32_t&>(node_lod0[sites::lod_offset]) = 0;
    reinterpret_cast<std::uint32_t&>(node_lod1[sites::lod_offset]) = 1;
    const unsigned char* const original = sites::expected_write;
    const unsigned char* const patched = sites::patched_write;
    const unsigned char corrupt[4] = {0xc8, 0x00, 0x00, 0x00};
    char temp_dir[MAX_PATH]{}, path[MAX_PATH + 64]{};
    GetTempPathA(MAX_PATH, temp_dir);
    std::snprintf(path, sizeof path, "%sx3m-lod-occlusion-patch-%lu.log", temp_dir, GetCurrentProcessId());
    restore_log = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    check(restore_log != INVALID_HANDLE_VALUE, "restore_log_opened");

    // ---- the pages ----
    unsigned char* const engine = occl_engine_page;
    check(reinterpret_cast<std::uintptr_t>(engine) == engine_page, "engine_page_at_engine_va");
    check(reinterpret_cast<std::uintptr_t>(&occl_placeholder_global) == placeholder_global_va && occl_placeholder_global == 0x5a5a1234u,
          "placeholder_global_at_engine_va");
    if (reinterpret_cast<std::uintptr_t>(engine) != engine_page || reinterpret_cast<std::uintptr_t>(&occl_placeholder_global) != placeholder_global_va) {
        std::printf("RESULT checks=%u failures=%u\n", checks, failures);
        return 1;
    }
    check(reinterpret_cast<std::uintptr_t>(engine) + window_offset == sites::window_va &&
          !std::memcmp(engine + window_offset, sites::expected_window, sites::window_length), "engine_window_at_window_va_is_expected_window");
    check(((reinterpret_cast<std::uintptr_t>(engine) + write_at) & 7u) == 1u && reinterpret_cast<std::uintptr_t>(engine) + write_at == sites::write_va,
          "engine_rel32_offset_1_of_its_qword");
    unsigned char reference[page_size];
    std::memcpy(reference, engine, page_size);

    auto* const priv = static_cast<unsigned char*>(VirtualAlloc(nullptr, page_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    check(priv != nullptr, "private_page_allocated");
    if (!priv) {
        std::printf("RESULT checks=%u failures=%u\n", checks, failures);
        return 1;
    }
    std::memcpy(priv, reference, page_size);
    DWORD old = 0;
    check(::VirtualProtect(priv, page_size, PAGE_EXECUTE_READ, &old) && ::FlushInstructionCache(GetCurrentProcess(), priv, page_size), "private_page_execute_read");
    char detail[160];

    MEMORY_BASIC_INFORMATION mi{};
    VirtualQuery(engine, &mi, sizeof mi);
    std::printf("MEMORY memory=engine type=0x%lx protect=0x%lx allocation_protect=0x%lx\n", mi.Type, mi.Protect, mi.AllocationProtect);
    check(mi.Type == MEM_IMAGE && mi.Protect == PAGE_EXECUTE_READ, "engine_page_is_mem_image_execute_read");
    VirtualQuery(priv, &mi, sizeof mi);
    std::printf("MEMORY memory=private type=0x%lx protect=0x%lx allocation_protect=0x%lx\n", mi.Type, mi.Protect, mi.AllocationProtect);
    check(mi.Type == MEM_PRIVATE && mi.Protect == PAGE_EXECUTE_READ, "private_page_is_mem_private_execute_read");

    Branch b = branch("engine", "before", engine);
    check(vanilla(b), "engine_before_placeholder_for_lod1");
    b = branch("private", "before", priv);
    check(vanilla(b), "private_before_placeholder_for_lod1");

    // ---- refusals and the default through initialize(): nothing is protected, written or flushed ----
    struct Refusal { const wchar_t* setting; bool exe; const char* name; const char* row; const wchar_t* marker; };
    const std::string unset_row = install_row("off", "record0", "record0", "-", "none");
    const std::string record0_row = install_row("off", "record0", "record0", "record0", "none");
    const std::string invalid_row = install_row("refused", "invalid_setting", "-", "All", "none");
    const std::string long_row = install_row("refused", "too_long", "-", "?", "none");
    const std::string exe_row = install_row("refused", "executable_mismatch", "all", "all", "none");
    const std::string exe_default_row = install_row("refused", "executable_mismatch", "all", "all", "none", true);
    const std::string record0_default_row = install_row("off", "record0", "record0", "record0", "none", true);
    // The launcher's default marker (X3M_LOD_OCCLUSION_DEFAULT=1) reaches the row as default=1 with a value, and
    // is ignored without one (unset stays default=0); any other marker value is default=0.
    const Refusal refusals[] = {{nullptr, true, "default_unset_off", unset_row.c_str(), nullptr},
                                {L"record0", true, "refuse_record0", record0_row.c_str(), nullptr},
                                {L"All", true, "refuse_invalid_setting", invalid_row.c_str(), nullptr},
                                {L"allallallallallallallallallallallall", true, "refuse_too_long", long_row.c_str(), nullptr},
                                {L"all", false, "refuse_executable_mismatch", exe_row.c_str(), nullptr},
                                {L"all", false, "default_marker_executable_mismatch", exe_default_row.c_str(), L"1"},
                                {L"record0", true, "default_marker_record0", record0_default_row.c_str(), L"1"},
                                {nullptr, true, "default_marker_unset_ignored", unset_row.c_str(), L"1"},
                                {L"all", false, "default_marker_other_value", exe_row.c_str(), L"yes"}};
    for (const Refusal& r : refusals) {
        set_mode(r.setting);
        SetEnvironmentVariableW(L"X3M_LOD_OCCLUSION_DEFAULT", r.marker);
        executable_ok = r.exe;
        arm();
        const bool applied = initialize_checked(r.name);
        char name[96];
        std::snprintf(name, sizeof name, "%s_row_and_untouched", r.name);
        check(!applied && !occl::patched() && last_log() == r.row && g.protects == 0 && g.writes == 0 && g.flushes == 0 &&
              page_is(engine, reference, original), name, last_log().c_str());
    }
    set_mode(L"all");
    SetEnvironmentVariableW(L"X3M_LOD_OCCLUSION_DEFAULT", nullptr);
    executable_ok = true;

    // A changed window byte (the cmp's displacement) and an already patched, unregistered window: bytes_mismatch.
    const unsigned char changed = 0x40, restored_byte = 0x4c;
    check(poke(engine + window_offset + 5, &changed, 1), "setup_changed_window_byte");
    unsigned char changed_reference[page_size];
    std::memcpy(changed_reference, reference, page_size);
    changed_reference[window_offset + 5] = changed;
    arm();
    bool r = initialize_checked("refuse_changed_window");
    check(!r && occl::state() == std::string("bytes_mismatch") && g.protects == 0 && g.writes == 0 &&
          page_is(engine, changed_reference, original) && last_log() == install_row("refused", "bytes_mismatch", "all", "all", "none"),
          "refuse_changed_window_untouched", last_log().c_str());
    check(poke(engine + window_offset + 5, &restored_byte, 1), "setup_restore_window_byte");
    check(poke(engine + write_at, patched, 4), "setup_foreign_patch");
    arm();
    check(!initialize_checked("refuse_already_patched") && occl::state() == std::string("bytes_mismatch") && g.protects == 0 && g.writes == 0 &&
          page_is(engine, reference, patched), "refuse_already_patched_window_untouched");
    check(poke(engine + write_at, original, 4) && page_is(engine, reference, original), "setup_foreign_patch_removed");

    // Injected protect failure: protect_failed, nothing written, branch unchanged.
    Faults f;
    f.protect_fail = 1;
    arm(f);
    r = initialize_checked("refuse_protect_failed");
    check(!r && !occl::patched() && g.writes == 0 && g.flushes == 0 && page_is(engine, reference, original) &&
          protection(engine) == PAGE_EXECUTE_READ && last_log() == install_row("refused", "protect_failed", "all", "all", "none"),
          "refuse_protect_failed_untouched", last_log().c_str());
    b = branch("engine", "after_protect_failed", engine);
    check(vanilla(b), "engine_after_protect_failed_placeholder");

    // ---- the production path at the engine's VA: initialize() -> patch, read-back, execute, restore ----
    LARGE_INTEGER t0, t1, t2, t3;
    arm();
    QueryPerformanceCounter(&t0);
    const bool applied = initialize_checked("install");
    QueryPerformanceCounter(&t1);
    check(applied && occl::patched() && occl::state() == std::string("ok") && occl::write_path() == std::string("atomic") &&
          last_log() == install_row("patched", "ok", "all", "all", "atomic"), "install_ok_atomic_row", last_log().c_str());
    std::snprintf(detail, sizeof detail, "protects=%u reads=%u writes=%u atomic=%u flushes=%u previous=0x%lx during=0x%lx", g.protects, g.reads,
                  g.writes, g.atomic_writes, g.flushes, g.first_previous, g.first_during);
    check(g.protects == 2 && g.reads == 2 && g.writes == 1 && g.atomic_writes == 1 && g.flushes == 1 && g.first_previous == PAGE_EXECUTE_READ &&
          writable_image(g.first_during), "install_sequence_counts", detail);
    std::printf("PROTECT memory=engine step=install previous=0x%lx during=0x%lx after=0x%lx\n", g.first_previous, g.first_during, protection(engine));
    check(span_is(engine, patched) && page_is(engine, reference, patched), "install_readback_00000000_rest_of_page_unchanged");
    check(protection(engine) == PAGE_EXECUTE_READ, "install_protection_restored");
    b = branch("engine", "after_patch", engine);
    check(patched_path(b), "engine_after_patch_lod0_path_for_lod1");
    arm();
    check(!occl::install_at(sites::window_va) && occl::patched() && occl::state() == std::string("already_installed") && g.protects == 0 && g.writes == 0 &&
          span_is(engine, patched), "second_install_refused_already_installed");
    check(initialize_checked("initialize_again") && occl::patched() && g.writes == 0, "initialize_again_no_second_write");
    arm();
    QueryPerformanceCounter(&t2);
    const bool clean = shutdown_checked("restore");
    QueryPerformanceCounter(&t3);
    std::string rows = new_restore_rows();
    check(clean && !occl::patched() && occl::state() == std::string("restored") && one_row(rows, "lod_occlusion_restore site=004c34f7 status=restored found=00000000 registered=0"),
          "restore_row", rows.c_str());
    check(g.protects == 2 && g.writes == 1 && g.atomic_writes == 1 && g.flushes == 1 && span_is(engine, original) && page_is(engine, reference, original) &&
          protection(engine) == PAGE_EXECUTE_READ, "restore_readback_c9000000_protection");
    b = branch("engine", "after_restore", engine);
    check(vanilla(b), "engine_after_restore_placeholder");
    check(shutdown_checked("restore_again") && new_restore_rows().empty(), "restore_again_no_row");
    std::printf("TIMING install_us=%.1f restore_us=%.1f\n", qpc_us(t0, t1), qpc_us(t2, t3));

    // ---- rollback paths (injected on the seam; every store and read is real) ----
    // Read-back fails after the real 00 00 00 00 store: rolled back, judged by reading c9 00 00 00.
    f = Faults{};
    f.read_fail = 2;
    arm(f);
    r = initialize_checked("rollback_readback");
    check(!r && !occl::patched() && occl::state() == std::string("patch_rolled_back") &&
          last_log() == install_row("refused", "patch_rolled_back", "all", "all", "atomic") && g.writes == 2 && g.atomic_writes == 2 &&
          page_is(engine, reference, original) && protection(engine) == PAGE_EXECUTE_READ, "rollback_readback_rolled_back", last_log().c_str());
    b = branch("engine", "after_rollback", engine);
    check(vanilla(b), "engine_after_rollback_placeholder");
    // The store does not land: read-back mismatch, rolled back.
    f = Faults{};
    f.write_drop = 1;
    arm(f);
    check(!initialize_checked("rollback_dropped_write") && !occl::patched() && occl::state() == std::string("patch_rolled_back") &&
          page_is(engine, reference, original) && protection(engine) == PAGE_EXECUTE_READ, "rollback_dropped_write_rolled_back");
    // Re-protect and the post-rollback protect fail: original bytes back, page left writable.
    f = Faults{};
    f.protect_fail = 2 | 4;
    arm(f);
    const bool unprotected = !initialize_checked("rollback_unprotected") && !occl::patched() && occl::state() == std::string("rollback_unprotected") &&
                             page_is(engine, reference, original);
    const DWORD left = protection(engine);
    std::snprintf(detail, sizeof detail, "protect=0x%lx", left);
    check(unprotected && writable_image(left), "rollback_unprotected_original_bytes_page_writable", detail);
    std::printf("PROTECT memory=engine step=rollback_unprotected left=0x%lx\n", left);
    b = branch("engine", "after_rollback_unprotected", engine);
    check(vanilla(b), "engine_after_rollback_unprotected_placeholder");
    check(::VirtualProtect(engine, page_size, PAGE_EXECUTE_READ, &old) != FALSE, "setup_reprotect_after_unprotected");
    // A wrong store that cannot be undone: rollback_failed stays registered. shutdown() restores only over the
    // patched 00 00 00 00 (the engine_patch::restore rule): the foreign c8 00 00 00 is refused (restore_not_owned,
    // nothing written, still registered). With the patched bytes back in the span, restore fails while stores are
    // dropped or protect fails, then puts c9 00 00 00 back.
    f = Faults{};
    f.write_corrupt = 1;
    f.write_drop = 2;
    arm(f);
    r = initialize_checked("rollback_failed");
    check(!r && occl::patched() && occl::state() == std::string("rollback_failed") &&
          last_log() == install_row("patched_unverified", "rollback_failed", "all", "all", "atomic") && span_is(engine, corrupt) &&
          protection(engine) == PAGE_EXECUTE_READ, "rollback_failed_registered", last_log().c_str());
    std::printf("BRANCH memory=engine step=after_rollback_failed lod0=- lod1=- (jne +0xc8 not executed: mid-instruction target)\n");
    arm();
    rows = shutdown_rows("restore_not_owned");
    check(occl::patched() && occl::state() == std::string("restore_not_owned") && one_row(rows, "status=restore_not_owned found=c8000000 registered=1") &&
          g.writes == 0 && g.protects == 0 && g.flushes == 0 && span_is(engine, corrupt) && protection(engine) == PAGE_EXECUTE_READ,
          "restore_not_owned_foreign_bytes_untouched_registered", rows.c_str());
    check(poke(engine + write_at, patched, 4), "setup_span_back_to_patched");
    f = Faults{};
    f.write_drop = 1;
    arm(f);
    rows = shutdown_rows("restore_dropped");
    check(occl::patched() && occl::state() == std::string("restore_failed") &&
          one_row(rows, "status=restore_failed found=00000000 registered=1") && span_is(engine, patched) && protection(engine) == PAGE_EXECUTE_READ,
          "restore_dropped_write_failed_registered", rows.c_str());
    f = Faults{};
    f.protect_fail = 1;
    arm(f);
    rows = shutdown_rows("restore_protect_failed");
    check(occl::patched() && one_row(rows, "status=restore_failed found=00000000 registered=1") && g.writes == 0, "restore_protect_failed_registered", rows.c_str());
    arm();
    rows = shutdown_rows("restore_after_failures");
    check(!occl::patched() && one_row(rows, "status=restored found=00000000 registered=0") && page_is(engine, reference, original) &&
          protection(engine) == PAGE_EXECUTE_READ, "restore_after_failures_c9000000", rows.c_str());
    b = branch("engine", "after_restore_after_failures", engine);
    check(vanilla(b), "engine_after_restore_after_failures_placeholder");
    // Restore when the span already holds c9 00 00 00 (no write), and when the first read fails (restore_not_owned,
    // nothing written, still registered; the next shutdown restores).
    arm();
    check(initialize_checked("install_2") && poke(engine + write_at, original, 4), "setup_install_then_external_restore");
    arm();
    rows = shutdown_rows("restore_already_original");
    check(!occl::patched() && one_row(rows, "status=restored found=c9000000 registered=0") && g.writes == 0 && g.protects == 0, "restore_already_original_no_write", rows.c_str());
    arm();
    check(initialize_checked("install_3"), "setup_install_3");
    f = Faults{};
    f.read_fail = 1;
    arm(f);
    rows = shutdown_rows("restore_unreadable");
    check(occl::patched() && one_row(rows, "status=restore_not_owned found=-- registered=1") && g.writes == 0 && g.protects == 0 &&
          page_is(engine, reference, patched), "restore_unreadable_found_unread_no_write", rows.c_str());
    arm();
    rows = shutdown_rows("restore_after_unreadable");
    check(!occl::patched() && one_row(rows, "status=restored found=00000000 registered=0") && page_is(engine, reference, original), "restore_after_unreadable_c9000000", rows.c_str());

    // ---- FlushInstructionCache: the same install and restore without the flush (measured, not required) ----
    for (int i = 0; i < 64; ++i) run(engine, true);
    f = Faults{};
    f.skip_flush = true;
    arm(f);
    check(initialize_checked("install_no_flush") && span_is(engine, patched), "install_no_flush_bytes_00000000");
    b = branch("engine", "after_patch_no_flush", engine);
    const int noflush_patch = b.lod1;
    ::FlushInstructionCache(GetCurrentProcess(), engine + write_at, 4);
    b = branch("engine", "after_patch_flushed", engine);
    check(patched_path(b), "engine_after_patch_flushed_lod0_path");
    for (int i = 0; i < 64; ++i) run(engine, true);
    arm(f);
    rows = shutdown_rows("restore_no_flush");
    check(!occl::patched() && one_row(rows, "status=restored found=00000000 registered=0") && span_is(engine, original), "restore_no_flush_bytes_c9000000", rows.c_str());
    b = branch("engine", "after_restore_no_flush", engine);
    const int noflush_restore = b.lod1;
    ::FlushInstructionCache(GetCurrentProcess(), engine + write_at, 4);
    b = branch("engine", "after_restore_flushed", engine);
    check(vanilla(b), "engine_after_restore_flushed_placeholder");
    std::printf("FLUSH patch_without_flush_lod1=%d (1 = new rel32 executed) restore_without_flush_lod1=%d (2 = old rel32 executed)\n", noflush_patch, noflush_restore);
    // The same without any protection change either: an already PAGE_EXECUTE_READWRITE page, run hot,
    // then engine_patch::write_code alone (no VirtualProtect, no flush) -- does the emulator see the store?
    auto* const hot = static_cast<unsigned char*>(VirtualAlloc(nullptr, page_size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    check(hot != nullptr, "hot_page_allocated");
    if (hot) {
        std::memcpy(hot, reference, page_size);
        ::FlushInstructionCache(GetCurrentProcess(), hot, page_size);
        for (int i = 0; i < 64; ++i) run(hot, true);
        const std::uintptr_t hot_span = reinterpret_cast<std::uintptr_t>(hot) + write_at;
        const bool atomic_patch = engine_patch::write_code(hot_span, patched, 4);
        const int raw_patch = branch("hot", "after_raw_patch_no_protect_no_flush", hot).lod1;
        ::FlushInstructionCache(GetCurrentProcess(), hot, page_size);
        b = branch("hot", "after_raw_patch_flushed", hot);
        check(atomic_patch && patched_path(b), "hot_after_raw_patch_flushed_lod0_path");
        for (int i = 0; i < 64; ++i) run(hot, true);
        const bool atomic_restore = engine_patch::write_code(hot_span, original, 4);
        const int raw_restore = branch("hot", "after_raw_restore_no_protect_no_flush", hot).lod1;
        ::FlushInstructionCache(GetCurrentProcess(), hot, page_size);
        b = branch("hot", "after_raw_restore_flushed", hot);
        check(atomic_restore && vanilla(b), "hot_after_raw_restore_flushed_placeholder");
        std::printf("FLUSH_RAW patch_lod1=%d restore_lod1=%d\n", raw_patch, raw_restore);
        VirtualFree(hot, 0, MEM_RELEASE);
    }

    // ---- the private page (MEM_PRIVATE, PAGE_EXECUTE_READ): install_at + shutdown ----
    arm();
    check(occl::install_at(reinterpret_cast<std::uintptr_t>(priv + window_offset)) && occl::write_path() == std::string("atomic") && span_is(priv, patched) &&
          page_is(priv, reference, patched), "private_install_ok_atomic_readback");
    std::printf("PROTECT memory=private step=install previous=0x%lx during=0x%lx after=0x%lx\n", g.first_previous, g.first_during, protection(priv));
    VirtualQuery(priv, &mi, sizeof mi);
    std::printf("MEMORY memory=private_after_patch type=0x%lx protect=0x%lx allocation_protect=0x%lx\n", mi.Type, mi.Protect, mi.AllocationProtect);
    check(g.first_previous == PAGE_EXECUTE_READ && g.first_during == PAGE_EXECUTE_READWRITE && protection(priv) == PAGE_EXECUTE_READ && mi.Type == MEM_PRIVATE,
          "private_protection_restored");
    b = branch("private", "after_patch", priv);
    check(patched_path(b), "private_after_patch_lod0_path");
    arm();
    rows = shutdown_rows("private_restore");
    check(!occl::patched() && one_row(rows, "status=restored found=00000000 registered=0") && page_is(priv, reference, original) && protection(priv) == PAGE_EXECUTE_READ,
          "private_restore_c9000000", rows.c_str());
    b = branch("private", "after_restore", priv);
    check(vanilla(b), "private_after_restore_placeholder");

    // ---- a real protect failure: an executable read-only view whose protection cannot be raised ----
    // (not executed: under this Wine a jump into a pagefile-backed FILE_MAP_EXECUTE view faults although
    // VirtualQuery reports PAGE_EXECUTE_READ; the check here is the refused protection and untouched bytes)
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, 0, page_size, nullptr);
    auto* writer = static_cast<unsigned char*>(section ? MapViewOfFile(section, FILE_MAP_WRITE, 0, 0, page_size) : nullptr);
    auto* roview = static_cast<unsigned char*>(section ? MapViewOfFile(section, FILE_MAP_READ | FILE_MAP_EXECUTE, 0, 0, page_size) : nullptr);
    check(writer && roview, "roview_mapped");
    if (writer && roview) {
        std::memcpy(writer, reference, page_size);
        ::FlushInstructionCache(GetCurrentProcess(), roview, page_size);
        DWORD previous = 0;
        const BOOL raised = ::VirtualProtect(roview + write_at, 4, PAGE_EXECUTE_READWRITE, &previous);
        const DWORD error = raised ? 0 : GetLastError();
        std::printf("ROVIEW protect=0x%lx raise=%d error=%lu\n", protection(roview), raised ? 1 : 0, error);
        if (raised) ::VirtualProtect(roview + write_at, 4, previous, &previous);
        check(!raised, "roview_raise_refused_by_os");
        if (!raised) {
            arm();
            check(!occl::install_at(reinterpret_cast<std::uintptr_t>(roview + window_offset)) && !occl::patched() && occl::state() == std::string("protect_failed") &&
                  occl::write_path() == std::string("none") && g.writes == 0 && page_is(roview, reference, original), "roview_install_protect_failed");
        }
    }

    // ---- late window: refused, nothing touched ----
    engine_patch::close_install_window("fixture");
    arm();
    r = initialize_checked("late");
    check(!r && last_log() == install_row("refused", "late_claim", "all", "all", "none") && g.protects == 0 && g.writes == 0 &&
          page_is(engine, reference, original), "late_initialize_refused", last_log().c_str());
    arm();
    check(!occl::install_at(reinterpret_cast<std::uintptr_t>(priv + window_offset)) && occl::state() == std::string("late_claim") && g.protects == 0 &&
          page_is(priv, reference, original), "late_install_at_refused");
    b = branch("engine", "after_late", engine);
    check(vanilla(b), "engine_after_late_placeholder");

    std::printf("RESULT checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
