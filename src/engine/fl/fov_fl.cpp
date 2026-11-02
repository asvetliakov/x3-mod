// FL edition of x3m::fov (src/proxy/fov.h; the ap target keeps src/proxy/fov.cpp): the field-of-view option on
// X3FL.exe (docs/reverse-engineering/fl/field-of-view.md, "Verified 2026-11-02 against X3FL.exe"; constants and the
// FL register variants of the two stubs: fov_sites_fl.h; the remap F -> F', the 81-entry table, the vanilla table, the
// parser and the constructor collision rule are AP's, src/proxy/fov_sites.h).
//
//   base+0x01e08d  imm32 of the registry constructor's MOV dword [ESI+0x24],0x4000 -> F'(N)  (plain write: crosses a
//                  qword; the constructor runs only on a new game or a load, never before the first Present)
//   base+0x030eaa  INS_SetFocus's MOV EAX,[base+0x1a82e4] claimed (5 bytes, one lock cmpxchg8b) with the remap stub
//                  in front (ECX in/out, EAX scratch); the tail replays the MOV and resumes at base+0x030eaf
//   base+0x01df09  the serializer's load store MOV AL,1; MOV [EDI+0x24],EDX claimed (5 bytes, one lock cmpxchg8b)
//                  with the exact-match stub in front (EDX in/out, ECX scratch); the tail resumes at base+0x01df0e
//
// Install (backend-load path, inside the engine_patch install window; a later call is refused, late_claim): X3FL.exe
// identity (fl::module_base() != 0), every window of fov_sites_fl.h at base + RVA with its fixups bound to base, before
// anything is emitted; then both stubs and their tables published in the never-freed arena; then the literal, the
// INS_SetFocus claim and the load claim, each read back. All three or none: a failed later site rolls the earlier ones
// back; a rollback that cannot complete keeps the site registered (patched_unverified). No registry write at install:
// at the FL install point the slot holds no constructed registry (the note's section 4), and the registry pointer is
// never retained. Per frame only present()'s counter test: one sample of the admitted registry's +0x24 on every 64th
// Present (current_focus() returns it; no other engine read), at most 16 fov_confirm rows. The stubs call nothing and reach only arena
// memory, so the module is not pinned. shutdown() is for the fixture: the FL loader's DLL_PROCESS_DETACH unload calls
// are AP only; it restores the two jumps (only over our jmp) and the literal only while the install window is open
// (the crossing literal has no atomic removal; after the window closes it stays, literal_retained).
#include "fov_fl.h"
#include "../../proxy/config.h"
#include "../../proxy/engine_memory.h"
#include "../../proxy/engine_patch.h"
#include "../../proxy/fov_sites.h"
#include "executable_identity_fl.h"
#include "fov_sites_fl.h"
#ifndef X3M_FOV_FL_FIXTURE
#include "sector_background_fl.h"
#include "sector_background_fl_inc.h"
#endif
#include <windows.h>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 code patching only");
#ifndef X3M_FOV_FL_FIXTURE
static_assert(x3m::fl::sector_background_fl::cockpit_registry_slot_rva == x3m::fl::fov_fl::registry_slot_rva,
              "the admitted cockpit registry slot is the one INS_SetFocus reads");
