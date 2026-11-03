// FL edition of x3m::pause_key_only (src/proxy/pause_key_only.h; the ap target keeps src/proxy/pause_key_only.cpp):
// X3M_PAUSE_KEY_ONLY=1 (+ X3M_PAUSE_KEY, the AP parser and key rule of pause_key_only_core.h) on X3FL.exe. The flight
// pause's wait loop leaves on any changed key or a new mouse or joystick button; the 13 bytes at base+0x07880c become
// `cmp si,KEY ; jne mouse test ; cmp esi,[esp+0x14] ; jne exit`, so only the configured key (default 0x1b5 = DIK_PAUSE)
// or a new mouse/joystick button ends it (pause_key_only_sites_fl.h; docs/reverse-engineering/fl/pause-background.md,
// section 1 and "Port (2026-11-03)"). Pure instruction bytes: no stub, no pointer into this module, nothing per frame.
// Backend-load path only (the adapter probe's Direct3DCreate9, before the startup routine reaches the main loop that
// owns the pause wait), inside the engine_patch install window (late_claim after the first Present); X3FL.exe identity
// (fl::module_base()), both windows compared with their fixups bound to the base, VirtualProtect, a plain write (the
// span crosses qwords; the wait loop runs only on the main thread, which blocks in the loader's InitOnce at the
// probe until the install is done, and the probe precedes the main loop on every startup path),
// FlushInstructionCache, read-back, rollback judged by a read-back of the original bytes. One row with edition=fl.
// Device Reset is not involved. shutdown() (fixture only: the FL loader's unload calls are AP only) puts the original
// bytes back only over ours.
#include "pause_key_only_fl.h"
#include "../../proxy/config.h"
#include "../../proxy/engine_patch.h"
#include "../../proxy/pause_key_only_core.h"
#include "executable_identity_fl.h"
#include "inline_patch_fl.h"
#include "pause_key_only_sites_fl.h"
#include <windows.h>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace engine_patch = x3m::engine_patch;
namespace core = x3m::pause_key_only::core;
namespace P = x3m::fl::pause_key_only_fl;
namespace I = x3m::fl::inline_patch_fl;
bool patched_ = false;
std::uintptr_t site_ = 0;
std::uint32_t key_ = 0;
std::uint32_t site_protection_ = 0;
unsigned char replacement_[P::site_length]{};
const char* state_ = "disabled";
const char* write_ = "none";
#ifdef X3M_PAUSE_KEY_ONLY_FL_FIXTURE
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
    // The span crosses qwords: engine_patch::write_code takes the plain copy (*atomic false), then the flush.
    bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic) {
        *atomic = engine_patch::write_code(at, bytes, n);
        return FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), n) != FALSE;
    }
};
bool read_window(std::uintptr_t at, unsigned char* out, unsigned n) {
    return engine_patch::read_code(at, out, n);
}
}

namespace x3m::pause_key_only {
bool install_base(std::uintptr_t base, std::uint32_t key) {
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
    else if (!core::key_valid(key))
        reason = "bad_key";
    else if (!engine_patch::install_window_open())
        reason = "late_claim";
    else if (!(reason = P::plan(base, read_window)) && !P::encode_site(key, replacement_))
        reason = "bad_key";
    if (!reason) {
        CodeOps ops;
        bool live = false, atomic = false;
        const std::uintptr_t at = base + P::site_rva;
        reason = I::install(ops, at, P::original, replacement_, P::site_length, &live, &atomic, &site_protection_);
        const bool wrote = live || !std::strcmp(reason, "patch_rolled_back") ||
                           !std::strcmp(reason, "rollback_unprotected");
        write_ = wrote ? (atomic ? "atomic" : "plain") : "none";
        if (live) { // "ok", or rollback_failed kept registered for shutdown()
            patched_ = true;
            site_ = at;
            key_ = key;
        }
    }
    state_ = reason;
    SetLastError(error);
    return patched_ && !std::strcmp(reason, "ok");
}
bool install_at(std::uintptr_t window, std::uint32_t key) {
    const std::uint32_t rva = P::windows[P::w_loop].rva;
    return install_base(window >= rva ? window - rva : 0, key);
}
#ifdef X3M_PAUSE_KEY_ONLY_FL_FIXTURE
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
    wchar_t setting[4]{};
    const DWORD length = x3m::config::get(L"X3M_PAUSE_KEY_ONLY", setting, 4);
    if (length == 0) {
        state_ = "disabled";
        SetLastError(error);
        return false;
    }
#ifdef X3M_PAUSE_KEY_ONLY_FL_FIXTURE
    const std::uintptr_t base = fixture_base_ ? fixture_base_ : x3m::fl::module_base();
#else
    const std::uintptr_t base = x3m::fl::module_base();
#endif
    bool applied = false;
    const bool requested = length == 1 && setting[0] == L'1';
    // X3M_PAUSE_KEY: absent = DIK_PAUSE's engine code; present but not a key code = refused, nothing patched.
    std::uint32_t key = core::default_key;
    wchar_t key_text[16]{};
    const DWORD key_length = x3m::config::get(L"X3M_PAUSE_KEY", key_text, 16);
    const bool key_ok = key_length == 0 || (key_length < 16 && core::parse_key(key_text, &key));
    if (!key_ok) key = 0;
    write_ = "none";
    if (!requested)
        state_ = "disabled";
    else if (!key_ok)
        state_ = "bad_key";
    else if (!base)
        state_ = "executable_mismatch";
    else
        applied = install_base(base, key);
    log("pause_key_only patched=%u key=0x%lx reason=%s requested=%u site=0x%08lx rva=0x%06lx edition=fl write=%s",
        patched_ ? 1u : 0u, static_cast<unsigned long>(key), state_, requested ? 1u : 0u,
        static_cast<unsigned long>(base ? base + P::site_rva : 0), static_cast<unsigned long>(P::site_rva), write_);
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    CodeOps ops;
    const char* reason = I::restore(ops, site_, P::original, replacement_, P::site_length, site_protection_);
    if (!std::strcmp(reason, "restored")) { // restore_not_owned / restore_failed stay registered
        patched_ = false;
        key_ = 0;
    }
    state_ = reason;
    SetLastError(error);
    return !patched_;
}
const char* state() {
    return state_;
}
const char* write_path() {
    return write_;
}
std::uint32_t key() {
    return patched_ ? key_ : 0;
}
}
