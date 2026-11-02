// Original synthetic fixture of the FL field of view (src/engine/fl/fov_fl.cpp, fov_sites_fl.h;
// docs/reverse-engineering/fl/field-of-view.md, "Verified 2026-11-02"): no game code, no game bytes beyond the six
// windows in fov_sites_fl.h, no launch. Built with -DX3M_EDITION_FL=1 -DX3M_FOV_FL_FIXTURE by
// verification/analysis/test_fl_fov.py (the module force-includes fl_fov_shim.h for the read-back / chain / restore
// faults) and run natively (WSL interop).
//
// A 64 KB-aligned private image stands in for X3FL.exe at `base`: the constructor window at base+0x01e076, the
// INS_SetFocus case window at base+0x030e9f (fixups = base+0x1a82e4 and base+0x1aa174), the callee prefix at
// base+0x0c1940, the load window at base+0x01dedc, the load caller at base+0x020db6 and the update reader at
// base+0x022a1c (fixup = base+0x1a82e4); the registry slot base+0x1a82e4 points at a fake registry, the VM slot holds a
// sentinel. `push imm32; ret` jumps stand at the case exit base+0x03246f and the stream reader base+0x106700, and (only
// while a case runs, never during an install) at the callee base+0x0c1940. The real case and load bytes run: the
// harness enters INS_SetFocus at its case start with EBX = a fake dispatcher frame, and the load window at its stream
// call with the serializer's stack (three saved registers, the return address, the two arguments, the raw stream dword
// in the first argument's slot), sentinels in every register, DF set, XMM7/x87 CW/MXCSR set and LastError set.
//
//   ./fl_fov.exe          the full run (natives, refusals, flips, install, vectors, shutdown, initialize rows,
//                         rollbacks, late_claim after everything is restored)
//   ./fl_fov.exe retain   install, close the window, the stubs still remap, shutdown restores the jumps and keeps the
//                         crossing literal (literal_retained)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../../src/engine/fl/fov_fl.h"
#include "../../src/engine/fl/fov_sites_fl.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/fov_sites.h"
namespace fov = x3m::fov;
namespace S = x3m::fl::fov_fl;
namespace sites = x3m::fov::sites;
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
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(last_row, sizeof last_row, format, args);
    va_end(args);
    ++rows;
    std::printf("LOG %s\n", last_row);
}
}
// The seam (fl_fov_shim.h): one-shot faults, otherwise the real calls.
std::uintptr_t fault_read_at = 0;
unsigned fault_read_count = 0, fault_read_skip = 0;
bool fault_read_fails = false; // false: the read succeeds with its second byte flipped; true: the read fails
std::uintptr_t fault_restore_at = 0, fault_push_at = 0;
unsigned faults_taken = 0;
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count) {
    const bool ok = read_code(address, out, count);
    if (ok && fault_read_at && address == fault_read_at && count == fault_read_count) {
        if (fault_read_skip) {
            --fault_read_skip;
            return ok;
        }
        fault_read_at = 0;
        ++faults_taken;
        if (fault_read_fails) return false;
        out[1] ^= 0xff;
    }
    return ok;
}
bool fixture_restore(Site& site) {
    if (fault_restore_at && site.spec.address == fault_restore_at) { // as a refused VirtualProtect: still registered
        fault_restore_at = 0;
        ++faults_taken;
        site.status = "restore_protect_failed";
        return false;
    }
    return restore(site);
}
void* fixture_push_front(Site& site, void* stub) {
    if (fault_push_at && site.spec.address == fault_push_at) {
        fault_push_at = 0;
        ++faults_taken;
        return nullptr;
    }
    return push_front(site, stub);
}
}

extern "C" {
uint32_t in_eax = 0, in_ebx = 0, in_ecx = 0, in_edx = 0, in_esi = 0, in_edi = 0, in_ebp = 0;
uint32_t in_raw = 0, in_stream = 0, in_pop_ebx = 0, in_pop_esi = 0, in_pop_edi = 0;
uint32_t site_case = 0, site_load = 0, saved_esp = 0, esp_before = 0;
uint32_t out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags;
uint32_t t_eax, t_ebx, t_ecx, t_edx, t_esi, t_edi, t_ebp, t_esp, t_flags, t_stack[3], t_calls = 0, reader_calls = 0;
alignas(16) unsigned char in_xmm[16], out_xmm[16];
uint16_t in_fcw = 0x027f, out_fcw = 0, def_fcw = 0;
uint32_t in_mxcsr = 0x1fa0, out_mxcsr = 0, def_mxcsr = 0;
void fl_fov_call_setfocus();
void fl_fov_return_setfocus();
void fl_fov_call_load();
void fl_fov_return_load();
void fl_fov_fake_callee();
void fl_fov_fake_reader();
}
#define ENTER                                                                                                          \
    "pushal\n\tpushfl\n\tmovl %esp,_saved_esp\n\tfnstcw _def_fcw\n\tstmxcsr _def_mxcsr\n\t"                           \
    "movdqu _in_xmm,%xmm7\n\tfldcw _in_fcw\n\tldmxcsr _in_mxcsr\n\t"
