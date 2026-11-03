// Native fixture of the FL pause key (src/engine/fl/pause_key_only_fl.cpp, pause_key_only_sites_fl.h, inline_patch_fl.h;
// docs/reverse-engineering/fl/pause-background.md, "Port (2026-11-03)"): no game code beyond the two windows of
// pause_key_only_sites_fl.h (X3FL.exe's bytes, verify_pause_key_only_sites_fl.py), no launch. Built with
// -DX3M_EDITION_FL=1 -DX3M_PAUSE_KEY_ONLY_FL_FIXTURE by verification/analysis/test_fl_pause_key_only.py and run natively
// (WSL interop).
//
// A 64 KB-aligned private image stands in for X3FL.exe at `base`: the input function's entry window at base+0x078690
// and the wait loop at base+0x0787ee, their fixups relocated to this base (previous-key word base+0x22ddf0, input slot
// base+0x1a8478 -> a fake input block, context slot base+0x2243cc -> a fake context); `jmp rel32` stand-ins at the pump
// base+0x0ec620 and the key reader base+0x0eccd0 (scripted mouse/joystick words and keys; both scramble EAX/ECX/EDX,
// the reader returns garbage above AX), and exits at base+0x0786cc (not paused) and base+0x078876 (after the pause
// exit's flag store). The harness calls the real entry with sentinels in every register; the exit records every
// register, EFLAGS, ESP, the loop's [esp+0x10]/[esp+0x14], the context flags, the step and LastError. Code pages are
// PAGE_EXECUTE_READ, as in the game; the production install changes and restores their protection.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../../src/engine/fl/inline_patch_fl.h"
#include "../../src/engine/fl/pause_key_only_fl.h"
#include "../../src/engine/fl/pause_key_only_sites_fl.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/pause_key_only_core.h"
namespace P = x3m::fl::pause_key_only_fl;
namespace I = x3m::fl::inline_patch_fl;
namespace pko = x3m::pause_key_only;
namespace core = x3m::pause_key_only::core;
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
uint32_t in_eax, in_ebx, in_ecx, in_edx, in_esi, in_edi, in_ebp, in_flags, in_entry;
uint32_t out_kind, out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, out_esp, out_flags, out_s10, out_s14;
uint32_t saved_esp, call_esp;
void flp_run();
void flp_exit_paused();
void flp_exit_unpaused();
void flp_pump();
void flp_key();
void flp_pump_c();
uint32_t flp_key_c();
}
asm(".text\n\t"
    ".globl _flp_run\n_flp_run:\n\t"
    "pushal\n\tpushfl\n\tmovl %esp,_saved_esp\n\t"
    "andl $-16,%esp\n\tsubl $0x204,%esp\n\tmovl %esp,_call_esp\n\t" // 4 mod 16 after the call's push: realigned by the entry
    "movl _in_ebx,%ebx\n\tmovl _in_ecx,%ecx\n\tmovl _in_edx,%edx\n\tmovl _in_esi,%esi\n\tmovl _in_edi,%edi\n\t"
    "movl _in_ebp,%ebp\n\tpushl _in_flags\n\tmovl _in_eax,%eax\n\tpopfl\n\tcall *_in_entry\n\t"
    "movl $3,_out_kind\n\tjmp 1f\n\t" // a return from the function is not expected
    ".globl _flp_exit_paused\n_flp_exit_paused:\n\tmovl $1,_out_kind\n\tjmp 1f\n\t"
    ".globl _flp_exit_unpaused\n_flp_exit_unpaused:\n\tmovl $2,_out_kind\n"
    "1:\n\tmovl %eax,_out_eax\n\tpushfl\n\tpopl _out_flags\n\tcld\n\t"
    "movl %ebx,_out_ebx\n\tmovl %ecx,_out_ecx\n\tmovl %edx,_out_edx\n\tmovl %esi,_out_esi\n\t"
    "movl %edi,_out_edi\n\tmovl %ebp,_out_ebp\n\tmovl %esp,_out_esp\n\t"
    "movl 0x10(%esp),%eax\n\tmovl %eax,_out_s10\n\tmovl 0x14(%esp),%eax\n\tmovl %eax,_out_s14\n\t"
    "movl _saved_esp,%esp\n\tpopfl\n\tpopal\n\tret\n\t"
    // The pump and key reader stand-ins: the C side, then the scratch registers scrambled as a real callee may.
    ".globl _flp_pump\n_flp_pump:\n\tcall _flp_pump_c\n\t"
    "movl $0xa5a5a5a5,%eax\n\tmovl $0xc5c5c5c5,%ecx\n\tmovl $0xd5d5d5d5,%edx\n\tret\n\t"
    ".globl _flp_key\n_flp_key:\n\tcall _flp_key_c\n\tmovl $0xc6c6c6c6,%ecx\n\tmovl $0xd6d6d6d6,%edx\n\tret\n\t");

