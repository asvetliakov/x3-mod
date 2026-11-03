// FL edition of x3m::circle_fill (src/proxy/circle_fill.h; the ap target keeps src/proxy/circle_fill.cpp): the
// oversized scanner-disc fix on X3FL.exe (docs/reverse-engineering/fl/circle-fill.md, "Port (2026-11-03)"; sites,
// routing, recurrence and row emitter: circle_fill_sites_fl.h; the parser and mode names are AP's,
// src/proxy/circle_fill_sites.h).
//
//   base+0x10c940  the filled-ellipse rasterizer's `push ebp; mov ebp,esp; and esp,-8` claimed (6 bytes, one lock
//                  cmpxchg8b) with the stub below in front; the tail replays the three and resumes at base+0x10c946
//
// Install (backend-load path, inside the engine_patch install window; a later call is refused, late_claim): X3FL.exe
// identity (fl::module_base() != 0), every window of circle_fill_sites_fl.h at base + RVA with its relocations bound to
// base, the module pinned (the stub and its C side live here), the native addresses published, then the claim, the
// continuation, the stub in front and the read-back; any failure after the write puts the six bytes back, and a
// rollback that cannot complete keeps the site registered (patched_unverified). One `circle_fill site=` row with
// edition=fl. Nothing per frame: the stub's radius/aspect test runs on every TexCircleFill (up to seven instructions;
// EAX and the arithmetic flags are dead at the entry); only r > 813 or a non-unit aspect reaches the C side. shutdown() is for
// the fixture: the FL loader's DLL_PROCESS_DETACH unload calls are AP only (and the pinned module never unloads); it
// restores the six bytes only over this module's jump.
//
// The C side runs inside the engine's draw behind a pushfd/pushad stub that saves no XMM or x87 state: integer
// arithmetic only, built without SSE/MMX/x87 and without exception bookends (CMakeLists.txt). The one floating
// sequence is the engine's own B conversion, reproduced instruction for instruction in the asm below and executed in
// the native order (after the lock and the class call) under the caller's MXCSR: its rounding and exception controls
// apply and its status bits stay as the native conversion leaves them. No allocation, lock or state beyond the frame
// except the one-row flag; the first-wide row is written after the last span with LastError (the value after the
// native work) and the FPU/SSE image (FXSAVE) kept.
#include "circle_fill_fl.h"
#include "../../proxy/circle_fill_sites.h"
#include "../../proxy/config.h"
#include "../../proxy/engine_patch.h"
#include "circle_fill_sites_fl.h"
#include "executable_identity_fl.h"
#include <windows.h>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace engine_patch = x3m::engine_patch;
namespace ap = x3m::circle_fill::sites; // the parser (on|off) and the setting capacity
namespace F = x3m::fl::circle_fill_fl;
engine_patch::Site site_{};
bool live_ = false;
std::uintptr_t base_ = 0;
const char* state_ = "not_initialized";
const char* write_ = "none";
volatile LONG first_wide_ = 0; // the one circle_fill_wide row
// The native addresses for the C side, published before the claim.
std::uintptr_t lock_va_ = 0, class_va_ = 0, diagnostic_va_ = 0, message_va_ = 0, leaf_base_ = 0;
unsigned char window_bytes_[F::max_window];
#ifdef X3M_CIRCLE_FILL_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
#endif

