// Wine fixture of the Terran-station LOD patch's real write path
// (src/proxy/terran_station_lod.cpp, compiled unchanged; see
// terran_lod_patch_fixture_shim.h for the pass-through fault seam).
//
// Memory under test, each holding the same executable stub with the verified
// 17-byte window (terran_lod_sites.h expected_window) at page offset 0x012, so
// the site sits at page offset 0x01c, offset 4 of its aligned 8-byte word,
// exactly as 0x0047d01c does:
//   engine   the image section .x3mlod of this executable, linked at the
//            engine's own page 0x0047d000 (MEM_IMAGE, the loader's
//            PAGE_EXECUTE_READ, as X3AP.exe's .text), driven through
//            initialize(), which installs at the production constant window_va;
//   private  a VirtualAlloc page (MEM_PRIVATE, PAGE_EXECUTE_READ), driven
//            through install_at();
//   roview   an executable read-only view of a pagefile section, whose
//            protection cannot be raised: a real VirtualProtect failure.
// The stub is cdecl int(const void* node): it runs the window's
// `test [edi+0x12c],0x80000000; je +5; mov byte [esp+0x18],1` and returns the
// flag byte, so a node with bit 31 set returns 1 while `je` is in place (not
// taken, flag stored) and 0 once it is `jmp` (store skipped); a node without
// the bit returns 0 either way. Every step checks the returned branch, the
// site bytes read back (ReadProcessMemory), the rest of the page, the page
// protection, the call counts on the seam, the log rows and LastError.
// Never launches the game.
#include "../../src/proxy/terran_station_lod.h"
#include "../../src/proxy/terran_lod_sites.h"
#include "../../src/proxy/engine_patch.h"
#include "terran_lod_patch_fixture_shim.h" // declarations only: X3M_TERRAN_LOD_SHIM is not defined here
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace lod = x3m::terran_station_lod;
namespace sites = x3m::terran_station_lod::sites;
namespace engine_patch = x3m::engine_patch;