std::uintptr_t base = 0;
unsigned char* image = nullptr;
constexpr unsigned image_size = 0x230000;
constexpr uint32_t sentinel_error = 0x2a2a1357, arith = 0x8d5, df = 0x400;
constexpr uint32_t force_bit = 0x40000000; // the timeout's mouse bit (no script uses it)
alignas(16) unsigned char input[0x500];
alignas(16) unsigned char context[0x500];
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
    for (uint32_t rva : {0x078000u, 0x0ec000u})
        VirtualProtect(image + rva, 0x1000, writable ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ, &old);
}
void exit_at(uint32_t rva, void* to) { // jmp rel32
    image[rva] = 0xe9;
    put32(image + rva + 1, u32(to) - uint32_t(base + rva + 5));
}
void lay_window(const P::Window& w) {
    std::memcpy(image + w.rva, w.bytes, w.length);
    for (unsigned r = 0; r < w.reloc_count; ++r) put32(image + w.rva + w.reloc_offset[r], uint32_t(base + w.reloc_target[r]));
}
void lay_out() {
    code_pages(true);
    std::memset(image + 0x078000, 0xcc, 0x1000);
    std::memset(image + 0x0ec000, 0xcc, 0x1000);
    for (const P::Window& w : P::windows) lay_window(w);
    exit_at(0x0786cc, reinterpret_cast<void*>(&flp_exit_unpaused));
    exit_at(P::after_exit_rva, reinterpret_cast<void*>(&flp_exit_paused));
    exit_at(P::pump_rva, reinterpret_cast<void*>(&flp_pump));
    exit_at(P::key_reader_rva, reinterpret_cast<void*>(&flp_key));
    put32(image + P::input_slot_rva, u32(input));
    put32(image + P::context_slot_rva, u32(context));
    code_pages(false);
    FlushInstructionCache(GetCurrentProcess(), image, image_size);
}
bool site_is(const unsigned char* bytes) {
    return !std::memcmp(image + P::site_rva, bytes, P::site_length);
}
unsigned char pristine_entry[60], pristine_loop[136];
bool windows_pristine() {
    return !std::memcmp(image + P::windows[0].rva, pristine_entry, 60) &&
           !std::memcmp(image + P::windows[1].rva, pristine_loop, 136);
}
DWORD protection_at(uint32_t rva) {
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(image + rva, &mbi, sizeof mbi);
    return mbi.Protect;
}

