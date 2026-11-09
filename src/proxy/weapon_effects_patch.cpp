#include "weapon_effects_patch.h"
#include "capture.h"
#include "config.h"
#include "engine_memory.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "sse_scalar.h"
#include "weapon_effects_bolt_sites.h"
#include "weapon_effects_death_sites.h"
#include "weapon_effects_sites.h"
#include "weapon_effects_table.h"
#if X3M_EDITION_FL
#include "weapon_projectile_family.h"
#include "../engine/fl/executable_identity_fl.h"
#include "../engine/fl/weapon_effects_sites_fl.h"
#endif
#include <windows.h>
#include <cstring>
#include <new>

// Install, rows and the Present-time upkeep of the weapon effects (weapon_effects_patch.h). Runs on the backend-load
// path (initialize) and in the Present hook (present): never inside the engine's call.
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
namespace engine_patch = x3m::engine_patch;
namespace sites = x3m::weapon_effects::sites;
namespace core = x3m::weapon_effects::core;
namespace bolt_sites = x3m::weapon_effects::bolt_sites;
namespace death_sites = x3m::weapon_effects::death_sites;
#if X3M_EDITION_FL
constexpr unsigned site_count = 8; // impact, tap1, tap2, bolt_tap, muzzle, flight, death (the one FL call), batch
#else
constexpr unsigned site_count = 8; // impact, tap1, tap2; step 4: bolt_tap, muzzle, flight; step 3: death_area, death_other
#endif
bool patched_ = false, installed_ = false;
core::Mode mode_ = core::mode_native;
const char* state_ = "not_initialized";
const char* site_state_[8] = {"not_initialized", "not_initialized", "not_initialized", "not_initialized",
                              "not_initialized", "not_initialized", "not_initialized", "not_initialized"};
bool written_[site_count] = {};
engine_patch::CallSite site_[site_count]{};
core::Table* table_ = nullptr;
unsigned logged_events_ = 0, period_events_ = 0;
// weapon_event rows outside capture frames: the run's first 64, then a rolling sample of up to 8 per weapon_effects_frame
// period (Run235 Flight B: the fixed 64 ended within the first minute, so the own ship's later hits were never seen).
constexpr unsigned event_row_budget = 64, period_row_budget = 8;
constexpr unsigned long long frame_row_period = 300;
struct Period {
    std::uint32_t events = 0, hits = 0, skipped = 0, forwarded = 0, staged = 0, expired = 0, owned = 0, frames = 0, drew = 0;
    unsigned patches = 0, decals = 0, retired = 0, dropped = 0, cells_dropped = 0, hits_dropped = 0, spills = 0;
    unsigned faces_culled = 0, area_dropped = 0, bubbles = 0, bubble_fallback = 0;
    unsigned bubble_walks = 0, bubble_failures = 0, bubble_held = 0, bubble_deferred = 0, bubble_clamped = 0;
    std::uint32_t hull_staged = 0;
    float stage_us_max = 0.f, stage_us_sum = 0.f;
    // step 3: death records seen (ours, census)
    std::uint32_t deaths = 0, deaths_ours = 0;
    // the projectile stage (FL): frames it owned, bolts drawn, traversal draws dropped, entries retired, traversal draws
    // of an enabled body the hook could not see (no scope), the stage's worst CPU time
    std::uint32_t projectile_owned = 0, projectile_bolts = 0, projectile_dropped = 0, projectile_retired = 0;
    std::uint32_t projectile_unhandled = 0, projectile_unpooled = 0, projectile_sampled = 0;
    std::uint32_t projectile_static = 0, projectile_jumped = 0; // entry-frames: position unchanged / off the prediction
    std::uint32_t projectile_ring_trimmed = 0;                  // bolt-frames whose comet ring was trimmed
    std::uint32_t projectile_flashes = 0, projectile_pulses = 0; // muzzle flashes and hull pulses drawn
    std::uint32_t projectile_flash_lost = 0, projectile_flash_dropped = 0, projectile_pulse_clamped = 0; // shooter unreadable; past the frame's room
    float projectile_us_max = 0.f;
    // the bullet re-shade (reshade mode): draws re-shaded, substitutions failed, bullet-program draws outside a submit,
    // the substitution's CPU time (sum and the frame's worst)
    std::uint32_t reshade_draws = 0, reshade_failures = 0, reshade_outside = 0, reshade_timed = 0;
    std::uint32_t reshade_additive = 0, reshade_flagged = 0; // draws the additive route converted, with its far flag
    float reshade_us_sum = 0.f, reshade_us_max = 0.f;
} period_{};
#if X3M_EDITION_FL
std::uint32_t reshade_scopes_seen_ = 0; // x3m_bullet_scope_calls at the last bullet_reshade_frame row
#endif
// weapon_death rows outside capture frames: the run's first 64, then up to 16 per weapon_effects_frame period.
constexpr unsigned death_row_budget = 64, death_period_budget = 16;
unsigned logged_deaths_ = 0, period_deaths_ = 0;
core::Counters seen_{};
core::ScopeLatch scope_{};
std::uint32_t scope_clears_ = 0, scope_changes_ = 0;
std::uint32_t seen_overflow_ = 0, seen_evicted_visible_ = 0, seen_retired_ = 0, seen_compared_ = 0;
// Step 2: no loaded table, or one without any "hull" key (generated before step 2): the repair and tug variants are
// unavailable until x3m-regenerate / `manage.py install` writes it again (logged once with the table row, carried in
// every weapon_effects_frame row).
bool table_hull_key_missing_ = true;