#endif
namespace {
namespace engine_patch = x3m::engine_patch;
namespace engine_memory = x3m::engine_memory;
namespace sites = x3m::fov::sites;
namespace F = x3m::fl::fov_fl;
bool literal_live_ = false; // the constructor's imm32 may hold this module's value (registered)
std::uintptr_t write_at_ = 0;
std::uint32_t protection_ = 0;
std::uint32_t focus_ = sites::engine_focus; // the value written into the imm32 while literal_live_
std::uint32_t configured_ = sites::engine_focus;
bool applied_ = false; // all three sites live
const char* state_ = "not_initialized";
const char* write_ = "none";
engine_patch::Site setfocus_site_{}, load_site_{};
bool setfocus_live_ = false, load_live_ = false;
const char* setfocus_ = "none";
const char* setfocus_write_ = "none";
const char* load_ = "none";
const char* load_write_ = "none";
// present()'s sample, once every fl_sample_interval Presents: the last (registry, focus) pair, compared only (never
// dereferenced), whether it was live, and the fov_confirm row count. current_focus() returns the sampled focus.
std::uint32_t confirm_registry_ = 0, confirm_focus_ = 0;
bool sample_live_ = false;
unsigned confirm_rows_ = 0, present_calls_ = 0;
constexpr unsigned confirm_row_cap = 16;
// The 81 F' entries for focus_kind(), built once (initialize, install_base or the first row).
std::uint16_t kind_table_[sites::remap_count];
bool kind_table_built_ = false;
void build_kind_table() {
    if (kind_table_built_) return;
    sites::build_remap_table(kind_table_);
    kind_table_built_ = true;
}
#ifdef X3M_FOV_FL_FIXTURE
std::uintptr_t fixture_base_ = 0, fixture_slot_ = 0;
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
    // The imm32 crosses a qword: engine_patch::write_code takes the plain copy (*atomic false), then the flush.
    bool write(std::uintptr_t at, const unsigned char* bytes, unsigned n, bool* atomic) {
        *atomic = engine_patch::write_code(at, bytes, n);
        return FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), n) != FALSE;
    }
};
void printable(const wchar_t* text, DWORD length, char* out) {
    if (!length || length >= sites::setting_capacity) {
        out[0] = length ? '?' : '-';
        out[1] = 0;
        return;
    }
    for (DWORD i = 0; i < length; ++i) out[i] = (text[i] >= 0x21 && text[i] <= 0x7e) ? static_cast<char>(text[i]) : '?';
    out[length] = 0;
}
bool reached_write(const engine_patch::Site& s, bool claimed) {
    return claimed || s.patched_in || !std::strcmp(s.status, "patch_rolled_back");
}
// The INS_SetFocus stub, its 4-aligned continuation slot and the 81 uint16 F' entries in one arena block.
std::uintptr_t emit_setfocus_stub(void*** slot_out, std::uintptr_t* table_out) {
    std::uint16_t values[sites::remap_count];
    sites::build_remap_table(values);
    engine_patch::Emitter e(F::stub_length + 3 + 4 + sizeof values);
    if (!e.ok()) return 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    const std::uintptr_t slot = (at + F::stub_length + 3) & ~std::uintptr_t(3);
    unsigned char code[F::stub_length];
    F::encode_setfocus_stub(std::uint32_t(slot + 4), std::uint32_t(slot), code);
    e.bytes(code, F::stub_length);
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < slot) e.byte(0xcc);
    e.dword(0);
    e.bytes(values, sizeof values);
    if (!e.finish()) return 0;
    *slot_out = reinterpret_cast<void**>(slot);
    *table_out = slot + 4;
    return at;
}
// The load stub, its slot and the 81 uint16 vanilla entries; `table` is the INS_SetFocus block's F' table.
std::uintptr_t emit_load_stub(std::uintptr_t table, void*** slot_out) {
    std::uint16_t vanilla[sites::remap_count];
    sites::build_vanilla_table(vanilla);
    engine_patch::Emitter e(F::load_stub_length + 3 + 4 + sizeof vanilla);
    if (!e.ok()) return 0;
    const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e.here());
    const std::uintptr_t slot = (at + F::load_stub_length + 3) & ~std::uintptr_t(3);
    unsigned char code[F::load_stub_length];
    F::encode_load_stub(std::uint32_t(slot + 4), std::uint32_t(table), std::uint32_t(slot), code);
    e.bytes(code, F::load_stub_length);
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < slot) e.byte(0xcc);
    e.dword(0);
    e.bytes(vanilla, sizeof vanilla);
    if (!e.finish()) return 0;
    *slot_out = reinterpret_cast<void**>(slot);
    return at;
}
// Claims one five-byte site at `at` (expected = the live bytes) and chains `stub` in front: "ok" or the reason. A
// failure after the jmp went in puts the original bytes back; when even that fails the site stays registered.
const char* claim_site(engine_patch::Site& site, bool* live, const char** write, const char* name, std::uintptr_t at,
                       const unsigned char* expected, void** slot, std::uintptr_t stub) {
    *write = "none";
    engine_patch::SiteSpec spec{};
    spec.name = name;
    spec.address = at;
    spec.length = 5;
    spec.ret_pop = 0;
    spec.rel32_offset = 0;
    std::memcpy(spec.expected, expected, 5);
    site = engine_patch::Site{};
    const bool claimed = engine_patch::claim(site, spec);
    if (reached_write(site, claimed)) *write = site.atomic_write ? "atomic" : "plain";
    *live = site.patched_in;
    if (!claimed) return *live ? "rollback_failed" : site.status;
    const char* failure = nullptr;
    unsigned char now[5]{};
    if (!engine_patch::store_pointer(slot, *site.entry) ||
        !engine_patch::push_front(site, reinterpret_cast<void*>(stub)))
        failure = "chain_failed";
    else if (!engine_patch::read_code(at, now, sizeof now) || std::memcmp(now, site.patched, sizeof now) ||
             *site.entry != reinterpret_cast<void*>(stub))
        failure = "readback_failed";
    if (!failure) return "ok";
    engine_patch::restore(site); // judged by the registration it leaves, not the protection result
    *live = site.patched_in;
    return *live ? "rollback_failed" : failure;
}
void rollback_setfocus() {
    if (!setfocus_live_) return;
    engine_patch::restore(setfocus_site_);
    setfocus_live_ = setfocus_site_.patched_in;
    setfocus_ = setfocus_live_ ? "rollback_failed" : "rolled_back";
}
// Puts the constructor's 00 40 00 00 back (only over our value) after a later site failed; the install window is open.
void rollback_literal() {
    if (!literal_live_) return;
    CodeOps ops;
    unsigned char found[sites::write_length]{};
    bool found_read = false;
    if (!std::strcmp(sites::restore(ops, write_at_, focus_, protection_, found, &found_read), "restored"))
        literal_live_ = false;
}
// What the constructor will store: our value when all three are live; after a rollback that could not complete the
// imm32 read back (0 = unknown when unreadable or implausible); else the engine's 0x4000.
void settle_configured() {
    if (applied_) {
        configured_ = focus_;
    } else if (literal_live_) {
        unsigned char now[sites::write_length]{};
        const std::uint32_t found = engine_patch::read_code(write_at_, now, sites::write_length) ? F::u32_at(now) : 0u;
        configured_ = sites::plausible_focus(found) ? found : 0u;
    } else {
        configured_ = sites::engine_focus;
    }
}
std::uintptr_t admitted_slot() {
#ifdef X3M_FOV_FL_FIXTURE
    return fixture_slot_;
#else
    // The cockpit registry slot only under sector_background_fl's admission (identity plus its code anchors; the same
    // admission resolve_own_ship uses; an identity/anchor check, not a lifetime lease); re-read at every sample,
    // never retained. Safe only because present() runs on the Present thread between frames.
    if (!x3m::fl::sector_background_fl::admitted()) return 0;
    return x3m::fl::module_base() + x3m::fl::sector_background_fl::cockpit_registry_slot_rva;
#endif
}
// registry+0x24 through the slot: false when the slot or the field is unreadable, the pointer is 0 or misaligned, or
// the value is not plausible.
bool read_focus(std::uintptr_t slot, std::uint32_t* registry, std::uint32_t* focus) {
    *registry = 0;
    if (!engine_memory::read(slot, registry, sizeof *registry) || !*registry || (*registry & 3u)) return false;
    return engine_memory::read(std::uintptr_t(*registry) + F::registry_focus_offset, focus, sizeof *focus) &&
           sites::plausible_focus(*focus);
}
// What a live focus is: the constructor's value, a remapped table value (a menu step through the stub, or a load the
// stub remapped), a vanilla-unit value (the hook did not run on it) or other.
const char* focus_kind(std::uint32_t focus) {
    if (focus == configured_) return "constructor";
    build_kind_table();
    for (unsigned i = 0; i < sites::remap_count; ++i)
        if (kind_table_[i] == focus) return "remapped";
    return sites::vanilla_focus(focus) ? "vanilla" : "other";
}
}

