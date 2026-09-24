#include "lod_occlusion.h"
#include "lod_occlusion_sites.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

// Four bytes in place, no emitted code: `jne rel32 +0xc9` -> `jne rel32 0`,
// same opcode, same length (lod_occlusion_sites.h, whose install()/restore()
// carry the write, read-back, rollback and restore sequence the host test also
// drives). Nothing here runs per frame.
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
using namespace x3m::lod_occlusion::sites;
namespace engine_patch = x3m::engine_patch;
namespace sites = x3m::lod_occlusion::sites;
bool patched_ = false;
std::uintptr_t write_at_ = 0;
std::uint32_t site_protection = 0;
const char* state_ = "not_initialized";
const char* write_ = "none";

// The engine's code page through documented Win32 calls.
struct CodeOps {
    static constexpr std::uint32_t writable = PAGE_EXECUTE_READWRITE;
    bool read(std::uintptr_t at, unsigned char* out, unsigned n) { return engine_patch::read_code(at, out, n); }
    bool protect(std::uintptr_t at, unsigned n, std::uint32_t protection, std::uint32_t* previous) {
        DWORD old = 0;
        const bool ok = VirtualProtect(reinterpret_cast<void*>(at), n, protection, &old) != FALSE;
        if (ok) *previous = old;
        return ok;
    }
    // One lock cmpxchg8b for the engine's rel32 (offset 1 of its aligned 8-byte word), then the flush.
    bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic) {
        *atomic = engine_patch::write_code(at, bytes, n);
        return FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), n) != FALSE;
    }
};
// The setting as one printable token for the log line: "-" when unset, "?"
// when too long, every character outside 0x21..0x7e shown as '?'.
void printable(const wchar_t* text, DWORD length, char* out) {
    if (!length) { out[0] = '-'; out[1] = 0; return; }
    if (length >= setting_capacity) { out[0] = '?'; out[1] = 0; return; }
    for (DWORD i = 0; i < length; ++i) out[i] = (text[i] >= 0x21 && text[i] <= 0x7e) ? static_cast<char>(text[i]) : '?';
    out[length] = 0;
}
}

namespace x3m::lod_occlusion {
bool install_at(std::uintptr_t window_address) {
    if (patched_) { state_ = "already_installed"; return false; }
    CodeOps ops;
    bool live = false, atomic = false;
    const char* reason = sites::install(ops, window_address, engine_patch::install_window_open(), &live, &atomic, &site_protection);
    const bool wrote = live || !std::strcmp(reason, "patch_rolled_back") || !std::strcmp(reason, "rollback_unprotected");
    write_ = wrote ? (atomic ? "atomic" : "plain") : "none";
    if (live) { patched_ = true; write_at_ = window_address + write_offset; } // "ok", or rollback_failed kept registered for shutdown()
    state_ = reason;
    return patched_ && !std::strcmp(reason, "ok");
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) { SetLastError(error); return true; }
    // Unset or empty = the DLL default (record0, engine bytes); 1..31 characters must be
    // exactly record0 or all; anything else is refused and nothing is patched.
    wchar_t text[setting_capacity]{};
    const DWORD length = GetEnvironmentVariableW(L"X3M_LOD_OCCLUSION", text, setting_capacity);
    char setting[setting_capacity]{};
    printable(text, length, setting);
    Mode mode = default_mode;
    bool applied = false, parsed = true;
    if (length >= setting_capacity) { state_ = "too_long"; parsed = false; }
    else if (length && !parse_mode(text, &mode)) { state_ = "invalid_setting"; parsed = false; }
    else if (mode == Mode::record0) state_ = "record0";
    else if (!object_trace::executable_verified()) state_ = "executable_mismatch";
    else applied = install_at(window_va);
    // rollback_failed leaves the site registered with bytes that may still be patched (or neither
    // original nor patched): the one failure that modifies the engine is not reported as a refusal.
    const char* status = applied ? "patched" : patched_ ? "patched_unverified" : parsed && mode == Mode::record0 ? "off" : "refused";
    log("lod_occlusion site=%08lx status=%s reason=%s mode=%s setting=%s write=%s",
        static_cast<unsigned long>(site_va), status, state_, parsed ? mode_name(mode) : "-", setting, write_);
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    CodeOps ops;
    unsigned char found[write_length]{};
    bool found_read = false;
    const char* reason = sites::restore(ops, write_at_, site_protection, found, &found_read);
    if (!std::strcmp(reason, "restored")) patched_ = false; // restore_not_owned and restore_failed keep the site registered
    state_ = reason;
    // One row, written straight to the log's OS handle: this runs inside DllMain (dynamic
    // unload), where the capture lock must not be taken. site= is the jne, found= its rel32.
    const HANDLE handle = log_handle();
    if (handle && handle != INVALID_HANDLE_VALUE) {
        char line[160], bytes[12] = "--";
        if (found_read) std::snprintf(bytes, sizeof bytes, "%02x%02x%02x%02x", found[0], found[1], found[2], found[3]);
        const int n = std::snprintf(line, sizeof line, "lod_occlusion_restore site=%08lx status=%s found=%s registered=%u\n",
                                    static_cast<unsigned long>(write_at_ - write_offset + site_offset), reason, bytes, patched_ ? 1u : 0u);
        DWORD written = 0;
        if (n > 0 && unsigned(n) < sizeof line) WriteFile(handle, line, DWORD(n), &written, nullptr);
    }
    SetLastError(error);
    return !patched_;
}
const char* state() { return state_; }
const char* write_path() { return write_; }
bool patched() { return patched_; }
}
