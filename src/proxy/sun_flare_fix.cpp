#include "sun_flare_fix.h"
#include "config.h"
#include "sun_flare_fix_sites.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

// One engine_patch claim plus one emitted stub (sun_flare_fix_sites.h); no
// code of this module runs inside the engine, nothing runs per frame.
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace sites = x3m::sun_flare_fix::sites;
namespace engine_patch = x3m::engine_patch;
bool patched_ = false;
engine_patch::Site site_{};
std::uintptr_t site_at_ = 0, stub_ = 0;
const char* state_ = "not_initialized";
const char* write_ = "none";

// The setting as one printable token for the log line: "-" when unset, "?"
// when too long, every character outside 0x21..0x7e shown as '?'.
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
// The stub followed by its 4-aligned continuation slot; 0 when the arena is full.
std::uintptr_t emit_stub(void*** slot_out) {
    engine_patch::Emitter e(sites::stub_length + 8);
    if (!e.ok()) return 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    const std::uintptr_t slot = (at + sites::stub_length + 3) & ~std::uintptr_t(3);
    unsigned char code[sites::stub_length];
    sites::encode_stub(std::uint32_t(slot), code);
    e.bytes(code, sites::stub_length);
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < slot) e.byte(0xcc);
    e.dword(0);
    if (!e.finish()) return 0;
    *slot_out = reinterpret_cast<void**>(slot);
    return at;
}
// The claimed span reads back as our jump over the untouched sixth byte, and the chain head is the stub.
bool read_back(std::uintptr_t stub) {
    unsigned char now[sites::site_length]{};
    return engine_patch::read_code(site_at_, now, sites::site_length) && !std::memcmp(now, site_.patched, 5) &&
           now[5] == sites::expected_site[5] && site_.entry && *site_.entry == reinterpret_cast<void*>(stub);
}
// Takes the claim back after a failed chain link or read-back: the original bytes, or the site stays registered.
const char* take_back(const char* reason) {
    if (engine_patch::restore(site_)) {
        site_ = engine_patch::Site{};
        site_at_ = 0;
        return reason;
    }
    patched_ = true;
    site_.patched_in = true; // our jump may still be there: shutdown() tries again
    return "rollback_failed";
}
}

