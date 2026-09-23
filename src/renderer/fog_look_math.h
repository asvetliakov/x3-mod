#pragma once
// d3d9-free constants of the single stored-density fog look
// (docs/architecture/fog-density-runtime-integration.md, "The look"; law in
// src/fog/fog_density_field_inc.h under FOG_LOOK). The presets L0/L1/L3 were
// retired on 2026-09-22: the former L2 is the only look and there is no selector. The shader fixture uploads these rows
// unchanged and tools/analysis/fog_density_shader_reference.py mirrors them on the host.
// No pow/sqrt/fabs: plain float arithmetic only (SSE2 in the proxy build).
#include <cstddef>
namespace x3m::renderer {
constexpr unsigned fog_look_first_register = 25; // c25..c35
constexpr unsigned fog_look_rows = 11;
// Shaft cascades the look program reads (the two coarsest current maps; the unshaped reference law reads three).
constexpr unsigned fog_look_cascades = 2;
// Far march bins over [12000, cap] (FOG_FAR_BINS; docs/architecture/fog-gpu-cost.md, step B): the accepted 40, or 24 with
// the separately compiled *_look_far24 march/repair (X3M_FOG_FAR_BINS=24). No other count has programs.
constexpr unsigned fog_far_bins_default = 40, fog_far_bins_coarse = 24;
constexpr bool fog_far_bins_valid(unsigned bins) { return bins == fog_far_bins_default || bins == fog_far_bins_coarse; }
// 24 bins is one sample per 4096-unit far node at the 112,500 cap (ds 4187.5); a larger column cap (X3M_FOG_LOOK_SKY_CAP up
// to 200,000: ds 7833, 1.9 nodes) would skip nodes and alias, so the 24-bin variant is refused above this cap.
constexpr float fog_far_bins_coarse_cap_max = 120000.f;
// Every scalar has an environment override X3M_FOG_LOOK_<NAME> read once at init (fog_look_fields).
struct FogLookTuning {
    float coverage = .35f, exponent = 2.f, sigma_scale = 8.f;         // rho' = saturate((rho-c)/(1-c))^p: soft zero-slope toe
    float coverage_variation = .12f;                                  // c moves by this x the mean of three oblique plane waves
    float warp_cycles_near = 13.f, warp_near = 500.f;                 // domain warp octaves: whole cycles per 65536 units,
    float warp_cycles_far = 5.f, warp_far = 1400.f;                   // amplitude in render units (sum <= 2000: fine window margin)
    float forward_g = .75f, forward_weight = .7f, back_g = -.15f;     // w HG(g0) + (1-w) HG(g1)
    float albedo_white = .5f;                                         // albedo = lerp(chroma, white, this)
    float ambient_gain = .35f;                                        // x mean(E_sun/pi)
    float ambient_sun[3] = {-1.f, -1.f, -1.f}, ambient_away[3] = {-1.f, -1.f, -1.f}; // negative: derived from chroma
    float extinction_tint = .6f;                                      // k = 1 + this x (1 - chroma)
    float scatter_lift = .5f, lift_floor = .5f;                       // half-extinction isotropic octave; its shaft floor
    float shadow_floor = .15f;                                        // minimum shaft visibility of the sun term
    float sky_cap = 112500.f, taper_start = 65000.f;                  // every ray ends at the cap; smoothstep fade from the start
                                                                      // (start > cap - 1000: the last quarter of the cap)
    float self_shadow = 3.f, powder = .5f;                            // Beer-powder sun-ward self-shadow
    float tap_distance = 3000.f, tap_length = 9000.f;                 // one sun-ward tap and the path it stands for
    float shadow_jitter = 1.f;                                        // offset of the shaft lookup alone (bins) while TAA
                                                                      // resolves it; 0 = bin centres (also without a resolve).
    // The visibility grid pass (fog_shadow_grid.h, X3M_FOG_SHADOW_PASS=1 only): penumbra kernel radius in texels of
    // the sampled map = clamp(blocker distance x half sun angle x penumbra / texel, min, max); penumbra 0 fixes it at min.
    float penumbra = 1.f, penumbra_min = 1.f, penumbra_max = 16.f;
};
struct FogLookField { const char* name; float FogLookTuning::* field; float minimum, maximum; };
constexpr FogLookField fog_look_fields[] = {
    {"COVERAGE", &FogLookTuning::coverage, 0.f, .9f}, {"EXPONENT", &FogLookTuning::exponent, .25f, 8.f},
    {"SIGMA_SCALE", &FogLookTuning::sigma_scale, .1f, 40.f}, {"FORWARD_G", &FogLookTuning::forward_g, 0.f, .95f},
    {"FORWARD_WEIGHT", &FogLookTuning::forward_weight, 0.f, 1.f}, {"BACK_G", &FogLookTuning::back_g, -.95f, 0.f},
    {"ALBEDO_WHITE", &FogLookTuning::albedo_white, 0.f, 1.f}, {"AMBIENT_GAIN", &FogLookTuning::ambient_gain, 0.f, 4.f},
    {"EXTINCTION_TINT", &FogLookTuning::extinction_tint, 0.f, 4.f}, {"SCATTER_LIFT", &FogLookTuning::scatter_lift, 0.f, 4.f},
    {"LIFT_FLOOR", &FogLookTuning::lift_floor, 0.f, 1.f}, {"SHADOW_FLOOR", &FogLookTuning::shadow_floor, 0.f, 1.f},
    {"SKY_CAP", &FogLookTuning::sky_cap, 20000.f, 200000.f}, {"TAPER_START", &FogLookTuning::taper_start, 0.f, 199000.f},
    {"SELF_SHADOW", &FogLookTuning::self_shadow, 0.f, 40.f},
    {"POWDER", &FogLookTuning::powder, 0.f, 1.f}, {"TAP_DISTANCE", &FogLookTuning::tap_distance, 100.f, 7000.f},
    {"TAP_LENGTH", &FogLookTuning::tap_length, 0.f, 40000.f},
    {"COVERAGE_VARIATION", &FogLookTuning::coverage_variation, 0.f, .3f},
    {"WARP_CYCLES_NEAR", &FogLookTuning::warp_cycles_near, 1.f, 64.f}, {"WARP_NEAR", &FogLookTuning::warp_near, 0.f, 500.f},
    {"WARP_CYCLES_FAR", &FogLookTuning::warp_cycles_far, 1.f, 64.f}, {"WARP_FAR", &FogLookTuning::warp_far, 0.f, 1500.f},
    {"SHADOW_JITTER", &FogLookTuning::shadow_jitter, 0.f, 1.f},
    {"PENUMBRA", &FogLookTuning::penumbra, 0.f, 4.f}, {"PENUMBRA_MIN", &FogLookTuning::penumbra_min, 0.f, 16.f},
    {"PENUMBRA_MAX", &FogLookTuning::penumbra_max, 0.f, 64.f},
};
// A value outside its range (or NaN) keeps the default; true when it was taken.
inline bool fog_look_set(FogLookTuning& tuning, const FogLookField& field, float value) noexcept {
    if (!(value >= field.minimum && value <= field.maximum)) return false;
    tuning.*field.field = value; return true;
}
inline float fog_look_unit(float v) noexcept { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
// Rows c25..c35 of the look and the factor on the family sigma (c2.w). `radiance_over_pi` is c8.rgb;
// `phase` is the TAA jitter sequence index; `resolved` says a temporal resolve averages the phases (without one the
// shaft lookup offset would be a static dither and would dither the hard cascade switch: it is dropped, and the
// lookup, cascade selection included, is the bin centre's).
inline float fog_look_constants(const FogLookTuning& t, const float chroma[3], const float radiance_over_pi[3],
                                unsigned phase, float rows[fog_look_rows][4], bool resolved = true) noexcept {
    for (unsigned i = 0; i < fog_look_rows; ++i) for (unsigned j = 0; j < 4; ++j) rows[i][j] = 0.f;
    const float mean = (radiance_over_pi[0] + radiance_over_pi[1] + radiance_over_pi[2]) / 3.f;
    rows[0][0] = t.coverage; rows[0][1] = 1.f / (1.f - t.coverage); rows[0][2] = t.exponent; rows[0][3] = t.sky_cap;
    const bool sun_given = t.ambient_sun[0] >= 0.f && t.ambient_sun[1] >= 0.f && t.ambient_sun[2] >= 0.f;
    const bool away_given = t.ambient_away[0] >= 0.f && t.ambient_away[1] >= 0.f && t.ambient_away[2] >= 0.f;
    for (unsigned i = 0; i < 3; ++i) {
        const float c = fog_look_unit(chroma[i]), rotated = fog_look_unit(chroma[(i + 2) % 3]); // (b, r, g)
        rows[1][i] = c + (1.f - c) * t.albedo_white;
        // Toward the sun the deep family hue, away from it a hue rotated half way round: neither is the
        // pale sun-lit albedo, so shadowed and anti-sun fog is coloured instead of black.
        rows[3][i] = t.ambient_gain * mean * (sun_given ? t.ambient_sun[i] : c);
        rows[2][i] = t.ambient_gain * mean * (away_given ? t.ambient_away[i] : .3f * (c + rotated));
        rows[8][i] = 1.f + t.extinction_tint * (1.f - c);
    }
    rows[1][3] = t.shadow_floor; rows[2][3] = .25f * t.scatter_lift; rows[3][3] = t.lift_floor;
    const float g[2] = {t.forward_g, t.back_g}, w[2] = {t.forward_weight, 1.f - t.forward_weight};
    for (unsigned i = 0; i < 2; ++i) { rows[4 + i][0] = 1.f + g[i] * g[i]; rows[4 + i][1] = 2.f * g[i]; rows[4 + i][2] = .25f * w[i] * (1.f - g[i] * g[i]); }
    rows[6][0] = t.self_shadow; rows[6][1] = t.powder; // rows[6][2..3], the retired L3 sample offset, stay zero
    rows[7][0] = t.tap_distance; rows[7][1] = t.tap_length;
    const float shaft = resolved ? t.shadow_jitter : 0.f;
    rows[7][2] = shaft; rows[7][3] = shaft;
    // Whole cycles per fine window (65536 units): the warp is world anchored under the camera modulo.
    rows[9][0] = float(int(t.warp_cycles_far + .5f)) / 65536.f; rows[9][1] = t.warp_far;
    rows[9][2] = float(int(t.warp_cycles_near + .5f)) / 65536.f; rows[9][3] = t.warp_near;
    const float start = t.taper_start <= t.sky_cap - 1000.f ? t.taper_start : .75f * t.sky_cap;
    rows[10][0] = t.coverage_variation / 3.f; rows[10][1] = start; rows[10][2] = 1.f / (t.sky_cap - start);
    rows[8][3] = 5.588238f * float(phase % 64u);
    return t.sigma_scale;
}
} // namespace x3m::renderer
