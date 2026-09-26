#include "fov.h"
#include "config.h"
#include "fov_sites.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "capture.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

// Three sites (fov_sites.h). The constructor: four immediate bytes in place,
// `MOV dword [ESI+0x24],0x4000` -> `MOV dword [ESI+0x24],F'`, same opcode, same
// length (install()/restore() carry the write, read-back, rollback and restore
// sequence the host test also drives). INS_SetFocus: the 6-byte
// `MOV EDX,[0x00608504]` at 0x0042dbf8 claimed through engine_patch (jmp to
// the dispatcher in one lock cmpxchg8b, tail = the displaced MOV + jmp
// 0x0042dbfe) with the remap stub and its 81-entry table pushed in front; the
// stub and table live in the never-freed arena, so a dynamic unload leaves
// nothing in this module that engine code can reach. The registry
// serializer's load store: the six bytes at 0x0041c8c1 claimed the same way
// (tail = MOV [EBP+0x24],EAX; POP ESI; MOV AL,1; jmp 0x0041c8c7) with the
// exact-match stub and its vanilla table in a second arena block; it reads the
// INS_SetFocus block's table. All three sites or none: a failed later site
// rolls the earlier ones back. Per frame only current_focus() (two validated
// reads, called by the small-parts cull when it is armed) and the flag test in
// present(); the INS_SetFocus stub runs once per menu step on the script VM's
// thread, the load stub once per savegame load.
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace engine_patch = x3m::engine_patch;
namespace engine_memory = x3m::engine_memory;
namespace sites = x3m::fov::sites;
bool patched_ = false;
bool identity_ = false; // the executable was verified at initialize(): engine reads allowed
std::uintptr_t write_at_ = 0;
std::uint32_t site_protection = 0;
std::uint32_t focus_ = sites::engine_focus; // the value in the immediate while patched_
std::uint32_t configured_ = sites::engine_focus;
const char* state_ = "not_initialized";
const char* write_ = "none";
const char* registry_ = "skipped";
// INS_SetFocus: the claimed site, whether it is registered (claimed, or a rollback that failed),
// the install-row value (none = not attempted, active, or the failure) and which write path the jmp took.
engine_patch::Site setfocus_site_{};
bool setfocus_live_ = false;
const char* setfocus_ = "none";
const char* setfocus_write_ = "none";
std::uintptr_t setfocus_table_ = 0; // the INS_SetFocus block's 81 F' entries, which the load stub reads too
// The registry serializer's load store, the same four facts as the INS_SetFocus claim.
engine_patch::Site load_site_{};
bool load_live_ = false;
const char* load_ = "none";
const char* load_write_ = "none";
bool confirm_done_ = false, absent_logged_ = false;
unsigned confirm_polls_ = 0;
constexpr unsigned confirm_poll_limit = 1u << 20; // Presents to wait for a registry that never appears

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
bool code_is(std::uintptr_t at, const unsigned char* expected, unsigned n) {
    unsigned char now[32]{};
    return n <= sizeof now && engine_patch::read_code(at, now, n) && !std::memcmp(now, expected, n);
}
// The remap stub, its 4-aligned continuation slot and the 81 uint16 table
// entries in one arena block; 0 when the arena is full. *table_out: the table.
std::uintptr_t emit_setfocus_stub(void*** slot_out, std::uintptr_t* table_out) {
    std::uint16_t values[sites::remap_count];
    sites::build_remap_table(values); // once, at install
    engine_patch::Emitter e(sites::stub_length + 3 + 4 + sizeof values);
    if (!e.ok()) return 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    const std::uintptr_t slot = (at + sites::stub_length + 3) & ~std::uintptr_t(3);
    unsigned char code[sites::stub_length];
    sites::encode_setfocus_stub(std::uint32_t(slot + 4), std::uint32_t(slot), code);
    e.bytes(code, sites::stub_length);
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < slot) e.byte(0xcc);
    e.dword(0);
    e.bytes(values, sizeof values); // x86 little endian: the stub's movzx reads them as stored
    if (!e.finish()) return 0;
    *slot_out = reinterpret_cast<void**>(slot);
    *table_out = slot + 4;
    return at;
}
// The load stub, its 4-aligned continuation slot and the 81 uint16 vanilla
// entries (N << 16) / 360 in one arena block; `table` is the INS_SetFocus
// block's F' table (immutable, never freed). 0 when the arena is full.
std::uintptr_t emit_load_stub(std::uintptr_t table, void*** slot_out) {
    std::uint16_t vanilla[sites::remap_count];
    sites::build_vanilla_table(vanilla); // once, at install
    engine_patch::Emitter e(sites::load_stub_length + 3 + 4 + sizeof vanilla);
    if (!e.ok()) return 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    const std::uintptr_t slot = (at + sites::load_stub_length + 3) & ~std::uintptr_t(3);
    unsigned char code[sites::load_stub_length];
    sites::encode_load_stub(std::uint32_t(slot + 4), std::uint32_t(table), std::uint32_t(slot), code);
    e.bytes(code, sites::load_stub_length);
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < slot) e.byte(0xcc);
    e.dword(0);
    e.bytes(vanilla, sizeof vanilla);
    if (!e.finish()) return 0;
    *slot_out = reinterpret_cast<void**>(slot);
    return at;
}
// Claims the INS_SetFocus MOV EDX at `site` and pushes the stub in front: "ok" or the reason.
// A failure after the jmp went in puts the original bytes back; when even that fails the site
// stays registered (setfocus_live_) for shutdown().
const char* install_setfocus(std::uintptr_t site) {
    setfocus_write_ = "none";
    if (setfocus_live_) return "already_installed";
    if (!engine_patch::install_window_open()) return "late_claim";
    void** slot = nullptr;
    std::uintptr_t table = 0;
    const std::uintptr_t stub = emit_setfocus_stub(&slot, &table);
    if (!stub || !slot) return "arena_full";
    setfocus_table_ = table;
    engine_patch::SiteSpec spec{};
    spec.name = "fov_setfocus";
    spec.address = site;
    spec.length = sites::setfocus_site_length;
    spec.ret_pop = 0;
    spec.rel32_offset = 0;
    std::memcpy(spec.expected, sites::expected_setfocus_site, sites::setfocus_site_length);
    setfocus_site_ = engine_patch::Site{};
    const bool claimed = engine_patch::claim(setfocus_site_, spec);
    if (claimed || setfocus_site_.patched_in || !std::strcmp(setfocus_site_.status, "patch_rolled_back"))
        setfocus_write_ = setfocus_site_.atomic_write ? "atomic" : "plain";
    setfocus_live_ = setfocus_site_.patched_in;
    if (!claimed) return setfocus_live_ ? "rollback_failed" : setfocus_site_.status;
    const char* failure = nullptr;
    unsigned char now[5]{};
    if (!engine_patch::store_pointer(slot, *setfocus_site_.entry) ||
        !engine_patch::push_front(setfocus_site_, reinterpret_cast<void*>(stub)))
        failure = "chain_failed";
    else if (!engine_patch::read_code(site, now, sizeof now) || std::memcmp(now, setfocus_site_.patched, sizeof now))
        failure = "readback_failed";
    if (!failure) return "ok";
    engine_patch::restore(setfocus_site_); // judged by the registration it leaves, not the protection result
    setfocus_live_ = setfocus_site_.patched_in;
    return setfocus_live_ ? "rollback_failed" : failure;
}
// Claims the load store at `site` and pushes the exact-match stub in front: "ok" or the reason.
// The same failure handling as install_setfocus(); the read-back covers the jmp, the untouched
// sixth byte (MOV AL,1's immediate) and the chain head.
const char* install_load(std::uintptr_t site) {
    load_write_ = "none";
    if (load_live_) return "already_installed";
    if (!engine_patch::install_window_open()) return "late_claim";
    if (!setfocus_table_) return "no_table";
    void** slot = nullptr;
    const std::uintptr_t stub = emit_load_stub(setfocus_table_, &slot);
    if (!stub || !slot) return "arena_full";
    engine_patch::SiteSpec spec{};
    spec.name = "fov_load";
    spec.address = site;
    spec.length = sites::load_site_length;
    spec.ret_pop = 0;
    spec.rel32_offset = 0;
    std::memcpy(spec.expected, sites::expected_load_site, sites::load_site_length);
    load_site_ = engine_patch::Site{};
    const bool claimed = engine_patch::claim(load_site_, spec);
    if (claimed || load_site_.patched_in || !std::strcmp(load_site_.status, "patch_rolled_back"))
        load_write_ = load_site_.atomic_write ? "atomic" : "plain";
    load_live_ = load_site_.patched_in;
    if (!claimed) return load_live_ ? "rollback_failed" : load_site_.status;
    const char* failure = nullptr;
    unsigned char now[sites::load_site_length]{};
    if (!engine_patch::store_pointer(slot, *load_site_.entry) ||
        !engine_patch::push_front(load_site_, reinterpret_cast<void*>(stub)))
        failure = "chain_failed";
    else if (!engine_patch::read_code(site, now, sizeof now) || std::memcmp(now, load_site_.patched, 5) ||
             now[5] != sites::expected_load_site[5] || *load_site_.entry != reinterpret_cast<void*>(stub))
        failure = "readback_failed";
    if (!failure) return "ok";
    engine_patch::restore(load_site_); // judged by the registration it leaves, not the protection result
    load_live_ = load_site_.patched_in;
    return load_live_ ? "rollback_failed" : failure;
}
// Takes the INS_SetFocus claim back after the load site failed (only over our jmp).
void rollback_setfocus() {
    if (!setfocus_live_) return;
    engine_patch::restore(setfocus_site_);
    setfocus_live_ = setfocus_site_.patched_in;
    setfocus_ = setfocus_live_ ? "rollback_failed" : "rolled_back";
}
// Puts the constructor's 00 40 00 00 back after a later site failed; true when restored.
bool rollback_constructor() {
    CodeOps ops;
    unsigned char found[sites::write_length]{};
    bool found_read = false;
    if (std::strcmp(sites::restore(ops, write_at_, focus_, site_protection, found, &found_read), "restored"))
        return false;
    patched_ = false;
    return true;
}
// The registry pointer and its +0x24 through the validated reader; false when
// either is unreadable, the pointer is 0 or misaligned, or the value is not
// plausible. *registry is set whenever the slot itself was read.
bool read_registry(std::uint32_t* registry, std::uint32_t* focus) {
    *registry = 0;
    if (!engine_memory::read(sites::registry_slot_va, registry, sizeof *registry) || !*registry || (*registry & 3u))
        return false;
    return engine_memory::read(std::uintptr_t(*registry) + sites::registry_focus_offset, focus, sizeof *focus) &&
           sites::plausible_focus(*focus);
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
    if (InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(field), static_cast<LONG>(focus), expected) !=
        expected)
        return "skipped";
    return "written";
}
}

