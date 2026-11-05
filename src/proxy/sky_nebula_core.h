#pragma once
// Sky nebula deband, Decision C1 (docs/architecture/sector-background-modern.md, "Decision C"): the pure part of the
// nebula part draw swap. The four nebula part draws of the background phase run the engine's SM1.1 pair
// 7b6393fe2d3e1d85 (adeffects.fb, VS) / 6109cf64c03529dd (gui2d.fb, PS); the route swaps in the own vs_3_0/ps_3_0
// pair src/effects/sky_nebula_{vs,ps}.hlsl for them and puts the engine's pair back after each draw. This header holds
// the pair, the settings and their ranges, the constants the swap sets (VS c255 jitter, PS c200-c201) and a CPU model of
// the pixel program's hash and tap set (verification/analysis/test_sky_nebula.py compiles exactly this header). No
// Windows/D3D dependency, no allocation, no CRT math, no float returned by value from an out-of-line function.
#include <cstdint>

namespace x3m::sky_nebula::core {
constexpr std::uint64_t part_vs = 0x7b6393fe2d3e1d85ull; // adeffects.fb vs_1_1, 512 B (FNV-1a 64 of the token stream)
constexpr std::uint64_t part_ps = 0x6109cf64c03529ddull; // gui2d.fb ps_1_1, 304 B
constexpr unsigned part_width_min = 1024;                 // FL 2048x1024 / 2048x2048, AP 1024x512 DXT5 parts
constexpr float threshold_default = 6.f, threshold_min = 1.f, threshold_max = 16.f; // sky_nebula_deband_threshold, 1/255
constexpr float radius_default = 12.f, radius_min = 2.f, radius_max = 32.f;          // sky_nebula_deband_radius, texels
// The weight's slope: w = saturate(slope (on - diff / threshold)); 2 = full weight up to half the threshold, none at it.
constexpr float weight_slope = 2.f;
constexpr unsigned vs_constant_jitter = 255;     // c255 = (2 jx / W, -2 jy / H, 0, 0), else 0
constexpr unsigned ps_constant_first = 200, ps_constant_count = 2; // c200 = (on, 1 / threshold, R, slope), c201 = size
inline bool accept_range(float v, float lo, float hi, float* out) noexcept {
    if (!(v >= lo && v <= hi)) return false;
    *out = v;
    return true;
}
inline bool accept_threshold(float v, float* out) noexcept { return accept_range(v, threshold_min, threshold_max, out); }
inline bool accept_radius(float v, float* out) noexcept { return accept_range(v, radius_min, radius_max, out); }
// PS c200-c201 for a part of level-0 size width x height: (on, 255 / threshold, radius, weight_slope), (width, height,
// 1 / width, 1 / height). threshold in 1/255 units; threshold 0 (no setting reaches it: the fixture's identity mode,
// invariant 1) gives on = 0, so the program's weight is exactly 0 and it returns the centre sample. Returns false (out
// untouched) for a zero size.
inline bool pixel_constants(float threshold_255, float radius_texels, unsigned width, unsigned height,
                            float out[8]) noexcept {
    if (!width || !height) return false;
    const bool on = threshold_255 > 0.f;
    out[0] = on ? 1.f : 0.f;
    out[1] = on ? 255.f / threshold_255 : 0.f;
    out[2] = radius_texels;
    out[3] = weight_slope;
    out[4] = float(width);
    out[5] = float(height);
    out[6] = 1.f / float(width);
    out[7] = 1.f / float(height);
    return true;
}
// VS c255: the frame's jitter in raster px (MotionOutput jitter_, the sign of fade_region_math.h jitter_rows, as the star
// batch's sky_stars::core::vertex_constants) folded into clip space as oPos.xy += c255.xy x oPos.w; zero when the jitter
// is off or sky_nebula_jitter = 0.
inline void jitter_constant(bool active, float jitter_x_px, float jitter_y_px, unsigned width, unsigned height,
                            float out[4]) noexcept {
    const bool on = active && width && height;
    out[0] = on ? 2.f * jitter_x_px / float(width) : 0.f;
    out[1] = on ? -2.f * jitter_y_px / float(height) : 0.f;
    out[2] = out[3] = 0.f;
}
// The pixel program's tap pattern angle: a quarter turn times a texel-indexed hash (Hoskins' hash12 of the texel index),
// interpolated bilinearly between the four nearest texel centres. It depends on the texture coordinate only (static in
// texture space: no per-frame term, no screen position) and is continuous in it (neighbouring texels blend their patterns
// instead of jumping). The GPU's fp32 rounding may differ from this model in the last bits; neither property depends on
// them.
inline float floor_small(float x) noexcept { // floor for |x| < 2^23 (the program's range)
    const float t = float(std::int32_t(x));
    return t > x ? t - 1.f : t;
}
inline float frac(float x) noexcept { return x - floor_small(x); }
inline float hash(float px, float py) noexcept {
    float x = frac(px * .1031f), y = frac(py * .1031f), z = frac(px * .1031f);
    const float d = x * (y + 33.33f) + y * (z + 33.33f) + z * (x + 33.33f);
    x += d;
    y += d;
    z += d;
    return frac((x + y) * z);
}
// The field at texel-space position (u x width, v x height): the hashes of the texel centres around it, bilinearly.
inline float pattern_field(float tx, float ty) noexcept {
    const float px = tx - .5f, py = ty - .5f;
    const float x0 = floor_small(px), y0 = floor_small(py);
    const float fx = px - x0, fy = py - y0;
    const float a = hash(x0, y0) + (hash(x0 + 1.f, y0) - hash(x0, y0)) * fx;
    const float b = hash(x0, y0 + 1.f) + (hash(x0 + 1.f, y0 + 1.f) - hash(x0, y0 + 1.f)) * fx;
    return a + (b - a) * fy;
}
} // namespace x3m::sky_nebula::core
