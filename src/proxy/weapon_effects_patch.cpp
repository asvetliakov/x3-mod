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
#include "bullet_reshade_core.h"
#if X3M_EDITION_FL
#include "../engine/fl/executable_identity_fl.h"
#include "../engine/fl/weapon_effects_sites_fl.h"
#endif
#include <windows.h>
#include <cstdio>
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
// The ship profiles (one heap block for the process, allocated with the first table text; ready only after a parse
// that kept an entry).
core::ShipProfiles* profiles_ = nullptr;
bool profiles_ready_ = false;
unsigned logged_events_ = 0, period_events_ = 0;
// weapon_event rows outside capture frames: the run's first 64, then a rolling sample of up to 8 per weapon_effects_frame
// period (Run235 Flight B: the fixed 64 ended within the first minute, so the own ship's later hits were never seen).
constexpr unsigned event_row_budget = 64, period_row_budget = 8;
constexpr unsigned long long frame_row_period = 300;
struct Period {
    std::uint32_t events = 0, hits = 0, skipped = 0, forwarded = 0, staged = 0, expired = 0, owned = 0, frames = 0, drew = 0;
    unsigned patches = 0, decals = 0, retired = 0, dropped = 0, cells_dropped = 0, hits_dropped = 0, spills = 0;
    unsigned faces_culled = 0, area_dropped = 0;
    unsigned lathes = 0, lathe_no_profile = 0, lathe_mismatch = 0, lathe_unreadable = 0, lathe_scale = 0;
    unsigned lathe_degenerate = 0, lathe_cache = 0, lathe_stale = 0, lathe_deferred = 0, lathe_entry_far = 0;
    std::uint32_t hull_staged = 0;
    float stage_us_max = 0.f, stage_us_sum = 0.f;
    // step 3: death records seen (ours, census)
    std::uint32_t deaths = 0, deaths_ours = 0;
    // the bullet re-shade (reshade mode): draws re-shaded, substitutions failed, bullet-program draws outside a submit,
    // the substitution's CPU time (sum and the frame's worst)
    std::uint32_t reshade_draws = 0, reshade_failures = 0, reshade_outside = 0, reshade_timed = 0;
    std::uint32_t reshade_additive = 0, reshade_flagged = 0; // draws the additive route converted, with its far flag
    std::uint32_t reshade_lone = 0, reshade_lone_flagged = 0; // the lone path's re-shaded draws, with the far flag
    std::uint32_t reshade_lone_failures = 0, reshade_lone_timed = 0; // its failed substitutions, its timed draws
    float reshade_lone_us_sum = 0.f;                                // its sequence's CPU time over them
    std::uint32_t reshade_lone_counts[x3m::weapon_effects::lone_count_slots] = {}; // the lone path's state, flag and refusal counts
    float reshade_us_sum = 0.f, reshade_us_max = 0.f;
    // the muzzle flash re-shade (reshade mode): draws re-shaded, matched, kept off the hull-emission route, failed,
    // timed, and the sequence's CPU time (sum and the frame's worst)
    std::uint32_t muzzle_draws = 0, muzzle_matched = 0, muzzle_hull = 0, muzzle_failures = 0, muzzle_timed = 0;
    float muzzle_us_sum = 0.f, muzzle_us_max = 0.f;
} period_{};
#if X3M_EDITION_FL
std::uint32_t reshade_scopes_seen_ = 0; // x3m_bullet_scope_calls at the last bullet_reshade_frame row
// The geometric size's body map (bullet_reshade_core.h build_body_map / body_layout): engine body id ->
// tables.bullet_bodies entry, built at the first lookup that can read the engine's TBullets; cleared with the table
// (load_table, shutdown) so a lookup never reaches a freed table.
x3m::weapon_effects::reshade::BodyMap body_map_{};
// One build attempt per frame (the Present count of the last attempt against the current one): an unbuilt map's
// retries (bullet_reshade_core.h body_map_retries) then span frames, not the draws of one frame.
unsigned long long body_map_attempt_ = ~0ull, present_frame_ = 0;
#endif
// weapon_death rows outside capture frames: the run's first 64, then up to 16 per weapon_effects_frame period.
constexpr unsigned death_row_budget = 64, death_period_budget = 16;
unsigned logged_deaths_ = 0, period_deaths_ = 0;
core::Counters seen_{};
unsigned native_rows_logged_ = 0;        // weapon_ship_profile_missing: the native rows already logged
std::uint32_t native_more_logged_ = 0;   // and the distinct rows past them already reported
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
#if X3M_EDITION_FL
    body_map_ = x3m::weapon_effects::reshade::BodyMap{}; // built again from this table at the next lookup
    body_map_attempt_ = ~0ull;
