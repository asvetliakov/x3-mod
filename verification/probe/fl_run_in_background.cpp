// Native fixture of the FL run in background (src/engine/fl/run_in_background_fl.cpp, run_in_background_sites_fl.h,
// inline_patch_fl.h; docs/reverse-engineering/fl/pause-background.md, "Port (2026-11-03)"): no game code beyond the two
// windows of run_in_background_sites_fl.h (X3FL.exe's bytes, verify_run_in_background_sites_fl.py), no launch. Built
// with -DX3M_EDITION_FL=1 -DX3M_RUN_IN_BACKGROUND_FL_FIXTURE by verification/analysis/test_fl_run_in_background.py and
// run natively (WSL interop).
//
// A 64 KB-aligned private image stands in for X3FL.exe at `base`: the startup decision window at base+0x0776c2 and the
// message pump's gate at base+0x0ec632, their fixups relocated to this base (input slot base+0x1a8478 -> a fake input
// block, value name base+0x18b9e8 -> "RunInBackground", active flag base+0x1aaf34). `jmp rel32` stand-ins: the
// registry read base+0x0d8850 (records ECX, returns the scripted value, clobbers ECX/EDX), the input setup
// base+0x100590 (records every register, EFLAGS, ESP and its return address, then returns), the window's end
// base+0x077701, and the pump's blocking (base+0x0ec651, GetMessageA) and non-blocking (base+0x0ec6f8, PeekMessageA)
// paths. The harness enters the decision with EBP = a startup frame whose [EBP+0x60] is the choice, sentinels in every
// other register and LastError set. Code pages are PAGE_EXECUTE_READ, as in the game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../../src/engine/fl/inline_patch_fl.h"
#include "../../src/engine/fl/run_in_background_fl.h"
#include "../../src/engine/fl/run_in_background_sites_fl.h"
#include "../../src/proxy/engine_patch.h"
namespace R = x3m::fl::run_in_background_fl;
namespace I = x3m::fl::inline_patch_fl;
namespace rib = x3m::run_in_background;
using std::int32_t;
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
extern "C" {
uint32_t in_ebx, in_ecx, in_edx, in_esi, in_edi, in_ebp, in_flags, in_entry, in_registry;
uint32_t out_kind, out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags;
uint32_t cs_eax, cs_ebx, cs_ecx, cs_edx, cs_esi, cs_edi, cs_ebp, cs_esp, cs_flags, cs_ret, cs_calls;
uint32_t reg_calls, reg_ecx, saved_esp, entry_esp;
void frb_run();
void frb_exit();
void frb_blocking();
void frb_nonblocking();
void frb_registry();
void frb_setup();
}
asm(".text\n\t"
    ".globl _frb_run\n_frb_run:\n\t"
    "pushal\n\tpushfl\n\tmovl %esp,_saved_esp\n\t"
    "andl $-16,%esp\n\tsubl $0x100,%esp\n\tmovl %esp,_entry_esp\n\t"
    "movl _in_ebx,%ebx\n\tmovl _in_ecx,%ecx\n\tmovl _in_edx,%edx\n\tmovl _in_esi,%esi\n\tmovl _in_edi,%edi\n\t"
    "movl _in_ebp,%ebp\n\tpushl _in_flags\n\tmovl $0xeaeaeaea,%eax\n\tpopfl\n\tjmp *_in_entry\n\t"
    ".globl _frb_exit\n_frb_exit:\n\tmovl $1,_out_kind\n\tjmp 1f\n\t"
    ".globl _frb_blocking\n_frb_blocking:\n\tmovl $2,_out_kind\n\tjmp 1f\n\t"
    ".globl _frb_nonblocking\n_frb_nonblocking:\n\tmovl $3,_out_kind\n"
    "1:\n\tmovl %eax,_out_eax\n\tpushfl\n\tpopl _out_flags\n\tcld\n\t"
    "movl %ebx,_out_ebx\n\tmovl %ecx,_out_ecx\n\tmovl %edx,_out_edx\n\tmovl %esi,_out_esi\n\t"
    "movl %edi,_out_edi\n\tmovl %ebp,_out_ebp\n\tmovl %esp,_out_esp\n\t"
    "movl _saved_esp,%esp\n\tpopfl\n\tpopal\n\tret\n\t"
    // The registry read 0x0d8850: ECX = the value name; returns the scripted DWORD; ECX/EDX clobbered.
    ".globl _frb_registry\n_frb_registry:\n\tincl _reg_calls\n\tmovl %ecx,_reg_ecx\n\tmovl _in_registry,%eax\n\t"
    "movl $0xc7c7c7c7,%ecx\n\tmovl $0xd7d7d7d7,%edx\n\tret\n\t"
    // The input setup 0x100590: every register, EFLAGS, ESP and the return address recorded; returns 0x00c0ffee.
    ".globl _frb_setup\n_frb_setup:\n\tmovl %eax,_cs_eax\n\tpushfl\n\tpopl _cs_flags\n\tincl _cs_calls\n\t"
    "movl %ebx,_cs_ebx\n\tmovl %ecx,_cs_ecx\n\tmovl %edx,_cs_edx\n\tmovl %esi,_cs_esi\n\tmovl %edi,_cs_edi\n\t"
    "movl %ebp,_cs_ebp\n\tmovl %esp,_cs_esp\n\tmovl (%esp),%eax\n\tmovl %eax,_cs_ret\n\tmovl $0x00c0ffee,%eax\n\tret\n\t");

