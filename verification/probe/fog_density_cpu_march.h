// CPU twin of src/fog/fog_density_field_inc.h's march for fixtures: double arithmetic,
// texel-exact trilinear reconstruction of the generator's FP16 nodes read straight from
// node_word (no atlas, no window, no c0 / uv convention). The caller hands in the view
// ray, so a reference built on this is independent of how a program derives its ray.
#pragma once
#include "../../src/fog/fog_density_generator.h"
#include "../../src/renderer/fog_look_math.h"
#include <cmath>
#include <functional>
namespace fog_cpu {
using namespace x3m::fog;
struct Setup {
    double camera[3]{};     // world, render units
    double inverse[3][3]{}; // rows c4..c6: world = inverse * view
    double sun[3]{1, 0, 0};
    double sigma = 0, chroma[3]{1, 1, 1}, ready_fine = 1;
    double phase[3]{1.09, .6, .91}, radiance[3]{1, 1, 1};
    WorldOffset offset = kNoOffset;
    // Verification control: nodes on the far side of a storage seam (index 127 -> 0) read as
    // zero, which is what a sampler without the duplicate border / lane wrap would mix in.
    bool break_seam = false;
    // Rows c25..c35 of the single look (renderer::fog_look_constants), which every stored-density draw binds.
    // Null selects the unshaped law, which only the shader fixture's parity programs still draw. The look rows
    // must come from `resolved=false` constants: this twin samples bin centres and carries no pixel noise.
    const float (*look)[4] = nullptr;
    unsigned far_bins = 40; // the far bins of both laws (fog_far_bins; the 24-bin variant was removed on 2026-09-25)
    std::function<double(const double view_position[3])> visibility;                   // empty: 1
    mutable unsigned seam_xy_samples[kLevelCount]{}, lane_wrap_samples[kLevelCount]{}; // base node at storage 127
};
struct Result {
    double S[3], T;
};
// look_wave: the shader's parabolic sine of period 1, range [-1,1].
inline double look_wave(double x) {
    const double f = x - std::floor(x) - .5;
    return f * (8 - 16 * std::fabs(f));
}
inline double look_density(const float k[][4], double rho, double cover) {
    const double x = std::min(std::max((rho - k[0][0] - cover) * k[0][1], 0.), 1.);
    return x > 0 ? std::pow(x, double(k[0][2])) : 0.;
}
inline double level_sample(const Setup& s, int level, const double p[3]) {
    const double delta = kLevelDelta[level];
    std::int64_t b[3];
    double f[3];
    for (int a = 0; a < 3; ++a) {
        const double q = p[a] / delta;
        b[a] = std::int64_t(std::floor(q));
        f[a] = q - double(b[a]);
    }
    const bool seam_xy = storage_index(b[0]) == 127 || storage_index(b[1]) == 127, seam_z = storage_index(b[2]) == 127;
    s.seam_xy_samples[level] += seam_xy;
    s.lane_wrap_samples[level] += seam_z;
    double value = 0;
    for (int c = 0; c < 8; ++c) {
        const int dx = c & 1, dy = (c >> 1) & 1, dz = c >> 2;
        const NodeKey key{b[0] + dx, b[1] + dy, b[2] + dz};
        double word = half_to_float(node_word(delta, key, s.offset));
        if (s.break_seam &&
            ((dx && storage_index(b[0]) == 127) || (dy && storage_index(b[1]) == 127) || (dz && seam_z)))
            word = 0;
        value += word * (dx ? f[0] : 1 - f[0]) * (dy ? f[1] : 1 - f[1]) * (dz ? f[2] : 1 - f[2]);
    }
    return value;
}
inline Result look_march(const Setup&, const double view[3], double geometry_depth);
// `view` is the unnormalised view ray with z = 1; geometry_depth <= 0 marks sky.
inline Result march(const Setup& s, const double view[3], double geometry_depth) {
    if (s.look) return look_march(s, view, geometry_depth);
    const double view_length = std::sqrt(view[0] * view[0] + view[1] * view[1] + 1);
    const double distance = geometry_depth > 0 ? std::min(geometry_depth * view_length, kTaperEnd) : kTaperEnd;
    double direction[3], length = 0;
    for (int a = 0; a < 3; ++a) {
        direction[a] = s.inverse[a][0] * view[0] + s.inverse[a][1] * view[1] + s.inverse[a][2] * view[2];
        length += direction[a] * direction[a];
    }
    length = std::sqrt(length);
    double cosine = 0;
    for (int a = 0; a < 3; ++a) {
        direction[a] /= length;
        cosine += direction[a] * s.sun[a];
    }
    const double base = s.phase[0] - s.phase[1] * cosine, phase = s.phase[2] / (4 * base * std::sqrt(base));
    const double near_step = std::min(distance, 12000.) / 24, far_step = std::max(distance - 12000., 0.) / 40;
    double lit = 0, T = 1;
    for (int i = 0; i < 64; ++i) {
        const double ds = i < 24 ? near_step : far_step;
        if (!(ds > 0)) continue;
        const double at = (i < 24 ? near_step * i : 12000. + far_step * (i - 24)) + .5 * ds;
        const LodWeights w = lod_weights(at);
        const double lambda = w.lambda * s.ready_fine;
        double p[3], view_position[3];
        for (int a = 0; a < 3; ++a) {
            p[a] = s.camera[a] + direction[a] * at;
            view_position[a] = view[a] / view_length * at;
        }
        double rho = 0;
        if (lambda > 0) rho += lambda * level_sample(s, 0, p);
        if (lambda < 1) rho += (1 - lambda) * level_sample(s, 1, p);
        rho *= w.taper;
        const double a = 1 - std::exp(-s.sigma * rho * ds);
        const double visible = rho > 0 && s.visibility ? s.visibility(view_position) : 1;
        lit += T * a * visible;
        T *= 1 - a;
    }
    Result r;
    r.T = T;
    for (int c = 0; c < 3; ++c) r.S[c] = lit * phase * s.radiance[c] * s.chroma[c];
    return r;
}
// The look law of src/fog/fog_density_field_inc.h under FOG_LOOK, at the bin centres (no pixel offsets):
// density remap over the oblique coverage waves and the domain warp, the two-lobe phase, the coloured
// ambient, the half-extinction lift octave and the one-tap Beer-powder sun-ward self-shadow.
inline Result look_march(const Setup& s, const double view[3], double geometry_depth) {
    const float (*k)[4] = s.look;
    const double view_length = std::sqrt(view[0] * view[0] + view[1] * view[1] + 1);
    const double distance = std::min(geometry_depth > 0 ? geometry_depth * view_length : double(kTaperEnd),
                                     std::min(double(kTaperEnd), double(k[0][3])));
    double direction[3], length = 0;
    for (int a = 0; a < 3; ++a) {
        direction[a] = s.inverse[a][0] * view[0] + s.inverse[a][1] * view[1] + s.inverse[a][2] * view[2];
        length += direction[a] * direction[a];
    }
    length = std::sqrt(length);
    double cosine = 0;
    for (int a = 0; a < 3; ++a) {
        direction[a] /= length;
        cosine += direction[a] * s.sun[a];
    }
    // c22.xyz of the pass: the camera modulo the fine window (65536 units), centred, in float32.
    double local[3];
    for (int a = 0; a < 3; ++a) local[a] = double(float(s.camera[a] - 65536. * std::floor(s.camera[a] / 65536. + .5)));
    const double near_step = std::min(distance, 12000.) / 24, far_step = std::max(distance - 12000., 0.) / s.far_bins;
    double sun_lit = 0, lift = 0, T = 1;
    for (int i = 0; i < 24 + int(s.far_bins); ++i) {
        const double ds = i < 24 ? near_step : far_step;
        if (!(ds > 0)) continue;
        const double at = (i < 24 ? near_step * i : 12000. + far_step * (i - 24)) + .5 * ds;
        const double lambda = lod_weights(at).lambda * s.ready_fine;
        double world[3], ray[3], p[3], warped[3], view_position[3];
        for (int a = 0; a < 3; ++a) world[a] = local[a] + direction[a] * at;
        const double wave1[3] = {look_wave(world[1] * k[9][0]), look_wave(world[2] * k[9][0]),
                                 look_wave(world[0] * k[9][0])};
        const double wave2[3] = {look_wave(world[2] * k[9][2]), look_wave(world[0] * k[9][2]),
                                 look_wave(world[1] * k[9][2])};
        for (int a = 0; a < 3; ++a) {
            ray[a] = direction[a] * at + wave1[a] * k[9][1] + wave2[a] * k[9][3];
            p[a] = s.camera[a] + ray[a];
            warped[a] = (local[a] + ray[a]) / 65536.;
            view_position[a] = view[a] / view_length * at;
        }
        static const double waves[3][3] = {{1, -2, 1}, {2, 1, -1}, {-1, 1, 2}};
        double cover = 0;
        for (const auto& w : waves) cover += look_wave(warped[0] * w[0] + warped[1] * w[1] + warped[2] * w[2]);
        cover *= k[10][0];
        double rho = 0;
        if (lambda > 0) rho += lambda * level_sample(s, 0, p);
        if (lambda < 1) rho += (1 - lambda) * level_sample(s, 1, p);
        const double e = std::min(std::max((at - k[10][1]) * k[10][2], 0.), 1.);
        rho = look_density(k, rho, cover) * (1 - e * e * (3 - 2 * e));
        const double step = s.sigma * rho * ds, a = 1 - std::exp(-step), a2 = 1 - std::exp(-.5 * step);
        double shaft = 1, sunward = 1;
        if (rho > 0) {
            if (s.visibility) shaft = s.visibility(view_position);
            double tap[3];
            for (int c = 0; c < 3; ++c) tap[c] = p[c] + s.sun[c] * k[7][0];
            const double r = look_density(k, level_sample(s, 1, tap), cover) * k[7][1];
            const double tau = s.sigma * k[6][0] * r;
            sunward = std::exp(-tau) * (1 - k[6][1] * std::exp(-2 * (tau + s.sigma * k[6][0] * rho * k[7][0])));
        }
        sun_lit += T * a * sunward * (k[1][3] + (1 - k[1][3]) * shaft);
        lift += T * a2 * (k[3][3] + (1 - k[3][3]) * shaft);
        T *= 1 - a;
    }
    const double lobe0 = k[4][0] - k[4][1] * cosine, lobe1 = k[5][0] - k[5][1] * cosine;
    const double phase = k[4][2] / (lobe0 * std::sqrt(lobe0)) + k[5][2] / (lobe1 * std::sqrt(lobe1));
    Result r;
    r.T = T;
    for (int c = 0; c < 3; ++c) {
        const double ambient = k[2][c] + (k[3][c] - k[2][c]) * (.5 + .5 * cosine);
        r.S[c] = k[1][c] * (s.radiance[c] * (phase * sun_lit + k[2][3] * lift) + ambient * (1 - T));
    }
    return r;
}
// fog_apply of the composite and repair programs; `extinction` is the look's per-channel exponent of T (1: scalar).
inline double apply(double scene, double S, double T, double gamma, double extinction = 1) {
    const double Tk = extinction == 1 ? T : std::pow(std::max(T, 1e-6), extinction);
    return std::pow(std::max(std::pow(std::max(scene, 0.), gamma) * Tk + S, 0.), 1 / gamma);
}
} // namespace fog_cpu
