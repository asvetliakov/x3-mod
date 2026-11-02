// CPU fixture of the own sun hook (src/proxy/own_sun_hook.cpp) over two synthetic call sites with the engine's
// shapes (RE note section 16): `push view; push record; call probe; add esp,8` with EBX / EBP / ESI / EDI live (EBP a
// value), and `push view; call traversal; ...; add esp,4` with the argument still on the stack after the call. Checks
// the redirect bytes, the original reached on the caller's exact frame, the own sun's "hidden" answer without the
// original (eligible record, latch on) and every other probe forwarded, the bracket-only install (latch ignored),
// callee-saved registers, stack balance, x87 stack / MXCSR / LastError across both thunks (the listener is deliberately
// dirty), bracket ordering, the foreign-thread and no-probe-yet paths, the log mode (the original runs exactly once),
// second-claim rollback, restore and late-claim refusal.
// Built by build_own_sun_hook.py with the production module and engine_patch.cpp.
#include "../../src/proxy/own_sun_hook.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/engine_memory.h"
#include "../../src/proxy/cpu_state.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace so = x3m::own_sun_hook;
namespace core = x3m::own_sun_hook::core;
static std::string last_probe_line;
static unsigned probe_lines = 0;
namespace x3m {
void log(const char* format, ...) { // self-preserving, as the production log()
    std::va_list a;
    va_start(a, format);
    call_preserved([&] {
        char text[768];
        std::vsnprintf(text, sizeof text, format, a);
        if (!std::strncmp(text, "sun_probe ", 10)) {
            last_probe_line = text;
            ++probe_lines;
        } else
            std::printf("%s\n", text);
    });
    va_end(a);
}
namespace object_trace {
bool executable_verified() {
    return true;
}
}
}
extern "C" {
std::uint32_t fx_out[8], fx_esp_before, fx_lens_out[8], fx_lens_esp_before, fx_pad, fx_df, fx_flags, fx_lens_flags;
unsigned fx_probe_calls, fx_lens_calls;
int fx_probe_answer;
const void* fx_probe_args[2];
const void* fx_probe_return;
const void* fx_lens_arg;
bool fx_bracket_inside;
unsigned fx_order, fx_begin_at, fx_original_at, fx_end_at;
std::uintptr_t fx_lens_stack;
void fx_probe_caller(const void* record, const void* view);
void fx_lens_caller(const void* view);
extern unsigned char fx_probe_site[], fx_lens_site[];
int __cdecl fx_probe_original(const void* record, const void* view) {
    ++fx_probe_calls;
    fx_probe_args[0] = record;
    fx_probe_args[1] = view;
    fx_probe_return = __builtin_return_address(0);
    return fx_probe_answer;
}
int __cdecl fx_other_target(const void*, const void*) {
    return 7;
}
int __cdecl fx_lens_original(const void* view) {
    ++fx_lens_calls;
    fx_lens_arg = view;
    fx_bracket_inside = so::bracket_open();
    fx_original_at = ++fx_order;
    fx_lens_stack = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
    return 0x5a5a5a5a; // EAX is dead at the site
}
}
asm(R"(
    .intel_syntax noprefix
    .text
    .globl _fx_probe_caller
_fx_probe_caller:
    push ebx
    push ebp
    push esi
    push edi
    mov esi, [esp+20]
    mov ecx, [esp+24]
    mov ebx, 0x11111111
    mov ebp, 0x22222222
    mov edi, 0x44444444
    sub esp, [_fx_pad]
    mov [_fx_esp_before], esp
    cmp dword ptr [_fx_df], 0
    je 1f
    std
1:  push ecx
    push esi
    .globl _fx_probe_site
_fx_probe_site:
    call _fx_probe_original
    mov [_fx_out], eax
    pushfd
    pop dword ptr [_fx_flags]
    cld
    mov [_fx_out+4], ebx
    mov [_fx_out+8], ebp
    mov [_fx_out+12], esi
    mov [_fx_out+16], edi
    mov eax, [esp]
    mov [_fx_out+24], eax
    mov eax, [esp+4]
    mov [_fx_out+28], eax
    add esp, 8
    mov [_fx_out+20], esp
    add esp, [_fx_pad]
    pop edi
    pop esi
    pop ebp
    pop ebx
    ret
    .globl _fx_lens_caller
_fx_lens_caller:
    push ebx
    push ebp
    push esi
    push edi
    mov esi, [esp+20]
    mov ebx, 0x11111111
    mov ebp, 0x22222222
    mov edi, 0x44444444
    sub esp, [_fx_pad]
    mov [_fx_lens_esp_before], esp
    cmp dword ptr [_fx_df], 0
    je 2f
    std
2:  push esi
    .globl _fx_lens_site
_fx_lens_site:
    call _fx_lens_original
    pushfd
    pop dword ptr [_fx_lens_flags]
    cld
    mov [_fx_lens_out+4], ebx
    mov [_fx_lens_out+8], ebp
    mov [_fx_lens_out+12], esi
    mov [_fx_lens_out+16], edi
    mov eax, [esp]
    mov [_fx_lens_out+24], eax
    add esp, 4
    mov [_fx_lens_out+20], esp
    add esp, [_fx_pad]
    pop edi
    pop esi
    pop ebp
    pop ebx
    ret
    .att_syntax
)");
namespace {
unsigned checks = 0, failures = 0;
bool check(const char* label, bool ok) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
    return ok;
}
// view_block: the main view (sector-camera marker). background_view: the layer-15 background-regime view (+0x270 &
// 0x400000) that owns the sun's record (run223). other_view: a view with neither (a monitor). late_view: a later
// full-screen view.
std::uint32_t view_block[0x308 / 4], background_view[0x308 / 4], other_view[0x308 / 4], late_view[0x308 / 4];
std::uint32_t record_block[0x70 / 4], foreign_record[0x70 / 4], main_owned_record[0x70 / 4];
std::uintptr_t main_view_value;
unsigned begins = 0, ends = 0, listener_df = 0;
bool dirty_listener = false;
unsigned direction_flag() {
    unsigned flags = 0;
    asm volatile("pushfl\n\tpopl %0" : "=r"(flags)::"memory");
    return flags & 0x400u;
}
void listener_begin() {
    ++begins;
    fx_begin_at = ++fx_order;
    listener_df += direction_flag();
    if (dirty_listener) {
        volatile double x = 3.0;
        asm volatile("fldl %0\n\tfsqrt\n\tfstp %%st(0)" ::"m"(x) : "memory");
        unsigned m = 0x7f80;
        asm volatile("ldmxcsr %0" ::"m"(m) : "memory");
        SetLastError(5);
    }
}
void listener_end() {
    ++ends;
    fx_end_at = ++fx_order;
    listener_df += direction_flag();
    if (dirty_listener) SetLastError(6);
}
void set_record(std::uint32_t* r, const std::uint32_t* owner, std::int32_t x, std::int32_t y) {
    std::memset(r, 0, 0x70);
    r[core::record_view / 4] = std::uint32_t(reinterpret_cast<std::uintptr_t>(owner));
    r[core::record_accumulator / 4] = 200;
    r[core::record_x / 4] = std::uint32_t(x);
    r[core::record_y / 4] = std::uint32_t(y);
    r[core::record_visible / 4] = 1;
    r[core::record_size / 4] = 655;
    r[core::record_group / 4] = 3;
}
void set_view(std::uint32_t* v, std::uint32_t flags, std::uint32_t layer = 16) {
    std::memset(v, 0, 0x308);
    v[core::view_flags / 4] = flags;
    v[0x29c / 4] = layer;
    v[core::view_rect_bottom / 4] = 0x10000;
    v[core::view_rect_right / 4] = 0x10000;
    v[core::view_fov / 4] = 0x2aaa;
    v[core::view_scale_x / 4] = 0x10000;
    v[core::view_scale_y / 4] = 0x9000;
}
// One probe through the synthetic site under a hostile CPU state; true when everything the site relies on came back.
bool probe(const std::uint32_t* record, const std::uint32_t* view, int expected_eax, unsigned expected_original_calls) {
    const unsigned before = fx_probe_calls;
    unsigned mxcsr = 0x7f80 | 0x6000, mxcsr_after = 0;
    double a = 1.25, b = 2.5, ra = 0, rb = 0;
    asm volatile("ldmxcsr %0\n\tfldl %1\n\tfldl %2" ::"m"(mxcsr), "m"(a), "m"(b) : "memory");
    SetLastError(0x1234);
    fx_probe_caller(record, view);
    const DWORD error = GetLastError();
    asm volatile("fstpl %0\n\tfstpl %1\n\tstmxcsr %2" : "=m"(rb), "=m"(ra), "=m"(mxcsr_after)::"memory");
    unsigned restore = 0x1f80;
    asm volatile("ldmxcsr %0" ::"m"(restore) : "memory");
    bool ok = int(fx_out[0]) == expected_eax && fx_probe_calls - before == expected_original_calls;
    ok = ok && fx_out[1] == 0x11111111 && fx_out[2] == 0x22222222 &&
         fx_out[3] == std::uint32_t(reinterpret_cast<std::uintptr_t>(record)) && fx_out[4] == 0x44444444;
    ok = ok && fx_out[5] == fx_esp_before && fx_out[6] == std::uint32_t(reinterpret_cast<std::uintptr_t>(record)) &&
         fx_out[7] == std::uint32_t(reinterpret_cast<std::uintptr_t>(view));
    ok = ok && error == 0x1234 && mxcsr_after == mxcsr && ra == a && rb == b &&
         ((fx_flags & 0x400u) != 0) == (fx_df != 0);
    if (expected_original_calls) ok = ok && fx_probe_args[0] == record && fx_probe_args[1] == view;
    return ok;
}
bool lens(const std::uint32_t* view, unsigned expected_begins, bool expected_inside) {
    const unsigned begins_before = begins, ends_before = ends, calls = fx_lens_calls;
    unsigned mxcsr = 0x7f80 | 0x2000, mxcsr_after = 0;
    double a = 7.5, ra = 0;
    asm volatile("ldmxcsr %0\n\tfldl %1" ::"m"(mxcsr), "m"(a) : "memory");
    SetLastError(0x4321);
    fx_lens_caller(view);
    const DWORD error = GetLastError();
    asm volatile("fstpl %0\n\tstmxcsr %1" : "=m"(ra), "=m"(mxcsr_after)::"memory");
    unsigned restore = 0x1f80;
    asm volatile("ldmxcsr %0" ::"m"(restore) : "memory");
    bool ok = fx_lens_calls - calls == 1 && fx_lens_arg == view && begins - begins_before == expected_begins &&
              ends - ends_before == expected_begins && fx_bracket_inside == expected_inside;
    ok = ok && fx_lens_out[1] == 0x11111111 && fx_lens_out[2] == 0x22222222 &&
         fx_lens_out[3] == std::uint32_t(reinterpret_cast<std::uintptr_t>(view)) && fx_lens_out[4] == 0x44444444;
    ok = ok && fx_lens_out[5] == fx_lens_esp_before &&
         fx_lens_out[6] == std::uint32_t(reinterpret_cast<std::uintptr_t>(view)) && !so::bracket_open();
    ok = ok && error == 0x4321 && mxcsr_after == mxcsr && ra == a;
    if (expected_begins) ok = ok && fx_begin_at < fx_original_at && fx_original_at < fx_end_at;
    return ok;
}
DWORD WINAPI foreign_thread(void*) {
    fx_probe_answer = 1;
    fx_probe_caller(record_block, view_block);
    return 0;
}
so::Addresses addresses() {
    so::Addresses a;
    a.probe_site = reinterpret_cast<std::uintptr_t>(fx_probe_site);
    a.probe_target = reinterpret_cast<std::uintptr_t>(&fx_probe_original);
    a.lens_site = reinterpret_cast<std::uintptr_t>(fx_lens_site);
    a.lens_target = reinterpret_cast<std::uintptr_t>(&fx_lens_original);
    a.main_view = &main_view_value;
    return a;
}
// A frame boundary with the next frame's own-sun latch (capture.cpp sets it before present()).
void next_frame(bool own_sun) {
    so::set_own_sun_frame(own_sun);
    so::present();
}
}
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    unsigned char probe_before[5], lens_before[5];
    std::memcpy(probe_before, fx_probe_site, 5);
    std::memcpy(lens_before, fx_lens_site, 5);
    set_view(view_block, 0x10001, 16);
    set_view(background_view, 0x400001, 15);
    set_view(other_view, 1, 20);
    set_view(late_view, 1, 100);
    set_record(record_block, background_view, 0x2000, -0x1000);
    set_record(foreign_record, other_view, 0, 0);
    set_record(main_owned_record, view_block, 0x1000, 0x1000);
    main_view_value = reinterpret_cast<std::uintptr_t>(view_block);
    so::set_listener(&listener_begin, &listener_end);

    // Second-claim failure: the lens site goes back, nothing stays patched.
    {
        so::Addresses bad = addresses();
        bad.probe_target = reinterpret_cast<std::uintptr_t>(&fx_other_target);
        check("rollback_refused",
              !so::install_at(bad, false) && !std::strcmp(so::state(), "target_mismatch") && !so::installed());
        check("rollback_bytes",
              !std::memcmp(probe_before, fx_probe_site, 5) && !std::memcmp(lens_before, fx_lens_site, 5));
    }
    // The bracket-only install (the lens-flare gain or the fog, the own sun not requested): the latch cannot be set, every
    // probe is the original's on the caller's frame, the bracket runs.
    check("install_bracket_only", so::install_at(addresses(), false) && so::installed() && !so::logging());
    {
        std::int32_t rel = 0;
        std::memcpy(&rel, fx_probe_site + 1, 4);
        check("probe_redirect", fx_probe_site[0] == 0xe8 &&
                                    fx_probe_site + 5 + rel == reinterpret_cast<unsigned char*>(&x3m_sun_probe_thunk));
        std::memcpy(&rel, fx_lens_site + 1, 4);
        check("lens_redirect", fx_lens_site[0] == 0xe8 &&
                                   fx_lens_site + 5 + rel == reinterpret_cast<unsigned char*>(&x3m_sun_lens_thunk));
    }
    check("install_twice", !so::install_at(addresses(), false) && so::installed());
    check("lens_before_any_probe", lens(view_block, 0, false)); // no probe yet on this thread: no bracket
    fx_probe_answer = 0;
    next_frame(true); // not requested: ignored
    check("bracket_only_original_visible", probe(record_block, view_block, 0, 1));
    check("original_return_address", fx_probe_return == fx_probe_site + 5);
    fx_probe_answer = 1;
    check("bracket_only_original_hidden", probe(record_block, view_block, 1, 1) && !so::own_sun_hidden());
    check("bracket_only_lens", lens(view_block, 1, true));
    check("shutdown_bracket_only", so::shutdown() && !so::installed() && !std::memcmp(probe_before, fx_probe_site, 5) &&
                                       !std::memcmp(lens_before, fx_lens_site, 5));

    // The own sun requested.
    so::request_own_sun();
    check("install", so::install_at(addresses(), false) && so::installed() && !so::logging());
    // Latch off (the hotkey, an unarmed stage): the original answers on the caller's frame, both answers.
    fx_probe_answer = 1;
    next_frame(false);
    check("latch_off_original_hidden", probe(record_block, view_block, 1, 1) && !so::own_sun_hidden());
    fx_probe_answer = 0;
    check("latch_off_original_visible", probe(record_block, view_block, 0, 1) && !so::own_sun_hidden());
    // Latch on: the sun's eligible record is "hidden" without running the original (although it would show it).
    next_frame(true);
    check("own_sun_hidden_without_the_original", probe(record_block, view_block, 1, 0) && so::own_sun_hidden());
    // Every other probe is the original's: the owner's own probe (layer 15), a record the main view owns, a foreign
    // record whose owner is not a background view, a later view's re-probe, another view's own record.
    check("owner_own_probe", probe(record_block, background_view, 0, 1));
    check("main_owned_record", probe(main_owned_record, view_block, 0, 1));
    check("foreign_record", probe(foreign_record, view_block, 0, 1));
    check("late_view_reprobe", probe(record_block, late_view, 0, 1));
    check("foreign_view_own_record", probe(foreign_record, other_view, 0, 1));
    check("hidden_holds_for_the_frame", so::own_sun_hidden());
    dirty_listener = true;
    check("lens_bracket", lens(view_block, 1, true));
    dirty_listener = false;
    // The next frame without a probe: nothing hidden in it.
    next_frame(true);
    check("hidden_is_per_frame", !so::own_sun_hidden());
    // The owner loses the background flag: nobody's record, the original; back with it.
    background_view[core::view_flags / 4] = 1;
    check("owner_without_background_flag_original", probe(record_block, view_block, 0, 1) && !so::own_sun_hidden());
    background_view[core::view_flags / 4] = 0x400001;
    next_frame(true);
    check("eligible_again", probe(record_block, view_block, 1, 0) && so::own_sun_hidden());
    // Main view unknown: the original.
    main_view_value = 0;
    next_frame(true);
    check("unknown_main_original", probe(record_block, view_block, 0, 1) && !so::own_sun_hidden());
    main_view_value = reinterpret_cast<std::uintptr_t>(view_block);
    // The 4-byte stack contract and a hostile direction flag: every alignment of ESP at the site, DF clear and set, on
    // the hidden path, the original's path and the bracket. The handlers run with DF clear; the site gets its flags back.
    {
        bool ok = true;
        unsigned handlers_df = 0;
        for (unsigned pad = 0; pad < 16; pad += 4)
            for (unsigned df = 0; df < 2; ++df) {
                fx_pad = pad;
                fx_df = df;
                fx_probe_answer = 0;
                next_frame(true);
                ok = probe(record_block, view_block, 1, 0) && ok;
                ok = probe(foreign_record, view_block, 0, 1) && ok;
                listener_df = 0;
                ok = lens(view_block, 1, true) && ok;
                handlers_df += listener_df;
                next_frame(false);
                ok = probe(record_block, view_block, 0, 1) && ok;
            }
        fx_pad = 0;
        fx_df = 0;
        check("stack_alignments_and_direction_flag", ok && handlers_df == 0);
    }
    // present() closes a bracket an unwind left open.
    x3m_sun_lens_begin();
    check("bracket_left_open", so::bracket_open());
    next_frame(true);
    check("present_closes_bracket", !so::bracket_open());
    // Another thread: straight to the original, counted, never hidden.
    {
        const unsigned before = fx_probe_calls;
        HANDLE t = CreateThread(nullptr, 0, &foreign_thread, nullptr, 0, nullptr);
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
        check("foreign_thread", fx_probe_calls - before == 1 && so::counters().foreign_thread == 1 && fx_out[0] == 1);
    }
    const so::Counters totals = so::counters();
    std::printf("COUNTERS probes=%u own=%u hidden=%u original=%u brackets=%u foreign=%u\n", totals.probes, totals.own,
                totals.own_sun_hidden, totals.original, totals.brackets, totals.foreign_thread);
    check("counters", totals.own_sun_hidden == 10 && totals.probes == totals.own_sun_hidden + totals.original &&
                          totals.own == totals.own_sun_hidden && totals.brackets == 10 && totals.foreign_thread == 1);
    check("shutdown", so::shutdown() && !so::installed() && !std::memcmp(probe_before, fx_probe_site, 5) &&
                          !std::memcmp(lens_before, fx_lens_site, 5) && !so::own_sun_hidden());

    // The log mode (X3M_OWN_SUN_LOG=1): the original runs exactly once per logged probe and its answer is returned; the
    // own sun's hidden answer is not logged and runs no original.
    check("install_log", so::install_at(addresses(), true) && so::logging());
    fx_probe_answer = 1;
    probe_lines = 0;
    next_frame(false);
    check("log_latch_off_original_once", probe(record_block, view_block, 1, 1));
    check("log_line_original",
          probe_lines == 1 && last_probe_line.find(" vanilla=1 answer=1") != std::string::npos &&
              last_probe_line.find(" eligible=1 acc=200 x=8192 y=-4096 visible=1 size=655 group=3 ") !=
                  std::string::npos &&
              last_probe_line.find(" owner_flags270=00400001 owner_layer=15 ") != std::string::npos &&
              last_probe_line.find(" layer=16 ") != std::string::npos);
    fx_probe_answer = 0;
    check("log_main_owned_once", probe(main_owned_record, view_block, 0, 1));
    check("log_line_main_owned", probe_lines == 2 && last_probe_line.find(" eligible=0 ") != std::string::npos &&
                                     last_probe_line.find(" owner_flags270=00000000 owner_layer=-1 ") != std::string::npos &&
                                     last_probe_line.find(" vanilla=0 answer=0") != std::string::npos);
    next_frame(true);
    check("log_own_sun_hidden_not_logged", probe(record_block, view_block, 1, 0) && probe_lines == 2);
    check("shutdown_log", so::shutdown());
    // The production resolution of the main view: no override pointer, so the frame's first probe walks the cockpit
    // registry through the real engine_memory (the engine's globals are not mapped here: VirtualQuery refuses the
    // span, the main view is unknown, the original answers). probe() holds LastError, MXCSR and the x87 stack hostile.
    {
        so::Addresses production = addresses();
        production.main_view = nullptr;
        check("install_production_walk", so::install_at(production, false));
        const auto before = x3m::engine_memory::stats();
        fx_probe_answer = 1;
        next_frame(true);
        check("production_walk_frame1", probe(record_block, view_block, 1, 1));
        fx_probe_answer = 0;
        next_frame(true);
        check("production_walk_frame2", probe(record_block, view_block, 0, 1) && !so::own_sun_hidden());
        const auto after = x3m::engine_memory::stats();
        check("production_walk_reached_the_reader", after.rejected > before.rejected || after.queries > before.queries);
        check("shutdown_production_walk", so::shutdown());
    }
    // Late claim.
    x3m::engine_patch::close_install_window("fixture");
    check("late_claim", !so::install_at(addresses(), false) && !std::strcmp(so::state(), "late_claim") &&
                            !std::memcmp(probe_before, fx_probe_site, 5) && !std::memcmp(lens_before, fx_lens_site, 5));
    std::printf("OWN SUN HOOK checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
