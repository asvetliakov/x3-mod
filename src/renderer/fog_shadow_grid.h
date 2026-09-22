#pragma once
// d3d9-free arithmetic of the sun-visibility slice grid (docs/architecture/fog-shadow-pass.md): a
// quarter-resolution screen grid (one texel per 4x4 full pixels) of 64 slices at the sky ray's bin
// distances, four slices per RGBA8 texel in a 4x4-tile atlas, filled by one quad before the march;
// march and repair read it with one bilinear fetch per non-empty step (X3M_FOG_SHADOW_PASS=1). The
// shader twin is src/fog/fog_shadow_grid_inc.h; verification/analysis/test_fog_shadow_grid.py compiles
// this header natively against tools/analysis/fog_density_shader_reference.py. Plain float arithmetic.
#include <cmath>
#include "fog_look_math.h"
namespace x3m::renderer {
constexpr unsigned fog_grid_first_register = 36, fog_grid_rows = 6; // c36..c41
constexpr unsigned fog_grid_slices = 64, fog_grid_near_slices = 24, fog_grid_tiles = 4, fog_grid_pixels = 4;
constexpr float fog_grid_near_range = 12000.f, fog_grid_near_width = 500.f;
// The sun's 0.53 degree disc, radians: a blocker d units nearer the light than a sample casts a penumbra
// 0.0093 d wide there. The kernel's radius is that full width: the blocker search is tap 0's own 2x2, which
// finds the blocker on the shadow side of the edge only, so the penumbra grows one-sided from the edge into
// the shadow and its 10-90 % width is about the radius (fixture item 5 measures it).
constexpr float fog_grid_sun_angle = .0093f;
constexpr float fog_grid_golden = .618034f; // the strata and the disc rotation advance by this per TAA phase
inline unsigned fog_grid_extent(unsigned pixels) noexcept { return (pixels + fog_grid_pixels - 1) / fog_grid_pixels; }
// The far slices need a finite width of at least one unit: cap >= 12040 (the look's SKY_CAP range starts at 20000,
// but the grid guards its own boundary; fog_grid_constants refuses an invalid cap and the caller lights the frame).
constexpr float fog_grid_cap_min = fog_grid_near_range + float(fog_grid_slices - fog_grid_near_slices);
inline bool fog_grid_valid_cap(float cap) noexcept { return std::isfinite(cap) && cap >= fog_grid_cap_min; }
inline float fog_grid_far_width(float cap) noexcept { return (cap - fog_grid_near_range) / float(fog_grid_slices - fog_grid_near_slices); }
inline float fog_grid_slice_start(unsigned j, float cap) noexcept {
    return j < fog_grid_near_slices ? fog_grid_near_width * float(j) : fog_grid_near_range + fog_grid_far_width(cap) * float(j - fog_grid_near_slices);
}
inline float fog_grid_slice_width(unsigned j, float cap) noexcept { return j < fog_grid_near_slices ? fog_grid_near_width : fog_grid_far_width(cap); }
// The slice holding a distance s along the ray: the shader's floor law with the reciprocal rows of c39, clamped.
inline unsigned fog_grid_slice_of(float s, float cap) noexcept {
    const float j = s < fog_grid_near_range ? std::floor(s * (1.f / fog_grid_near_width))
                                            : float(fog_grid_near_slices) + std::floor((s - fog_grid_near_range) * (1.f / fog_grid_far_width(cap)));
    return j <= 0.f ? 0u : j >= float(fog_grid_slices - 1) ? fog_grid_slices - 1 : unsigned(j);
}
// Slice j lives in tile (x, y) of the 4x4 layout, lane j mod 4 of its texel.
inline void fog_grid_tile(unsigned j, unsigned& tile_x, unsigned& tile_y, unsigned& lane) noexcept {
    const unsigned t = j / fog_grid_tiles; lane = j % fog_grid_tiles; tile_y = t / fog_grid_tiles; tile_x = t % fog_grid_tiles;
}
// shadow_weight's cross-fade for a cascade: 1 inside the band start, linear to 0 at the margin (.85 -> .95 with
// the rows the pass uploads in c9), 0 outside the margin or the depth range, times the cascade's validity.
inline float fog_grid_blend(float m, float z, float valid, float margin = .95f, float band = .85f, float reciprocal = 10.f) noexcept {
    if (!(m <= margin && z >= 0.f && z <= 1.f)) return 0.f;
    float w = (m - band) * reciprocal; w = w < 0.f ? 0.f : w > 1.f ? 1.f : w;
    return valid * (1.f - w);
}
// Penumbra kernel radius in texels of the sampled map from the blocker's normalized depth gap (reference minus the
// nearest of the four compared depths, <= 0 when lit) and the cascade's range_world / texel_world.
inline float fog_grid_penumbra_radius(float depth_gap, float range_over_texel, const FogLookTuning& t) noexcept {
    const float r = depth_gap * range_over_texel * fog_grid_sun_angle * t.penumbra;
    return r < t.penumbra_min ? t.penumbra_min : r > t.penumbra_max ? t.penumbra_max : r;
}
// What the pass needs of a bound cascade beyond its rows: the world texel and the sun-space depth range.
struct FogGridCascade { float texel_world = 0.f, range_world = 0.f; };
// Rows c36..c41: per cascade (texel_world, range_world, range/texel or 0 for a fixed kernel, 0); the slice layout
// (500, far width, 1/500, 1/far width) from the sky column cap; the grid (tile W, tile H, 1/atlas W, 1/atlas H);
// the penumbra (sun angle x penumbra, min, max, frame term). The frame term advances the strata and the disc
// rotation by the golden ratio per TAA phase while a resolve averages them, and holds at 0 without one.
// False (rows zero) for a cap the slice law cannot divide.
inline bool fog_grid_constants(const FogLookTuning& t, float cap, unsigned width, unsigned height, const FogGridCascade cascades[3],
                               unsigned phase, bool resolved, float rows[fog_grid_rows][4]) noexcept {
    for (unsigned i = 0; i < fog_grid_rows; ++i) for (unsigned j = 0; j < 4; ++j) rows[i][j] = 0.f;
    if (!fog_grid_valid_cap(cap)) return false;
    for (unsigned i = 0; i < 3; ++i) {
        const FogGridCascade& c = cascades[i];
        const bool known = c.texel_world > 0.f && c.range_world > 0.f && std::isfinite(c.texel_world) && std::isfinite(c.range_world);
        rows[i][0] = known ? c.texel_world : 0.f; rows[i][1] = known ? c.range_world : 0.f; rows[i][2] = known ? c.range_world / c.texel_world : 0.f;
    }
    const float far_width = fog_grid_far_width(cap);
    rows[3][0] = fog_grid_near_width; rows[3][1] = far_width; rows[3][2] = 1.f / fog_grid_near_width; rows[3][3] = 1.f / far_width;
    const float gw = float(fog_grid_extent(width)), gh = float(fog_grid_extent(height));
    rows[4][0] = gw; rows[4][1] = gh; rows[4][2] = 1.f / (float(fog_grid_tiles) * gw); rows[4][3] = 1.f / (float(fog_grid_tiles) * gh);
    const float turn = float(phase % 64u) * fog_grid_golden;
    rows[5][0] = fog_grid_sun_angle * t.penumbra; rows[5][1] = t.penumbra_min; rows[5][2] = t.penumbra_max > t.penumbra_min ? t.penumbra_max : t.penumbra_min;
    rows[5][3] = resolved ? turn - std::floor(turn) : 0.f;
    return true;
}
// Shadow-map fetches of one frame at a resolution: the pass reads every slice of every grid texel (16 per slice).
inline unsigned long long fog_grid_pass_fetches(unsigned width, unsigned height) noexcept {
    return 16ull * fog_grid_slices * fog_grid_extent(width) * fog_grid_extent(height);
}
} // namespace x3m::renderer
