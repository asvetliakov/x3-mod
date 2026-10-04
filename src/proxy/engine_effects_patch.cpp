#include "engine_effects_patch.h"
#include "config.h"
#include "engine_effects_sites.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <cstring>

// Two call redirects, no emitted code: the stubs below live in this module, which is pinned before the first
// redirect goes live (the engine calls into it every frame).
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace engine_patch = x3m::engine_patch;
namespace sites = x3m::engine_effects_patch::sites;
bool patched_ = false;   // a site may hold this module's call (registered for shutdown)
bool installed_ = false; // both redirects live
const char* state_ = "not_initialized";
const char* site_state_[2] = {"not_initialized", "not_initialized"};
bool written_[2] = {false, false};
engine_patch::CallSite site_[2]{};

bool window_matches(std::uintptr_t window, const unsigned char* expected, unsigned length) {
    unsigned char current[64]{};
    return length <= sizeof current && engine_patch::read_code(window, current, length) &&
           !std::memcmp(current, expected, length);
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the engine calls into it.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE &&
           module != nullptr;
}
// claim_call wrote the five bytes; whether it reached the store at all (for the write= field).
bool reached_write(const engine_patch::CallSite& s, bool claimed) {
    return claimed || s.patched_in || !std::strcmp(s.status, "patch_rolled_back");
}
// Read-back after a successful claim_call: nullptr when the site holds the redirect. Otherwise the original five
// bytes go back (the window was verified just before and the install window excludes another writer), judged by a
// second read-back: readback_rolled_back, or rollback_failed with the site kept registered for shutdown().
const char* confirm(engine_patch::CallSite& s) {
    unsigned char now[sites::call_length]{};
    if (engine_patch::read_code(s.address, now, sites::call_length) && !std::memcmp(now, s.patched, sites::call_length))
        return nullptr;
    auto* code = reinterpret_cast<void*>(s.address);
    DWORD writable = 0, unused = 0;
    if (VirtualProtect(code, sites::call_length, PAGE_EXECUTE_READWRITE, &writable)) {
        engine_patch::write_code(s.address, s.original, sites::call_length);
        FlushInstructionCache(GetCurrentProcess(), code, sites::call_length);
        VirtualProtect(code, sites::call_length, s.protection, &unused);
    }
    unsigned char back[sites::call_length]{};
    if (engine_patch::read_code(s.address, back, sites::call_length) &&
        !std::memcmp(back, s.original, sites::call_length)) {
        s.patched_in = false;
        s.status = "readback_rolled_back";
        return s.status;
    }
    s.status = "rollback_failed"; // patched_in stays true
    return s.status;
}
// The setting as one printable token for the log row: "-" when unset, "?" when too long, every character outside
// 0x21..0x7e shown as '?'.
void printable(const wchar_t* text, DWORD length, char* out) {
    if (!length || length >= sites::setting_capacity) {
        out[0] = length ? '?' : '-';
        out[1] = 0;
        return;
    }
    for (DWORD i = 0; i < length; ++i) out[i] = (text[i] >= 0x21 && text[i] <= 0x7e) ? static_cast<char>(text[i]) : '?';
    out[length] = 0;
}
}

extern "C" {
std::uintptr_t x3m_engine_effects_continue_a = sites::target_a_va;
std::uintptr_t x3m_engine_effects_continue_b = sites::target_b_va;
}
// In: the engine's call at 0x004147eb (A) or 0x0041482c (B), obj at [esp+0x10] (A) or [esp+0x0c] (B). EAX is saved
// around the class test so the forward path reaches the callee with every register as the call left them (only the
// status flags differ, which both callees write before reading). The skip path changes only EAX and EFLAGS, both
// dead at the caller; A's caller pops its 0x28 bytes, B's stub pops the stdcall 0x10 as the callee would.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_engine_effects_stub_a
_x3m_engine_effects_stub_a:
    push eax
    mov eax, dword ptr [esp+0x14]
    cmp word ptr [eax+0x48], 7
    pop eax
    je .Lx3m_engine_effects_skip_a
    jmp dword ptr [_x3m_engine_effects_continue_a]
.Lx3m_engine_effects_skip_a:
    xor eax, eax
    ret
    .p2align 4
    .globl _x3m_engine_effects_stub_b
_x3m_engine_effects_stub_b:
    push eax
    mov eax, dword ptr [esp+0x10]
    cmp word ptr [eax+0x48], 7
    pop eax
    je .Lx3m_engine_effects_skip_b
    jmp dword ptr [_x3m_engine_effects_continue_b]
.Lx3m_engine_effects_skip_b:
    xor eax, eax
    ret 0x10
    .att_syntax
)");
static_assert(sites::obj_arg_a + 4 == 0x14 && sites::obj_arg_b + 4 == 0x10 && sites::class_offset == 0x48 &&
                  sites::ship_class == 7 && sites::callee_pop_b == 0x10,
              "the stubs' operands follow the site contract");