// The scripted input: step 0 is the entry's key read; each pump call advances one step (mouse word, joystick word,
// then the key the following read returns). Past the script the pump forces a new mouse bit (timeout).
struct Step {
    uint32_t mouse, joy, key;
};
const Step* script = nullptr;
unsigned script_len = 0, step = 0, pump_calls = 0, key_calls = 0;
uint32_t entry_key = 0;
bool timed_out = false;
extern "C" void flp_pump_c() {
    ++step;
    ++pump_calls;
    if (step <= script_len) {
        put32(input + P::input_mouse_offset, script[step - 1].mouse);
        put32(input + P::input_joystick_offset, script[step - 1].joy);
    } else {
        timed_out = true;
        put32(input + P::input_mouse_offset, get32(input + P::input_mouse_offset) | force_bit);
    }
}
extern "C" uint32_t flp_key_c() {
    ++key_calls;
    const uint32_t key = step == 0 ? entry_key : (step <= script_len ? script[step - 1].key : 0);
    return 0xdead0000u | (key & 0xffff); // garbage above AX: the engine zero-extends
}
struct Out {
    uint32_t kind, eax, ebx, ecx, edx, esi, edi, ebp, esp_off, flags, s10, s14, ctx, steps, error;
    bool timeout;
};
// One pass through the real entry: previous key `prev`, entry key `k0`, the buttons at entry, the script.
Out run(uint32_t prev, uint32_t k0, uint32_t m0, uint32_t j0, const Step* s, unsigned n, uint32_t ctx_flags = 0x41,
        uint32_t flags = 0x0d5) {
    std::memset(input, 0x5a, sizeof input);
    std::memset(context, 0x3c, sizeof context);
    put32(input + P::input_mouse_offset, m0);
    put32(input + P::input_joystick_offset, j0);
    put32(context + P::context_flags_offset, ctx_flags);
    const std::uint16_t prev16 = std::uint16_t(prev);
    std::memcpy(image + P::previous_key_rva, &prev16, 2);
    script = s, script_len = n, step = 0, pump_calls = key_calls = 0, entry_key = k0, timed_out = false;
    in_eax = 0xeaeaeaea, in_ebx = 0xb0b0b0b0, in_ecx = 0xc0c0c0c0, in_edx = 0xd0d0d0d0, in_esi = 0x51515151;
    in_edi = 0xd1d1d1d1, in_ebp = 0xbebebebe, in_flags = flags, in_entry = uint32_t(base + P::function_rva);
    SetLastError(sentinel_error);
    flp_run();
    return Out{out_kind, out_eax, out_ebx, out_ecx, out_edx, out_esi, out_edi, out_ebp, call_esp - out_esp, out_flags,
               out_s10, out_s14, get32(context + P::context_flags_offset), step, GetLastError(), timed_out};
}
// The loop's rule (pause_key_only_sites_fl.h), for the expectations: the step at which the wait ends, or ~0u (timeout).
unsigned model(uint32_t prev, uint32_t k0, uint32_t m0, uint32_t j0, const Step* s, unsigned n, bool patched,
               uint32_t key) {
    uint32_t esi = k0 & 0xffff, p = prev & 0xffff, mo = m0, mn = m0, jo = j0, jn = j0;
    for (unsigned st = 0;; ++st) {
        if (esi && (patched ? (esi == key && esi != p) : ((esi ^ p) & 0xfff) != 0)) return st;
        if ((mo | mn) != mo || (jo | jn) != jo) return st;
        if (st >= n) return ~0u;
        mo = mn, jo = jn, mn = s[st].mouse, jn = s[st].joy, p = esi, esi = s[st].key & 0xffff;
    }
}
// Runs a scenario against the image as it stands and checks the exit against the model.
bool scenario(const char* label, bool patched, uint32_t key, uint32_t prev, uint32_t k0, uint32_t m0, uint32_t j0,
              std::initializer_list<Step> steps, Out* out = nullptr) {
    const Step* s = steps.begin();
    const unsigned n = unsigned(steps.size());
    const Out o = run(prev, k0, m0, j0, s, n);
    const unsigned want = model(prev, k0, m0, j0, s, n, patched, key);
    const bool timeout = want == ~0u;
    const bool ok = o.kind == 1 && o.timeout == timeout && (timeout ? o.steps == n + 1 : o.steps == want) &&
                    o.ctx == ((0x41u & ~1u) | 2u) && o.eax == o.ctx && o.ecx == u32(context) && o.error == sentinel_error &&
                    !(o.flags & df) && o.edi != 0xd1d1d1d1u;
    char detail[160];
    std::snprintf(detail, sizeof detail, "kind=%u steps=%u want=%d timeout=%u esi=%08x", o.kind, o.steps, int(want),
                  unsigned(o.timeout), o.esi);
    check(ok, label, detail);
    if (out) *out = o;
    return ok;
}
bool same_registers(const Out& a, const Out& b) {
    return a.kind == b.kind && a.eax == b.eax && a.ebx == b.ebx && a.ecx == b.ecx && a.edi == b.edi && a.ebp == b.ebp &&
           a.esi == b.esi && a.esp_off == b.esp_off && a.s10 == b.s10 && a.s14 == b.s14 && a.ctx == b.ctx &&
           a.steps == b.steps && (a.flags & ~arith) == (b.flags & ~arith) && a.error == b.error;
}

