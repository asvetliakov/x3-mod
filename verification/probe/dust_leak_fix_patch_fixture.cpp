// Wine fixture of the dust-scene leak fix (src/proxy/dust_leak_fix.cpp,
// compiled unchanged; see dust_leak_fix_patch_fixture_shim.h for the
// pass-through fault seam) executing the engine's own loop-tail bytes.
//
// The image section .x3mdlf of this executable is linked at the engine's page
// 0x0041f000 (MEM_IMAGE, the loader's PAGE_EXECUTE_READ, as X3AP.exe's .text)
// and carries the engine's 37 window bytes 0x0041f4b7..0x0041f4db at their own
// addresses (copied by build_dust_leak_fix_patch.py from the installed
// X3AP.exe into the untracked dust_window_inc.h; copyrighted bytes are not
// tracked), the fixture's loop head at 0x0041f328 (the JNE's target: it hands
// the next scripted node to EDI and jumps to the site, standing in for the
// engine's allocation and validation) and its exit at 0x0041f4dc (the loop's
// fall-through: records EAX..ESP, [ESP+0x20], [ESP+0x18] and EFLAGS, restores
// the driver's stack and returns). A second section .x3mdlr at 0x00487000
// holds a jump at the engine's release address 0x00487be0 to the fixture's
// dlf_release (cdecl, as the engine's: records its argument and the registers
// it sees, unlinks the node from a list laid out like the render manager's
// R+0x28, zeroes the block and returns with EAX/ECX/EDX clobbered), so the
// production initialize() installs exactly what it installs in the game.
// Every case runs unpatched, after initialize() (unset = on) and after the
// restore: the loop must run to the count when every node
// attaches or is null, and end after the first never-attached node with that
// node released, [ESP+0x20] = 0, every other register as it was. Refusals,
// rollback paths (injected on the seam; every store and read is real), the
// hit counter and its report row, restore ownership, a real protect failure
// (a read-only view) and the late window are checked on the bytes, the
// protection, the arena and the log rows, with LastError preserved. Never
// launches the game.
#include "../../src/proxy/dust_leak_fix.h"
#include "../../src/proxy/dust_leak_fix_sites.h"
#include "../../src/proxy/engine_patch.h"
#include "dust_leak_fix_patch_fixture_shim.h" // declarations only: X3M_DUST_LEAK_FIX_SHIM is not defined here
#include "dust_window_inc.h"                  // generated, untracked: DLF_WINDOW_ASM (the engine's window bytes)
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace fix = x3m::dust_leak_fix;
namespace sites = x3m::dust_leak_fix::sites;
namespace engine_patch = x3m::engine_patch;

// ---- the fault seam (declared in dust_leak_fix_patch_fixture_shim.h) ----
namespace {
struct Faults {
    unsigned read_fail = 0;    // bit i: the module's i-th read_code since arm() fails
    unsigned store_fail = 0;   // bit i: the i-th store_pointer fails (nothing stored)
    unsigned restore_fail = 0; // bit i: the i-th restore fails as a refused VirtualProtect would (nothing written)
    unsigned reads = 0, stores = 0, restores = 0;
};
Faults g;
void arm(const Faults& f = Faults{}) {
    g = f;
}
bool bit(unsigned mask, unsigned i) {
    return i < 32 && ((mask >> i) & 1u);
}
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
    if (bit(g.restore_fail, g.restores++)) {
        site.status = "restore_protect_failed";
        return false;
    }
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
HANDLE log_handle() noexcept {
    return restore_log;
}
}
namespace x3m::object_trace {
bool executable_verified() {
    return executable_ok;
}
}