namespace x3m::engine_effects_patch {
bool install_at(const Addresses& a) {
    const DWORD error = GetLastError();
    const auto done = [&](const char* reason, const char* a_state, const char* b_state, bool ok) {
        state_ = reason;
        site_state_[0] = a_state;
        site_state_[1] = b_state;
        SetLastError(error);
        return ok;
    };
    if (patched_) {
        state_ = "already_installed"; // the registered sites keep their states
        SetLastError(error);
        return false;
    }
    written_[0] = written_[1] = false;
    if (!a.window_a || !a.target_a || !a.window_b || !a.target_b)
        return done("invalid_site", "invalid_site", "invalid_site", false);
    if (!engine_patch::install_window_open()) return done("late_claim", "late_claim", "late_claim", false);
    // Both windows before any write: a changed B never costs an A rollback.
    const bool a_matches = window_matches(a.window_a, sites::expected_window_a, sites::window_a_length);
    const bool b_matches = window_matches(a.window_b, sites::expected_window_b, sites::window_b_length);
    if (!a_matches || !b_matches) {
        const char* reason = !a_matches ? "bytes_mismatch_a" : "bytes_mismatch_b";
        return done(reason, a_matches ? "not_attempted" : "bytes_mismatch",
                    b_matches ? "not_attempted" : "bytes_mismatch", false);
    }
    if (!pin_self()) return done("pin_failed", "pin_failed", "pin_failed", false);
    x3m_engine_effects_continue_a = a.target_a; // published before the redirects go live
    x3m_engine_effects_continue_b = a.target_b;
    site_[0] = engine_patch::CallSite{};
    site_[1] = engine_patch::CallSite{};
    // A: one lock cmpxchg8b, then the read-back.
    const bool a_claimed = engine_patch::claim_call(site_[0], a.window_a + sites::site_a_offset, a.target_a,
                                                    reinterpret_cast<void*>(&x3m_engine_effects_stub_a));
    written_[0] = reached_write(site_[0], a_claimed);
    const char* a_failed = a_claimed ? confirm(site_[0]) : site_[0].status;
    if (a_failed) {
        patched_ = site_[0].patched_in; // rollback_failed stays registered: shutdown() tries again
        return done(a_failed, a_failed, "not_attempted", false);
    }
    // B, only with A live: the plain copy (its rel32 crosses a qword), then the read-back.
    const bool b_claimed = engine_patch::claim_call(site_[1], a.window_b + sites::site_b_offset, a.target_b,
                                                    reinterpret_cast<void*>(&x3m_engine_effects_stub_b));
    written_[1] = reached_write(site_[1], b_claimed);
    const char* b_failed = b_claimed ? confirm(site_[1]) : site_[1].status;
    if (b_failed) {
        // Both or none: A goes back too.
        const bool a_back = engine_patch::restore_call(site_[0]);
        patched_ = site_[0].patched_in || site_[1].patched_in;
        return done(patched_ ? "rollback_failed" : b_failed, a_back ? "rolled_back" : "rollback_failed", b_failed,
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
    // Unset or empty = plumes (both redirects); 1..15 characters must be exactly native, off or plumes; anything
    // else is refused and nothing is patched (the engine's bytes, as native).
    wchar_t text[sites::setting_capacity]{};
    const DWORD length = x3m::config::get(L"X3M_ENGINE_EFFECTS", text, sites::setting_capacity);
    char setting[sites::setting_capacity]{};
    printable(text, length, setting);
    sites::Mode mode = sites::default_mode;
    bool applied = false, parsed = true;
    const char* reason = nullptr;
    if (length >= sites::setting_capacity) {
        reason = "too_long";
        parsed = false;
    } else if (length && !sites::parse_mode(text, &mode)) {
        reason = "invalid_setting";
        parsed = false;
    } else if (mode == sites::Mode::native)
        reason = "native";
    else if (!object_trace::executable_verified())
        reason = "executable_mismatch";
    else
        applied = install_at(Addresses{sites::window_a_va, sites::target_a_va, sites::window_b_va, sites::target_b_va});
    if (reason) {
        state_ = site_state_[0] = site_state_[1] = reason;
        written_[0] = written_[1] = false;
    }
    const std::uintptr_t va[2] = {sites::a_site_va, sites::b_site_va};
    for (unsigned i = 0; i < 2; ++i)
        log("engine_effects_patch site=%c va=%08lx state=%s reason=%s mode=%s setting=%s write=%s", i ? 'B' : 'A',
            static_cast<unsigned long>(va[i]), site_state_[i], state_, parsed ? sites::mode_name(mode) : "-", setting,
            write_path(i));
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    installed_ = false;
    for (unsigned i = 2; i-- > 0;) { // B first, the reverse of the install order
        if (!site_[i].patched_in) continue;
        engine_patch::restore_call(site_[i]); // only over this module's five bytes (restore_not_owned otherwise)
        site_state_[i] = site_[i].status;
    }
    patched_ = site_[0].patched_in || site_[1].patched_in; // a refused restore stays registered for a later detach
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
