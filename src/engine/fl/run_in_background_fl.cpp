// FL edition of x3m::run_in_background (src/proxy/run_in_background.h; the ap target keeps
// src/proxy/run_in_background.cpp): X3M_RUN_IN_BACKGROUND=1|0 (the AP parser of run_in_background_sites.h) on X3FL.exe.
// The game behaves as if `-runinbg` had been given: the startup decision's JE at base+0x0776ec (to the AND that clears
// bit 0x4000 of the input flags word when neither the argument nor the registry value RunInBackground asks for it)
// becomes two NOPs, so the existing OR path sets the bit for every choice (run_in_background_sites_fl.h;
// docs/reverse-engineering/fl/pause-background.md, section 2 and "Port (2026-11-03)"). With the setting off nothing is
// written: the argument and the registry stay authoritative. A later X2_SetRunInBackground script call still changes
// the bit. No thunk, no stub, no pointer into this module, no runtime row (nothing of ours runs when the startup decision
// does), nothing per frame.
// Backend-load path only: the FL proxy loads on the adapter probe's Direct3DCreate9 (startup 0x0772bc), which precedes
// the decision window 0x0776c2 on the startup path (docs/reverse-engineering/fl/startup.md); inside the engine_patch
// install window (late_claim after the first Present); X3FL.exe identity (fl::module_base()), the decision and pump-gate
// windows compared with their fixups bound to the base, VirtualProtect, one lock cmpxchg8b (the two bytes lie in one
// aligned qword), FlushInstructionCache, read-back, rollback judged by a read-back of the original bytes. One row with
// edition=fl. Device Reset is not involved. shutdown() (fixture only: the FL loader's unload calls are AP only) puts
// the JE back only over our NOPs.
#include "run_in_background_fl.h"
#include "../../proxy/config.h"
#include "../../proxy/engine_patch.h"
#include "../../proxy/run_in_background_sites.h"
#include "executable_identity_fl.h"
#include "inline_patch_fl.h"
#include "run_in_background_sites_fl.h"
#include <windows.h>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace engine_patch = x3m::engine_patch;
namespace sites = x3m::run_in_background::sites; // the AP parser (1 | 0 | unset)
namespace R = x3m::fl::run_in_background_fl;
namespace I = x3m::fl::inline_patch_fl;
bool patched_ = false;
std::uintptr_t site_ = 0;
std::uint32_t site_protection_ = 0;
const char* state_ = "not_initialized";
const char* write_ = "none";
#ifdef X3M_RUN_IN_BACKGROUND_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
#endif
struct CodeOps {
    static constexpr std::uint32_t writable = PAGE_EXECUTE_READWRITE;
    bool read(std::uintptr_t at, unsigned char* out, unsigned n) { return engine_patch::read_code(at, out, n); }
    bool protect(std::uintptr_t at, unsigned n, std::uint32_t protection, std::uint32_t* previous) {
        DWORD old = 0;
        const bool ok = VirtualProtect(reinterpret_cast<void*>(at), n, protection, &old) != FALSE;
        if (ok) *previous = old;
        return ok;
    }
    // The two bytes lie in one aligned qword: engine_patch::write_code takes one lock cmpxchg8b (*atomic true).
    bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic) {
        *atomic = engine_patch::write_code(at, bytes, n);
        return FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), n) != FALSE;
    }
};
bool read_window(std::uintptr_t at, unsigned char* out, unsigned n) {
    return engine_patch::read_code(at, out, n);
}
}

namespace x3m::run_in_background {
bool install_base(std::uintptr_t base) {
    const DWORD error = GetLastError();
    if (patched_) {
        state_ = "already_installed";
        SetLastError(error);
        return false;
    }
    write_ = "none";
    const char* reason = nullptr;
    if (!base || (base & 0xffff))
        reason = "invalid_site";
    else if (!engine_patch::install_window_open())
        reason = "late_claim";
    else
        reason = R::plan(base, read_window);
    if (!reason) {
        CodeOps ops;
        bool live = false, atomic = false;
        const std::uintptr_t at = base + R::site_rva;
        reason = I::install(ops, at, R::original, R::patched, R::site_length, &live, &atomic, &site_protection_);
        const bool wrote = live || !std::strcmp(reason, "patch_rolled_back") ||
                           !std::strcmp(reason, "rollback_unprotected");
        write_ = wrote ? (atomic ? "atomic" : "plain") : "none";
        if (live) { // "ok", or rollback_failed kept registered for shutdown()
            patched_ = true;
            site_ = at;
        }
    }
    state_ = reason;
    SetLastError(error);
    return patched_ && !std::strcmp(reason, "ok");
}
bool install_at(const Addresses& a) {
    const std::uint32_t rva = R::windows[R::w_decision].rva;
    return install_base(a.window >= rva ? a.window - rva : 0);
}
#ifdef X3M_RUN_IN_BACKGROUND_FL_FIXTURE
void fixture_base(std::uintptr_t base) {
    fixture_base_ = base;
}
#endif
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
#ifdef X3M_RUN_IN_BACKGROUND_FL_FIXTURE
    const std::uintptr_t base = fixture_base_ ? fixture_base_ : x3m::fl::module_base();
#else
    const std::uintptr_t base = x3m::fl::module_base();
#endif
    bool applied = false;
    write_ = "none";
    if (setting == sites::Setting::invalid)
        state_ = "invalid_setting";
    else if (setting == sites::Setting::off)
        state_ = length ? "setting_off" : "unset";
    else if (!base)
        state_ = "executable_mismatch";
    else
        applied = install_base(base);
    const char* status = applied                          ? "armed"
                         : patched_                       ? "armed_unverified"
                         : setting == sites::Setting::off ? "off"
                                                          : "refused";
    log("run_in_background site=0x%08lx rva=0x%06lx edition=fl status=%s reason=%s setting=%s value_before=- "
        "value_after=- write=%s handler=none",
        static_cast<unsigned long>(base ? base + R::site_rva : 0), static_cast<unsigned long>(R::site_rva), status,
        state_, setting_text, write_);
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    CodeOps ops;
    const char* reason = I::restore(ops, site_, R::original, R::patched, R::site_length, site_protection_);
    if (!std::strcmp(reason, "restored")) patched_ = false; // restore_not_owned / restore_failed stay registered
    state_ = reason;
    SetLastError(error);
    return !patched_;
}
const char* state() {
    return state_;
}
// No thunk on FL: nothing of ours runs when the startup decision does, so there is no site outcome to report.
const char* outcome() {
    return nullptr;
}
std::uint32_t flags_before() {
    return 0;
}
std::uint32_t flags_after() {
    return 0;
}
}