// ---- the engine page at 0x0041f000, the driver and the stand-in release ----
extern "C" {
extern unsigned char dlf_engine_page[], dlf_release_page[];
int dlf_run(unsigned remaining);
// eax, ecx, edx, ebx, esi, edi, ebp, esp, [esp+0x20], [esp+0x18], eflags at the exit
volatile std::uint32_t dlf_exit_regs[11];
volatile std::uint32_t dlf_saved_esp, dlf_entry_esp, dlf_iterations;
volatile std::uint32_t dlf_nodes[16]; // the node the loop head hands to EDI on iteration i (0 = allocation failed)
// ebx, esi, edi, ebp, esp as the stand-in release sees them
volatile std::uint32_t dlf_release_regs[5];
void dlf_release(); // cdecl(node) in asm: records, calls dlf_release_c, clobbers EAX/ECX/EDX
void dlf_release_c(std::uint32_t node);
}
asm(R"(
    .section .x3mdlf,"xr"
    .balign 4096, 0xcc
    .globl _dlf_engine_page
_dlf_engine_page:
    .fill 0x328, 1, 0xcc
_dlf_loop:                                     # 0x0041f328: the JNE's target (a relative jump: no base relocation in the page)
    jmp _dlf_loop_text
    .fill 0x4b7 - (. - _dlf_engine_page), 1, 0xcc
_dlf_window:                                   # 0x0041f4b7: the engine's window; entered at the site 0x0041f4d1 only
)" DLF_WINDOW_ASM R"(
_dlf_exit:                                     # 0x0041f4dc: the loop's fall-through
    jmp _dlf_exit_text
    .balign 4096, 0xcc
    .section .x3mdlr,"xr"
    .balign 4096, 0xcc
    .globl _dlf_release_page
_dlf_release_page:
    .fill 0xbe0, 1, 0xcc
    jmp _dlf_release                           # 0x00487be0: the engine's node release, standing in
    .balign 4096, 0xcc
    .text
_dlf_loop_text:
    pushl %eax
    movl _dlf_iterations, %eax
    cmpl $16, %eax
    jae 1f                                     # runaway guard: leave through the exit
    movl _dlf_nodes(,%eax,4), %edi
    incl _dlf_iterations
    popl %eax
    jmp _dlf_window + 0x1a                     # the site: SUB dword [ESP+0x20],1 (or the claim's jump)
1:  popl %eax
_dlf_exit_text:
    movl %eax, _dlf_exit_regs
    movl %ecx, _dlf_exit_regs+4
    movl %edx, _dlf_exit_regs+8
    movl %ebx, _dlf_exit_regs+12
    movl %esi, _dlf_exit_regs+16
    movl %edi, _dlf_exit_regs+20
    movl %ebp, _dlf_exit_regs+24
    movl %esp, _dlf_exit_regs+28
    movl 0x20(%esp), %eax
    movl %eax, _dlf_exit_regs+32
    movl 0x18(%esp), %eax
    movl %eax, _dlf_exit_regs+36
    pushfl
    popl %eax
    movl %eax, _dlf_exit_regs+40
    movl _dlf_saved_esp, %esp
    popl %ebp
    popl %edi
    popl %esi
    popl %ebx
    ret
    .globl _dlf_run
_dlf_run:
    pushl %ebx
    pushl %esi
    pushl %edi
    pushl %ebp
    movl 20(%esp), %eax
    movl %esp, _dlf_saved_esp
    subl $0x70, %esp                           # the engine's frame: 0x4c of locals below four pushes
    movl %eax, 0x20(%esp)                      # nodes still to fill
    movl $0, 0x18(%esp)
    movl $0x5a5a5a5a, 0x1c(%esp)
    movl $0x5a5a5a5a, 0x24(%esp)
    movl $0, _dlf_iterations
    movl %esp, _dlf_entry_esp
    movl $0xb1b1b1b1, %ebx
    movl $0x51515151, %esi
    movl $0xb0b0b0b0, %ebp
    movl $0xa0a0a0a0, %eax
    movl $0xc0c0c0c0, %ecx
    movl $0xd0d0d0d0, %edx
    movl $0xd1d1d1d1, %edi
    jmp _dlf_loop
    .globl _dlf_release
_dlf_release:                                  # cdecl(node), as 0x00487be0: callee-saved kept, EAX/ECX/EDX clobbered
    movl %ebx, _dlf_release_regs
    movl %esi, _dlf_release_regs+4
    movl %edi, _dlf_release_regs+8
    movl %ebp, _dlf_release_regs+12
    movl %esp, _dlf_release_regs+16
    movl 4(%esp), %eax
    pushl %eax
    call _dlf_release_c
    addl $4, %esp
    movl $0xdead0001, %eax
    movl $0xdead0002, %ecx
    movl $0xdead0003, %edx
    ret
)");