namespace x3m::fov {
bool install_at(std::uintptr_t window_address, std::uint32_t focus) {
    if (patched_) {
        state_ = "already_installed";
        return false;
    }
    CodeOps ops;
    bool live = false, atomic = false;
    const char* reason = sites::install(ops, window_address, engine_patch::install_window_open(), focus, &live, &atomic,
                                        &site_protection);
    const bool wrote = live || !std::strcmp(reason, "patch_rolled_back") ||
                       !std::strcmp(reason, "rollback_unprotected");
    write_ = wrote ? (atomic ? "atomic" : "plain") : "none";
    if (live) {
        patched_ = true;
        focus_ = focus;
        write_at_ = window_address + sites::write_offset;
    } // "ok", or rollback_failed kept registered for shutdown()
    state_ = reason;
    return patched_ && !std::strcmp(reason, "ok");
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_ || setfocus_live_ || load_live_) {
        SetLastError(error);
        return true;
    }
    // Unset, empty or `game` = the engine's own model (nothing patched); 1..31 characters of
    // the game's degrees N in [70, 100]; anything else is refused and nothing is patched.
    wchar_t text[sites::setting_capacity]{};
    const DWORD length = x3m::config::get(L"X3M_FOV", text, sites::setting_capacity);
    char setting[sites::setting_capacity]{};
    printable(text, length, setting);
    identity_ = object_trace::executable_verified();
    registry_ = "skipped";
    write_ = "none";
    setfocus_ = "none";
    setfocus_write_ = "none";
    load_ = "none";
    load_write_ = "none";
    double degrees = 0;
    std::uint32_t focus = sites::engine_focus;
    bool applied = false, requested = false;
    const sites::Parse parsed = length >= sites::setting_capacity ? sites::Parse::invalid
                                                                  : sites::parse_setting(text, &degrees);
    if (length >= sites::setting_capacity)
        state_ = "too_long";
    else if (parsed == sites::Parse::invalid)
        state_ = "invalid_setting";
    else if (parsed == sites::Parse::game)
        state_ = "game";
    else if (!sites::in_range(degrees))
        state_ = "out_of_range";
    else if (!identity_) {
        state_ = "executable_mismatch";
        requested = true;
    } else if (!code_is(sites::reader_va, sites::expected_reader, sites::reader_length) ||
               !code_is(sites::setfocus_va, sites::expected_setfocus, sites::setfocus_length)) {
        state_ = "reader_mismatch";
        requested = true;
    } else if (!code_is(sites::setfocus_case_va, sites::expected_setfocus_case, sites::setfocus_case_length) ||
               !code_is(sites::setfocus_callee_va, sites::expected_setfocus_callee, sites::setfocus_callee_length)) {
        state_ = "setfocus_mismatch";
        requested = true;
    } else if (!code_is(sites::load_window_va, sites::expected_load_window, sites::load_window_length) ||
               !code_is(sites::load_caller_va, sites::expected_load_caller, sites::load_caller_length)) {
        state_ = "load_mismatch";
        requested = true;
    } else {
        requested = true;
        applied = install_at(sites::window_va, focus = sites::constructor_focus(degrees)); // F'(N), off any vanilla
                                                                                           // value
        if (applied) {
            // The second site; both or neither: its failure takes the immediate back out.
            const char* second = install_setfocus(sites::setfocus_site_va);
            if (!std::strcmp(second, "ok"))
                setfocus_ = "active";
            else {
                setfocus_ = second;
                applied = false;
                state_ = "setfocus_failed";
                rollback_constructor();
            }
        }
        if (applied) {
            // The third site; all or none: its failure takes the INS_SetFocus jmp and the immediate back out.
            const char* third = install_load(sites::load_site_va);
            if (!std::strcmp(third, "ok"))
                load_ = "active";
            else {
                load_ = third;
                applied = false;
                state_ = "load_failed";
                rollback_setfocus();
                rollback_constructor();
            }
        }
    }
    std::uint32_t before = 0;
    bool before_read = false;
    if (applied) registry_ = write_registry(focus, &before, &before_read);
    if (patched_ && !applied) {
        // rollback_failed: the immediate may hold neither the engine's nor our value. Report what the
        // constructor will actually store (read back); unreadable or implausible bytes leave it unknown
        // (0), which the small-parts cull treats as a vanilla frame.
        unsigned char now[sites::write_length]{};
        const std::uint32_t found = engine_patch::read_code(write_at_, now, sites::write_length)
                                        ? std::uint32_t(now[0]) | std::uint32_t(now[1]) << 8 |
                                              std::uint32_t(now[2]) << 16 | std::uint32_t(now[3]) << 24
                                        : 0u;
        configured_ = sites::plausible_focus(found) ? found : 0u;
    } else {
        configured_ = patched_ ? focus_ : sites::engine_focus;
    }
    // rollback_failed leaves a site registered with bytes that may still be patched (or neither
    // original nor patched): the one failure that modifies the engine is not reported as a refusal.
    const bool off = !requested && parsed == sites::Parse::game;
    const char* status = applied                                      ? "patched"
                         : (patched_ || setfocus_live_ || load_live_) ? "patched_unverified"
                         : off                                        ? "off"
                                                                      : "refused";
    char previous[16] = "-";
    if (before_read) std::snprintf(previous, sizeof previous, "0x%04lx", static_cast<unsigned long>(before));
    log("fov site=%08lx status=%s reason=%s value=0x%04lx vertical_deg=%.2f setting=%s write=%s registry=%s registry_before=%s setfocus=%s setfocus_write=%s load=%s load_write=%s",
        static_cast<unsigned long>(sites::write_va), status, state_, static_cast<unsigned long>(configured_),
        sites::vertical_for_focus(configured_), setting, write_, registry_, previous, setfocus_, setfocus_write_, load_,
        load_write_);
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_ && !setfocus_live_ && !load_live_) return true;
    const DWORD error = GetLastError();
    // INS_SetFocus and the load store first (only over our jmp; the stubs and tails stay callable in
    // the arena for a thread already inside them), then the constructor's immediate (only over our value).
    const char* second = "none";
    if (setfocus_live_) {
        engine_patch::restore(setfocus_site_);
        second = setfocus_site_.status; // restored, restore_not_owned, restore_protect_failed, restore_failed
        setfocus_live_ = setfocus_site_.patched_in; // only restore_not_owned keeps it registered
        if (!setfocus_live_) setfocus_ = "none";
    }
    const char* third = "none";
    if (load_live_) {
        engine_patch::restore(load_site_);
        third = load_site_.status; // the same four outcomes
        load_live_ = load_site_.patched_in;
        if (!load_live_) load_ = "none";
    }
    CodeOps ops;
    unsigned char found[sites::write_length]{};
    bool found_read = false;
    const char* reason = "none";
    if (patched_) {
        reason = sites::restore(ops, write_at_, focus_, site_protection, found, &found_read);
        if (!std::strcmp(reason, "restored")) {
            patched_ = false;
            configured_ = sites::engine_focus;
        } // restore_not_owned and restore_failed keep the site registered
    }
    state_ = std::strcmp(reason, "none") ? reason : second;
    // One row, written straight to the log's OS handle: this runs inside DllMain (dynamic
    // unload), where the capture lock must not be taken. found= is the imm32 as stored.
    const HANDLE handle = log_handle();
    if (handle && handle != INVALID_HANDLE_VALUE) {
        char line[240], bytes[12] = "--";
        if (found_read) std::snprintf(bytes, sizeof bytes, "%02x%02x%02x%02x", found[0], found[1], found[2], found[3]);
        const int n = std::snprintf(line, sizeof line,
                                    "fov_restore site=%08lx status=%s found=%s registered=%u setfocus=%s load=%s\n",
                                    static_cast<unsigned long>(write_at_), reason, bytes,
                                    (patched_ || setfocus_live_ || load_live_) ? 1u : 0u, second, third);
        DWORD written = 0;
        if (n > 0 && unsigned(n) < sizeof line) WriteFile(handle, line, DWORD(n), &written, nullptr);
    }
    SetLastError(error);
    return !patched_ && !setfocus_live_ && !load_live_;
}
std::uint32_t configured_focus() {
    return configured_;
} // 0 = unknown (rollback_failed with unreadable or implausible bytes)
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
            frame, static_cast<unsigned long>(registry), static_cast<unsigned long>(live ? focus : 0u),
            static_cast<unsigned long>(configured_), live && focus == configured_ ? 1u : 0u,
            live ? sites::vertical_for_focus(focus) : 0.0);
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
void loaded(unsigned long long frame) {
    if (!identity_) return;
    const DWORD error = GetLastError();
    std::uint32_t registry = 0, focus = 0;
    const bool live = read_registry(&registry, &focus);
    // expected= stays the configured (new-game) value: a savegame's own base may differ (match=0).
    if (live || registry)
        log("fov_confirm frame=%llu registry=%08lx focus=0x%04lx expected=0x%04lx match=%u vertical_deg=%.2f camera=skipped after=save_load_complete",
            frame, static_cast<unsigned long>(registry), static_cast<unsigned long>(live ? focus : 0u),
            static_cast<unsigned long>(configured_), live && focus == configured_ ? 1u : 0u,
            live ? sites::vertical_for_focus(focus) : 0.0);
    else
        log("fov_confirm frame=%llu registry=absent focus=- expected=0x%04lx match=0 vertical_deg=- camera=skipped after=save_load_complete",
            frame, static_cast<unsigned long>(configured_));
    SetLastError(error);
}
const char* state() {
    return state_;
}
const char* write_path() {
    return write_;
}
const char* registry_state() {
    return registry_;
}
const char* setfocus_state() {
    return setfocus_;
}
const char* load_state() {
    return load_;
}
bool patched() {
    return patched_ || setfocus_live_ || load_live_;
}
bool setfocus_patched() {
    return setfocus_live_;
}
bool load_patched() {
    return load_live_;
}
}
