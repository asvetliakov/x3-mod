// FL edition of x3m::engine_effects_patch (src/proxy/engine_effects_patch.h; the ap target keeps
// src/proxy/engine_effects_patch.cpp): the two call redirects of X3FL.exe's per-ship engine effect routine
// (docs/architecture/engine-effects-modern.md, "FL port, step 1"; sites and ABI: docs/reverse-engineering/fl/
// engine-effects.md, "Verified 2026-11-02 against X3FL.exe"; constants: engine_effects_sites_fl.h).
//
//   base+0x015075  call base+0x0150e0   the effect instance (emitter sprite, engine lens flare). ECX = 0, EDX = k,
//                                       eight stack dwords (obj at [esp+8]), plain RET, the caller pops 0x20.
//   base+0x0150aa  call base+0x013640   the Particles3 trail. Four stack dwords (obj at [esp+0xc]), RET 0x10.
//
// Each stub compares the int16 class word obj+0x48 with 7: a ship returns at once (A `xor eax,eax; ret`, B
// `xor eax,eax; ret 0x10`), every other class (10, missiles) tail-jumps to the original callee with ECX, EDX, EAX,
// EBX, ESI, EDI, EBP and the stack exactly as the call left them (EAX is saved around the test; only the status flags
// differ, which both callees write before reading). No Win32 call, no x87/SSE, no memory written: LastError, XMM,
// x87, MXCSR and DF stay the engine's. Integer-only and stateless, so reentrant.
//
// Install (backend-load path, inside the engine_patch install window): X3FL.exe identity (fl::module_base() != 0),
// then both windows at base + RVA before any write (A's 41 bytes exactly; B's 42 with its one fixup equal to base +
// 0x1a83b0), then B with one lock cmpxchg8b and A with the plain copy (its rel32 crosses a qword: only while the
// routine cannot run, before the first Present), each read back. Both or none: when A fails, B is restored.
// shutdown() (dynamic unload only) restores A then B, each only over this module's five bytes.
#include "engine_effects_fl.h"
#include "../../proxy/config.h"
#include "../../proxy/engine_effects_option.h"
#include "../../proxy/engine_patch.h"
#include "engine_effects_sites_fl.h"
#include "executable_identity_fl.h"
#include <windows.h>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace engine_patch = x3m::engine_patch;
namespace S = x3m::fl::engine_effects_fl;
namespace option = x3m::engine_effects::option;
constexpr unsigned setting_capacity = 16;
bool patched_ = false;   // a site may hold this module's call (registered for shutdown)
bool installed_ = false; // both redirects live
const char* state_ = "not_initialized";
const char* site_state_[2] = {"not_initialized", "not_initialized"};
bool written_[2] = {false, false};
engine_patch::CallSite site_[2]{}; // 0 = A, 1 = B

// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the engine calls into it.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE &&
           module != nullptr;
}
bool reached_write(const engine_patch::CallSite& s, bool claimed) {
    return claimed || s.patched_in || !std::strcmp(s.status, "patch_rolled_back");
}
// Read-back after a successful claim_call (as the AP module): nullptr when the site holds the redirect, else the
// original five bytes go back (readback_rolled_back) or stay registered (rollback_failed).
const char* confirm(engine_patch::CallSite& s) {
    unsigned char now[S::call_length]{};
    if (engine_patch::read_code(s.address, now, S::call_length) && !std::memcmp(now, s.patched, S::call_length))
        return nullptr;
    auto* code = reinterpret_cast<void*>(s.address);
    DWORD writable = 0, unused = 0;
    if (VirtualProtect(code, S::call_length, PAGE_EXECUTE_READWRITE, &writable)) {
        engine_patch::write_code(s.address, s.original, S::call_length);
        FlushInstructionCache(GetCurrentProcess(), code, S::call_length);
        VirtualProtect(code, S::call_length, s.protection, &unused);
    }
    unsigned char back[S::call_length]{};
    if (engine_patch::read_code(s.address, back, S::call_length) && !std::memcmp(back, s.original, S::call_length)) {
        s.patched_in = false;
        s.status = "readback_rolled_back";
        return s.status;
    }
    s.status = "rollback_failed"; // patched_in stays true
    return s.status;
}
void printable(const wchar_t* text, DWORD length, char* out) {
    if (!length || length >= setting_capacity) {
        out[0] = length ? '?' : '-';
        out[1] = 0;
        return;
    }
    for (DWORD i = 0; i < length; ++i) out[i] = (text[i] >= 0x21 && text[i] <= 0x7e) ? static_cast<char>(text[i]) : '?';
    out[length] = 0;
}
}