// ---- the fault seam (declared in terran_lod_patch_fixture_shim.h) ----
namespace {
struct Faults {
    unsigned protect_fail = 0;  // bit i: the i-th VirtualProtect since arm() fails (ERROR_ACCESS_DENIED)
    unsigned read_fail = 0;     // bit i: the i-th read_code fails
    unsigned write_drop = 0;    // bit i: the i-th write_code stores nothing (and reports the atomic path)
    unsigned write_corrupt = 0; // bit i: the i-th write_code stores eb 06 instead of the requested bytes
    bool skip_flush = false;    // FlushInstructionCache returns TRUE without flushing
    unsigned protects = 0, reads = 0, writes = 0, flushes = 0, atomic_writes = 0;
    DWORD first_previous = 0, first_during = 0; // what the first VirtualProtect returned / VirtualQuery saw after it
};
Faults g;
void arm(const Faults& f = Faults{}) {
    g = f;
}
bool bit(unsigned mask, unsigned i) {
    return i < 32 && ((mask >> i) & 1u);
}
}
namespace x3m::terran_lod_fixture {
BOOL WINAPI virtual_protect(LPVOID address, SIZE_T size, DWORD protection, PDWORD previous) {
    const unsigned i = g.protects++;
    if (bit(g.protect_fail, i)) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
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
    if (bit(g.write_corrupt, i) && n == 2) {
        corrupt[0] = 0xeb;
        corrupt[1] = 0x06;
        bytes = corrupt;
    }
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
HANDLE log_handle() noexcept {
    return restore_log;
}
}
namespace x3m::object_trace {
bool executable_verified() {
    return executable_ok;
}
}

// ---- the stub: its own image section .x3mlod, linked at 0x0047d000 (MEM_IMAGE), window at +0x12 = 0x0047d012 ----
extern "C" unsigned char terran_engine_page[];
asm(R"(
    .section .x3mlod,"xr"
    .balign 4096, 0xcc
    .globl _terran_engine_page
_terran_engine_page:
    .byte 0x57                                   # +00 push edi
    .byte 0x83,0xec,0x1c                         # +01 sub esp,0x1c
    .byte 0x8b,0x7c,0x24,0x24                    # +04 mov edi,[esp+0x24]      ; node
    .byte 0xc6,0x44,0x24,0x18,0x00               # +08 mov byte [esp+0x18],0   ; the flag slot, as the engine's frame
    .byte 0x90,0x90,0x90,0x90,0x90               # +0d nop x5
    .byte 0xf7,0x87,0x2c,0x01,0x00,0x00,0x00,0x00,0x00,0x80  # +12 test dword [edi+0x12c],0x80000000  <- window
    .byte 0x74,0x05                              # +1c je +5   <- site (eb 05 once patched)
    .byte 0xc6,0x44,0x24,0x18,0x01               # +1e mov byte [esp+0x18],1
    .byte 0x8b,0x87,0x2c,0x01,0x00,0x00          # +23 mov eax,[edi+0x12c]     ; the engine's next instruction
    .byte 0x0f,0xb6,0x44,0x24,0x18               # +29 movzx eax,byte [esp+0x18]
    .byte 0x83,0xc4,0x1c                         # +2e add esp,0x1c
    .byte 0x5f                                   # +31 pop edi
    .byte 0xc3                                   # +32 ret
    .balign 4096, 0xcc
    .text
)");

namespace {
constexpr unsigned page_size = 4096, window_offset = 0x12;
constexpr std::uintptr_t engine_page = sites::window_va & ~std::uintptr_t(page_size - 1);
static_assert(engine_page + window_offset == sites::window_va, "the stub's window sits at the engine's page offset");
using Stub = int (*)(const void* node);

unsigned checks = 0, failures = 0;
void check(bool ok, const char* name, const char* detail = "") {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s%s%s\n", ok ? "pass" : "FAIL", name, !ok && *detail ? " " : "", ok ? "" : detail);
}
alignas(16) unsigned char node_flag[0x200], node_clear[0x200];
int run(const unsigned char* page, bool flag31) {
    return reinterpret_cast<Stub>(reinterpret_cast<std::uintptr_t>(page))(flag31 ? node_flag : node_clear);
}
// One BRANCH row per executed step: flag31 (1 = je not taken, 0 = jump taken) and the control node.
struct Branch {
    int flag31, clear;
};
Branch branch(const char* memory, const char* step, const unsigned char* page) {
    Branch b{run(page, true), run(page, false)};
    std::printf("BRANCH memory=%s step=%s flag31=%d clear=%d\n", memory, step, b.flag31, b.clear);
    return b;
}
DWORD protection(const void* at) {
    MEMORY_BASIC_INFORMATION m{};
    return VirtualQuery(at, &m, sizeof m) ? m.Protect : 0;
}
bool read_site(const unsigned char* page, unsigned char out[2]) {
    SIZE_T n = 0;
    return ReadProcessMemory(GetCurrentProcess(), page + window_offset + sites::site_offset, out, 2, &n) && n == 2;
}
bool site_is(const unsigned char* page, const unsigned char want[2]) {
    unsigned char now[2]{};
    return read_site(page, now) && !std::memcmp(now, want, 2);
}
// The protection a PAGE_EXECUTE_READWRITE request leaves on an image page: this Wine reports
// PAGE_EXECUTE_WRITECOPY before and after the store (measured); on Windows the page is expected
// (inferred, not verified) to read PAGE_EXECUTE_READWRITE once the store has made a private copy.
bool writable_image(DWORD protect) {
    return protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
}
// The page equals `reference` except for the two site bytes, which must be `site`.
bool page_is(const unsigned char* page, const unsigned char* reference, const unsigned char site[2]) {
    unsigned char now[page_size];
    SIZE_T n = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), page, now, page_size, &n) || n != page_size) return false;
    const unsigned s = window_offset + sites::site_offset;
    return !std::memcmp(now, reference, s) && !std::memcmp(now + s, site, 2) &&
           !std::memcmp(now + s + 2, reference + s + 2, page_size - s - 2);
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
void set_mode(const wchar_t* value) {
    SetEnvironmentVariableW(L"X3M_TERRAN_STATION_LOD", value);
}
// The last install row, and the restore rows written to the log handle since the previous call.
std::string last_log() {
    return log_lines.empty() ? std::string() : log_lines.back();
}
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
bool contains(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}
bool one_row(const std::string& rows, const char* needle) {
    std::size_t lines = 0;
    for (char c : rows) lines += c == '\n';
    return lines == 1 && contains(rows, needle);
}
std::string install_row(const char* status, const char* reason, const char* mode, const char* setting,
                        const char* write) {
    char text[200];
    std::snprintf(text, sizeof text, "terran_station_lod site=%08lx status=%s reason=%s mode=%s setting=%s write=%s",
                  static_cast<unsigned long>(sites::site_va), status, reason, mode, setting, write);
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
    const bool r = lod::initialize();
    char label[96];
    std::snprintf(label, sizeof label, "%s_lasterror_preserved", name);
    check(GetLastError() == 0x2bad, label);
    return r;
}
bool shutdown_checked(const char* name) {
    SetLastError(0x2bad);
    const bool r = lod::shutdown();
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
    std::printf("CRASH code=%08lx address=%p checks=%u\n", e->ExceptionRecord->ExceptionCode,
                e->ExceptionRecord->ExceptionAddress, checks);
    std::printf("RESULT checks=%u failures=%u\n", checks + 1, failures + 1);
    std::fflush(stdout);
    ExitProcess(3);
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // the msvcrt treats _IOLBF as full buffering
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(unhandled);
    reinterpret_cast<std::uint32_t&>(node_flag[sites::flags_offset]) = sites::root_flag;
    reinterpret_cast<std::uint32_t&>(node_clear[sites::flags_offset]) = 0x7fffffffu;
    const unsigned char* const original = sites::expected_site;
    const unsigned char* const patched = sites::patched_site;
    char temp_dir[MAX_PATH]{}, path[MAX_PATH + 64]{};
    GetTempPathA(MAX_PATH, temp_dir);
    std::snprintf(path, sizeof path, "%sx3m-terran-lod-patch-%lu.log", temp_dir, GetCurrentProcessId());
    restore_log = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    check(restore_log != INVALID_HANDLE_VALUE, "restore_log_opened");

    // ---- the pages ----
    unsigned char* const engine = terran_engine_page;
    check(reinterpret_cast<std::uintptr_t>(engine) == engine_page, "engine_page_at_engine_va");
    if (reinterpret_cast<std::uintptr_t>(engine) != engine_page) {
        std::printf("RESULT checks=%u failures=%u\n", checks, failures);
        return 1;
    }
    check(reinterpret_cast<std::uintptr_t>(engine) + window_offset == sites::window_va &&
              !std::memcmp(engine + window_offset, sites::expected_window, sites::window_length),
          "engine_window_at_window_va_is_expected_window");
    check(((reinterpret_cast<std::uintptr_t>(engine) + window_offset + sites::site_offset) & 7u) == 4u,
          "engine_site_offset_4_of_its_qword");
    unsigned char reference[page_size];
    std::memcpy(reference, engine, page_size);

    auto* const priv = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, page_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    check(priv != nullptr, "private_page_allocated");
    if (!priv) {
        std::printf("RESULT checks=%u failures=%u\n", checks, failures);
        return 1;
    }
    std::memcpy(priv, reference, page_size);
    std::memcpy(priv + window_offset, sites::expected_window, sites::window_length);
    DWORD old = 0;
    check(::VirtualProtect(priv, page_size, PAGE_EXECUTE_READ, &old) &&
              ::FlushInstructionCache(GetCurrentProcess(), priv, page_size),
          "private_page_execute_read");
    char detail[160];

    MEMORY_BASIC_INFORMATION mi{};
    VirtualQuery(engine, &mi, sizeof mi);
    std::printf("MEMORY memory=engine type=0x%lx protect=0x%lx allocation_protect=0x%lx\n", mi.Type, mi.Protect,
                mi.AllocationProtect);
    check(mi.Type == MEM_IMAGE && mi.Protect == PAGE_EXECUTE_READ, "engine_page_is_mem_image_execute_read");
    VirtualQuery(priv, &mi, sizeof mi);
    std::printf("MEMORY memory=private type=0x%lx protect=0x%lx allocation_protect=0x%lx\n", mi.Type, mi.Protect,
                mi.AllocationProtect);
    check(mi.Type == MEM_PRIVATE && mi.Protect == PAGE_EXECUTE_READ, "private_page_is_mem_private_execute_read");

    Branch b = branch("engine", "before", engine);
    check(b.flag31 == 1 && b.clear == 0, "engine_before_je");
    b = branch("private", "before", priv);
    check(b.flag31 == 1 && b.clear == 0, "private_before_je");

    // ---- refusals through initialize(): nothing is protected, written or flushed ----
    struct Refusal {
        const wchar_t* setting;
        bool exe;
        const char* name;
        const char* row;
    };
    const std::string distance_row = install_row("off", "distance", "distance", "distance", "none");
    const std::string invalid_row = install_row("refused", "invalid_setting", "-", "Size", "none");
    const std::string long_row = install_row("refused", "too_long", "-", "?", "none");
    const std::string exe_row = install_row("refused", "executable_mismatch", "size", "-", "none");
    const Refusal refusals[] = {{L"distance", true, "refuse_distance", distance_row.c_str()},
                                {L"Size", true, "refuse_invalid_setting", invalid_row.c_str()},
                                {L"sizesizesizesizesizesizesizesizesize", true, "refuse_too_long", long_row.c_str()},
                                {nullptr, false, "refuse_executable_mismatch", exe_row.c_str()}};
    for (const Refusal& r : refusals) {
        set_mode(r.setting);
        executable_ok = r.exe;
        arm();
        const bool applied = initialize_checked(r.name);
        char name[96];
        std::snprintf(name, sizeof name, "%s_row_and_untouched", r.name);
        check(!applied && !lod::patched() && last_log() == r.row && g.protects == 0 && g.writes == 0 &&
                  g.flushes == 0 && page_is(engine, reference, original),
              name, last_log().c_str());
    }
    set_mode(nullptr);
    executable_ok = true;

    // A changed window byte (the imm32's top byte) and an already patched, unregistered window: bytes_mismatch.
    const unsigned char changed = 0x40, restored_byte = 0x80;
    check(poke(engine + window_offset + 9, &changed, 1), "setup_changed_window_byte");
    unsigned char changed_reference[page_size];
    std::memcpy(changed_reference, reference, page_size);
    changed_reference[window_offset + 9] = changed;
    arm();
    bool r = initialize_checked("refuse_changed_window");
    check(!r && lod::state() == std::string("bytes_mismatch") && g.protects == 0 && g.writes == 0 &&
              page_is(engine, changed_reference, original) &&
              last_log() == install_row("refused", "bytes_mismatch", "size", "-", "none"),
          "refuse_changed_window_untouched", last_log().c_str());
    check(poke(engine + window_offset + 9, &restored_byte, 1), "setup_restore_window_byte");
    check(poke(engine + window_offset + sites::site_offset, patched, 2), "setup_foreign_patch");
    arm();
    check(!initialize_checked("refuse_already_patched") && lod::state() == std::string("bytes_mismatch") &&
              g.protects == 0 && g.writes == 0 && page_is(engine, reference, patched),
          "refuse_already_patched_window_untouched");
    check(poke(engine + window_offset + sites::site_offset, original, 2) && page_is(engine, reference, original),
          "setup_foreign_patch_removed");

    // Injected protect failure: protect_failed, nothing written, branch unchanged.
    Faults f;
    f.protect_fail = 1;
    arm(f);
    r = initialize_checked("refuse_protect_failed");
    check(!r && !lod::patched() && g.writes == 0 && g.flushes == 0 && page_is(engine, reference, original) &&
              protection(engine) == PAGE_EXECUTE_READ &&
              last_log() == install_row("refused", "protect_failed", "size", "-", "none"),
          "refuse_protect_failed_untouched", last_log().c_str());
    b = branch("engine", "after_protect_failed", engine);
    check(b.flag31 == 1 && b.clear == 0, "engine_after_protect_failed_je");

    // ---- the production path at the engine's VA: initialize() -> patch, read-back, execute, restore ----
    LARGE_INTEGER t0, t1, t2, t3;
    arm();
    QueryPerformanceCounter(&t0);
    const bool applied = initialize_checked("install");
    QueryPerformanceCounter(&t1);
    check(applied && lod::patched() && lod::state() == std::string("ok") &&
              lod::write_path() == std::string("atomic") &&
              last_log() == install_row("patched", "ok", "size", "-", "atomic"),
          "install_ok_atomic_row", last_log().c_str());
    std::snprintf(detail, sizeof detail,
                  "protects=%u reads=%u writes=%u atomic=%u flushes=%u previous=0x%lx during=0x%lx", g.protects,
                  g.reads, g.writes, g.atomic_writes, g.flushes, g.first_previous, g.first_during);
    check(g.protects == 2 && g.reads == 2 && g.writes == 1 && g.atomic_writes == 1 && g.flushes == 1 &&
              g.first_previous == PAGE_EXECUTE_READ && writable_image(g.first_during),
          "install_sequence_counts", detail);
    std::printf("PROTECT memory=engine step=install previous=0x%lx during=0x%lx after=0x%lx\n", g.first_previous,
                g.first_during, protection(engine));
    check(site_is(engine, patched) && page_is(engine, reference, patched),
          "install_readback_eb05_rest_of_page_unchanged");
    check(protection(engine) == PAGE_EXECUTE_READ, "install_protection_restored");
    b = branch("engine", "after_patch", engine);
    check(b.flag31 == 0 && b.clear == 0, "engine_after_patch_jmp");
    arm();
    check(!lod::install_at(sites::window_va) && lod::patched() && lod::state() == std::string("already_installed") &&
              g.protects == 0 && g.writes == 0 && site_is(engine, patched),
          "second_install_refused_already_installed");
    check(initialize_checked("initialize_again") && lod::patched() && g.writes == 0,
          "initialize_again_no_second_write");
    arm();
    QueryPerformanceCounter(&t2);
    const bool clean = shutdown_checked("restore");
    QueryPerformanceCounter(&t3);
    std::string rows = new_restore_rows();
    check(clean && !lod::patched() && lod::state() == std::string("restored") &&
              one_row(rows, "terran_station_lod_restore site=0047d01c status=restored found=eb05 registered=0"),
          "restore_row", rows.c_str());
    check(g.protects == 2 && g.writes == 1 && g.atomic_writes == 1 && g.flushes == 1 && site_is(engine, original) &&
              page_is(engine, reference, original) && protection(engine) == PAGE_EXECUTE_READ,
          "restore_readback_7405_protection");
    b = branch("engine", "after_restore", engine);
    check(b.flag31 == 1 && b.clear == 0, "engine_after_restore_je");
    check(shutdown_checked("restore_again") && new_restore_rows().empty(), "restore_again_no_row");
    std::printf("TIMING install_us=%.1f restore_us=%.1f\n", qpc_us(t0, t1), qpc_us(t2, t3));

    // ---- rollback paths (injected on the seam; every store and read is real) ----
    // Read-back fails after the real eb 05 store: rolled back, judged by reading 74 05.
    f = Faults{};
    f.read_fail = 2;
    arm(f);
    r = initialize_checked("rollback_readback");
    check(!r && !lod::patched() && lod::state() == std::string("patch_rolled_back") &&
              last_log() == install_row("refused", "patch_rolled_back", "size", "-", "atomic") && g.writes == 2 &&
              g.atomic_writes == 2 && page_is(engine, reference, original) && protection(engine) == PAGE_EXECUTE_READ,
          "rollback_readback_rolled_back", last_log().c_str());
    b = branch("engine", "after_rollback", engine);
    check(b.flag31 == 1 && b.clear == 0, "engine_after_rollback_je");
    // The store does not land: read-back mismatch, rolled back.
    f = Faults{};
    f.write_drop = 1;
    arm(f);
    check(!initialize_checked("rollback_dropped_write") && !lod::patched() &&
              lod::state() == std::string("patch_rolled_back") && page_is(engine, reference, original) &&
              protection(engine) == PAGE_EXECUTE_READ,
          "rollback_dropped_write_rolled_back");
    // Re-protect and the post-rollback protect fail: original bytes back, page left writable.
    f = Faults{};
    f.protect_fail = 2 | 4;
    arm(f);
    const bool unprotected = !initialize_checked("rollback_unprotected") && !lod::patched() &&
                             lod::state() == std::string("rollback_unprotected") &&
                             page_is(engine, reference, original);
    const DWORD left = protection(engine);
    std::snprintf(detail, sizeof detail, "protect=0x%lx", left);
    check(unprotected && writable_image(left), "rollback_unprotected_original_bytes_page_writable", detail);
    std::printf("PROTECT memory=engine step=rollback_unprotected left=0x%lx\n", left);
    b = branch("engine", "after_rollback_unprotected", engine);
    check(b.flag31 == 1 && b.clear == 0, "engine_after_rollback_unprotected_je");
    check(::VirtualProtect(engine, page_size, PAGE_EXECUTE_READ, &old) != FALSE, "setup_reprotect_after_unprotected");
    // A wrong store that cannot be undone: rollback_failed stays registered; restore fails while stores
    // are dropped or protect fails, then puts 74 05 back (restore_not_owned, found eb06).
    f = Faults{};
    f.write_corrupt = 1;
    f.write_drop = 2;
    arm(f);
    r = initialize_checked("rollback_failed");
    check(!r && lod::patched() && lod::state() == std::string("rollback_failed") &&
              last_log() == install_row("patched_unverified", "rollback_failed", "size", "-", "atomic") &&
              site_is(engine, reinterpret_cast<const unsigned char*>("\xeb\x06")) &&
              protection(engine) == PAGE_EXECUTE_READ,
          "rollback_failed_registered", last_log().c_str());
    std::printf(
        "BRANCH memory=engine step=after_rollback_failed flag31=- clear=- (eb 06 not executed: mid-instruction target)\n");
    f = Faults{};
    f.write_drop = 1;
    arm(f);
    rows = shutdown_rows("restore_dropped");
    check(lod::patched() && lod::state() == std::string("restore_failed") &&
              one_row(rows, "status=restore_failed found=eb06 registered=1") &&
              site_is(engine, reinterpret_cast<const unsigned char*>("\xeb\x06")),
          "restore_dropped_write_failed_registered", rows.c_str());
    f = Faults{};
    f.protect_fail = 1;
    arm(f);
    rows = shutdown_rows("restore_protect_failed");
    check(lod::patched() && one_row(rows, "status=restore_failed found=eb06 registered=1") && g.writes == 0,
          "restore_protect_failed_registered", rows.c_str());
    arm();
    rows = shutdown_rows("restore_not_owned");
    check(!lod::patched() && one_row(rows, "status=restore_not_owned found=eb06 registered=0") &&
              page_is(engine, reference, original) && protection(engine) == PAGE_EXECUTE_READ,
          "restore_not_owned_7405", rows.c_str());
    b = branch("engine", "after_restore_not_owned", engine);
    check(b.flag31 == 1 && b.clear == 0, "engine_after_restore_not_owned_je");
    // Restore when the site already holds 74 05 (no write), and when the first read fails (74 05 written, found --).
    arm();
    check(initialize_checked("install_2") && poke(engine + window_offset + sites::site_offset, original, 2),
          "setup_install_then_external_restore");
    arm();
    rows = shutdown_rows("restore_already_original");
    check(!lod::patched() && one_row(rows, "status=restored found=7405 registered=0") && g.writes == 0 &&
              g.protects == 0,
          "restore_already_original_no_write", rows.c_str());
    arm();
    check(initialize_checked("install_3"), "setup_install_3");
    f = Faults{};
    f.read_fail = 1;
    arm(f);
    rows = shutdown_rows("restore_unreadable");
    check(!lod::patched() && one_row(rows, "status=restore_not_owned found=-- registered=0") &&
              page_is(engine, reference, original),
          "restore_unreadable_found_unread", rows.c_str());

    // ---- FlushInstructionCache: the same install and restore without the flush (measured, not required) ----
    for (int i = 0; i < 64; ++i) run(engine, true);
    f = Faults{};
    f.skip_flush = true;
    arm(f);
    check(initialize_checked("install_no_flush") && site_is(engine, patched), "install_no_flush_bytes_eb05");
    b = branch("engine", "after_patch_no_flush", engine);
    const int noflush_patch = b.flag31;
    ::FlushInstructionCache(GetCurrentProcess(), engine + window_offset + sites::site_offset, 2);
    b = branch("engine", "after_patch_flushed", engine);
    check(b.flag31 == 0 && b.clear == 0, "engine_after_patch_flushed_jmp");
    for (int i = 0; i < 64; ++i) run(engine, true);
    arm(f);
    rows = shutdown_rows("restore_no_flush");
    check(!lod::patched() && one_row(rows, "status=restored found=eb05 registered=0") && site_is(engine, original),
          "restore_no_flush_bytes_7405", rows.c_str());
    b = branch("engine", "after_restore_no_flush", engine);
    const int noflush_restore = b.flag31;
    ::FlushInstructionCache(GetCurrentProcess(), engine + window_offset + sites::site_offset, 2);
    b = branch("engine", "after_restore_flushed", engine);
    check(b.flag31 == 1 && b.clear == 0, "engine_after_restore_flushed_je");
    std::printf(
        "FLUSH patch_without_flush_flag31=%d (0 = new jmp executed) restore_without_flush_flag31=%d (1 = je executed)\n",
        noflush_patch, noflush_restore);
    // The same without any protection change either: an already PAGE_EXECUTE_READWRITE page, run hot,
    // then engine_patch::write_code alone (no VirtualProtect, no flush) -- does the emulator see the store?
    auto* const hot = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, page_size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    check(hot != nullptr, "hot_page_allocated");
    if (hot) {
        std::memcpy(hot, reference, page_size);
        ::FlushInstructionCache(GetCurrentProcess(), hot, page_size);
        for (int i = 0; i < 64; ++i) run(hot, true);
        const std::uintptr_t hot_site = reinterpret_cast<std::uintptr_t>(hot) + window_offset + sites::site_offset;
        const bool atomic_patch = engine_patch::write_code(hot_site, patched, 2);
        const int raw_patch = branch("hot", "after_raw_patch_no_protect_no_flush", hot).flag31;
        ::FlushInstructionCache(GetCurrentProcess(), hot, page_size);
        b = branch("hot", "after_raw_patch_flushed", hot);
        check(atomic_patch && b.flag31 == 0 && b.clear == 0, "hot_after_raw_patch_flushed_jmp");
        for (int i = 0; i < 64; ++i) run(hot, true);
        const bool atomic_restore = engine_patch::write_code(hot_site, original, 2);
        const int raw_restore = branch("hot", "after_raw_restore_no_protect_no_flush", hot).flag31;
        ::FlushInstructionCache(GetCurrentProcess(), hot, page_size);
        b = branch("hot", "after_raw_restore_flushed", hot);
        check(atomic_restore && b.flag31 == 1 && b.clear == 0, "hot_after_raw_restore_flushed_je");
        std::printf("FLUSH_RAW patch_flag31=%d restore_flag31=%d\n", raw_patch, raw_restore);
        VirtualFree(hot, 0, MEM_RELEASE);
    }

    // ---- the private page (MEM_PRIVATE, PAGE_EXECUTE_READ): install_at + shutdown ----
    arm();
    check(lod::install_at(reinterpret_cast<std::uintptr_t>(priv + window_offset)) &&
              lod::write_path() == std::string("atomic") && site_is(priv, patched) && page_is(priv, reference, patched),
          "private_install_ok_atomic_readback");
    std::printf("PROTECT memory=private step=install previous=0x%lx during=0x%lx after=0x%lx\n", g.first_previous,
                g.first_during, protection(priv));
    VirtualQuery(priv, &mi, sizeof mi);
    std::printf("MEMORY memory=private_after_patch type=0x%lx protect=0x%lx allocation_protect=0x%lx\n", mi.Type,
                mi.Protect, mi.AllocationProtect);
    check(g.first_previous == PAGE_EXECUTE_READ && g.first_during == PAGE_EXECUTE_READWRITE &&
              protection(priv) == PAGE_EXECUTE_READ && mi.Type == MEM_PRIVATE,
          "private_protection_restored");
    b = branch("private", "after_patch", priv);
    check(b.flag31 == 0 && b.clear == 0, "private_after_patch_jmp");
    arm();
    rows = shutdown_rows("private_restore");
    check(!lod::patched() && one_row(rows, "status=restored found=eb05 registered=0") &&
              page_is(priv, reference, original) && protection(priv) == PAGE_EXECUTE_READ,
          "private_restore_7405", rows.c_str());
    b = branch("private", "after_restore", priv);
    check(b.flag31 == 1 && b.clear == 0, "private_after_restore_je");

    // ---- a real protect failure: an executable read-only view whose protection cannot be raised ----
    // (not executed: under this Wine a jump into a pagefile-backed FILE_MAP_EXECUTE view faults although
    // VirtualQuery reports PAGE_EXECUTE_READ; the check here is the refused protection and untouched bytes)
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, 0, page_size, nullptr);
    auto* writer = static_cast<unsigned char*>(section ? MapViewOfFile(section, FILE_MAP_WRITE, 0, 0, page_size)
                                                       : nullptr);
    auto* roview = static_cast<unsigned char*>(
        section ? MapViewOfFile(section, FILE_MAP_READ | FILE_MAP_EXECUTE, 0, 0, page_size) : nullptr);
    check(writer && roview, "roview_mapped");
    if (writer && roview) {
        std::memcpy(writer, reference, page_size);
        ::FlushInstructionCache(GetCurrentProcess(), roview, page_size);
        DWORD previous = 0;
        const BOOL raised = ::VirtualProtect(roview + window_offset + sites::site_offset, 2, PAGE_EXECUTE_READWRITE,
                                             &previous);
        const DWORD error = raised ? 0 : GetLastError();
        std::printf("ROVIEW protect=0x%lx raise=%d error=%lu\n", protection(roview), raised ? 1 : 0, error);
        if (raised) ::VirtualProtect(roview + window_offset + sites::site_offset, 2, previous, &previous);
        check(!raised, "roview_raise_refused_by_os");
        if (!raised) {
            arm();
            check(!lod::install_at(reinterpret_cast<std::uintptr_t>(roview + window_offset)) && !lod::patched() &&
                      lod::state() == std::string("protect_failed") && lod::write_path() == std::string("none") &&
                      g.writes == 0 && page_is(roview, reference, original),
                  "roview_install_protect_failed");
        }
    }

    // ---- late window: refused, nothing touched ----
    engine_patch::close_install_window("fixture");
    arm();
    r = initialize_checked("late");
    check(!r && last_log() == install_row("refused", "late_claim", "size", "-", "none") && g.protects == 0 &&
              g.writes == 0 && page_is(engine, reference, original),
          "late_initialize_refused", last_log().c_str());
    arm();
    check(!lod::install_at(reinterpret_cast<std::uintptr_t>(priv + window_offset)) &&
              lod::state() == std::string("late_claim") && g.protects == 0 && page_is(priv, reference, original),
          "late_install_at_refused");
    b = branch("engine", "after_late", engine);
    check(b.flag31 == 1 && b.clear == 0, "engine_after_late_je");

    std::printf("RESULT checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
