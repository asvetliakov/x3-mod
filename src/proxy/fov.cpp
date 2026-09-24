#include "fov.h"
#include "fov_sites.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

// Four immediate bytes in place, no emitted code: `MOV dword [ESI+0x24],0x4000`
// -> `MOV dword [ESI+0x24],F`, same opcode, same length (fov_sites.h, whose
// install()/restore() carry the write, read-back, rollback and restore
// sequence the host test also drives). Per frame only current_focus() (two
// validated reads, called by the small-parts cull when it is armed) and the
// flag test in present().
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace engine_patch = x3m::engine_patch;
namespace engine_memory = x3m::engine_memory;
namespace sites = x3m::fov::sites;
bool patched_ = false;
bool identity_ = false;          // the executable was verified at initialize(): engine reads allowed
std::uintptr_t write_at_ = 0;
std::uint32_t site_protection = 0;
std::uint32_t focus_ = sites::engine_focus;  // the value in the immediate while patched_
std::uint32_t configured_ = sites::engine_focus;
const char* state_ = "not_initialized";
const char* write_ = "none";
const char* registry_ = "skipped";
bool confirm_done_ = false, absent_logged_ = false;
unsigned confirm_polls_ = 0;
constexpr unsigned confirm_poll_limit = 1u << 20;  // Presents to wait for a registry that never appears

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
    // One lock cmpxchg8b for the imm32 (the upper half of its aligned 8-byte word), then the flush.
    bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic) {
        *atomic = engine_patch::write_code(at, bytes, n);
        return FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), n) != FALSE;
    }
};
// The setting as one printable token for the log line: "-" when unset, "?"
// when too long, every character outside 0x21..0x7e shown as '?'.
void printable(const wchar_t* text, DWORD length, char* out) {
    if (!length) { out[0] = '-'; out[1] = 0; return; }
    if (length >= sites::setting_capacity) { out[0] = '?'; out[1] = 0; return; }
    for (DWORD i = 0; i < length; ++i) out[i] = (text[i] >= 0x21 && text[i] <= 0x7e) ? static_cast<char>(text[i]) : '?';
    out[length] = 0;
}
bool code_is(std::uintptr_t at, const unsigned char* expected, unsigned n) {
    unsigned char now[16]{};
    return n <= sizeof now && engine_patch::read_code(at, now, n) && !std::memcmp(now, expected, n);
}
// The registry pointer and its +0x24 through the validated reader; false when
// either is unreadable, the pointer is 0 or misaligned, or the value is not
// plausible. *registry is set whenever the slot itself was read.
bool read_registry(std::uint32_t* registry, std::uint32_t* focus) {
    *registry = 0;
    if (!engine_memory::read(sites::registry_slot_va, registry, sizeof *registry) || !*registry || (*registry & 3u)) return false;
    return engine_memory::read(std::uintptr_t(*registry) + sites::registry_focus_offset, focus, sizeof *focus) && sites::plausible_focus(*focus);
}
// The one-off data write of registry+0x24 when the registry already exists at
// install: a plausible current value, a committed writable page (VirtualQuery),
// then one aligned InterlockedCompareExchange from the value read, so a
// concurrent INS_SetFocus store wins and is not overwritten.
const char* write_registry(std::uint32_t focus, std::uint32_t* before, bool* before_read) {
    *before_read = false;
    std::uint32_t registry = 0;
    if (!engine_memory::read(sites::registry_slot_va, &registry, sizeof registry)) return "skipped";
    if (!registry) return "absent";
    if (registry & 3u) return "skipped";
    const std::uintptr_t field = std::uintptr_t(registry) + sites::registry_focus_offset;
    if (!engine_memory::read(field, before, sizeof *before)) return "skipped";
    *before_read = true;
    if (!sites::plausible_focus(*before)) return "skipped";
    MEMORY_BASIC_INFORMATION m{};
    if (VirtualQuery(reinterpret_cast<const void*>(field), &m, sizeof m) != sizeof m || m.State != MEM_COMMIT ||
        (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(m.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) ||
        field + sizeof(LONG) > reinterpret_cast<std::uintptr_t>(m.BaseAddress) + m.RegionSize)
        return "skipped";
    const LONG expected = static_cast<LONG>(*before);
    if (InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(field), static_cast<LONG>(focus), expected) != expected) return "skipped";
    return "written";
}
}