#if !X3M_EDITION_FL
bool window_matches(std::uintptr_t window, const unsigned char* expected, unsigned length) {
    unsigned char current[64]{};
    return length <= sizeof current && engine_patch::read_code(window, current, length) &&
           !std::memcmp(current, expected, length);
}
bool witnesses_match(std::uintptr_t prologue) {
    // Each witness sits at a fixed offset from the prologue, so the compare follows the prologue the caller passed
    // (the production VA, or a fixture's relocated copy): the same bytes either way.
    for (unsigned i = 0; i < sites::witness_count; ++i)
        if (!window_matches(prologue + (sites::witnesses[i].va - sites::prologue_va), sites::witnesses[i].bytes,
                            sites::witnesses[i].length))
            return false;
    return true;
}
#endif
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE &&
           module != nullptr;
}
bool reached_write(const engine_patch::CallSite& s, bool claimed) {
    return claimed || s.patched_in || !std::strcmp(s.status, "patch_rolled_back");
}
// Read-back after claim_call (engine_effects_patch::confirm's rule): nullptr when the site holds the redirect, else
// the original five bytes go back, judged by a second read-back.
const char* confirm(engine_patch::CallSite& s) {
    unsigned char now[sites::call_length]{};
    if (engine_patch::read_code(s.address, now, sites::call_length) && !std::memcmp(now, s.patched, sites::call_length))
        return nullptr;
    auto* code = reinterpret_cast<void*>(s.address);
    DWORD writable = 0, unused = 0;
    if (VirtualProtect(code, sites::call_length, PAGE_EXECUTE_READWRITE, &writable)) {
        engine_patch::write_code(s.address, s.original, sites::call_length);
        FlushInstructionCache(GetCurrentProcess(), code, sites::call_length);
        VirtualProtect(code, sites::call_length, s.protection, &unused);
    }
    unsigned char back[sites::call_length]{};
    if (engine_patch::read_code(s.address, back, sites::call_length) &&
        !std::memcmp(back, s.original, sites::call_length)) {
        s.patched_in = false;
        s.status = "readback_rolled_back";
        return s.status;
    }
    s.status = "rollback_failed";
    return s.status;
}
void printable(const wchar_t* text, DWORD length, char* out) {
    if (!length || length >= core::setting_capacity) {
        out[0] = length ? '?' : '-';
        out[1] = 0;
        return;
    }
    for (DWORD i = 0; i < length; ++i) out[i] = (text[i] >= 0x21 && text[i] <= 0x7e) ? static_cast<char>(text[i]) : '?';
    out[length] = 0;
}
// <EXE directory>\x3m\weapon_effects.json, or X3M_WEAPON_EFFECTS_TABLE verbatim (0 / none: no table).
constexpr DWORD table_size_limit = 4u << 20;
const char* read_table_file(char** text, DWORD* size, char* path_utf8, unsigned cap) {
    *text = nullptr;
    *size = 0;
    path_utf8[0] = 0;
    wchar_t value[8]{};
    const DWORD env = x3m::config::get(L"X3M_WEAPON_EFFECTS_TABLE", value, 8);
    if (env && env < 8 && (!lstrcmpW(value, L"0") || !lstrcmpiW(value, L"none"))) return "disabled";
    static wchar_t path[MAX_PATH + 64];
    const DWORD length =
        env ? x3m::config::get(L"X3M_WEAPON_EFFECTS_TABLE", path, MAX_PATH) : GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return "path";
    if (!env) {
        DWORD cut = length;
        while (cut && path[cut - 1] != L'\\' && path[cut - 1] != L'/') --cut;
        static const wchar_t leaf[] = L"x3m\\weapon_effects.json";
        if (!cut || cut + sizeof leaf / sizeof leaf[0] >= MAX_PATH + 64) return "path";
        std::memcpy(path + cut, leaf, sizeof leaf);
    }
    if (!WideCharToMultiByte(CP_UTF8, 0, path, -1, path_utf8, int(cap), nullptr, nullptr)) std::memcpy(path_utf8, "(long)", 7);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return "absent";
    const char* reason = nullptr;
    LARGE_INTEGER bytes{};
    if (!GetFileSizeEx(file, &bytes) || bytes.QuadPart <= 0)
        reason = "empty";
    else if (bytes.QuadPart > table_size_limit)
        reason = "too_large";
    else if (!(*text = new (std::nothrow) char[std::size_t(bytes.QuadPart)]))
        reason = "allocation";
    else {
        DWORD read = 0;
        if (ReadFile(file, *text, DWORD(bytes.QuadPart), &read, nullptr) && read == DWORD(bytes.QuadPart))
            *size = read;
        else {
            delete[] *text;
            *text = nullptr;
            reason = "read_failed";
        }
    }
    CloseHandle(file);
    return reason ? reason : "read";
}
void load_table() {
    char* text = nullptr;
    DWORD size = 0;
    char path[260];
    const char* read = read_table_file(&text, &size, path, sizeof path);
    x3m::weapon_effects::table::Stats st{};
    std::size_t fault = 0;
    bool parsed = false;
    const bool had_text = text != nullptr;
    if (text) {
        table_ = new (std::nothrow) core::Table;
        if (table_) parsed = x3m::weapon_effects::table::parse(text, size, table_, &st, &fault);
        delete[] text;
        if (!parsed) {
            delete table_;
            table_ = nullptr;
        }
    }
    table_hull_key_missing_ = !parsed || !st.hull_keys;
    // The live count is checked per hit (core::impact: a differing *0x00607038 disables the lookups, stale_table).
    x3m::log("weapon_effects_table event=%s reason=%s rows=%lu missile_rows=%lu entries=%u textures=%u overrides=%u "
             "dropped=%u hull_keys=%u bolt_pins=%u bolt_rejected=%u table_hull_key_missing=%u bytes=%lu fault=%lu path=\"%s\"",
             parsed ? "loaded" : had_text ? "rejected" : read, parsed ? "ok" : had_text ? "malformed" : read,
             static_cast<unsigned long>(parsed ? table_->rows : 0), static_cast<unsigned long>(parsed ? table_->missile_rows : 0),
             st.entries, st.textures, st.overrides, st.dropped, st.hull_keys, st.bolt_pins, st.bolt_rejected, unsigned(table_hull_key_missing_),
             static_cast<unsigned long>(size), static_cast<unsigned long>(fault), path);
}
#if !X3M_EDITION_FL
const char* site_name(unsigned i) {
    static const char* const names[site_count] = {"impact", "tap1", "tap2", "bolt_tap", "muzzle", "flight", "death_area",
                                                  "death_other"};
    return i < site_count ? names[i] : "?";
}
std::uintptr_t site_va(unsigned i) {
    return i == 0   ? sites::impact_site_va
           : i < 3  ? sites::tap_site_va[i - 1]
           : i == 3 ? bolt_sites::tap_site_va
           : i == 4 ? bolt_sites::muzzle_site_va
           : i == 5 ? bolt_sites::flight_site_va
           : i == 6 ? death_sites::area_site_va
                    : death_sites::other_site_va;
}
// Step 4: a bolt site's window, offset, callee and stub (3 bolt_tap, 4 muzzle, 5 flight).
struct BoltSite {
    std::uintptr_t window, target;
    unsigned offset, length;
    const unsigned char* expected;
    void* stub;
};
// Step 3: a death site's window, offset, callee and stub (6 death_area, 7 death_other).
BoltSite death_site(const x3m::weapon_effects::Addresses& a, unsigned i) {
    if (i == 6)
        return {a.death_area_window, a.death_target, death_sites::site_offset, death_sites::window_length,
                death_sites::expected_area_window, reinterpret_cast<void*>(&x3m_weapon_effects_stub_death_area)};
    return {a.death_other_window, a.death_target, death_sites::site_offset, death_sites::window_length,
            death_sites::expected_other_window, reinterpret_cast<void*>(&x3m_weapon_effects_stub_death_other)};
}
BoltSite bolt_site(const x3m::weapon_effects::Addresses& a, unsigned i) {
    if (i >= 6) return death_site(a, i);
    if (i == 3)
        return {a.bolt_tap_window, a.bolt_tap_target, bolt_sites::tap_site_offset, bolt_sites::tap_window_length,
                bolt_sites::expected_tap_window, reinterpret_cast<void*>(&x3m_weapon_effects_stub_bolt_tap)};
    if (i == 4)
        return {a.muzzle_window, a.muzzle_target, bolt_sites::muzzle_site_offset, bolt_sites::muzzle_window_length,
                bolt_sites::expected_muzzle_window, reinterpret_cast<void*>(&x3m_weapon_effects_stub_muzzle)};
    return {a.flight_window, a.flight_target, bolt_sites::flight_site_offset, bolt_sites::flight_window_length,
            bolt_sites::expected_flight_window, reinterpret_cast<void*>(&x3m_weapon_effects_stub_flight)};
}
#else
namespace S = x3m::fl::weapon_effects_fl;
#ifdef X3M_WEAPON_EFFECTS_SHIM
std::uintptr_t fixture_base_ = 0; // initialize()'s base instead of fl::module_base() when nonzero
#endif
// One window or witness at base + RVA through the read seam: the relocation-aware compare.
bool fl_window_matches(std::uintptr_t base, const S::Window& w) {
    unsigned char current[S::max_window]{};
    return w.length <= S::max_window && engine_patch::read_code(base + w.rva, current, w.length) &&
           S::matches(w, current, base);
}
template <unsigned N> bool fl_witnesses_match(std::uintptr_t base, const S::Window (&w)[N]) {
    for (const S::Window& x : w)
        if (!fl_window_matches(base, x)) return false;
    return true;
}
void* fl_stub(unsigned i, bool reshade = false) {
    switch (i) {
    case 0: return reinterpret_cast<void*>(&x3m_weapon_effects_stub_impact_fl);
    case 1:
    case 2: return reinterpret_cast<void*>(&x3m_weapon_effects_stub_tap_fl);
    case 3: return reinterpret_cast<void*>(&x3m_weapon_effects_stub_bolt_tap_fl);
    case 4: return reinterpret_cast<void*>(&x3m_weapon_effects_stub_muzzle_fl);
    case 5: return reinterpret_cast<void*>(&x3m_weapon_effects_stub_flight_fl);
    case 6: return reinterpret_cast<void*>(&x3m_weapon_effects_stub_death_fl);
    default:
        return reshade ? reinterpret_cast<void*>(&x3m_weapon_effects_stub_batch_scope_fl)
                       : reinterpret_cast<void*>(&x3m_weapon_effects_stub_batch_fl);
    }
}
std::uintptr_t bound_base_ = 0; // the base the sites were claimed at (the rows' va)
// The projectile stage's enabled rows of the loaded table (weapon_projectile_family.h resolve_rows), built at install.
x3m::weapon_effects::projectile_family::Resolution projectile_rows_{};
std::uint32_t projectile_seen_skipped_ = 0;
x3m::weapon_effects::core::ProjectileCounters projectile_seen_{};
#endif
// The bolt hooks' period (present_bolts): frames since the last weapon_bolts_frame row and the counters it last saw.
std::uint32_t bolt_frames_ = 0;
x3m::weapon_effects::core::BoltCounters bolt_seen_{};
} // namespace

