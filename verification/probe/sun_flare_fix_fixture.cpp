// Wine fixture of the lens-flare collector fix (src/proxy/sun_flare_fix.cpp,
// compiled unchanged; see sun_flare_fix_fixture_shim.h for the pass-through
// fault seam) executing the engine's own gate.
//
// The image section .x3msfc of this executable is linked at the engine's page
// 0x0047e000 (MEM_IMAGE, the loader's PAGE_EXECUTE_READ, as X3AP.exe's .text)
// and carries the engine's gate bytes 0x0047e315..0x0047e401 at their own
// addresses (copied by build_sun_flare_fix.py from the installed X3AP.exe into
// the untracked sun_gate_inc.h; copyrighted bytes are not tracked), a landing
// pad at 0x0047e402 (the on-screen path) and one at 0x0047e5b6 (the JGE
// target, off-screen). sun_run_gate builds the gate's ESP-relative locals
// ([ESP+0x14] = H, [ESP+0x18] = W, [ESP+0x1c] = tan(F/2) 16.16), sets EBX to
// a fake node (+0xa0 r, +0xf0 x, +0xf4 y, +0xf8 z), sentinels in ESI/EDI/EBP/
// ECX/EDX, and jumps to 0x0047e315; the pads record EAX..ESP and the two
// read-only locals, restore the saved ESP and return 1 (on) or 2 (off).
// Every vector runs unpatched, after the production initialize() and after the
// restore; the section 9.1 table plus a deterministic sweep are checked
// against a model of the engine arithmetic (vanilla and saturated), and
// every non-overflowing input must give identical registers patched and
// unpatched. Refusals, rollback paths (injected on the seam; every store and
// read is real), restore ownership, a real protect failure (a read-only view)
// and the late window are checked on the bytes, the protection, the arena and
// the log rows, with LastError preserved. Never launches the game.
#include "../../src/proxy/sun_flare_fix.h"
#include "../../src/proxy/sun_flare_fix_sites.h"
#include "../../src/proxy/engine_patch.h"
#include "sun_flare_fix_fixture_shim.h"  // declarations only: X3M_SUN_FLARE_FIX_SHIM is not defined here
#include "sun_gate_inc.h"                // generated, untracked: SUN_GATE_ASM (the engine's gate bytes)
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace fix = x3m::sun_flare_fix;
namespace sites = x3m::sun_flare_fix::sites;
namespace engine_patch = x3m::engine_patch;

