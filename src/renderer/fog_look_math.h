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
// Far march bins over [12000, cap] (FOG_FAR_BINS; docs/architecture/fog-gpu-cost.md, step B): the accepted 40, the only
// count with programs since the 24-bin variant (X3M_FOG_FAR_BINS) was removed on 2026-09-25.
constexpr unsigned fog_far_bins = 40;
// The march spacing in full pixels (FOG_MARCH_SCALE; docs/architecture/fog-gpu-cost.md, step C): 4 (quarter resolution,
// the separately compiled *_q4 march/repair/composite) is the default since Run 77 C2 (2026-09-24); 2 (half resolution)
// is the opt-out (X3M_FOG_MARCH_SCALE=2) and the fallback of every refusal of 4 (programs, target). No other spacing
// has programs. The march target of a W x H scene is fog_march_extent(W, scale) x fog_march_extent(H, scale).
constexpr unsigned fog_march_scale_half = 2, fog_march_scale_quarter = 4,
                   fog_march_scale_default = fog_march_scale_quarter;
constexpr bool fog_march_scale_valid(unsigned scale) {
    return scale == fog_march_scale_half || scale == fog_march_scale_quarter;
}
constexpr unsigned fog_march_extent(unsigned full, unsigned scale) {
    return (full + scale - 1) / scale;
}
// The accepted L2 values, baked: the X3M_FOG_LOOK_<NAME> overrides (24 scalars, AMBIENT_SUN / AMBIENT_AWAY) were
// removed on 2026-09-26; the ambient hues stay derived from the family chroma (negative = derived).
struct FogLookTuning {
    float coverage = .35f, exponent = 2.f, sigma_scale = 8.f; // rho' = saturate((rho-c)/(1-c))^p: soft zero-slope toe
    float coverage_variation = .12f;                          // c moves by this x the mean of three oblique plane waves
    float warp_cycles_near = 13.f, warp_near = 500.f;         // domain warp octaves: whole cycles per 65536 units,
    float warp_cycles_far = 5.f, warp_far = 1400.f; // amplitude in render units (sum <= 2000: fine window margin)
    float forward_g = .75f, forward_weight = .7f, back_g = -.15f; // w HG(g0) + (1-w) HG(g1)
    float albedo_white = .5f;                                     // albedo = lerp(chroma, white, this)
    float ambient_gain = .35f;                                    // x mean(E_sun/pi)
    float ambient_sun[3] = {-1.f, -1.f, -1.f}, ambient_away[3] = {-1.f, -1.f, -1.f}; // negative: derived from chroma
    float extinction_tint = .6f;                                                     // k = 1 + this x (1 - chroma)
    float scatter_lift = .5f, lift_floor = .5f;       // half-extinction isotropic octave; its shaft floor
    float shadow_floor = .15f;                        // minimum shaft visibility of the sun term
    float sky_cap = 112500.f, taper_start = 65000.f;  // every ray ends at the cap; smoothstep fade from the start
                                                      // (start > cap - 1000: the last quarter of the cap)
    float self_shadow = 3.f, powder = .5f;            // Beer-powder sun-ward self-shadow
    float tap_distance = 3000.f, tap_length = 9000.f; // one sun-ward tap and the path it stands for
    float shadow_jitter = 1.f;                        // offset of the shaft lookup alone (bins) while TAA
                                                      // resolves it; 0 = bin centres (also without a resolve).
};
inline float fog_look_unit(float v) noexcept {
    return v < 0.f ? 0.f : v > 1.f ? 1.f : v;
}
// Rows c25..c35 of the look and the factor on the family sigma (c2.w). `radiance_over_pi` is c8.rgb;
// `phase` is the TAA jitter sequence index; `resolved` says a temporal resolve averages the phases (without one the
// shaft lookup offset would be a static dither and would dither the hard cascade switch: it is dropped, and the
// lookup, cascade selection included, is the bin centre's).
inline float fog_look_constants(const FogLookTuning& t, const float chroma[3], const float radiance_over_pi[3],
                                unsigned phase, float rows[fog_look_rows][4], bool resolved = true) noexcept {
    for (unsigned i = 0; i < fog_look_rows; ++i)
        for (unsigned j = 0; j < 4; ++j) rows[i][j] = 0.f;
    const float mean = (radiance_over_pi[0] + radiance_over_pi[1] + radiance_over_pi[2]) / 3.f;
    rows[0][0] = t.coverage;
    rows[0][1] = 1.f / (1.f - t.coverage);
    rows[0][2] = t.exponent;
    rows[0][3] = t.sky_cap;
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
    rows[1][3] = t.shadow_floor;
    rows[2][3] = .25f * t.scatter_lift;
    rows[3][3] = t.lift_floor;
    const float g[2] = {t.forward_g, t.back_g}, w[2] = {t.forward_weight, 1.f - t.forward_weight};
    for (unsigned i = 0; i < 2; ++i) {
        rows[4 + i][0] = 1.f + g[i] * g[i];
        rows[4 + i][1] = 2.f * g[i];
        rows[4 + i][2] = .25f * w[i] * (1.f - g[i] * g[i]);
    }
    rows[6][0] = t.self_shadow;
    rows[6][1] = t.powder; // rows[6][2..3], the retired L3 sample offset, stay zero
    rows[7][0] = t.tap_distance;
    rows[7][1] = t.tap_length;
    const float shaft = resolved ? t.shadow_jitter : 0.f;
    rows[7][2] = shaft;
    rows[7][3] = shaft;
    // Whole cycles per fine window (65536 units): the warp is world anchored under the camera modulo.
    rows[9][0] = float(int(t.warp_cycles_far + .5f)) / 65536.f;
    rows[9][1] = t.warp_far;
    rows[9][2] = float(int(t.warp_cycles_near + .5f)) / 65536.f;
    rows[9][3] = t.warp_near;
    const float start = t.taper_start <= t.sky_cap - 1000.f ? t.taper_start : .75f * t.sky_cap;
    rows[10][0] = t.coverage_variation / 3.f;
    rows[10][1] = start;
    rows[10][2] = 1.f / (t.sky_cap - start);
    rows[8][3] = 5.588238f * float(phase % 64u);
    return t.sigma_scale;
}
} // namespace x3m::renderer
