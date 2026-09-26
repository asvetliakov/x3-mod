#pragma once
// Per-part minimum light-space footprint of the shadow cascades
// (docs/architecture/shadow-cascades.md, "Minimum caster footprint";
// docs/architecture/shadow-cascade-cost-policy.md, option (a)).
//
// A pixel is sampled from cascade k only where it lies outside cascade k-1's
// box (the apply quad's select_margin 0.95), and its sun-space lateral
// distance is a lower bound on its distance from the camera, so every receiver
// pixel cascade k serves is at least 0.95 x E_{k-1} away. Under a parallel
// light the shadow on that receiver is the caster's light-space silhouette, so
// a caster whose silhouette is under
//
//     min_k = max(P x 0.95 x E_{k-1} x 2 / (m00 x width), 3 x texel_k)
//
// world units can darken at most P pixels of any receiver that cascade serves
// (and under three texels of its own map it casts nothing coherent anyway).
// The measure is the largest of the two LATERAL sides of the caster's
// sun-space AABB (never the depth side: a depth-long sliver keeps its bit),
// which the draw-time box test and the retention store's unseen walk both form
// already. E_{k-1} is the nearest ACTIVE cascade below k (the sliding ladder
// drops cascades; the apply quad's slots are the active ones), 0 for cascade 0,
// which therefore keeps the texel bound alone.
//
// No Windows dependency and no renderer type: the host tests compile this
// header directly, and the production callers (src/proxy/motion_output.cpp,
// src/proxy/shadow_retention_core.h) pass plain arrays.
#include <cmath>
#include <cstdint>

namespace x3m::renderer {
// The option's band (X3M_SHADOW_CASCADE_MIN_FOOTPRINT, screen pixels): 0 is
// off (bit-identical), 64 px is a guard against a typo. Absent, the cascades use
// the default 8 px, the launcher's default (user selection after run251/run253,
// 2026-09-22), so a launch without the launcher behaves the same.
constexpr float shadow_cascade_min_footprint_max = 64.f;
constexpr float shadow_cascade_min_footprint_default = 8.f;
constexpr float shadow_cascade_footprint_select_margin = .95f; // the apply quad's cascade-selection margin
constexpr float shadow_cascade_footprint_texels = 3.f;         // the texel floor of the law
constexpr unsigned shadow_cascade_footprint_max_cascades = 5;  // renderer::shadow_cascade_max

inline bool shadow_cascade_min_footprint_valid(double px) noexcept {
    return std::isfinite(px) && px > 0. && px <= double(shadow_cascade_min_footprint_max);
}
// The resolved thresholds of one frame: world units per cascade, 0 = no gate
// (the option off, an inactive cascade, or an unusable cascade).
struct ShadowCascadeFootprintLaw {
    unsigned count = 0;
    float px = 0.f;                                           // the option's value, for the log line
    float min_units[shadow_cascade_footprint_max_cascades]{}; // 0: cascade k is not gated
    bool gates() const noexcept {
        for (unsigned i = 0; i < count && i < shadow_cascade_footprint_max_cascades; ++i)
            if (min_units[i] > 0.f) return true;
        return false;
    }
    bool operator==(const ShadowCascadeFootprintLaw& o) const noexcept {
        if (count != o.count || px != o.px) return false;
        for (unsigned i = 0; i < shadow_cascade_footprint_max_cascades; ++i)
            if (min_units[i] != o.min_units[i]) return false;
        return true;
    }
    bool operator!=(const ShadowCascadeFootprintLaw& o) const noexcept { return !(*this == o); }
};
// `extents`/`sizes`: the live cascade half-extents and map sizes; `active`:
// null = every cascade active. `m00` is the projection's P[0] and `width` the
// back buffer's, as the small-parts cull reads them; an unusable pair leaves
// the screen term out (the texel floor alone still holds, being a property of
// the map). False and a cleared law when the option is off or the inputs are
// unusable: the gate then drops nothing.
inline bool shadow_cascade_footprint_law(float px, float m00, unsigned width, const float* extents,
                                         const unsigned* sizes, const unsigned* active, unsigned count,
                                         ShadowCascadeFootprintLaw& out) noexcept {
    out = ShadowCascadeFootprintLaw{};
    if (!shadow_cascade_min_footprint_valid(double(px)) || !extents || !sizes) return false;
    if (!count || count > shadow_cascade_footprint_max_cascades) return false;
    // The pixel footprint at distance d is 2 d / (m00 x width) units (the
    // small-parts cull's scale): usable only with a live perspective latch.
    const bool screen_ok = std::isfinite(m00) && m00 > .05f && m00 < 20.f && width >= 16 && width <= 16384;
    const double per_pixel = screen_ok ? 2. / (double(m00) * double(width)) : 0.;
    out.count = count;
    out.px = px;
    float previous = 0.f; // the nearest active cascade below k, its half-extent
    for (unsigned k = 0; k < count; ++k) {
        const bool on = !active || ((*active >> k) & 1u) != 0;
        const float e = extents[k];
        const unsigned size = sizes[k];
        if (!on || !std::isfinite(e) || !(e > 0.f) || !size) continue;
        const double screen = double(px) * double(shadow_cascade_footprint_select_margin) * double(previous) *
                              per_pixel;
        const double texel = double(shadow_cascade_footprint_texels) * 2. * double(e) / double(size);
        const double min_units = screen > texel ? screen : texel;
        out.min_units[k] = std::isfinite(min_units) ? float(min_units) : 0.f;
        previous = e;
    }
    return true;
}
// The drop decision for one caster and one cascade: `footprint` is the largest
// of the two lateral sides of its sun-space AABB, in world units. A caster
// whose footprint is unknown (0 or nonfinite: no extent read yet) is kept.
inline bool shadow_cascade_footprint_drops(const ShadowCascadeFootprintLaw& law, unsigned cascade,
                                           float footprint) noexcept {
    if (cascade >= law.count || cascade >= shadow_cascade_footprint_max_cascades) return false;
    const float min_units = law.min_units[cascade];
    if (!(min_units > 0.f) || !(footprint > 0.f) || !std::isfinite(footprint)) return false;
    return footprint < min_units;
}
// The gate over a whole cascade mask: returns the mask to keep and, in
// `refused`, the bits the gate removed. `footprints` holds one measure per
// cascade (the per-cascade suns each form their own box; one shared box
// repeats its value).
inline unsigned shadow_cascade_footprint_gate(const ShadowCascadeFootprintLaw& law, unsigned mask,
                                              const float* footprints, unsigned* refused = nullptr) noexcept {
    if (refused) *refused = 0;
    if (!law.count || !footprints || !mask) return mask;
    unsigned out = mask, dropped = 0;
    for (unsigned k = 0; k < law.count && k < shadow_cascade_footprint_max_cascades; ++k) {
        if (!((mask >> k) & 1u)) continue;
        if (!shadow_cascade_footprint_drops(law, k, footprints[k])) continue;
        out &= ~(1u << k);
        dropped |= 1u << k;
    }
    if (refused) *refused = dropped;
    return out;
}
// The largest lateral side of a sun-space AABB given as min/max per axis
// (axes 0 and 1 are the map's; axis 2 is depth towards the light).
inline float shadow_cascade_footprint_lateral(const float smin[3], const float smax[3]) noexcept {
    if (!smin || !smax) return 0.f;
    const float x = smax[0] - smin[0], y = smax[1] - smin[1];
    const float side = x > y ? x : y;
    return std::isfinite(side) && side > 0.f ? side : 0.f;
}
} // namespace x3m::renderer