namespace x3m::fov {
bool install_base(std::uintptr_t base, std::uint32_t focus) {
    const DWORD error = GetLastError();
    const auto done = [&](const char* reason) {
        state_ = reason;
        settle_configured();
        SetLastError(error);
        return applied_;
    };
    build_kind_table();
    if (literal_live_ || setfocus_live_ || load_live_) {
        state_ = "already_installed";
        SetLastError(error);
        return false;
    }
    applied_ = false;
    write_ = setfocus_write_ = load_write_ = "none";
    setfocus_ = load_ = "none";
    if (!base || (base & 0xffff)) return done("invalid_site");
    if (!sites::plausible_focus(focus) || focus == sites::engine_focus) return done("invalid_value");
    if (!engine_patch::install_window_open()) return done("late_claim");
    // Every window before anything is emitted or written.
    for (const F::Window& w : F::windows) {
        unsigned char now[F::max_window]{};
        if (!engine_patch::read_code(base + w.rva, now, w.length)) return done("unreadable");
        if (const char* refused = F::plan(w, now, base)) return done(refused);
    }
    // Both stubs and their immutable tables published before any engine byte changes.
    void** setfocus_slot = nullptr;
    void** load_slot = nullptr;
    std::uintptr_t table = 0;
    const std::uintptr_t setfocus_stub = emit_setfocus_stub(&setfocus_slot, &table);
    if (!setfocus_stub || !setfocus_slot) return done("arena_full");
    const std::uintptr_t load_stub = emit_load_stub(table, &load_slot);
    if (!load_stub || !load_slot) return done("arena_full");
    // 1. The constructor literal (plain write, read back).
    CodeOps ops;
    bool live = false, atomic = false;
    const char* first = F::install_literal(ops, base, engine_patch::install_window_open(), focus, &live, &atomic,
                                           &protection_);
    const bool wrote = live || !std::strcmp(first, "patch_rolled_back") || !std::strcmp(first, "rollback_unprotected");
    write_ = wrote ? (atomic ? "atomic" : "plain") : "none";
    if (live) {
        literal_live_ = true;
        focus_ = focus;
        write_at_ = base + F::write_rva;
    }
    if (std::strcmp(first, "ok")) return done(first); // rollback_failed stays registered
    // 2. INS_SetFocus; its failure takes the literal back out.
    unsigned char expected[F::setfocus_site_length];
    F::setfocus_site(base, expected);
    const char* second = claim_site(setfocus_site_, &setfocus_live_, &setfocus_write_, "fov_setfocus_fl",
                                    base + F::setfocus_site_rva, expected, setfocus_slot, setfocus_stub);
    if (std::strcmp(second, "ok")) {
        setfocus_ = second;
        rollback_literal();
        return done("setfocus_failed");
    }
    setfocus_ = "active";
    // 3. The load store; its failure takes the INS_SetFocus jmp and the literal back out.
    const char* third = claim_site(load_site_, &load_live_, &load_write_, "fov_load_fl", base + F::load_site_rva,
                                   F::expected_load_site, load_slot, load_stub);
    if (std::strcmp(third, "ok")) {
        load_ = third;
        rollback_setfocus();
        rollback_literal();
        return done("load_failed");
    }
    load_ = "active";
    applied_ = true;
    return done("ok");
}
bool install_at(std::uintptr_t window, std::uint32_t focus) {
    if (window < F::window_rva) {
        state_ = "invalid_site";
        return false;
    }
    return install_base(window - F::window_rva, focus);
}
bool initialize() {
    const DWORD error = GetLastError();
    if (literal_live_ || setfocus_live_ || load_live_) {
        SetLastError(error);
        return applied_;
    }
    build_kind_table();
    wchar_t text[sites::setting_capacity]{};
    const DWORD length = x3m::config::get(L"X3M_FOV", text, sites::setting_capacity);
    char setting[sites::setting_capacity]{};
    printable(text, length, setting);
#ifdef X3M_FOV_FL_FIXTURE
    const std::uintptr_t base = fixture_base_ ? fixture_base_ : x3m::fl::module_base();
#else
    const std::uintptr_t base = x3m::fl::module_base();
#endif
    write_ = setfocus_ = setfocus_write_ = load_ = load_write_ = "none";
    double degrees = 0;
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
    else if (!base) {
        state_ = "executable_mismatch";
        requested = true;
    } else {
        requested = true;
        applied = install_base(base, sites::constructor_focus(degrees)); // F'(N), off any vanilla value
    }
    settle_configured();
    const bool off = !requested && parsed == sites::Parse::game;
    const char* status = applied                                          ? "patched"
                         : (literal_live_ || setfocus_live_ || load_live_) ? "patched_unverified"
                         : off                                            ? "off"
                                                                          : "refused";
    log("fov site=%08lx rva=%06lx edition=fl status=%s reason=%s value=0x%04lx vertical_deg=%.2f setting=%s write=%s registry=none setfocus=%s setfocus_write=%s load=%s load_write=%s",
        static_cast<unsigned long>(base ? base + F::write_rva : 0), static_cast<unsigned long>(F::write_rva), status,
        state_, static_cast<unsigned long>(configured_), sites::vertical_for_focus(configured_), setting, write_,
        setfocus_, setfocus_write_, load_, load_write_);
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!literal_live_ && !setfocus_live_ && !load_live_) return true;
    const DWORD error = GetLastError();
    applied_ = false;
    const char* second = "none";
    if (setfocus_live_) {
        engine_patch::restore(setfocus_site_); // only over our jmp (restore_not_owned otherwise)
        second = setfocus_site_.status;
        setfocus_live_ = setfocus_site_.patched_in;
        if (!setfocus_live_) setfocus_ = "none";
    }
    const char* third = "none";
    if (load_live_) {
        engine_patch::restore(load_site_);
        third = load_site_.status;
        load_live_ = load_site_.patched_in;
        if (!load_live_) load_ = "none";
    }
    const char* reason = "none";
    unsigned char found[sites::write_length]{};
    bool found_read = false;
    if (literal_live_) {
        if (!engine_patch::install_window_open()) {
            reason = "literal_retained"; // no quiescence for a plain four-byte rewrite once the engine runs
        } else {
            CodeOps ops;
            reason = sites::restore(ops, write_at_, focus_, protection_, found, &found_read);
            if (!std::strcmp(reason, "restored")) literal_live_ = false;
        }
    }
    settle_configured();
    state_ = std::strcmp(reason, "none") ? reason : second;
    char bytes[12] = "--";
    if (found_read) {
        static const char digits[] = "0123456789abcdef";
        for (unsigned i = 0; i < 4; ++i) {
            bytes[2 * i] = digits[found[i] >> 4];
            bytes[2 * i + 1] = digits[found[i] & 15];
        }
        bytes[8] = 0;
    }
    log("fov_restore site=%08lx rva=%06lx edition=fl status=%s found=%s registered=%u setfocus=%s load=%s",
        static_cast<unsigned long>(write_at_), static_cast<unsigned long>(F::write_rva), reason, bytes,
        (literal_live_ || setfocus_live_ || load_live_) ? 1u : 0u, second, third);
    SetLastError(error);
    return !literal_live_ && !setfocus_live_ && !load_live_;
}
std::uint32_t configured_focus() {
    return configured_;
}
// No engine read: the focus present() sampled last (live), else the configured value.
std::uint32_t current_focus() {
    return sample_live_ ? confirm_focus_ : configured_;
}
// One sample on the first call and then every fl_sample_interval-th call (Present thread, between frames): the admitted
// slot and registry+0x24, two validated reads; a row per changed (registry, focus) pair while under the cap.
void present(unsigned long long frame) {
    if (present_calls_++ % fl_sample_interval) return;
    const std::uintptr_t slot = admitted_slot();
    if (!slot) {
        sample_live_ = false; // admission refused (or a fixture without a slot): the configured value
        return;
    }
    const DWORD error = GetLastError();
    std::uint32_t registry = 0, focus = 0;
    sample_live_ = read_focus(slot, &registry, &focus);
    if (sample_live_ && (registry != confirm_registry_ || focus != confirm_focus_)) {
        confirm_registry_ = registry;
        confirm_focus_ = focus;
        if (confirm_rows_ < confirm_row_cap) {
            ++confirm_rows_;
            log("fov_confirm frame=%llu edition=fl registry=%08lx focus=0x%04lx expected=0x%04lx match=%u kind=%s vertical_deg=%.2f row=%u/%u",
                frame, static_cast<unsigned long>(registry), static_cast<unsigned long>(focus),
                static_cast<unsigned long>(configured_), focus == configured_ ? 1u : 0u, focus_kind(focus),
                sites::vertical_for_focus(focus), confirm_rows_, confirm_row_cap);
        }
    }
    SetLastError(error);
}
void loaded(unsigned long long) {} // FL has no save_load_complete marker: present()'s change rows cover a load
const char* state() {
    return state_;
}
const char* write_path() {
    return write_;
}
const char* registry_state() {
    return "none"; // FL: never written at install
}
const char* setfocus_state() {
    return setfocus_;
}
const char* load_state() {
    return load_;
}
bool patched() {
    return literal_live_ || setfocus_live_ || load_live_;
}
bool setfocus_patched() {
    return setfocus_live_;
}
bool load_patched() {
    return load_live_;
}
#ifdef X3M_FOV_FL_FIXTURE
void fixture_base(std::uintptr_t base) {
    fixture_base_ = base;
}
void fixture_registry_slot(std::uintptr_t slot) {
    fixture_slot_ = slot;
    confirm_rows_ = present_calls_ = 0;
    confirm_registry_ = confirm_focus_ = 0;
    sample_live_ = false;
}
#endif
}