namespace x3m::weapon_effects {
#if !X3M_EDITION_FL
bool install_at(const Addresses& a, core::Mode mode) {
    const DWORD error = GetLastError();
    const auto done = [&](const char* reason, bool ok) {
        state_ = reason;
        SetLastError(error);
        return ok;
    };
    if (patched_) {
        state_ = "already_installed";
        SetLastError(error);
        return false;
    }
    for (unsigned i = 0; i < site_count; ++i) {
        written_[i] = false;
        site_state_[i] = "not_attempted";
    }
    const bool taps = mode == core::mode_observe;
    const bool bolts = core::bolts_mode(mode);
    const bool booms = core::explosions_mode(mode);
    if (!taps) site_state_[1] = site_state_[2] = "not_used";
    if (!bolts) site_state_[3] = site_state_[4] = site_state_[5] = "not_used";
    if (!booms) site_state_[6] = site_state_[7] = "not_used";
    const auto used = [&](unsigned i) { return i == 0 || (i < 3 ? taps : i < 6 ? bolts : booms); };
    if (mode == core::mode_native) return done("native", false);
    if (!a.impact_window || !a.impact_target || !a.prologue || (taps && (!a.tap_window[0] || !a.tap_window[1] || !a.tap_target)) ||
        (bolts && (!a.bolt_tap_window || !a.bolt_tap_target || !a.muzzle_window || !a.muzzle_target || !a.flight_window ||
                   !a.flight_target)) ||
        (booms && (!a.death_area_window || !a.death_other_window || !a.death_target))) {
        site_state_[0] = "invalid_site";
        return done("invalid_site", false);
    }
    if (!engine_patch::install_window_open()) {
        for (unsigned i = 0; i < site_count; ++i)
            if (used(i)) site_state_[i] = "late_claim";
        return done("late_claim", false);
    }
    // Every window and witness before any write: a changed byte anywhere costs no rollback.
    if (!window_matches(a.impact_window, sites::expected_impact_window, sites::impact_window_length)) {
        site_state_[0] = "bytes_mismatch";
        return done("bytes_mismatch_impact", false);
    }
    if (!window_matches(a.prologue, sites::expected_prologue, sites::prologue_length) || !witnesses_match(a.prologue)) {
        site_state_[0] = "frame_mismatch";
        return done("frame_mismatch", false);
    }
    if (taps)
        for (unsigned i = 0; i < 2; ++i)
            if (!window_matches(a.tap_window[i], sites::expected_tap_window[i], sites::tap_window_length)) {
                site_state_[1 + i] = "bytes_mismatch";
                return done("bytes_mismatch_tap", false);
            }
    if (bolts)
        for (unsigned i = 3; i < 6; ++i) {
            const BoltSite b = bolt_site(a, i);
            if (!window_matches(b.window, b.expected, b.length)) {
                site_state_[i] = "bytes_mismatch";
                return done(i == 3 ? "bytes_mismatch_bolt_tap" : i == 4 ? "bytes_mismatch_muzzle" : "bytes_mismatch_flight",
                            false);
            }
        }
    if (booms)
        for (unsigned i = 6; i < site_count; ++i) {
            const BoltSite b = death_site(a, i);
            if (!window_matches(b.window, b.expected, b.length)) {
                site_state_[i] = "bytes_mismatch";
                return done(i == 6 ? "bytes_mismatch_death_area" : "bytes_mismatch_death_other", false);
            }
        }
    if (!pin_self()) {
        site_state_[0] = "pin_failed";
        return done("pin_failed", false);
    }
    core::HookState& s = hook_state();
    core::ring_clear(s.ring);
    s.table = table_;
    s.mode = std::uint8_t(mode);
    s.armed = mode == core::mode_shields || mode == core::mode_reshade ? core::family_shields
              : core::hull_mode(mode)
                  ? std::uint8_t(core::family_shields | core::family_hull | (bolts ? core::family_bolts : 0) |
                                 (booms ? core::family_boom : 0))
                  : std::uint8_t(0);
    s.boom_ready = 0;
    s.boom_floor_ms = 0;
    s.death_ms = 0;
    core::BoltHookState& bs = bolt_hook_state();
    bs.armed = 0; // set once every site is live
    bs.owned = 0;
    bs.muzzle_owned = 0;
    bs.globals = s.globals;
    s.owned_next = 0;
    s.lifetime_ms = std::uint16_t(core::patch_lifetime_ms);
    s.hull_lifetime_ms = std::uint16_t(core::hull_lifetime_ms);
    s.life_floor_ms = 0;
    s.suppress_native = core::suppress_native_shield_hit; // true: a stage-owned shield hit skips the native spawn
    s.suppress_native_hull = core::suppress_native_hull_hit; // true: a stage-owned hull hit too (shields,hull)
    x3m_weapon_effects_continue_impact = a.impact_target; // published before the redirects go live
    x3m_weapon_effects_continue_tap = a.tap_target;
    if (bolts) {
        x3m_weapon_effects_continue_bolt_tap = a.bolt_tap_target;
        x3m_weapon_effects_continue_muzzle = a.muzzle_target;
        x3m_weapon_effects_continue_flight = a.flight_target;
    }
    if (booms) x3m_weapon_effects_continue_death = a.death_target;
    for (unsigned i = 0; i < site_count; ++i) site_[i] = engine_patch::CallSite{};
    // Atomic sites first (the taps, the muzzle, the flight effect, the other death site), then the plain copies (the bolt
    // tap, the area death site, the impact); all or none.
    const unsigned order[site_count] = {1, 2, 4, 5, 7, 3, 6, 0};
    unsigned claimed[site_count]{};
    unsigned n = 0;
    for (unsigned k = 0; k < site_count; ++k) {
        const unsigned i = order[k];
        if (!used(i)) continue;
        std::uintptr_t at = 0, target = 0;
        void* stub = nullptr;
        if (i >= 3) {
            const BoltSite b = bolt_site(a, i);
            at = b.window + b.offset;
            target = b.target;
            stub = b.stub;
        } else {
            at = i ? a.tap_window[i - 1] + sites::tap_site_offset : a.impact_window + sites::impact_site_offset;
            target = i ? a.tap_target : a.impact_target;
            stub = i ? reinterpret_cast<void*>(&x3m_weapon_effects_stub_tap)
                     : reinterpret_cast<void*>(&x3m_weapon_effects_stub_impact);
        }
        const bool ok = engine_patch::claim_call(site_[i], at, target, stub);
        written_[i] = reached_write(site_[i], ok);
        const char* failed = ok ? confirm(site_[i]) : site_[i].status;
        if (failed) {
            site_state_[i] = failed;
            bool back = true;
            while (n) {
                const unsigned j = claimed[--n];
                back = engine_patch::restore_call(site_[j]) && back;
                site_state_[j] = site_[j].patched_in ? "rollback_failed" : "rolled_back";
            }
            patched_ = false;
            for (unsigned j = 0; j < site_count; ++j) patched_ = patched_ || site_[j].patched_in;
            s.mode = core::mode_native;
            s.armed = 0;
            bs.armed = 0;
            return done(patched_ || !back ? "rollback_failed" : failed, false);
        }
        site_state_[i] = "active";
        claimed[n++] = i;
    }
    patched_ = installed_ = true;
    mode_ = mode;
    bs.armed = std::uint8_t(bolts);
    return done("ok", true);
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) {
        SetLastError(error);
        return installed_;
    }
    wchar_t text[core::setting_capacity]{};
    const DWORD length = x3m::config::get(L"X3M_WEAPON_EFFECTS", text, core::setting_capacity);
    char setting[core::setting_capacity]{};
    printable(text, length, setting);
    core::Mode mode = core::default_mode;
    unsigned ignored = 0;
    bool applied = false, parsed = true;
    const char* reason = nullptr;
    if (length >= core::setting_capacity) {
        reason = "too_long";
        parsed = false;
    } else if (length && !core::parse_mode(text, length, &mode, &ignored)) {
        reason = "invalid_setting";
        parsed = false;
    } else if (mode == core::mode_native)
        reason = "native";
    else if (!object_trace::executable_verified())
        reason = "executable_mismatch";
    else {
        load_table();
        const Addresses a{sites::impact_window_va,       sites::impact_target_va,
                          sites::prologue_va,            {sites::tap_window_va[0], sites::tap_window_va[1]},
                          sites::tap_target_va,          bolt_sites::tap_window_va,
                          bolt_sites::tap_target_va,     bolt_sites::muzzle_window_va,
                          bolt_sites::muzzle_target_va,  bolt_sites::flight_window_va,
                          bolt_sites::flight_target_va,  death_sites::area_window_va,
                          death_sites::other_window_va,  death_sites::target_va};
        applied = install_at(a, mode);
    }
    if (reason) {
        state_ = reason;
        for (unsigned i = 0; i < site_count; ++i) {
            site_state_[i] = reason;
            written_[i] = false;
        }
    }
    // The scratched families' names (2026-11-08): accepted beside shields and ignored, one row each.
    for (unsigned i = 0; i < core::ignored_name_count; ++i)
        if (ignored & (1u << i)) log("weapon_effects_ignored name=%s setting=%s", core::ignored_name(i), setting);
    // reshade is FL's (the bullet batch site): on AP it runs as shields, one row.
    if (parsed && core::reshade_mode(mode))
        log("weapon_effects_reshade edition=ap runs=shields reason=no_bullet_batch_site setting=%s", setting);
    for (unsigned i = 0; i < site_count; ++i)
        log("weapon_effects_patch site=%s va=%08lx state=%s reason=%s mode=%s setting=%s write=%s", site_name(i),
            static_cast<unsigned long>(site_va(i)), site_state_[i], state_, parsed ? core::mode_name(mode) : "-", setting,
            write_path(i));
    SetLastError(error);
    return applied;
}
#else
// FL: X3FL.exe's seven calls at base + RVA (src/engine/fl/weapon_effects_sites_fl.h). The order of install_at: every
// used window and witness compared before any write (the relocated dword must be base + its target), the DLL pinned,
// the globals and continues bound, then the atomic claims (the taps, the muzzle, the flight, the death, the impact) and
// last the plain copy (the object tap, whose call straddles a qword), each read back; all or none.
bool install_base(std::uintptr_t base, core::Mode mode) {
    const DWORD error = GetLastError();
    const auto done = [&](const char* reason, bool ok) {
        state_ = reason;
        SetLastError(error);
        return ok;
    };
    if (patched_) {
        state_ = "already_installed";
        SetLastError(error);
        return false;
    }
    for (unsigned i = 0; i < site_count; ++i) {
        written_[i] = false;
        site_state_[i] = "not_attempted";
    }
    const bool taps = mode == core::mode_observe;
    const bool bolts = core::bolts_mode(mode);
    const bool booms = core::explosions_mode(mode);
    // The projectile stage (every stage mode on FL): the object tap registers the enabled bodies' bullets, the batch
    // submit skips their instanced draw while the stage owns the frame, and the muzzle site skips their shots' game
    // flash (the stage draws its own; docs/architecture/muzzle-flash-look.md section 4).
    // Reshade mode (bullet-draw-batch.md section 6): the shield stage and the batch call claimed with the scope stub,
    // which forwards every submit; no projectile stage, batch skip, object tap or muzzle stub.
    const bool reshade = core::reshade_mode(mode);
    const bool projectiles = core::stage_mode(mode) && !reshade;
    if (!taps) site_state_[1] = site_state_[2] = "not_used";
    if (!bolts && !projectiles) site_state_[4] = "not_used";
    if (!bolts) site_state_[5] = "not_used";
    if (!bolts && !projectiles) site_state_[3] = "not_used";
    if (!booms) site_state_[6] = "not_used";
    if (!projectiles && !reshade) site_state_[7] = "not_used";
    const auto used = [&](unsigned i) {
        return i == 0 || (i < 3     ? taps
                          : i == 3 || i == 4 ? bolts || projectiles
                          : i == 5           ? bolts
                          : i == 6           ? booms
                                             : projectiles || reshade);
    };
    core::ProjectilePool& pool = x3m_projectile_pool;
    pool.skip_on = 0;
    pool.armed = 0;
    x3m_bullet_reshade_armed = 0;
    x3m_bullet_scope = 0;
    if (mode == core::mode_native) return done("native", false);
    if (!base || (base & 0xffffu)) {
        site_state_[0] = "invalid_site";
        return done("invalid_site", false);
    }
    if (!engine_patch::install_window_open()) {
        for (unsigned i = 0; i < site_count; ++i)
            if (used(i)) site_state_[i] = "late_claim";
        return done("late_claim", false);
    }
    static const char* const mismatch[site_count] = {"bytes_mismatch_impact", "bytes_mismatch_tap", "bytes_mismatch_tap",
                                                     "bytes_mismatch_bolt_tap", "bytes_mismatch_muzzle",
                                                     "bytes_mismatch_flight", "bytes_mismatch_death",
                                                     "bytes_mismatch_batch"};
    for (unsigned i = 0; i < site_count; ++i) {
        if (!used(i)) continue;
        if (!fl_window_matches(base, S::sites[i].window)) {
            site_state_[i] = "bytes_mismatch";
            return done(mismatch[i], false);
        }
    }
    if (!fl_witnesses_match(base, S::impact_witnesses)) {
        site_state_[0] = "frame_mismatch";
        return done("frame_mismatch", false);
    }
    if (used(4) && !fl_witnesses_match(base, S::muzzle_witnesses)) {
        site_state_[4] = "frame_mismatch";
        return done("frame_mismatch_muzzle", false);
    }
    if (booms && !fl_witnesses_match(base, S::death_witnesses)) {
        site_state_[6] = "frame_mismatch";
        return done("frame_mismatch_death", false);
    }
    if (!pin_self()) {
        site_state_[0] = "pin_failed";
        return done("pin_failed", false);
    }
    core::HookState& s = hook_state();
    core::ring_clear(s.ring);
    s.globals = core::fl_globals(base);
    s.table = table_;
    s.mode = std::uint8_t(mode);
    s.armed = mode == core::mode_shields || mode == core::mode_reshade ? core::family_shields
              : core::hull_mode(mode)
                  ? std::uint8_t(core::family_shields | core::family_hull | (bolts ? core::family_bolts : 0) |
                                 (booms ? core::family_boom : 0))
                  : std::uint8_t(0);
    s.boom_ready = 0;
    s.boom_floor_ms = 0;
    s.death_ms = 0;
    core::BoltHookState& bs = bolt_hook_state();
    bs.armed = 0; // set once every site is live
    bs.owned = 0;
    bs.muzzle_owned = 0;
    bs.globals = s.globals;
    s.owned_next = 0;
    s.lifetime_ms = std::uint16_t(core::patch_lifetime_ms);
    s.hull_lifetime_ms = std::uint16_t(core::hull_lifetime_ms);
    s.life_floor_ms = 0;
    s.suppress_native = core::suppress_native_shield_hit;
    s.suppress_native_hull = core::suppress_native_hull_hit;
    // The projectile pool: empty, the enabled rows of the loaded table (none without one: nothing is ever skipped), the
    // bodies resolved by the first tap that can read the engine's TBullets.
    core::projectile_clear(pool);
    pool.globals = s.globals;
    pool.resolved = 0;
    pool.stale = 0;
    pool.engine_rows = 0;
    pool.table_rows = table_ ? table_->rows : 0;
    pool.body_count = 0;
    for (std::uint32_t& w : pool.bits) w = 0;
    for (core::MuzzleRecord& m : pool.muzzles) m = core::MuzzleRecord{}; // the muzzle site's records start empty
    pool.muzzle_sequence = 0;
    projectile_rows_ = table_ ? x3m::weapon_effects::projectile_family::resolve_rows(*table_)
                              : x3m::weapon_effects::projectile_family::Resolution{};
    pool.row_count = projectile_rows_.count;
    for (unsigned i = 0; i < projectile_rows_.count; ++i) {
        pool.rows[i] = projectile_rows_.row[i];
        pool.row_family[i] = projectile_rows_.family[i];
    }
    projectile_seen_skipped_ = pool.skipped;
    projectile_seen_ = pool.counters;
    // Published before the redirects go live.
    x3m_weapon_effects_continue_impact = base + S::sites[0].target_rva;
    x3m_weapon_effects_continue_tap = base + S::sites[1].target_rva;
    x3m_weapon_effects_continue_bolt_tap = base + S::sites[3].target_rva;
    x3m_weapon_effects_continue_spawner = base + S::sites[4].target_rva;
    x3m_weapon_effects_continue_death = base + S::sites[6].target_rva;
    x3m_weapon_effects_continue_batch = base + S::sites[7].target_rva;
    bound_base_ = base;
    for (unsigned i = 0; i < site_count; ++i) site_[i] = engine_patch::CallSite{};
    const unsigned order[site_count] = {1, 2, 4, 5, 6, 7, 0, 3};
    unsigned claimed[site_count]{};
    unsigned n = 0;
    for (unsigned k = 0; k < site_count; ++k) {
        const unsigned i = order[k];
        if (!used(i)) continue;
        const S::Site& site = S::sites[i];
        const bool ok = engine_patch::claim_call(site_[i], base + site.window.rva + site.call_offset,
                                                 base + site.target_rva, fl_stub(i, reshade));
        written_[i] = reached_write(site_[i], ok);
        const char* failed = ok ? confirm(site_[i]) : site_[i].status;
        if (failed) {
            site_state_[i] = failed;
            bool back = true;
            while (n) {
                const unsigned j = claimed[--n];
                back = engine_patch::restore_call(site_[j]) && back;
                site_state_[j] = site_[j].patched_in ? "rollback_failed" : "rolled_back";
            }
            patched_ = false;
            for (unsigned j = 0; j < site_count; ++j) patched_ = patched_ || site_[j].patched_in;
            s.mode = core::mode_native;
            s.armed = 0;
            bs.armed = 0;
            pool.armed = 0;
            pool.skip_on = 0;
            return done(patched_ || !back ? "rollback_failed" : failed, false);
        }
        site_state_[i] = "active";
        claimed[n++] = i;
    }
    patched_ = installed_ = true;
    mode_ = mode;
    bs.armed = std::uint8_t(bolts);
    pool.armed = std::uint8_t(projectiles);
    x3m_bullet_reshade_armed = std::uint32_t(reshade);
    return done("ok", true);
}
#ifdef X3M_WEAPON_EFFECTS_SHIM
void fixture_base(std::uintptr_t base) {
    fixture_base_ = base;
}
#endif
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) {
        SetLastError(error);
        return installed_;
    }
    // Nothing reads an AP address on FL: the slots stay 0 until install_base binds them.
    hook_state().globals = core::unbound_globals();
    bolt_hook_state().globals = hook_state().globals;
    wchar_t text[core::setting_capacity]{};
    const DWORD length = x3m::config::get(L"X3M_WEAPON_EFFECTS", text, core::setting_capacity);
    char setting[core::setting_capacity]{};
    printable(text, length, setting);
    core::Mode mode = core::default_mode;
    unsigned ignored = 0;
    bool applied = false, parsed = true;
    const char* reason = nullptr;
