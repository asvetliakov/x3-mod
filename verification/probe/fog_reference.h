#pragma once
// float64 CPU reference of the volumetric sun fog (src/fog/*.hlsl; the law of
// tools/analysis/fog_offline_mock.py), for verification/probe/fog_pass_fixture.cpp.
// No d3d9.h: also compiled natively by verification/analysis/test_fog_reference.py.
// Depth input is RGBA float per pixel (.r device depth, -1 sentinel; .b view
// depth when `view_depth_lane`), exactly what the pass samples.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>
namespace fog_reference {
struct Cascade {
    double rows[12]{}; unsigned size = 0; double bias = 0; bool valid = false;
    std::vector<float> map; // size x size, row-major, normalized sun depth
};
struct Params {
    double m00 = 1, m11 = 1, m20 = 0, m21 = 0, m22 = 1.000003, m32 = -6.0000184;
    double tau_max = .02, radius = 10000, g = .3, margin = 1, decode = 2.2;
    double sun[3]{0, 0, 1}, radiance[3]{3.14159, 3.14159, 3.14159};
    unsigned jitter_index = 0; bool view_depth_lane = true;
};
using Visibility = std::function<double(const double p[3])>; // 1 lit, 0 shadowed
inline double view_depth(const float* ds, const Params& p) {
    const bool geometry = ds[0] >= 0.f && ds[0] <= 1.f;
    if (!geometry) return 1e9;
    return (p.view_depth_lane && ds[2] > 0.f) ? double(ds[2]) : p.m32 / (double(ds[0]) - p.m22);
}
inline void ray_of(unsigned x, unsigned y, unsigned w, unsigned h, const Params& p, double ray[3], double* length) {
    const double u = (x + .5) / w, v = (y + .5) / h;
    ray[0] = ((u * 2 - 1) - p.m20) / p.m00; ray[1] = ((1 - v * 2) - p.m21) / p.m11; ray[2] = 1;
    *length = std::sqrt(ray[0] * ray[0] + ray[1] * ray[1] + 1);
}
// The shader's cascade lookup: first containing valid cascade, one nearest tap.
inline double map_visibility(const std::vector<Cascade>& cascades, const Params& p, const double q[3]) {
    for (const Cascade& c : cascades) {
        if (!c.valid) continue;
        double s[3];
        for (unsigned r = 0; r < 3; ++r) s[r] = c.rows[r * 4] * q[0] + c.rows[r * 4 + 1] * q[1] + c.rows[r * 4 + 2] * q[2] + c.rows[r * 4 + 3];
        if (!(std::max(std::fabs(s[0]), std::fabs(s[1])) <= p.margin && s[2] >= 0 && s[2] <= 1)) continue;
        const double n = c.size;
        const int i = int(std::clamp(std::floor((s[0] * .5 + .5 + .5 / n) * n), 0., n - 1)), j = int(std::clamp(std::floor((-s[1] * .5 + .5 + .5 / n) * n), 0., n - 1));
        return double(c.map[std::size_t(j) * c.size + i]) >= s[2] - c.bias ? 1. : 0.;
    }
    return 1.;
}
// Interleaved gradient noise in float32, as the GPU evaluates it.
inline double ign(unsigned i, unsigned j, unsigned jitter_index) {
    const float o = 5.588238f * float(jitter_index % 8u), cx = float(i) + o, cy = float(j) + o;
    float inner = 0.06711056f * cx + 0.00583715f * cy; inner -= std::floor(inner);
    float outer = 52.9829189f * inner; outer -= std::floor(outer);
    return double(outer);
}
// Half-resolution lit fraction (fog_march_ps.hlsl) under any visibility
// functor: the cascade maps (the shader's twin) or an analytic scene.
inline std::vector<double> march(const std::vector<float>& depth, unsigned w, unsigned h, const Params& p, const Visibility& visible) {
    const unsigned hw = (w + 1) / 2, hh = (h + 1) / 2;
    std::vector<double> out(std::size_t(hw) * hh);
    for (unsigned j = 0; j < hh; ++j) for (unsigned i = 0; i < hw; ++i) {
        const unsigned x = 2 * i, y = 2 * j;
        const float* ds = &depth[(std::size_t(y) * w + x) * 4];
        double ray[3], length; ray_of(x, y, w, h, p, ray, &length);
        const double z = view_depth(ds, p);
        const double falloff = z < 1e8 ? std::exp(-z * length / p.radius) : 0.;
        const double reach = 1 - std::exp(-p.tau_max * (1 - falloff));
        const double noise = ign(i, j, p.jitter_index);
        double lit = 0;
        for (unsigned k = 0; k < 16; ++k) {
            const double q = (k + noise) / 16, tau = -std::log(1 - q * reach);
            const double t = -p.radius * std::log(std::max(1 - tau / p.tau_max, 1e-12));
            const double point[3] = {ray[0] / length * t, ray[1] / length * t, ray[2] / length * t};
            lit += visible(point);
        }
        out[std::size_t(j) * hw + i] = lit / 16;
    }
    return out;
}
inline double phase(double g, double cosine) { return (1 - g * g) / (4 * 3.14159265358979 * std::pow(std::max(1 + g * g - 2 * g * cosine, 1e-6), 1.5)); }
inline std::array<double, 3> hue_of(const double sky[3]) {
    const double luma = .2126 * sky[0] + .7152 * sky[1] + .0722 * sky[2];
    if (!(luma > 1e-6)) return {1, 1, 1};
    return {std::clamp(sky[0] / luma, 0., 4.), std::clamp(sky[1] / luma, 0., 4.), std::clamp(sky[2] / luma, 0., 4.)};
}
struct Pixel { double transmittance, lit, phase; std::array<double, 3> linear_out; };
// One full-resolution pixel of fog_composite_ps.hlsl from the engine-space
// scene value, the half-resolution F and the smoothed sky colour.
inline Pixel composite(const std::vector<float>& depth, const std::vector<double>& lit_half, unsigned w, unsigned h, unsigned x, unsigned y,
                       const double engine[3], const double sky[3], const Params& p) {
    const unsigned hw = (w + 1) / 2, hh = (h + 1) / 2;
    double ray[3], length; ray_of(x, y, w, h, p, ray, &length);
    const double zf = view_depth(&depth[(std::size_t(y) * w + x) * 4], p);
    const double falloff = zf < 1e8 ? std::exp(-zf * length / p.radius) : 0.;
    Pixel out{};
    out.transmittance = std::exp(-p.tau_max * (1 - falloff));
    const double fx = (double(x) - .5) / 2, fy = (double(y) - .5) / 2, bx = std::floor(fx), by = std::floor(fy);
    double sum = 0, weights = 0;
    for (int b = 0; b < 2; ++b) for (int a = 0; a < 2; ++a) {
        const unsigned tx = unsigned(std::clamp(bx + a, 0., double(hw - 1))), ty = unsigned(std::clamp(by + b, 0., double(hh - 1)));
        const double bilinear = (a ? fx - bx : 1 - (fx - bx)) * (b ? fy - by : 1 - (fy - by));
        const double zh = view_depth(&depth[(std::size_t(std::min(2 * ty, h - 1)) * w + std::min(2 * tx, w - 1)) * 4], p);
        const double weight = bilinear * (std::exp(-std::fabs(zh - zf) / (.05 * std::min(zf, zh))) + 1e-4);
        sum += weight * lit_half[std::size_t(ty) * hw + tx]; weights += weight;
    }
    out.lit = sum / weights;
    const double cosine = (ray[0] * p.sun[0] + ray[1] * p.sun[1] + ray[2] * p.sun[2]) / length;
    out.phase = phase(p.g, cosine);
    const auto hue = hue_of(sky);
    for (unsigned c = 0; c < 3; ++c) {
        const double e = std::clamp(engine[c], 0., 65504.), linear = e > 0 ? std::pow(e, p.decode) : 0;
        out.linear_out[c] = linear * out.transmittance + hue[c] * p.radiance[c] * out.phase * out.lit * (1 - out.transmittance);
    }
    return out;
}
// Mean linear sky colour as the two sky quads sample it (12 x 12 taps per cell
// of an 8 x 8 grid, nearest texel, linear clamped at 4). coverage = sky taps / all.
inline std::array<double, 3> sky_mean(const std::vector<float>& depth, const std::vector<double>& engine_rgba, unsigned w, unsigned h, double decode, double* coverage) {
    double sum[3]{}, count = 0;
    for (unsigned n = 0; n < 96; ++n) for (unsigned m = 0; m < 96; ++m) {
        const unsigned x = std::min(unsigned((m + .5) / 96. * w), w - 1), y = std::min(unsigned((n + .5) / 96. * h), h - 1);
        const std::size_t i = std::size_t(y) * w + x;
        if (!(depth[i * 4] < 0.f)) continue;
        for (unsigned c = 0; c < 3; ++c) { const double e = std::clamp(engine_rgba[i * 4 + c], 0., 65504.); sum[c] += std::min(e > 0 ? std::pow(e, decode) : 0., 4.); }
        ++count;
    }
    if (coverage) *coverage = count / (96. * 96.);
    if (!count) return {0, 0, 0};
    return {sum[0] / count, sum[1] / count, sum[2] / count};
}
} // namespace fog_reference