// The named scenarios under one configuration (vanilla or the key `key` installed); `tag` prefixes the labels.
struct Named {
    Out key_exit, mouse_exit, joy_exit;
};
Named named(const char* tag, bool patched, uint32_t key) {
    char l[96];
    Named r{};
    auto L = [&](const char* what) {
        std::snprintf(l, sizeof l, "%s_%s", tag, what);
        return l;
    };
    // Another key, then the pause key: vanilla ends on the first, the patch only on the pause key.
    scenario(L("other_then_pause"), patched, key, 0, 0, 0, 0, {{0, 0, 0x1e}, {0, 0, key}});
    // The pause key still held from the press that paused (prev = key), released, pressed again.
    scenario(L("held_release_press"), patched, key, key, key, 0, 0, {{0, 0, key}, {0, 0, 0}, {0, 0, key}}, &r.key_exit);
    // A click (new mouse bit) after an idle step; a held button does not end it, a second new bit does.
    scenario(L("click"), patched, key, 0, 0, 0, 0, {{0, 0, 0}, {1, 0, 0}}, &r.mouse_exit);
    scenario(L("held_mouse_then_new"), patched, key, 0, 0, 1, 0, {{1, 0, 0}, {0, 0, 0}, {2, 0, 0}});
    // A joystick button: rising ends it; held and lost do not.
    scenario(L("joystick"), patched, key, 0, 0, 0, 0, {{0, 0, 0}, {0, 0x10, 0}}, &r.joy_exit);
    scenario(L("joystick_held_lost"), patched, key, 0, 0, 0, 0x80000001u, {{0, 0x80000001u, 0}, {0, 0, 0}, {0, 0, 0}});
    // A focus change: Alt, Tab, then the activation helper clears the keyboard state and both button words; Alt up.
    scenario(L("focus_change"), patched, key, 0, 0, 0x4, 0x2, {{0x4, 0x2, 0x38}, {0x4, 0x2, 0x0f}, {0, 0, 0}, {0, 0, 0x38},
                                                                {0, 0, 0}});
    // Shift + the key, and the key with the previous read carrying Shift.
    scenario(L("shift_variant"), patched, key, 0, 0, 0, 0, {{0, 0, key | 0x1000}, {0, 0, 0}});
    scenario(L("shift_previous"), patched, key, key | 0x1000, key | 0x1000, 0, 0, {{0, 0, key}});
    // Not paused: the entry leaves through the unpaused path, nothing waits.
    const Out u = run(0, 0x1e, 0, 0, nullptr, 0, 0x40);
    check(u.kind == 2 && u.steps == 0 && u.ctx == 0x40 && u.error == sentinel_error, L("unpaused_path"));
    return r;
}
// Every key 1..0x1fff after an idle entry: the step-1 exit (or not) against the rule; counts the exits.
unsigned sweep(bool patched, uint32_t key, unsigned* bad) {
    unsigned exits = 0;
    *bad = 0;
    for (uint32_t k = 1; k <= 0x1fff; ++k) {
        const Step s[1] = {{0, 0, k}};
        const Out o = run(0, 0, 0, 0, s, 1);
        const bool exit = !o.timeout;
        const bool want = patched ? k == key : (k & 0xfff) != 0;
        if (exit) ++exits;
        if (exit != want || o.kind != 1 || o.error != sentinel_error || (exit && o.steps != 1)) ++*bad;
    }
    return exits;
}