namespace x3m::sun_flare_fix {
bool install_at(std::uintptr_t window) {
    if (patched_) {
        state_ = "already_installed";
        return false;
    }
    const char* reason = nullptr;
    unsigned char current[sites::window_length]{};
    std::uintptr_t stub = 0;
    void** slot = nullptr;
    write_ = "none";
    if (!window)
        reason = "invalid_site";
    else if (!engine_patch::install_window_open())
        reason = "late_claim";
    else if (!engine_patch::read_code(window, current, sites::window_length))
        reason = "unreadable";
    else if ((reason = sites::plan(current)) != nullptr) {
    } else if (!(stub = emit_stub(&slot)))
        reason = "arena_full";
    if (!reason) {
        engine_patch::SiteSpec spec{};
        spec.name = sites::claim_spec.name;
        spec.address = window + sites::site_offset;
        spec.length = sites::site_length;
        spec.ret_pop = sites::claim_spec.ret_pop;
        spec.rel32_offset = sites::claim_spec.rel32_offset;
        std::memcpy(spec.expected, sites::expected_site, sites::site_length);
        site_ = engine_patch::Site{};
        site_at_ = spec.address;
        if (!engine_patch::claim(site_, spec)) {
            reason = site_.status;
            if (!std::strcmp(reason, "patch_rolled_back") || !std::strcmp(reason, "rollback_failed"))
                write_ = site_.atomic_write ? "atomic" : "plain";
            if (site_.patched_in) {
                patched_ = true;
                state_ = "rollback_failed";
                return false;
            } // registered for shutdown()
            site_ = engine_patch::Site{};
            site_at_ = 0;
        } else {
            write_ = site_.atomic_write ? "atomic" : "plain";
            // The site is live with its tail only (the engine's behaviour) until the stub is linked in.
            if (!engine_patch::store_pointer(slot, *site_.entry) ||
                !engine_patch::push_front(site_, reinterpret_cast<void*>(stub)))
                reason = take_back("chain_failed");
            else if (!read_back(stub))
                reason = take_back("readback_mismatch");
            else {
                patched_ = true;
                stub_ = stub;
                reason = "ok";
            }
        }
    }
    state_ = reason;
    return patched_ && !std::strcmp(reason, "ok");
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) {
        SetLastError(error);
        return stub_ != 0;
    } // live only after ok; a registered rollback_failed stays a failure
    // Unset or empty = the DLL default (off, engine bytes); 1..31 characters must be exactly on or off;
    // anything else is refused and nothing is patched.
    wchar_t text[sites::setting_capacity]{};
    const DWORD length = x3m::config::get(L"X3M_SUN_FLARE_FIX", text, sites::setting_capacity);
    char setting[sites::setting_capacity]{};
    printable(text, length, setting);
    sites::Mode mode = sites::default_mode;
    bool applied = false, parsed = true;
    write_ = "none";
    if (length >= sites::setting_capacity) {
        state_ = "too_long";
        parsed = false;
    } else if (length && !sites::parse_mode(text, &mode)) {
        state_ = "invalid_setting";
        parsed = false;
    } else if (mode == sites::Mode::off)
        state_ = "off";
    else if (!object_trace::executable_verified())
        state_ = "executable_mismatch";
    else
        applied = install_at(sites::window_va);
    // rollback_failed leaves the site registered with a jump that may still be live: not reported as a refusal.
    const char* status = applied                              ? "patched"
                         : patched_                           ? "patched_unverified"
                         : parsed && mode == sites::Mode::off ? "off"
                                                              : "refused";
    log("sun_flare_fix site=%08lx status=%s reason=%s mode=%s setting=%s write=%s stub=%08lx",
        static_cast<unsigned long>(sites::site_va), status, state_, parsed ? sites::mode_name(mode) : "-", setting,
        write_, static_cast<unsigned long>(stub_));
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    unsigned char found[sites::site_length]{};
    const bool found_read = engine_patch::read_code(site_at_, found, sites::site_length);
    const char* reason;
    if (found_read && !std::memcmp(found, sites::expected_site, sites::site_length))
        reason = "restored"; // already the engine's bytes: no write
    else if (!found_read || std::memcmp(found, site_.patched, 5))
        reason = "restore_not_owned"; // someone else's bytes: no write
    else {
        engine_patch::restore(site_);
        unsigned char back[sites::site_length]{};
        const bool back_ok = engine_patch::read_code(site_at_, back, sites::site_length) &&
                             !std::memcmp(back, sites::expected_site, sites::site_length);
        reason = back_ok && !std::strcmp(site_.status, "restored") ? "restored" : "restore_failed";
        if (!back_ok) site_.patched_in = true; // still ours: the next shutdown() tries again
    }
    const std::uintptr_t at = site_at_;
    if (!std::strcmp(reason, "restored")) {
        patched_ = false;
        stub_ = 0;
        site_ = engine_patch::Site{};
        site_at_ = 0;
    } // the stub and tail stay in the arena
    state_ = reason;
    // One row, written straight to the log's OS handle: this runs inside DllMain (dynamic
    // unload), where the capture lock must not be taken. found= is the span's six bytes.
    const HANDLE handle = log_handle();
    if (handle && handle != INVALID_HANDLE_VALUE) {
        char line[160], bytes[16] = "--";
        if (found_read)
            std::snprintf(bytes, sizeof bytes, "%02x%02x%02x%02x%02x%02x", found[0], found[1], found[2], found[3],
                          found[4], found[5]);
        const int n = std::snprintf(line, sizeof line,
                                    "sun_flare_fix_restore site=%08lx status=%s found=%s registered=%u\n",
                                    static_cast<unsigned long>(at), reason, bytes, patched_ ? 1u : 0u);
        DWORD written = 0;
        if (n > 0 && unsigned(n) < sizeof line) WriteFile(handle, line, DWORD(n), &written, nullptr);
    }
    SetLastError(error);
    return !patched_;
}
const char* state() {
    return state_;
}
const char* write_path() {
    return write_;
}
bool patched() {
    return patched_;
}
std::uintptr_t stub_address() {
    return patched_ ? stub_ : 0;
}
}