std::uintptr_t base = 0;
unsigned char* image = nullptr;
constexpr unsigned image_size = 0x1b0000;
constexpr uint32_t sentinel_error = 0x2a2a1357, arith = 0x8d5, df = 0x400;
alignas(16) unsigned char input[0x500];
alignas(16) unsigned char frame[0x100];
uint32_t u32(const void* p) {
    return uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
void put32(unsigned char* p, uint32_t v) {
    std::memcpy(p, &v, 4);
}
uint32_t get32(const unsigned char* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
void code_pages(bool writable) {
    DWORD old = 0;
    for (uint32_t rva : {0x077000u, 0x0d8000u, 0x0ec000u, 0x100000u})
        VirtualProtect(image + rva, 0x1000, writable ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ, &old);
}
void exit_at(uint32_t rva, void* to) { // jmp rel32
    image[rva] = 0xe9;
    put32(image + rva + 1, u32(to) - uint32_t(base + rva + 5));
}
void lay_window(const R::Window& w) {
    std::memcpy(image + w.rva, w.bytes, w.length);
    for (unsigned r = 0; r < w.reloc_count; ++r) put32(image + w.rva + w.reloc_offset[r], uint32_t(base + w.reloc_target[r]));
}
void lay_out() {
    code_pages(true);
    for (uint32_t rva : {0x077000u, 0x0d8000u, 0x0ec000u, 0x100000u}) std::memset(image + rva, 0xcc, 0x1000);
    for (const R::Window& w : R::windows) lay_window(w);
    exit_at(R::after_window_rva, reinterpret_cast<void*>(&frb_exit));
    exit_at(R::registry_read_rva, reinterpret_cast<void*>(&frb_registry));
    exit_at(R::setup_rva, reinterpret_cast<void*>(&frb_setup));
    exit_at(R::pump_blocking_rva, reinterpret_cast<void*>(&frb_blocking));
    exit_at(R::pump_nonblocking_rva, reinterpret_cast<void*>(&frb_nonblocking));
    put32(image + R::input_slot_rva, u32(input));
    std::memcpy(image + R::value_name_rva, "RunInBackground", 16);
    code_pages(false);
    FlushInstructionCache(GetCurrentProcess(), image, image_size);
}
unsigned char pristine_decision[63], pristine_gate[31];
bool windows_pristine() {
    return !std::memcmp(image + R::windows[0].rva, pristine_decision, 63) &&
           !std::memcmp(image + R::windows[1].rva, pristine_gate, 31);
}
bool site_is(const unsigned char* bytes) {
    return !std::memcmp(image + R::site_rva, bytes, R::site_length);
}
DWORD protection_at(uint32_t rva) {
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(image + rva, &mbi, sizeof mbi);
    return mbi.Protect;
}

struct Out {
    uint32_t kind, word, eax, ebx, ecx, edx, esi, edi, ebp, esp_off, flags, error;
    uint32_t c_eax, c_ebx, c_ecx, c_edx, c_esi, c_edi, c_ebp, c_esp_off, c_flags, c_ret, c_calls, r_calls, r_ecx;
};
// One pass through the real decision window: seed flags word, the choice local, the registry value.
Out decide(uint32_t seed, int32_t choice, uint32_t registry, uint32_t flags = 0x0d5) {
    std::memset(input, 0x5a, sizeof input);
    put32(input, seed);
    std::memset(frame, 0x3c, sizeof frame);
    put32(frame + R::choice_offset, uint32_t(choice));
    cs_calls = reg_calls = 0, reg_ecx = 0;
    in_ebx = 0xb0b0b0b0, in_ecx = 0xc0c0c0c0, in_edx = 0xd0d0d0d0, in_esi = 0x51515151, in_edi = 0xd1d1d1d1;
    in_ebp = u32(frame), in_flags = flags, in_entry = uint32_t(base + R::windows[0].rva), in_registry = registry;
    SetLastError(sentinel_error);
    frb_run();
    return Out{out_kind, get32(input), out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp,
               entry_esp - out_esp, out_flags, GetLastError(), cs_eax, cs_ebx, cs_ecx, cs_edx, cs_esi, cs_edi, cs_ebp,
               entry_esp - cs_esp, cs_flags, cs_ret, cs_calls, reg_calls, reg_ecx};
}
// One pass through the real pump gate: inactive/active and the current flags word; 2 = blocking, 3 = non-blocking.
uint32_t pump(uint32_t word, uint32_t active) {
    put32(input, word);
    code_pages(true);
    put32(image + R::active_rva, active);
    code_pages(false);
    in_ebx = 0xb0b0b0b0, in_ecx = 0xc0c0c0c0, in_edx = 0xd0d0d0d0, in_esi = 0x51515151, in_edi = 0xd1d1d1d1;
    in_ebp = u32(frame), in_flags = 0x0d5, in_entry = uint32_t(base + R::windows[1].rva);
    SetLastError(sentinel_error);
    frb_run();
    return GetLastError() == sentinel_error ? out_kind : 0;
}
const int32_t choices[] = {-1, 0, 1, 2, -2, 0x7fffffff};
const uint32_t registries[] = {0, 1, 5};
const uint32_t seeds[] = {0, 0x4000, 0x0105, 0xffffbfffu, 0xffffffffu};
// Every (seed, choice, registry) through the window; the word against decide(); the setup call's view.
struct Tally {
    unsigned cases = 0, bad = 0, set = 0;
};
Tally sweep(bool patched, Out* records, unsigned capacity) {
    Tally t;
    for (uint32_t seed : seeds)
        for (int32_t choice : choices)
            for (uint32_t registry : registries) {
                if (choice != -1 && registry != 0) continue; // the registry is read only for -1
                const Out o = decide(seed, choice, registry);
                const uint32_t want = R::decide(seed, choice, registry, patched);
                const bool ok = o.kind == 1 && o.word == want && o.c_calls == 1 && o.r_calls == (choice == -1 ? 1u : 0u) &&
                                (choice != -1 || o.r_ecx == uint32_t(base + R::value_name_rva)) &&
                                o.c_ret == uint32_t(base + R::after_window_rva) && o.c_eax == u32(input) &&
                                o.c_ebx == 0xb0b0b0b0 && o.c_esi == 0x51515151 && o.c_edi == 0xd1d1d1d1 &&
                                o.c_ebp == u32(frame) && o.c_esp_off == 4 && !(o.c_flags & df) && o.eax == 0x00c0ffee &&
                                o.esp_off == 0 && o.error == sentinel_error;
                if (records && t.cases < capacity) records[t.cases] = o;
                ++t.cases;
                if (!ok) ++t.bad;
                if (o.word & R::run_in_background_bit) ++t.set;
            }
    return t;
}

// A buffer-backed Ops for the two-byte span's failure paths.
struct FakeOps {
    static constexpr std::uint32_t writable = 0x40;
    unsigned char* mem;
    std::uintptr_t origin;
    bool corrupt_write = false, refuse_rollback_write = false;
    unsigned protect_calls = 0, protect_fail_call = 0, writes = 0;
    bool read(std::uintptr_t at, unsigned char* out, unsigned n) {
        std::memcpy(out, mem + (at - origin), n);
        return true;
    }
    bool protect(std::uintptr_t, unsigned, std::uint32_t, std::uint32_t* previous) {
        ++protect_calls;
        if (protect_fail_call && protect_calls >= protect_fail_call) return false;
        *previous = 0x20;
        return true;
    }
    bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic) {
        ++writes;
        *atomic = true;
        if (refuse_rollback_write && writes == 2) return false;
        std::memcpy(mem + (at - origin), bytes, n);
        if (corrupt_write && writes == 1) mem[at - origin] ^= 0x10;
        return true;
    }
};
void span_sequences() {
    unsigned char mem[2];
    bool live = false, atomic = false;
    std::uint32_t protection = 0;
    const std::uintptr_t at = 0x004776ec;
    std::memcpy(mem, R::original, 2);
    FakeOps a{mem, at};
    a.corrupt_write = true;
    const char* r = I::install(a, at, R::original, R::patched, 2, &live, &atomic, &protection);
    check(!std::strcmp(r, "patch_rolled_back") && !live && !std::memcmp(mem, R::original, 2), "span_readback_rolled_back", r);
    std::memcpy(mem, R::original, 2);
    FakeOps b{mem, at};
    b.corrupt_write = true;
    b.refuse_rollback_write = true;
    r = I::install(b, at, R::original, R::patched, 2, &live, &atomic, &protection);
    check(!std::strcmp(r, "rollback_failed") && live, "span_rollback_failed_registered", r);
    std::memcpy(mem, R::original, 2);
    FakeOps c{mem, at};
    c.protect_fail_call = 2;
    r = I::install(c, at, R::original, R::patched, 2, &live, &atomic, &protection);
    check(!std::strcmp(r, "rollback_unprotected") && !live && !std::memcmp(mem, R::original, 2), "span_reprotect_refused", r);
    std::memcpy(mem, R::patched, 2);
    mem[1] = 0x74;
    FakeOps e{mem, at};
    r = I::restore(e, at, R::original, R::patched, 2, 0x20);
    check(!std::strcmp(r, "restore_not_owned") && e.writes == 0, "span_restore_not_owned", r);
}
void set_env(const wchar_t* name, const wchar_t* value) {
    SetEnvironmentVariableW(name, value);
}
void initialize_row(const char* label, const wchar_t* setting, const char* needle, bool patched) {
    set_env(L"X3M_RUN_IN_BACKGROUND", setting);
    const unsigned before = rows;
    SetLastError(sentinel_error);
    const bool applied = rib::initialize();
    const bool error_kept = GetLastError() == sentinel_error;
    const bool row = rows == before + 1 && std::strstr(last_row, needle) && std::strstr(last_row, " edition=fl ") &&
                     std::strstr(last_row, " rva=0x0776ec ") && std::strstr(last_row, " handler=none");
    const bool bytes = patched ? site_is(R::patched) : windows_pristine();
    check(applied == patched && error_kept && row && bytes, label, last_row);
    if (patched) {
        SetLastError(sentinel_error);
        check(rib::shutdown() && windows_pristine() && GetLastError() == sentinel_error &&
                  !std::strcmp(rib::state(), "restored"),
              label, "shutdown");
    }
}

int main() {
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    base = reinterpret_cast<std::uintptr_t>(image);
    check(image && !(base & 0xffff) && base != 0x400000, "image_64k_aligned_relocated");
    if (!image) return 1;
    lay_out();
    std::memcpy(pristine_decision, image + R::windows[0].rva, 63);
    std::memcpy(pristine_gate, image + R::windows[1].rva, 31);
    std::printf("IMAGE base=%08x\n", uint32_t(base));
    {
        auto read = [](std::uintptr_t at, unsigned char* out, unsigned n) { return x3m::engine_patch::read_code(at, out, n); };
        const char* which = nullptr;
        check(!R::plan(base, read, &which) && !std::strcmp(which, "-"), "plan_accepts_the_image");
        check(R::plan(base + 0x10000, read) != nullptr, "plan_refuses_another_base");
    }

    // Vanilla: the choice (or the registry value for -1) decides; the pump blocks while inactive with the bit clear.
    static Out vanilla[64], patched[64];
    const Tally v = sweep(false, vanilla, 64);
    std::printf("DECIDE vanilla cases=%u bad=%u bit_set=%u\n", v.cases, v.bad, v.set);
    check(v.bad == 0, "vanilla_decision");
    unsigned gate_bad = 0;
    for (uint32_t active : {0u, 1u})
        for (uint32_t word : {0x1000u, 0x5000u, 0u, 0xffffbfffu, 0xffffffffu}) {
            const uint32_t want = (active || (word & 0x4000)) ? 3u : 2u;
            if (pump(word, active) != want) ++gate_bad;
        }
    std::printf("GATE cases=10 bad=%u\n", gate_bad);
    check(gate_bad == 0, "pump_gate_rule");
    // The CrossOver shortcut (no argument, no registry value) and -noruninbg: inactive -> blocking GetMessageA.
    check(pump(decide(0, -1, 0).word, 0) == 2 && pump(decide(0x4000, 0, 0).word, 0) == 2, "vanilla_unfocused_blocks");

    // Refusals before anything is written.
    check(!rib::install_base(base + 0x1000) && !std::strcmp(rib::state(), "invalid_site"), "refuse_unaligned_base");
    check(!rib::install_base(0) && !std::strcmp(rib::state(), "invalid_site"), "refuse_null_base");
    unsigned flips = 0, flips_refused = 0, fixups = 0, fixup_refused = 0;
    for (const R::Window& w : R::windows) {
        for (unsigned i = 0; i < w.length; ++i)
            for (unsigned bit = 0; bit < 8; ++bit) {
                code_pages(true);
                image[w.rva + i] ^= std::uint8_t(1u << bit);
                code_pages(false);
                ++flips;
                SetLastError(sentinel_error);
                const bool refused = !rib::install_base(base) && !std::strcmp(rib::state(), "bytes_mismatch") &&
                                     GetLastError() == sentinel_error;
                code_pages(true);
                image[w.rva + i] ^= std::uint8_t(1u << bit);
                code_pages(false);
                if (refused && windows_pristine()) ++flips_refused;
            }
        for (unsigned r = 0; r < w.reloc_count; ++r) {
            ++fixups;
            code_pages(true);
            put32(image + w.rva + w.reloc_offset[r], uint32_t(base + w.reloc_target[r] + 0x10000));
            code_pages(false);
            if (!rib::install_base(base) && !std::strcmp(rib::state(), "bytes_mismatch")) ++fixup_refused;
            code_pages(true);
            put32(image + w.rva + w.reloc_offset[r], uint32_t(base + w.reloc_target[r]));
            code_pages(false);
        }
    }
    std::printf("FLIPS windows=2 bits=%u refused=%u fixups=%u fixups_refused=%u\n", flips, flips_refused, fixups,
                fixup_refused);
    check(flips == (63 + 31) * 8 && flips_refused == flips && fixup_refused == fixups && windows_pristine(),
          "every_window_change_refused");
    code_pages(true);
    std::memcpy(image + R::site_rva, R::patched, 2);
    code_pages(false);
    check(!rib::install_base(base) && !std::strcmp(rib::state(), "bytes_mismatch"), "refuse_already_patched_window");
    code_pages(true);
    std::memcpy(image + R::site_rva, R::original, 2);
    code_pages(false);

    // Install through the production module: one lock cmpxchg8b, read back, protection restored.
    SetLastError(sentinel_error);
    check(rib::install_base(base) && !std::strcmp(rib::state(), "ok") && site_is(R::patched) &&
              GetLastError() == sentinel_error && protection_at(R::site_rva) == PAGE_EXECUTE_READ &&
              rib::outcome() == nullptr,
          "install", rib::state());
    check(!rib::install_base(base) && !std::strcmp(rib::state(), "already_installed"), "second_install_refused");
    const Tally p = sweep(true, patched, 64);
    std::printf("DECIDE patched cases=%u bad=%u bit_set=%u\n", p.cases, p.bad, p.set);
    check(p.bad == 0 && p.set == p.cases, "patched_decision");
    // Parity at the input setup call: every register, ESP, the return address and DF as the engine's; only the flag
    // word's bit and the dead arithmetic flags may differ.
    unsigned parity_bad = 0, words_changed = 0;
    for (unsigned i = 0; i < v.cases && i < 64; ++i) {
        const Out& a = vanilla[i];
        const Out& b = patched[i];
        if (a.c_eax != b.c_eax || a.c_ebx != b.c_ebx || a.c_ecx != b.c_ecx || a.c_edx != b.c_edx || a.c_esi != b.c_esi ||
            a.c_edi != b.c_edi || a.c_ebp != b.c_ebp || a.c_esp_off != b.c_esp_off || a.c_ret != b.c_ret ||
            (a.c_flags & ~arith) != (b.c_flags & ~arith) || (a.word | R::run_in_background_bit) != b.word ||
            a.ebx != b.ebx || a.esi != b.esi || a.edi != b.edi || a.ebp != b.ebp || a.eax != b.eax)
            ++parity_bad;
        if (a.word != b.word) ++words_changed;
    }
    std::printf("PARITY cases=%u bad=%u words_changed=%u\n", v.cases, parity_bad, words_changed);
    check(parity_bad == 0 && words_changed > 0, "parity_at_setup_call");
    // The flag honoured while unfocused: the shortcut and -noruninbg now take the non-blocking PeekMessageA path; a
    // later X2_SetRunInBackground(0) (the bit cleared) blocks again.
    const uint32_t shortcut = decide(0, -1, 0).word, noruninbg = decide(0x4000, 0, 0).word;
    check(pump(shortcut, 0) == 3 && pump(noruninbg, 0) == 3 && pump(shortcut, 1) == 3, "patched_unfocused_runs");
    check(pump(shortcut & ~R::run_in_background_bit, 0) == 2, "script_clear_blocks_again");
    std::printf("UNFOCUSED shortcut_word=%08x noruninbg_word=%08x path=%s\n", shortcut, noruninbg,
                pump(shortcut, 0) == 3 ? "peek" : "get");
    // Shutdown restores (only over the NOPs).
    SetLastError(sentinel_error);
    check(rib::shutdown() && windows_pristine() && !std::strcmp(rib::state(), "restored") &&
              GetLastError() == sentinel_error && protection_at(R::site_rva) == PAGE_EXECUTE_READ,
          "shutdown_restores");
    check(rib::install_base(base), "reinstall");
    code_pages(true);
    image[R::site_rva + 1] = 0x08;
    code_pages(false);
    check(!rib::shutdown() && !std::strcmp(rib::state(), "restore_not_owned"), "shutdown_not_owned");
    code_pages(true);
    image[R::site_rva + 1] = 0x90;
    code_pages(false);
    check(rib::shutdown() && windows_pristine(), "shutdown_after_not_owned");

    // initialize(): the rows and refusals.
    rib::fixture_base(base);
    initialize_row("init_unset", nullptr, "status=off reason=unset setting=- value_before=- value_after=- write=none", false);
    initialize_row("init_off", L"0", "status=off reason=setting_off setting=0", false);
    initialize_row("init_on", L"1", "status=armed reason=ok setting=1 value_before=- value_after=- write=atomic", true);
    for (const wchar_t* bad : {L"2", L"10", L"on", L"abcd", L" 1"})
        initialize_row("init_invalid", bad, "status=refused reason=invalid_setting setting=?", false);
    rib::fixture_base(0); // the fixture's own module is not X3FL.exe
    initialize_row("init_not_x3fl", L"1", "site=0x00000000 rva=0x0776ec edition=fl status=refused reason=executable_mismatch", false);
    rib::fixture_base(base);

    span_sequences();

    // The install window closed (first Present): refused, nothing written.
    x3m::engine_patch::close_install_window("fixture_late");
    check(!rib::install_base(base) && !std::strcmp(rib::state(), "late_claim") && windows_pristine(), "late_claim_refused");
    initialize_row("init_late_claim", L"1", "status=refused reason=late_claim setting=1", false);
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