#ifdef X3M_WEAPON_EFFECTS_SHIM
    const std::uintptr_t base = fixture_base_ ? fixture_base_ : x3m::fl::module_base();
#else
    const std::uintptr_t base = x3m::fl::module_base();
#endif
    if (length >= core::setting_capacity) {
        reason = "too_long";
        parsed = false;
    } else if (length && !core::parse_mode(text, length, &mode, &ignored)) {
        reason = "invalid_setting";
        parsed = false;
    } else if (mode == core::mode_native)
        reason = "native";
    else if (!base)
        reason = "executable_mismatch";
    else {
        load_table();
        applied = install_base(base, mode);
    }
    if (reason) {
        state_ = reason;
        for (unsigned i = 0; i < site_count; ++i) {
            site_state_[i] = reason;
            written_[i] = false;
        }
    }
    for (unsigned i = 0; i < core::ignored_name_count; ++i)
        if (ignored & (1u << i)) log("weapon_effects_ignored name=%s setting=%s", core::ignored_name(i), setting);
    for (unsigned i = 0; i < site_count; ++i) {
        const std::uint32_t rva = S::sites[i].window.rva + S::sites[i].call_offset;
        log("weapon_effects_patch site=%s va=%08lx rva=%06lx edition=fl state=%s reason=%s mode=%s setting=%s write=%s",
            S::sites[i].window.name, static_cast<unsigned long>(base ? base + rva : 0), static_cast<unsigned long>(rva),
            site_state_[i], state_, parsed ? core::mode_name(mode) : "-", setting, write_path(i));
    }
    // Reshade mode: the batch call's scope claim (one row a session) instead of the projectile stage's.
    if (parsed && core::reshade_mode(mode)) {
        const char* b = site_state_[7];
        const std::uint32_t rva = S::sites[7].window.rva + S::sites[7].call_offset;
        log("bullet_reshade_site state=%s rva=%06lx va=%08lx reason=%s site=%s armed=%lu", !std::strcmp(b, "active") ? "claimed" : "refused",
            static_cast<unsigned long>(rva), static_cast<unsigned long>(base ? base + rva : 0), state_, b,
            static_cast<unsigned long>(x3m_bullet_reshade_armed));
    } else {
        // The projectile stage's claim (one row a session) and its enabled rows (weapon_projectile_family.h).
        const char* b = site_state_[7];
        const char* verdict = !std::strcmp(b, "active")                                         ? "claimed"
                              : !std::strcmp(b, "rolled_back") || !std::strcmp(b, "rollback_failed") ||
                                        !std::strcmp(b, "readback_rolled_back")
                                    ? "rollback"
                                                                                                        : "refused";
        const std::uint32_t rva = S::sites[7].window.rva + S::sites[7].call_offset;
        log("projectile_site state=%s rva=%06lx va=%08lx reason=%s site=%s object_tap=%s rows=%u bodies_pending=%u",
            verdict, static_cast<unsigned long>(rva), static_cast<unsigned long>(base ? base + rva : 0), state_, b,
            site_state_[3], projectile_rows_.count, unsigned(!x3m_projectile_pool.resolved));
        namespace pf = x3m::weapon_effects::projectile_family;
        for (unsigned f = 0; f < pf::family_count && !std::strcmp(b, "active"); ++f) {
            char rows[pf::max_rows * 4 + 2];
            unsigned at = 0;
            for (unsigned i = 0; i < projectile_rows_.count; ++i) {
                if (projectile_rows_.family[i] != f) continue;
                const unsigned r = projectile_rows_.row[i];
                if (at) rows[at++] = ',';
                if (r >= 100) rows[at++] = char('0' + r / 100);
                if (r >= 10) rows[at++] = char('0' + r / 10 % 10);
                rows[at++] = char('0' + r % 10);
            }
            rows[at] = 0;
            log("projectile_rows family=%s rows=%s refused=%u shared=%u", pf::families[f].name, at ? rows : "-",
                projectile_rows_.refused, projectile_rows_.shared);
        }
    }
    SetLastError(error);
    return applied;
}
#endif
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    installed_ = false;
    hook_state().mode = core::mode_native; // a thread still inside a stub forwards
    bolt_hook_state().armed = 0;
    bolt_hook_state().owned = 0;
    bolt_hook_state().muzzle_owned = 0;
