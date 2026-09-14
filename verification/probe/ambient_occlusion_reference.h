#pragma once
// CPU reference of the ambient occlusion chain (src/temporal/ao_*_ps.hlsl),
// float64 with the same texel choices: linearize at the even full texel,
// the 5-tap normal, 2 slices x 2 sides x 4 steps of horizon taps at texel
// centres (elevation above the tangent plane), the normalized per-slice arc, the two 5-tap depth-aware blurs and
// the bilateral upsample / multiply factor. R16F storage between GPU passes is
// modelled by fp16 rounding (round to nearest even). Verification only: no
// D3D, no Windows headers, compiled on the host by
// verification/analysis/test_ambient_occlusion_reference.py and into the
// fixture. Not production code.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
namespace ao_reference {
struct Params {
    double m00 = 0, m11 = 0, m20 = 0, m21 = 0, m22 = 0, m32 = 0;
    double radius = 1, strength = .5, falloff = .615, max_radius_px = 64, depth_tolerance = .05;
    unsigned jitter_index = 0;
};
struct V3 { double x, y, z; };
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double length(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 normalize(V3 a) { const double l = length(a); return l > 0 ? a * (1 / l) : a; }
constexpr double kPi = 3.14159265358979323846;
inline double saturate(double v) { return std::min(1., std::max(0., v)); }
// IEEE binary16 round trip of a value (round to nearest even), as the R16F targets store it.
inline double fp16(double value) {
    float f = float(value);
    std::uint32_t bits; std::memcpy(&bits, &f, 4);
    const std::uint32_t sign = bits & 0x80000000u; const int exponent = int((bits >> 23) & 255) - 127;
    std::uint32_t mantissa = bits & 0x7fffffu;
    if (exponent == 128) return value;
    int he = exponent + 15;
    std::uint16_t h;
    if (he >= 31) h = std::uint16_t((sign >> 16) | 0x7c00);
    else if (he <= 0) {
        if (he < -10) h = std::uint16_t(sign >> 16);
        else {
            mantissa |= 0x800000u;
            const unsigned shift = unsigned(14 - he), remainder = mantissa & ((1u << shift) - 1), halfway = 1u << (shift - 1);
            std::uint32_t rounded = mantissa >> shift;
            if (remainder > halfway || (remainder == halfway && (rounded & 1))) ++rounded;
            h = std::uint16_t((sign >> 16) | rounded);
        }
    } else {
        const std::uint32_t rounded = mantissa + 0xfff + ((mantissa >> 13) & 1);
        h = std::uint16_t((sign >> 16) | ((std::uint32_t(he) << 10) + (rounded >> 13)));
    }
    const unsigned e = (h >> 10) & 31; const double s = (h & 0x8000) ? -1 : 1;
    if (e == 31) return (h & 1023) ? NAN : s * INFINITY;
    return s * (e ? std::ldexp(double(1024 + (h & 1023)), int(e) - 25) : std::ldexp(double(h & 1023), -24));
}
inline double linearize(double d, const Params& p) { return d < 0 ? -1 : p.m32 / (d - p.m22); }
// Half-resolution linear depth: the even full texel of every half texel.
inline std::vector<double> linearize(const std::vector<float>& depth, unsigned w, unsigned h, unsigned hw, unsigned hh, const Params& p) {
    std::vector<double> out(std::size_t(hw) * hh);
    for (unsigned j = 0; j < hh; ++j) for (unsigned i = 0; i < hw; ++i) {
        const unsigned x = std::min(2 * i, w - 1), y = std::min(2 * j, h - 1);
        out[std::size_t(j) * hw + i] = linearize(depth[std::size_t(y) * w + x], p);
    }
    return out;
}
struct HalfImage {
    const std::vector<double>& z; unsigned w, h; const Params& p;
    double at(long x, long y) const { x = std::clamp(x, 0L, long(w) - 1); y = std::clamp(y, 0L, long(h) - 1); return z[std::size_t(y) * w + x]; }
    V3 position(double px, double py, double depth) const {
        const double u = (px + .5) / w, v = (py + .5) / h;
        const double nx = 2 * u - 1, ny = 1 - 2 * v;
        return {(nx - p.m20) * depth / p.m00, (ny - p.m21) * depth / p.m11, depth};
    }
};
inline double bayer4(unsigned x, unsigned y) {
    const unsigned x0 = x & 1, x1 = (x >> 1) & 1, y0 = y & 1, y1 = (y >> 1) & 1;
    return double((x0 ^ y0) * 8 + y0 * 4 + (x1 ^ y1) * 2 + y1);
}
// The horizon search of one half-resolution pixel (ao_gtao_ps.hlsl main): occlusion 1 - visibility.
inline double gtao_pixel(const HalfImage& img, unsigned x, unsigned y) {
    const Params& p = img.p;
    const double zc = img.at(x, y);
    if (zc < 0) return 0;
    const V3 centre = img.position(x, y, zc);
    const double zl = img.at(long(x) - 1, y), zr = img.at(long(x) + 1, y), zu = img.at(x, long(y) - 1), zd = img.at(x, long(y) + 1);
    const bool lv = zl >= 0 && x >= 1, rv = zr >= 0 && x + 2 <= img.w, uv = zu >= 0 && y >= 1, dv = zd >= 0 && y + 2 <= img.h;
    const bool useR = rv && (!lv || std::fabs(zr - zc) <= std::fabs(zc - zl));
    const bool useD = dv && (!uv || std::fabs(zd - zc) <= std::fabs(zc - zu));
    const double zx = useR ? zr : (lv ? zl : zc), zy = useD ? zd : (uv ? zu : zc);
    const double ox = (useR || !lv) ? 1 : -1, oy = (useD || !uv) ? 1 : -1;
    const V3 dx = (img.position(x + ox, y, zx) - centre) * ox, dy = (img.position(x, y + oy, zy) - centre) * oy;
    V3 normal = normalize(cross(dx, dy));
    const V3 view = normalize(centre * -1);
    if (dot(normal, view) < 0) normal = normal * -1;
    const double pxScale = p.m11 * img.h * .5;
    const double radiusPx = std::clamp(p.radius * pxScale / zc, 2., p.max_radius_px);
    const double falloffRange = p.falloff * p.radius, falloffMul = -1 / falloffRange, falloffAdd = (p.radius - falloffRange) / falloffRange + 1;
    const double bayer = bayer4(x, y), rot = double(p.jitter_index % 16u);
    const double sliceNoise = std::fmod(bayer + rot, 16.) / 16;
    const double stepIndex = bayer * 5 + rot * 3;
    const double stepNoise = (stepIndex - 16 * std::floor(stepIndex / 16) + .5) / 16;
    double visibility = 0, weightSum = 0;
    for (int slice = 0; slice < 2; ++slice) {
        const double phi = (slice + sliceNoise) * (kPi / 2);
        const double dirx = std::cos(phi), diry = std::sin(phi);
        const V3 dirView{dirx, -diry, 0};
        const V3 ortho = dirView - view * dot(dirView, view);
        const V3 axis = normalize(cross(ortho, view));
        const V3 projected = normal - axis * dot(normal, axis);
        const double projectedLength = std::max(length(projected), 1e-6);
        const double cosN = saturate(dot(projected, view) / projectedLength);
        const double sign = dot(ortho, projected) >= 0 ? 1 : -1;
        const double n = sign * std::acos(cosN);
        const double sinN = sign * std::sqrt(saturate(1 - cosN * cosN));
        double elevation0 = 0, elevation1 = 0;
        auto tap = [&](double offx, double offy, double elevation) {
            const double tx = std::clamp(std::floor(x + .5 + offx), 0., double(img.w) - 1), ty = std::clamp(std::floor(y + .5 + offy), 0., double(img.h) - 1);
            const double z = img.at(long(tx), long(ty));
            const V3 delta = img.position(tx, ty, z) - centre;
            const double dist = length(delta);
            const double sine = saturate(dot(delta, normal) / std::max(dist, 1e-12)) * saturate(dist * falloffMul + falloffAdd);
            return (z >= 0 && dist > zc * 1e-4) ? std::max(elevation, sine) : elevation;
        };
        for (int step = 0; step < 4; ++step) {
            const double s = (step + stepNoise) * .25 * radiusPx;
            elevation0 = tap(dirx * s, diry * s, elevation0);
            elevation1 = tap(-dirx * s, -diry * s, elevation1);
        }
        const double h1 = n + std::acos(elevation0), h0 = n - std::acos(elevation1);
        const double cos2h1n = cosN * (2 * elevation0 * elevation0 - 1) - sinN * 2 * elevation0 * std::sqrt(saturate(1 - elevation0 * elevation0));
        const double cos2h0n = cosN * (2 * elevation1 * elevation1 - 1) + sinN * 2 * elevation1 * std::sqrt(saturate(1 - elevation1 * elevation1));
        const double arc0 = (cosN + 2 * h0 * sinN - cos2h0n) * .25, arc1 = (cosN + 2 * h1 * sinN - cos2h1n) * .25;
        visibility += projectedLength * (arc0 + arc1) / (cosN + n * sinN);
        weightSum += projectedLength;
    }
    return saturate(1 - visibility / weightSum); // occlusion, as the R16F term stores it
}
inline std::vector<double> gtao(const std::vector<double>& halfz, unsigned hw, unsigned hh, const Params& p) {
    const HalfImage img{halfz, hw, hh, p};
    std::vector<double> out(halfz.size());
    for (unsigned y = 0; y < hh; ++y) for (unsigned x = 0; x < hw; ++x) out[std::size_t(y) * hw + x] = fp16(gtao_pixel(img, x, y));
    return out;
}
// One separable 5-tap depth-aware blur (ao_blur_ps.hlsl), fp16-stored.
inline std::vector<double> blur(const std::vector<double>& ao, const std::vector<double>& halfz, unsigned hw, unsigned hh, int dx, int dy, double tau) {
    std::vector<double> out(ao.size());
    auto at = [&](const std::vector<double>& v, long x, long y) { return v[std::size_t(std::clamp(y, 0L, long(hh) - 1)) * hw + std::size_t(std::clamp(x, 0L, long(hw) - 1))]; };
    static const int taps[4] = {-2, -1, 1, 2}; static const double weights[4] = {1, 4, 4, 1};
    for (unsigned y = 0; y < hh; ++y) for (unsigned x = 0; x < hw; ++x) {
        const double zc = at(halfz, x, y);
        if (zc < 0) { out[std::size_t(y) * hw + x] = 0; continue; }
        double sum = 6 * at(ao, x, y), weightSum = 6;
        for (int i = 0; i < 4; ++i) {
            const long tx = x + dx * taps[i], ty = y + dy * taps[i];
            const double z = at(halfz, tx, ty);
            const double w = z < 0 ? 0 : weights[i] * saturate(1 - std::fabs(z - zc) / (tau * zc));
            sum += w * at(ao, tx, ty); weightSum += w;
        }
        out[std::size_t(y) * hw + x] = fp16(sum / weightSum);
    }
    return out;
}
// The whole term chain: linearize, GTAO, horizontal then vertical blur. The
// result is the occlusion term as stored (0 = unoccluded); ambient_occlusion_fixture.cpp
// compares 1 - term against 1 - readback.
inline std::vector<double> term(const std::vector<float>& depth, unsigned w, unsigned h, const Params& p, std::vector<double>* half_depth = nullptr) {
    const unsigned hw = (w + 1) / 2, hh = (h + 1) / 2;
    std::vector<double> z = linearize(depth, w, h, hw, hh, p);
    std::vector<double> a = gtao(z, hw, hh, p);
    a = blur(a, z, hw, hh, 1, 0, p.depth_tolerance);
    a = blur(a, z, hw, hh, 0, 1, p.depth_tolerance);
    if (half_depth) *half_depth = std::move(z);
    return a;
}
// The apply pass's factor of one full-resolution pixel (ao_apply_ps.hlsl) from
// the half-resolution occlusion term and depth (the GPU's readback in the fixture) and
// the pixel's own device depth.
inline double factor(const std::vector<double>& ao, const std::vector<double>& halfz, unsigned hw, unsigned hh, unsigned x, unsigned y, float d, const Params& p) {
    if (d < 0) return 1;
    const double zf = p.m32 / (double(d) - p.m22);
    const double hx = x * .5, hy = y * .5, bx = std::floor(hx), by = std::floor(hy), fx = hx - bx, fy = hy - by;
    double sum = 0, weightSum = 0;
    for (int b = 0; b < 2; ++b) for (int a = 0; a < 2; ++a) {
        const unsigned tx = unsigned(std::min(bx + a, double(hw) - 1)), ty = unsigned(std::min(by + b, double(hh) - 1));
        const double bilinear = (a ? fx : 1 - fx) * (b ? fy : 1 - fy);
        const double z = halfz[std::size_t(ty) * hw + tx];
        const double w = z < 0 ? 0 : bilinear * saturate(1 - std::fabs(z - zf) / (p.depth_tolerance * zf));
        sum += w * ao[std::size_t(ty) * hw + tx]; weightSum += w;
    }
    const double occlusion = weightSum > 1e-4 ? sum / weightSum : 0;
    return std::pow(1 - p.strength * occlusion, 1 / 2.2);
}
} // namespace ao_reference
