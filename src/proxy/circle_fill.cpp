#include "circle_fill.h"
#include "circle_fill_sites.h"
#include "config.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

// One entry claim through engine_patch (circle_fill_sites.h) with a thunk in this module (pinned before the claim).
// The thunk's radius test runs on every TexCircleFill (three instructions; EAX and EFLAGS are dead at the entry);
// only radii above 813 texels reach the C side, which runs the 64-bit port and calls the engine's own lock, class
// function and span leaves. Integer arithmetic only; built without SSE/MMX/x87 and without exception bookends
// (CMakeLists.txt; check_no_x87.py walks x3m_circle_fill_wide). No allocation, no lock, no state beyond the frame
// except the one-row flag.
static_assert(sizeof(void*) == 4, "x86 code patching only");

namespace {
namespace engine_patch = x3m::engine_patch;
namespace sites = x3m::circle_fill::sites;
engine_patch::Site site_{};
bool live_ = false;
const char* state_ = "not_initialized";
const char* write_ = "none";
volatile LONG first_wide_ = 0; // the one circle_fill_wide row

// The setting as one printable token for the log line: "-" when unset, "?" when too long, every character outside
// 0x21..0x7e shown as '?'.
void printable(const wchar_t* text, DWORD length, char* out) {
    if (!length) {
        out[0] = '-';
        out[1] = 0;
        return;
    }
    if (length >= sites::setting_capacity) {
        out[0] = '?';
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
// FNV-1a 64 of [va, va + length) read through engine_patch::read_code; false when a byte is unreadable.
bool region_hash(std::uintptr_t va, unsigned length, std::uint64_t* out) {
    unsigned char chunk[256];
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned done = 0; done < length;) {
        const unsigned n = length - done < sizeof chunk ? length - done : unsigned(sizeof chunk);
        if (!engine_patch::read_code(va + done, chunk, n)) return false;
        h = sites::fnv1a64(chunk, n, h);
        done += n;
    }
    *out = h;
    return true;
}
// The site window and every region the port mirrors or calls (fail closed); nullptr when all match.
const char* windows_verified() {
    unsigned char now[sites::window_length]{};
    if (!engine_patch::read_code(sites::site_va, now, sites::window_length) ||
        std::memcmp(now, sites::expected_window, sites::window_length))
        return "site_mismatch";
    for (const sites::Region& r : sites::regions) {
        std::uint64_t h = 0;
        if (!region_hash(r.va, r.length, &h) || h != r.fnv1a64) return r.va == sites::site_va ? "routine_mismatch" : "helper_mismatch";
    }
    return nullptr;
}
const char* install() {
    write_ = "none";
    if (!engine_patch::install_window_open()) return "late_claim";
    if (const char* mismatch = windows_verified()) return mismatch;
    if (!pin_self()) return "pin_failed";
    engine_patch::SiteSpec spec{};
    spec.name = sites::claim_spec.name;
    spec.address = sites::claim_spec.address;
    spec.length = sites::claim_spec.length;
    spec.ret_pop = sites::claim_spec.ret_pop;
    spec.rel32_offset = sites::claim_spec.rel32_offset;
    std::memcpy(spec.expected, sites::claim_spec.expected, sites::site_length);
    site_ = engine_patch::Site{};
    const bool claimed = engine_patch::claim(site_, spec);
    if (claimed || site_.patched_in || !std::strcmp(site_.status, "patch_rolled_back"))
        write_ = site_.atomic_write ? "atomic" : "plain";
    live_ = site_.patched_in;
    if (!claimed) return live_ ? "rollback_failed" : site_.status;
    // The continuation word (the tail) is published before the thunk goes in front of it.
    x3m_circle_fill_continue = reinterpret_cast<std::uintptr_t>(*site_.entry);
    const char* failure = nullptr;
    unsigned char now[sites::site_length]{};
    if (!x3m_circle_fill_continue || !engine_patch::push_front(site_, reinterpret_cast<void*>(&x3m_circle_fill_thunk)))
        failure = "chain_failed";
    else if (!engine_patch::read_code(sites::site_va, now, sites::site_length) || std::memcmp(now, site_.patched, 5) ||
             std::memcmp(now + 5, sites::expected_window + 5, sites::site_length - 5) ||
             *site_.entry != reinterpret_cast<void*>(&x3m_circle_fill_thunk))
        failure = "readback_failed";
    if (!failure) return "ok";
    engine_patch::restore(site_); // the one claim: rollback is its restore
    live_ = site_.patched_in;
    return live_ ? "rollback_failed" : failure;
}
std::uint32_t read32(const unsigned char* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
std::int16_t read16(const unsigned char* p) {
    std::int16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
}

extern "C" {
std::uintptr_t x3m_circle_fill_continue = 0;
void __cdecl x3m_circle_fill_call_leaf(std::uintptr_t fn, void* surface, std::int32_t x0, std::int32_t y, std::int32_t x1,
                                       std::uint32_t colour);

// The wide path (r > 813 texels). Same order of effects as 0x004ef620: nothing drawn without a backing surface or with
// surface flag 0x200000; the lock when +0 is null (the result stored, +0x10 |= 2 on success); the class; the spans.
std::uint32_t __cdecl x3m_circle_fill_wide(std::uint32_t* frame) {
    const std::uint32_t* args = frame + sites::frame_args;
    const std::int32_t r = sites::s16(args[sites::arg_r]);
    if (r <= sites::stock_radius_max) return 0;
    std::int64_t B = 0;
    if (!sites::semi_axis(std::int64_t(r) * r, static_cast<std::int32_t>(args[sites::arg_aspect]), &B)) return 0;
    unsigned char* surface = reinterpret_cast<unsigned char*>(static_cast<std::uintptr_t>(frame[sites::frame_edi]));
    const std::uint32_t backing = read32(surface + sites::object_backing);
    if (!backing || (read32(surface + sites::object_flags) & sites::flag_no_draw)) return 1;
    if (!read32(surface + sites::object_bits)) {
        using Lock = std::uint32_t(__cdecl*)(std::uint32_t, void*, std::uint32_t);
        std::uint32_t out[sites::lock_out_bytes / 4 + 1] = {};
        const std::uint32_t bits = reinterpret_cast<Lock>(sites::lock_va)(backing, out, 0);
        std::memcpy(surface + sites::object_bits, &bits, 4);
        if (!bits) return 1;
        const std::uint32_t flags = read32(surface + sites::object_flags) | sites::flag_locked;
        std::memcpy(surface + sites::object_flags, &flags, 4);
    }
    const std::int32_t pixel_class = x3m_circle_fill_call_class(sites::class_va, surface);
    const std::uintptr_t leaf = sites::leaf_for_class(pixel_class);
    if (!leaf) return 0; // the stock routine finds the surface locked and prints its Unknown Blittype line
    const std::int32_t width = read16(surface + sites::object_width), height = read16(surface + sites::object_height);
    const std::uint32_t colour = args[sites::arg_colour];
    sites::fill(static_cast<std::int32_t>(args[sites::arg_cx]), static_cast<std::int32_t>(args[sites::arg_cy]), r, B, width, height,
                [&](std::int32_t x0, std::int32_t y, std::int32_t x1) { x3m_circle_fill_call_leaf(leaf, surface, x0, y, x1, colour); });
    if (!first_wide_ && !InterlockedExchange(&first_wide_, 1)) {
        const DWORD error = GetLastError();
        x3m::log("circle_fill_wide r=%ld cx=%ld cy=%ld aspect=%ld surface=%dx%d class=%ld leaf=%08lx", static_cast<long>(r),
                 static_cast<long>(static_cast<std::int32_t>(args[sites::arg_cx])), static_cast<long>(static_cast<std::int32_t>(args[sites::arg_cy])),
                 static_cast<long>(static_cast<std::int32_t>(args[sites::arg_aspect])), int(width), int(height), static_cast<long>(pixel_class),
                 static_cast<unsigned long>(leaf));
        SetLastError(error);
    }
    return 1;
}
}

// The thunk. The fast path reads the radius as 0x004ef62a does (movsx of the word at [esp+0xc]) and, at most 813,
// jumps to the tail (the displaced `sub esp,0x4c; mov eax,[esp+0x58]`, then 0x004ef627): EAX and EFLAGS are dead at the
// entry (the displaced mov writes EAX, the sub writes the flags). The wide path saves every register and EFLAGS, clears
// DF for the C ABI, and puts the C result into the saved EAX: non-zero returns to the caller with a plain `ret` (the
// caller pops its 0x14 bytes; EAX is unused there: 0x0048bee0 tail-jumps to 0x004f5110, which writes EAX first), zero
// runs the stock routine from its first instruction with every register as the caller left it (EAX dead). The shims
// call the class function with ECX = surface, EAX = 0, and a span leaf with EDI = surface and the five cdecl
// arguments, as 0x004ef6b4 and the leaf calls do.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_circle_fill_thunk
_x3m_circle_fill_thunk:
    movsx eax, word ptr [esp+12]
    cmp eax, 813
    jg 1f
    jmp dword ptr [_x3m_circle_fill_continue]
1:  pushfd
    pushad
    cld
    mov eax, esp
    push eax
    call _x3m_circle_fill_wide
    add esp, 4
    mov dword ptr [esp+28], eax
    popad
    popfd
    test eax, eax
    jz 2f
    ret
2:  jmp dword ptr [_x3m_circle_fill_continue]
    .p2align 4
    .globl _x3m_circle_fill_call_class
_x3m_circle_fill_call_class:
    mov ecx, dword ptr [esp+8]
    xor eax, eax
    call dword ptr [esp+4]
    movsx eax, ax
    ret
    .p2align 4
    .globl _x3m_circle_fill_call_leaf
_x3m_circle_fill_call_leaf:
    push edi
    mov edi, dword ptr [esp+12]
    push dword ptr [esp+28]
    push dword ptr [esp+28]
    push dword ptr [esp+28]
    push dword ptr [esp+28]
    push edi
    call dword ptr [esp+28]
    add esp, 20
    pop edi
    ret
    .att_syntax
)");

namespace x3m::circle_fill {
bool initialize() {
    const DWORD error = GetLastError();
    if (live_) {
        SetLastError(error);
        return std::strcmp(state_, "ok") == 0;
    }
    // Unset or empty = the DLL default (on); 1..31 characters must be exactly on or off; anything else is refused and
    // nothing is patched.
    wchar_t text[sites::setting_capacity]{};
    const DWORD length = x3m::config::get(L"X3M_CIRCLE_FILL_FIX", text, sites::setting_capacity);
    char setting[sites::setting_capacity]{};
    printable(text, length, setting);
    sites::Mode mode = sites::default_mode;
    bool parsed = true;
    const char* reason;
    write_ = "none";
    if (length >= sites::setting_capacity) {
        reason = "too_long";
        parsed = false;
    } else if (length && !sites::parse_mode(text, &mode)) {
        reason = "invalid_setting";
        parsed = false;
    } else if (mode == sites::Mode::off)
        reason = "off";
    else
        reason = !object_trace::executable_verified() ? "executable_mismatch" : install();
    state_ = reason;
    const bool applied = !std::strcmp(reason, "ok");
    // rollback_failed leaves the site registered with a jump that may still be live: not reported as a refusal.
    const char* status = applied                              ? "patched"
                         : live_                              ? "patched_unverified"
                         : parsed && mode == sites::Mode::off ? "off"
                                                              : "refused";
    log("circle_fill site=%08lx status=%s reason=%s mode=%s setting=%s write=%s threshold=%ld continue=%08lx",
        static_cast<unsigned long>(sites::site_va), status, reason, parsed ? sites::mode_name(mode) : "-", setting, write_,
        static_cast<long>(sites::stock_radius_max), static_cast<unsigned long>(applied ? x3m_circle_fill_continue : 0));
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!live_) return true;
    const DWORD error = GetLastError();
    unsigned char found[sites::site_length]{};
    const bool found_read = engine_patch::read_code(sites::site_va, found, sites::site_length);
    const char* reason;
    if (found_read && !std::memcmp(found, sites::expected_window, sites::site_length))
        reason = "restored"; // already the engine's bytes: no write
    else if (!found_read || std::memcmp(found, site_.patched, 5))
        reason = "restore_not_owned"; // someone else's bytes: no write
    else {
        engine_patch::restore(site_);
        unsigned char back[sites::site_length]{};
        const bool back_ok = engine_patch::read_code(sites::site_va, back, sites::site_length) &&
                             !std::memcmp(back, sites::expected_window, sites::site_length);
        reason = back_ok && !std::strcmp(site_.status, "restored") ? "restored" : "restore_failed";
        if (!back_ok) site_.patched_in = true; // still ours: the next shutdown() tries again
    }
    if (!std::strcmp(reason, "restored")) {
        live_ = false;
        site_ = engine_patch::Site{};
    } // the thunk stays (the module is pinned; a thread may still be inside it)
    state_ = reason;
    // One row, written straight to the log's OS handle: this runs inside DllMain (dynamic unload), where the capture
    // lock must not be taken.
    const HANDLE handle = log_handle();
    if (handle && handle != INVALID_HANDLE_VALUE) {
        char line[128];
        const int n = std::snprintf(line, sizeof line, "circle_fill_restore site=%08lx status=%s registered=%u\n",
                                    static_cast<unsigned long>(sites::site_va), reason, live_ ? 1u : 0u);
        DWORD written = 0;
        if (n > 0 && unsigned(n) < sizeof line) WriteFile(handle, line, DWORD(n), &written, nullptr);
    }
    SetLastError(error);
    return !live_;
}
const char* state() {
    return state_;
}
bool patched() {
    return live_;
}
}
