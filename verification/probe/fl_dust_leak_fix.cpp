// Original synthetic fixture of the FL dust leak fix (src/engine/fl/dust_leak_fix_fl.cpp, dust_leak_fix_sites_fl.h;
// docs/reverse-engineering/fl/dust-light.md, "Port (2026-11-03)"): no game code, no game bytes beyond the eight windows
// in dust_leak_fix_sites_fl.h, no launch. Built with -DX3M_EDITION_FL=1 -DX3M_DUST_LEAK_FIX_FL_FIXTURE by
// verification/analysis/test_fl_dust_leak_fix.py (the module force-includes fl_dust_leak_fix_shim.h for the read-back /
// chain / restore faults) and run natively (WSL interop).
//
// A 64 KB-aligned private image stands in for X3FL.exe at `base`: the alloc, body and tail windows of the fill loop, the
// release entry and both release exits, the attach prefix and the manager clear (fixup = base+0x1aa100), every other
// byte INT3. While a run executes (never during an install) `push imm32; ret` pads stand at the loop head
// base+0x020950 and the loop exit base+0x020ace (they record the CPU state and which way the tail went) and right after
// the release's real prologue at base+0x0a330f (a recording fake that clobbers EAX/ECX/EDX/EBX/ESI/EDI/EBP, the flags,
// XMM7, the x87 control word, MXCSR and LastError, then jumps to the real exit bytes at base+0x0a359a, whose pops
// restore the callee-saved registers). The harness enters the tail base+0x020ac4 as the engine does: ESP at a 0x60-byte
// frame whose [ESP+0x18] is the target count, EBX the incremented fill count, ESI the node or 0, sentinels elsewhere,
// chosen flags, XMM7/x87 CW/MXCSR set and LastError set. A recorder mode redirects the stub's continuation slot to a
// recorder, which captures the state exactly as the tail would receive it (flags included).
//
//   ./fl_dust_leak_fix.exe          the full run (stock runs, initialize rows, refusals, flips, faults, install, runs,
//                                   recorder runs, report rows, shutdown cases, late_claim after everything is restored)
//   ./fl_dust_leak_fix.exe retain   install, close the window: the stub still releases, shutdown keeps the crossing
//                                   jump (restore_retained, registered=1)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../../src/engine/fl/dust_leak_fix_fl.h"
#include "../../src/engine/fl/dust_leak_fix_sites_fl.h"
#include "../../src/proxy/dust_leak_fix_sites.h"
#include "../../src/proxy/engine_patch.h"
namespace dlf = x3m::dust_leak_fix;
namespace S = x3m::fl::dust_leak_fix_fl;
namespace engine_patch = x3m::engine_patch;
using std::uint32_t;
unsigned checks = 0, failures = 0;
void check(bool value, const char* label, const char* detail = "") {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s %s\n", label, detail);
    }
}
char last_row[1024];
unsigned rows = 0;
HANDLE pipe_read = nullptr, pipe_write = nullptr;
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(last_row, sizeof last_row, format, args);
    va_end(args);
    ++rows;
    std::printf("LOG %s\n", last_row);
}
HANDLE log_handle() noexcept {
    return pipe_write;
}
}
// The shutdown row written to the log handle (the pipe), or "" when none.
char restore_row[256];
const char* take_restore_row() {
    restore_row[0] = 0;
    DWORD avail = 0, got = 0;
    if (PeekNamedPipe(pipe_read, nullptr, 0, nullptr, &avail, nullptr) && avail &&
        ReadFile(pipe_read, restore_row, avail < sizeof restore_row - 1 ? avail : sizeof restore_row - 1, &got, nullptr))
        restore_row[got] = 0;
    if (restore_row[0]) std::printf("ROW %s", restore_row);
    return restore_row;
}
// The seam (fl_dust_leak_fix_shim.h): one-shot faults, otherwise the real calls.
std::uintptr_t fault_read_at = 0;
unsigned fault_read_count = 0;
bool fault_store = false, fault_push = false, fault_restore = false;
unsigned faults_taken = 0;
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count) {
    const bool ok = read_code(address, out, count);
    if (ok && fault_read_at && address == fault_read_at && count == fault_read_count) {
        fault_read_at = 0;
        ++faults_taken;
        out[1] ^= 0xff;
    }
    return ok;
}
bool fixture_store_pointer(void** slot, void* value) {
    if (fault_store) {
        fault_store = false;
        ++faults_taken;
        return false;
    }
    return store_pointer(slot, value);
}
void* fixture_push_front(Site& site, void* stub) {
    if (fault_push) {
        fault_push = false;
        ++faults_taken;
        return nullptr;
    }
    return push_front(site, stub);
}
bool fixture_restore(Site& site) {
    if (fault_restore) { // as a refused VirtualProtect: the site stays registered
        fault_restore = false;
        ++faults_taken;
        site.status = "restore_protect_failed";
        return false;
    }
    return restore(site);
}
}

