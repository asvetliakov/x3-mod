// CPU twin of src/fog/fog_density_field_inc.h's march for fixtures: double arithmetic,
// texel-exact trilinear reconstruction of the generator's FP16 nodes read straight from
// node_word (no atlas, no window, no c0 / uv convention). The caller hands in the view
// ray, so a reference built on this is independent of how a program derives its ray.
#pragma once
#include "../../src/fog/fog_density_generator.h"
#include <cmath>
#include <functional>
namespace fog_cpu {
using namespace x3m::fog;
struct Setup {
    double camera[3]{};             // world, render units
    double inverse[3][3]{};         // rows c4..c6: world = inverse * view
    double sun[3]{1, 0, 0};
    double sigma = 0, chroma[3]{1, 1, 1}, ready_fine = 1;
    double phase[3]{1.09, .6, .91}, radiance[3]{1, 1, 1};
    WorldOffset offset = kNoOffset;
    // Verification control: nodes on the far side of a storage seam (index 127 -> 0) read as
    // zero, which is what a sampler without the duplicate border / lane wrap would mix in.
    bool break_seam = false;
    std::function<double(const double view_position[3])> visibility; // empty: 1
    mutable unsigned seam_xy_samples[kLevelCount]{}, lane_wrap_samples[kLevelCount]{}; // base node at storage 127
};
struct Result { double S[3], T; };
inline double level_sample(const Setup& s, int level, const double p[3]) {
    const double delta = kLevelDelta[level];
    std::int64_t b[3]; double f[3];
    for (int a = 0; a < 3; ++a) { const double q = p[a] / delta; b[a] = std::int64_t(std::floor(q)); f[a] = q - double(b[a]); }
    const bool seam_xy = storage_index(b[0]) == 127 || storage_index(b[1]) == 127, seam_z = storage_index(b[2]) == 127;
    s.seam_xy_samples[level] += seam_xy; s.lane_wrap_samples[level] += seam_z;
    double value = 0;
    for (int c = 0; c < 8; ++c) {
        const int dx = c & 1, dy = (c >> 1) & 1, dz = c >> 2;
        const NodeKey key{b[0] + dx, b[1] + dy, b[2] + dz};
        double word = half_to_float(node_word(delta, key, s.offset));
        if (s.break_seam && ((dx && storage_index(b[0]) == 127) || (dy && storage_index(b[1]) == 127) || (dz && seam_z))) word = 0;
        value += word * (dx ? f[0] : 1 - f[0]) * (dy ? f[1] : 1 - f[1]) * (dz ? f[2] : 1 - f[2]);
    }
    return value;
}
// `view` is the unnormalised view ray with z = 1; geometry_depth <= 0 marks sky.
inline Result march(const Setup& s, const double view[3], double geometry_depth) {
    const double view_length = std::sqrt(view[0] * view[0] + view[1] * view[1] + 1);
    const double distance = geometry_depth > 0 ? std::min(geometry_depth * view_length, kTaperEnd) : kTaperEnd;
    double direction[3], length = 0;
    for (int a = 0; a < 3; ++a) { direction[a] = s.inverse[a][0] * view[0] + s.inverse[a][1] * view[1] + s.inverse[a][2] * view[2]; length += direction[a] * direction[a]; }
    length = std::sqrt(length);
    double cosine = 0;
    for (int a = 0; a < 3; ++a) { direction[a] /= length; cosine += direction[a] * s.sun[a]; }
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
        for (int a = 0; a < 3; ++a) { p[a] = s.camera[a] + direction[a] * at; view_position[a] = view[a] / view_length * at; }
        double rho = 0;
        if (lambda > 0) rho += lambda * level_sample(s, 0, p);
        if (lambda < 1) rho += (1 - lambda) * level_sample(s, 1, p);
        rho *= w.taper;
        const double a = 1 - std::exp(-s.sigma * rho * ds);
        const double visible = rho > 0 && s.visibility ? s.visibility(view_position) : 1;
        lit += T * a * visible;
        T *= 1 - a;
    }
    Result r; r.T = T;
    for (int c = 0; c < 3; ++c) r.S[c] = lit * phase * s.radiance[c] * s.chroma[c];
    return r;
}
// fog_apply of the composite and repair programs.
inline double apply(double scene, double S, double T, double gamma) { return std::pow(std::max(std::pow(std::max(scene, 0.), gamma) * T + S, 0.), 1 / gamma); }
}  // namespace fog_cpu
