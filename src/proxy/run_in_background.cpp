#include "run_in_background.h"
#include "config.h"
#include "run_in_background_sites.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <cstring>

// One call redirect, no emitted code: the thunk below lives in this module, which is pinned before the redirect
// goes live (the engine calls into it). The site runs once per process, during the game's init.
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace engine_patch = x3m::engine_patch;
namespace sites = x3m::run_in_background::sites;
bool patched_ = false;
const char* state_ = "not_initialized";
engine_patch::CallSite site_{};
std::uintptr_t slot_ = 0;
volatile LONG armed_ = 0; // 1 while the one write is still due; the thunk takes it with one exchange
const char* volatile outcome_ = nullptr;
std::uint32_t flags_before_ = 0, flags_after_ = 0;

bool window_matches(std::uintptr_t window) {
    unsigned char current[sites::window_length]{};
    return engine_patch::read_code(window, current, sites::window_length) && !sites::plan(current);
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the engine calls into it.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE &&
           module != nullptr;
}
}

extern "C" {
std::uintptr_t x3m_run_in_background_continue = sites::target_va;
// Runs on the game's init thread with every register and EFLAGS saved by the thunk. The game has just written
// the flags word through the same pointer (0x004033bb/0x004033c3), so the block is live.
void __cdecl x3m_run_in_background_apply() {
    const DWORD error = GetLastError();
    if (InterlockedExchange(&armed_, 0) == 1) {
        std::uint32_t* const flags = *reinterpret_cast<std::uint32_t* const volatile*>(slot_);
        if (!flags) {
            outcome_ = "refused";
            x3m::log("run_in_background site=0x%08lx status=refused reason=no_input_block setting=1 value_before=- "
                     "value_after=- flags_before=- flags_after=-",
                     static_cast<unsigned long>(site_.address));
        } else {
            const auto planned = sites::apply(*reinterpret_cast<volatile std::uint32_t*>(flags));
            std::uint32_t before = planned.before, after = planned.after;
            if (planned.write) { // one locked write; its return is the word it replaced
                before = static_cast<std::uint32_t>(InterlockedOr(reinterpret_cast<volatile LONG*>(flags),
                                                                  static_cast<LONG>(sites::run_in_background_bit)));
                after = before | sites::run_in_background_bit;
            }
            const auto result = sites::apply(before); // patched when this write set the bit, else already
            flags_before_ = before;
            flags_after_ = after;
            outcome_ = result.status;
            x3m::log("run_in_background site=0x%08lx status=%s reason=ok setting=1 value_before=%u value_after=%u "
                     "flags_before=0x%08lx flags_after=0x%08lx",
                     static_cast<unsigned long>(site_.address), result.status,
                     (before & sites::run_in_background_bit) ? 1u : 0u,
                     (after & sites::run_in_background_bit) ? 1u : 0u, static_cast<unsigned long>(before),
                     static_cast<unsigned long>(after));
        }
    }
    SetLastError(error);
}
}
// In: the engine's `call 0x004d2580` (no argument; the callee reads no register). Everything is saved around the
// C function (EFLAGS with DF, then the eight general registers); DF is cleared for the C ABI and restored by the
// popfd. The final `jmp` leaves the stack exactly as the call left it: the callee returns to 0x004033ce.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_run_in_background_thunk
_x3m_run_in_background_thunk:
    pushfd
    pushad
    cld
    call _x3m_run_in_background_apply
    popad
    popfd
    jmp dword ptr [_x3m_run_in_background_continue]
    .att_syntax
)");

namespace x3m::run_in_background {
bool install_at(const Addresses& a) {
    const DWORD error = GetLastError();
    const auto done = [&](const char* reason, bool ok) {
        state_ = reason;
        SetLastError(error);
        return ok;
    };
    if (patched_) return done("already_installed", false);
    if (!a.window || !a.target || !a.slot) return done("invalid_site", false);
    if (!engine_patch::install_window_open()) return done("late_claim", false);
    if (!window_matches(a.window)) return done("bytes_mismatch", false);
    if (!pin_self()) return done("pin_failed", false);
    x3m_run_in_background_continue = a.target;
    slot_ = a.slot;
    InterlockedExchange(&armed_, 1); // published before the redirect goes live
    outcome_ = nullptr;
    site_ = engine_patch::CallSite{};
    if (!engine_patch::claim_call(site_, a.window + sites::site_offset, a.target,
                                  reinterpret_cast<void*>(&x3m_run_in_background_thunk))) {
        InterlockedExchange(&armed_, 0);
        if (site_.patched_in && !engine_patch::restore_call(site_)) {
            patched_ = true;
            return done("rollback_failed", false);
        } // registered: shutdown() tries again
        return done(site_.status, false);
    }
    patched_ = true;
    return done("ok", true);
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) {
        SetLastError(error);
        return true;
    }
    wchar_t text[4]{};
    const DWORD length = x3m::config::get(L"X3M_RUN_IN_BACKGROUND", text, 4);
    const sites::Setting setting = length < 4 ? sites::parse_setting(text, length) : sites::Setting::invalid;
    const char* setting_text = setting == sites::Setting::on    ? "1"
                               : setting == sites::Setting::off ? (length ? "0" : "-")
                                                                : "?";
    bool applied = false;
    if (setting == sites::Setting::invalid)
        state_ = "invalid_setting";
    else if (setting == sites::Setting::off)
        state_ = length ? "setting_off" : "unset";
    else if (!object_trace::executable_verified())
        state_ = "executable_mismatch";
    else
        applied = install_at(Addresses{sites::window_va, sites::target_va, sites::input_block_slot_va});
    const char* status = applied                          ? "armed"
                         : patched_                       ? "armed_unverified"
                         : setting == sites::Setting::off ? "off"
                                                          : "refused";
    log("run_in_background site=0x%08lx status=%s reason=%s setting=%s value_before=- value_after=- write=%s "
        "handler=0x%08lx",
        static_cast<unsigned long>(sites::site_va), status, state_, setting_text,
        site_.patched_in ? (site_.atomic_write ? "atomic" : "plain") : "none",
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_run_in_background_thunk)));
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    InterlockedExchange(&armed_, 0);
    const bool back = engine_patch::restore_call(site_);
    if (back) patched_ = false;
    state_ = back ? "restored" : "restore_failed";
    SetLastError(error);
    return back;
}
const char* state() {
    return state_;
}
const char* outcome() {
    return outcome_;
}
std::uint32_t flags_before() {
    return flags_before_;
}
std::uint32_t flags_after() {
    return flags_after_;
}
}