#endif
    char* text = nullptr;
    DWORD size = 0;
    char path[260];
    const char* read = read_table_file(&text, &size, path, sizeof path);
    x3m::weapon_effects::table::Stats st{};
    std::size_t fault = 0;
    bool parsed = false;
    const bool had_text = text != nullptr;
    profiles_ready_ = false;
    bool section = false;
    if (text) {
        // the profiles block (about 0.55 MB) only for a table that carries the section (an older table: none)
        section = x3m::weapon_effects::table::carries_ship_profiles(text, size);
        if (!profiles_ && section) profiles_ = new (std::nothrow) core::ShipProfiles;
        table_ = new (std::nothrow) core::Table;
        if (table_) parsed = x3m::weapon_effects::table::parse(text, size, table_, &st, &fault, profiles_);
        delete[] text;
        if (!parsed) {
            delete table_;
            table_ = nullptr;
        }
        profiles_ready_ = parsed && profiles_ && profiles_->count > 0;
    }
    x3m::log("weapon_ship_profiles event=%s present=%u profiles=%u dropped=%u class_unknown=%u tships_rows=%lu "
             "capacity=%u block_bytes=%lu slices=%u",
             profiles_ready_ ? "loaded" : !section ? "absent" : !profiles_ ? "allocation" : "none",
             unsigned(st.profiles_present), profiles_ready_ ? profiles_->count : 0u, st.profiles_dropped,
             st.profiles_class_unknown, static_cast<unsigned long>(profiles_ready_ ? profiles_->tships_rows : 0u),
             core::profile_capacity, static_cast<unsigned long>(profiles_ ? sizeof(core::ShipProfiles) : 0),
             core::profile_slices);
    table_hull_key_missing_ = !parsed || !st.hull_keys;
    // The live count is checked per hit (core::impact: a differing *0x00607038 disables the lookups, stale_table).
    x3m::log("weapon_effects_table event=%s reason=%s rows=%lu missile_rows=%lu entries=%u textures=%u overrides=%u "
             "dropped=%u hull_keys=%u bolt_pins=%u bolt_rejected=%u table_hull_key_missing=%u bullet_textures=%u "
             "bullet_textures_dropped=%u bullet_bodies=%u bullet_bodies_dropped=%u bullet_bodies_key=%u bytes=%lu "
             "fault=%lu path=\"%s\"",
             parsed ? "loaded" : had_text ? "rejected" : read, parsed ? "ok" : had_text ? "malformed" : read,
             static_cast<unsigned long>(parsed ? table_->rows : 0), static_cast<unsigned long>(parsed ? table_->missile_rows : 0),
             st.entries, st.textures, st.overrides, st.dropped, st.hull_keys, st.bolt_pins, st.bolt_rejected, unsigned(table_hull_key_missing_),
             st.bullet_textures, st.bullet_textures_dropped, st.bullet_bodies, st.bullet_bodies_dropped,
             unsigned(st.bullet_bodies_present), static_cast<unsigned long>(size), static_cast<unsigned long>(fault), path);
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
void* fl_stub(unsigned i) {
    switch (i) {
    case 0: return reinterpret_cast<void*>(&x3m_weapon_effects_stub_impact_fl);
    case 1:
    case 2: return reinterpret_cast<void*>(&x3m_weapon_effects_stub_tap_fl);
    case 3: return reinterpret_cast<void*>(&x3m_weapon_effects_stub_bolt_tap_fl);
    case 4: return reinterpret_cast<void*>(&x3m_weapon_effects_stub_muzzle_fl);
    case 5: return reinterpret_cast<void*>(&x3m_weapon_effects_stub_flight_fl);
    case 6: return reinterpret_cast<void*>(&x3m_weapon_effects_stub_death_fl);
    default: return reinterpret_cast<void*>(&x3m_weapon_effects_stub_batch_scope_fl); // reshade mode only
    }
}
std::uintptr_t bound_base_ = 0; // the base the sites were claimed at (the rows' va)
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
    s.profiles = ship_profiles(); // null without a loaded profiles block: every ship forwards native (profile_none)
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
        log("weapon_effects_reshade edition=ap runs=shields reason=no_bullet_batch_site muzzle=vanilla "
            "muzzle_reason=fl_only setting=%s",
            setting);
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
    // Reshade mode (bullet-draw-batch.md section 6): the shield stage and the batch call claimed with the scope stub,
    // which forwards every submit. The object tap and the muzzle serve the bolts modes only (the custom projectile
    // stage that also claimed them, and the batch call with a skip stub, in every FL stage mode was removed on
    // 2026-11-13).
    const bool reshade = core::reshade_mode(mode);
    if (!taps) site_state_[1] = site_state_[2] = "not_used";
    if (!bolts) site_state_[3] = site_state_[4] = site_state_[5] = "not_used";
    if (!booms) site_state_[6] = "not_used";
    if (!reshade) site_state_[7] = "not_used";
    const auto used = [&](unsigned i) {
        return i == 0 || (i < 3 ? taps : i <= 5 ? bolts : i == 6 ? booms : reshade);
    };
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
    s.profiles = ship_profiles(); // null without a loaded profiles block: every ship forwards native (profile_none)
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
                                                 base + site.target_rva, fl_stub(i));
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
    // Reshade mode: the batch call's scope claim (one row a session).
    if (parsed && core::reshade_mode(mode)) {
        const char* b = site_state_[7];
        const std::uint32_t rva = S::sites[7].window.rva + S::sites[7].call_offset;
        log("bullet_reshade_site state=%s rva=%06lx va=%08lx reason=%s site=%s armed=%lu", !std::strcmp(b, "active") ? "claimed" : "refused",
            static_cast<unsigned long>(rva), static_cast<unsigned long>(base ? base + rva : 0), state_, b,
            static_cast<unsigned long>(x3m_bullet_reshade_armed));
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
    x3m_bullet_reshade_armed = 0; // Present stops its row and counter reset; the draw hook keys on x3m_bullet_scope,
                                  // which no submit raises once the site is restored below
    body_map_ = x3m::weapon_effects::reshade::BodyMap{}; // a reinstall builds it again from its table
    body_map_attempt_ = ~0ull;
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
const core::ShipProfiles* ship_profiles() noexcept {
    return profiles_ready_ ? profiles_ : nullptr;
}
#if X3M_EDITION_FL
// The table is loaded once inside the install window, before the device and its textures exist; read-only after.
int bullet_texture_key(const void* data, std::size_t size) noexcept {
    return table_ && x3m_bullet_reshade_armed ? x3m::weapon_effects::reshade::texture_key(*table_, data, size) : -1;
}
unsigned bullet_texture_count() noexcept {
    return table_ && x3m_bullet_reshade_armed ? table_->bullet_textures : 0u;
}
std::uint16_t bullet_texture_p99_q(unsigned key) noexcept {
    return table_ && key < table_->bullet_textures ? table_->bullet_texture_p99_q[key] : std::uint16_t(0);
}
const std::int32_t* bullet_body_layout(std::uint32_t body, bool* final) noexcept {
    namespace rs = x3m::weapon_effects::reshade;
    *final = true;
    if (!x3m_bullet_reshade_armed) return nullptr;
    if (!body_map_.state) {
        if (body_map_attempt_ == present_frame_) {
            *final = false; // this frame's attempt is spent: asked again next frame
            return nullptr;
        }
        body_map_attempt_ = present_frame_;
        const core::Globals& g = hook_state().globals; // bound at install_base (the engine's TBullets slots)
        const auto read = [](std::uintptr_t a, void* out, std::size_t n) { return engine_memory::read(a, out, n); };
        if (!rs::build_body_map(body_map_, table_, g.bullet_table_slot, g.bullet_count_slot, read)) {
            *final = false; // TBullets not readable yet, its row count not the table's, or a row read failed: asked
                            // again next frame
            return nullptr;
        }
        x3m::log("bullet_reshade_layout event=%s reason=%s bodies_mapped=%u table_entries=%u engine_rows=%lu "
                 "table_rows=%lu failed_builds=%lu (an unmapped body keeps the accepted look: no geometric size)",
                 body_map_.state == 1 ? "mapped" : "off", body_map_.reason, body_map_.count,
                 table_ ? table_->bullet_bodies : 0u, static_cast<unsigned long>(body_map_.rows),
                 static_cast<unsigned long>(table_ ? table_->rows : 0), static_cast<unsigned long>(body_map_.failures));
    }
    return rs::body_layout(body_map_, table_, body);
}
#endif
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
    return engine_memory::read(cfg + core::layout::cfg_step_ms, &step, 4) ? step : 0;
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
void present(unsigned long long frame, bool capture, bool owned, const StageFrame& stage, const core::Scope& scope) {
#if X3M_EDITION_FL
    present_frame_ = frame; // the body map's one build attempt per frame
#endif
    if (!installed_) return;
    core::HookState& s = hook_state();
    // A load, a sector change or a new load epoch: the stage-owned records of the scene that went leave the ring.
    if (scope_.changed(scope) && core::stage_mode(mode_)) {
        ++scope_changes_;
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
    // The bullet re-shade (reshade mode): a scope count leaked by an exception through the submit lasts one frame.
    if (x3m_bullet_reshade_armed) {
        x3m_bullet_scope = 0;
        period_.reshade_draws += stage.reshade_draws;
        period_.reshade_failures += stage.reshade_failures;
        period_.reshade_outside += stage.reshade_outside;
        period_.reshade_timed += stage.reshade_timed;
        period_.reshade_additive += stage.reshade_additive;
        period_.reshade_flagged += stage.reshade_flagged;
        period_.reshade_lone += stage.reshade_lone;
        period_.reshade_lone_flagged += stage.reshade_lone_flagged;
        period_.reshade_lone_failures += stage.reshade_lone_failures;
        period_.reshade_lone_timed += stage.reshade_lone_timed;
        period_.reshade_lone_us_sum += stage.reshade_lone_us;
        for (unsigned i = 0; i < lone_count_slots; ++i) period_.reshade_lone_counts[i] += stage.reshade_lone_counts[i];
        period_.reshade_us_sum += stage.reshade_us;
        if (stage.reshade_us > period_.reshade_us_max) period_.reshade_us_max = stage.reshade_us;
    }
    if (core::reshade_mode(mode_)) {
        period_.muzzle_draws += stage.muzzle_draws;
        period_.muzzle_matched += stage.muzzle_matched;
        period_.muzzle_hull += stage.muzzle_hull;
        period_.muzzle_failures += stage.muzzle_failures;
        period_.muzzle_timed += stage.muzzle_timed;
        period_.muzzle_us_sum += stage.muzzle_us;
        if (stage.muzzle_us > period_.muzzle_us_max) period_.muzzle_us_max = stage.muzzle_us;
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
    period_.lathes += stage.lathes;
    period_.lathe_no_profile += stage.lathe_no_profile;
    period_.lathe_mismatch += stage.lathe_mismatch;
    period_.lathe_unreadable += stage.lathe_unreadable;
    period_.lathe_scale += stage.lathe_scale;
    period_.lathe_degenerate += stage.lathe_degenerate;
    period_.lathe_cache += stage.lathe_cache;
    period_.lathe_stale += stage.lathe_stale;
    period_.lathe_deferred += stage.lathe_deferred;
    period_.lathe_entry_far += stage.lathe_entry_far;
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
        if (core::release_due(e, now, s.life_floor_ms, s.boom_floor_ms)) {
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
    // and the worst frame's sum, the batch's draws only); lone / lone_flagged / lone_failures / lone_us_per_draw: the
    // lone path's re-shaded draws (bullet-draw-batch.md section 7), of them with the far flag, its failed
    // substitutions and its sequence's CPU time per timed draw, in none of the batch's fields (lone_flagged counts both
    // flag paths); the lone counts (weapon_effects_patch.h lone_count_*): lone_flagged_native the flagged lone draws the
    // additive route did not take (ONE / ONE, the game's blend); lone_refused the lone draws that ended without the far
    // flag, lone_refused_why the same per reason (bullet_reshade_route.h LoneUnflagged, in its order); lone_route_state
    // the lone draws the route refused for a known differing state (a defect: the sequence normalises the four it
    // requires), lone_route_unknown those it refused on an unknown state; lone_zwrite .. lone_sepalpha the lone draws
    // drawn re-shaded whose application state the sequence normalised, per state (on the route the four, off it Z write
    // only); lone_additive the re-shaded lone draws under ONE / ONE.
    if (x3m_bullet_reshade_armed) {
        const std::uint32_t scopes = x3m_bullet_scope_calls;
        const std::uint32_t* n = period_.reshade_lone_counts;
        const std::uint32_t* why = n + lone_count_refused_why;
        log("bullet_reshade_frame frame=%llu frames=%lu submits=%lu draws=%lu additive=%lu flagged=%lu lone=%lu "
            "lone_flagged=%lu failures=%lu outside=%lu timed=%lu us_timed=%.1f us_per_draw=%.3f us_frame_max=%.1f "
            "lone_failures=%lu lone_timed=%lu lone_us_per_draw=%.3f lone_flagged_native=%lu lone_refused=%lu "
            "lone_refused_why=off:%lu,scene:%lu,no_depth:%lu,no_variant:%lu,no_target:%lu,r32f_lane:%lu,write_masks:%lu,"
            "bind:%lu,screen_unrouted:%lu,failed:%lu lone_route_state=%lu lone_route_unknown=%lu lone_zwrite=%lu "
            "lone_mask=%lu lone_srgb=%lu lone_sepalpha=%lu lone_additive=%lu",
            frame, static_cast<unsigned long>(period_.frames), static_cast<unsigned long>(scopes - reshade_scopes_seen_),
            static_cast<unsigned long>(period_.reshade_draws), static_cast<unsigned long>(period_.reshade_additive),
            static_cast<unsigned long>(period_.reshade_flagged), static_cast<unsigned long>(period_.reshade_lone),
            static_cast<unsigned long>(period_.reshade_lone_flagged), static_cast<unsigned long>(period_.reshade_failures),
            static_cast<unsigned long>(period_.reshade_outside), static_cast<unsigned long>(period_.reshade_timed),
            double(period_.reshade_us_sum),
            period_.reshade_timed ? double(period_.reshade_us_sum) / double(period_.reshade_timed) : 0.0,
            double(period_.reshade_us_max), static_cast<unsigned long>(period_.reshade_lone_failures),
            static_cast<unsigned long>(period_.reshade_lone_timed),
            period_.reshade_lone_timed ? double(period_.reshade_lone_us_sum) / double(period_.reshade_lone_timed) : 0.0,
            static_cast<unsigned long>(n[lone_count_flagged_native]), static_cast<unsigned long>(n[lone_count_refused]),
            static_cast<unsigned long>(why[0]), static_cast<unsigned long>(why[1]), static_cast<unsigned long>(why[2]),
            static_cast<unsigned long>(why[3]), static_cast<unsigned long>(why[4]), static_cast<unsigned long>(why[5]),
            static_cast<unsigned long>(why[6]), static_cast<unsigned long>(why[7]), static_cast<unsigned long>(why[8]),
            static_cast<unsigned long>(why[9]), static_cast<unsigned long>(n[lone_count_route_state]),
            static_cast<unsigned long>(n[lone_count_route_unknown]),
            static_cast<unsigned long>(n[lone_count_states + 0]), static_cast<unsigned long>(n[lone_count_states + 1]),
            static_cast<unsigned long>(n[lone_count_states + 2]), static_cast<unsigned long>(n[lone_count_states + 3]),
            static_cast<unsigned long>(n[lone_count_additive]));
        reshade_scopes_seen_ = scopes;
    }
    // The muzzle flash re-shade's period row (muzzle-flash-look.md): muzzle draws matched (a listed body id under ONE /
    // ONE), re-shaded, of them kept off the hull-emission route, failed substitutions (left vanilla), and the
    // sequence's CPU time over the timed draws (every 60th frame and capture frames).
    if (core::reshade_mode(mode_))
        log("muzzle_reshade_frame frame=%llu frames=%lu matched=%lu draws=%lu hull=%lu failures=%lu timed=%lu "
            "us_timed=%.1f us_per_draw=%.3f us_frame_max=%.1f",
            frame, static_cast<unsigned long>(period_.frames), static_cast<unsigned long>(period_.muzzle_matched),
            static_cast<unsigned long>(period_.muzzle_draws), static_cast<unsigned long>(period_.muzzle_hull),
            static_cast<unsigned long>(period_.muzzle_failures), static_cast<unsigned long>(period_.muzzle_timed),
            double(period_.muzzle_us_sum),
            period_.muzzle_timed ? double(period_.muzzle_us_sum) / double(period_.muzzle_timed) : 0.0,
            double(period_.muzzle_us_max));
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
    std::uint32_t ships_native = 0; // the period's ship hits forwarded native for want of a usable profile, every verdict
    for (unsigned v = core::profile_none; v < core::profile_verdict_count; ++v)
        ships_native += c.ships_native[v] - seen_.ships_native[v];
    log("weapon_effects_frame frame=%llu mode=%s frames=%lu events=%lu hits=%lu skipped=%lu forwarded=%lu staged=%lu "
        "patches=%u decals=%u retired=%u expired=%lu overflow=%lu evicted_visible=%lu owned=%lu drew=%lu stage_us=%.1f stage_us_max=%.1f "
        "calls=%lu no_projectile=%lu no_node=%lu stale_table=%lu nested=%lu shield_read=%lu shield_unread=%lu "
        "scope_clears=%lu live=%u dropped=%u cells_dropped=%u hits_dropped=%u spills=%u dir_fallback=%lu faces_culled=%u "
        "area_dropped=%u drained=%lu hull_staged=%lu life_floor_ms=%lu table_hull_key_missing=%u"
        " lathes=%u lathe_no_profile=%u lathe_mismatch=%u lathe_unreadable=%u lathe_scale=%u lathe_degenerate=%u"
        " lathe_cache=%u lathe_stale=%u lathe_deferred=%u lathe_entry_far=%u ships_native=%lu ships_native_no_profile=%lu ships_native_stale_table=%lu"
        " ships_native_mismatch=%lu ships_native_unreadable=%lu ships_native_scale=%lu others_native=%lu",
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
        period_.area_dropped, static_cast<unsigned long>(c.drained - seen_.drained),
        static_cast<unsigned long>(period_.hull_staged), static_cast<unsigned long>(s.life_floor_ms),
        unsigned(table_hull_key_missing_), period_.lathes, period_.lathe_no_profile, period_.lathe_mismatch,
        period_.lathe_unreadable, period_.lathe_scale, period_.lathe_degenerate, period_.lathe_cache,
        period_.lathe_stale, period_.lathe_deferred, period_.lathe_entry_far, static_cast<unsigned long>(ships_native),
        static_cast<unsigned long>(c.ships_native[core::profile_none] - seen_.ships_native[core::profile_none]),
        static_cast<unsigned long>(c.ships_native[core::profile_stale] - seen_.ships_native[core::profile_stale]),
        static_cast<unsigned long>(c.ships_native[core::profile_mismatch] - seen_.ships_native[core::profile_mismatch]),
        static_cast<unsigned long>(c.ships_native[core::profile_unreadable] - seen_.ships_native[core::profile_unreadable]),
        static_cast<unsigned long>(c.ships_native[core::profile_scale] - seen_.ships_native[core::profile_scale]),
        static_cast<unsigned long>(c.others_native - seen_.others_native));
    // The TShips rows forwarded native for want of a usable profile (core::NativeRows), across the session: at a frame
    // row, the rows first seen since the last one (row:class:verdict each; class -1 when not read), the first
    // native_row_capacity distinct rows in all, then the distinct rows past them as one more= count (logged when it moved).
    if (s.native_rows.count > native_rows_logged_ || s.native_rows.more != native_more_logged_) {
        char list[core::native_row_capacity * 40] = {};
        std::size_t used = 0;
        for (unsigned i = native_rows_logged_; i < s.native_rows.count && used < sizeof list; ++i) {
            const int w = std::snprintf(list + used, sizeof list - used, "%s%d:%ld:%s", i > native_rows_logged_ ? "," : "",
                                        int(s.native_rows.row[i]), static_cast<long>(s.native_rows.class_id[i]),
                                        core::profile_verdict_name(s.native_rows.verdict[i]));
            if (w < 0) break;
            used += std::size_t(w);
        }
        log("weapon_ship_profile_missing frame=%llu rows=%u listed=%u more=%lu profiles=%u tships_rows=%lu list=%s "
            "(these ships keep the game's shield effect: regenerate weapon_effects.json for this game tree)",
            frame, unsigned(s.native_rows.count) - native_rows_logged_, unsigned(s.native_rows.count),
            static_cast<unsigned long>(s.native_rows.more), s.profiles ? s.profiles->count : 0u,
            static_cast<unsigned long>(s.profiles ? s.profiles->tships_rows : 0u), used ? list : "-");
        native_rows_logged_ = s.native_rows.count;
        native_more_logged_ = s.native_rows.more;
    }
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