#define LOAD_REGS                                                                                                      \
    "movl _in_eax,%eax\n\tmovl _in_ebx,%ebx\n\tmovl _in_ecx,%ecx\n\tmovl _in_edx,%edx\n\tmovl _in_esi,%esi\n\t"        \
    "movl _in_edi,%edi\n\tmovl _in_ebp,%ebp\n\tmovl %esp,_esp_before\n\tstd\n\t"
#define STORE_OUT                                                                                                      \
    "pushfl\n\tpopl _out_flags\n\tcld\n\tmovl %eax,_out_eax\n\tmovl %ebx,_out_ebx\n\tmovl %ecx,_out_ecx\n\t"          \
    "movl %edx,_out_edx\n\tmovl %esi,_out_esi\n\tmovl %edi,_out_edi\n\tmovl %ebp,_out_ebp\n\tmovl %esp,_out_esp\n\t"   \
    "movdqu %xmm7,_out_xmm\n\tfnstcw _out_fcw\n\tstmxcsr _out_mxcsr\n\tfldcw _def_fcw\n\tldmxcsr _def_mxcsr\n\t"       \
    "movl _saved_esp,%esp\n\tpopfl\n\tpopal\n\tret\n\t"
asm(".text\n\t"
    // INS_SetFocus: EBX = the fake frame; the real case bytes push the two arguments and call the callee.
    ".globl _fl_fov_call_setfocus\n_fl_fov_call_setfocus:\n\t" ENTER LOAD_REGS "jmp *_site_case\n\t"
    ".globl _fl_fov_return_setfocus\n_fl_fov_return_setfocus:\n\t" STORE_OUT
    // The load: POP EDI/ESI/EBX, RET 8 to the harness; the raw stream dword in the first argument's slot.
    ".globl _fl_fov_call_load\n_fl_fov_call_load:\n\t" ENTER
    "pushl _in_stream\n\tpushl _in_raw\n\tpushl $_fl_fov_return_load\n\tpushl _in_pop_ebx\n\tpushl _in_pop_esi\n\t"
    "pushl _in_pop_edi\n\t" LOAD_REGS "jmp *_site_load\n\t"
    ".globl _fl_fov_return_load\n_fl_fov_return_load:\n\t" STORE_OUT
    // The callee: entry registers, EFLAGS, ESP and three stack dwords; EAX kept; RET 8 as 0x0c1940 does.
    ".globl _fl_fov_fake_callee\n_fl_fov_fake_callee:\n\t"
    "movl %eax,_t_eax\n\tmovl %ebx,_t_ebx\n\tmovl %ecx,_t_ecx\n\tmovl %edx,_t_edx\n\tmovl %esi,_t_esi\n\t"
    "movl %edi,_t_edi\n\tmovl %ebp,_t_ebp\n\tmovl %esp,_t_esp\n\tpushfl\n\tpopl _t_flags\n\t"
    "movl (%esp),%eax\n\tmovl %eax,_t_stack\n\tmovl 4(%esp),%eax\n\tmovl %eax,_t_stack+4\n\t"
    "movl 8(%esp),%eax\n\tmovl %eax,_t_stack+8\n\tmovl _t_eax,%eax\n\tincl _t_calls\n\tret $8\n\t"
    // The stream reader: plain RET (the harness enters at the call with nothing pushed for it).
    ".globl _fl_fov_fake_reader\n_fl_fov_fake_reader:\n\tincl _reader_calls\n\tret\n\t");

std::uintptr_t base = 0;
unsigned char* image = nullptr;
unsigned char* pristine = nullptr;
constexpr unsigned image_size = 0x1b0000;
constexpr uint32_t sentinel_error = 0x2a2a1357, vm_value = 0x5a5a0001, ctx_value = 0x0c0c0c0c;
alignas(16) unsigned char registry[0x60];
uint32_t frame[8];
unsigned char args[8];
uint16_t table[sites::remap_count], vanilla[sites::remap_count];

