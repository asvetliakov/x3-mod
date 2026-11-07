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
constexpr unsigned site_count = 8; // impact, tap1, tap2; step 4: bolt_tap, muzzle, flight; step 3: death_area, death_other
bool patched_ = false, installed_ = false;
core::Mode mode_ = core::mode_native;
const char* state_ = "not_initialized";
const char* site_state_[site_count] = {"not_initialized", "not_initialized", "not_initialized", "not_initialized",
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
    unsigned hull_hits = 0, sparks = 0, glows = 0, embers = 0, flashes = 0, budget_shed = 0, orphans = 0, floored = 0;
    unsigned owners_dropped = 0, capped = 0, bursts = 0, rings = 0, bursts_shed = 0, burst_lod = 0;
    unsigned streaks = 0, chips = 0, filaments = 0, scorches = 0, spots = 0, merges = 0, shimmer = 0, jet_no_dir = 0;
    std::uint32_t hull_staged = 0;
    float stage_us_max = 0.f, stage_us_sum = 0.f;
    // step 3: death records seen (ours, census), the stage's death figures
    std::uint32_t deaths = 0, deaths_ours = 0;
    unsigned boom_drawn = 0, boom_bodies = 0, boom_stars = 0, boom_shocks = 0, boom_embers = 0, boom_fragments = 0;
    unsigned boom_smokes = 0, boom_glows = 0, boom_shed = 0, boom_lod = 0, boom_out_of_reach = 0, boom_capped = 0;
} period_{};
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
             "dropped=%u hull_keys=%u table_hull_key_missing=%u bytes=%lu fault=%lu path=\"%s\"",
             parsed ? "loaded" : had_text ? "rejected" : read, parsed ? "ok" : had_text ? "malformed" : read,
             static_cast<unsigned long>(parsed ? table_->rows : 0), static_cast<unsigned long>(parsed ? table_->missile_rows : 0),
             st.entries, st.textures, st.overrides, st.dropped, st.hull_keys, unsigned(table_hull_key_missing_),
             static_cast<unsigned long>(size), static_cast<unsigned long>(fault), path);
}
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
// The bolts family's period figures (present_bolts): frames drawn in place, bolts and muzzle elements, suppressed draws.
struct BoltPeriod {
    std::uint32_t frames = 0, drew = 0, bolts = 0, elements = 0, suppressed = 0, culled = 0, beams = 0, parity = 0;
    std::uint32_t muzzles = 0, glows = 0, puffs = 0, rims = 0, refused = 0;
    std::uint32_t armed = 0, taken = 0, lost_early = 0, muzzle_native = 0, muzzle_endon = 0, muzzle_starved = 0;
    std::uint32_t muzzle_owned = 0;
    float build_us_sum = 0.f, build_us_max = 0.f;
} bolt_period_{};
x3m::weapon_effects::core::BoltCounters bolt_seen_{};
} // namespace

