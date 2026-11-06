#include "weapon_effects_patch.h"
#include "capture.h"
#include "config.h"
#include "engine_memory.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "sse_scalar.h"
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
constexpr unsigned site_count = 3; // impact, tap1, tap2
bool patched_ = false, installed_ = false;
core::Mode mode_ = core::mode_native;
const char* state_ = "not_initialized";
const char* site_state_[site_count] = {"not_initialized", "not_initialized", "not_initialized"};
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
    unsigned faces_culled = 0, area_dropped = 0;
    float stage_us_max = 0.f, stage_us_sum = 0.f;
} period_{};
core::Counters seen_{};
core::ScopeLatch scope_{};
std::uint32_t scope_clears_ = 0, scope_changes_ = 0;
std::uint32_t seen_overflow_ = 0, seen_retired_ = 0, seen_compared_ = 0;

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
    // The live count is checked per hit (core::impact: a differing *0x00607038 disables the lookups, stale_table).
    x3m::log("weapon_effects_table event=%s reason=%s rows=%lu missile_rows=%lu entries=%u textures=%u overrides=%u "
             "dropped=%u bytes=%lu fault=%lu path=\"%s\"",
             parsed ? "loaded" : had_text ? "rejected" : read, parsed ? "ok" : had_text ? "malformed" : read,
             static_cast<unsigned long>(parsed ? table_->rows : 0), static_cast<unsigned long>(parsed ? table_->missile_rows : 0),
             st.entries, st.textures, st.overrides, st.dropped, static_cast<unsigned long>(size),
             static_cast<unsigned long>(fault), path);
}
const char* site_name(unsigned i) {
    return i == 0 ? "impact" : i == 1 ? "tap1" : "tap2";
}
std::uintptr_t site_va(unsigned i) {
    return i == 0 ? sites::impact_site_va : sites::tap_site_va[i - 1];
}
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
    if (!taps) site_state_[1] = site_state_[2] = "not_used";
    if (mode == core::mode_native) return done("native", false);
    if (!a.impact_window || !a.impact_target || !a.prologue || (taps && (!a.tap_window[0] || !a.tap_window[1] || !a.tap_target))) {
        site_state_[0] = "invalid_site";
        return done("invalid_site", false);
    }
    if (!engine_patch::install_window_open()) {
        for (unsigned i = 0; i < site_count; ++i)
            if (i == 0 || taps) site_state_[i] = "late_claim";
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
    if (!pin_self()) {
        site_state_[0] = "pin_failed";
        return done("pin_failed", false);
    }
    core::HookState& s = hook_state();
    core::ring_clear(s.ring);
    s.table = table_;
    s.mode = std::uint8_t(mode);
    s.armed = mode == core::mode_shields ? core::family_shields : 0;
    s.owned_next = 0;
    s.lifetime_ms = std::uint16_t(core::patch_lifetime_ms);
    s.suppress_native = core::suppress_native_shield_hit; // true: a stage-owned shield hit skips the native spawn
    x3m_weapon_effects_continue_impact = a.impact_target; // published before the redirects go live
    x3m_weapon_effects_continue_tap = a.tap_target;
    for (unsigned i = 0; i < site_count; ++i) site_[i] = engine_patch::CallSite{};
    // Atomic sites first (the taps), then the impact (plain copy); all or none.
    const unsigned order[site_count] = {1, 2, 0};
    unsigned claimed[site_count]{};
    unsigned n = 0;
    for (unsigned k = 0; k < site_count; ++k) {
        const unsigned i = order[k];
        if (i && !taps) continue;
        const std::uintptr_t at = i ? a.tap_window[i - 1] + sites::tap_site_offset : a.impact_window + sites::impact_site_offset;
        void* stub = i ? reinterpret_cast<void*>(&x3m_weapon_effects_stub_tap) : reinterpret_cast<void*>(&x3m_weapon_effects_stub_impact);
        const bool ok = engine_patch::claim_call(site_[i], at, i ? a.tap_target : a.impact_target, stub);
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
            return done(patched_ || !back ? "rollback_failed" : failed, false);
        }
        site_state_[i] = "active";
        claimed[n++] = i;
    }
    patched_ = installed_ = true;
    mode_ = mode;
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
        const Addresses a{sites::impact_window_va, sites::impact_target_va, sites::prologue_va,
                          {sites::tap_window_va[0], sites::tap_window_va[1]}, sites::tap_target_va};
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
    static constexpr unsigned reverse[site_count] = {0, 2, 1}; // the reverse of the install order
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
    if (scope_.changed(scope) && mode_ == core::mode_shields) {
        ++scope_changes_;
        const unsigned cleared = clear_owned_count();
        if (cleared) {
            ++scope_clears_;
            log("weapon_effects_scope frame=%llu cleared=%u loading=%u sector_key=%016llx load_epoch=%llu clears=%lu", frame,
                cleared, unsigned(scope.loading), static_cast<unsigned long long>(scope_.sector_key),
                static_cast<unsigned long long>(scope_.load_epoch), static_cast<unsigned long>(scope_clears_));
        }
    }
    // The next frame's latch (shields only; observe never skips).
    s.owned_next = std::uint8_t(mode_ == core::mode_shields && owned);
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
    period_.stage_us_sum += stage.stage_us;
    if (stage.stage_us > period_.stage_us_max) period_.stage_us_max = stage.stage_us;
    const std::uint32_t now = game_ms();
    core::Ring& r = s.ring;
    for (unsigned i = 0; i < core::ring_capacity; ++i) {
        core::Event& e = r.slots[i];
        if (!e.sequence) continue;
        if (!(e.flags & core::flag_logged)) {
            e.flags |= core::flag_logged;
            ++period_.events;
            if (e.role == core::role_shield_hit) ++period_.hits;
            if (e.flags & core::flag_forwarded)
                ++period_.forwarded;
            else
                ++period_.skipped;
            if (e.flags & core::flag_stage) ++period_.staged;
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
                    "pos=%ld,%ld,%ld pos_r=%.3f dir=%d,%d,%d shield=%.4f shield_read=%u patch_tier=%s tier_source=%s sample=%s",
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
                    unsigned((e.flags & core::flag_shield_read) != 0), core::patch_tier_name(e.patch_tier),
                    core::tier_source_name(e.tier_source), sample);
            }
        }
        // Census-only records leave once logged; the stage's at the end of their game-time life.
        if (!(e.flags & core::flag_stage) || (now && core::expired(now, e))) {
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
    log("weapon_effects_frame frame=%llu mode=%s frames=%lu events=%lu hits=%lu skipped=%lu forwarded=%lu staged=%lu "
        "patches=%u decals=%u retired=%u expired=%lu overflow=%lu owned=%lu drew=%lu stage_us=%.1f stage_us_max=%.1f "
        "calls=%lu no_projectile=%lu no_node=%lu stale_table=%lu nested=%lu shield_read=%lu shield_unread=%lu "
        "scope_clears=%lu live=%u dropped=%u cells_dropped=%u hits_dropped=%u spills=%u dir_fallback=%lu faces_culled=%u "
        "area_dropped=%u",
        frame, core::mode_name(mode_), static_cast<unsigned long>(period_.frames), static_cast<unsigned long>(period_.events),
        static_cast<unsigned long>(period_.hits), static_cast<unsigned long>(period_.skipped),
        static_cast<unsigned long>(period_.forwarded), static_cast<unsigned long>(period_.staged), period_.patches,
        period_.decals,
        period_.retired + unsigned(r.retired - seen_retired_), static_cast<unsigned long>(period_.expired),
        static_cast<unsigned long>(r.overflow - seen_overflow_), static_cast<unsigned long>(period_.owned),
        static_cast<unsigned long>(period_.drew),
        double(period_.drew ? period_.stage_us_sum / float(period_.drew) : 0.f), double(period_.stage_us_max),
        static_cast<unsigned long>(c.calls - seen_.calls), static_cast<unsigned long>(c.no_projectile - seen_.no_projectile),
        static_cast<unsigned long>(c.no_node - seen_.no_node), static_cast<unsigned long>(c.stale_table - seen_.stale_table),
        static_cast<unsigned long>(c.nested - seen_.nested),
        static_cast<unsigned long>(c.shield_read - seen_.shield_read),
        static_cast<unsigned long>(c.shield_unread - seen_.shield_unread), static_cast<unsigned long>(scope_clears_),
        core::ring_capacity - r.free_count, period_.dropped, period_.cells_dropped, period_.hits_dropped,
        period_.spills, static_cast<unsigned long>(c.dir_fallback - seen_.dir_fallback), period_.faces_culled,
        period_.area_dropped);
    seen_ = c;
    seen_overflow_ = r.overflow;
    seen_retired_ = r.retired;
    period_ = {};
    period_events_ = 0;
}
} // namespace x3m::weapon_effects