#if X3M_EDITION_FL
    static constexpr unsigned reverse[site_count] = {3, 0, 7, 6, 5, 4, 2, 1}; // the reverse of install_base's order
    x3m_projectile_pool.skip_on = 0; // the batch stub forwards from here on
    x3m_projectile_pool.armed = 0;
    x3m_bullet_reshade_armed = 0; // Present stops its row and counter reset; the draw hook keys on x3m_bullet_scope,
                                  // which no submit raises once the site is restored below
#else
    static constexpr unsigned reverse[site_count] = {0, 6, 3, 7, 5, 4, 2, 1}; // the reverse of the install order
#endif
    for (unsigned k = 0; k < site_count; ++k) {
        const unsigned i = reverse[k];
        if (!site_[i].patched_in) continue;
        engine_patch::restore_call(site_[i]);
        site_state_[i] = site_[i].status;
    }
    patched_ = false;
    for (unsigned i = 0; i < site_count; ++i) patched_ = patched_ || site_[i].patched_in;
    state_ = patched_ ? "restore_failed" : "restored";
    SetLastError(error);
    return !patched_;
}
bool installed() {
    return installed_;
}
core::Mode mode() {
    return installed_ ? mode_ : core::mode_native;
}
const char* state() {
    return state_;
}
const char* site_state(unsigned site) {
    return site < site_count ? site_state_[site] : "invalid_site";
}
const char* write_path(unsigned site) {
    if (site >= site_count || !written_[site]) return "none";
    return site_[site].atomic_write ? "atomic" : "plain";
}
std::uint32_t game_ms() {
    std::uint32_t cfg = 0, now = 0;
    const core::Globals& g = hook_state().globals; // the engine's slots (the fixture points them at its own pages)
    if (!engine_memory::read(g.cfg_slot, &cfg, 4) || !cfg) return 0;
    return engine_memory::read(cfg + core::layout::cfg_game_ms, &now, 4) ? now : 0;
}
std::uint32_t game_step_ms() {
    std::uint32_t cfg = 0, step = 0;
    const core::Globals& g = hook_state().globals;
    if (!engine_memory::read(g.cfg_slot, &cfg, 4) || !cfg) return 0;
    return engine_memory::read(cfg + core::projectile::cfg_step_ms, &step, 4) ? step : 0;
}
static unsigned clear_owned_count() {
    core::Ring& r = hook_state().ring;
    unsigned n = 0;
    for (unsigned i = 0; i < core::ring_capacity; ++i)
        if (r.slots[i].sequence && (r.slots[i].flags & core::flag_stage)) {
            core::ring_release(r, i);
            ++n;
        }
    return n;
}
std::uint32_t scope_changes() {
    return scope_changes_;
}
void clear_owned() {
    if (installed_) clear_owned_count();
}
#if X3M_EDITION_FL
// The projectile stage's figures in weapon_effects_frame (FL only; the AP row is unchanged): frames owned, batch
// submits skipped, traversal draws dropped and unseen, bolts drawn, entries tracked now, retired, the pool's overflow and
// recycled nodes since the last row, the stage's worst CPU time, and the entry-frames whose position did not change
// since the previous stage frame (static) or left the velocity prediction (jumped), and the bolt-frames whose comet ring
// the adaptive rule trimmed (ring_trimmed; projectile-look.md section 2). The muzzle (muzzle-flash-look.md section 4):
// enabled-body shots whose game flash the muzzle site skipped and those it forwarded (latch off: the engine's flash
// shows), records evicted; the tap's flash matches (matched, unmatched, registered too late); flash quads and hull
// pulses drawn, flashes whose shooter could not be read, flashes past the frame's room, pulses shrunk to the screen cap.
#define X3M_PROJECTILE_FIELDS                                                                                          \
    " projectile_owned=%lu projectile_skipped=%lu projectile_dropped=%lu projectile_unhandled=%lu projectile_bolts=%lu " \
    "projectile_tracked=%lu projectile_retired=%lu projectile_overflow=%lu projectile_recycled=%lu projectile_bodies=%lu " \
    "projectile_us_max=%.1f projectile_unpooled=%lu projectile_sampled=%lu projectile_static=%lu projectile_jumped=%lu " \
    "projectile_ring_trimmed=%lu muzzle_skipped=%lu muzzle_unskipped=%lu muzzle_evicted=%lu flash_matched=%lu "        \
    "flash_unmatched=%lu flash_stale=%lu flash_drawn=%lu pulse_drawn=%lu flash_lost=%lu flash_dropped=%lu pulse_clamped=%lu"
