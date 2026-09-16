#pragma once
// Cascade-0 light projection of the one-cascade depth replay
// (docs/architecture/shadow-replay-gates.md, section 2, "Target and projection").
// Pure arithmetic on the route's CameraState latch, the submitted clip rows
// and the world sun direction; no device access. Row convention throughout:
// a 4x4 is four dp4 rows applied to (x, y, z, 1) as the vertex program does.
#include <cmath>
#include <cstdint>
#include "camera_reprojection.h"

namespace x3m::renderer {
// The own-ship cascade: centred on the camera position plus forward x
// forward_offset, half-extent in sun-space x/y, z within +-depth_half_range,
// texel-snapped in sun space (the sun is world-fixed, so snapping removes
// camera-translation swim). Production constants per the note; the seam
// fixture narrows them to its unit-size geometry.
struct ShadowReplayCascade {
    float half_extent = 250.f, forward_offset = 128.f, depth_half_range = 512.f;
    unsigned size = 1024;
};
struct ShadowReplayBasis {
    bool valid = false;
    float right[3]{}, up[3]{}, forward[3]{}; // sun-space axes in world space (forward = direction the light travels)
    float center[3]{};                        // snapped cascade centre in world space
};
// World sun direction validity: finite, unit within 5 % (the engine writes a
// normalized object->light vector quantized to 1/65536, w = 0).
inline bool shadow_replay_sun_valid(const float sun[4]) noexcept {
    for (unsigned i = 0; i < 3; ++i) if (!std::isfinite(sun[i])) return false;
    const double n = std::sqrt(double(sun[0]) * sun[0] + double(sun[1]) * sun[1] + double(sun[2]) * sun[2]);
    return n > .95 && n < 1.05;
}
// Basis from the camera latch and the object->light direction. The camera
// position is -t R^T (row-vector view), forward is the third view axis.
inline bool shadow_replay_basis(const CameraState& camera, const float sun[4], const ShadowReplayCascade& cascade,
                                ShadowReplayBasis& out) noexcept {
    out = ShadowReplayBasis{};
    if (!camera.valid || !shadow_replay_sun_valid(sun) || !cascade.size) return false;
    if (!(cascade.half_extent > 0.f) || !(cascade.depth_half_range > 0.f) || !std::isfinite(cascade.forward_offset)) return false;
    double f[3], n = 0;
    for (unsigned i = 0; i < 3; ++i) { f[i] = -double(sun[i]); n += f[i] * f[i]; }
    n = std::sqrt(n);
    for (double& v : f) v /= n;
    double hint[3] = {0, 1, 0};
    if (std::fabs(f[1]) > .99) { hint[0] = 1; hint[1] = 0; }
    double right[3] = {hint[1] * f[2] - hint[2] * f[1], hint[2] * f[0] - hint[0] * f[2], hint[0] * f[1] - hint[1] * f[0]};
    double rn = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (!(rn > 1e-6)) return false;
    for (double& v : right) v /= rn;
    const double up[3] = {f[1] * right[2] - f[2] * right[1], f[2] * right[0] - f[0] * right[2], f[0] * right[1] - f[1] * right[0]};
    // Camera position and forward in world space (camera_reprojection.h convention).
    double position[3], forward[3];
    for (unsigned i = 0; i < 3; ++i) {
        position[i] = 0; forward[i] = double(camera.r[i * 3 + 2]);
        for (unsigned j = 0; j < 3; ++j) position[i] -= double(camera.t[j]) * double(camera.r[i * 3 + j]);
    }
    double center[3];
    for (unsigned i = 0; i < 3; ++i) center[i] = position[i] + forward[i] * double(cascade.forward_offset);
    // Snap the centre to the texel grid in sun space.
    const double texel = 2. * double(cascade.half_extent) / double(cascade.size);
    double cx = 0, cy = 0, cz = 0;
    for (unsigned i = 0; i < 3; ++i) { cx += center[i] * right[i]; cy += center[i] * up[i]; cz += center[i] * f[i]; }
    cx = std::floor(cx / texel + .5) * texel; cy = std::floor(cy / texel + .5) * texel;
    for (unsigned i = 0; i < 3; ++i) {
        const double c = right[i] * cx + up[i] * cy + f[i] * cz;
        if (!std::isfinite(c)) return false;
        out.center[i] = float(c); out.right[i] = float(right[i]); out.up[i] = float(up[i]); out.forward[i] = float(f[i]);
    }
    out.valid = true;
    return true;
}
// The per-draw light matrix: clip = rows . pos; p_view = (clip.x / m00,
// clip.y / m11, clip.w); world = (p_view - t) R^T; sun-space NDC x, y in
// [-1, 1] over the half-extent, z in [0, 1] over +-depth_half_range.
inline bool shadow_replay_light_rows(const CameraState& camera, const float rows[16], const ShadowReplayBasis& basis,
                                     const ShadowReplayCascade& cascade, float out[16]) noexcept {
    if (!camera.valid || !basis.valid || !rows || !out) return false;
    for (unsigned i = 0; i < 16; ++i) if (!std::isfinite(rows[i])) return false;
    // A: pos -> (view.x, view.y, view.z, 1).
    double A[4][4] = {};
    for (unsigned k = 0; k < 4; ++k) { A[0][k] = double(rows[k]) / camera.m00; A[1][k] = double(rows[4 + k]) / camera.m11; A[2][k] = double(rows[12 + k]); }
    A[3][3] = 1;
    // W: view -> world, w_i = sum_j (v_j - t_j) r[i*3+j].
    double W[4][4] = {};
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned j = 0; j < 3; ++j) { W[i][j] = double(camera.r[i * 3 + j]); W[i][3] -= double(camera.t[j]) * double(camera.r[i * 3 + j]); }
    }
    W[3][3] = 1;
    // S: world -> sun-space NDC.
    const double E = double(cascade.half_extent), D = double(cascade.depth_half_range);
    double S[4][4] = {};
    const float* axes[3] = {basis.right, basis.up, basis.forward};
    for (unsigned a = 0; a < 3; ++a) {
        double dot = 0;
        for (unsigned j = 0; j < 3; ++j) dot += double(axes[a][j]) * double(basis.center[j]);
        const double scale = a == 2 ? 1. / (2. * D) : 1. / E;
        for (unsigned j = 0; j < 3; ++j) S[a][j] = double(axes[a][j]) * scale;
        S[a][3] = a == 2 ? (D - dot) / (2. * D) : -dot / E;
    }
    S[3][3] = 1;
    double WA[4][4] = {}, M[4][4] = {};
    for (unsigned i = 0; i < 4; ++i) for (unsigned j = 0; j < 4; ++j) for (unsigned k = 0; k < 4; ++k) WA[i][j] += W[i][k] * A[k][j];
    for (unsigned i = 0; i < 4; ++i) for (unsigned j = 0; j < 4; ++j) for (unsigned k = 0; k < 4; ++k) M[i][j] += S[i][k] * WA[k][j];
    for (unsigned i = 0; i < 4; ++i) for (unsigned j = 0; j < 4; ++j) {
        if (!std::isfinite(M[i][j]) || std::fabs(M[i][j]) > 1e15) return false;
        out[i * 4 + j] = float(M[i][j]);
    }
    return true;
}
// The frame's view -> sun-space rows for the scene-end apply quad
// (docs/architecture/legacy-sun-application.md, section 2): S . W of the
// per-draw product above without the draw's clip rows, three dp4 rows applied
// to the view position (x, y, z, 1) giving sun-space NDC x, y and the
// normalized depth. out[12] row-major.
inline bool shadow_replay_view_rows(const CameraState& camera, const ShadowReplayBasis& basis, const ShadowReplayCascade& cascade,
                                    float out[12]) noexcept {
    if (!camera.valid || !basis.valid || !out) return false;
    double W[3][4] = {};
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned j = 0; j < 3; ++j) { W[i][j] = double(camera.r[i * 3 + j]); W[i][3] -= double(camera.t[j]) * double(camera.r[i * 3 + j]); }
    }
    const double E = double(cascade.half_extent), D = double(cascade.depth_half_range);
    if (!(E > 0.) || !(D > 0.)) return false;
    const float* axes[3] = {basis.right, basis.up, basis.forward};
    for (unsigned a = 0; a < 3; ++a) {
        double dot = 0;
        for (unsigned j = 0; j < 3; ++j) dot += double(axes[a][j]) * double(basis.center[j]);
        const double scale = a == 2 ? 1. / (2. * D) : 1. / E, offset = a == 2 ? (D - dot) / (2. * D) : -dot / E;
        for (unsigned j = 0; j < 4; ++j) {
            double m = j == 3 ? offset : 0.;
            for (unsigned k = 0; k < 3; ++k) m += double(axes[a][k]) * scale * W[k][j];
            if (!std::isfinite(m) || std::fabs(m) > 1e15) return false;
            out[a * 4 + j] = float(m);
        }
    }
    return true;
}
} // namespace x3m::renderer