extern "C" {
uint32_t in_eax = 0, in_ebx = 0, in_ecx = 0, in_edx = 0, in_esi = 0, in_edi = 0, in_ebp = 0, in_flags = 0;
uint32_t in_frame[24], out_frame[24];
uint32_t site_entry = 0, release_exit = 0, saved_esp = 0, esp_before = 0, landing = 0;
uint32_t out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags;
uint32_t r_calls = 0, r_ecx = 0, r_ebp = 0, r_flags = 0, r_nodes[8], r_error_seen = 0;
alignas(16) unsigned char in_xmm[16], out_xmm[16];
uint16_t in_fcw = 0x027f, out_fcw = 0, def_fcw = 0, clobber_fcw = 0x0c7f;
uint32_t in_mxcsr = 0x1fa0, out_mxcsr = 0, def_mxcsr = 0, clobber_mxcsr = 0x7f80;
void fl_dust_enter();
void fl_dust_landing_loop();
void fl_dust_landing_exit();
void fl_dust_recorder();
void fl_dust_fake_release();
// The fake release's C side (called from the asm below after the real prologue): records the node, sees LastError,
// overwrites it.
void fl_dust_release_body() {
    if (r_calls < 8) r_nodes[r_calls] = r_ecx;
    ++r_calls;
    r_error_seen = GetLastError();
    SetLastError(0x0badf00d);
}
}
#define STORE_OUT                                                                                                      \
    "pushfl\n\tpopl _out_flags\n\tcld\n\tmovl %eax,_out_eax\n\tmovl %ebx,_out_ebx\n\tmovl %ecx,_out_ecx\n\t"          \
    "movl %edx,_out_edx\n\tmovl %esi,_out_esi\n\tmovl %edi,_out_edi\n\tmovl %ebp,_out_ebp\n\tmovl %esp,_out_esp\n\t"   \
    "movl %esp,%esi\n\tmovl $_out_frame,%edi\n\tmovl $24,%ecx\n\trep movsl\n\t"                                        \
    "movdqu %xmm7,_out_xmm\n\tfnstcw _out_fcw\n\tstmxcsr _out_mxcsr\n\tfldcw _def_fcw\n\tldmxcsr _def_mxcsr\n\t"       \
    "movl _saved_esp,%esp\n\tpopfl\n\tpopal\n\tret\n\t"
asm(".text\n\t"
    // The tail as the engine reaches it: the frame (count at +0x18), the registers, the flags; then jmp to the site.
    ".globl _fl_dust_enter\n_fl_dust_enter:\n\t"
    "pushal\n\tpushfl\n\tmovl %esp,_saved_esp\n\tfnstcw _def_fcw\n\tstmxcsr _def_mxcsr\n\t"
    "movdqu _in_xmm,%xmm7\n\tfldcw _in_fcw\n\tldmxcsr _in_mxcsr\n\t"
    "subl $0x60,%esp\n\tcld\n\tmovl %esp,%edi\n\tmovl $_in_frame,%esi\n\tmovl $24,%ecx\n\trep movsl\n\t"
    "movl _in_eax,%eax\n\tmovl _in_ebx,%ebx\n\tmovl _in_ecx,%ecx\n\tmovl _in_edx,%edx\n\tmovl _in_esi,%esi\n\t"
    "movl _in_edi,%edi\n\tmovl _in_ebp,%ebp\n\tmovl %esp,_esp_before\n\tpushl _in_flags\n\tpopfl\n\t"
    "jmp *_site_entry\n\t"
    ".globl _fl_dust_landing_loop\n_fl_dust_landing_loop:\n\tmovl $1,_landing\n\tjmp 1f\n\t"
    ".globl _fl_dust_landing_exit\n_fl_dust_landing_exit:\n\tmovl $2,_landing\n\tjmp 1f\n\t"
    ".globl _fl_dust_recorder\n_fl_dust_recorder:\n\tmovl $3,_landing\n"
    "1:\n\t" STORE_OUT
    // After the release's real prologue (EBP = ECX = node): record, clobber, leave through the real exit bytes.
    ".globl _fl_dust_fake_release\n_fl_dust_fake_release:\n\t"
    "movl %ebp,_r_ebp\n\tmovl %ecx,_r_ecx\n\tpushfl\n\tpopl _r_flags\n\tcall _fl_dust_release_body\n\t"
    "movl $0x11111111,%eax\n\tmovl $0x22222222,%ecx\n\tmovl $0x33333333,%edx\n\tmovl $0x44444444,%ebx\n\t"
    "movl $0x55555555,%esi\n\tmovl $0x66666666,%edi\n\tmovl $0x77777777,%ebp\n\t"
    "pcmpeqd %xmm7,%xmm7\n\tpxor %xmm0,%xmm0\n\tfldcw _clobber_fcw\n\tldmxcsr _clobber_mxcsr\n\t"
    "cmpl $0x7fffffff,%eax\n\tjmp *_release_exit\n\t");

