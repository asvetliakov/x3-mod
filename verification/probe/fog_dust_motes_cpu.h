// CPU twin of the stored fog's dust motes (src/fog/fog_dust_motes_vs.hlsl, fog_dust_motes_ps.hlsl;
// docs/architecture/fog-dust-motes.md) for the pass fixture: double arithmetic from the tuning, the seed lattice, the
// frame's camera and rotation, the look rows and fog_density_cpu_march.h's texel-exact density. Independent of the
// pass's constant layout: it takes the world->view rotation, the jittered projection and the target size directly.
#pragma once
#include "fog_density_cpu_march.h"
#include "../../src/renderer/fog_mote_math.h"
#include <vector>
namespace fog_motes_cpu {
struct Frame {
    double camera[3]{}, rotation[3][3]{}; // world->view R: view_j = sum_i q_i R[i][j]
    bool streak = false;                  // the pass's streak rule, decided by the caller
    double previous_camera[3]{}, previous_rotation[3][3]{}, previous_seconds = 0;
    double m00 = 0, m11 = 0, m20 = 0, m21 = 0; // the jittered projection (no quad pixel-centre term)
    unsigned width = 0, height = 0;
    double seconds = 0, brightness = 1; // drift clock; GAIN x density scale x far ramp
    double visibility = 1;              // sun visibility at every mote (1 lit, 0 the dark map)
};
struct Mote {
    std::uint32_t index = 0;
    bool drawn = false;
    double q[3]{}, view[3]{}, centre[2]{}, axis[2]{1, 0}, length = 0, radius = 0, brightness = 0, rho = 0, margin = 0,
                                                          colour[3]{};
    double start(unsigned a) const { return centre[a] - axis[a] * length; }
};
inline void window(const Frame& f, const double v[3], double out[2]) {
    const double z = std::max(v[2], 1.0);
    out[0] = (f.m00 * v[0] / z + f.m20 + 1) * .5 * f.width;
    out[1] = (1 - (f.m11 * v[1] / z + f.m21)) * .5 * f.height;
}
// The fog look's density at camera-relative q, as the pixel program evaluates it (fine level: ready_fine 1).
// `margin`, when given, receives rho - coverage - cover: how far the raw density sits above (or below) the remap's
// zero.
inline double density(const fog_cpu::Setup& s, const double q[3], double* margin = nullptr) {
    const float (*k)[4] = s.look;
    double local[3], anchored[3], ray[3], p[3], warped[3];
    for (int a = 0; a < 3; ++a) {
        local[a] = double(float(s.camera[a] - 65536. * std::floor(s.camera[a] / 65536. + .5)));
        anchored[a] = local[a] + q[a];
    }
    const double wave1[3] = {fog_cpu::look_wave(anchored[1] * k[9][0]), fog_cpu::look_wave(anchored[2] * k[9][0]),
                             fog_cpu::look_wave(anchored[0] * k[9][0])};
    const double wave2[3] = {fog_cpu::look_wave(anchored[2] * k[9][2]), fog_cpu::look_wave(anchored[0] * k[9][2]),
                             fog_cpu::look_wave(anchored[1] * k[9][2])};
    for (int a = 0; a < 3; ++a) {
        ray[a] = q[a] + wave1[a] * k[9][1] + wave2[a] * k[9][3];
        p[a] = s.camera[a] + ray[a];
        warped[a] = (local[a] + ray[a]) / 65536.;
    }
    static const double waves[3][3] = {{1, -2, 1}, {2, 1, -1}, {-1, 1, 2}};
    double cover = 0;
    for (const auto& w : waves) cover += fog_cpu::look_wave(warped[0] * w[0] + warped[1] * w[1] + warped[2] * w[2]);
    cover *= k[10][0];
    const double lambda = s.ready_fine;
    double rho = 0;
    if (lambda > 0) rho += lambda * fog_cpu::level_sample(s, 0, p);
    if (lambda < 1) rho += (1 - lambda) * fog_cpu::level_sample(s, 1, p);
    const double distance = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
    const double e = std::min(std::max((distance - k[10][1]) * k[10][2], 0.), 1.);
    if (margin) *margin = rho - double(k[0][0]) - cover;
    return fog_cpu::look_density(k, rho, cover) * (1 - e * e * (3 - 2 * e));
}
inline std::vector<Mote> motes(const x3m::renderer::FogMoteTuning& t, const Frame& f, const fog_cpu::Setup& s) {
    std::vector<Mote> out(t.count);
    const double radius = t.radius, side = 2 * radius, pi2 = 6.283185307179586;
    const double now = pi2 * (f.seconds / 8 - std::floor(f.seconds / 8));
    const double then = f.streak ? pi2 * (f.previous_seconds / 8 - std::floor(f.previous_seconds / 8)) : now;
    double wrapped[3];
    for (int a = 0; a < 3; ++a) {
        wrapped[a] = std::fmod(f.camera[a], side);
        if (wrapped[a] < 0) wrapped[a] += side;
        wrapped[a] = double(float(wrapped[a]));
    }
    for (std::uint32_t i = 0; i < t.count; ++i) {
        Mote& m = out[i];
        m.index = i;
        float seed[3];
        x3m::renderer::fog_mote_seed(i, t.seed, seed);
        double base[3], q[3], qp[3];
        const double phase_seed[3] = {seed[2], seed[0], seed[1]};
        for (int a = 0; a < 3; ++a) {
            base[a] = double(seed[a]) * side - wrapped[a];
            base[a] -= side * std::floor(base[a] / side + .5);
            const double phi = pi2 * (phase_seed[a] * 13 - std::floor(phase_seed[a] * 13));
            q[a] = base[a] + t.drift * std::sin(now + phi);
            qp[a] = base[a] + (f.streak ? f.camera[a] - f.previous_camera[a] : 0.) + t.drift * std::sin(then + phi);
        }
        const double d = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
        const double fade = std::min(std::max((radius - d) / (.25 * radius), 0.), 1.) *
                            std::min(std::max((d - t.near_fade) / t.near_fade, 0.), 1.);
        double vp[3];
        for (int j = 0; j < 3; ++j) {
            m.view[j] = vp[j] = 0;
            for (int a = 0; a < 3; ++a) {
                m.view[j] += q[a] * f.rotation[a][j];
                vp[j] += qp[a] * (f.streak ? f.previous_rotation[a][j] : f.rotation[a][j]);
            }
        }
        double before[2];
        window(f, m.view, m.centre);
        window(f, vp, before);
        double streak[2] = {0, 0};
        if (f.streak && vp[2] >= 1)
            for (int a = 0; a < 2; ++a) streak[a] = m.centre[a] - before[a];
        const double span = std::sqrt(streak[0] * streak[0] + streak[1] * streak[1]);
        m.length = std::min(span, double(t.streak));
        if (span > 1e-4) {
            m.axis[0] = streak[0] / span;
            m.axis[1] = streak[1] / span;
        }
        const double size = std::min(std::max(double(t.size) * radius / std::max(d, 1e-3), double(t.size)),
                                     double(t.max_px));
        m.radius = .5 * size;
        m.brightness = f.brightness * fade * size / (size + m.length);
        for (int a = 0; a < 3; ++a) m.q[a] = q[a];
        m.drawn = m.view[2] >= 1 && m.brightness > 0;
        if (!m.drawn) continue;
        m.rho = density(s, q, &m.margin);
        const float (*k)[4] = s.look;
        double cosine = 0;
        for (int a = 0; a < 3; ++a) cosine += q[a] * s.sun[a] / std::max(d, 1e-3);
        const double lobe0 = k[4][0] - k[4][1] * cosine, lobe1 = k[5][0] - k[5][1] * cosine;
        const double phase = k[4][2] / (lobe0 * std::sqrt(lobe0)) + k[5][2] / (lobe1 * std::sqrt(lobe1));
        const double lit = k[1][3] + (1 - k[1][3]) * f.visibility;
        for (int c = 0; c < 3; ++c) {
            const double ambient = k[2][c] + (k[3][c] - k[2][c]) * (.5 + .5 * cosine);
            m.colour[c] = k[1][c] * (s.radiance[c] * phase * lit + ambient) * m.rho * m.brightness;
        }
    }
    return out;
}
// The capsule falloff of pixel (x, y) (window coordinates) for one mote.
inline double cover(const Mote& m, double x, double y) {
    const double dx = x - m.start(0), dy = y - m.start(1);
    const double along = dx * m.axis[0] + dy * m.axis[1], across = -dx * m.axis[1] + dy * m.axis[0];
    const double e = along - std::min(std::max(along, 0.), m.length);
    return std::max(1 - (e * e + across * across) / (m.radius * m.radius), 0.);
}
// Pixel bounds of a mote's capsule (inclusive, unclipped).
inline void bounds(const Mote& m, int box[4]) {
    const double x0 = std::min(m.centre[0], m.start(0)) - m.radius, x1 = std::max(m.centre[0], m.start(0)) + m.radius;
    const double y0 = std::min(m.centre[1], m.start(1)) - m.radius, y1 = std::max(m.centre[1], m.start(1)) + m.radius;
    box[0] = int(std::floor(x0));
    box[1] = int(std::floor(y0));
    box[2] = int(std::ceil(x1));
    box[3] = int(std::ceil(y1));
}
// Occlusion of the pixel program: sky 1, invalid geometry 0, else the soft ramp in front of the RT2 depth.
inline double occlusion(const float depth[4], double z, double soft) {
    if (!(depth[0] >= 0 && depth[0] <= 1)) return 1;
    if (!(depth[2] > 0 && depth[2] <= 3.402823466e38)) return 0;
    return std::min(std::max((double(depth[2]) - z) / std::max(soft * z, 1e-4), 0.), 1.);
}
// The additive image (RGB per pixel) the motes put on the target; depth is the RT2 image (RGBA32F), may be null (sky).
inline std::vector<double> image(const std::vector<Mote>& list, unsigned width, unsigned height, const float* depth,
                                 double soft) {
    std::vector<double> out(std::size_t(width) * height * 3, 0.);
    for (const Mote& m : list) {
        if (!m.drawn || !(m.rho > 0)) continue;
        int box[4];
        bounds(m, box);
        for (int y = std::max(box[1], 0); y <= std::min(box[3], int(height) - 1); ++y)
            for (int x = std::max(box[0], 0); x <= std::min(box[2], int(width) - 1); ++x) {
                const double c = cover(m, x, y);
                if (!(c > 0)) continue;
                const float sky[4] = {2, 0, 0, 0};
                const double o = occlusion(depth ? depth + (std::size_t(y) * width + x) * 4 : sky, m.view[2], soft);
                for (int k = 0; k < 3; ++k) out[(std::size_t(y) * width + x) * 3 + k] += c * o * m.colour[k];
            }
    }
    return out;
}
} // namespace fog_motes_cpu