extern "C" {
std::uintptr_t x3m_engine_effects_continue_a = 0;
std::uintptr_t x3m_engine_effects_continue_b = 0;
}
// In: the engine's call at base+0x015075 (A: ECX = 0, EDX = k, obj [esp+8]) or base+0x0150aa (B: obj [esp+0xc]).
// After `push eax` obj is [esp+0xc] / [esp+0x10]. ECX and EDX are never touched (A's callee reads both).
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_engine_effects_stub_a
_x3m_engine_effects_stub_a:
    push eax
    mov eax, dword ptr [esp+0xc]
    cmp word ptr [eax+0x48], 7
    pop eax
    je .Lx3m_fl_engine_effects_skip_a
    jmp dword ptr [_x3m_engine_effects_continue_a]
.Lx3m_fl_engine_effects_skip_a:
    xor eax, eax
    ret
    .p2align 4
    .globl _x3m_engine_effects_stub_b
_x3m_engine_effects_stub_b:
    push eax
    mov eax, dword ptr [esp+0x10]
    cmp word ptr [eax+0x48], 7
    pop eax
    je .Lx3m_fl_engine_effects_skip_b
    jmp dword ptr [_x3m_engine_effects_continue_b]
.Lx3m_fl_engine_effects_skip_b:
    xor eax, eax
    ret 0x10
    .att_syntax
)");
static_assert(S::obj_arg_a + 4 == 0xc && S::obj_arg_b + 4 == 0x10 && S::class_offset == 0x48 && S::ship_class == 7 &&
                  S::callee_pop_b == 0x10,
              "the stubs' operands follow the FL site contract");