#define X3M_PROJECTILE_ARGS                                                                                            \
    , static_cast<unsigned long>(period_.projectile_owned),                                                           \
        static_cast<unsigned long>(x3m_projectile_pool.skipped - projectile_seen_skipped_),                             \
        static_cast<unsigned long>(period_.projectile_dropped), static_cast<unsigned long>(period_.projectile_unhandled), \
        static_cast<unsigned long>(period_.projectile_bolts), static_cast<unsigned long>(x3m_projectile_pool.count),     \
        static_cast<unsigned long>(period_.projectile_retired),                                                         \
        static_cast<unsigned long>(x3m_projectile_pool.counters.overflow - projectile_seen_.overflow),                 \
        static_cast<unsigned long>(x3m_projectile_pool.counters.recycled - projectile_seen_.recycled),                 \
        static_cast<unsigned long>(x3m_projectile_pool.body_count), double(period_.projectile_us_max),                \
        static_cast<unsigned long>(period_.projectile_unpooled), static_cast<unsigned long>(period_.projectile_sampled), \
        static_cast<unsigned long>(period_.projectile_static), static_cast<unsigned long>(period_.projectile_jumped), \
        static_cast<unsigned long>(period_.projectile_ring_trimmed),                                                  \
        static_cast<unsigned long>(x3m_projectile_pool.counters.muzzle_skipped - projectile_seen_.muzzle_skipped),     \
        static_cast<unsigned long>(x3m_projectile_pool.counters.muzzle_unskipped - projectile_seen_.muzzle_unskipped), \
        static_cast<unsigned long>(x3m_projectile_pool.counters.muzzle_evicted - projectile_seen_.muzzle_evicted),     \
        static_cast<unsigned long>(x3m_projectile_pool.counters.flash_matched - projectile_seen_.flash_matched),       \
        static_cast<unsigned long>(x3m_projectile_pool.counters.flash_unmatched - projectile_seen_.flash_unmatched),   \
        static_cast<unsigned long>(x3m_projectile_pool.counters.flash_stale - projectile_seen_.flash_stale),           \
        static_cast<unsigned long>(period_.projectile_flashes), static_cast<unsigned long>(period_.projectile_pulses), \
        static_cast<unsigned long>(period_.projectile_flash_lost),                                                     \
        static_cast<unsigned long>(period_.projectile_flash_dropped),                                                     \
        static_cast<unsigned long>(period_.projectile_pulse_clamped)