// A buffer-backed Ops for the span sequence's failure paths (the production CodeOps is driven through the image above).
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
        if (protect_fail_call && protect_calls >= protect_fail_call) return false; // sticky from that call on
        *previous = 0x20;
        return true;
    }
    bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic) {
        ++writes;
        *atomic = false;
        if (refuse_rollback_write && writes == 2) return false;
        std::memcpy(mem + (at - origin), bytes, n);
        if (corrupt_write && writes == 1) mem[at - origin + n - 1] ^= 0x10;
        return true;
    }
};
void span_sequences() {
    unsigned char mem[P::site_length], patched[P::site_length];
    P::encode_site(core::default_key, patched);
    bool live = false, atomic = false;
    std::uint32_t protection = 0;
    const std::uintptr_t at = 0x00478000;
    auto fresh = [&]() { std::memcpy(mem, P::original, P::site_length); };
    fresh();
    FakeOps a{mem, at};
    a.corrupt_write = true;
    const char* r = I::install(a, at, P::original, patched, P::site_length, &live, &atomic, &protection);
    check(!std::strcmp(r, "patch_rolled_back") && !live && !std::memcmp(mem, P::original, P::site_length),
          "span_readback_rolled_back", r);
    fresh();
    FakeOps b{mem, at};
    b.corrupt_write = true;
    b.refuse_rollback_write = true;
    r = I::install(b, at, P::original, patched, P::site_length, &live, &atomic, &protection);
    check(!std::strcmp(r, "rollback_failed") && live, "span_rollback_failed_registered", r);
    fresh();
    FakeOps c{mem, at};
    c.protect_fail_call = 2;
    r = I::install(c, at, P::original, patched, P::site_length, &live, &atomic, &protection);
    check(!std::strcmp(r, "rollback_unprotected") && !live && !std::memcmp(mem, P::original, P::site_length),
          "span_reprotect_refused", r);
    fresh();
    FakeOps d{mem, at};
    d.protect_fail_call = 1;
    r = I::install(d, at, P::original, patched, P::site_length, &live, &atomic, &protection);
    check(!std::strcmp(r, "protect_failed") && !live && d.writes == 0, "span_protect_refused", r);
    fresh();
    mem[3] = 0x90;
    FakeOps e{mem, at};
    r = I::restore(e, at, P::original, patched, P::site_length, 0x20);
    check(!std::strcmp(r, "restore_not_owned") && e.writes == 0, "span_restore_not_owned", r);
    std::memcpy(mem, patched, P::site_length);
    FakeOps f{mem, at};
    r = I::restore(f, at, P::original, patched, P::site_length, 0x20);
    check(!std::strcmp(r, "restored") && !std::memcmp(mem, P::original, P::site_length), "span_restore_owned", r);
    std::memcpy(mem, patched, P::site_length);
    FakeOps g{mem, at};
    g.protect_fail_call = 1;
    r = I::restore(g, at, P::original, patched, P::site_length, 0x20);
    check(!std::strcmp(r, "restore_failed") && g.writes == 0, "span_restore_protect_refused", r);
    check(!std::strcmp(I::install(a, at, P::original, patched, 0, &live, &atomic, &protection), "invalid_site") &&
              !std::strcmp(I::install(a, at, P::original, patched, I::max_span + 1, &live, &atomic, &protection),
                           "invalid_site"),
          "span_length_bounds");
}
void set_env(const wchar_t* name, const wchar_t* value) {
    SetEnvironmentVariableW(name, value);
}
// initialize() with the given settings; checks the row and that LastError survives. `patched` = the site's expected
// state afterwards (then shutdown() restores it).
void initialize_row(const char* label, const wchar_t* only, const wchar_t* key, const char* needle, bool patched,
                    uint32_t expect_key = core::default_key) {
    set_env(L"X3M_PAUSE_KEY_ONLY", only);
    set_env(L"X3M_PAUSE_KEY", key);
    const unsigned before = rows;
    SetLastError(sentinel_error);
    const bool applied = pko::initialize();
    const bool error_kept = GetLastError() == sentinel_error;
    const bool row = needle ? rows == before + 1 && std::strstr(last_row, needle) &&
                                  std::strstr(last_row, " edition=fl ") && std::strstr(last_row, " rva=0x07880c ")
                            : rows == before;
    unsigned char want[P::site_length];
    P::encode_site(expect_key, want);
    const bool bytes = patched ? site_is(want) && pko::key() == expect_key : windows_pristine();
    check(applied == patched && error_kept && row && bytes, label, last_row);
    if (patched) {
        SetLastError(sentinel_error);
        check(pko::shutdown() && windows_pristine() && GetLastError() == sentinel_error && pko::key() == 0 &&
                  !std::strcmp(pko::state(), "restored"),
              label, "shutdown");
    }
}

