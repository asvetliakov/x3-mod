#include "pause_key_only.h"
#include "config.h"
#include "pause_key_only_core.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <cstring>

// Twelve bytes in place, no emitted code: the replacement reads only SI/BX
// and writes only EFLAGS, which its own two JNE consume (core header).
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
using namespace x3m::pause_key_only::core;
namespace engine_patch = x3m::engine_patch;
bool patched_ = false;
std::uintptr_t site_ = 0;
std::uint32_t key_ = 0;
unsigned char replacement[site_length]{};
DWORD site_protection = 0;
const char* state_ = "disabled";
const char* write_ = "none";

bool protect(void* code, DWORD protection, DWORD* previous) {
    return VirtualProtect(code, site_length, protection, previous) != FALSE;
}
// Writes the twelve bytes and flushes. The span 0x004043a5..0x004043b0
// straddles an 8-byte word, so the engine site takes the plain copy: the
// install window guarantees the main loop has not started, and the wait loop
// runs only on the main thread (its single caller, 0x00403db8).
bool swap(void* code, const unsigned char* bytes, bool* atomic) {
    const bool used_atomic = engine_patch::write_code(reinterpret_cast<std::uintptr_t>(code), bytes, site_length);
    if (atomic) *atomic = used_atomic;
    return FlushInstructionCache(GetCurrentProcess(), code, site_length) != FALSE;
}
}

namespace x3m::pause_key_only {
bool install_at(std::uintptr_t window_address, std::uint32_t key) {
    if (patched_) { state_ = "already_installed"; return false; }
    const char* reason = nullptr;
    write_ = "none";
    unsigned char current[window_length]{};
    if (!window_address) reason = "invalid_site";
    else if (!key_valid(key)) reason = "bad_key";
    else if (!engine_patch::install_window_open()) reason = "late_claim";
    else if (!engine_patch::read_code(window_address, current, window_length)) reason = "bytes_mismatch";
    else reason = plan(current, key, replacement);
    if (!reason) {
        const std::uintptr_t site = window_address + site_offset;
        auto* code = reinterpret_cast<unsigned char*>(site);
        if (!protect(code, PAGE_EXECUTE_READWRITE, &site_protection)) reason = "protect_failed";
        else {
            patched_ = true; site_ = site; key_ = key; // ownership published before the mutation, so a failed rollback stays registered
            bool atomic = false;
            const bool flushed = swap(code, replacement, &atomic);
            write_ = atomic ? "atomic" : "plain";
            unsigned char readback[site_length]{};
            const bool verified = flushed && engine_patch::read_code(site, readback, site_length) && !std::memcmp(readback, replacement, site_length);
            DWORD unused = 0;
            const bool protected_again = protect(code, site_protection, &unused);
            if (!verified || !protected_again) {
                // Rollback: the page is still writable when the re-protect failed; otherwise make it so. The
                // outcome is judged by a read-back of the original bytes, not by the protection calls.
                DWORD writable = 0;
                bool reprotected = false;
                if (!protected_again || protect(code, PAGE_EXECUTE_READWRITE, &writable)) {
                    swap(code, original, nullptr);
                    reprotected = protect(code, site_protection, &unused);
                }
                unsigned char back[site_length]{};
                if (engine_patch::read_code(site, back, site_length) && !std::memcmp(back, original, site_length)) {
                    // Vanilla bytes are back: nothing is live, whatever the protection calls returned.
                    patched_ = false; key_ = 0;
                    reason = reprotected ? "patch_rolled_back" : "rollback_unprotected"; // the latter: original bytes, page left writable
                } else reason = "rollback_failed"; // live bytes kept registered for shutdown()
            } else reason = "ok";
        }
    }
    state_ = reason;
    return patched_ && !std::strcmp(reason, "ok");
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) { SetLastError(error); return true; }
    wchar_t setting[4]{};
    const DWORD length = x3m::config::get(L"X3M_PAUSE_KEY_ONLY", setting, 4);
    if (length == 0) { state_ = "disabled"; SetLastError(error); return false; }
    bool applied = false;
    const bool requested = length == 1 && setting[0] == L'1';
    // X3M_PAUSE_KEY: absent = DIK_PAUSE's engine code; present but not a key code = refused, nothing patched.
    std::uint32_t key = default_key;
    wchar_t key_text[16]{};
    const DWORD key_length = x3m::config::get(L"X3M_PAUSE_KEY", key_text, 16);
    const bool key_ok = key_length == 0 || (key_length < 16 && parse_key(key_text, &key));
    if (!key_ok) key = 0;
    if (!requested) state_ = "disabled";
    else if (!key_ok) state_ = "bad_key";
    else if (!object_trace::executable_verified()) state_ = "executable_mismatch";
    else applied = install_at(window_va, key);
    log("pause_key_only patched=%u key=0x%lx reason=%s requested=%u site=0x%08lx write=%s",
        patched_ ? 1u : 0u, static_cast<unsigned long>(key), state_, requested ? 1u : 0u, static_cast<unsigned long>(site_va), write_);
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    unsigned char current[site_length]{};
    if (!engine_patch::read_code(site_, current, site_length) || std::memcmp(current, replacement, site_length)) {
        state_ = "restore_not_owned"; SetLastError(error); return false;
    }
    auto* code = reinterpret_cast<unsigned char*>(site_);
    DWORD previous = 0, unused = 0;
    if (!protect(code, PAGE_EXECUTE_READWRITE, &previous)) { state_ = "restore_protect_failed"; SetLastError(error); return false; }
    const bool flushed = swap(code, original, nullptr);
    const bool protected_again = protect(code, site_protection, &unused);
    patched_ = false; key_ = 0;
    state_ = flushed && protected_again ? "restored" : "restore_failed";
    SetLastError(error);
    return flushed && protected_again;
}
const char* state() { return state_; }
const char* write_path() { return write_; }
std::uint32_t key() { return patched_ ? key_ : 0; }
}
