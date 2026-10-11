#include "dust_leak_fix.h"
#include "config.h"
#include "dust_leak_fix_sites.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

// One engine_patch claim plus one emitted stub (dust_leak_fix_sites.h); no
// code of this module runs inside the engine. Per frame the proxy side does
// nothing; report() reads the counter every 300 frames under a tier.
static_assert(sizeof(void*) == 4, "x86 code patching only");

// The stub's hit counter (INC dword [abs32]): a module global, never in the
// engine_patch arena, which is PAGE_EXECUTE_READ outside emission (native
// Windows faults on the write; 2026-10-11). The module is pinned before the
// stub that names it is emitted.
volatile std::uint32_t x3m_dust_leak_fix_hits = 0; // declared extern "C" in the header

namespace {
namespace sites = x3m::dust_leak_fix::sites;
namespace engine_patch = x3m::engine_patch;
bool patched_ = false;
engine_patch::Site site_{};
std::uintptr_t site_at_ = 0, stub_ = 0;
std::uint32_t reported_ = 0;
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
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN),
// so the stub's counter operand can never point into freed memory.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE &&
           module != nullptr;
}
// The stub followed by its 4-aligned continuation slot (the only arena word it names; read, never written).
// nullptr and *stub_out set on success, else arena_full or counter_in_arena (nothing emitted).
const char* emit_stub(std::uintptr_t release, std::uintptr_t* stub_out, void*** slot_out) {
    engine_patch::Emitter e(sites::stub_length + 3 + 4);
    if (!e.ok()) return "arena_full";
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    const std::uintptr_t slot = (at + sites::stub_length + 3) & ~std::uintptr_t(3);
    unsigned char code[sites::stub_length];
    sites::encode_stub(std::uint32_t(at), std::uint32_t(release),
                       std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_dust_leak_fix_hits)), std::uint32_t(slot),
                       code);
    // The INC must not write into the arena (PAGE_EXECUTE_READ outside emission); checked before any byte is
    // written, so a refusal leaves the arena cursor unchanged.
    if (!sites::counter_outside(sites::encoded_hits(code),
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(engine_patch::arena_base())),
                                engine_patch::arena_capacity()))
        return "counter_in_arena";
    e.bytes(code, sites::stub_length);
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < slot) e.byte(0xcc);
    e.dword(0);
    if (!e.finish()) return "arena_full";
    *slot_out = reinterpret_cast<void**>(slot);
    *stub_out = at;
    return nullptr;
}
// The claimed span reads back as our jump, and the chain head is the stub.
bool read_back(std::uintptr_t stub) {
    unsigned char now[sites::site_length]{};
    return engine_patch::read_code(site_at_, now, sites::site_length) && !std::memcmp(now, site_.patched, 5) &&
           site_.entry && *site_.entry == reinterpret_cast<void*>(stub);
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

namespace x3m::dust_leak_fix {
bool install_at(std::uintptr_t window, std::uintptr_t release) {
    if (patched_) {
        state_ = "already_installed";
        return false;
    }
    const char* reason = nullptr;
    unsigned char current[sites::window_length]{};
    std::uintptr_t stub = 0;
    void** slot = nullptr;
    write_ = "none";
    if (!window || !release)
        reason = "invalid_site";
    else if (!engine_patch::install_window_open())
        reason = "late_claim";
    else if (!engine_patch::read_code(window, current, sites::window_length))
        reason = "unreadable";
    else if ((reason = sites::plan(current)) != nullptr) {
    } else if (!pin_self())
        reason = "pin_failed";
    else
        reason = emit_stub(release, &stub, &slot);
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
            x3m_dust_leak_fix_hits = 0; // this install's count starts before the stub is reachable
            // The site is live with its tail only (the engine's behaviour) until the stub is linked in.
            if (!engine_patch::store_pointer(slot, *site_.entry) ||
                !engine_patch::push_front(site_, reinterpret_cast<void*>(stub)))
                reason = take_back("chain_failed");
            else if (!read_back(stub))
                reason = take_back("readback_mismatch");
            else {
                patched_ = true;
                stub_ = stub;
                reported_ = 0;
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
    // Unset or empty = the DLL default (on, the stub); 1..31 characters must be exactly on or off;
    // anything else is refused and nothing is patched.
    wchar_t text[sites::setting_capacity]{};
    const DWORD length = x3m::config::get(L"X3M_DUST_LEAK_FIX", text, sites::setting_capacity);
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
        applied = install_at(sites::window_va, sites::release_va);
    // rollback_failed leaves the site registered with a jump that may still be live: not reported as a refusal.
    const char* status = applied                              ? "patched"
                         : patched_                           ? "patched_unverified"
                         : parsed && mode == sites::Mode::off ? "off"
                                                              : "refused";
    log("dust_leak_fix site=%08lx status=%s reason=%s mode=%s setting=%s write=%s stub=%08lx",
        static_cast<unsigned long>(sites::site_va), status, state_, parsed ? sites::mode_name(mode) : "-", setting,
        write_, static_cast<unsigned long>(stub_));
    SetLastError(error);
    return applied;
}
void report(unsigned frame) {
    if (!stub_) return;
    const std::uint32_t total = x3m_dust_leak_fix_hits;
    log("dust_leak_fix hits=%lu total=%lu frame=%u", static_cast<unsigned long>(total - reported_),
        static_cast<unsigned long>(total), frame);
    reported_ = total;
}
std::uint32_t hits() {
    return stub_ ? x3m_dust_leak_fix_hits : 0;
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
    } // the stub and the tail stay in the arena (the module is pinned: the counter stays too)
    state_ = reason;
    // One row, written straight to the log's OS handle: this runs inside DllMain (dynamic
    // unload), where the capture lock must not be taken. found= is the span's five bytes.
    const HANDLE handle = log_handle();
    if (handle && handle != INVALID_HANDLE_VALUE) {
        char line[160], bytes[16] = "--";
        if (found_read)
            std::snprintf(bytes, sizeof bytes, "%02x%02x%02x%02x%02x", found[0], found[1], found[2], found[3], found[4]);
        const int n = std::snprintf(line, sizeof line,
                                    "dust_leak_fix_restore site=%08lx status=%s found=%s registered=%u\n",
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