std::uintptr_t base = 0;
unsigned char* image = nullptr;
unsigned char* pristine = nullptr;
constexpr unsigned image_size = 0xb0000;
constexpr uint32_t sentinel_error = 0x2a2a1357;
constexpr uint32_t flags_mask = 0xcd5; // CF PF AF ZF SF DF OF
constexpr uint32_t flags_set = 0x8d7, flags_set_df = 0xcd7;
alignas(16) unsigned char node_failed_a[S::node_size], node_failed_b[S::node_size], node_attached[S::node_size];
unsigned char scene[64];

uint32_t u32(const void* p) {
    return uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
uint32_t get32(const unsigned char* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}
void writable(bool on) {
    DWORD old = 0;
    VirtualProtect(image, image_size, on ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ, &old);
}
void put32(uint32_t rva, uint32_t v) {
    std::memcpy(image + rva, &v, 4);
}
void jump_at(uint32_t rva, void* to) { // push imm32; ret
    image[rva] = 0x68;
    put32(rva + 1, u32(to));
    image[rva + 5] = 0xc3;
}
void lay_out() {
    writable(true);
    std::memset(image, 0xcc, image_size);
    for (const S::Window& w : S::windows) {
        std::memcpy(image + w.rva, w.bytes, w.length);
        for (unsigned k = 0; k < w.fixups; ++k) put32(w.rva + w.fixup_offset[k], uint32_t(base + w.fixup_rva[k]));
    }
    writable(false);
    FlushInstructionCache(GetCurrentProcess(), image, image_size);
}
constexpr uint32_t pad_rvas[3] = {S::loop_rva, S::exit_rva, S::release_rva + S::release_length};
void pads(bool on) {
    writable(true);
    if (on) {
        jump_at(pad_rvas[0], reinterpret_cast<void*>(&fl_dust_landing_loop));
        jump_at(pad_rvas[1], reinterpret_cast<void*>(&fl_dust_landing_exit));
        jump_at(pad_rvas[2], reinterpret_cast<void*>(&fl_dust_fake_release));
    } else {
        for (uint32_t rva : pad_rvas) std::memcpy(image + rva, pristine + rva, 6);
    }
    writable(false);
    FlushInstructionCache(GetCurrentProcess(), image, image_size);
}
bool windows_original() {
    for (const S::Window& w : S::windows)
        if (std::memcmp(image + w.rva, pristine + w.rva, w.length)) return false;
    return true;
}
bool span_original() {
    return !std::memcmp(image + S::site_rva, S::expected_site, S::site_length);
}
bool only_span_changed() { // the image differs from the pristine copy only in the jump's five bytes
    for (unsigned i = 0; i < image_size; ++i)
        if (image[i] != pristine[i] && (i < S::site_rva || i >= S::site_rva + 5)) return false;
    return true;
}

struct Run {
    uint32_t landing, eax, ebx, ecx, edx, esi, edi, ebp, esp_delta, flags, error, calls;
    bool frame_kept, xmm_kept, fcw_kept, mxcsr_kept;
};
// One arrival at the tail with ESI = esi, EBX = ebx, [ESP+0x18] = count and the given flags.
Run arrive(uint32_t esi, uint32_t ebx, uint32_t count, uint32_t flags) {
    in_eax = 0xa0a0a0a0;
    in_ecx = 0xc1c1c1c1;
    in_edx = 0xd2d2d2d2;
    in_edi = 0xe3e3e3e3;
    in_ebp = 0xb5b5b5b5;
    in_esi = esi;
    in_ebx = ebx;
    in_flags = flags;
    for (unsigned k = 0; k < 24; ++k) in_frame[k] = 0xf0f00000u + k;
    in_frame[S::count_slot / 4] = count;
    for (unsigned k = 0; k < 16; ++k) in_xmm[k] = static_cast<unsigned char>(0x5a + 7 * k);
    site_entry = uint32_t(base + S::site_rva);
    release_exit = uint32_t(base + S::release_exit_rvas[0]);
    const uint32_t calls = r_calls;
    landing = 0;
    pads(true);
    SetLastError(sentinel_error);
    fl_dust_enter();
    const uint32_t error = GetLastError();
    pads(false);
    Run r{landing, out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, esp_before - out_esp,
          out_flags & flags_mask, error, r_calls - calls,
          !std::memcmp(in_frame, out_frame, sizeof in_frame), !std::memcmp(in_xmm, out_xmm, 16), out_fcw == in_fcw,
          out_mxcsr == in_mxcsr};
    return r;
}
// Every register but EBX as entered, the frame/ESP/XMM7/CW/MXCSR/LastError kept.
bool kept_but_ebx(const Run& r) {
    return r.eax == in_eax && r.ecx == in_ecx && r.edx == in_edx && r.esi == in_esi && r.edi == in_edi &&
           r.ebp == in_ebp && r.esp_delta == 0 && r.frame_kept && r.xmm_kept && r.fcw_kept && r.mxcsr_kept &&
           r.error == sentinel_error;
}
bool same(const Run& a, const Run& b) {
    return a.landing == b.landing && a.eax == b.eax && a.ebx == b.ebx && a.ecx == b.ecx && a.edx == b.edx &&
           a.esi == b.esi && a.edi == b.edi && a.ebp == b.ebp && a.esp_delta == b.esp_delta && a.flags == b.flags &&
           a.error == b.error && a.calls == b.calls && a.frame_kept == b.frame_kept && a.xmm_kept == b.xmm_kept &&
           a.fcw_kept == b.fcw_kept && a.mxcsr_kept == b.mxcsr_kept;
}
void initialize_row(const char* label, const wchar_t* value, const char* expect) {
    SetEnvironmentVariableW(L"X3M_DUST_LEAK_FIX", value);
    const unsigned before = rows;
    SetLastError(sentinel_error);
    dlf::initialize();
    const bool error_kept = GetLastError() == sentinel_error;
    char detail[1200];
    std::snprintf(detail, sizeof detail, "row=[%s]", last_row);
    check(rows == before + 1 && std::strstr(last_row, expect) && error_kept, label, detail);
}
void refused(const char* label, bool result, const char* reason, unsigned arena_before) {
    char detail[256];
    std::snprintf(detail, sizeof detail, "state=%s", dlf::state());
    check(!result && !std::strcmp(dlf::state(), reason) && !dlf::patched() && engine_patch::arena_used() == arena_before &&
              windows_original() && span_original(),
          label, detail);
}
uint32_t slot_of_stub() {
    unsigned char code[S::stub_length];
    std::memcpy(code, reinterpret_cast<const void*>(dlf::stub_address()), S::stub_length);
    return get32(code + S::slot_abs32_offset);
}
// After a failed install: the engine's bytes, nothing registered, a failed node leaks as stock (loop landing).
void stock_again(const char* label) {
    const Run r = arrive(u32(node_failed_a), 2, 5, flags_set);
    check(span_original() && !dlf::patched() && r.landing == 1 && r.calls == 0 && r.ebx == 2 && kept_but_ebx(r), label);
}
bool install() {
    SetLastError(sentinel_error);
    const bool ok = dlf::install_base(base);
    return ok && GetLastError() == sentinel_error;
}

int retain_run() {
    check(install() && dlf::patched(), "retain_installed");
    engine_patch::close_install_window("fixture_retain");
    const Run r = arrive(u32(node_failed_a), 2, 5, flags_set);
    check(r.landing == 2 && r.calls == 1 && r_ecx == u32(node_failed_a) && r.ebx == 5 && kept_but_ebx(r),
          "retain_stub_releases_after_close");
    unsigned char before[S::site_length];
    std::memcpy(before, image + S::site_rva, S::site_length);
    SetLastError(sentinel_error);
    const bool gone = dlf::shutdown();
    const bool error_kept = GetLastError() == sentinel_error;
    const char* row = take_restore_row();
    check(!gone && error_kept && dlf::patched() && !std::strcmp(dlf::state(), "restore_retained") &&
              !std::memcmp(before, image + S::site_rva, S::site_length) && image[S::site_rva] == 0xe9 &&
              std::strstr(row, "edition=fl status=restore_retained found=e9") && std::strstr(row, " registered=1"),
          "retain_jump_kept_after_close", row);
    std::printf("RETAIN span=");
    for (unsigned i = 0; i < 5; ++i) std::printf("%02x", image[S::site_rva + i]);
    std::printf("\nRESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    pristine = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    base = reinterpret_cast<std::uintptr_t>(image);
    check(image && pristine && !(base & 0xffff), "image_64k_aligned");
    if (!image || !pristine || !CreatePipe(&pipe_read, &pipe_write, nullptr, 1 << 16)) return 1;
    lay_out();
    std::memcpy(pristine, image, image_size);
    std::memset(node_failed_a, 0, sizeof node_failed_a);
    std::memset(node_failed_b, 0, sizeof node_failed_b);
    std::memset(node_attached, 0, sizeof node_attached);
    {
        const uint32_t s = u32(scene);
        std::memcpy(node_attached + S::scene_offset, &s, 4);
    }
    dlf::fixture_base(0);
    if (argc > 1 && !std::strcmp(argv[1], "retain")) return retain_run();

    // The window identities: FNV-1a with the fixups zeroed; plan() binds every fixup to base.
    std::printf("WINDOWS");
    bool hashes = true;
    for (const S::Window& w : S::windows) {
        unsigned char masked[S::max_window];
        std::memcpy(masked, image + w.rva, w.length);
        for (unsigned k = 0; k < w.fixups; ++k) std::memset(masked + w.fixup_offset[k], 0, 4);
        std::printf(" %s@%06x=%08x", w.name, unsigned(w.rva), S::fnv1a(masked, w.length));
        hashes = hashes && !S::plan(w, image + w.rva, base) &&
                 S::plan(w, image + w.rva, base + 0x10000) == (w.fixups ? w.name : nullptr);
    }
    std::printf("\n");
    check(hashes, "plan_binds_fixups_to_base");

    // Stock: the unpatched tail. A failed node mid-fill loops on (the leak), the last count exits, nothing released.
    const Run stock_null = arrive(0, 2, 5, flags_set);
    const Run stock_attached = arrive(u32(node_attached), 2, 5, flags_set);
    const Run stock_failed = arrive(u32(node_failed_a), 2, 5, flags_set);
    const Run stock_last = arrive(u32(node_failed_a), 5, 5, flags_set);
    check(stock_null.landing == 1 && stock_attached.landing == 1 && stock_failed.landing == 1 && stock_last.landing == 2 &&
              stock_failed.calls == 0 && stock_last.calls == 0 && stock_failed.ebx == 2 && kept_but_ebx(stock_last),
          "stock_tail");

    // initialize(): the fixture's main module is not X3FL.exe (module_base 0); the setting refusals.
    initialize_row("init_unset_executable_mismatch", nullptr,
                   "dust_leak_fix site=00000000 rva=020ac4 edition=fl status=refused reason=executable_mismatch mode=on setting=- write=none stub=00000000");
    initialize_row("init_off", L"off", "status=off reason=off mode=off setting=off write=none");
    initialize_row("init_invalid", L"yes", "status=refused reason=invalid_setting mode=- setting=yes");
    initialize_row("init_case", L"ON", "status=refused reason=invalid_setting mode=-");
    initialize_row("init_too_long", L"onononononononononononononononon", "status=refused reason=too_long mode=- setting=?");
    check(windows_original() && span_original() && !dlf::patched(), "init_refusals_write_nothing");

    // Refusals before anything is emitted or written.
    {
        const unsigned a = engine_patch::arena_used();
        refused("invalid_base_zero", dlf::install_base(0), "invalid_site", a);
        refused("invalid_base_unaligned", dlf::install_base(base + 0x1000), "invalid_site", a);
        refused("install_at_interior", dlf::install_at(base + S::tail_window_rva + 1, base + S::release_rva), "invalid_site", a);
        refused("install_at_wrong_release", dlf::install_at(base + S::tail_window_rva, base + S::release_rva + 1),
                "invalid_site", a);
        // The never-claim start base+0x020ac5: a tail laid one byte later (the CMP at +0x020ac5) is not the window.
        writable(true);
        std::memmove(image + S::tail_window_rva + 1, image + S::tail_window_rva, S::tail_length);
        writable(false);
        const bool shifted = dlf::install_base(base);
        const bool shifted_refused = !shifted && !std::strcmp(dlf::state(), "tail_mismatch") &&
                                     engine_patch::arena_used() == a;
        writable(true);
        std::memcpy(image + S::tail_window_rva, pristine + S::tail_window_rva, S::tail_length + 1);
        writable(false);
        check(shifted_refused && windows_original(), "never_claim_interior_start_refused");
    }
    // Every single-bit change of every window (the fixup included) is refused with that window's reason.
    {
        unsigned flips = 0, refused_flips = 0, fixup_flips = 0, fixup_refused = 0;
        for (const S::Window& w : S::windows) {
            for (unsigned i = 0; i < w.length * 8; ++i) {
                writable(true);
                image[w.rva + i / 8] ^= static_cast<unsigned char>(1u << (i % 8));
                writable(false);
                const unsigned used = engine_patch::arena_used();
                SetLastError(sentinel_error);
                const bool ok = dlf::install_base(base);
                const bool good = !ok && !std::strcmp(dlf::state(), w.name) && engine_patch::arena_used() == used &&
                                  !dlf::patched() && GetLastError() == sentinel_error;
                writable(true);
                image[w.rva + i / 8] ^= static_cast<unsigned char>(1u << (i % 8));
                writable(false);
                ++flips;
                refused_flips += good;
                bool fixup = false;
                for (unsigned k = 0; k < w.fixups; ++k) fixup = fixup || (i / 8 >= w.fixup_offset[k] && i / 8 < w.fixup_offset[k] + 4);
                fixup_flips += fixup;
                fixup_refused += fixup && good;
            }
        }
        std::printf("FLIPS windows=%u bits=%u refused=%u fixup=%u fixup_refused=%u\n", S::window_count, flips,
                    refused_flips, fixup_flips, fixup_refused);
        check(flips == refused_flips && fixup_flips == 32 && fixup_refused == 32 && windows_original(), "every_flip_refused");
    }

    // Claim, chain and read-back faults: all or nothing; the engine's bytes afterwards (or registered when the
    // rollback itself fails, until shutdown()).
    fault_push = true;
    check(!dlf::install_base(base) && !std::strcmp(dlf::state(), "chain_failed") && !dlf::patched() && span_original(),
          "push_fault_rolled_back", dlf::state());
    stock_again("push_fault_stock_after");
    fault_store = true;
    check(!dlf::install_base(base) && !std::strcmp(dlf::state(), "chain_failed") && !dlf::patched() && span_original(),
          "store_fault_rolled_back", dlf::state());
    fault_read_at = base + S::site_rva;
    fault_read_count = S::site_length;
    check(!dlf::install_base(base) && !std::strcmp(dlf::state(), "readback_mismatch") && !dlf::patched() &&
              span_original(),
          "readback_mismatch_rolled_back", dlf::state());
    stock_again("readback_stock_after");
    {
        fault_read_at = base + S::site_rva;
        fault_read_count = S::site_length;
        fault_restore = true;
        const bool ok = dlf::install_base(base);
        check(!ok && !std::strcmp(dlf::state(), "rollback_failed") && dlf::patched() && !span_original() &&
                  dlf::stub_address() == 0 && dlf::hits() == 0,
              "rollback_failed_stays_registered", dlf::state());
        SetEnvironmentVariableW(L"X3M_DUST_LEAK_FIX", L"on");
        check(!dlf::initialize() && rows, "rollback_failed_initialize_not_live");
        SetLastError(sentinel_error);
        const bool gone = dlf::shutdown();
        const char* row = take_restore_row();
        check(gone && !dlf::patched() && span_original() && GetLastError() == sentinel_error &&
                  std::strstr(row, "dust_leak_fix_restore site=") && std::strstr(row, " rva=020ac4 edition=fl status=restored found=e9") &&
                  std::strstr(row, " registered=0"),
              "rollback_failed_then_shutdown_restores", row);
        stock_again("rollback_failed_stock_after");
    }
    std::printf("FAULTS taken=%u\n", faults_taken);
    check(faults_taken == 5, "faults_taken");

    // The install: initialize() with the fixture's base.
    dlf::fixture_base(base);
    initialize_row("init_patched", L"on", "rva=020ac4 edition=fl status=patched reason=ok mode=on setting=on write=plain stub=");
    check(dlf::patched() && dlf::stub_address() && only_span_changed() && !std::strcmp(dlf::write_path(), "plain"),
          "installed_only_the_jump");
    {
        const unsigned before = rows;
        SetEnvironmentVariableW(L"X3M_DUST_LEAK_FIX", L"on");
        check(dlf::initialize() && rows == before, "initialize_live_is_quiet");
        check(!dlf::install_base(base) && !std::strcmp(dlf::state(), "already_installed"), "already_installed");
        const unsigned char* stub = reinterpret_cast<const unsigned char*>(dlf::stub_address());
        std::printf("STUB at=%08x release=%08x get=%08x set=%08x hits=%08x arena=%08x size=%u bytes=", u32(stub),
                    uint32_t(base + S::release_rva), u32(reinterpret_cast<void*>(&GetLastError)),
                    u32(reinterpret_cast<void*>(&SetLastError)), u32(const_cast<std::uint32_t*>(&x3m_dust_leak_fix_hits)),
                    u32(engine_patch::arena_base()), engine_patch::arena_capacity());
        for (unsigned i = 0; i < S::stub_length; ++i) std::printf("%02x", stub[i]);
        std::printf("\n");
    }

    // Patched runs through the real tail bytes.
    {
        const uint32_t hits0 = dlf::hits();
        const Run null_run = arrive(0, 2, 5, flags_set);
        const Run attached_run = arrive(u32(node_attached), 2, 5, flags_set);
        check(same(null_run, stock_null) && same(attached_run, stock_attached) && dlf::hits() == hits0,
              "patched_null_and_attached_unchanged");
        const Run failed = arrive(u32(node_failed_a), 2, 5, flags_set);
        check(failed.landing == 2 && failed.calls == 1 && r_ecx == u32(node_failed_a) && r_ebp == u32(node_failed_a) &&
                  !(r_flags & 0x400) && r_error_seen == sentinel_error && failed.ebx == 5 && kept_but_ebx(failed) &&
                  dlf::hits() == hits0 + 1,
              "failed_node_released_once_and_fill_ends");
        const Run last = arrive(u32(node_failed_b), 5, 5, flags_set);
        check(last.landing == 2 && last.calls == 1 && r_ecx == u32(node_failed_b) && last.ebx == 5 && kept_but_ebx(last) &&
                  dlf::hits() == hits0 + 2,
              "failed_last_node_released");
        const Run first = arrive(u32(node_failed_a), 1, 27, flags_set);
        check(first.landing == 2 && first.calls == 1 && first.ebx == 27 && kept_but_ebx(first), "failed_first_of_27");
        // One invocation's arrivals: null, attached, failed A (ends it); the next invocation: failed B (ends it).
        const uint32_t calls = r_calls;
        const Run s1 = arrive(0, 1, 4, flags_set), s2 = arrive(u32(node_attached), 2, 4, flags_set),
                  s3 = arrive(u32(node_failed_a), 3, 4, flags_set), s4 = arrive(u32(node_failed_b), 1, 4, flags_set);
        check(s1.landing == 1 && s2.landing == 1 && s3.landing == 2 && s4.landing == 2 && r_calls == calls + 2 &&
                  r_nodes[(calls) % 8] == u32(node_failed_a) && r_nodes[(calls + 1) % 8] == u32(node_failed_b),
              "sequence_one_release_per_failed_node");
        std::printf("RUNS hits=%u releases=%u\n", unsigned(dlf::hits()), unsigned(r_calls));
        // The 300-frame row: the delta since the previous row and the total.
        const unsigned before = rows;
        dlf::report(900);
        char expect[96];
        std::snprintf(expect, sizeof expect, "dust_leak_fix hits=%u total=%u frame=900 edition=fl", unsigned(dlf::hits()),
                      unsigned(dlf::hits()));
        const bool first_row = rows == before + 1 && !std::strcmp(last_row, expect);
        dlf::report(1200);
        std::snprintf(expect, sizeof expect, "dust_leak_fix hits=0 total=%u frame=1200 edition=fl", unsigned(dlf::hits()));
        check(first_row && rows == before + 2 && !std::strcmp(last_row, expect), "report_rows");
    }
    // Recorder runs: the state the tail receives from the stub, flags included (DF set where nothing is called).
    {
        const uint32_t slot = slot_of_stub();
        void* tail = *reinterpret_cast<void**>(slot);
        const bool redirected = engine_patch::store_pointer(reinterpret_cast<void**>(slot), reinterpret_cast<void*>(&fl_dust_recorder));
        const Run n = arrive(0, 2, 5, flags_set_df);
        const bool null_ok = n.landing == 3 && n.flags == (flags_set_df & flags_mask) && n.ebx == 2 && kept_but_ebx(n) && n.calls == 0;
        const Run a = arrive(u32(node_attached), 2, 5, flags_set_df);
        const bool attached_ok = a.landing == 3 && a.flags == (flags_set_df & flags_mask) && a.ebx == 2 && kept_but_ebx(a) && a.calls == 0;
        const Run f = arrive(u32(node_failed_a), 2, 5, flags_set);
        const bool failed_ok = f.landing == 3 && f.flags == (flags_set & flags_mask) && f.ebx == 5 && kept_but_ebx(f) && f.calls == 1 &&
                               r_ecx == u32(node_failed_a);
        const Run g = arrive(u32(node_failed_b), 2, 5, 0x002); // all arithmetic flags clear
        const bool clear_ok = g.landing == 3 && g.flags == 0 && g.ebx == 5 && kept_but_ebx(g) && g.calls == 1;
        const bool back = engine_patch::store_pointer(reinterpret_cast<void**>(slot), tail);
        check(redirected && back, "recorder_slot_redirect");
        check(null_ok, "recorder_null_flags_df_registers_kept");
        check(attached_ok, "recorder_attached_flags_df_registers_kept");
        check(failed_ok, "recorder_failed_flags_kept_ebx_is_count");
        check(clear_ok, "recorder_failed_clear_flags_kept");
        std::printf("RECORDER null=%u attached=%u failed=%u clear=%u\n", unsigned(null_ok), unsigned(attached_ok),
                    unsigned(failed_ok), unsigned(clear_ok));
    }

    // shutdown(): only over our jump; foreign bytes are left; the engine's bytes need no write.
    {
        unsigned char ours[5];
        std::memcpy(ours, image + S::site_rva, 5);
        writable(true);
        std::memset(image + S::site_rva, 0x90, 5);
        writable(false);
        SetLastError(sentinel_error);
        const bool gone = dlf::shutdown();
        const char* row = take_restore_row();
        check(!gone && dlf::patched() && image[S::site_rva] == 0x90 && GetLastError() == sentinel_error &&
                  std::strstr(row, "status=restore_not_owned found=9090909090 registered=1"),
              "shutdown_foreign_bytes_left", row);
        writable(true);
        std::memcpy(image + S::site_rva, ours, 5);
        writable(false);
        SetLastError(sentinel_error);
        const bool gone2 = dlf::shutdown();
        const char* row2 = take_restore_row();
        check(gone2 && !dlf::patched() && span_original() && windows_original() && GetLastError() == sentinel_error &&
                  std::strstr(row2, "edition=fl status=restored found=e9") && std::strstr(row2, " registered=0") &&
                  dlf::hits() == 0 && dlf::stub_address() == 0,
              "shutdown_restores_our_bytes", row2);
        stock_again("shutdown_stock_after");
        // Reinstall; the engine's bytes put back by someone else: no write, nothing registered.
        check(install() && dlf::patched() && only_span_changed(), "reinstall_after_shutdown");
        writable(true);
        std::memcpy(image + S::site_rva, S::expected_site, S::site_length);
        writable(false);
        const bool gone3 = dlf::shutdown();
        const char* row3 = take_restore_row();
        check(gone3 && !dlf::patched() && span_original() && std::strstr(row3, "status=restored found=3b5c24180f"),
              "shutdown_engine_bytes_no_write", row3);
        check(dlf::shutdown(), "shutdown_idempotent");
    }
    // Last (the window cannot reopen): a late install is refused before any read.
    engine_patch::close_install_window("fixture_late");
    dlf::fixture_base(base);
    initialize_row("init_late_claim", L"on", "edition=fl status=refused reason=late_claim mode=on setting=on write=none stub=00000000");
    check(span_original() && !dlf::patched(), "late_claim_writes_nothing");
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