uint32_t u32(const void* p) {
    return uint32_t(reinterpret_cast<std::uintptr_t>(p));
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
    jump_at(S::setfocus_exit_rva, reinterpret_cast<void*>(&fl_fov_return_setfocus));
    jump_at(S::stream_reader_rva, reinterpret_cast<void*>(&fl_fov_fake_reader));
    put32(S::registry_slot_rva, u32(registry));
    put32(S::vm_slot_rva, vm_value);
    writable(false);
    FlushInstructionCache(GetCurrentProcess(), image, image_size);
}
// The callee prefix (validated at install) or the recording fake (only while a case runs).
void callee_fake(bool on) {
    writable(true);
    if (on)
        jump_at(S::setfocus_callee_rva, reinterpret_cast<void*>(&fl_fov_fake_callee));
    else
        std::memcpy(image + S::setfocus_callee_rva, S::expected_setfocus_callee, S::setfocus_callee_length);
    writable(false);
    FlushInstructionCache(GetCurrentProcess(), image + S::setfocus_callee_rva, S::setfocus_callee_length);
}
bool windows_original() {
    for (const S::Window& w : S::windows)
        if (std::memcmp(image + w.rva, pristine + w.rva, w.length)) return false;
    return true;
}
uint32_t imm_now() {
    uint32_t v = 0;
    std::memcpy(&v, image + S::write_rva, 4);
    return v;
}
uint32_t focus_now() {
    uint32_t v = 0;
    std::memcpy(&v, registry + 0x24, 4);
    return v;
}
void set_focus(uint32_t v) {
    std::memcpy(registry + 0x24, &v, 4);
}
// Only the imm32 and the two five-byte sites differ from the pristine windows.
bool only_sites_changed() {
    unsigned char now[S::max_window];
    for (const S::Window& w : S::windows) {
        std::memcpy(now, image + w.rva, w.length);
        if (w.rva == S::window_rva) std::memcpy(now + S::write_offset, pristine + S::write_rva, 4);
        if (w.rva == S::setfocus_case_rva) std::memcpy(now + S::setfocus_site_offset, pristine + S::setfocus_site_rva, 5);
        if (w.rva == S::load_window_rva) std::memcpy(now + S::load_site_offset, pristine + S::load_site_rva, 5);
        if (std::memcmp(now, pristine + w.rva, w.length)) return false;
    }
    return true;
}
bool jump_at_site(uint32_t rva) {
    return image[rva] == 0xe9;
}

// ---- INS_SetFocus through the real case bytes ----
uint32_t native_flags_setfocus = 0, native_flags_load = 0;
constexpr uint32_t arithmetic = 0x8d5; // CF PF AF ZF SF OF: dead at both continuations (documented clobbers)
void cpu_inputs() {
    for (unsigned i = 0; i < 16; ++i) in_xmm[i] = static_cast<unsigned char>(0xa0 + i);
    in_fcw = 0x027f;
    in_mxcsr = 0x1fa0;
}
bool cpu_kept() {
    return !std::memcmp(in_xmm, out_xmm, 16) && out_fcw == in_fcw && out_mxcsr == in_mxcsr && (out_flags & 0x400);
}
// One INS_SetFocus with the script's F; true when the registry got `expect` and every invariant held.
bool run_setfocus(uint32_t f, uint32_t expect, bool patched, unsigned* bad = nullptr) {
    args[0] = 0x11;
    std::memcpy(args + 1, &f, 4);
    frame[0x18 / 4] = u32(args);
    frame[0x0c / 4] = ctx_value;
    in_eax = 0xeaeaeaea, in_ebx = u32(frame), in_ecx = 0x11110000, in_edx = 0x22220003, in_esi = 0x5e5e5e5e;
    in_edi = 0x7d7d7d7d, in_ebp = 0xebebebeb;
    cpu_inputs();
    set_focus(0xdeadbeef);
    const unsigned calls = t_calls;
    callee_fake(true);
    SetLastError(sentinel_error);
    fl_fov_call_setfocus();
    const DWORD error = GetLastError();
    callee_fake(false);
    const uint32_t expect_stack[3] = {uint32_t(base + S::setfocus_case_rva + 30), ctx_value, 0};
    const bool callee_ok = t_calls == calls + 1 && t_eax == u32(registry) && t_ecx == vm_value && t_edx == in_edx &&
                           t_ebx == in_ebx && t_esi == in_esi && t_edi == in_edi && t_ebp == in_ebp &&
                           t_esp == esp_before - 12 && !std::memcmp(t_stack, expect_stack, sizeof expect_stack) &&
                           (t_flags & 0x400);
    if (!patched) native_flags_setfocus = t_flags & ~arithmetic;
    const bool flags_ok = (t_flags & ~arithmetic) == native_flags_setfocus;
    const bool out_ok = out_eax == u32(registry) && out_ecx == vm_value && out_edx == in_edx && out_ebx == in_ebx &&
                        out_esi == in_esi && out_edi == in_edi && out_ebp == in_ebp && out_esp == esp_before;
    const bool ok = callee_ok && flags_ok && out_ok && cpu_kept() && error == sentinel_error && focus_now() == expect;
    if (!ok) {
        char detail[300];
        std::snprintf(detail, sizeof detail,
                      "f=%08x focus=%08x/%08x callee=%u flags=%u out=%u cpu=%u error=%u t_eax=%08x t_ecx=%08x t_esp=%08x/%08x",
                      f, focus_now(), expect, callee_ok, flags_ok, out_ok, cpu_kept(), error == sentinel_error, t_eax,
                      t_ecx, t_esp, esp_before);
        if (bad) ++*bad;
        if (!bad || *bad <= 4) check(false, patched ? "setfocus_patched_case" : "setfocus_native_case", detail);
    }
    return ok;
}
// ---- The load store through the real window bytes ----
uint32_t native_load_eax(uint32_t raw) { // EAX's path through 01dee1..01df07, then MOV AL,1
    uint32_t ecx = raw, eax = ecx;
    eax <<= 16;
    ecx &= 0xff00;
    eax |= ecx;
    eax <<= 8;
    return (eax & ~0xffu) | 1u;
}
uint32_t bswap(uint32_t v) {
    return (v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24);
}
bool run_load(uint32_t saved, uint32_t expect, bool patched, unsigned* bad = nullptr) {
    in_raw = bswap(saved), in_stream = 0x57575757, in_pop_ebx = 0xb0b0b0b0, in_pop_esi = 0x51515151;
    in_pop_edi = 0xd0d0d0d0;
    in_eax = 0xeaeaea00, in_ebx = 0x0b0b0b0b, in_ecx = 0x11110000, in_edx = 0x22220003, in_esi = 0x5e5e5e5e;
    in_edi = u32(registry), in_ebp = 0xebebebeb;
    cpu_inputs();
    set_focus(0xdeadbeef);
    const unsigned calls = reader_calls;
    SetLastError(sentinel_error);
    fl_fov_call_load();
    const DWORD error = GetLastError();
    if (!patched) native_flags_load = out_flags & ~arithmetic;
    const bool out_ok = reader_calls == calls + 1 && out_eax == native_load_eax(in_raw) && out_edx == expect &&
                        out_ebx == in_pop_ebx && out_esi == in_pop_esi && out_edi == in_pop_edi && out_ebp == in_ebp &&
                        out_esp == esp_before + 0x18 && (patched || out_ecx == (in_raw & 0xff00)) &&
                        (out_flags & ~arithmetic) == native_flags_load;
    const bool ok = out_ok && cpu_kept() && error == sentinel_error && focus_now() == expect;
    if (!ok) {
        char detail[240];
        std::snprintf(detail, sizeof detail, "saved=%08x focus=%08x/%08x out=%u cpu=%u error=%u eax=%08x/%08x esp=%08x/%08x",
                      saved, focus_now(), expect, out_ok, cpu_kept(), error == sentinel_error, out_eax,
                      native_load_eax(in_raw), out_esp, esp_before + 0x18);
        if (bad) ++*bad;
        if (!bad || *bad <= 4) check(false, patched ? "load_patched_case" : "load_native_case", detail);
    }
    return ok;
}
// The vectors: every vanilla (N << 16) / 360, N 0..180; for the load also every F'(N) and every vanilla + 1 in the
// table range; plus the boundaries and arbitrary values.
const uint32_t extras[] = {0,      1,      sites::remap_focus_min - 1, sites::remap_focus_min, sites::remap_focus_max,
                           sites::remap_focus_max + 1, 0x4000, 0x3470, 0x2ccc, 0x8000, 0x10000, 0x7fffffff,
                           0xffffffff, 0x80002334, 0x00012334};