// ---- the fault seam (declared in sun_flare_fix_fixture_shim.h) ----
namespace {
struct Faults {
    unsigned read_fail = 0;     // bit i: the module's i-th read_code since arm() fails
    unsigned store_fail = 0;    // bit i: the i-th store_pointer fails (nothing stored)
    unsigned restore_fail = 0;  // bit i: the i-th restore fails as a refused VirtualProtect would (nothing written)
    unsigned reads = 0, stores = 0, restores = 0;
};
Faults g;
void arm(const Faults& f = Faults{}) { g = f; }
bool bit(unsigned mask, unsigned i) { return i < 32 && ((mask >> i) & 1u); }
}
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count) {
    if (bit(g.read_fail, g.reads++)) return false;
    return read_code(address, out, count);
}
bool fixture_store_pointer(void** slot, void* value) {
    if (bit(g.store_fail, g.stores++)) return false;
    return store_pointer(slot, value);
}
bool fixture_restore(Site& site) {
    if (bit(g.restore_fail, g.restores++)) { site.status = "restore_protect_failed"; return false; }
    return restore(site);
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

// ---- the engine page at 0x0047e000 and the driver ----
struct GateIn { std::uint32_t w, h, tan; const void* node; };
extern "C" {
extern unsigned char sun_engine_page[];
int sun_run_gate(const GateIn* in);
// eax, ecx, edx, ebx, esi, edi, ebp, esp, [esp+0x14], [esp+0x18] at the pad
volatile std::uint32_t sun_exit[10];
volatile std::uint32_t sun_saved_esp, sun_entry_esp;
}
asm(R"(
    .section .x3msfc,"xr"
    .balign 4096, 0xcc
    .globl _sun_engine_page
_sun_engine_page:
    .fill 0x315, 1, 0xcc
)" SUN_GATE_ASM R"(
    .fill 0x402 - (. - _sun_engine_page), 1, 0xcc
_sun_pad_on:                                   # 0x0047e402: the on-screen path (a relative jump: no base relocation in the page)
    jmp _sun_pad_on_text
    .fill 0x5b6 - (. - _sun_engine_page), 1, 0xcc
_sun_pad_off:                                  # 0x0047e5b6: the JGE target (off-screen)
    jmp _sun_pad_off_text
    .balign 4096, 0xcc
    .text
_sun_pad_on_text:
    movl %eax, _sun_exit
    movl $1, %eax
    jmp _sun_pad_common
_sun_pad_off_text:
    movl %eax, _sun_exit
    movl $2, %eax
_sun_pad_common:
    movl %ecx, _sun_exit+4
    movl %edx, _sun_exit+8
    movl %ebx, _sun_exit+12
    movl %esi, _sun_exit+16
    movl %edi, _sun_exit+20
    movl %ebp, _sun_exit+24
    movl %esp, _sun_exit+28
    movl 0x14(%esp), %ecx
    movl %ecx, _sun_exit+32
    movl 0x18(%esp), %ecx
    movl %ecx, _sun_exit+36
    movl _sun_saved_esp, %esp
    popl %ebp
    popl %edi
    popl %esi
    popl %ebx
    ret
    .globl _sun_run_gate
_sun_run_gate:
    pushl %ebx
    pushl %esi
    pushl %edi
    pushl %ebp
    movl 20(%esp), %eax
    movl %esp, _sun_saved_esp
    subl $0x40, %esp
    movl 0(%eax), %ecx
    movl %ecx, 0x18(%esp)
    movl 4(%eax), %ecx
    movl %ecx, 0x14(%esp)
    movl 8(%eax), %ecx
    movl %ecx, 0x1c(%esp)
    movl $0x5a5a5a5a, 0x10(%esp)
    movl %esp, _sun_entry_esp
    movl 12(%eax), %ebx
    movl $0x51515151, %esi
    movl $0xd1d1d1d1, %edi
    movl $0xb0b0b0b0, %ebp
    movl $0xc0c0c0c0, %ecx
    movl $0xd0d0d0d0, %edx
    movl $0x0047e315, %eax
    jmp *%eax
)");

namespace {
constexpr unsigned page_size = 4096;
constexpr std::uintptr_t engine_page = sites::site_va & ~std::uintptr_t(page_size - 1);
constexpr unsigned window_offset = unsigned(sites::window_va - engine_page), site_offset = unsigned(sites::site_va - engine_page);
constexpr std::uint32_t H = 0xc000, W_16_9 = (0xc000u * 1920) / 1080, W_21_9 = (0xc000u * 2560) / 1080, W_32_9 = (0xc000u * 5120) / 1440, W_48_9 = (0xc000u * 48) / 9;

unsigned checks = 0, failures = 0;
void check(bool ok, const char* name, const char* detail = "") {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s%s%s\n", ok ? "pass" : "FAIL", name, !ok && *detail ? " " : "", ok ? "" : detail);
}

// ---- one gate execution and its model ----
struct Vector { const char* name; std::uint32_t w, tan; std::int32_t x, y, z, r; int vanilla, fixed; };  // vanilla/fixed: 1 on, 2 off, 0 = model only
struct Result { int pad; std::uint32_t eax, ecx; bool regs_ok; };
alignas(16) unsigned char node[0x200];
Result execute(const Vector& v) {
    std::memcpy(node + 0xa0, &v.r, 4); std::memcpy(node + 0xf0, &v.x, 4); std::memcpy(node + 0xf4, &v.y, 4); std::memcpy(node + 0xf8, &v.z, 4);
    const GateIn in{v.w, H, v.tan, node};
    SetLastError(0x5f1a);
    Result r{};
    r.pad = sun_run_gate(&in);
    r.eax = sun_exit[0]; r.ecx = sun_exit[1];
    r.regs_ok = sun_exit[3] == reinterpret_cast<std::uintptr_t>(node) && sun_exit[4] == 0x51515151u && sun_exit[5] == 0xd1d1d1d1u &&
                sun_exit[6] == 0xb0b0b0b0u && sun_exit[7] == sun_entry_esp && sun_exit[8] == H && sun_exit[9] == v.w && GetLastError() == 0x5f1a;
    return r;
}
std::int32_t half(std::int32_t v) { return v / 2; }  // cdq; sub eax,edx; sar eax,1
std::int32_t fixmul(std::int32_t a, std::int32_t b, bool fixed) {
    const std::int64_t p = std::int64_t(a) * b + 0x8000;
    return fixed ? sites::fixed_bound(p) : sites::vanilla_bound(p);
}
// The gate's exit (1 on, 2 off), EAX and ECX (ECX only when a coordinate test ran); overflow = the x bound wrapped.
struct Model { int pad; std::uint32_t eax, ecx; bool ecx_known, overflow; std::uint32_t y_bound; };
Model model(const Vector& v, bool fixed) {
    Model m{2, 0, 0, false, false, 0};
    if (!(v.z > 100) || !(2 * v.r < v.z)) return m;
    const std::int32_t by = fixmul(std::int32_t(H), fixmul(std::int32_t(v.tan), half(v.z), false), false);  // the vertical bound, independent of the fix
    m.y_bound = std::uint32_t(by);
    const std::int32_t t1 = fixmul(std::int32_t(v.tan), half(v.z), false);
    const std::int32_t bound = fixmul(std::int32_t(v.w), t1, fixed);
    m.overflow = fixmul(std::int32_t(v.w), t1, false) != fixmul(std::int32_t(v.w), t1, true);
    std::int32_t ecx = half(v.x); if (ecx < 0) ecx = -ecx;
    m.ecx = std::uint32_t(ecx); m.ecx_known = true; m.eax = std::uint32_t(bound);
    if (ecx >= bound) return m;
    ecx = half(v.y); if (ecx < 0) ecx = -ecx;
    m.ecx = std::uint32_t(ecx); m.eax = std::uint32_t(by);
    m.pad = ecx >= by ? 2 : 1;
    return m;
}
bool matches(const Result& r, const Model& m) { return r.regs_ok && r.pad == m.pad && (!m.ecx_known || (r.ecx == m.ecx && r.eax == m.eax)); }

std::int32_t scaled(double f, std::int32_t z, std::uint32_t w, std::uint32_t tan) {
    const double v = f * z * (w / 65536.0) * (tan / 65536.0);
    return v >= 2147483647.0 ? 2147483647 : v <= -2147483648.0 ? std::int32_t(-2147483647 - 1) : std::int32_t(v);
}
std::vector<Vector> table() {
    const std::uint32_t t471c = 78099, t4000 = 65536, t3470 = 49152;
    return {{"A_run309", W_32_9, t471c, 0, 0, 1500000000, 1000, 2, 1},
            {"B_below_zcrit", W_32_9, t471c, 0, 0, 1200000000, 1000, 1, 1},
            {"C_vanilla_bug", W_32_9, t4000, 0, 0, 1700000000, 1000, 2, 1},
            {"D_off_left", W_32_9, t471c, scaled(-1.05, 500000000, W_32_9, t471c), 0, 500000000, 1000, 2, 2},
            {"E_inside_edge", W_32_9, t471c, scaled(0.99, 500000000, W_32_9, t471c), 0, 500000000, 1000, 1, 1},
            {"E2_largest_x", W_32_9, t471c, 0x7fffffff, 0, 1500000000, 1000, 2, 1},
            {"F_off_top", W_32_9, t471c, 0, 1500000000, 1500000000, 1000, 2, 2},
            {"G_no_overflow", W_16_9, t3470, 0, 0, 2100000000, 1000, 1, 1},
            {"H_at_bound", W_32_9, t3470, 0, 0, 2147483000, 1000, 1, 1}};
}
// A deterministic sweep: wide and narrow planes, F from 0x3470 to 0x5000, z across the int32 range (half of them past 1e9),
// x inside, near and outside the bound, y inside or outside.
std::vector<Vector> sweep(unsigned n) {
    const std::uint32_t planes[] = {W_16_9, W_21_9, W_32_9, W_48_9}, tans[] = {49152, 55016, 65536, 78099, 90000};
    std::vector<Vector> out;
    std::uint32_t s = 0x13572468u;
    auto next = [&s]() { s = s * 1664525u + 1013904223u; return s; };
    for (unsigned i = 0; i < n; ++i) {
        Vector v{"sweep", planes[next() % 4], tans[next() % 5], 0, 0, 0, 1000, 0, 0};
        v.z = (next() & 1) ? std::int32_t(1000000000u + next() % 1147483647u) : std::int32_t(3000u + next() % 1000000000u);
        const unsigned kind = next() % 4;
        if (kind == 0) v.x = std::int32_t(next());
        else v.x = scaled((kind == 1 ? 0.5 : kind == 2 ? 0.999 : 1.01) * ((next() & 1) ? 1 : -1), v.z, v.w, v.tan);
        v.y = (next() % 3) ? std::int32_t(next() % 1000u) : std::int32_t(next());
        out.push_back(v);
    }
    return out;
}

// ---- memory helpers ----
DWORD protection(const void* at) {
    MEMORY_BASIC_INFORMATION m{};
    return VirtualQuery(at, &m, sizeof m) ? m.Protect : 0;
}
bool read_mem(std::uintptr_t at, void* out, unsigned n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(at), out, n, &got) && got == n;
}
// The page equals `reference` except for the five bytes at the site, which must be `span` (nullptr = the reference's).
bool page_is(const unsigned char* page, const unsigned char* reference, const unsigned char* span) {
    unsigned char now[page_size];
    if (!read_mem(reinterpret_cast<std::uintptr_t>(page), now, page_size)) return false;
    if (!span) return !std::memcmp(now, reference, page_size);
    return !std::memcmp(now, reference, site_offset) && !std::memcmp(now + site_offset, span, 5) &&
           !std::memcmp(now + site_offset + 5, reference + site_offset + 5, page_size - site_offset - 5);
}
bool poke(unsigned char* at, const unsigned char* bytes, unsigned n) {
    DWORD old = 0, unused = 0;
    if (!::VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(at, bytes, n);
    const bool ok = ::VirtualProtect(at, n, old, &unused) != FALSE;
    ::FlushInstructionCache(GetCurrentProcess(), at, n);
    return ok;
}
// The chain the claim built at `site`: jmp dispatcher; dispatcher = jmp [entry]; entry = stub; stub = the 20 bytes + slot;
// *slot = the tail; tail = the six displaced bytes + jmp 0x0047e397.
bool chain_ok(std::uintptr_t site, std::uintptr_t stub, char* detail, unsigned size) {
    unsigned char jump[6]{}, dispatcher[6]{}, stub_bytes[sites::stub_length]{}, tail[11]{};
    std::uint32_t entry = 0, head = 0, slot = 0, tail_at = 0, rel = 0;
    if (!read_mem(site, jump, 6) || jump[0] != 0xe9 || jump[5] != 0xc8) { std::snprintf(detail, size, "jump"); return false; }
    std::memcpy(&rel, jump + 1, 4);
    const std::uintptr_t disp = site + 5 + rel;
    const auto arena = reinterpret_cast<std::uintptr_t>(engine_patch::arena_base());
    if (disp < arena || disp >= arena + engine_patch::arena_capacity() || !read_mem(disp, dispatcher, 6) || dispatcher[0] != 0xff || dispatcher[1] != 0x25) {
        std::snprintf(detail, size, "dispatcher %08lx", static_cast<unsigned long>(disp)); return false;
    }
    std::memcpy(&entry, dispatcher + 2, 4);
    if (!read_mem(entry, &head, 4) || head != stub) { std::snprintf(detail, size, "head %08lx stub %08lx", static_cast<unsigned long>(head), static_cast<unsigned long>(stub)); return false; }
    if (!read_mem(stub, stub_bytes, sites::stub_length) || std::memcmp(stub_bytes, sites::stub_code, sites::stub_code_length)) { std::snprintf(detail, size, "stub bytes"); return false; }
    std::memcpy(&slot, stub_bytes + sites::stub_code_length, 4);
    if ((slot & 3) || !read_mem(slot, &tail_at, 4) || !read_mem(tail_at, tail, 11) || std::memcmp(tail, sites::expected_site, 6) || tail[6] != 0xe9) {
        std::snprintf(detail, size, "slot %08lx tail %08lx", static_cast<unsigned long>(slot), static_cast<unsigned long>(tail_at)); return false;
    }
    std::memcpy(&rel, tail + 7, 4);
    if (tail_at + 11 + rel != sites::jge_va) { std::snprintf(detail, size, "tail jumps to %08lx", static_cast<unsigned long>(tail_at + 11 + rel)); return false; }
    return true;
}
void set_mode(const wchar_t* value) { SetEnvironmentVariableW(L"X3M_SUN_FLARE_FIX", value); }
std::string last_log() { return log_lines.empty() ? std::string() : log_lines.back(); }
std::string install_row(const char* status, const char* reason, const char* mode, const char* setting, const char* write, std::uintptr_t stub = 0) {
    char text[220];
    std::snprintf(text, sizeof text, "sun_flare_fix site=%08lx status=%s reason=%s mode=%s setting=%s write=%s stub=%08lx",
                  static_cast<unsigned long>(sites::site_va), status, reason, mode, setting, write, static_cast<unsigned long>(stub));
    return text;
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
bool one_row(const std::string& rows, const char* needle) {
    std::size_t lines = 0;
    for (char c : rows) lines += c == '\n';
    return lines == 1 && rows.find(needle) != std::string::npos;
}
bool initialize_checked(const char* name) {
    SetLastError(0x2bad);
    const bool r = fix::initialize();
    char label[96];
    std::snprintf(label, sizeof label, "%s_lasterror_preserved", name);
    check(GetLastError() == 0x2bad, label);
    return r;
}
std::string shutdown_rows(const char* name, bool* result = nullptr) {
    SetLastError(0x2bad);
    const bool r = fix::shutdown();
    if (result) *result = r;
    char label[96];
    std::snprintf(label, sizeof label, "%s_lasterror_preserved", name);
    check(GetLastError() == 0x2bad, label);
    return new_restore_rows();
}
double qpc_us(LARGE_INTEGER a, LARGE_INTEGER b) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return double(b.QuadPart - a.QuadPart) * 1e6 / double(f.QuadPart);
}
// Runs the section 9.1 table (one VECTOR row each) and the sweep; `fixed` selects the expected column.
struct Pass { unsigned table_ok = 0, sweep_ok = 0, overflow = 0; std::vector<Result> sweep_results; };
Pass run_all(const char* step, bool fixed, const std::vector<Vector>& vectors) {
    Pass p;
    for (const Vector& v : table()) {
        const Result r = execute(v);
        const Model m = model(v, fixed);
        const int want = fixed ? v.fixed : v.vanilla;
        const bool ok = matches(r, m) && r.pad == want;
        p.table_ok += ok;
        std::printf("VECTOR step=%s case=%s pad=%d want=%d eax=%08lx ecx=%08lx regs=%d ok=%d\n", step, v.name, r.pad, want,
                    static_cast<unsigned long>(r.eax), static_cast<unsigned long>(r.ecx), r.regs_ok ? 1 : 0, ok ? 1 : 0);
    }
    for (const Vector& v : vectors) {
        const Result r = execute(v);
        const Model m = model(v, fixed);
        p.sweep_ok += matches(r, m);
        p.overflow += m.overflow;
        p.sweep_results.push_back(r);
    }
    std::printf("SWEEP step=%s vectors=%u model_matches=%u overflowing=%u\n", step, unsigned(vectors.size()), p.sweep_ok, p.overflow);
    return p;
}

LONG WINAPI unhandled(EXCEPTION_POINTERS* e) {
    std::printf("CRASH code=%08lx address=%p checks=%u\n", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress, checks);
    std::printf("RESULT checks=%u failures=%u\n", checks + 1, failures + 1);
    std::fflush(stdout);
    ExitProcess(3);
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(unhandled);
    char temp_dir[MAX_PATH]{}, path[MAX_PATH + 64]{}, detail[200];
    GetTempPathA(MAX_PATH, temp_dir);
    std::snprintf(path, sizeof path, "%sx3m-sun-flare-fix-%lu.log", temp_dir, GetCurrentProcessId());
    restore_log = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    check(restore_log != INVALID_HANDLE_VALUE, "restore_log_opened");

    // ---- the page ----
    unsigned char* const engine = sun_engine_page;
    const std::uintptr_t window = reinterpret_cast<std::uintptr_t>(engine) + window_offset, site = window + sites::site_offset;
    check(reinterpret_cast<std::uintptr_t>(engine) == engine_page, "engine_page_at_engine_va");
    if (reinterpret_cast<std::uintptr_t>(engine) != engine_page) { std::printf("RESULT checks=%u failures=%u\n", checks, failures); return 1; }
    check(window == sites::window_va && site == sites::site_va && !std::memcmp(engine + window_offset, sites::expected_window, sites::window_length),
          "engine_window_at_window_va_is_expected_window");
    check((site & 7u) == 1u, "site_offset_1_of_its_qword");
    MEMORY_BASIC_INFORMATION mi{};
    VirtualQuery(engine, &mi, sizeof mi);
    std::printf("MEMORY memory=engine type=0x%lx protect=0x%lx allocation_protect=0x%lx\n", mi.Type, mi.Protect, mi.AllocationProtect);
    check(mi.Type == MEM_IMAGE && mi.Protect == PAGE_EXECUTE_READ, "engine_page_is_mem_image_execute_read");
    unsigned char reference[page_size];
    std::memcpy(reference, engine, page_size);
    const std::vector<Vector> vectors = sweep(4096);

    // ---- vanilla: the engine's gate as it is ----
    const Pass vanilla = run_all("vanilla", false, vectors);
    check(vanilla.table_ok == table().size(), "vanilla_table_exits_and_registers");
    check(vanilla.sweep_ok == vectors.size(), "vanilla_sweep_matches_model");
    std::snprintf(detail, sizeof detail, "overflowing=%u", vanilla.overflow);
    check(vanilla.overflow > 100 && vanilla.overflow < vectors.size() - 100, "sweep_covers_both_regimes", detail);

    // ---- refusals through initialize(): nothing protected, written, emitted ----
    struct Refusal { const wchar_t* setting; bool exe; const char* name; std::string row; };
    const Refusal refusals[] = {{nullptr, true, "default_unset_off", install_row("off", "off", "off", "-", "none")},
                                {L"off", true, "explicit_off", install_row("off", "off", "off", "off", "none")},
                                {L"On", true, "refuse_invalid_setting", install_row("refused", "invalid_setting", "-", "On", "none")},
                                {L"onononononononononononononononon", true, "refuse_too_long", install_row("refused", "too_long", "-", "?", "none")},
                                {L"on", false, "refuse_executable_mismatch", install_row("refused", "executable_mismatch", "on", "on", "none")}};
    for (const Refusal& r : refusals) {
        set_mode(r.setting);
        executable_ok = r.exe;
        arm();
        const unsigned arena = engine_patch::arena_used();
        const bool applied = initialize_checked(r.name);
        char name[96];
        std::snprintf(name, sizeof name, "%s_row_and_untouched", r.name);
        check(!applied && !fix::patched() && last_log() == r.row && g.reads == 0 && engine_patch::arena_used() == arena && page_is(engine, reference, nullptr), name,
              last_log().c_str());
    }
    set_mode(L"on");
    executable_ok = true;
    // A changed window byte (the first FixMul's rounding constant) and a foreign jump at the site: bytes_mismatch.
    const unsigned char changed = 0x81, original_byte = 0x80;
    check(poke(engine + window_offset + 12, &changed, 1), "setup_changed_window_byte");
    arm();
    unsigned arena = engine_patch::arena_used();
    bool r = initialize_checked("refuse_changed_window");
    check(!r && fix::state() == std::string("bytes_mismatch") && engine_patch::arena_used() == arena &&
          last_log() == install_row("refused", "bytes_mismatch", "on", "on", "none"), "refuse_changed_window_untouched", last_log().c_str());
    check(poke(engine + window_offset + 12, &original_byte, 1) && page_is(engine, reference, nullptr), "setup_window_byte_back");
    const unsigned char foreign[5] = {0xe9, 0x10, 0x20, 0x30, 0x40};
    check(poke(engine + site_offset, foreign, 5), "setup_foreign_jump");
    arm();
    check(!initialize_checked("refuse_foreign_jump") && fix::state() == std::string("bytes_mismatch") && page_is(engine, reference, foreign), "refuse_foreign_jump_untouched");
    check(poke(engine + site_offset, reference + site_offset, 5) && page_is(engine, reference, nullptr), "setup_foreign_jump_removed");

    // ---- the production path at the engine's VA: initialize() -> claim, stub, read-back, execute, restore ----
    LARGE_INTEGER t0, t1, t2, t3;
    arm();
    QueryPerformanceCounter(&t0);
    const bool applied = initialize_checked("install");
    QueryPerformanceCounter(&t1);
    const std::uintptr_t stub = fix::stub_address();
    check(applied && fix::patched() && stub && fix::state() == std::string("ok") && fix::write_path() == std::string("atomic") &&
          last_log() == install_row("patched", "ok", "on", "on", "atomic", stub), "install_ok_atomic_row", last_log().c_str());
    unsigned char patched[6]{};
    check(read_mem(site, patched, 6) && patched[0] == 0xe9 && patched[5] == 0xc8 && page_is(engine, reference, patched), "install_jump_sixth_byte_kept_rest_of_page_unchanged");
    check(protection(engine) == PAGE_EXECUTE_READ, "install_protection_restored");
    check(chain_ok(site, stub, detail, sizeof detail), "install_chain_jump_dispatcher_stub_slot_tail", detail);
    std::snprintf(detail, sizeof detail, "reads=%u stores=%u restores=%u", g.reads, g.stores, g.restores);
    check(g.reads == 2 && g.stores == 1 && g.restores == 0, "install_sequence_counts", detail);
    const Pass fixed = run_all("patched", true, vectors);
    check(fixed.table_ok == table().size(), "patched_table_exits_and_registers");
    check(fixed.sweep_ok == vectors.size(), "patched_sweep_matches_saturating_model");
    // Past the x test = the exit's EAX is the vertical bound (independent of the fix): on, or off by y.
    unsigned same = 0, differ = 0, past_x = 0, vanilla_past_x = 0;
    for (unsigned i = 0; i < vectors.size(); ++i) {
        const Result& a = vanilla.sweep_results[i];
        const Result& b = fixed.sweep_results[i];
        const Model m = model(vectors[i], false);
        const bool identical = a.pad == b.pad && a.eax == b.eax && a.ecx == b.ecx;
        if (!m.overflow) { same += identical; continue; }
        differ += !identical;
        past_x += b.eax == m.y_bound;
        vanilla_past_x += a.eax == m.y_bound;
    }
    std::snprintf(detail, sizeof detail, "same=%u of %u non_overflowing past_x=%u of %u overflowing", same, unsigned(vectors.size()) - vanilla.overflow, past_x, vanilla.overflow);
    std::printf("COMPARE non_overflowing=%u identical=%u overflowing=%u changed=%u past_x_patched=%u past_x_vanilla=%u\n", unsigned(vectors.size()) - vanilla.overflow,
                same, vanilla.overflow, differ, past_x, vanilla_past_x);
    check(same == vectors.size() - vanilla.overflow, "non_overflowing_inputs_identical_patched_and_vanilla", detail);
    check(past_x == vanilla.overflow, "every_overflowing_input_passes_the_x_test_patched", detail);
    arm();
    check(!fix::install_at(sites::window_va) && fix::patched() && fix::state() == std::string("already_installed") && g.reads == 0, "second_install_refused_already_installed");
    check(initialize_checked("initialize_again") && fix::patched() && page_is(engine, reference, patched), "initialize_again_no_second_claim");
    arm();
    bool clean = false;
    QueryPerformanceCounter(&t2);
    std::string rows = shutdown_rows("restore", &clean);
    QueryPerformanceCounter(&t3);
    char want[160];
    std::snprintf(want, sizeof want, "sun_flare_fix_restore site=0047e391 status=restored found=%02x%02x%02x%02x%02x%02x registered=0",
                  patched[0], patched[1], patched[2], patched[3], patched[4], patched[5]);
    check(clean && !fix::patched() && fix::stub_address() == 0 && fix::state() == std::string("restored") && one_row(rows, want), "restore_row", rows.c_str());
    check(page_is(engine, reference, nullptr) && protection(engine) == PAGE_EXECUTE_READ, "restore_original_bytes_protection");
    Pass after = run_all("restored", false, vectors);
    check(after.table_ok == table().size() && after.sweep_ok == vectors.size(), "restored_vanilla_again");
    check(shutdown_rows("restore_again", &clean).empty() && clean, "restore_again_no_row");
    std::printf("TIMING install_us=%.1f restore_us=%.1f\n", qpc_us(t0, t1), qpc_us(t2, t3));

    // ---- rollback paths (injected on the seam; the claim, its jump write and the restore are real) ----
    Faults f;
    f.store_fail = 1;
    arm(f);
    r = initialize_checked("rollback_chain");
    check(!r && !fix::patched() && fix::state() == std::string("chain_failed") && last_log() == install_row("refused", "chain_failed", "on", "on", "atomic") &&
          g.restores == 1 && page_is(engine, reference, nullptr) && protection(engine) == PAGE_EXECUTE_READ, "rollback_chain_failed_original_bytes", last_log().c_str());
    f = Faults{};
    f.read_fail = 2;  // the read-back after the claim
    arm(f);
    r = initialize_checked("rollback_readback");
    check(!r && !fix::patched() && fix::state() == std::string("readback_mismatch") && last_log() == install_row("refused", "readback_mismatch", "on", "on", "atomic") &&
          g.restores == 1 && page_is(engine, reference, nullptr), "rollback_readback_original_bytes", last_log().c_str());
    after = run_all("after_rollbacks", false, vectors);
    check(after.table_ok == table().size() && after.sweep_ok == vectors.size(), "after_rollbacks_vanilla");
    // The chain link fails and so does the take-back: registered, the jump live through the tail only (vanilla behaviour).
    f = Faults{};
    f.store_fail = 1;
    f.restore_fail = 1;
    arm(f);
    r = initialize_checked("rollback_failed");
    unsigned char live[6]{};
    check(!r && fix::patched() && fix::state() == std::string("rollback_failed") && fix::stub_address() == 0 &&
          last_log() == install_row("patched_unverified", "rollback_failed", "on", "on", "atomic") && read_mem(site, live, 6) && live[0] == 0xe9 && live[5] == 0xc8,
          "rollback_failed_registered_jump_live", last_log().c_str());
    after = run_all("rollback_failed_tail_only", false, vectors);
    check(after.table_ok == table().size() && after.sweep_ok == vectors.size(), "rollback_failed_tail_only_vanilla");
    arm();
    check(!initialize_checked("initialize_after_rollback_failed") && fix::patched() && fix::state() == std::string("rollback_failed") && g.reads == 0 &&
          page_is(engine, reference, live), "initialize_after_rollback_failed_reports_failure_no_second_claim");
    f = Faults{};
    f.restore_fail = 1;
    arm(f);
    std::snprintf(want, sizeof want, "status=restore_failed found=%02x%02x%02x%02x%02x%02x registered=1", live[0], live[1], live[2], live[3], live[4], live[5]);
    rows = shutdown_rows("restore_refused_protect", &clean);
    check(!clean && fix::patched() && one_row(rows, want) && page_is(engine, reference, live), "restore_failed_stays_registered", rows.c_str());
    arm();
    rows = shutdown_rows("restore_after_failure", &clean);
    check(clean && !fix::patched() && one_row(rows, "status=restored") && page_is(engine, reference, nullptr), "restore_after_failure_original_bytes", rows.c_str());

    // ---- restore ownership: foreign bytes are left alone; an external restore needs no write ----
    arm();
    check(initialize_checked("install_2") && read_mem(site, patched, 6), "setup_install_2");
    check(poke(engine + site_offset, foreign, 5), "setup_foreign_over_ours");
    arm();
    rows = shutdown_rows("restore_not_owned", &clean);
    check(!clean && fix::patched() && g.restores == 0 && one_row(rows, "status=restore_not_owned found=e910203040c8 registered=1") && page_is(engine, reference, foreign),
          "restore_not_owned_foreign_untouched_registered", rows.c_str());
    check(poke(engine + site_offset, patched, 5), "setup_ours_back");
    f = Faults{};
    f.read_fail = 1;
    arm(f);
    rows = shutdown_rows("restore_unreadable", &clean);
    check(!clean && fix::patched() && g.restores == 0 && one_row(rows, "status=restore_not_owned found=-- registered=1") && page_is(engine, reference, patched),
          "restore_unreadable_no_write_registered", rows.c_str());
    arm();
    rows = shutdown_rows("restore_after_not_owned", &clean);
    check(clean && !fix::patched() && one_row(rows, "status=restored") && page_is(engine, reference, nullptr), "restore_after_not_owned_original_bytes", rows.c_str());
    arm();
    check(initialize_checked("install_3") && poke(engine + site_offset, reference + site_offset, 5), "setup_install_then_external_restore");
    arm();
    rows = shutdown_rows("restore_already_original", &clean);
    check(clean && !fix::patched() && g.restores == 0 && one_row(rows, "status=restored found=0facd0103bc8 registered=0"), "restore_already_original_no_write", rows.c_str());

    // ---- a real protect failure: an executable read-only view whose protection cannot be raised (not executed) ----
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, 0, page_size, nullptr);
    auto* writer = static_cast<unsigned char*>(section ? MapViewOfFile(section, FILE_MAP_WRITE, 0, 0, page_size) : nullptr);
    auto* roview = static_cast<unsigned char*>(section ? MapViewOfFile(section, FILE_MAP_READ | FILE_MAP_EXECUTE, 0, 0, page_size) : nullptr);
    check(writer && roview, "roview_mapped");
    if (writer && roview) {
        std::memcpy(writer, reference, page_size);
        DWORD previous = 0;
        const BOOL raised = ::VirtualProtect(roview + site_offset, 5, PAGE_EXECUTE_READWRITE, &previous);
        std::printf("ROVIEW protect=0x%lx raise=%d\n", protection(roview), raised ? 1 : 0);
        if (raised) ::VirtualProtect(roview + site_offset, 5, previous, &previous);
        check(!raised, "roview_raise_refused_by_os");
        if (!raised) {
            arm();
            check(!fix::install_at(reinterpret_cast<std::uintptr_t>(roview) + window_offset) && !fix::patched() && fix::state() == std::string("protect_failed") &&
                  fix::write_path() == std::string("none") && page_is(roview, reference, nullptr), "roview_install_protect_failed_untouched");
        }
    }

    // ---- late window: refused, nothing touched ----
    engine_patch::close_install_window("fixture");
    arm();
    arena = engine_patch::arena_used();
    r = initialize_checked("late");
    check(!r && last_log() == install_row("refused", "late_claim", "on", "on", "none") && g.reads == 0 && engine_patch::arena_used() == arena &&
          page_is(engine, reference, nullptr), "late_initialize_refused", last_log().c_str());
    after = run_all("after_late", false, vectors);
    check(after.table_ok == table().size() && after.sweep_ok == vectors.size(), "after_late_vanilla");
    std::printf("ARENA used=%u capacity=%u\n", engine_patch::arena_used(), engine_patch::arena_capacity());
    std::printf("RESULT checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
