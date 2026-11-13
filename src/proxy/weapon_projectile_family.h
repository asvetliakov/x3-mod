#pragma once
#include "weapon_effects_core.h"

// The re-shades' look numbers (the bullet re-shade, docs/reverse-engineering/fl/bullet-draw-batch.md section 6 and
// docs/architecture/projectile-look.md; the muzzle flash re-shade, docs/architecture/muzzle-flash-look.md) and the dev
// tuning file's keys. Portable and constexpr: the re-shade cores, the tuning reader, the host tests and both fixtures
// read it. The custom projectile stage's families, enabled rows and bolt and flash numbers were removed on 2026-11-13
// (docs/archive/effects-look-2026-11-13-custom-projectile/index.md); their former keys in the dev file are unknown keys.
namespace x3m::weapon_effects::projectile_family {
struct Tune {
    // The bullet re-shade (X3M_WEAPON_EFFECTS=reshade; src/effects/weapon_bullet_reshade_ps.hlsl c31.xyz): the body's
    // gain over the game's radiance, the luminance smoothstep's upper edge (the fringe fades below it), the hot core's
    // gain (its l^4 term).
    // Baked from FL 64 A (accepted 2026-11-09, 0.4/0.4/0.2) and re-baked from FL 68 A (2026-11-12): the user's flown
    // values on fighters and capitals, judged in flight; rs_edge 0.4 is what removes the halo bulb, rs_core 0.5 keeps a
    // slightly whiter centre below the level where a near bolt turns into a hard white dot (the user saw no difference
    // between 0.2 and 1.5: the l^4 term lives in the clipped centre). The far bolts were judged acceptable in flight (the
    // fixture's 1500 m frame keeps a lower peak than at the earlier rs_edge 0.1, ledger "Bullet re-shade on FL").
    float rs_gain = 0.75f;
    float rs_edge = 0.4f;
    float rs_core = 0.5f;
    // The muzzle flash re-shade (reshade mode; src/effects/weapon_muzzle_reshade_ps.hlsl, muzzle_reshade_core.h;
    // docs/architecture/muzzle-flash-look.md): the glow's radius around the gun over vanilla's lit radius, the hot core's
    // and the glow's luminance at the gun (summed over the fan), the share by which the texel's grain is flattened.
    // Baked from FL 65 A (accepted 2026-11-09): no white core, a glow at 0.15 within a fifth of vanilla's lit radius,
    // the grain nearly kept (the ledger's "Muzzle flash baked and bullet geometric size on FL (2026-11-10)"). mz_size is
    // the 0.2 the flight drew: the run's file asked for 0.1 under the earlier 0.2 floor and the reader clamped it; the
    // floor is now 0.05, so a file's 0.1 is drawn as written.
    float mz_size = 0.2f;
    float mz_core = 0.f;
    float mz_gain = 0.15f;
    float mz_smooth = 0.1f;
    // The bullet re-shade's geometric size (src/effects/weapon_bullet_reshade.hlsl c29..c30; tables.bullet_bodies, the
    // body's UV layout): the head glow's lit radius and the fins' lit half-width as shares of the body's (1: unchanged).
    // Global across bodies (one value set, the user's decision 2026-11-10).
    float rs_head = 1.f;
    float rs_width = 1.f;
    // The hull hit re-shade (reshade mode; src/effects/weapon_hull_reshade.hlsl, hull_reshade_core.h;
    // docs/architecture/hull-hit-look.md): the body term's gain over the game's radiance, the luminance smoothstep's
    // upper edge, the hot core's gain (l^4) and how far a fading element reddens by the end of its life (0: fade only).
    // The first flown step: the bullets' edge and the design's cooling, gain and core raised to 1.0 / 1.0 from the
    // bullets' 0.75 / 0.5 after the hull dump's sweep (2026-11-13: at 0.75 / 0.5 the PPC mesh had no hot centre at half
    // life and the IRE haze read brown; 1.25 / 1.5 filled the mesh and the glow into flat white). Not yet flown.
    float hh_gain = 1.f;
    float hh_edge = 0.4f;
    float hh_core = 1.f;
    float hh_cool = 0.3f;
};
constexpr Tune baked{}; // the accepted values, the dev file's starting point on every reload
constexpr unsigned tune_keys = 13;
enum Key : unsigned {
    key_rs_gain = 0, key_rs_edge, key_rs_core, key_mz_size, key_mz_core, key_mz_gain, key_mz_smooth, key_rs_head,
    key_rs_width, key_hh_gain, key_hh_edge, key_hh_core, key_hh_cool
};
constexpr const char* tune_key_names[tune_keys] = {"rs_gain", "rs_edge",   "rs_core", "mz_size",  "mz_core",
                                                   "mz_gain", "mz_smooth", "rs_head", "rs_width", "hh_gain",
                                                   "hh_edge", "hh_core",   "hh_cool"};
constexpr float tune_min[tune_keys] = {0.f, 0.05f, 0.f, 0.05f, 0.f, 0.f, 0.f, 0.2f, 0.2f, 0.f, 0.05f, 0.f, 0.f};
constexpr float tune_max[tune_keys] = {2.f, 1.f, 4.f, 1.f, 4.f, 2.f, 1.f, 1.f, 1.f, 2.f, 1.f, 4.f, 1.f};
constexpr bool key_is(const char* a, const char* b) noexcept { return *a == *b && (!*a || key_is(a + 1, b + 1)); }
static_assert(key_is(tune_key_names[key_rs_gain], "rs_gain") && key_is(tune_key_names[key_rs_edge], "rs_edge") &&
                  key_is(tune_key_names[key_rs_core], "rs_core") && key_is(tune_key_names[key_mz_size], "mz_size") &&
                  key_is(tune_key_names[key_mz_core], "mz_core") && key_is(tune_key_names[key_mz_gain], "mz_gain") &&
                  key_is(tune_key_names[key_mz_smooth], "mz_smooth") && key_is(tune_key_names[key_rs_head], "rs_head") &&
                  key_is(tune_key_names[key_rs_width], "rs_width") && key_is(tune_key_names[key_hh_gain], "hh_gain") &&
                  key_is(tune_key_names[key_hh_edge], "hh_edge") && key_is(tune_key_names[key_hh_core], "hh_core") &&
                  key_is(tune_key_names[key_hh_cool], "hh_cool"),
              "the key indices follow the names");
inline float& tune_at(Tune& t, unsigned k) noexcept {
    float* const fields[tune_keys] = {&t.rs_gain, &t.rs_edge,   &t.rs_core, &t.mz_size,  &t.mz_core,
                                      &t.mz_gain, &t.mz_smooth, &t.rs_head, &t.rs_width, &t.hh_gain,
                                      &t.hh_edge, &t.hh_core,   &t.hh_cool};
    return *fields[k < tune_keys ? k : 0];
}
// A key's value clamped to its bounds (the re-shades' constants); an index past tune_keys returns v unchanged.
inline float clamp_key(float v, unsigned k) noexcept {
    if (k >= tune_keys) return v;
    return !(v >= tune_min[k]) ? tune_min[k] : v > tune_max[k] ? tune_max[k] : v;
}
constexpr bool tune_in_bounds(const Tune& t) noexcept {
    const float v[tune_keys] = {t.rs_gain, t.rs_edge,   t.rs_core, t.mz_size,  t.mz_core, t.mz_gain, t.mz_smooth,
                                t.rs_head, t.rs_width,  t.hh_gain, t.hh_edge,  t.hh_core, t.hh_cool};
    for (unsigned k = 0; k < tune_keys; ++k)
        if (!(v[k] >= tune_min[k] && v[k] <= tune_max[k])) return false;
    return true;
}
static_assert(tune_in_bounds(baked), "the baked values lie inside the tuning bounds");
} // namespace x3m::weapon_effects::projectile_family