void printable(const wchar_t* text, DWORD length, char* out) {
    if (!length || length >= ap::setting_capacity) {
        out[0] = length ? '?' : '-';
        out[1] = 0;
        return;
    }
    for (DWORD i = 0; i < length; ++i) out[i] = (text[i] >= 0x21 && text[i] <= 0x7e) ? static_cast<char>(text[i]) : '?';
    out[length] = 0;
}
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&live_), &module) != FALSE &&
           module != nullptr;
}
// Every window at base + RVA (fail closed): nullptr when all match, else the first refusal.
const char* windows_verified(std::uintptr_t base) {
    for (const F::Window& w : F::windows) {
        if (w.length > F::max_window || !engine_patch::read_code(base + w.rva, window_bytes_, w.length))
            return "unreadable";
        if (!F::window_matches(w, window_bytes_, base)) return w.name;
    }
    return nullptr;
}
const char* install(std::uintptr_t base) {
    write_ = "none";
    if (!base || (base & 0xffff)) return "invalid_site";
    if (!engine_patch::install_window_open()) return "late_claim";
    if (const char* mismatch = windows_verified(base)) return mismatch;
    if (!pin_self()) return "pin_failed";
    lock_va_ = base + F::lock_rva;
    class_va_ = base + F::class_rva;
    diagnostic_va_ = base + F::diagnostic_rva;
    message_va_ = base + F::message_rva;
    leaf_base_ = base;
    base_ = base;
    engine_patch::SiteSpec spec{};
    spec.name = "circle_fill_fl";
    spec.address = base + F::entry_rva;
    spec.length = F::site_length;
    spec.ret_pop = 0;
    spec.rel32_offset = 0;
    std::memcpy(spec.expected, F::expected_entry, F::site_length);
    site_ = engine_patch::Site{};
    const bool claimed = engine_patch::claim(site_, spec);
    if (claimed || site_.patched_in || !std::strcmp(site_.status, "patch_rolled_back"))
        write_ = site_.atomic_write ? "atomic" : "plain";
    live_ = site_.patched_in;
    if (!claimed) return live_ ? "rollback_failed" : site_.status;
    // The continuation word (the tail) is published before the stub goes in front of it.
    x3m_circle_fill_fl_continue = reinterpret_cast<std::uintptr_t>(*site_.entry);
    const char* failure = nullptr;
    unsigned char now[F::site_length]{};
    if (!x3m_circle_fill_fl_continue || !engine_patch::push_front(site_, reinterpret_cast<void*>(&x3m_circle_fill_fl_thunk)))
        failure = "chain_failed";
    else if (!engine_patch::read_code(base + F::entry_rva, now, F::site_length) || std::memcmp(now, site_.patched, 5) ||
             now[5] != F::expected_entry[5] || *site_.entry != reinterpret_cast<void*>(&x3m_circle_fill_fl_thunk))
        failure = "readback_failed";
    if (!failure) return "ok";
    engine_patch::restore(site_); // the one claim: rollback is its restore
    live_ = site_.patched_in;
    return live_ ? "rollback_failed" : failure;
}
std::uint32_t read32(const unsigned char* p) {
    return *reinterpret_cast<const volatile std::uint32_t*>(p);
}
std::int32_t read16(const unsigned char* p) {
    return *reinterpret_cast<const volatile std::int16_t*>(p);
}
void write32(unsigned char* p, std::uint32_t v) {
    *reinterpret_cast<volatile std::uint32_t*>(p) = v;
}
struct alignas(16) FxArea {
    unsigned char bytes[512];
};
FxArea fx_area_; // the first-wide row only (once per process, on the drawing thread)
}

extern "C" {
std::uintptr_t x3m_circle_fill_fl_continue = 0;
// asm shims below: the lock (ECX = backing, EDX = out, one stack word 0, the caller pops it), the class function
// (ECX = 0, EDX = surface; AX sign-extended, the callee's ECX stored for the diagnostic), the diagnostic (ECX as the
// native call leaves it, one cdecl stack argument) and the native B conversion.
std::uint32_t __cdecl x3m_circle_fill_fl_lock(std::uintptr_t fn, std::uint32_t backing, void* out);
std::int32_t __cdecl x3m_circle_fill_fl_class(std::uintptr_t fn, void* surface, std::uint32_t* ecx_out);
void __cdecl x3m_circle_fill_fl_diagnostic(std::uintptr_t fn, std::uintptr_t message, std::uint32_t ecx);
std::int32_t __cdecl x3m_circle_fill_fl_axis(std::int32_t A, std::int32_t q);

std::uint32_t __cdecl x3m_circle_fill_fl_wide(std::uint32_t* frame) {
    const std::uint32_t* args = frame + F::frame_args;
    const std::int32_t r = F::s16(args[F::arg_r]);
    const std::int32_t aspect = static_cast<std::int32_t>(args[F::arg_aspect]);
    const std::int32_t cx = static_cast<std::int32_t>(frame[F::frame_edx]);
    const std::int32_t cy = static_cast<std::int32_t>(args[F::arg_cy]);
    unsigned char* surface = reinterpret_cast<unsigned char*>(static_cast<std::uintptr_t>(frame[F::frame_ecx]));
    // The routing decision and the native clip flag from the pre-lock dimensions (0x10c975..a9); integer only.
    const std::int32_t width0 = read16(surface + F::object_width), height0 = read16(surface + F::object_height);
    if (F::route(cx, cy, r, aspect, width0, height0) != F::Route::wide) return 0;
    const bool clip = F::native_clip(cx, cy, r, width0, height0);
    // From here the native order; nothing declines to the stock routine.
    const std::uint32_t backing = read32(surface + F::object_backing);
    if (!backing || (read32(surface + F::object_flags) & F::flag_no_draw)) return 1;
    if (!read32(surface + F::object_bits)) {
        std::uint32_t out[F::lock_out_bytes / 4 + 1] = {};
        const std::uint32_t bits = x3m_circle_fill_fl_lock(lock_va_, backing, out);
        write32(surface + F::object_bits, bits);
        if (!bits) return 1;
        write32(surface + F::object_flags, read32(surface + F::object_flags) | F::flag_locked);
    }
    std::uint32_t class_ecx = 0;
    const std::int32_t pixel_class = x3m_circle_fill_fl_class(class_va_, surface, &class_ecx);
    const std::uint32_t leaf_rva = F::leaf_for_class(pixel_class);
    if (!leaf_rva) {
        x3m_circle_fill_fl_diagnostic(diagnostic_va_, message_va_, class_ecx);
        return 1;
    }
    const std::int32_t A = r * r;
    const std::int64_t B = x3m_circle_fill_fl_axis(A, F::quotient(aspect));
    // The route bounded B (q >= 65536: B in [0, A]; else exactly (A << 16) / q <= 32767^2); kept as a guard for the
    // 64-bit terms, never taken on an admitted call.
    if (B < 0 || B > F::max_b) return 1;
    using Leaf = void(__cdecl*)(void*, std::int32_t, std::int32_t, std::int32_t, std::uint32_t);
    const Leaf leaf = reinterpret_cast<Leaf>(leaf_base_ + leaf_rva);
    const std::uint32_t colour = args[F::arg_colour];
    F::emit(cx, cy, r, B, clip, [&] { return read16(surface + F::object_width); },
            [&] { return read16(surface + F::object_height); },
            [&](std::int32_t x0, std::int32_t y, std::int32_t x1) { leaf(surface, x0, y, x1, colour); });
    // The surface borrow has ended: copied scalars only.
    if (!first_wide_ && !InterlockedExchange(&first_wide_, 1)) {
        const DWORD error = GetLastError();
        __asm__ __volatile__("fxsave %0" : "=m"(fx_area_));
        x3m::log("circle_fill_wide edition=fl r=%ld cx=%ld cy=%ld aspect=%ld surface=%dx%d class=%ld leaf_rva=%06lx b=%ld",
                 static_cast<long>(r), static_cast<long>(F::s16(cx)), static_cast<long>(F::s16(cy)),
                 static_cast<long>(aspect), int(width0), int(height0), static_cast<long>(pixel_class),
                 static_cast<unsigned long>(leaf_rva), static_cast<long>(B));
        __asm__ __volatile__("fxrstor %0" : : "m"(fx_area_) : "memory");
        SetLastError(error);
    }
    return 1;
}
}