namespace x3m::engine_effects_patch {
bool install_base(std::uintptr_t base) {
    const DWORD error = GetLastError();
    const auto done = [&](const char* reason, const char* a_state, const char* b_state, bool ok) {
        state_ = reason;
        site_state_[0] = a_state;
        site_state_[1] = b_state;
        SetLastError(error);
        return ok;
    };
    if (patched_) {
        state_ = "already_installed";
        SetLastError(error);
        return false;
    }
    written_[0] = written_[1] = false;
    if (!base || (base & 0xffff)) return done("invalid_site", "invalid_site", "invalid_site", false);
    if (!engine_patch::install_window_open()) return done("late_claim", "late_claim", "late_claim", false);
    // Both windows before any write: a changed A never costs a B rollback.
    unsigned char a_bytes[S::window_a_length]{}, b_bytes[S::window_b_length]{};
    const bool a_read = engine_patch::read_code(base + S::window_a_rva, a_bytes, S::window_a_length);
    const bool b_read = engine_patch::read_code(base + S::window_b_rva, b_bytes, S::window_b_length);
    const bool a_matches = a_read && !S::plan_a(a_bytes);
    const bool b_matches = b_read && !S::plan_b(b_bytes, base);
    if (!a_matches || !b_matches)
        return done(!a_matches ? "bytes_mismatch_a" : "bytes_mismatch_b", a_matches ? "not_attempted" : "bytes_mismatch",
                    b_matches ? "not_attempted" : "bytes_mismatch", false);
    if (!pin_self()) return done("pin_failed", "pin_failed", "pin_failed", false);
    x3m_engine_effects_continue_a = base + S::target_a_rva; // published before the redirects go live
    x3m_engine_effects_continue_b = base + S::target_b_rva;
    site_[0] = engine_patch::CallSite{};
    site_[1] = engine_patch::CallSite{};
    // B: one lock cmpxchg8b, then the read-back.
    const bool b_claimed = engine_patch::claim_call(site_[1], base + S::b_site_rva, base + S::target_b_rva,
                                                    reinterpret_cast<void*>(&x3m_engine_effects_stub_b));
    written_[1] = reached_write(site_[1], b_claimed);
    const char* b_failed = b_claimed ? confirm(site_[1]) : site_[1].status;
    if (b_failed) {
        patched_ = site_[1].patched_in; // rollback_failed stays registered: shutdown() tries again
        return done(b_failed, "not_attempted", b_failed, false);
    }
    // A, only with B live: the plain copy (its rel32 crosses a qword), then the read-back.
    const bool a_claimed = engine_patch::claim_call(site_[0], base + S::a_site_rva, base + S::target_a_rva,
                                                    reinterpret_cast<void*>(&x3m_engine_effects_stub_a));
    written_[0] = reached_write(site_[0], a_claimed);
    const char* a_failed = a_claimed ? confirm(site_[0]) : site_[0].status;
    if (a_failed) {
        // Both or none: B goes back too.
        const bool b_back = engine_patch::restore_call(site_[1]);
        patched_ = site_[0].patched_in || site_[1].patched_in;
        return done(patched_ ? "rollback_failed" : a_failed, a_failed, b_back ? "rolled_back" : "rollback_failed",
                    false);
    }
    patched_ = installed_ = true;
    return done("ok", "active", "active", true);
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) {
        SetLastError(error);
        return installed_;
    }
    // Unset or empty = plumes (both redirects); 1..15 characters must be exactly native, off or plumes; anything else
    // is refused and nothing is patched.
    wchar_t text[setting_capacity]{};
    const DWORD length = x3m::config::get(L"X3M_ENGINE_EFFECTS", text, setting_capacity);
    char setting[setting_capacity]{};
    printable(text, length, setting);
    option::Mode mode = option::Mode::plumes;
    bool applied = false, parsed = true;
    const char* reason = nullptr;
    const std::uintptr_t base = x3m::fl::module_base();
    if (length >= setting_capacity) {
        reason = "too_long";
        parsed = false;
    } else if (length && !option::parse_mode(text, &mode)) {
        reason = "invalid_setting";
        parsed = false;
    } else if (mode == option::Mode::native)
        reason = "native";
    else if (!base)
        reason = "executable_mismatch";
    else
        applied = install_base(base);
    if (reason) {
        state_ = site_state_[0] = site_state_[1] = reason;
        written_[0] = written_[1] = false;
    }
    const std::uint32_t rva[2] = {S::a_site_rva, S::b_site_rva};
    for (unsigned i = 0; i < 2; ++i)
        log("engine_effects_patch site=%c va=%08lx rva=%06lx edition=fl state=%s reason=%s mode=%s setting=%s write=%s",
            i ? 'B' : 'A', static_cast<unsigned long>(base ? base + rva[i] : 0), static_cast<unsigned long>(rva[i]),
            site_state_[i], state_, parsed ? option::mode_name(mode) : "-", setting, write_path(i));
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    installed_ = false;
    for (unsigned i = 0; i < 2; ++i) { // A first, the reverse of the install order
        if (!site_[i].patched_in) continue;
        engine_patch::restore_call(site_[i]); // only over this module's five bytes (restore_not_owned otherwise)
        site_state_[i] = site_[i].status;
    }
    patched_ = site_[0].patched_in || site_[1].patched_in;
    state_ = patched_ ? "restore_failed" : "restored";
    SetLastError(error);
    return !patched_;
}
bool installed() {
    return installed_;
}
const char* state() {
    return state_;
}
const char* site_state(unsigned site) {
    return site < 2 ? site_state_[site] : "invalid_site";
}
const char* write_path(unsigned site) {
    if (site >= 2 || !written_[site]) return "none";
    return site_[site].atomic_write ? "atomic" : "plain";
}
}