namespace x3m::fov {
bool install_at(std::uintptr_t window_address, std::uint32_t focus) {
    if (patched_) { state_ = "already_installed"; return false; }
    CodeOps ops;
    bool live = false, atomic = false;
    const char* reason = sites::install(ops, window_address, engine_patch::install_window_open(), focus, &live, &atomic, &site_protection);
    const bool wrote = live || !std::strcmp(reason, "patch_rolled_back") || !std::strcmp(reason, "rollback_unprotected");
    write_ = wrote ? (atomic ? "atomic" : "plain") : "none";
    if (live) { patched_ = true; focus_ = focus; write_at_ = window_address + sites::write_offset; } // "ok", or rollback_failed kept registered for shutdown()
    state_ = reason;
    return patched_ && !std::strcmp(reason, "ok");
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) { SetLastError(error); return true; }
    // Unset, empty or `game` = the engine's 0x4000 (nothing patched); 1..31 characters of
    // decimal vertical degrees in [36, 120]; anything else is refused and nothing is patched.
    wchar_t text[sites::setting_capacity]{};
    const DWORD length = GetEnvironmentVariableW(L"X3M_FOV", text, sites::setting_capacity);
    char setting[sites::setting_capacity]{};
    printable(text, length, setting);
    identity_ = object_trace::executable_verified();
    registry_ = "skipped";
    write_ = "none";
    double degrees = 0;
    std::uint32_t focus = sites::engine_focus;
    bool applied = false, requested = false;
    const sites::Parse parsed = length >= sites::setting_capacity ? sites::Parse::invalid : sites::parse_setting(text, &degrees);
    if (length >= sites::setting_capacity) state_ = "too_long";
    else if (parsed == sites::Parse::invalid) state_ = "invalid_setting";
    else if (parsed == sites::Parse::game) state_ = "game";
    else if (!sites::in_range(degrees)) state_ = "out_of_range";
    else if ((focus = sites::focus_for_vertical(degrees)) == sites::engine_focus) state_ = "engine_value";
    else if (!identity_) { state_ = "executable_mismatch"; requested = true; }
    else if (!code_is(sites::reader_va, sites::expected_reader, sites::reader_length) ||
             !code_is(sites::setfocus_va, sites::expected_setfocus, sites::setfocus_length)) { state_ = "reader_mismatch"; requested = true; }
    else { requested = true; applied = install_at(sites::window_va, focus); }
    std::uint32_t before = 0;
    bool before_read = false;
    if (applied) registry_ = write_registry(focus, &before, &before_read);
    if (patched_ && !applied) {
        // rollback_failed: the immediate may hold neither the engine's nor our value. Report what the
        // constructor will actually store (read back); unreadable or implausible bytes leave it unknown
        // (0), which the small-parts cull treats as a vanilla frame.
        unsigned char now[sites::write_length]{};
        const std::uint32_t found = engine_patch::read_code(write_at_, now, sites::write_length)
            ? std::uint32_t(now[0]) | std::uint32_t(now[1]) << 8 | std::uint32_t(now[2]) << 16 | std::uint32_t(now[3]) << 24 : 0u;
        configured_ = sites::plausible_focus(found) ? found : 0u;
    } else {
        configured_ = patched_ ? focus_ : sites::engine_focus;
    }
    // rollback_failed leaves the site registered with bytes that may still be patched (or neither
    // original nor patched): the one failure that modifies the engine is not reported as a refusal.
    const bool off = !requested && (parsed == sites::Parse::game || !std::strcmp(state_, "engine_value"));
    const char* status = applied ? "patched" : patched_ ? "patched_unverified" : off ? "off" : "refused";
    char previous[16] = "-";
    if (before_read) std::snprintf(previous, sizeof previous, "0x%04lx", static_cast<unsigned long>(before));
    log("fov site=%08lx status=%s reason=%s value=0x%04lx vertical_deg=%.2f setting=%s write=%s registry=%s registry_before=%s",
        static_cast<unsigned long>(sites::write_va), status, state_, static_cast<unsigned long>(configured_),
        sites::vertical_for_focus(configured_), setting, write_, registry_, previous);
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    CodeOps ops;
    unsigned char found[sites::write_length]{};
    bool found_read = false;
    const char* reason = sites::restore(ops, write_at_, focus_, site_protection, found, &found_read);
    if (!std::strcmp(reason, "restored")) { patched_ = false; configured_ = sites::engine_focus; } // restore_not_owned and restore_failed keep the site registered
    state_ = reason;
    // One row, written straight to the log's OS handle: this runs inside DllMain (dynamic
    // unload), where the capture lock must not be taken. found= is the imm32 as stored.
    const HANDLE handle = log_handle();
    if (handle && handle != INVALID_HANDLE_VALUE) {
        char line[160], bytes[12] = "--";
        if (found_read) std::snprintf(bytes, sizeof bytes, "%02x%02x%02x%02x", found[0], found[1], found[2], found[3]);
        const int n = std::snprintf(line, sizeof line, "fov_restore site=%08lx status=%s found=%s registered=%u\n",
                                    static_cast<unsigned long>(write_at_), reason, bytes, patched_ ? 1u : 0u);
        DWORD written = 0;
        if (n > 0 && unsigned(n) < sizeof line) WriteFile(handle, line, DWORD(n), &written, nullptr);
    }
    SetLastError(error);
    return !patched_;
}
std::uint32_t configured_focus() { return configured_; }  // 0 = unknown (rollback_failed with unreadable or implausible bytes)
std::uint32_t current_focus() {
    if (!identity_) return configured_;
    const DWORD error = GetLastError();
    std::uint32_t registry = 0, focus = 0;
    const bool live = read_registry(&registry, &focus);
    SetLastError(error);
    return live ? focus : configured_;
}
void present(unsigned long long frame) {
    if (confirm_done_ || !identity_) return;
    const DWORD error = GetLastError();
    std::uint32_t registry = 0, focus = 0;
    const bool live = read_registry(&registry, &focus);
    if (live || registry) {
        // registry+0x24 against the configured value; the sector camera's +0x298 is not
        // exposed by the camera reader (camera_state latches the projection buffers only).
        log("fov_confirm frame=%llu registry=%08lx focus=0x%04lx expected=0x%04lx match=%u vertical_deg=%.2f camera=skipped",
            frame, static_cast<unsigned long>(registry), static_cast<unsigned long>(live ? focus : 0u), static_cast<unsigned long>(configured_),
            live && focus == configured_ ? 1u : 0u, live ? sites::vertical_for_focus(focus) : 0.0);
        confirm_done_ = true;
    } else {
        if (!absent_logged_) {
            log("fov_confirm frame=%llu registry=absent focus=- expected=0x%04lx match=0 vertical_deg=- camera=skipped",
                frame, static_cast<unsigned long>(configured_));
            absent_logged_ = true;
        }
        if (++confirm_polls_ >= confirm_poll_limit) confirm_done_ = true;
    }
    SetLastError(error);
}
const char* state() { return state_; }
const char* write_path() { return write_; }
const char* registry_state() { return registry_; }
bool patched() { return patched_; }
}
