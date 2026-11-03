// FL edition of x3m::dust_leak_fix (src/proxy/dust_leak_fix.h; the ap target keeps src/proxy/dust_leak_fix.cpp): the
// dust-scene leak fix on X3FL.exe (docs/reverse-engineering/fl/dust-light.md, section 1 and "Port (2026-11-03)"; sites,
// windows and the FL stub: dust_leak_fix_sites_fl.h; the X3M_DUST_LEAK_FIX parser, the counter check and the host model
// are AP's, src/proxy/dust_leak_fix_sites.h).
//
//   base+0x020ac4  the fill loop's common tail CMP EBX,[ESP+0x18]; JL base+0x020950 (ten bytes, two whole
//                  instructions) claimed through engine_patch (rel32_offset 6 re-bases the JL in the tail; the five-
//                  byte jump crosses a qword: plain write) with the release-and-leave stub in front: a never-attached
//                  node (ESI != 0, node+0x1c == 0) goes to the engine's own release base+0x0a3300 (ECX = node) and
//                  EBX = the target count ends the fill for this invocation; every other arrival is unchanged.
//
// Install (backend-load path, inside the engine_patch install window; a later call is refused, late_claim): X3FL.exe
// identity (fl::module_base() != 0), every window of dust_leak_fix_sites_fl.h at base + RVA with its fixup bound to
// base, before anything is emitted; the module pinned (the stub's INC names this module's counter); the stub and its
// slot published in the never-freed arena; the claim, the chain link and a read-back of the ten bytes. Any failure after
// the jump went in puts the original bytes back; a rollback that cannot complete keeps the site registered
// (patched_unverified). One row with edition=fl. Per frame the proxy runs nothing; report() writes one hits row per 300
// frames under --perf/--debug. Device Reset does not touch it. shutdown() is for the fixture: the FL loader's
// DLL_PROCESS_DETACH unload calls are AP only (as for cull_small_parts_fl.cpp and terran_station_lod_fl.cpp; the module is
// pinned before the stub is emitted anyway). It puts the CMP back only over our jump and only while the install window
// is open: the five bytes cross a qword, so once the engine runs they stay (restore_retained), as fov_fl.cpp keeps its
// crossing literal.
#include "dust_leak_fix_fl.h"
#include "../../proxy/config.h"
#include "../../proxy/dust_leak_fix_sites.h"
#include "../../proxy/engine_patch.h"
#include "dust_leak_fix_sites_fl.h"
#include "executable_identity_fl.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
HANDLE log_handle() noexcept;
}
static_assert(sizeof(void*) == 4, "x86 code patching only");

// The stub's hit counter (INC dword [abs32]): a module global, never in the PAGE_EXECUTE_READ arena; declared
// extern "C" in dust_leak_fix.h. The module is pinned before the stub that names it is emitted.
volatile std::uint32_t x3m_dust_leak_fix_hits = 0;