namespace {
constexpr unsigned page_size = 4096;
constexpr std::uintptr_t engine_page = sites::site_va & ~std::uintptr_t(page_size - 1);
constexpr unsigned window_offset = unsigned(sites::window_va - engine_page),
                   site_offset = unsigned(sites::site_va - engine_page);
constexpr std::uint32_t EAX0 = 0xa0a0a0a0, ECX0 = 0xc0c0c0c0, EDX0 = 0xd0d0d0d0, EBX0 = 0xb1b1b1b1, ESI0 = 0x51515151,
                        EBP0 = 0xb0b0b0b0;

unsigned checks = 0, failures = 0;
void check(bool ok, const char* name, const char* detail = "") {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s%s%s\n", ok ? "pass" : "FAIL", name, !ok && *detail ? " " : "", ok ? "" : detail);
}

// ---- the fixture's render manager list (R+0x28 next-of-head, R+0x2c terminator, R+0x30 last) and nodes ----
alignas(16) std::uint32_t manager[3];
alignas(16) unsigned char node_blocks[4][sites::node_size];
unsigned release_calls = 0;
std::uint32_t release_last = 0;
std::uint32_t rd(std::uint32_t at) {
    std::uint32_t v;
    std::memcpy(&v, reinterpret_cast<const void*>(at), 4);
    return v;
}
void wr(std::uint32_t at, std::uint32_t v) {
    std::memcpy(reinterpret_cast<void*>(at), &v, 4);
}
void list_reset() {
    manager[0] = std::uint32_t(reinterpret_cast<std::uintptr_t>(&manager[1]));
    manager[1] = 0;
    manager[2] = std::uint32_t(reinterpret_cast<std::uintptr_t>(&manager[0]));
}
// Exactly the constructor's append 0x00486e28..0x00486e38.
void list_append(std::uint32_t node) {
    const std::uint32_t r = std::uint32_t(reinterpret_cast<std::uintptr_t>(&manager[0]));
    wr(node + 4, manager[2]);
    wr(manager[2], node);
    wr(node, r + 4);
    manager[2] = node;
}
unsigned list_count(std::uint32_t needle, bool* found) {
    unsigned n = 0;
    *found = false;
    for (std::uint32_t at = manager[0]; rd(at) != 0 && n < 64; at = rd(at), ++n)
        if (at == needle) *found = true;
    return n;
}
std::uint32_t node_at(unsigned i) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(node_blocks[i]));
}
// A freshly constructed node as 0x00486d10 leaves it, appended to the list; `attached` sets +0x1c as 0x00489da0 does.
void make_node(unsigned i, bool attached) {
    std::memset(node_blocks[i], 0, sites::node_size);
    const std::uint32_t n = node_at(i);
    wr(n + 0x28, 0x1000 + i);
    wr(n + 0xc, n + 0x10); // empty child list
    wr(n + 0x18, n + 0xc);
    wr(n + 0x10, 0);
    wr(n + sites::model_offset, 0xffffffffu);
    list_append(n);
    if (attached) wr(n + sites::scene_offset, 0x5ce9e000);
}
}
extern "C" void dlf_release_c(std::uint32_t node) {
    ++release_calls;
    release_last = node;
    // 0x00487d55..0x00487d67: unlink from whatever list the node is on; then the block is zeroed and freed.
    const std::uint32_t next = rd(node), prev = rd(node + 4);
    wr(prev, next);
    wr(next + 4, prev);
    std::memset(reinterpret_cast<void*>(node), 0, sites::node_size);
}
namespace {

// ---- one loop execution and its expectation ----
struct Case {
    const char* name;
    unsigned remaining;
    char nodes[4]; // per iteration: 'f' never attached, 'a' attached, '0' allocation failed
};
const Case cases[] = {{"all_failed", 3, {'f', 'f', 'f'}},     {"all_attached", 3, {'a', 'a', 'a'}},
                      {"null_attached_failed", 3, {'0', 'a', 'f'}}, {"attached_failed_attached", 3, {'a', 'f', 'a'}},
                      {"one_failed", 1, {'f'}},                {"null_only", 2, {'0', '0'}}};
constexpr unsigned case_count = sizeof cases / sizeof cases[0];
struct Result {
    unsigned iterations, releases;
    std::uint32_t released, exit_edi, esp, slot, index, flags;
    bool regs_ok, released_unlinked, released_zeroed, others_listed;
};
Result execute(const Case& c) {
    list_reset();
    release_calls = 0;
    release_last = 0;
    for (unsigned i = 0; i < 16; ++i) dlf_nodes[i] = 0;
    for (unsigned i = 0; i < c.remaining; ++i) {
        if (c.nodes[i] == '0') continue;
        make_node(i, c.nodes[i] == 'a');
        dlf_nodes[i] = node_at(i);
    }
    dlf_run(c.remaining);
    Result r{};
    r.iterations = dlf_iterations;
    r.releases = release_calls;
    r.released = release_last;
    r.exit_edi = dlf_exit_regs[5];
    r.esp = dlf_exit_regs[7];
    r.slot = dlf_exit_regs[8];
    r.index = dlf_exit_regs[9];
    r.flags = dlf_exit_regs[10];
    r.regs_ok = dlf_exit_regs[0] == EAX0 && dlf_exit_regs[1] == ECX0 && dlf_exit_regs[2] == EDX0 &&
                dlf_exit_regs[3] == EBX0 && dlf_exit_regs[4] == ESI0 && dlf_exit_regs[6] == EBP0 &&
                r.esp == dlf_entry_esp && r.index == 0;
    bool found = false;
    if (r.releases) {
        list_count(r.released, &found);
        r.released_unlinked = !found;
        unsigned char zero[sites::node_size]{};
        r.released_zeroed = !std::memcmp(reinterpret_cast<const void*>(r.released), zero, sites::node_size);
    }
    r.others_listed = true;
    for (unsigned i = 0; i < c.remaining; ++i) {
        if (c.nodes[i] == '0' || node_at(i) == r.released) continue;
        if (i >= r.iterations) continue; // not handed out
        list_count(node_at(i), &found);
        r.others_listed = r.others_listed && found;
    }
    return r;
}
// The expectation: vanilla runs `remaining` iterations and releases nothing; patched ends at the first
// never-attached node and releases exactly it.
bool expected(const Case& c, bool patched, const Result& r, char* detail, unsigned size) {
    unsigned want_iterations = c.remaining, want_releases = 0;
    std::uint32_t want_released = 0;
    if (patched)
        for (unsigned i = 0; i < c.remaining; ++i)
            if (c.nodes[i] == 'f') {
                want_iterations = i + 1;
                want_releases = 1;
                want_released = node_at(i);
                break;
            }
    const std::uint32_t last = dlf_nodes[want_iterations - 1];
    const bool ok = r.iterations == want_iterations && r.releases == want_releases && r.released == want_released &&
                    r.exit_edi == last && r.slot == 0 && r.regs_ok && (r.flags & 0x40) &&
                    (!want_releases || (r.released_unlinked && r.released_zeroed)) && r.others_listed;
    std::snprintf(detail, size,
                  "iterations=%u/%u releases=%u/%u released=%08lx/%08lx edi=%08lx/%08lx slot=%lu regs=%d zf=%d "
                  "unlinked=%d zeroed=%d others=%d",
                  r.iterations, want_iterations, r.releases, want_releases, static_cast<unsigned long>(r.released),
                  static_cast<unsigned long>(want_released), static_cast<unsigned long>(r.exit_edi),
                  static_cast<unsigned long>(last), static_cast<unsigned long>(r.slot), r.regs_ok ? 1 : 0,
                  (r.flags & 0x40) ? 1 : 0, r.released_unlinked ? 1 : 0, r.released_zeroed ? 1 : 0,
                  r.others_listed ? 1 : 0);
    return ok;
}
struct Pass {
    unsigned ok = 0, detours = 0;
    Result results[case_count];
};
Pass run_all(const char* step, bool patched) {
    Pass p;
    char detail[300];
    for (unsigned i = 0; i < case_count; ++i) {
        const Result r = execute(cases[i]);
        const bool ok = expected(cases[i], patched, r, detail, sizeof detail);
        p.ok += ok;
        p.detours += r.releases;
        p.results[i] = r;
        std::printf("CASE step=%s case=%s ok=%d %s\n", step, cases[i].name, ok ? 1 : 0, detail);
    }
    return p;
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
// The chain the claim built at `site`: jmp dispatcher; dispatcher = jmp [entry]; entry = stub; stub = the 45 bytes
// with its three fields (the call reaches dlf_release, the counter and the slot follow the stub); *slot = the tail;
// tail = the displaced SUB + jmp 0x0041f4d6.
bool chain_ok(std::uintptr_t site, std::uintptr_t stub, char* detail, unsigned size) {
    unsigned char jump[5]{}, dispatcher[6]{}, stub_bytes[sites::stub_length]{}, tail[10]{},
        want[sites::stub_length]{};
    std::uint32_t entry = 0, head = 0, slot = 0, hits = 0, tail_at = 0, rel = 0;
    if (!read_mem(site, jump, 5) || jump[0] != 0xe9) {
        std::snprintf(detail, size, "jump");
        return false;
    }
    std::memcpy(&rel, jump + 1, 4);
    const std::uintptr_t disp = site + 5 + rel;
    const auto arena = reinterpret_cast<std::uintptr_t>(engine_patch::arena_base());
    if (disp < arena || disp >= arena + engine_patch::arena_capacity() || !read_mem(disp, dispatcher, 6) ||
        dispatcher[0] != 0xff || dispatcher[1] != 0x25) {
        std::snprintf(detail, size, "dispatcher %08lx", static_cast<unsigned long>(disp));
        return false;
    }
    std::memcpy(&entry, dispatcher + 2, 4);
    if (!read_mem(entry, &head, 4) || head != stub) {
        std::snprintf(detail, size, "head %08lx stub %08lx", static_cast<unsigned long>(head),
                      static_cast<unsigned long>(stub));
        return false;
    }
    if (!read_mem(stub, stub_bytes, sites::stub_length)) {
        std::snprintf(detail, size, "stub unreadable");
        return false;
    }
    std::memcpy(&slot, stub_bytes + sites::slot_abs32_offset, 4);
    std::memcpy(&hits, stub_bytes + sites::hits_abs32_offset, 4);
    sites::encode_stub(std::uint32_t(stub), std::uint32_t(sites::release_va), hits, slot, want);
    if (std::memcmp(stub_bytes, want, sites::stub_length)) {
        std::snprintf(detail, size, "stub bytes");
        return false;
    }
    if ((slot & 3) || hits != slot + 4 || slot < arena || hits + 4 > arena + engine_patch::arena_capacity() ||
        !read_mem(slot, &tail_at, 4) || !read_mem(tail_at, tail, 10) ||
        std::memcmp(tail, sites::expected_site, 5) || tail[5] != 0xe9) {
        std::snprintf(detail, size, "slot %08lx hits %08lx tail %08lx", static_cast<unsigned long>(slot),
                      static_cast<unsigned long>(hits), static_cast<unsigned long>(tail_at));
        return false;
    }
    std::memcpy(&rel, tail + 6, 4);
    if (tail_at + 10 + rel != sites::jne_va) {
        std::snprintf(detail, size, "tail jumps to %08lx", static_cast<unsigned long>(tail_at + 10 + rel));
        return false;
    }
    return true;
}
void set_mode(const wchar_t* value) {
    SetEnvironmentVariableW(L"X3M_DUST_LEAK_FIX", value);
}
std::string last_log() {
    return log_lines.empty() ? std::string() : log_lines.back();
}
std::string install_row(const char* status, const char* reason, const char* mode, const char* setting,
                        const char* write, std::uintptr_t stub = 0) {
    char text[220];
    std::snprintf(text, sizeof text,
                  "dust_leak_fix site=%08lx status=%s reason=%s mode=%s setting=%s write=%s stub=%08lx",
                  static_cast<unsigned long>(sites::site_va), status, reason, mode, setting, write,
                  static_cast<unsigned long>(stub));
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
bool install_here(std::uintptr_t window) {
    return fix::install_at(window, sites::release_va);
}

LONG WINAPI unhandled(EXCEPTION_POINTERS* e) {
    std::printf("CRASH code=%08lx address=%p checks=%u\n", e->ExceptionRecord->ExceptionCode,
                e->ExceptionRecord->ExceptionAddress, checks);
    std::printf("RESULT checks=%u failures=%u\n", checks + 1, failures + 1);
    std::fflush(stdout);
    ExitProcess(3);
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(unhandled);
    char temp_dir[MAX_PATH]{}, path[MAX_PATH + 64]{}, detail[300];
    GetTempPathA(MAX_PATH, temp_dir);
    std::snprintf(path, sizeof path, "%sx3m-dust-leak-fix-%lu.log", temp_dir, GetCurrentProcessId());
    restore_log = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    check(restore_log != INVALID_HANDLE_VALUE, "restore_log_opened");

    // ---- the page ----
    unsigned char* const engine = dlf_engine_page;
    const std::uintptr_t window = reinterpret_cast<std::uintptr_t>(engine) + window_offset,
                         site = window + sites::site_offset;
    check(reinterpret_cast<std::uintptr_t>(engine) == engine_page, "engine_page_at_engine_va");
    if (reinterpret_cast<std::uintptr_t>(engine) != engine_page) {
        std::printf("RESULT checks=%u failures=%u\n", checks, failures);
        return 1;
    }
    check(window == sites::window_va && site == sites::site_va &&
              !std::memcmp(engine + window_offset, sites::expected_window, sites::window_length),
          "engine_window_at_window_va_is_expected_window");
    check((site & 7u) == 1u, "site_offset_1_of_its_qword");
    MEMORY_BASIC_INFORMATION mi{};
    VirtualQuery(engine, &mi, sizeof mi);
    std::printf("MEMORY memory=engine type=0x%lx protect=0x%lx allocation_protect=0x%lx\n", mi.Type, mi.Protect,
                mi.AllocationProtect);
    check(mi.Type == MEM_IMAGE && mi.Protect == PAGE_EXECUTE_READ, "engine_page_is_mem_image_execute_read");
    check(reinterpret_cast<std::uintptr_t>(dlf_release_page) + 0xbe0 == sites::release_va &&
              dlf_release_page[0xbe0] == 0xe9,
          "release_stand_in_at_release_va");
    unsigned char reference[page_size];
    std::memcpy(reference, engine, page_size);

    // ---- vanilla: the engine's loop tail as it is ----
    const Pass vanilla = run_all("vanilla", false);
    check(vanilla.ok == case_count && vanilla.detours == 0, "vanilla_cases_loop_to_count_no_release");

    // ---- refusals through initialize(): nothing protected, written, emitted ----
    struct Refusal {
        const wchar_t* setting;
        bool exe;
        const char* name;
        std::string row;
    };
    const Refusal refusals[] = {
        {L"off", true, "explicit_off", install_row("off", "off", "off", "off", "none")},
        {L"On", true, "refuse_invalid_setting", install_row("refused", "invalid_setting", "-", "On", "none")},
        {L"onononononononononononononononon", true, "refuse_too_long",
         install_row("refused", "too_long", "-", "?", "none")},
        {nullptr, false, "refuse_executable_mismatch",
         install_row("refused", "executable_mismatch", "on", "-", "none")}};
    for (const Refusal& r : refusals) {
        set_mode(r.setting);
        executable_ok = r.exe;
        arm();
        const unsigned arena = engine_patch::arena_used();
        const bool applied = initialize_checked(r.name);
        char name[96];
        std::snprintf(name, sizeof name, "%s_row_and_untouched", r.name);
        check(!applied && !fix::patched() && last_log() == r.row && g.reads == 0 &&
                  engine_patch::arena_used() == arena && page_is(engine, reference, nullptr),
              name, last_log().c_str());
    }
    set_mode(nullptr); // unset = on
    executable_ok = true;
    // A changed window byte (the attach call's rel32) and a foreign jump at the site: bytes_mismatch.
    const unsigned char changed = 0xa9, original_byte = 0xa8;
    check(poke(engine + window_offset + 23, &changed, 1), "setup_changed_window_byte");
    arm();
    unsigned arena = engine_patch::arena_used();
    bool r = initialize_checked("refuse_changed_window");
    check(!r && fix::state() == std::string("bytes_mismatch") && engine_patch::arena_used() == arena &&
              last_log() == install_row("refused", "bytes_mismatch", "on", "-", "none"),
          "refuse_changed_window_untouched", last_log().c_str());
    check(poke(engine + window_offset + 23, &original_byte, 1) && page_is(engine, reference, nullptr),
          "setup_window_byte_back");
    const unsigned char foreign[5] = {0xe9, 0x10, 0x20, 0x30, 0x40};
    check(poke(engine + site_offset, foreign, 5), "setup_foreign_jump");
    arm();
    check(!initialize_checked("refuse_foreign_jump") && fix::state() == std::string("bytes_mismatch") &&
              page_is(engine, reference, foreign),
          "refuse_foreign_jump_untouched");
    check(poke(engine + site_offset, reference + site_offset, 5) && page_is(engine, reference, nullptr),
          "setup_foreign_jump_removed");
    arm();
    check(!fix::install_at(0, sites::release_va) && fix::state() == std::string("invalid_site") && g.reads == 0 &&
              !fix::install_at(window, 0) && fix::state() == std::string("invalid_site") && g.reads == 0,
          "invalid_site_refused_without_reads");

    // ---- the production path at the engine's VA: initialize() -> claim, stub, read-back, execute, restore ----
    LARGE_INTEGER t0, t1, t2, t3;
    arm();
    QueryPerformanceCounter(&t0);
    const bool applied = initialize_checked("install");
    QueryPerformanceCounter(&t1);
    const std::uintptr_t stub = fix::stub_address();
    check(applied && fix::patched() && stub && fix::state() == std::string("ok") &&
              fix::write_path() == std::string("atomic") &&
              last_log() == install_row("patched", "ok", "on", "-", "atomic", stub),
          "install_ok_atomic_row", last_log().c_str());
    unsigned char patched[5]{};
    check(read_mem(site, patched, 5) && patched[0] == 0xe9 && page_is(engine, reference, patched),
          "install_jump_rest_of_page_unchanged");
    check(protection(engine) == PAGE_EXECUTE_READ, "install_protection_restored");
    check(chain_ok(site, stub, detail, sizeof detail), "install_chain_jump_dispatcher_stub_slot_tail", detail);
    std::snprintf(detail, sizeof detail, "reads=%u stores=%u restores=%u", g.reads, g.stores, g.restores);
    check(g.reads == 2 && g.stores == 1 && g.restores == 0, "install_sequence_counts", detail);
    // initialize() on the live patch: the short cut, no second claim, no row.
    arm();
    const std::size_t rows_before = log_lines.size();
    check(initialize_checked("initialize_live") && fix::patched() && g.reads == 0 && log_lines.size() == rows_before &&
              page_is(engine, reference, patched),
          "initialize_live_no_second_claim");
    check(fix::hits() == 0, "hits_zero_before_execution");
    const Pass fixed = run_all("patched", true);
    check(fixed.ok == case_count, "patched_cases_release_once_and_leave");
    std::snprintf(detail, sizeof detail, "hits=%lu detours=%u", static_cast<unsigned long>(fix::hits()),
                  fixed.detours);
    check(fixed.detours == 4 && fix::hits() == fixed.detours, "hits_count_the_detours", detail);
    // Every case without a never-attached node is identical patched and unpatched (registers, ESP, flags, count).
    unsigned same = 0, expected_same = 0;
    for (unsigned i = 0; i < case_count; ++i) {
        const Result &a = vanilla.results[i], &b = fixed.results[i];
        if (b.releases) continue;
        ++expected_same;
        same += a.iterations == b.iterations && a.exit_edi == b.exit_edi && a.esp == b.esp && a.slot == b.slot &&
                a.flags == b.flags && a.regs_ok && b.regs_ok;
    }
    std::snprintf(detail, sizeof detail, "same=%u of %u", same, expected_same);
    check(expected_same == 2 && same == expected_same, "non_detour_cases_identical_patched_and_vanilla", detail);
    // The stand-in release saw the callee-saved registers and ESP the site had (three pushes + the argument + return).
    std::snprintf(detail, sizeof detail, "ebx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx entry=%08lx",
                  static_cast<unsigned long>(dlf_release_regs[0]), static_cast<unsigned long>(dlf_release_regs[1]),
                  static_cast<unsigned long>(dlf_release_regs[2]), static_cast<unsigned long>(dlf_release_regs[3]),
                  static_cast<unsigned long>(dlf_release_regs[4]), static_cast<unsigned long>(dlf_entry_esp));
    check(dlf_release_regs[0] == EBX0 && dlf_release_regs[1] == ESI0 && dlf_release_regs[3] == EBP0 &&
              dlf_release_regs[2] == fixed.results[case_count - 2].released &&
              dlf_release_regs[4] == dlf_entry_esp - 20,
          "release_sees_site_registers", detail);
    // The report row: hits since the previous row, the total, the frame; then a zero row.
    fix::report(300);
    char want[200];
    std::snprintf(want, sizeof want, "dust_leak_fix hits=%u total=%u frame=300", fixed.detours, fixed.detours);
    check(last_log() == want, "report_row_hits_total_frame", last_log().c_str());
    fix::report(600);
    std::snprintf(want, sizeof want, "dust_leak_fix hits=0 total=%u frame=600", fixed.detours);
    check(last_log() == want, "report_row_zero_after", last_log().c_str());
    arm();
    check(!install_here(window) && fix::patched() && fix::state() == std::string("already_installed") &&
              g.reads == 0,
          "second_install_refused_already_installed");
    arm();
    bool clean = false;
    QueryPerformanceCounter(&t2);
    std::string rows = shutdown_rows("restore", &clean);
    QueryPerformanceCounter(&t3);
    std::snprintf(want, sizeof want,
                  "dust_leak_fix_restore site=0041f4d1 status=restored found=%02x%02x%02x%02x%02x registered=0",
                  patched[0], patched[1], patched[2], patched[3], patched[4]);
    check(clean && !fix::patched() && fix::stub_address() == 0 && fix::hits() == 0 &&
              fix::state() == std::string("restored") && one_row(rows, want),
          "restore_row", rows.c_str());
    check(page_is(engine, reference, nullptr) && protection(engine) == PAGE_EXECUTE_READ,
          "restore_original_bytes_protection");
    Pass after = run_all("restored", false);
    check(after.ok == case_count && after.detours == 0, "restored_vanilla_again");
    check(shutdown_rows("restore_again", &clean).empty() && clean, "restore_again_no_row");
    std::printf("TIMING install_us=%.1f restore_us=%.1f\n", qpc_us(t0, t1), qpc_us(t2, t3));

    // ---- rollback paths (injected on the seam; the claim, its jump write and the restore are real) ----
    Faults f;
    f.store_fail = 1;
    arm(f);
    r = initialize_checked("rollback_chain");
    check(!r && !fix::patched() && fix::state() == std::string("chain_failed") &&
              last_log() == install_row("refused", "chain_failed", "on", "-", "atomic") && g.restores == 1 &&
              page_is(engine, reference, nullptr) && protection(engine) == PAGE_EXECUTE_READ,
          "rollback_chain_failed_original_bytes", last_log().c_str());
    f = Faults{};
    f.read_fail = 2; // the read-back after the claim
    arm(f);
    r = initialize_checked("rollback_readback");
    check(!r && !fix::patched() && fix::state() == std::string("readback_mismatch") &&
              last_log() == install_row("refused", "readback_mismatch", "on", "-", "atomic") && g.restores == 1 &&
              page_is(engine, reference, nullptr),
          "rollback_readback_original_bytes", last_log().c_str());
    after = run_all("after_rollbacks", false);
    check(after.ok == case_count && after.detours == 0, "after_rollbacks_vanilla");
    // The chain link fails and so does the take-back: registered, the jump live through the tail only (vanilla
    // behaviour); initialize() reports the failure without a second claim.
    f = Faults{};
    f.store_fail = 1;
    f.restore_fail = 1;
    arm(f);
    r = initialize_checked("rollback_failed");
    unsigned char live[5]{};
    check(!r && fix::patched() && fix::state() == std::string("rollback_failed") && fix::stub_address() == 0 &&
              last_log() == install_row("patched_unverified", "rollback_failed", "on", "-", "atomic") &&
              read_mem(site, live, 5) && live[0] == 0xe9,
          "rollback_failed_registered_jump_live", last_log().c_str());
    after = run_all("rollback_failed_tail_only", false);
    check(after.ok == case_count && after.detours == 0, "rollback_failed_tail_only_vanilla");
    arm();
    check(!initialize_checked("initialize_after_rollback_failed") && fix::patched() &&
              fix::state() == std::string("rollback_failed") && g.reads == 0 && page_is(engine, reference, live),
          "initialize_after_rollback_failed_reports_failure_no_second_claim");
    f = Faults{};
    f.restore_fail = 1;
    arm(f);
    std::snprintf(want, sizeof want, "status=restore_failed found=%02x%02x%02x%02x%02x registered=1", live[0],
                  live[1], live[2], live[3], live[4]);
    rows = shutdown_rows("restore_refused_protect", &clean);
    check(!clean && fix::patched() && one_row(rows, want) && page_is(engine, reference, live),
          "restore_failed_stays_registered", rows.c_str());
    arm();
    rows = shutdown_rows("restore_after_failure", &clean);
    check(clean && !fix::patched() && one_row(rows, "status=restored") && page_is(engine, reference, nullptr),
          "restore_after_failure_original_bytes", rows.c_str());

    // ---- restore ownership: foreign bytes are left alone; an external restore needs no write ----
    arm();
    check(initialize_checked("install_2") && read_mem(site, patched, 5), "setup_install_2");
    check(poke(engine + site_offset, foreign, 5), "setup_foreign_over_ours");
    arm();
    rows = shutdown_rows("restore_not_owned", &clean);
    check(!clean && fix::patched() && g.restores == 0 &&
              one_row(rows, "status=restore_not_owned found=e910203040 registered=1") &&
              page_is(engine, reference, foreign),
          "restore_not_owned_foreign_untouched_registered", rows.c_str());
    check(poke(engine + site_offset, patched, 5), "setup_ours_back");
    f = Faults{};
    f.read_fail = 1;
    arm(f);
    rows = shutdown_rows("restore_unreadable", &clean);
    check(!clean && fix::patched() && g.restores == 0 &&
              one_row(rows, "status=restore_not_owned found=-- registered=1") && page_is(engine, reference, patched),
          "restore_unreadable_no_write_registered", rows.c_str());
    arm();
    rows = shutdown_rows("restore_after_not_owned", &clean);
    check(clean && !fix::patched() && one_row(rows, "status=restored") && page_is(engine, reference, nullptr),
          "restore_after_not_owned_original_bytes", rows.c_str());
    arm();
    check(initialize_checked("install_3") && poke(engine + site_offset, reference + site_offset, 5),
          "setup_install_then_external_restore");
    arm();
    rows = shutdown_rows("restore_already_original", &clean);
    check(clean && !fix::patched() && g.restores == 0 &&
              one_row(rows, "status=restored found=836c242001 registered=0"),
          "restore_already_original_no_write", rows.c_str());

    // ---- a real protect failure: an executable read-only view whose protection cannot be raised (not executed) ----
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, 0, page_size, nullptr);
    auto* writer = static_cast<unsigned char*>(section ? MapViewOfFile(section, FILE_MAP_WRITE, 0, 0, page_size)
                                                       : nullptr);
    auto* roview = static_cast<unsigned char*>(
        section ? MapViewOfFile(section, FILE_MAP_READ | FILE_MAP_EXECUTE, 0, 0, page_size) : nullptr);
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
            check(!install_here(reinterpret_cast<std::uintptr_t>(roview) + window_offset) && !fix::patched() &&
                      fix::state() == std::string("protect_failed") && fix::write_path() == std::string("none") &&
                      page_is(roview, reference, nullptr),
                  "roview_install_protect_failed_untouched");
        }
    }

    // ---- late window: refused, nothing touched ----
    engine_patch::close_install_window("fixture");
    arm();
    arena = engine_patch::arena_used();
    r = initialize_checked("late");
    check(!r && last_log() == install_row("refused", "late_claim", "on", "-", "none") && g.reads == 0 &&
              engine_patch::arena_used() == arena && page_is(engine, reference, nullptr),
          "late_initialize_refused", last_log().c_str());
    after = run_all("after_late", false);
    check(after.ok == case_count && after.detours == 0, "after_late_vanilla");
    std::printf("ARENA used=%u capacity=%u\n", engine_patch::arena_used(), engine_patch::arena_capacity());
    std::printf("RESULT checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