struct Tally {
    unsigned cases = 0, remapped = 0, bad = 0;
};
Tally setfocus_vectors(bool patched) {
    Tally t;
    auto one = [&](uint32_t f) {
        const uint32_t expect = patched ? sites::remap_lookup(f, table) : f;
        ++t.cases;
        t.remapped += expect != f;
        run_setfocus(f, expect, patched, &t.bad);
    };
    for (uint32_t n = 0; n <= 180; ++n) one((n << 16) / 360u);
    for (uint32_t f : extras) one(f);
    return t;
}
Tally load_vectors(bool patched) {
    Tally t;
    auto one = [&](uint32_t f) {
        const uint32_t expect = patched ? sites::load_lookup(f, vanilla, table) : f;
        ++t.cases;
        t.remapped += expect != f;
        run_load(f, expect, patched, &t.bad);
    };
    for (uint32_t n = 0; n <= 180; ++n) one((n << 16) / 360u);
    for (unsigned i = 0; i < sites::remap_count; ++i) one(table[i]);
    for (unsigned i = 0; i < sites::remap_count; ++i) one(vanilla[i] + 1u);
    for (uint32_t f : extras) one(f);
    return t;
}

void set_setting(const wchar_t* value) {
    SetEnvironmentVariableW(L"X3M_FOV", value);
}
bool initialize_row(const char* label, const wchar_t* value, const char* expect) {
    set_setting(value);
    const unsigned before = rows, used = engine_patch::arena_used();
    SetLastError(sentinel_error);
    const bool applied = fov::initialize();
    const bool ok = !applied && GetLastError() == sentinel_error && rows == before + 1 && std::strstr(last_row, expect) &&
                    std::strstr(last_row, " edition=fl ") && windows_original() && !fov::patched() &&
                    engine_patch::arena_used() == used;
    check(ok, label, last_row);
    return ok;
}
// A refused install: nothing emitted, nothing written, nothing registered.
bool refused(const char* label, std::uintptr_t at, uint32_t focus, const char* state) {
    const unsigned used = engine_patch::arena_used();
    SetLastError(sentinel_error);
    const bool ok = fov::install_base(at, focus);
    const bool pass = !ok && GetLastError() == sentinel_error && !std::strcmp(fov::state(), state) && windows_original() &&
                      !fov::patched() && engine_patch::arena_used() == used;
    check(pass, label, fov::state());
    return pass;
}
// A failed install after writes: the reason, the per-site states, and whether the image is back to the engine's.
void failed_install(const char* label, const char* state, const char* setfocus, const char* load, bool original) {
    SetLastError(sentinel_error);
    const bool ok = fov::install_base(base, sites::constructor_focus(90));
    char detail[200];
    std::snprintf(detail, sizeof detail, "state=%s setfocus=%s load=%s write=%s patched=%u imm=%08x", fov::state(),
                  fov::setfocus_state(), fov::load_state(), fov::write_path(), fov::patched(), imm_now());
    check(!ok && GetLastError() == sentinel_error && !std::strcmp(fov::state(), state) &&
              !std::strcmp(fov::setfocus_state(), setfocus) && !std::strcmp(fov::load_state(), load) &&
              (!original || (windows_original() && !fov::patched() && fov::configured_focus() == sites::engine_focus)),
          label, detail);
}
bool install_ok(const char* label) {
    SetLastError(sentinel_error);
    const uint32_t focus = sites::constructor_focus(90);
    const bool ok = fov::install_base(base, focus);
    char detail[200];
    std::snprintf(detail, sizeof detail, "state=%s setfocus=%s load=%s write=%s imm=%08x", fov::state(),
                  fov::setfocus_state(), fov::load_state(), fov::write_path(), imm_now());
    const bool pass = ok && GetLastError() == sentinel_error && !std::strcmp(fov::state(), "ok") &&
                      !std::strcmp(fov::setfocus_state(), "active") && !std::strcmp(fov::load_state(), "active") &&
                      !std::strcmp(fov::write_path(), "plain") && fov::setfocus_patched() && fov::load_patched() &&
                      imm_now() == focus && fov::configured_focus() == focus && only_sites_changed() &&
                      jump_at_site(S::setfocus_site_rva) && jump_at_site(S::load_site_rva);
    check(pass, label, detail);
    return pass;
}
bool shutdown_clean(const char* label) {
    SetLastError(sentinel_error);
    const bool ok = fov::shutdown();
    const bool pass = ok && GetLastError() == sentinel_error && windows_original() && !fov::patched() &&
                      fov::configured_focus() == sites::engine_focus && std::strstr(last_row, "fov_restore ") &&
                      std::strstr(last_row, " status=restored found=") && std::strstr(last_row, " registered=0 setfocus=restored load=restored");
    check(pass, label, last_row);
    return pass;
}

