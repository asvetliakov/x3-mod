#pragma once
#include "weapon_effects_core.h"

// The projectile stage's families (docs/architecture/projectile-look.md section 1 and "Implementation (2026-11-08)").
// Portable and constexpr: the hook, the stage, the tuning reader, the host tests and both fixtures read it.
//
// A family is (shape, mount tier) as the table generator classifies every TBullets row (weapon_effects.json: shape from
// the body's elongation, mount tier from the ship catalogue; weapon_overrides.json for exceptions), with one Tune (the
// eight look numbers) and one compile-time list of enabled TBullets ids. A row is drawn by the stage when its id is in
// its family's list and the table puts it in that family; every other row sharing its body (the batch is per body) is
// drawn with it. Adding a family is a second Family entry with its own list and Tune, not a code path.
//
// THE PLACE A ROW IS ENABLED: the *_rows lists below. Today only SS_BULLET_IRE (and, through its body bullet_ls_alpha,
// rows 37-39, the cluster and alpha flak fragments).
namespace x3m::weapon_effects::projectile_family {
// The eight numbers of a family's look (projectile-look.md section 4; the dev tuning file's keys).
struct Tune {
    float core_px;      // core full width (FWHM, px) at the 500 m reference, at the live projection
    float glow_ratio;   // glow sigma over the core radius
    float glow_gain;    // glow radiance in scene white
    float stretch_k;    // stretch = k x |v| x the game-time frame step
    float hot_share;    // white share of the core colour (x (1 + calibre))
    float lod_dot_m;    // past it (view depth, metres) no stretch: a dot
    float length_cap_m; // the stretch's cap (metres)
    float stops;        // core radiance 2^(stops + calibre) x scene white
};
constexpr unsigned tune_keys = 8;
constexpr const char* tune_key_names[tune_keys] = {"core_px", "glow_ratio", "glow_gain", "stretch_k",
                                                   "hot_share", "lod_dot_m", "length_cap_m", "stops"};
constexpr float tune_min[tune_keys] = {0.5f, 1.f, 0.f, 0.f, 0.f, 500.f, 5.f, -2.f};
constexpr float tune_max[tune_keys] = {8.f, 12.f, 2.f, 3.f, 1.f, 20000.f, 500.f, 6.f};
inline float& tune_at(Tune& t, unsigned k) noexcept {
    float* const fields[tune_keys] = {&t.core_px, &t.glow_ratio, &t.glow_gain, &t.stretch_k,
                                      &t.hot_share, &t.lod_dot_m, &t.length_cap_m, &t.stops};
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
    // stretch_k 0.4 and length_cap_m 30 (coordinator, 2026-11-08, from the fixture's look frames: 1.0 / 116 drew a 48 px
    // dash at 500 m at 1x and full-cap beams under SETA 6, the bulk the user rejected)
    {"fighter_tracer", core::shape_tracer, 0, fighter_tracer_rows, 1, {2.f, 3.f, 0.2f, 0.4f, 0.3f, 3000.f, 30.f, 2.f}},
};
constexpr unsigned fighter_tracer = 0;
constexpr bool tune_in_bounds(const Tune& t) noexcept {
    const float v[tune_keys] = {t.core_px, t.glow_ratio, t.glow_gain, t.stretch_k,
                                t.hot_share, t.lod_dot_m, t.length_cap_m, t.stops};
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
