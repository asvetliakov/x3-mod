#pragma once
// Per-point transmittance of the stored-density fog look, for overlays drawn after the fog composite (the engine plumes
// and ribbons: docs/architecture/engine-effects-modern.md section 4; the look: docs/architecture/
// fog-density-runtime-integration.md, "The look"). The composite multiplies the scene by the march's own T along each
// pixel's ray; an overlay drawn after it at a point d render units from the camera gets the column's mean instead:
//
//   T_rgb(d) = exp(-k_rgb x extinction x D(d))
//   D(d)     = the integral of the column weight over [0, min(d, cap)]: 1 to `start`, a smoothstep fade to 0 at `cap`
//            = min(d, start) + (cap - start) (u - u^3 + u^4 / 2),  u = clamp((d - start) / (cap - start), 0, 1)
//   extinction = family sigma x density_scale x ready_far x sigma_scale (the march's c2.w, fog_pass.cpp) x rho_mean
//   rho_mean = the volume mean of the look's shaped density saturate((rho - c - dc) / (1 - c))^p, linear in the
//              family's occupancy through the two measured points (0.12 -> 0.05613, 0.24 -> 0.10980; verification/
//              results/engine-effects/phase3_fog_mean_density.py: the baked shared field, c .35, p 2, dc -.12 / 0 / +.12)
//   k_rgb    = 1 + extinction_tint x (1 - chroma): the composite's tinted extinction T^k
//
// The law is a mean: inside a cloud an overlay is attenuated less than the hull behind it, in a gap more. Off (T = 1)
// whenever the frame's density composite did not apply. d3d9-free and x87-free (SSE scalars; no float returned by
// value from a function that may stay out of line), compiled by the host tests.
#include <cstdint>
#include <cstring>
namespace x3m::renderer {
// The look's column (FogLookTuning sky_cap / taper_start; a start later than cap - 1000 falls back to the last quarter
// of the cap, as fog_look_constants does).
constexpr float fog_transmittance_cap = 112500.f, fog_transmittance_start = 65000.f;
constexpr float fog_mean_density_low_occupancy = .12f, fog_mean_density_low = .05613f;
constexpr float fog_mean_density_high_occupancy = .24f, fog_mean_density_high = .10980f;
struct FogTransmittanceLaw {
    bool on = false;
    float extinction = 0.f;            // mean extinction per render unit (sigma_eff x rho_mean)
    float start = fog_transmittance_start, cap = fog_transmittance_cap;
    float k[3] = {1.f, 1.f, 1.f};      // per-channel exponent of the tinted extinction
};
// exp(-x) for x >= 0 (0 past 87, 1 for x <= 0 or NaN): 2^-n by the exponent bits, e^-f (f in [0, ln 2)) by a degree-7
// series; relative error under 1e-5 on [0, 87] (the host test measures it).
inline void fog_exp_negative(float x, float* out) noexcept {
    if (!(x > 0.f)) {
        *out = 1.f;
        return;
    }
    if (x > 87.f) {
        *out = 0.f;
        return;
    }
    const float y = x * 1.44269504f; // log2(e)
    const int n = int(y);            // truncation: y >= 0
    const float f = (y - float(n)) * .693147181f; // in [0, ln 2)
    // e^-f
    float p = 1.f - f * (1.f - f * (.5f - f * (1.f / 6.f - f * (1.f / 24.f - f * (1.f / 120.f - f * (1.f / 720.f - f * (1.f / 5040.f)))))));
    const std::uint32_t bits = std::uint32_t(127 - n) << 23; // 2^-n, n <= 125
    float scale;
    std::memcpy(&scale, &bits, sizeof scale);
    *out = p * scale;
}
// rho_mean of a family's occupancy (the linear fit through the two measured points, never negative).
inline void fog_mean_density(float occupancy, float* out) noexcept {
    const float slope = (fog_mean_density_high - fog_mean_density_low) /
                        (fog_mean_density_high_occupancy - fog_mean_density_low_occupancy);
    const float v = fog_mean_density_low + slope * (occupancy - fog_mean_density_low_occupancy);
    *out = v > 0.f ? v : 0.f;
}
// The occupancy of a compiled family (tools/fog_field_recipe.py PROFILES; test_engine_ribbons cross-checks the table):
// foggreenoutlands (2) 0.24, the other thirteen 0.12; 0 for an id outside 1..14.
inline float fog_compiled_occupancy(std::uint32_t profile) noexcept {
    return profile == 2u ? .24f : profile >= 1u && profile <= 14u ? .12f : 0.f;
}
// D(d): the weighted column length to distance d.
inline void fog_column_depth(float d, float start, float cap, float* out) noexcept {
    if (!(d > 0.f)) {
        *out = 0.f;
        return;
    }
    if (!(cap > start)) start = .75f * cap;
    if (d <= start) {
        *out = d;
        return;
    }
    float u = (d - start) / (cap - start);
    u = u > 1.f ? 1.f : u;
    const float u3 = u * u * u;
    *out = start + (cap - start) * (u - u3 + .5f * u3 * u);
}
// The law of a frame: the march's own extinction (sigma x density_scale x ready_far x sigma_scale), the family's
// occupancy and chroma, the look's extinction tint. Off for a non-positive or non-finite extinction.
inline void fog_transmittance_law(float sigma_eff, float occupancy, const float chroma[3], float extinction_tint,
                                  FogTransmittanceLaw* out) noexcept {
    *out = FogTransmittanceLaw{};
    float rho = 0.f;
    fog_mean_density(occupancy, &rho);
    const float e = sigma_eff * rho;
    if (!(e > 0.f) || !(e < 1.f)) return; // NaN, zero, or absurd (one unit of path would already be opaque)
    out->on = true;
    out->extinction = e;
    for (unsigned i = 0; i < 3; ++i) {
        float c = chroma ? chroma[i] : 1.f;
        c = c < 0.f ? 0.f : c > 1.f ? 1.f : c;
        out->k[i] = 1.f + extinction_tint * (1.f - c);
    }
}
// T_rgb at distance d; all ones when the law is off.
inline void fog_transmittance(const FogTransmittanceLaw& law, float d, float out[3]) noexcept {
    out[0] = out[1] = out[2] = 1.f;
    if (!law.on) return;
    float depth = 0.f;
    fog_column_depth(d, law.start, law.cap, &depth);
    const float tau = law.extinction * depth;
    for (unsigned i = 0; i < 3; ++i) fog_exp_negative(law.k[i] * tau, &out[i]);
}
} // namespace x3m::renderer