#else
#define X3M_PROJECTILE_FIELDS ""
#define X3M_PROJECTILE_ARGS
#endif
void present(unsigned long long frame, bool capture, bool owned, const StageFrame& stage, const core::Scope& scope) {
    if (!installed_) return;
    core::HookState& s = hook_state();
    // A load, a sector change or a new load epoch: the stage-owned records of the scene that went leave the ring.
    if (scope_.changed(scope) && core::stage_mode(mode_)) {
        ++scope_changes_;
#if X3M_EDITION_FL
        core::projectile_clear(x3m_projectile_pool); // a load or a sector change: the scene's bullets went with it
#endif
        const unsigned cleared = clear_owned_count();
        if (cleared) {
            ++scope_clears_;
            log("weapon_effects_scope frame=%llu cleared=%u loading=%u sector_key=%016llx load_epoch=%llu clears=%lu", frame,
                cleared, unsigned(scope.loading), static_cast<unsigned long long>(scope_.sector_key),
                static_cast<unsigned long long>(scope_.load_epoch), static_cast<unsigned long>(scope_clears_));
        }
    }
    // The next frame's latch (the stage's modes only; observe never skips). No stage draws hull hits or deaths since the
    // look layer was scratched (2026-11-08): their latches stay 0, so those records forward native.
    s.owned_next = std::uint8_t(core::stage_mode(mode_) && owned);
    s.hull_ready = 0;
    s.boom_ready = 0;
#if X3M_EDITION_FL
    // The projectile stage's latch for the next frame's batch submits and traversal draws: it drew (or had nothing to
    // draw while armed) this frame and the enabled bodies are resolved.
    core::ProjectilePool& pool = x3m_projectile_pool;
    pool.skip_on = std::uint32_t(pool.armed && stage.projectile_owned && pool.body_count && !pool.stale);
    static bool stale_logged = false;
    if (pool.stale && !stale_logged) {
        stale_logged = true;
        log("projectile_rows state=stale table_rows=%lu engine_rows=%lu (the stage stays off: regenerate weapon_effects.json)",
            static_cast<unsigned long>(pool.table_rows), static_cast<unsigned long>(pool.engine_rows));
    }
    if (stage.projectile_owned) ++period_.projectile_owned;
    period_.projectile_bolts += stage.projectile_bolts;
    period_.projectile_dropped += stage.projectile_dropped;
    period_.projectile_retired += stage.projectile_retired;
    period_.projectile_unhandled += stage.projectile_unhandled;
    period_.projectile_unpooled += stage.projectile_unpooled;
    period_.projectile_sampled += stage.projectile_sampled;
    period_.projectile_static += stage.projectile_static;
    period_.projectile_jumped += stage.projectile_jumped;
    period_.projectile_ring_trimmed += stage.projectile_ring_trimmed;
    period_.projectile_flashes += stage.projectile_flashes;
    period_.projectile_pulses += stage.projectile_pulses;
    period_.projectile_flash_lost += stage.projectile_flash_lost;
    period_.projectile_flash_dropped += stage.projectile_flash_dropped;
    period_.projectile_pulse_clamped += stage.projectile_pulse_clamped;
    if (stage.projectile_us > period_.projectile_us_max) period_.projectile_us_max = stage.projectile_us;
    // The bullet re-shade (reshade mode): a scope count leaked by an exception through the submit lasts one frame.
    if (x3m_bullet_reshade_armed) {
        x3m_bullet_scope = 0;
        period_.reshade_draws += stage.reshade_draws;
        period_.reshade_failures += stage.reshade_failures;
        period_.reshade_outside += stage.reshade_outside;
        period_.reshade_timed += stage.reshade_timed;
        period_.reshade_additive += stage.reshade_additive;
        period_.reshade_flagged += stage.reshade_flagged;
        period_.reshade_us_sum += stage.reshade_us;
        if (stage.reshade_us > period_.reshade_us_max) period_.reshade_us_max = stage.reshade_us;
    }
#endif
    ++period_.frames;
    if (owned) ++period_.owned;
    if (stage.drew) ++period_.drew;
    period_.patches += stage.patches;
    period_.decals += stage.decals;
    period_.retired += stage.retired;
    period_.dropped += stage.dropped;
    period_.cells_dropped += stage.cells_dropped;
    period_.hits_dropped += stage.hits_dropped;
    period_.spills += stage.spills;
    period_.faces_culled += stage.faces_culled;
    period_.area_dropped += stage.area_dropped;
    period_.bubbles += stage.bubbles;
    period_.bubble_fallback += stage.bubble_fallback;
    period_.bubble_walks += stage.bubble_walks;
    period_.bubble_failures += stage.bubble_failures;
    period_.bubble_held += stage.bubble_held;
    period_.bubble_deferred += stage.bubble_deferred;
    period_.bubble_clamped += stage.bubble_clamped;
    period_.stage_us_sum += stage.stage_us;
    if (stage.stage_us > period_.stage_us_max) period_.stage_us_max = stage.stage_us;
    const std::uint32_t now = game_ms();
    core::Ring& r = s.ring;
    for (unsigned i = 0; i < core::ring_capacity; ++i) {
        core::Event& e = r.slots[i];
        if (!e.sequence) continue;
        if (!(e.flags & core::flag_logged) && e.role == core::role_explosion) {
            // Step 3: a death record (core::death): its tier, class, subtype, death row, radius, ours or forwarded, the
            // site (area 0x0044ac64 / other 0x0044acb2), the reason word, the killer argument (pointer or id [u]),
            // nested (another death in the same game ms), the world position (m) and the TShips class id.
            e.flags |= core::flag_logged;
            ++period_.deaths;
            if (e.flags & core::flag_stage) ++period_.deaths_ours;
            if (capture || logged_deaths_ < death_row_budget || period_deaths_ < death_period_budget) {
                const char* sample = capture ? "capture" : logged_deaths_ < death_row_budget ? "start" : "period";
                if (!capture) ++(logged_deaths_ < death_row_budget ? logged_deaths_ : period_deaths_);
                const float m = 1.f / core::engine_units_per_metre;
                log("weapon_death frame=%llu seq=%lu tier=%s class=%u row=%u radius_m=%.1f ours=%u site=%s "
                    "reason=%u killer=%08lx nested=%u t0=%lu pos_m=%.1f,%.1f,%.1f class_id=%ld patch_tier=%s "
                    "tier_source=%s owner=%08lx life_ms=%u decision=%s cause=%s row2=%d,%d,%d sample=%s",
                    frame, static_cast<unsigned long>(e.sequence), core::boom::tier_name(e.style), unsigned(e.table),
                    unsigned(e.row == 0xffffu ? 0xffffu : e.row), double(float(e.radius) * m),
                    unsigned((e.flags & core::flag_stage) != 0), e.tint_source ? "other" : "area", unsigned(e.shield_q),
                    static_cast<unsigned long>(std::uint32_t(e.damage)), unsigned((e.flags & core::flag_nested) != 0),
                    static_cast<unsigned long>(e.t0_game_ms), double(float(e.pos[0]) * m), double(float(e.pos[1]) * m),
                    double(float(e.pos[2]) * m), static_cast<long>(std::int32_t(e.tint_rgb)),
                    core::patch_tier_name(e.patch_tier), core::tier_source_name(e.tier_source),
                    static_cast<unsigned long>(e.owner), unsigned(e.lifetime_ms),
                    core::boom::decision_name(core::boom::decide(e.style)), core::boom::cause_name(e.tier),
                    int(e.dir[0]), int(e.dir[1]), int(e.dir[2]), sample);
            }
        }
        if (!(e.flags & core::flag_logged)) {
            e.flags |= core::flag_logged;
            ++period_.events;
            if (e.role == core::role_shield_hit) ++period_.hits;
            if (e.flags & core::flag_forwarded)
                ++period_.forwarded;
            else
                ++period_.skipped;
            if (e.flags & core::flag_stage) ++period_.staged;
            if ((e.flags & core::flag_stage) && e.role == core::role_hull_hit) ++period_.hull_staged;
            if (capture || logged_events_ < event_row_budget || period_events_ < period_row_budget) {
                const char* sample = capture ? "capture" : logged_events_ < event_row_budget ? "start" : "period";
                if (!capture) ++(logged_events_ < event_row_budget ? logged_events_ : period_events_);
                // The contact point's distance from the owner's centre over its radius (obj+0xa4); the bolt's travel
                // (dir, diagnostics only: the screen-space patch picks no face from it); int32 -> double converts with
                // SSE2.
                const double px = double(e.pos[0]), py = double(e.pos[1]), pz = double(e.pos[2]);
                const double pos_r = e.radius > 0 ? double(x3m::scalar::sqrt(float(px * px + py * py + pz * pz))) /
                                                        double(e.radius)
                                                  : 0.0;
                log("weapon_event frame=%llu seq=%lu role=%s table=%s row=%u style=%s%s tier=%s calibre=%.3f absorbed=%u "
                    "damage=%ld forwarded=%u stage=%u nested=%u t0=%lu tint=%s:%06lx owner=%08lx radius=%ld "
                    "pos=%ld,%ld,%ld pos_r=%.3f dir=%d,%d,%d shield=%.4f shield_read=%u drained=%u patch_tier=%s "
                    "tier_source=%s class=%s sample=%s",
                    frame, static_cast<unsigned long>(e.sequence), core::role_name(e.role), core::table_name(e.table),
                    unsigned(e.row), core::shape_name(e.style & core::style_shape_mask),
                    (e.style & core::style_burst) ? "+burst" : "", core::tier_name(e.tier), double(core::calibre_of(e)),
                    unsigned((e.flags & core::flag_absorbed) != 0), static_cast<long>(e.damage),
                    unsigned((e.flags & core::flag_forwarded) != 0), unsigned((e.flags & core::flag_stage) != 0),
                    unsigned((e.flags & core::flag_nested) != 0),
                    static_cast<unsigned long>(e.t0_game_ms), core::tint_source_name(e.tint_source),
                    static_cast<unsigned long>(e.tint_rgb & 0xffffffu), static_cast<unsigned long>(e.owner),
                    static_cast<long>(e.radius), static_cast<long>(e.pos[0]), static_cast<long>(e.pos[1]),
                    static_cast<long>(e.pos[2]), pos_r, int(e.dir[0]), int(e.dir[1]), int(e.dir[2]),
                    double(float(e.shield_q) * (1.f / 65535.f)),
                    unsigned((e.flags & core::flag_shield_read) != 0),
                    unsigned((e.detail & core::detail_drained) != 0), core::patch_tier_name(e.patch_tier),
                    core::tier_source_name(e.tier_source), core::hull_class_name(core::hull_class_of(e)), sample);
            }
        }
        // Census-only records leave once logged; the stage's at the end of their game-time life (a hull hit's, its
        // floored elements included: the stage's life_floor_ms).
        const std::uint32_t extra = e.role == core::role_hull_hit    ? s.life_floor_ms
                                    : e.role == core::role_explosion ? s.boom_floor_ms
                                                                     : 0u;
        if (!(e.flags & core::flag_stage) || (now && core::expired(now, e, extra))) {
            if (e.flags & core::flag_stage) ++period_.expired;
            core::ring_release(r, i);
        }
    }
    if (mode_ == core::mode_observe && s.check.compared != seen_compared_ && (capture || frame % frame_row_period == 0)) {
        seen_compared_ = s.check.compared;
        log("weapon_pos_check frame=%llu compared=%lu unmatched=%lu max_error=%lu taps=%lu", frame,
            static_cast<unsigned long>(s.check.compared), static_cast<unsigned long>(s.check.unmatched),
            static_cast<unsigned long>(s.check.max_error), static_cast<unsigned long>(s.counters.taps));
    }
    if (frame % frame_row_period) return;
    const core::Counters& c = s.counters;
#if X3M_EDITION_FL
    // The bullet re-shade's period row: the game's bullet submits seen by the scope stub, the draws re-shaded and those
    // whose substitution failed (forwarded with the game's program), bullet-program draws outside a submit (left
    // vanilla), and the substitution's CPU time over the timed draws (every 60th frame and capture frames: per draw,
    // and the worst frame's sum).
    if (x3m_bullet_reshade_armed) {
        const std::uint32_t scopes = x3m_bullet_scope_calls;
        log("bullet_reshade_frame frame=%llu frames=%lu submits=%lu draws=%lu additive=%lu flagged=%lu failures=%lu "
            "outside=%lu timed=%lu us_timed=%.1f us_per_draw=%.3f us_frame_max=%.1f",
            frame, static_cast<unsigned long>(period_.frames), static_cast<unsigned long>(scopes - reshade_scopes_seen_),
            static_cast<unsigned long>(period_.reshade_draws), static_cast<unsigned long>(period_.reshade_additive),
            static_cast<unsigned long>(period_.reshade_flagged), static_cast<unsigned long>(period_.reshade_failures),
            static_cast<unsigned long>(period_.reshade_outside), static_cast<unsigned long>(period_.reshade_timed),
            double(period_.reshade_us_sum),
            period_.reshade_timed ? double(period_.reshade_us_sum) / double(period_.reshade_timed) : 0.0,
            double(period_.reshade_us_max));
        reshade_scopes_seen_ = scopes;
    }
#endif
    if (core::explosions_mode(mode_))
        log("weapon_boom_frame frame=%llu deaths=%lu ours=%lu calls=%lu no_node=%lu bullet=%lu rule_b=%lu nested=%lu "
            "fighter=%lu corvette=%lu capital=%lu station=%lu missile=%lu asteroid=%lu other=%lu boom_ready=%u "
            "floor_ms=%lu tier_native=%u",
            frame, static_cast<unsigned long>(period_.deaths), static_cast<unsigned long>(period_.deaths_ours),
            static_cast<unsigned long>(c.deaths - seen_.deaths), static_cast<unsigned long>(c.death_no_node - seen_.death_no_node),
            static_cast<unsigned long>(c.death_bullet - seen_.death_bullet),
            static_cast<unsigned long>(c.death_rule_b - seen_.death_rule_b),
            static_cast<unsigned long>(c.death_nested - seen_.death_nested),
            static_cast<unsigned long>(c.death_tier[0] - seen_.death_tier[0]),
            static_cast<unsigned long>(c.death_tier[1] - seen_.death_tier[1]),
            static_cast<unsigned long>(c.death_tier[2] - seen_.death_tier[2]),
            static_cast<unsigned long>(c.death_tier[3] - seen_.death_tier[3]),
            static_cast<unsigned long>(c.death_tier[4] - seen_.death_tier[4]),
            static_cast<unsigned long>(c.death_tier[5] - seen_.death_tier[5]),
            static_cast<unsigned long>(c.death_tier[6] - seen_.death_tier[6]), unsigned(s.boom_ready),
            static_cast<unsigned long>(s.boom_floor_ms), unsigned(core::boom::tier_native));
    log("weapon_effects_frame frame=%llu mode=%s frames=%lu events=%lu hits=%lu skipped=%lu forwarded=%lu staged=%lu "
        "patches=%u decals=%u retired=%u expired=%lu overflow=%lu evicted_visible=%lu owned=%lu drew=%lu stage_us=%.1f stage_us_max=%.1f "
        "calls=%lu no_projectile=%lu no_node=%lu stale_table=%lu nested=%lu shield_read=%lu shield_unread=%lu "
        "scope_clears=%lu live=%u dropped=%u cells_dropped=%u hits_dropped=%u spills=%u dir_fallback=%lu faces_culled=%u "
        "area_dropped=%u drained=%lu bubbles=%u bubble_fallback=%u bubble_walks=%u bubble_walk_failures=%u "
        "bubble_held=%u bubble_deferred=%u bubble_clamped=%u hull_staged=%lu life_floor_ms=%lu table_hull_key_missing=%u"
        X3M_PROJECTILE_FIELDS,
        frame, core::mode_name(mode_), static_cast<unsigned long>(period_.frames), static_cast<unsigned long>(period_.events),
        static_cast<unsigned long>(period_.hits), static_cast<unsigned long>(period_.skipped),
        static_cast<unsigned long>(period_.forwarded), static_cast<unsigned long>(period_.staged), period_.patches,
        period_.decals,
        period_.retired + unsigned(r.retired - seen_retired_), static_cast<unsigned long>(period_.expired),
        static_cast<unsigned long>(r.overflow - seen_overflow_),
        static_cast<unsigned long>(r.evicted_visible - seen_evicted_visible_),
        static_cast<unsigned long>(period_.owned),
        static_cast<unsigned long>(period_.drew),
        double(period_.drew ? period_.stage_us_sum / float(period_.drew) : 0.f), double(period_.stage_us_max),
        static_cast<unsigned long>(c.calls - seen_.calls), static_cast<unsigned long>(c.no_projectile - seen_.no_projectile),
        static_cast<unsigned long>(c.no_node - seen_.no_node), static_cast<unsigned long>(c.stale_table - seen_.stale_table),
        static_cast<unsigned long>(c.nested - seen_.nested),
        static_cast<unsigned long>(c.shield_read - seen_.shield_read),
        static_cast<unsigned long>(c.shield_unread - seen_.shield_unread), static_cast<unsigned long>(scope_clears_),
        core::ring_capacity - r.free_count, period_.dropped, period_.cells_dropped, period_.hits_dropped,
        period_.spills, static_cast<unsigned long>(c.dir_fallback - seen_.dir_fallback), period_.faces_culled,
        period_.area_dropped, static_cast<unsigned long>(c.drained - seen_.drained), period_.bubbles,
        period_.bubble_fallback, period_.bubble_walks, period_.bubble_failures, period_.bubble_held, period_.bubble_deferred,
        period_.bubble_clamped, static_cast<unsigned long>(period_.hull_staged), static_cast<unsigned long>(s.life_floor_ms),
        unsigned(table_hull_key_missing_) X3M_PROJECTILE_ARGS);
#if X3M_EDITION_FL
    projectile_seen_skipped_ = x3m_projectile_pool.skipped;
    projectile_seen_ = x3m_projectile_pool.counters;
#endif
    seen_ = c;
    seen_overflow_ = r.overflow;
    seen_evicted_visible_ = r.evicted_visible;
    seen_retired_ = r.retired;
    period_ = {};
    period_events_ = 0;
    period_deaths_ = 0;
}
// Step 4: the generation advance and the weapon_bolts_frame row (every 300 frames, and on a capture frame) with the
// hooks' counters since the last row. No in-place draw latches the bolts since the look layer was scratched (2026-11-08):
// owned and muzzle_owned stay 0, so the flight effect and the muzzle always forward.
void present_bolts(unsigned long long frame, bool capture) {
    if (!installed_ || !core::bolts_mode(mode_)) return;
    core::BoltHookState& b = bolt_hook_state();
    b.owned = 0;
    b.muzzle_owned = 0;
    const std::uint32_t passes = b.pass + 1u, listed = core::bolt_list_count(b, core::bolt_list_current(b));
    ++b.generation; // the next frame's list starts empty (the tap resets a list of an older generation)
    ++bolt_frames_;
    if (!capture && frame % frame_row_period) return;
    const core::BoltCounters& c = b.counters;
    log("weapon_bolts_frame frame=%llu frames=%lu listed=%lu passes=%lu tap_calls=%lu tap_bullets=%lu tap_no_node=%lu "
        "tap_overflow=%lu tap_replaced=%lu tap_refreshed=%lu flight_calls=%lu flight_skipped=%lu muzzle_calls=%lu "
        "muzzle_skipped=%lu muzzle_relit=%lu muzzle_evicted=%lu muzzle_restarted=%lu shooter_ship=%lu shooter_dock=%lu "
        "shooter_factory=%lu shooter_other=%lu shooter_other_class=%u tap_tombstoned=%lu tap_unkeyed=%lu capture=%u",
        frame, static_cast<unsigned long>(bolt_frames_), static_cast<unsigned long>(listed),
        static_cast<unsigned long>(passes), static_cast<unsigned long>(c.tap_calls - bolt_seen_.tap_calls),
        static_cast<unsigned long>(c.tap_bullets - bolt_seen_.tap_bullets),
        static_cast<unsigned long>(c.tap_no_node - bolt_seen_.tap_no_node),
        static_cast<unsigned long>(c.tap_overflow - bolt_seen_.tap_overflow),
        static_cast<unsigned long>(c.tap_replaced - bolt_seen_.tap_replaced),
        static_cast<unsigned long>(c.tap_refreshed - bolt_seen_.tap_refreshed),
        static_cast<unsigned long>(c.flight_calls - bolt_seen_.flight_calls),
        static_cast<unsigned long>(c.flight_skipped - bolt_seen_.flight_skipped),
        static_cast<unsigned long>(c.muzzle_calls - bolt_seen_.muzzle_calls),
        static_cast<unsigned long>(c.muzzle_skipped - bolt_seen_.muzzle_skipped),
        static_cast<unsigned long>(c.muzzle_relit - bolt_seen_.muzzle_relit),
        static_cast<unsigned long>(c.muzzle_evicted - bolt_seen_.muzzle_evicted),
        static_cast<unsigned long>(c.muzzle_restarted - bolt_seen_.muzzle_restarted),
        static_cast<unsigned long>(c.shooter_ship - bolt_seen_.shooter_ship),
        static_cast<unsigned long>(c.shooter_dock - bolt_seen_.shooter_dock),
        static_cast<unsigned long>(c.shooter_factory - bolt_seen_.shooter_factory),
        static_cast<unsigned long>(c.shooter_other - bolt_seen_.shooter_other), unsigned(b.shooter_last_other),
        static_cast<unsigned long>(c.tap_tombstoned - bolt_seen_.tap_tombstoned),
        static_cast<unsigned long>(c.tap_unkeyed - bolt_seen_.tap_unkeyed), unsigned(capture));
    if (capture && frame % frame_row_period) return; // a capture frame's row does not end the period
    bolt_seen_ = c;
    bolt_frames_ = 0;
}
} // namespace x3m::weapon_effects