// The stub. The fast path reads r as 0x10c949/0x10c958 do (movsx of the word at [esp+8]): r <= 0, or r <= 813 with
// aspect ([esp+16]) exactly 65536, jumps to the tail with ECX (surface) and EDX (cx) untouched; EAX and the arithmetic
// flags are dead at the entry. Otherwise EFLAGS and every GPR are saved, DF is cleared for the C ABI, and the C side's
// result decides: non-zero restores every register and EFLAGS (DF included) and returns with a plain `ret` (the caller
// pops its 16 bytes), zero restores them and runs the stock routine from its first instruction. The C side is built
// with -mstackrealign and a four-byte incoming boundary; the stub keeps no frame pointer in a clobbered register.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_circle_fill_fl_thunk
_x3m_circle_fill_fl_thunk:
    movsx eax, word ptr [esp+8]
    test eax, eax
    jle 2f
    cmp eax, 813
    jg 1f
    cmp dword ptr [esp+16], 65536
    je 2f
1:  pushfd
    pushad
    cld
    mov eax, esp
    push eax
    call _x3m_circle_fill_fl_wide
    add esp, 4
    test eax, eax
    jz 3f
    popad
    popfd
    ret
3:  popad
    popfd
2:  jmp dword ptr [_x3m_circle_fill_fl_continue]
    .p2align 4
    .globl _x3m_circle_fill_fl_lock
_x3m_circle_fill_fl_lock:
    mov ecx, dword ptr [esp+8]
    mov edx, dword ptr [esp+12]
    push 0
    call dword ptr [esp+8]
    add esp, 4
    ret
    .p2align 4
    .globl _x3m_circle_fill_fl_class
_x3m_circle_fill_fl_class:
    mov edx, dword ptr [esp+8]
    xor ecx, ecx
    call dword ptr [esp+4]
    mov edx, dword ptr [esp+12]
    mov dword ptr [edx], ecx
    movsx eax, ax
    ret
    .p2align 4
    .globl _x3m_circle_fill_fl_diagnostic
_x3m_circle_fill_fl_diagnostic:
    mov ecx, dword ptr [esp+12]
    push dword ptr [esp+8]
    call dword ptr [esp+8]
    add esp, 4
    ret
    .p2align 4
    .globl _x3m_circle_fill_fl_axis