namespace {
namespace engine_patch = x3m::engine_patch;
namespace sites = x3m::dust_leak_fix::sites; // the AP parser, counter check and host model
namespace F = x3m::fl::dust_leak_fix_fl;
bool patched_ = false;
engine_patch::Site site_{};
std::uintptr_t site_at_ = 0, stub_ = 0;
std::uint32_t reported_ = 0;
const char* state_ = "not_initialized";
const char* write_ = "none";
#ifdef X3M_DUST_LEAK_FIX_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
#endif

void printable(const wchar_t* text, DWORD length, char* out) {
    if (!length || length >= sites::setting_capacity) {
        out[0] = length ? '?' : '-';
        out[1] = 0;
        return;
    }
    for (DWORD i = 0; i < length; ++i) out[i] = (text[i] >= 0x21 && text[i] <= 0x7e) ? static_cast<char>(text[i]) : '?';
    out[length] = 0;
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN), so the stub's counter operand can
// never point into freed memory.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE &&
           module != nullptr;
}
// The stub followed by its 4-aligned continuation slot. nullptr and *stub_out set on success, else arena_full or
// counter_in_arena (nothing emitted).
const char* emit_stub(std::uintptr_t release, std::uintptr_t* stub_out, void*** slot_out) {
    engine_patch::Emitter e(F::stub_length + 3 + 4);
    if (!e.ok()) return "arena_full";
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    const std::uintptr_t slot = (at + F::stub_length + 3) & ~std::uintptr_t(3);
    unsigned char code[F::stub_length];
    F::encode_stub(std::uint32_t(at), std::uint32_t(reinterpret_cast<std::uintptr_t>(&GetLastError)),
                   std::uint32_t(release), std::uint32_t(reinterpret_cast<std::uintptr_t>(&SetLastError)),
                   std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_dust_leak_fix_hits)), std::uint32_t(slot), code);
    if (!sites::counter_outside(F::encoded_hits(code),
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(engine_patch::arena_base())),
                                engine_patch::arena_capacity()))
        return "counter_in_arena";
    e.bytes(code, F::stub_length);
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < slot) e.byte(0xcc);
    e.dword(0);
    if (!e.finish()) return "arena_full";
    *slot_out = reinterpret_cast<void**>(slot);
    *stub_out = at;
    return nullptr;
}
// The claimed span reads back as our jump plus the JL's untouched rest, and the chain head is the stub.
bool read_back(std::uintptr_t stub) {
    unsigned char now[F::site_length]{};
    return engine_patch::read_code(site_at_, now, F::site_length) && F::claimed_span(now, site_.patched) &&
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
std::uintptr_t current_base() {
#ifdef X3M_DUST_LEAK_FIX_FL_FIXTURE
    if (fixture_base_) return fixture_base_;
#endif
    return x3m::fl::module_base();
}
}

