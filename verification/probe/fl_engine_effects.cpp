// Original synthetic fixture of the FL engine-effects call redirects (src/engine/fl/engine_effects_fl.cpp,
// docs/architecture/engine-effects-modern.md "FL port, step 1"): no game code, no game bytes beyond the two windows in
// engine_effects_sites_fl.h, no launch. Built with -DX3M_EDITION_FL=1 by verification/analysis/test_fl_engine_effects.py
// (the module force-includes engine_effects_patch_fixture_shim.h for the read-back / restore faults) and run natively
// (WSL interop).
//
// A 64 KB-aligned private image stands in for X3FL.exe at `base`: window A at base+0x015054, window B at base+0x015085
// with its fixup dword = base+0x1a83b0, `push imm32; ret` jumps at the two joins (base+0x01507d after A's caller pop,
// base+0x0150af after B's call) and at the two callees (base+0x0150e0, base+0x013640) into recording fakes. A harness
// enters each site's CALL with the FL register/stack contract (A: ECX, EDX, eight dwords, obj [esp+8]; B: four dwords,
// obj [esp+0xc]), sentinels in EAX/EBX/ESI/EDI/EBP and DF set; the real window bytes then run the call and A's
// `add esp,0x20`.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../../src/engine/fl/engine_effects_fl.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/engine/fl/engine_effects_sites_fl.h"
namespace eep = x3m::engine_effects_patch;
namespace S = x3m::fl::engine_effects_fl;
using std::uint32_t;
unsigned checks = 0, failures = 0;
void check(bool value, const char* label, const char* detail = "") {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s %s\n", label, detail);
    }
}
char last_row[1024], prev_row[1024];
unsigned rows = 0;
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::memcpy(prev_row, last_row, sizeof prev_row);
    std::vsnprintf(last_row, sizeof last_row, format, args);
    va_end(args);
    ++rows;
    std::printf("LOG %s\n", last_row);
}
}
// The seam (engine_effects_patch_fixture_shim.h): one-shot faults, otherwise the real calls.
std::uintptr_t fault_read_at = 0, fault_restore_at = 0;
unsigned faults_taken = 0;
namespace x3m::engine_effects_fixture {
BOOL WINAPI virtual_protect(LPVOID address, SIZE_T size, DWORD protection, PDWORD previous) {
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

extern "C" {
// Harness inputs and outputs (absolute addresses in the asm).
uint32_t in_eax = 0, in_ebx = 0, in_ecx = 0, in_edx = 0, in_esi = 0, in_edi = 0, in_ebp = 0;
uint32_t in_obj = 0, in_eff = 0, in_pos = 0, in_slot7 = 0, in_k = 0, in_trail = 0;
uint32_t site_a = 0, site_b = 0, saved_esp = 0, esp_before_call = 0;
uint32_t out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags;
// The fakes: entry registers, EFLAGS, ESP and nine stack dwords from [esp]; one counter each.
uint32_t t_eax, t_ebx, t_ecx, t_edx, t_esi, t_edi, t_ebp, t_esp, t_flags, t_stack[9], t_calls_a = 0, t_calls_b = 0;
void fl_ee_call_a();
void fl_ee_call_b();
void fl_ee_return_a();
void fl_ee_return_b();
void fl_fake_target_a();
void fl_fake_target_b();
}
#define RECORD                                                                                                         \
    "movl %eax,_t_eax\n\tmovl %ebx,_t_ebx\n\tmovl %ecx,_t_ecx\n\tmovl %edx,_t_edx\n\tmovl %esi,_t_esi\n\t"             \
    "movl %edi,_t_edi\n\tmovl %ebp,_t_ebp\n\tmovl %esp,_t_esp\n\tpushfl\n\tpopl _t_flags\n\t"                          \
    "movl (%esp),%eax\n\tmovl %eax,_t_stack\n\tmovl 4(%esp),%eax\n\tmovl %eax,_t_stack+4\n\t"                          \
    "movl 8(%esp),%eax\n\tmovl %eax,_t_stack+8\n\tmovl 12(%esp),%eax\n\tmovl %eax,_t_stack+12\n\t"                     \
    "movl 16(%esp),%eax\n\tmovl %eax,_t_stack+16\n\tmovl 20(%esp),%eax\n\tmovl %eax,_t_stack+20\n\t"                   \
    "movl 24(%esp),%eax\n\tmovl %eax,_t_stack+24\n\tmovl 28(%esp),%eax\n\tmovl %eax,_t_stack+28\n\t"                   \
    "movl 32(%esp),%eax\n\tmovl %eax,_t_stack+32\n\t"
#define LOAD_REGS                                                                                                      \
    "movl _in_ebx,%ebx\n\tmovl _in_esi,%esi\n\tmovl _in_edi,%edi\n\tmovl _in_ebp,%ebp\n\t"
#define STORE_OUT                                                                                                      \
    "pushfl\n\tpopl _out_flags\n\tcld\n\tmovl %eax,_out_eax\n\tmovl %ebx,_out_ebx\n\tmovl %ecx,_out_ecx\n\t"          \
    "movl %edx,_out_edx\n\tmovl %esi,_out_esi\n\tmovl %edi,_out_edi\n\tmovl %ebp,_out_ebp\n\tmovl %esp,_out_esp\n\t"   \
    "movl _saved_esp,%esp\n\tpopfl\n\tpopal\n\tret\n\t"
asm(".text\n\t"
    // A: the eight dwords of 0x015060..0x015072 in the FL order, ECX/EDX, then the site's CALL.
    ".globl _fl_ee_call_a\n_fl_ee_call_a:\n\tpushal\n\tpushfl\n\tmovl %esp,_saved_esp\n\t" LOAD_REGS
    "pushl $0\n\tpushl _in_slot7\n\tpushl _in_pos\n\tpushl $0\n\tpushl $0\n\tpushl $0\n\tpushl _in_obj\n\t"
    "pushl _in_eff\n\tmovl _in_ecx,%ecx\n\tmovl _in_edx,%edx\n\tmovl _in_eax,%eax\n\tmovl %esp,_esp_before_call\n\t"
    "std\n\tjmp *_site_a\n\t"
    ".globl _fl_ee_return_a\n_fl_ee_return_a:\n\t" STORE_OUT
    // B: &pos, obj, trail, k (0x0150a6..0x0150a9), then the site's CALL.
    ".globl _fl_ee_call_b\n_fl_ee_call_b:\n\tpushal\n\tpushfl\n\tmovl %esp,_saved_esp\n\t" LOAD_REGS
    "pushl _in_pos\n\tpushl _in_obj\n\tpushl _in_trail\n\tpushl _in_k\n\t"
    "movl _in_ecx,%ecx\n\tmovl _in_edx,%edx\n\tmovl _in_eax,%eax\n\tmovl %esp,_esp_before_call\n\t"
    "std\n\tjmp *_site_b\n\t"
    ".globl _fl_ee_return_b\n_fl_ee_return_b:\n\t" STORE_OUT
    // The callees: A plain RET (caller pops), B RET 0x10; both clobber EAX/ECX/EDX as a callee may.
    ".globl _fl_fake_target_a\n_fl_fake_target_a:\n\t" RECORD
    "incl _t_calls_a\n\tmovl $0xaaaa0001,%eax\n\tmovl $0xc1c1c1c1,%ecx\n\tmovl $0xd1d1d1d1,%edx\n\tret\n\t"
    ".globl _fl_fake_target_b\n_fl_fake_target_b:\n\t" RECORD
    "incl _t_calls_b\n\tmovl $0xbbbb0001,%eax\n\tmovl $0xc2c2c2c2,%ecx\n\tmovl $0xd2d2d2d2,%edx\n\tret $0x10\n\t");

std::uintptr_t base = 0;
unsigned char* image = nullptr;
constexpr unsigned image_size = 0x20000;
constexpr uint32_t sentinel_error = 0x2a2a1357;
void jump_at(uint32_t rva, void* to) { // push imm32; ret
    unsigned char* p = image + rva;
    p[0] = 0x68;
    const uint32_t v = uint32_t(reinterpret_cast<std::uintptr_t>(to));
    std::memcpy(p + 1, &v, 4);
    p[5] = 0xc3;
}
void writable(bool on) {
    DWORD old = 0;
    VirtualProtect(image, image_size, on ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ, &old);
}
void lay_out() {
    writable(true);
    std::memset(image, 0xcc, image_size);
    std::memcpy(image + S::window_a_rva, S::expected_window_a, S::window_a_length);
    std::memcpy(image + S::window_b_rva, S::expected_window_b, S::window_b_length);
    const uint32_t fixup = uint32_t(base + S::config_slot_rva);
    std::memcpy(image + S::window_b_rva + S::window_b_fixup_offset, &fixup, 4);
    jump_at(S::join_a_rva, reinterpret_cast<void*>(&fl_ee_return_a));
    jump_at(S::return_b_rva, reinterpret_cast<void*>(&fl_ee_return_b));
    jump_at(S::target_a_rva, reinterpret_cast<void*>(&fl_fake_target_a));
    jump_at(S::target_b_rva, reinterpret_cast<void*>(&fl_fake_target_b));
    writable(false);
    FlushInstructionCache(GetCurrentProcess(), image, image_size);
}
unsigned char window_a_now[S::window_a_length], window_b_now[S::window_b_length];
bool windows_original() {
    unsigned char b[S::window_b_length];
    std::memcpy(b, image + S::window_b_rva, S::window_b_length);
    return !std::memcmp(image + S::window_a_rva, window_a_now, S::window_a_length) &&
           !std::memcmp(b, window_b_now, S::window_b_length);
}
uint32_t call_target(uint32_t site_rva) {
    uint32_t rel = 0;
    std::memcpy(&rel, image + site_rva + 1, 4);
    return uint32_t(base + site_rva + 5 + rel);
}
uint32_t u32(const void* p) {
    return uint32_t(reinterpret_cast<std::uintptr_t>(p));
}

struct Obj {
    unsigned char bytes[0x60];
};
Obj ship{}, missile{};
void set_class(Obj& o, uint16_t c) {
    std::memset(o.bytes, 0x5c, sizeof o.bytes);
    std::memcpy(o.bytes + S::class_offset, &c, 2);
}
void inputs(const Obj& o) {
    in_eax = 0xeaeaeaea, in_ebx = 0x0000000b, in_ecx = 0x11110000, in_edx = 0x22220003, in_esi = 0x5e5e5e5e;
    in_edi = u32(&o), in_ebp = 0xebebebeb, in_obj = u32(&o), in_eff = 0x0000e0e0, in_pos = 0x00f0f0f0;
    in_slot7 = 0x77777777, in_k = 3, in_trail = 0x0000a1a1;
}
// One call through a site; true when every caller-side invariant holds (live registers, ESP, DF, LastError).
bool caller_kept(const char* label, bool a) {
    SetLastError(sentinel_error);
    a ? fl_ee_call_a() : fl_ee_call_b();
    const bool ok = GetLastError() == sentinel_error && out_ebx == in_ebx && out_esi == in_esi && out_edi == in_edi &&
                    out_ebp == in_ebp && out_esp == saved_esp && (out_flags & 0x400);
    char detail[200];
    std::snprintf(detail, sizeof detail, "esp=%08x/%08x ebx=%08x esi=%08x edi=%08x ebp=%08x flags=%08x", out_esp,
                  saved_esp, out_ebx, out_esi, out_edi, out_ebp, out_flags);
    check(ok, label, detail);
    return ok;
}
void run_cases(bool redirected) {
    // A, class 7: skipped when redirected (EAX 0, the callee not entered, ECX/EDX untouched, caller pop only).
    inputs(ship);
    unsigned before = t_calls_a;
    caller_kept(redirected ? "a_ship_frame" : "a_ship_native_frame", true);
    if (redirected)
        check(t_calls_a == before && out_eax == 0 && out_ecx == in_ecx && out_edx == in_edx, "a_ship_skipped");
    else
        check(t_calls_a == before + 1 && out_eax == 0xaaaa0001, "a_ship_native_calls");
    // A, class 10: forwarded with ECX, EDX, EAX, EBX, ESI, EDI, EBP and the eight dwords exactly as the call left them.
    inputs(missile);
    before = t_calls_a;
    caller_kept(redirected ? "a_missile_frame" : "a_missile_native_frame", true);
    const uint32_t expect_a[9] = {uint32_t(base + S::return_a_rva), in_eff, in_obj, 0, 0, 0, in_pos, in_slot7, 0};
    check(t_calls_a == before + 1 && out_eax == 0xaaaa0001 && t_ecx == in_ecx && t_edx == in_edx && t_eax == in_eax &&
              t_ebx == in_ebx && t_esi == in_esi && t_edi == in_edi && t_ebp == in_ebp &&
              t_esp == esp_before_call - 4 && !std::memcmp(t_stack, expect_a, sizeof expect_a) && (t_flags & 0x400),
          redirected ? "a_missile_forwarded_exact" : "a_missile_native_exact");
    // B, class 7: skipped with RET 0x10.
    inputs(ship);
    before = t_calls_b;
    caller_kept(redirected ? "b_ship_frame" : "b_ship_native_frame", false);
    if (redirected)
        check(t_calls_b == before && out_eax == 0 && out_ecx == in_ecx && out_edx == in_edx, "b_ship_skipped");
    else
        check(t_calls_b == before + 1 && out_eax == 0xbbbb0001, "b_ship_native_calls");
    // B, class 10: forwarded exactly; the callee's RET 0x10 balances the caller.
    inputs(missile);
    before = t_calls_b;
    caller_kept(redirected ? "b_missile_frame" : "b_missile_native_frame", false);
    const uint32_t expect_b[5] = {uint32_t(base + S::return_b_rva), in_k, in_trail, in_obj, in_pos};
    check(t_calls_b == before + 1 && out_eax == 0xbbbb0001 && t_ecx == in_ecx && t_edx == in_edx && t_eax == in_eax &&
              t_ebx == in_ebx && t_esi == in_esi && t_edi == in_edi && t_ebp == in_ebp &&
              t_esp == esp_before_call - 4 && !std::memcmp(t_stack, expect_b, sizeof expect_b) && (t_flags & 0x400),
          redirected ? "b_missile_forwarded_exact" : "b_missile_native_exact");
}
bool refused(const char* label, std::uintptr_t at, const char* state, const char* a_state, const char* b_state) {
    SetLastError(sentinel_error);
    const bool ok = eep::install_base(at);
    const bool pass = !ok && GetLastError() == sentinel_error && !eep::installed() && !std::strcmp(eep::state(), state) &&
                      !std::strcmp(eep::site_state(0), a_state) && !std::strcmp(eep::site_state(1), b_state) &&
                      windows_original() && call_target(S::a_site_rva) == base + S::target_a_rva &&
                      call_target(S::b_site_rva) == base + S::target_b_rva;
    char detail[200];
    std::snprintf(detail, sizeof detail, "state=%s a=%s b=%s", eep::state(), eep::site_state(0), eep::site_state(1));
    if (label) check(pass, label, detail);
    return pass;
}
void set_setting(const wchar_t* value) {
    SetEnvironmentVariableW(L"X3M_ENGINE_EFFECTS", value);
}
bool initialize_row(const char* label, const wchar_t* value, const char* expect) {
    set_setting(value);
    const unsigned before = rows;
    SetLastError(sentinel_error);
    const bool applied = eep::initialize();
    const bool ok = !applied && GetLastError() == sentinel_error && rows == before + 2 && std::strstr(last_row, expect) &&
                    std::strstr(last_row, "engine_effects_patch site=B ") && std::strstr(last_row, " edition=fl ") &&
                    windows_original();
    check(ok, label, last_row);
    return ok;
}

int main() {
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    base = reinterpret_cast<std::uintptr_t>(image);
    check(image && !(base & 0xffff), "image_64k_aligned");
    if (!image) return 1;
    lay_out();
    std::memcpy(window_a_now, image + S::window_a_rva, S::window_a_length);
    std::memcpy(window_b_now, image + S::window_b_rva, S::window_b_length);
    site_a = uint32_t(base + S::a_site_rva);
    site_b = uint32_t(base + S::b_site_rva);
    set_class(ship, 7);
    set_class(missile, 10);
    // The window identity: FNV-1a over A, and over B with its fixup zeroed (the producer's convention).
    unsigned char masked[S::window_b_length];
    std::memcpy(masked, window_b_now, sizeof masked);
    std::memset(masked + S::window_b_fixup_offset, 0, 4);
    std::printf("WINDOWS a_fnv1a=%08x b_fnv1a_masked=%08x b_fixup=%08x base=%08x\n",
                S::fnv1a(window_a_now, S::window_a_length), S::fnv1a(masked, S::window_b_length),
                uint32_t(base + S::config_slot_rva), uint32_t(base));
    check(S::fnv1a(window_a_now, S::window_a_length) == 0x5a69143c && S::fnv1a(masked, S::window_b_length) == 0x601f528c,
          "window_hashes_measured");
    // The native image runs both callees for both classes.
    run_cases(false);

    // initialize(): the fixture's main module is not X3FL.exe (module_base 0); native, invalid and long settings.
    initialize_row("init_unset_executable_mismatch", nullptr,
                   "state=executable_mismatch reason=executable_mismatch mode=plumes setting=- write=none");
    initialize_row("init_off_executable_mismatch", L"off", "reason=executable_mismatch mode=off setting=off write=none");
    initialize_row("init_native", L"native", "state=native reason=native mode=native setting=native write=none");
    initialize_row("init_invalid", L"Plumes", "state=invalid_setting reason=invalid_setting mode=- setting=Plumes");
    initialize_row("init_too_long", L"plumesplumesplumes", "state=too_long reason=too_long mode=- setting=?");
    set_setting(nullptr);

    // Refusals before any write.
    refused("invalid_base_zero", 0, "invalid_site", "invalid_site", "invalid_site");
    refused("invalid_base_unaligned", base + 0x1000, "invalid_site", "invalid_site", "invalid_site");
    // Every single-bit change of either window (B's fixup included) is refused, and nothing is written.
    unsigned flips_a = 0, refused_a = 0, flips_b = 0, refused_b = 0, fixup_flips = 0, fixup_refused = 0;
    for (unsigned w = 0; w < 2; ++w) {
        const unsigned length = w ? S::window_b_length : S::window_a_length;
        unsigned char* window = image + (w ? S::window_b_rva : S::window_a_rva);
        for (unsigned i = 0; i < length * 8; ++i) {
            writable(true);
            window[i / 8] ^= static_cast<unsigned char>(1u << (i % 8));
            writable(false);
            unsigned char flipped_a[S::window_a_length], flipped_b[S::window_b_length];
            std::memcpy(flipped_a, image + S::window_a_rva, sizeof flipped_a);
            std::memcpy(flipped_b, image + S::window_b_rva, sizeof flipped_b);
            SetLastError(sentinel_error);
            const bool ok = eep::install_base(base);
            const bool fixup = w && i / 8 >= S::window_b_fixup_offset && i / 8 < S::window_b_fixup_offset + 4;
            const bool pass = !ok && GetLastError() == sentinel_error &&
                              !std::strcmp(eep::state(), w ? "bytes_mismatch_b" : "bytes_mismatch_a") &&
                              !std::memcmp(image + S::window_a_rva, flipped_a, sizeof flipped_a) &&
                              !std::memcmp(image + S::window_b_rva, flipped_b, sizeof flipped_b);
            writable(true);
            window[i / 8] ^= static_cast<unsigned char>(1u << (i % 8));
            writable(false);
            (w ? flips_b : flips_a) += 1;
            (w ? refused_b : refused_a) += pass;
            fixup_flips += fixup;
            fixup_refused += fixup && pass;
        }
    }
    std::printf("FLIPS a=%u refused_a=%u b=%u refused_b=%u fixup=%u fixup_refused=%u\n", flips_a, refused_a, flips_b,
                refused_b, fixup_flips, fixup_refused);
    check(refused_a == flips_a && flips_a == 328 && refused_b == flips_b && flips_b == 336 && fixup_refused == 32,
          "every_window_bit_refused");
    check(windows_original(), "windows_restored_after_flips");
    // B's fixup zeroed (the hashing form, not a relocated image) or rebased to another base: refused.
    {
        const uint32_t zero = 0, other = uint32_t(base + 0x10000 + S::config_slot_rva);
        for (uint32_t value : {zero, other}) {
            writable(true);
            std::memcpy(image + S::window_b_rva + S::window_b_fixup_offset, &value, 4);
            writable(false);
            SetLastError(sentinel_error);
            const bool ok = eep::install_base(base);
            check(!ok && !std::strcmp(eep::state(), "bytes_mismatch_b") && !std::strcmp(eep::site_state(0), "not_attempted") &&
                      !std::strcmp(eep::site_state(1), "bytes_mismatch"),
                  value ? "fixup_other_base_refused" : "fixup_zeroed_refused", eep::state());
        }
        writable(true);
        std::memcpy(image + S::window_b_rva, window_b_now, S::window_b_length);
        writable(false);
        // The same window B at another base is refused too (its fixup belongs to `base`).
        check(!S::plan_b(window_b_now, base) && S::plan_b(window_b_now, base + 0x10000) && !S::plan_a(window_a_now),
              "plan_binds_fixup_to_base");
    }

    // Install: B atomic (one qword), A plain (crosses a qword); the five bytes call the stubs.
    SetLastError(sentinel_error);
    bool ok = eep::install_base(base);
    check(ok && GetLastError() == sentinel_error && eep::installed() && !std::strcmp(eep::state(), "ok") &&
              !std::strcmp(eep::site_state(0), "active") && !std::strcmp(eep::site_state(1), "active") &&
              !std::strcmp(eep::write_path(0), "plain") && !std::strcmp(eep::write_path(1), "atomic") &&
              call_target(S::a_site_rva) == u32(reinterpret_cast<void*>(&x3m_engine_effects_stub_a)) &&
              call_target(S::b_site_rva) == u32(reinterpret_cast<void*>(&x3m_engine_effects_stub_b)) &&
              x3m_engine_effects_continue_a == base + S::target_a_rva &&
              x3m_engine_effects_continue_b == base + S::target_b_rva,
          "installed_b_atomic_a_plain", eep::state());
    check(!std::memcmp(image + S::window_a_rva, window_a_now, S::site_a_offset) &&
              !std::memcmp(image + S::window_a_rva + S::site_a_offset + 5, window_a_now + S::site_a_offset + 5, 3) &&
              !std::memcmp(image + S::window_b_rva, window_b_now, S::site_b_offset),
          "only_the_two_calls_changed");
    check(!eep::install_base(base) && !std::strcmp(eep::state(), "already_installed") && eep::installed(),
          "second_install_refused");
    run_cases(true);
    // Timing: the skip and forward paths (harness included), for the report only.
    LARGE_INTEGER f{}, t0{}, t1{}, t2{};
    QueryPerformanceFrequency(&f);
    inputs(ship);
    QueryPerformanceCounter(&t0);
    for (unsigned i = 0; i < 100000; ++i) fl_ee_call_a();
    QueryPerformanceCounter(&t1);
    inputs(missile);
    for (unsigned i = 0; i < 100000; ++i) fl_ee_call_a();
    QueryPerformanceCounter(&t2);
    std::printf("TIMING skip_a_ns=%.1f forward_a_ns=%.1f\n", double(t1.QuadPart - t0.QuadPart) * 1e9 / f.QuadPart / 1e5,
                double(t2.QuadPart - t1.QuadPart) * 1e9 / f.QuadPart / 1e5);

    // shutdown: A then B back, only over our calls; the image is native again.
    SetLastError(sentinel_error);
    check(eep::shutdown() && GetLastError() == sentinel_error && !eep::installed() && windows_original() &&
              !std::strcmp(eep::state(), "restored") && !std::strcmp(eep::site_state(0), "restored") &&
              !std::strcmp(eep::site_state(1), "restored"),
          "shutdown_restores_both", eep::state());
    run_cases(false);

    // initialize() past the identity (seam: the fixture image's base instead of fl::module_base()): unset = plumes
    // reaches install_base, both redirects go live, the two rows carry the live VA, the RVA and the write paths.
    {
        x3m::engine_effects_patch::fixture_base(base);
        set_setting(nullptr);
        const unsigned before = rows;
        SetLastError(sentinel_error);
        const bool applied = eep::initialize();
        char want_a[160], want_b[160];
        std::snprintf(want_a, sizeof want_a,
                      "engine_effects_patch site=A va=%08lx rva=015075 edition=fl state=active reason=ok mode=plumes "
                      "setting=- write=plain",
                      static_cast<unsigned long>(base + S::a_site_rva));
        std::snprintf(want_b, sizeof want_b,
                      "engine_effects_patch site=B va=%08lx rva=0150aa edition=fl state=active reason=ok mode=plumes "
                      "setting=- write=atomic",
                      static_cast<unsigned long>(base + S::b_site_rva));
        check(applied && GetLastError() == sentinel_error && rows == before + 2 && !std::strcmp(prev_row, want_a) &&
                  !std::strcmp(last_row, want_b) && eep::installed(),
              "init_nonzero_base_installs", last_row);
        check(eep::initialize() && rows == before + 2, "init_again_no_rows");
        run_cases(true);
        check(eep::shutdown() && windows_original(), "init_install_restored", eep::state());
        x3m::engine_effects_patch::fixture_base(0);
    }

    // Partial installs (seam faults): A's read-back fails after B is live -> A rolled back by the module, B restored.
    fault_read_at = base + S::a_site_rva;
    refused("a_readback_rolls_back_b", base, "readback_rolled_back", "readback_rolled_back", "rolled_back");
    // B's read-back fails -> B rolled back, A never attempted.
    fault_read_at = base + S::b_site_rva;
    refused("b_readback_rolled_back", base, "readback_rolled_back", "not_attempted", "readback_rolled_back");
    check(faults_taken == 2 && !fault_read_at, "readback_faults_taken");
    // A fails and B's restore is refused: rollback_failed, B stays registered (already_installed), shutdown restores it.
    fault_read_at = base + S::a_site_rva;
    fault_restore_at = base + S::b_site_rva;
    SetLastError(sentinel_error);
    ok = eep::install_base(base);
    check(!ok && GetLastError() == sentinel_error && !eep::installed() && !std::strcmp(eep::state(), "rollback_failed") &&
              !std::strcmp(eep::site_state(0), "readback_rolled_back") &&
              !std::strcmp(eep::site_state(1), "rollback_failed") &&
              call_target(S::b_site_rva) == u32(reinterpret_cast<void*>(&x3m_engine_effects_stub_b)) &&
              call_target(S::a_site_rva) == base + S::target_a_rva && faults_taken == 4,
          "b_restore_refused_registered", eep::state());
    check(!eep::install_base(base) && !std::strcmp(eep::state(), "already_installed"), "registered_refuses_reinstall");
    {
        // The registered B alone: ships lose the trail call only; A still native.
        inputs(ship);
        const unsigned a = t_calls_a, b = t_calls_b;
        fl_ee_call_a();
        fl_ee_call_b();
        check(t_calls_a == a + 1 && t_calls_b == b && out_esp == saved_esp, "b_alone_skips_only_b");
    }
    check(eep::shutdown() && windows_original() && !std::strcmp(eep::state(), "restored"), "registered_b_restored",
          eep::state());

    // Late window: after the first Present nothing is claimed (A's plain copy must never race the routine).
    x3m::engine_patch::close_install_window("fixture_present");
    refused("late_claim_refused", base, "late_claim", "late_claim", "late_claim");
    run_cases(false);

    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