_x3m_circle_fill_fl_axis:
    movd xmm1, dword ptr [esp+4]
    cvtdq2pd xmm1, xmm1
    movd xmm0, dword ptr [esp+8]
    cvtdq2ps xmm0, xmm0
    mulss xmm0, dword ptr [_x3m_circle_fill_fl_scale]
    cvtps2pd xmm0, xmm0
    divsd xmm1, xmm0
    cvttsd2si eax, xmm1
    cvttsd2si ecx, xmm1
    ret
    .section .rdata,"dr"
    .p2align 2
_x3m_circle_fill_fl_scale:
    .long 0x37800000
    .text
    .att_syntax
)");

namespace x3m::circle_fill {
bool install_base(std::uintptr_t base) {
    const DWORD error = GetLastError();
    if (live_) {
        state_ = "already_installed";
        SetLastError(error);
        return false;
    }
    state_ = install(base);
    SetLastError(error);
    return !std::strcmp(state_, "ok");
}
bool initialize() {
    const DWORD error = GetLastError();
    if (live_) {
        SetLastError(error);
        return std::strcmp(state_, "ok") == 0;
    }
    // Unset or empty = the DLL default (on); 1..31 characters must be exactly on or off; anything else is refused and
    // nothing is patched.
    wchar_t text[ap::setting_capacity]{};
    const DWORD length = x3m::config::get(L"X3M_CIRCLE_FILL_FIX", text, ap::setting_capacity);
    char setting[ap::setting_capacity]{};
    printable(text, length, setting);
#ifdef X3M_CIRCLE_FILL_FL_FIXTURE
    const std::uintptr_t base = fixture_base_ ? fixture_base_ : x3m::fl::module_base();
#else
    const std::uintptr_t base = x3m::fl::module_base();
#endif
    ap::Mode mode = ap::default_mode;
    bool parsed = true;
    write_ = "none";
    if (length >= ap::setting_capacity) {
        state_ = "too_long";
        parsed = false;
    } else if (length && !ap::parse_mode(text, &mode)) {
        state_ = "invalid_setting";
        parsed = false;
    } else if (mode == ap::Mode::off)
        state_ = "off";
    else if (!base)
        state_ = "executable_mismatch";
    else
        state_ = install(base);
    const bool applied = !std::strcmp(state_, "ok");
    // rollback_failed leaves the site registered with a jump that may still be live: not reported as a refusal.
    const char* status = applied                            ? "patched"
                         : live_                            ? "patched_unverified"
                         : parsed && mode == ap::Mode::off ? "off"
                                                            : "refused";
    log("circle_fill site=%08lx rva=%06lx edition=fl status=%s reason=%s mode=%s setting=%s write=%s threshold=%ld continue=%08lx",
        static_cast<unsigned long>(base ? base + F::entry_rva : 0), static_cast<unsigned long>(F::entry_rva), status,
        state_, parsed ? ap::mode_name(mode) : "-", setting, write_, static_cast<long>(F::stock_radius_max),
        static_cast<unsigned long>(applied ? x3m_circle_fill_fl_continue : 0));
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!live_) return true;
    const DWORD error = GetLastError();
    const std::uintptr_t at = base_ + F::entry_rva;
    unsigned char found[F::site_length]{};
    const bool found_read = engine_patch::read_code(at, found, F::site_length);
    const char* reason;
    if (found_read && !std::memcmp(found, F::expected_entry, F::site_length))
        reason = "restored"; // already the engine's bytes: no write
    else if (!found_read || std::memcmp(found, site_.patched, 5))
        reason = "restore_not_owned"; // someone else's bytes: no write
    else {
        engine_patch::restore(site_);
        unsigned char back[F::site_length]{};
        const bool back_ok = engine_patch::read_code(at, back, F::site_length) &&
                             !std::memcmp(back, F::expected_entry, F::site_length);
        reason = back_ok && !std::strcmp(site_.status, "restored") ? "restored" : "restore_failed";
        if (!back_ok) site_.patched_in = true; // still ours: the next shutdown() tries again
    }
    if (!std::strcmp(reason, "restored")) {
        live_ = false;
        site_ = engine_patch::Site{};
    } // the stub stays (the module is pinned; a thread may still be inside it)
    state_ = reason;
    log("circle_fill_restore site=%08lx rva=%06lx edition=fl status=%s registered=%u", static_cast<unsigned long>(at),
        static_cast<unsigned long>(F::entry_rva), reason, live_ ? 1u : 0u);
    SetLastError(error);
    return !live_;
}
const char* state() {
    return state_;
}
bool patched() {
    return live_;
}
#ifdef X3M_CIRCLE_FILL_FL_FIXTURE
void fixture_base(std::uintptr_t base) {
    fixture_base_ = base;
}
#endif
}