int main() {
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    base = reinterpret_cast<std::uintptr_t>(image);
    check(image && !(base & 0xffff) && base != 0x400000, "image_64k_aligned_relocated");
    if (!image) return 1;
    lay_out();
    std::memcpy(pristine_entry, image + P::windows[0].rva, 60);
    std::memcpy(pristine_loop, image + P::windows[1].rva, 136);
    std::printf("IMAGE base=%08x\n", uint32_t(base));
    // Windows: the identities with the fixups zeroed, and the plan binding them to this base only.
    {
        auto read = [](std::uintptr_t at, unsigned char* out, unsigned n) { return x3m::engine_patch::read_code(at, out, n); };
        const char* which = nullptr;
        check(!P::plan(base, read, &which) && !std::strcmp(which, "-"), "plan_accepts_the_image");
        check(P::plan(base + 0x10000, read) != nullptr, "plan_refuses_another_base");
        check(!std::strcmp(P::plan(base + 4, read), "invalid_site"), "plan_refuses_unaligned_base");
    }

    // The unpatched image: the engine's rule (any changed key ends the wait).
    const Named vanilla = named("vanilla", false, core::default_key);
    unsigned bad = 0;
    const unsigned vanilla_exits = sweep(false, core::default_key, &bad);
    std::printf("SWEEP vanilla keys=8191 exits=%u bad=%u\n", vanilla_exits, bad);
    check(bad == 0 && vanilla_exits == 8191 - 1, "vanilla_sweep"); // 0x1000 alone has no low 12 bits

    // Refusals before anything is written: bad keys, an unaligned base, every single-bit change of both windows, every
    // fixup bound to another base, an already patched site.
    check(!pko::install_base(base, 0) && !std::strcmp(pko::state(), "bad_key") && windows_pristine(), "refuse_key_0");
    check(!pko::install_base(base, 0x1000) && !std::strcmp(pko::state(), "bad_key"), "refuse_key_0x1000");
    check(!pko::install_base(base, 0x2000) && !std::strcmp(pko::state(), "bad_key"), "refuse_key_0x2000");
    check(!pko::install_base(base + 0x1000, core::default_key) && !std::strcmp(pko::state(), "invalid_site"),
          "refuse_unaligned_base");
    check(!pko::install_base(0, core::default_key) && !std::strcmp(pko::state(), "invalid_site"), "refuse_null_base");
    unsigned flips = 0, flips_refused = 0, fixup_refused = 0, fixups = 0;
    for (const P::Window& w : P::windows) {
        for (unsigned i = 0; i < w.length; ++i)
            for (unsigned bit = 0; bit < 8; ++bit) {
                code_pages(true);
                image[w.rva + i] ^= std::uint8_t(1u << bit);
                code_pages(false);
                ++flips;
                SetLastError(sentinel_error);
                const bool refused = !pko::install_base(base, core::default_key) &&
                                     !std::strcmp(pko::state(), "bytes_mismatch") && GetLastError() == sentinel_error;
                code_pages(true);
                image[w.rva + i] ^= std::uint8_t(1u << bit);
                code_pages(false);
                if (refused && windows_pristine()) ++flips_refused; // nothing else was written
            }
        for (unsigned r = 0; r < w.reloc_count; ++r) {
            ++fixups;
            code_pages(true);
            put32(image + w.rva + w.reloc_offset[r], uint32_t(base + w.reloc_target[r] + 0x10000));
            code_pages(false);
            if (!pko::install_base(base, core::default_key) && !std::strcmp(pko::state(), "bytes_mismatch"))
                ++fixup_refused;
            code_pages(true);
            put32(image + w.rva + w.reloc_offset[r], uint32_t(base + w.reloc_target[r]));
            code_pages(false);
        }
    }
    std::printf("FLIPS windows=2 bits=%u refused=%u fixups=%u fixups_refused=%u\n", flips, flips_refused, fixups,
                fixup_refused);
    check(flips == (60 + 136) * 8 && flips_refused == flips && fixup_refused == fixups && windows_pristine(),
          "every_window_change_refused");
    {
        unsigned char pre[P::site_length];
        P::encode_site(core::default_key, pre);
        code_pages(true);
        std::memcpy(image + P::site_rva, pre, P::site_length);
        code_pages(false);
        check(!pko::install_base(base, core::default_key) && !std::strcmp(pko::state(), "bytes_mismatch"),
              "refuse_already_patched_window");
        code_pages(true);
        std::memcpy(image + P::site_rva, P::original, P::site_length);
        code_pages(false);
    }

    // Install the default key through the production module: plain write, read back, protection restored.
    SetLastError(sentinel_error);
    const bool ok = pko::install_base(base, core::default_key);
    unsigned char want[P::site_length];
    P::encode_site(core::default_key, want);
    check(ok && !std::strcmp(pko::state(), "ok") && !std::strcmp(pko::write_path(), "plain") && site_is(want) &&
              pko::key() == core::default_key && GetLastError() == sentinel_error &&
              protection_at(P::site_rva) == PAGE_EXECUTE_READ,
          "install_default_key", pko::state());
    check(!pko::install_base(base, core::default_key) && !std::strcmp(pko::state(), "already_installed"),
          "second_install_refused");
    std::printf("SITE patched=");
    for (unsigned i = 0; i < P::site_length; ++i) std::printf("%02x", image[P::site_rva + i]);
    std::printf("\n");
    const Named patched = named("patched", true, core::default_key);
    const unsigned patched_exits = sweep(true, core::default_key, &bad);
    std::printf("SWEEP patched key=0x1b5 keys=8191 exits=%u bad=%u\n", patched_exits, bad);
    check(bad == 0 && patched_exits == 1, "patched_sweep");
    // Register parity at the same exits: the replacement leaves every register, both locals and DF as the engine's.
    check(same_registers(vanilla.key_exit, patched.key_exit), "parity_key_exit");
    check(same_registers(vanilla.mouse_exit, patched.mouse_exit), "parity_mouse_exit");
    check(same_registers(vanilla.joy_exit, patched.joy_exit), "parity_joystick_exit");
    std::printf("PARITY key_exit esi=%08x ebx=%08x ebp=%08x s14=%08x\n", patched.key_exit.esi, patched.key_exit.ebx,
                patched.key_exit.ebp, patched.key_exit.s14);
    // Shutdown restores (only over ours); a foreign byte is not owned.
    SetLastError(sentinel_error);
    check(pko::shutdown() && windows_pristine() && !std::strcmp(pko::state(), "restored") &&
              GetLastError() == sentinel_error && protection_at(P::site_rva) == PAGE_EXECUTE_READ,
          "shutdown_restores");
    check(pko::install_base(base, core::default_key), "reinstall");
    code_pages(true);
    image[P::site_rva + 3] ^= 0x01;
    code_pages(false);
    check(!pko::shutdown() && !std::strcmp(pko::state(), "restore_not_owned"), "shutdown_not_owned");
    code_pages(true);
    image[P::site_rva + 3] ^= 0x01;
    code_pages(false);
    check(pko::shutdown() && windows_pristine(), "shutdown_after_not_owned");

    // A rebound key (decimal and 0x forms) through initialize(): only that key ends the wait.
    pko::fixture_base(base);
    initialize_row("init_unset", nullptr, nullptr, nullptr, false);
    initialize_row("init_off", L"0", nullptr, "patched=0 key=0x1b5 reason=disabled requested=0", false);
    initialize_row("init_default", L"1", nullptr, "patched=1 key=0x1b5 reason=ok requested=1", true);
    initialize_row("init_key_decimal", L"1", L"44", "patched=1 key=0x2c reason=ok requested=1", true, 0x2c);
    initialize_row("init_key_hex", L"1", L"0x2C", "patched=1 key=0x2c reason=ok requested=1", true, 0x2c);
    initialize_row("init_key_shift", L"1", L"0x11b5", "patched=1 key=0x11b5 reason=ok", true, 0x11b5);
    for (const wchar_t* badkey : {L"0", L"0x2000", L"4096", L"abc", L"0x", L"-5", L"1b5", L"0x1b5 "})
        initialize_row("init_bad_key", L"1", badkey, "patched=0 key=0x0 reason=bad_key requested=1", false);
    {
        set_env(L"X3M_PAUSE_KEY_ONLY", L"1");
        set_env(L"X3M_PAUSE_KEY", L"44");
        check(pko::initialize(), "rebound_install");
        const Named rebound = named("rebound", true, 0x2c);
        (void)rebound;
        const unsigned rebound_exits = sweep(true, 0x2c, &bad);
        std::printf("SWEEP patched key=0x2c keys=8191 exits=%u bad=%u\n", rebound_exits, bad);
        check(bad == 0 && rebound_exits == 1, "rebound_sweep");
        check(pko::shutdown() && windows_pristine(), "rebound_shutdown");
    }
    pko::fixture_base(0); // the fixture's own module is not X3FL.exe
    initialize_row("init_not_x3fl", L"1", nullptr, "patched=0 key=0x1b5 reason=executable_mismatch requested=1 site=0x00000000", false);
    pko::fixture_base(base);

    span_sequences();

    // The install window closed (first Present): refused, nothing written; the row says late_claim.
    x3m::engine_patch::close_install_window("fixture_late");
    check(!pko::install_base(base, core::default_key) && !std::strcmp(pko::state(), "late_claim") && windows_pristine(),
          "late_claim_refused");
    initialize_row("init_late_claim", L"1", nullptr, "patched=0 key=0x1b5 reason=late_claim requested=1", false);
    const Named after = named("late", false, core::default_key);
    (void)after;

    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
