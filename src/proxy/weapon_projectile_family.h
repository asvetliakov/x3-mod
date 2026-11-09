#pragma once
#include "weapon_effects_core.h"

// The projectile stage's families (docs/architecture/projectile-look.md section 1 and "Implementation (2026-11-08)").
// Portable and constexpr: the hook, the stage, the tuning reader, the host tests and both fixtures read it.
//
// A family is (shape, mount tier) as the table generator classifies every TBullets row (weapon_effects.json: shape from
// the body's elongation, mount tier from the ship catalogue; weapon_overrides.json for exceptions), with one Tune (the
// nine bolt and five muzzle-flash numbers) and one compile-time list of enabled TBullets ids. A row is drawn by the stage when its id is in
// its family's list and the table puts it in that family; every other row sharing its body (the batch is per body) is
// drawn with it. Adding a family is a second Family entry with its own list and Tune, not a code path.
//
// THE PLACE A ROW IS ENABLED: the *_rows lists below. Today only SS_BULLET_IRE (and, through its body bullet_ls_alpha,
// rows 37-39, the cluster and alpha flak fragments).
namespace x3m::weapon_effects::projectile_family {
// The numbers of a family's look (projectile-look.md section 4, muzzle-flash-look.md (superseded) section 5; the dev
// tuning file's keys). The streak has no multiplier: its length is the bolt's travel since the previous frame (the
// bridged law, projectile-look.md section 2), so the file's former `stretch_k` key is an unknown key, ignored without a
// row. The comet (FL 60 A): trail_ms and head_gain; trail_ms 0 is the flat streak of FL run 60 (no trail, no head).
struct Tune {
    float core_px;      // core full width (FWHM, px) at the 500 m reference, at the live projection
    float glow_ratio;   // glow sigma over the core radius
    float glow_gain;    // glow radiance in scene white
    float hot_share;    // white share of the core colour (x (1 + calibre))
    float lod_dot_m;    // past it (view depth, metres) no streak: a dot
    float length_cap_m; // past this streak length (metres) the radiance scales by cap / length (energy conserved)
    float stops;        // core radiance 2^(stops + calibre) x scene white
    float trail_ms;     // the trail behind the head covers the bolt's travel over this many game ms (0: no trail)
    float head_gain;    // the head's radiance over the trail's start (with a trail)
    // The muzzle flash (docs/architecture/muzzle-flash-look.md (superseded) section 5; the start values as member
    // defaults, so a Tune written with the nine bolt numbers carries them):
    float flash_core_mult = 1.25f; // the flash point's extent at scene white over the bolt core's at the same depth
    float flash_len_m = 2.5f;      // the streak's world length from the muzzle along the shot's axis (0: no streak)
    float flash_life_ms = 80.f;    // the life in wall-clock ms from the birth (QPC; drawn every stage frame while younger)
    float flash_stops = 2.f;       // the flash's radiance over the bolt core's, in stops
    float hull_pulse_r = 3.f;      // the hull pulse's radius in metres at calibre 0 (x (1 + calibre); 0: no pulse)
    // The bullet re-shade (X3M_WEAPON_EFFECTS=reshade; src/effects/weapon_bullet_reshade_ps.hlsl c31.xyz): the body's
    // gain over the game's radiance, the luminance smoothstep's upper edge (the fringe fades below it), the hot core's
    // gain (its l^4 term).
    // Baked from FL 64 A (accepted 2026-11-09): the user's flown values; the far bolts were judged acceptable in flight
    // (the fixture's 1500 m frame keeps a lower peak than at the earlier rs_edge 0.1, ledger "Bullet re-shade on FL").
    float rs_gain = 0.4f;
    float rs_edge = 0.4f;
    float rs_core = 0.2f;
    // The muzzle flash re-shade (reshade mode; src/effects/weapon_muzzle_reshade_ps.hlsl, muzzle_reshade_core.h;
    // docs/architecture/muzzle-flash-look.md): the glow's radius around the gun over vanilla's lit radius, the hot core's
    // and the glow's luminance at the gun (summed over the fan), the share by which the texel's grain is flattened.
    // Calibrated 2026-11-09 on the real mesh: the core reaches scene white within 0.27 of vanilla's white radius.
    float mz_size = 0.5f;
    float mz_core = 0.5f;
    float mz_gain = 0.2f;
    float mz_smooth = 0.6f;
};
constexpr unsigned tune_keys = 21;
constexpr const char* tune_key_names[tune_keys] = {"core_px",      "glow_ratio",  "glow_gain",       "hot_share",
                                                   "lod_dot_m",    "length_cap_m", "stops",          "trail_ms",
                                                   "head_gain",    "flash_core_mult", "flash_len_m", "flash_life_ms",
                                                   "flash_stops",  "hull_pulse_r", "rs_gain",        "rs_edge",
                                                   "rs_core",      "mz_size",      "mz_core",        "mz_gain",
                                                   "mz_smooth"};
constexpr float tune_min[tune_keys] = {0.5f, 1.f, 0.f, 0.f, 500.f, 5.f, -2.f, 0.f, 1.f, 0.5f, 0.f, 16.f, -2.f, 0.f,
                                       0.f, 0.05f, 0.f, 0.2f, 0.f, 0.f, 0.f};
constexpr float tune_max[tune_keys] = {8.f, 12.f, 2.f, 1.f, 20000.f, 500.f, 6.f, 200.f, 4.f, 8.f, 20.f, 400.f, 6.f, 20.f,
                                       2.f, 1.f, 4.f, 1.f, 4.f, 2.f, 1.f};
constexpr bool key_is(const char* a, const char* b) noexcept { return *a == *b && (!*a || key_is(a + 1, b + 1)); }
constexpr unsigned key_flash_life_ms = 11; // the stage's backdate cap is checked against this key's maximum
static_assert(key_is(tune_key_names[key_flash_life_ms], "flash_life_ms"), "the flash life key's index");
inline float& tune_at(Tune& t, unsigned k) noexcept {
    float* const fields[tune_keys] = {&t.core_px,         &t.glow_ratio,  &t.glow_gain,       &t.hot_share,
                                      &t.lod_dot_m,       &t.length_cap_m, &t.stops,          &t.trail_ms,
                                      &t.head_gain,       &t.flash_core_mult, &t.flash_len_m, &t.flash_life_ms,
                                      &t.flash_stops,     &t.hull_pulse_r, &t.rs_gain,        &t.rs_edge,
                                      &t.rs_core,         &t.mz_size,     &t.mz_core,         &t.mz_gain,
                                      &t.mz_smooth};
    return *fields[k < tune_keys ? k : 0];
}
constexpr float reference_m = 500.f; // the tuning numbers' reference distance, never a constant in the program
struct Family {
    const char* name;
    std::uint8_t shape, mount_tier; // core::Shape, the table's mount tier (0 fighter, 1 corvette, 2 capital)
    const char* const* rows;        // enabled TBullets ids
    unsigned row_count;
    Tune tune; // the starting values (projectile-look.md section 4), baked after the user accepts a flight's numbers
};
// ---- enabled rows, one list per family
constexpr const char* fighter_tracer_rows[] = {"SS_BULLET_IRE"};
constexpr unsigned family_count = 1;
constexpr Family families[family_count] = {
    // Baked from FL 61 A (2026-11-09): the user's accepted dev file values, except core_px 1.0 (the file's 0.5 looked
    // better but too faint over bright nebulas: the user's choice). The comet is off (trail_ms 0: at the user's frame
    // rate, with the core saturating, it made no visible difference); the mechanism stays, head_gain 1.5 for a trail.
    // The muzzle flash at its design start values (2026-11-09); FL 62 A judged the two-step life invisible: the life is
    // now flash_life_ms of wall clock (80), calibrated in flight from the dev file.
    {"fighter_tracer", core::shape_tracer, 0, fighter_tracer_rows, sizeof(fighter_tracer_rows) / sizeof(fighter_tracer_rows[0]),
     {1.f, 2.f, 0.3f, 0.4f, 3000.f, 60.f, 2.f, 0.f, 1.5f, 1.25f, 2.5f, 80.f, 2.f, 3.f}},
};
constexpr unsigned fighter_tracer = 0;
constexpr bool tune_in_bounds(const Tune& t) noexcept {
    const float v[tune_keys] = {t.core_px,         t.glow_ratio,  t.glow_gain,       t.hot_share,   t.lod_dot_m,
                                t.length_cap_m,    t.stops,       t.trail_ms,        t.head_gain,   t.flash_core_mult,
                                t.flash_len_m,     t.flash_life_ms, t.flash_stops, t.hull_pulse_r, t.rs_gain,
                                t.rs_edge,         t.rs_core,     t.mz_size,         t.mz_core,     t.mz_gain,
                                t.mz_smooth};
    for (unsigned k = 0; k < tune_keys; ++k)
        if (!(v[k] >= tune_min[k] && v[k] <= tune_max[k])) return false;
    return true;
}
static_assert(tune_in_bounds(families[fighter_tracer].tune), "the starting values lie inside the tuning bounds");
// The enabled rows of a loaded table, built once at table load: each listed id whose row the table puts in its family,
// then every row sharing that row's body (same body hash). At most max_rows; a listed id without a row, or whose row
// the table classifies into another family, is not enabled (Resolution::refused counts it).
constexpr unsigned max_rows = 64;
struct Resolution {
    unsigned count = 0, refused = 0, shared = 0;
    std::uint16_t row[max_rows]{};
    std::uint8_t family[max_rows]{};
};
inline bool resolution_has(const Resolution& r, unsigned row) noexcept {
    for (unsigned i = 0; i < r.count; ++i)
        if (r.row[i] == row) return true;
    return false;
}
inline Resolution resolve_rows(const core::Table& t) noexcept {
    Resolution r;
    for (unsigned f = 0; f < family_count; ++f)
        for (unsigned k = 0; k < families[f].row_count; ++k) {
            const std::uint32_t id = core::name_hash(families[f].rows[k]);
            unsigned found = core::table_capacity;
            for (unsigned i = 0; i < core::table_capacity && found == core::table_capacity; ++i)
                if ((t.bits[i] & core::row_present) && t.id_hash[i] == id) found = i;
            if (found == core::table_capacity || t.shape[found] != families[f].shape ||
                t.mount_tier[found] != families[f].mount_tier || r.count >= max_rows) {
                ++r.refused;
                continue;
            }
            if (!resolution_has(r, found)) {
                r.row[r.count] = std::uint16_t(found);
                r.family[r.count++] = std::uint8_t(f);
            }
            const std::uint32_t body = t.body_hash[found];
            for (unsigned i = 0; body && i < core::table_capacity && r.count < max_rows; ++i)
                if (i != found && (t.bits[i] & core::row_present) && t.body_hash[i] == body && !resolution_has(r, i)) {
                    r.row[r.count] = std::uint16_t(i);
                    r.family[r.count++] = std::uint8_t(f);
                    ++r.shared;
                }
        }
    return r;
}
} // namespace x3m::weapon_effects::projectile_family