namespace x3m::weapon_effects {
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
    s.armed = mode == core::mode_shields ? core::family_shields
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
    bool applied = false, parsed = true;
    const char* reason = nullptr;
    if (length >= core::setting_capacity) {
        reason = "too_long";
        parsed = false;
    } else if (length && !core::parse_mode(text, length, &mode)) {
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
    for (unsigned i = 0; i < site_count; ++i)
        log("weapon_effects_patch site=%s va=%08lx state=%s reason=%s mode=%s setting=%s write=%s", site_name(i),
            static_cast<unsigned long>(site_va(i)), site_state_[i], state_, parsed ? core::mode_name(mode) : "-", setting,
            write_path(i));
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    installed_ = false;
    hook_state().mode = core::mode_native; // a thread still inside a stub forwards
    bolt_hook_state().armed = 0;
    bolt_hook_state().owned = 0;
    bolt_hook_state().muzzle_owned = 0;
    static constexpr unsigned reverse[site_count] = {0, 6, 3, 7, 5, 4, 2, 1}; // the reverse of the install order
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
    // The next frame's latch (the stage's modes only; observe never skips); the hull family's also needs the pass's
    // hull programs (StageFrame::hull_ready).
    s.owned_next = std::uint8_t(core::stage_mode(mode_) && owned);
    s.hull_ready = std::uint8_t(core::hull_mode(mode_) && owned && stage.hull_ready);
    // Step 3: the death family's also needs the death programs (StageFrame::boom_ready: the shock beside the hull's).
    s.boom_ready = std::uint8_t(core::explosions_mode(mode_) && owned && stage.hull_ready && stage.boom_ready);
    period_.boom_drawn += stage.deaths;
    period_.boom_bodies += stage.boom_bodies;
    period_.boom_stars += stage.boom_stars;
    period_.boom_shocks += stage.boom_shocks;
    period_.boom_embers += stage.boom_embers;
    period_.boom_fragments += stage.boom_fragments;
    period_.boom_smokes += stage.boom_smokes;
    period_.boom_glows += stage.boom_glows;
    period_.boom_shed += stage.boom_shed;
    period_.boom_lod += stage.boom_lod;
    period_.boom_out_of_reach += stage.boom_out_of_reach;
    period_.boom_capped += stage.boom_capped;
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
    period_.hull_hits += stage.hull_hits;
    period_.sparks += stage.sparks;
    period_.glows += stage.glows;
    period_.embers += stage.embers;
    period_.flashes += stage.flashes;
    period_.budget_shed += stage.budget_shed;
    period_.orphans += stage.orphans;
    period_.floored += stage.floored;
    period_.owners_dropped += stage.owners_dropped;
    period_.capped += stage.capped;
    period_.bursts += stage.bursts;
    period_.rings += stage.rings;
    period_.bursts_shed += stage.bursts_shed;
    period_.burst_lod += stage.burst_lod;
    period_.streaks += stage.hull_streaks;
    period_.chips += stage.hull_chips;
    period_.filaments += stage.hull_filaments;
    period_.scorches += stage.hull_scorches;
    period_.spots += stage.hull_spots;
    period_.merges += stage.hull_merges;
    period_.shimmer += stage.hull_shimmer;
    period_.jet_no_dir += stage.hull_dir_fallback;
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
                    "tier_source=%s owner=%08lx life_ms=%u decision=%s sample=%s",
                    frame, static_cast<unsigned long>(e.sequence), core::boom::tier_name(e.style), unsigned(e.table),
                    unsigned(e.row == 0xffffu ? 0xffffu : e.row), double(float(e.radius) * m),
                    unsigned((e.flags & core::flag_stage) != 0), e.tint_source ? "other" : "area", unsigned(e.shield_q),
                    static_cast<unsigned long>(std::uint32_t(e.damage)), unsigned((e.flags & core::flag_nested) != 0),
                    static_cast<unsigned long>(e.t0_game_ms), double(float(e.pos[0]) * m), double(float(e.pos[1]) * m),
                    double(float(e.pos[2]) * m), static_cast<long>(std::int32_t(e.tint_rgb)),
                    core::patch_tier_name(e.patch_tier), core::tier_source_name(e.tier_source),
                    static_cast<unsigned long>(e.owner), unsigned(e.lifetime_ms),
                    core::boom::decide(e.style) == core::boom::decide_ours ? "ours" : "native", sample);
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
    if (core::explosions_mode(mode_))
        log("weapon_boom_frame frame=%llu deaths=%lu ours=%lu calls=%lu no_node=%lu bullet=%lu rule_b=%lu nested=%lu "
            "fighter=%lu corvette=%lu capital=%lu station=%lu missile=%lu asteroid=%lu other=%lu boom_ready=%u drawn=%u "
            "bodies=%u stars=%u shocks=%u embers=%u fragments=%u smokes=%u glows=%u shed=%u lod=%u out_of_reach=%u "
            "capped=%u floor_ms=%lu tier_native=%u",
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
            static_cast<unsigned long>(c.death_tier[6] - seen_.death_tier[6]), unsigned(s.boom_ready), period_.boom_drawn,
            period_.boom_bodies, period_.boom_stars, period_.boom_shocks, period_.boom_embers, period_.boom_fragments,
            period_.boom_smokes, period_.boom_glows, period_.boom_shed, period_.boom_lod, period_.boom_out_of_reach,
            period_.boom_capped, static_cast<unsigned long>(s.boom_floor_ms), unsigned(core::boom::tier_native));
    log("weapon_effects_frame frame=%llu mode=%s frames=%lu events=%lu hits=%lu skipped=%lu forwarded=%lu staged=%lu "
        "patches=%u decals=%u retired=%u expired=%lu overflow=%lu evicted_visible=%lu owned=%lu drew=%lu stage_us=%.1f stage_us_max=%.1f "
        "calls=%lu no_projectile=%lu no_node=%lu stale_table=%lu nested=%lu shield_read=%lu shield_unread=%lu "
        "scope_clears=%lu live=%u dropped=%u cells_dropped=%u hits_dropped=%u spills=%u dir_fallback=%lu faces_culled=%u "
        "area_dropped=%u drained=%lu bubbles=%u bubble_fallback=%u bubble_walks=%u bubble_walk_failures=%u "
        "bubble_held=%u bubble_deferred=%u bubble_clamped=%u hull_staged=%lu hull_hits=%u flashes=%u sparks=%u glows=%u "
        "embers=%u budget_shed=%u orphans=%u floored=%u owners_dropped=%u capped=%u life_floor_ms=%lu "
        "table_hull_key_missing=%u bursts=%u rings=%u bursts_shed=%u burst_lod=%u streaks=%u chips=%u filaments=%u "
        "scorches=%u spots=%u merges=%u shimmer=%u jet_no_dir=%u",
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
        period_.bubble_clamped, static_cast<unsigned long>(period_.hull_staged), period_.hull_hits, period_.flashes,
        period_.sparks, period_.glows, period_.embers, period_.budget_shed, period_.orphans, period_.floored,
        period_.owners_dropped, period_.capped, static_cast<unsigned long>(s.life_floor_ms),
        unsigned(table_hull_key_missing_), period_.bursts, period_.rings, period_.bursts_shed, period_.burst_lod,
        period_.streaks, period_.chips, period_.filaments, period_.scorches, period_.spots, period_.merges, period_.shimmer,
        period_.jet_no_dir);
    seen_ = c;
    seen_overflow_ = r.overflow;
    seen_evicted_visible_ = r.evicted_visible;
    seen_retired_ = r.retired;
    period_ = {};
    period_events_ = 0;
    period_deaths_ = 0;
}
// Step 4: the bolt latch for the next frame, the generation advance and the weapon_bolts_frame row (every 300 frames,
// and on a capture frame) with the in-place draw's period figures and the hooks' counters since the last row.
void present_bolts(unsigned long long frame, bool capture, const BoltFrame& f) {
    if (!installed_ || !core::bolts_mode(mode_)) return;
    core::BoltHookState& b = bolt_hook_state();
    b.owned = std::uint8_t(f.drew);
    b.muzzle_owned = std::uint8_t(f.muzzle_owned); // the muzzle's own latch (the hook marks its shots ours by it)
    const std::uint32_t passes = b.pass + 1u, listed = core::bolt_list_count(b, core::bolt_list_current(b));
    ++b.generation; // the next frame's list starts empty (the tap resets a list of an older generation)
    BoltPeriod& p = bolt_period_;
    ++p.frames;
    p.drew += f.drew;
    p.bolts += f.bolts;
    p.elements += f.muzzle_elements;
    p.suppressed += f.suppressed;
    p.culled += f.culled;
    p.beams += f.beams;
    p.parity += f.parity_mismatch;
    p.muzzles += f.muzzles;
    p.glows += f.muzzle_glows;
    p.puffs += f.muzzle_puffs;
    p.rims += f.rims;
    p.refused += f.refused;
    p.armed += f.armed;
    p.taken += f.taken;
    p.lost_early += f.lost_early;
    p.muzzle_native += f.muzzle_native;
    p.muzzle_endon += f.muzzle_endon;
    p.muzzle_starved += f.muzzle_starved;
    p.muzzle_owned += f.muzzle_owned;
    if (f.drew) {
        p.build_us_sum += f.build_us;
        if (f.build_us > p.build_us_max) p.build_us_max = f.build_us;
    }
    if (!capture && frame % frame_row_period) return;
    const core::BoltCounters& c = b.counters;
    log("weapon_bolts_frame frame=%llu frames=%lu drew=%lu bolts=%lu muzzle_elements=%lu suppressed=%lu culled=%lu "
        "beams=%lu parity_mismatch=%lu muzzles=%lu muzzle_glows=%lu muzzle_puffs=%lu rims=%lu refused=%lu "
        "build_us=%.1f build_us_max=%.1f listed=%lu passes=%lu tap_calls=%lu tap_bullets=%lu tap_no_node=%lu "
        "tap_overflow=%lu tap_replaced=%lu tap_refreshed=%lu flight_calls=%lu flight_skipped=%lu muzzle_calls=%lu "
        "muzzle_skipped=%lu muzzle_relit=%lu muzzle_evicted=%lu owned=%u capture=%u reason=%s armed=%lu taken=%lu "
        "lost_early=%lu muzzle_owned=%lu muzzle_native=%lu muzzle_endon=%lu muzzle_starved=%lu muzzle_restarted=%lu "
        "shooter_ship=%lu shooter_dock=%lu shooter_factory=%lu shooter_other=%lu shooter_other_class=%u "
        "tap_tombstoned=%lu tap_unkeyed=%lu",
        frame, static_cast<unsigned long>(p.frames), static_cast<unsigned long>(p.drew), static_cast<unsigned long>(p.bolts),
        static_cast<unsigned long>(p.elements), static_cast<unsigned long>(p.suppressed), static_cast<unsigned long>(p.culled),
        static_cast<unsigned long>(p.beams), static_cast<unsigned long>(p.parity), static_cast<unsigned long>(p.muzzles),
        static_cast<unsigned long>(p.glows), static_cast<unsigned long>(p.puffs), static_cast<unsigned long>(p.rims),
        static_cast<unsigned long>(p.refused), double(p.drew ? p.build_us_sum / float(p.drew) : 0.f),
        double(p.build_us_max), static_cast<unsigned long>(listed), static_cast<unsigned long>(passes),
        static_cast<unsigned long>(c.tap_calls - bolt_seen_.tap_calls),
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
        static_cast<unsigned long>(c.muzzle_evicted - bolt_seen_.muzzle_evicted), unsigned(b.owned), unsigned(capture),
        f.reason ? f.reason : "none", static_cast<unsigned long>(p.armed), static_cast<unsigned long>(p.taken),
        static_cast<unsigned long>(p.lost_early), static_cast<unsigned long>(p.muzzle_owned),
        static_cast<unsigned long>(p.muzzle_native), static_cast<unsigned long>(p.muzzle_endon),
        static_cast<unsigned long>(p.muzzle_starved),
        static_cast<unsigned long>(c.muzzle_restarted - bolt_seen_.muzzle_restarted),
        static_cast<unsigned long>(c.shooter_ship - bolt_seen_.shooter_ship),
        static_cast<unsigned long>(c.shooter_dock - bolt_seen_.shooter_dock),
        static_cast<unsigned long>(c.shooter_factory - bolt_seen_.shooter_factory),
        static_cast<unsigned long>(c.shooter_other - bolt_seen_.shooter_other), unsigned(b.shooter_last_other),
        static_cast<unsigned long>(c.tap_tombstoned - bolt_seen_.tap_tombstoned),
        static_cast<unsigned long>(c.tap_unkeyed - bolt_seen_.tap_unkeyed));
    if (capture && frame % frame_row_period) return; // a capture frame's row does not end the period
    bolt_seen_ = c;
    bolt_period_ = {};
}
} // namespace x3m::weapon_effects