namespace x3m::dust_leak_fix {
bool install_base(std::uintptr_t base) {
    const DWORD error = GetLastError();
    if (patched_) {
        state_ = "already_installed";
        SetLastError(error);
        return false;
    }
    const char* reason = nullptr;
    std::uintptr_t stub = 0;
    void** slot = nullptr;
    write_ = "none";
    if (!base || (base & 0xffff))
        reason = "invalid_site";
    else if (!engine_patch::install_window_open())
        reason = "late_claim";
    else {
        // Every window before anything is emitted or written.
        for (const F::Window& w : F::windows) {
            unsigned char now[F::max_window]{};
            if (!engine_patch::read_code(base + w.rva, now, w.length)) {
                reason = "unreadable";
                break;
            }
            if ((reason = F::plan(w, now, base)) != nullptr) break;
        }
        if (!reason && !pin_self()) reason = "pin_failed";
        if (!reason) reason = emit_stub(base + F::release_rva, &stub, &slot);
    }
    if (!reason) {
        engine_patch::SiteSpec spec{};
        spec.name = "fl_dust_fill_failed_body";
        spec.address = base + F::site_rva;
        spec.length = F::site_length;
        spec.ret_pop = F::site_ret_pop;
        spec.rel32_offset = F::site_rel32_offset;
        std::memcpy(spec.expected, F::expected_site, F::site_length);
        site_ = engine_patch::Site{};
        site_at_ = spec.address;
        if (!engine_patch::claim(site_, spec)) {
            reason = site_.status;
            if (!std::strcmp(reason, "patch_rolled_back") || !std::strcmp(reason, "rollback_failed"))
                write_ = site_.atomic_write ? "atomic" : "plain";
            if (site_.patched_in) {
                patched_ = true;
                state_ = "rollback_failed";
                SetLastError(error);
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
    SetLastError(error);
    return patched_ && !std::strcmp(reason, "ok");
}
// AP's signature: `window` is base + the tail window's RVA and `release` must be base + the release's RVA.
bool install_at(std::uintptr_t window, std::uintptr_t release) {
    const std::uintptr_t base = window - F::tail_window_rva;
    if (window < F::tail_window_rva || (base & 0xffff) || release != base + F::release_rva) {
        state_ = patched_ ? "already_installed" : "invalid_site";
        return false;
    }
    return install_base(base);
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) {
        SetLastError(error);
        return stub_ != 0;
    } // live only after ok; a registered rollback_failed stays a failure
    // Unset or empty = the DLL default (on, the stub); 1..31 characters must be exactly on or off; anything else is
    // refused and nothing is patched.
    wchar_t text[sites::setting_capacity]{};
    const DWORD length = x3m::config::get(L"X3M_DUST_LEAK_FIX", text, sites::setting_capacity);
    char setting[sites::setting_capacity]{};
    printable(text, length, setting);
    const std::uintptr_t base = current_base();
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
    else if (!base)
        state_ = "executable_mismatch";
    else
        applied = install_base(base);
    // rollback_failed leaves the site registered with a jump that may still be live: not reported as a refusal.
    const char* status = applied                              ? "patched"
                         : patched_                           ? "patched_unverified"
                         : parsed && mode == sites::Mode::off ? "off"
                                                              : "refused";
    log("dust_leak_fix site=%08lx rva=%06lx edition=fl status=%s reason=%s mode=%s setting=%s write=%s stub=%08lx",
        static_cast<unsigned long>(base ? base + F::site_rva : 0), static_cast<unsigned long>(F::site_rva), status,
        state_, parsed ? sites::mode_name(mode) : "-", setting, write_, static_cast<unsigned long>(stub_));
    SetLastError(error);
    return applied;
}
void report(unsigned frame) {
    if (!stub_) return;
    const std::uint32_t total = x3m_dust_leak_fix_hits;
    log("dust_leak_fix hits=%lu total=%lu frame=%u edition=fl", static_cast<unsigned long>(total - reported_),
        static_cast<unsigned long>(total), frame);
    reported_ = total;
}
std::uint32_t hits() {
    return stub_ ? x3m_dust_leak_fix_hits : 0;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    unsigned char found[F::site_length]{};
    const bool found_read = engine_patch::read_code(site_at_, found, F::site_length);
    const char* reason;
    if (found_read && !std::memcmp(found, F::expected_site, F::site_length))
        reason = "restored"; // already the engine's bytes: no write
    else if (!found_read || !F::claimed_span(found, site_.patched))
        reason = "restore_not_owned"; // someone else's bytes: no write
    else if (!engine_patch::install_window_open())
        reason = "restore_retained"; // the jump crosses a qword: no plain rewrite once the engine runs
    else {
        engine_patch::restore(site_);
        unsigned char back[F::site_length]{};
        const bool back_ok = engine_patch::read_code(site_at_, back, F::site_length) &&
                             !std::memcmp(back, F::expected_site, F::site_length);
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
    // One row, written straight to the log's OS handle: on a dynamic unload this runs inside DllMain, where the capture
    // lock must not be taken. found= is the span's first five bytes.
    const HANDLE handle = log_handle();
    if (handle && handle != INVALID_HANDLE_VALUE) {
        char line[192], bytes[16] = "--";
        if (found_read)
            std::snprintf(bytes, sizeof bytes, "%02x%02x%02x%02x%02x", found[0], found[1], found[2], found[3], found[4]);
        const int n = std::snprintf(line, sizeof line,
                                    "dust_leak_fix_restore site=%08lx rva=%06lx edition=fl status=%s found=%s registered=%u\n",
                                    static_cast<unsigned long>(at), static_cast<unsigned long>(F::site_rva), reason,
                                    bytes, patched_ ? 1u : 0u);
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
#ifdef X3M_DUST_LEAK_FIX_FL_FIXTURE
void fixture_base(std::uintptr_t base) {
    fixture_base_ = base;
}
#endif
}