int retain_run() {
    // Install, close the window: the stubs keep remapping; shutdown restores both jumps (one lock cmpxchg8b each) and
    // keeps the crossing literal registered (no quiescence once the engine runs).
    const uint32_t focus = sites::constructor_focus(90);
    check(run_setfocus((90u << 16) / 360u, (90u << 16) / 360u, false) && run_load(0x4000, 0x4000, false),
          "retain_native_baseline");
    install_ok("retain_installed");
    engine_patch::close_install_window("fixture_retain");
    check(run_setfocus((90u << 16) / 360u, 0x3470, true) && run_load((90u << 16) / 360u, 0x3470, true),
          "retain_stubs_live_after_close");
    SetLastError(sentinel_error);
    const bool clean = fov::shutdown();
    char detail[200];
    std::snprintf(detail, sizeof detail, "row=%s", last_row);
    check(!clean && GetLastError() == sentinel_error && fov::patched() && !fov::setfocus_patched() &&
              !fov::load_patched() && imm_now() == focus && fov::configured_focus() == focus &&
              !std::strcmp(fov::state(), "literal_retained") && std::strstr(last_row, " status=literal_retained found=-- registered=1 setfocus=restored load=restored") &&
              !jump_at_site(S::setfocus_site_rva) && !jump_at_site(S::load_site_rva),
          "retain_jumps_restored_literal_kept", detail);
    check(run_setfocus((90u << 16) / 360u, (90u << 16) / 360u, false) && run_load(0x4000, 0x4000, false),
          "retain_native_paths_after_restore");
    std::printf("RETAIN literal=%08x\n", imm_now());
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    pristine = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    base = reinterpret_cast<std::uintptr_t>(image);
    check(image && pristine && !(base & 0xffff), "image_64k_aligned");
    if (!image || !pristine) return 1;
    lay_out();
    std::memcpy(pristine, image, image_size);
    site_case = uint32_t(base + S::setfocus_case_rva);
    site_load = uint32_t(base + S::load_window_rva);
    sites::build_remap_table(table);
    sites::build_vanilla_table(vanilla);
    if (argc > 1 && !std::strcmp(argv[1], "retain")) return retain_run();

    // The window identities: FNV-1a with the fixups zeroed (the producer's convention).
    std::printf("WINDOWS");
    bool hashes = true;
    for (const S::Window& w : S::windows) {
        unsigned char masked[S::max_window];
        std::memcpy(masked, image + w.rva, w.length);
        for (unsigned k = 0; k < w.fixups; ++k) std::memset(masked + w.fixup_offset[k], 0, 4);
        std::printf(" %s=%08x", w.name, S::fnv1a(masked, w.length));
        hashes = hashes && !S::plan(w, image + w.rva, base) && S::plan(w, image + w.rva, base + 0x10000) == (w.fixups ? w.name : nullptr);
    }
    std::printf(" base=%08x\n", uint32_t(base));
    check(hashes, "plan_binds_fixups_to_base");
    // The two stubs with fixed operands, for the Capstone audit in test_fl_fov.py.
    {
        unsigned char sf[S::stub_length], ld[S::load_stub_length];
        S::encode_setfocus_stub(0x20000104, 0x20000100, sf);
        S::encode_load_stub(0x20000204, 0x20000104, 0x20000200, ld);
        std::printf("STUB setfocus=");
        for (unsigned char b : sf) std::printf("%02x", b);
        std::printf("\nSTUB load=");
        for (unsigned char b : ld) std::printf("%02x", b);
        std::printf("\n");
    }

    // The unpatched image: every vector stores F unchanged.
    const Tally native_sf = setfocus_vectors(false), native_ld = load_vectors(false);
    check(native_sf.bad == 0 && native_ld.bad == 0, "native_vectors");

    // initialize(): the fixture's main module is not X3FL.exe (module_base 0); the setting refusals.
    initialize_row("init_unset_off", nullptr, "status=off reason=game value=0x4000 vertical_deg=73.74 setting=- write=none registry=none setfocus=none");
    initialize_row("init_game_off", L"game", "status=off reason=game value=0x4000");
    initialize_row("init_invalid", L"wide", "status=refused reason=invalid_setting value=0x4000");
    initialize_row("init_out_of_range", L"69", "status=refused reason=out_of_range");
    initialize_row("init_too_long", L"9000000000000000000000000000000000000", "status=refused reason=too_long value=0x4000 vertical_deg=73.74 setting=?");
    initialize_row("init_executable_mismatch", L"90", "fov site=00000000 rva=01e08d edition=fl status=refused reason=executable_mismatch");

    // Refusals before anything is emitted or written.
    refused("invalid_base_zero", 0, sites::constructor_focus(90), "invalid_site");
    refused("invalid_base_unaligned", base + 0x1000, sites::constructor_focus(90), "invalid_site");
    refused("invalid_value_engine", base, sites::engine_focus, "invalid_value");
    refused("invalid_value_zero", base, 0, "invalid_value");
    refused("invalid_value_ceiling", base, 0x8001, "invalid_value");
    // Every single-bit change of every window (fixups included) is refused with that window's reason; nothing emitted.
    unsigned flips = 0, refused_flips = 0, fixup_flips = 0, fixup_refused = 0;
    for (const S::Window& w : S::windows) {
        for (unsigned i = 0; i < w.length * 8; ++i) {
            writable(true);
            image[w.rva + i / 8] ^= static_cast<unsigned char>(1u << (i % 8));
            writable(false);
            const unsigned used = engine_patch::arena_used();
            SetLastError(sentinel_error);
            const bool ok = fov::install_base(base, sites::constructor_focus(90));
            bool fixup = false;
            for (unsigned k = 0; k < w.fixups; ++k) fixup = fixup || (i / 8 >= w.fixup_offset[k] && i / 8 < w.fixup_offset[k] + 4);
            const bool pass = !ok && GetLastError() == sentinel_error && !std::strcmp(fov::state(), w.name) &&
                              !fov::patched() && engine_patch::arena_used() == used;
            writable(true);
            image[w.rva + i / 8] ^= static_cast<unsigned char>(1u << (i % 8));
            writable(false);
            ++flips;
            refused_flips += pass && windows_original();
            fixup_flips += fixup;
            fixup_refused += fixup && pass;
        }
    }
    std::printf("FLIPS windows=%u bits=%u refused=%u fixup=%u fixup_refused=%u\n", S::window_count, flips, refused_flips,
                fixup_flips, fixup_refused);
    check(flips == 1360 && refused_flips == flips && fixup_flips == 96 && fixup_refused == 96, "every_window_bit_refused");
    // A fixup zeroed (the hashing form) or rebased to another base: refused.
    for (uint32_t value : {0u, uint32_t(base + 0x10000 + S::registry_slot_rva)}) {
        writable(true);
        put32(S::setfocus_case_rva + 0x0c, value);
        writable(false);
        const unsigned used = engine_patch::arena_used();
        const bool ok = fov::install_base(base, sites::constructor_focus(90));
        const bool kept = image[S::setfocus_site_rva] == 0xa1 && imm_now() == sites::engine_focus;
        writable(true);
        std::memcpy(image + S::setfocus_case_rva, pristine + S::setfocus_case_rva, S::setfocus_case_length);
        writable(false);
        check(!ok && kept && !std::strcmp(fov::state(), "setfocus_mismatch") && !fov::patched() &&
                  engine_patch::arena_used() == used && windows_original(),
              value ? "setfocus_fixup_other_base" : "setfocus_fixup_zeroed", fov::state());
    }
    FlushInstructionCache(GetCurrentProcess(), image, image_size);

    // Install: the literal (plain), INS_SetFocus and the load (one lock cmpxchg8b each).
    install_ok("installed_all_three");
    check(!fov::install_base(base, sites::constructor_focus(90)) && !std::strcmp(fov::state(), "already_installed") &&
              fov::setfocus_patched() && fov::load_patched(),
          "second_install_refused");
    // Rebased qword geometry: the base is 64 KB aligned, so the imm32 crosses a qword and both jumps fit one.
    check(((base + S::write_rva) & 7) + 4 > 8 && ((base + S::setfocus_site_rva) & 7) + 5 <= 8 &&
              ((base + S::load_site_rva) & 7) + 5 <= 8,
          "qword_geometry_at_base");
    const Tally sf = setfocus_vectors(true), ld = load_vectors(true);
    std::printf("VECTORS setfocus=%u remapped=%u bad=%u load=%u remapped=%u bad=%u\n", sf.cases, sf.remapped, sf.bad,
                ld.cases, ld.remapped, ld.bad);
    check(sf.bad == 0 && ld.bad == 0 && sf.remapped >= 81 && ld.remapped >= 81, "patched_vectors");
    // The stock menu values and the vanilla save default.
    check(run_setfocus((70u << 16) / 360u, 0x2768, true) && run_setfocus((90u << 16) / 360u, 0x3470, true) &&
              run_setfocus((100u << 16) / 360u, 0x3b6f, true) && run_load(0x4000, 0x3470, true) &&
              run_load(0x3470, 0x3470, true) && run_load(0x4001, 0x4001, true),
          "stock_values");
    // current_focus() and present() through a fixture slot (production: the admitted base+0x1a82e4). present() samples
    // on its first call and then every fl_sample_interval-th call; current_focus() returns the sample and reads nothing.
    {
        constexpr unsigned interval = fov::fl_sample_interval;
        unsigned long long frame = 0;
        // n Present calls; true when the sample landed on the first of them (the counter is at a multiple).
        auto presents = [&](unsigned n) {
            for (unsigned i = 0; i < n; ++i) fov::present(++frame);
        };
        const uint32_t configured = sites::constructor_focus(90);
        fov::fixture_registry_slot(0);
        check(fov::current_focus() == configured, "current_focus_without_slot_is_configured");
        presents(interval);
        check(fov::current_focus() == configured, "no_slot_sample_is_configured");
        fov::fixture_registry_slot(base + S::registry_slot_rva); // resets the counter: the next call samples
        set_focus(0x2768);
        check(fov::current_focus() == configured, "current_focus_before_a_sample_is_configured");
        const unsigned before = rows;
        set_focus(configured);
        SetLastError(sentinel_error);
        presents(1);
        const bool first = rows == before + 1 && GetLastError() == sentinel_error &&
                           std::strstr(last_row, "fov_confirm frame=") && std::strstr(last_row, " edition=fl registry=") &&
                           std::strstr(last_row, " focus=0x3470 expected=0x3470 match=1 kind=constructor vertical_deg=58.72 row=1/16") &&
                           fov::current_focus() == configured;
        // A change between samples is seen only at the next sample; current_focus() keeps the old one until then.
        set_focus(0x2768);
        presents(interval - 1);
        const bool between = rows == before + 1 && fov::current_focus() == configured;
        presents(1);
        const bool change = rows == before + 2 && fov::current_focus() == 0x2768 &&
                            std::strstr(last_row, " focus=0x2768 expected=0x3470 match=0 kind=remapped ");
        presents(interval - 1);
        set_focus(0x4000);
        presents(1);
        const bool vanilla_row = rows == before + 3 && std::strstr(last_row, " kind=vanilla ") && fov::current_focus() == 0x4000;
        presents(interval - 1);
        set_focus(0x9000); // implausible: the sample is not live, current_focus() falls back, no row
        presents(1);
        const bool implausible = rows == before + 3 && fov::current_focus() == configured;
        for (unsigned i = 0; i < 40; ++i) {
            presents(interval - 1);
            set_focus(0x3000 + i);
            presents(1);
        }
        const bool capped = rows == before + 16 && std::strstr(last_row, " row=16/16") && fov::current_focus() == 0x3027;
        check(first && between && change && vanilla_row && implausible && capped, "present_samples_every_interval_rows_capped",
              last_row);
        std::printf("SAMPLES interval=%u frames=%llu rows=%u\n", interval, frame, rows - before);
        fov::fixture_registry_slot(0);
    }
    shutdown_clean("shutdown_restores_all_three");
    const Tally after_sf = setfocus_vectors(false), after_ld = load_vectors(false);
    check(after_sf.bad == 0 && after_ld.bad == 0, "native_vectors_after_shutdown");

    // initialize() past the identity (seam: the fixture image's base): X3M_FOV=90 installs all three.
    {
        fov::fixture_base(base);
        set_setting(L"90");
        const unsigned before = rows;
        SetLastError(sentinel_error);
        const bool applied = fov::initialize();
        char want[260];
        std::snprintf(want, sizeof want,
                      "fov site=%08lx rva=01e08d edition=fl status=patched reason=ok value=0x3470 vertical_deg=58.72 "
                      "setting=90 write=plain registry=none setfocus=active setfocus_write=atomic load=active load_write=atomic",
                      static_cast<unsigned long>(base + S::write_rva));
        check(applied && GetLastError() == sentinel_error && rows == before + 1 && !std::strcmp(last_row, want) &&
                  only_sites_changed(),
              "init_installs_all_three", last_row);
        check(fov::initialize() && rows == before + 1, "init_again_no_rows");
        check(run_setfocus((80u << 16) / 360u, table[30], true), "init_install_remaps");
        shutdown_clean("init_install_restored");
        // A decimal setting on a vanilla value: the constructor value is moved one unit (shared collision rule).
        set_setting(L"78.5");
        check(fov::initialize() && imm_now() == sites::constructor_focus(78.5) && !sites::vanilla_focus(imm_now()),
              "init_decimal_off_vanilla", last_row);
        shutdown_clean("init_decimal_restored");
        set_setting(nullptr);
        fov::fixture_base(0);
    }

    // Partial installs (seam faults), all three or none.
    fault_read_fails = false;
    fault_read_at = base + S::write_rva, fault_read_count = 4, fault_read_skip = 0; // the literal's read-back
    failed_install("literal_readback_rolled_back", "patch_rolled_back", "none", "none", true);
    fault_read_at = base + S::setfocus_site_rva, fault_read_count = 5;
    failed_install("setfocus_readback_rolls_back_literal", "setfocus_failed", "readback_failed", "none", true);
    fault_push_at = base + S::setfocus_site_rva;
    failed_install("setfocus_chain_rolls_back_literal", "setfocus_failed", "chain_failed", "none", true);
    fault_read_at = base + S::load_site_rva, fault_read_count = 5;
    failed_install("load_readback_rolls_back_both", "load_failed", "rolled_back", "readback_failed", true);
    fault_push_at = base + S::load_site_rva;
    failed_install("load_chain_rolls_back_both", "load_failed", "rolled_back", "chain_failed", true);
    // The load fails and the INS_SetFocus rollback is refused: that jump stays registered, the literal goes back.
    fault_read_at = base + S::load_site_rva, fault_read_count = 5;
    fault_restore_at = base + S::setfocus_site_rva;
    failed_install("setfocus_rollback_refused", "load_failed", "rollback_failed", "readback_failed", false);
    check(fov::patched() && fov::setfocus_patched() && !fov::load_patched() && jump_at_site(S::setfocus_site_rva) &&
              imm_now() == sites::engine_focus && fov::configured_focus() == sites::engine_focus,
          "setfocus_rollback_refused_registered");
    check(!fov::install_base(base, sites::constructor_focus(90)) && !std::strcmp(fov::state(), "already_installed"),
          "registered_blocks_reinstall");
    SetLastError(sentinel_error);
    check(fov::shutdown() && windows_original() && !fov::patched() && GetLastError() == sentinel_error,
          "registered_setfocus_restored_by_shutdown", last_row);
    // The load's chain fails and the literal's restore cannot read it: the INS_SetFocus jump goes back, the literal stays
    // registered with our value. Its first four-byte read (the install read-back) passes, the second (the restore's)
    // fails; the read in settle_configured() is real again.
    {
        const uint32_t focus = sites::constructor_focus(90);
        fault_read_at = base + S::write_rva, fault_read_count = 4, fault_read_skip = 1, fault_read_fails = true;
        fault_push_at = base + S::load_site_rva;
        SetLastError(sentinel_error);
        const bool ok = fov::install_base(base, focus);
        check(!ok && GetLastError() == sentinel_error && !std::strcmp(fov::state(), "load_failed") &&
                  !std::strcmp(fov::load_state(), "chain_failed") && !std::strcmp(fov::setfocus_state(), "rolled_back") &&
                  fov::patched() && !fov::setfocus_patched() && !fov::load_patched() && imm_now() == focus &&
                  fov::configured_focus() == focus,
              "literal_rollback_unreadable_stays_registered", fov::state());
        fault_read_fails = false;
        SetLastError(sentinel_error);
        check(fov::shutdown() && windows_original() && !fov::patched() && GetLastError() == sentinel_error &&
                  std::strstr(last_row, " status=restored found=7034"),
              "registered_literal_restored_by_shutdown", last_row);
    }
    std::printf("FAULTS taken=%u\n", faults_taken);
    check(faults_taken == 9 && !fault_read_at && !fault_restore_at && !fault_push_at, "faults_taken");
    const Tally final_sf = setfocus_vectors(false), final_ld = load_vectors(false);
    check(final_sf.bad == 0 && final_ld.bad == 0 && windows_original(), "native_after_rollbacks");

    // Late: the window closed (the first Present) with nothing installed: refused, nothing emitted or written.
    engine_patch::close_install_window("fixture_late");
    refused("late_claim_refused", base, sites::constructor_focus(90), "late_claim");
    fov::fixture_base(base);
    initialize_row("init_late_claim", L"90", "status=refused reason=late_claim");
    fov::fixture_base(0);
    set_setting(nullptr);
    std::printf("ARENA used=%u capacity=%u\n", engine_patch::arena_used(), engine_patch::arena_capacity());
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
