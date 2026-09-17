#pragma once
// Cascade-0 light projection of the one-cascade depth replay
// (docs/architecture/shadow-replay-gates.md, section 2, "Target and projection").
// Pure arithmetic on the route's CameraState latch, the submitted clip rows
// and the world sun direction; no device access. Row convention throughout:
// a 4x4 is four dp4 rows applied to (x, y, z, 1) as the vertex program does.
#include <cmath>
#include <cstdint>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#include "camera_reprojection.h"

namespace x3m::renderer {
// The own-ship cascade: centred on the camera position plus forward x
// forward_offset, half-extent in sun-space x/y, z within +-depth_half_range,
// texel-snapped in sun space (the sun is world-fixed, so snapping removes
// camera-translation swim). Production defaults per the note; half_extent
// (X3M_SHADOW_REPLAY_EXTENT), depth_half_range (X3M_SHADOW_REPLAY_DEPTH_HALF)
// and size (X3M_SHADOW_REPLAY_SIZE) are read once at device creation within
// the ranges below; the seam fixture narrows them to its unit-size geometry.
// The world texel is 2 half_extent / size (legacy-sun-application.md, section 2).
constexpr float shadow_replay_extent_default = 250.f, shadow_replay_extent_min = 50.f, shadow_replay_extent_max = 4000.f;
constexpr float shadow_replay_depth_half_default = 512.f, shadow_replay_depth_half_min = 128.f, shadow_replay_depth_half_max = 8192.f;
constexpr unsigned shadow_replay_size_default = 1024, shadow_replay_size_min = 64, shadow_replay_size_max = 4096;
constexpr float shadow_replay_forward_offset_default = 128.f;
struct ShadowReplayCascade {
    float half_extent = shadow_replay_extent_default, forward_offset = shadow_replay_forward_offset_default, depth_half_range = shadow_replay_depth_half_default;
    unsigned size = shadow_replay_size_default;
};
inline double shadow_replay_world_texel(const ShadowReplayCascade& c) noexcept { return c.size ? 2. * double(c.half_extent) / double(c.size) : 0.; }
struct ShadowReplayBasis {
    bool valid = false;
    float right[3]{}, up[3]{}, forward[3]{}; // sun-space axes in world space (forward = direction the light travels)
    float center[3]{};                        // snapped cascade centre in world space
};
// No libm on the i686 build (check_no_x87.py): GCC's std::sqrt(double) keeps
// an errno call whose result returns on the x87 stack, and floor/fabs are x87
// routines there; sqrtsd and a cvttsd2si truncation stay in SSE (host builds
// without SSE2 use <cmath>). The snap is skipped beyond +-2^30 texels.
inline double sqrt_sd(double x) noexcept {
#if defined(__SSE2__)
    return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_set_sd(x), _mm_set_sd(x)));
#else
    return std::sqrt(x);
#endif
}
inline double snap_floor(double v) noexcept {
    if (!(v > -1073741824. && v < 1073741824.)) return v;
    const double t = double(std::int32_t(v));
    return t > v ? t - 1. : t;
}
// World sun direction validity: finite, unit within 5 % (the engine writes a
// normalized object->light vector quantized to 1/65536, w = 0).
inline bool shadow_replay_sun_valid(const float sun[4]) noexcept {
    for (unsigned i = 0; i < 3; ++i) if (!std::isfinite(sun[i])) return false;
    const double n = sqrt_sd(double(sun[0]) * sun[0] + double(sun[1]) * sun[1] + double(sun[2]) * sun[2]);
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
    n = sqrt_sd(n);
    for (double& v : f) v /= n;
    double hint[3] = {0, 1, 0};
    if (f[1] > .99 || f[1] < -.99) { hint[0] = 1; hint[1] = 0; } // no std::fabs: the mingw build emits x87 fabs for it
    double right[3] = {hint[1] * f[2] - hint[2] * f[1], hint[2] * f[0] - hint[0] * f[2], hint[0] * f[1] - hint[1] * f[0]};
    double rn = sqrt_sd(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
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
    cx = snap_floor(cx / texel + .5) * texel; cy = snap_floor(cy / texel + .5) * texel;
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
        if (!std::isfinite(M[i][j]) || M[i][j] > 1e15 || M[i][j] < -1e15) return false;
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
            if (!std::isfinite(m) || m > 1e15 || m < -1e15) return false;
            out[a * 4 + j] = float(m);
        }
    }
    return true;
}
// Draw-time caster test (shadow-replay-gates.md, "Casters by bounds"): the
// eight corners of the draw's object-space AABB through the draw's clip rows
// (x, y, w only: p_view = (clip.x / m00, clip.y / m11, clip.w)) and the
// frame's view -> sun rows (shadow_replay_view_rows); the corners' sun-space
// AABB meets the map box when it overlaps [-1, 1]^2 x [0, 1]. Conservative
// for a rotated box. 1 meets, 0 misses, -1 unknown (nonfinite input).
inline int shadow_replay_bounds_verdict(const CameraState& camera, const float rows[16], const float view_rows[12],
                                        const float lo[3], const float hi[3]) noexcept {
    if (!camera.valid || !rows || !view_rows || !lo || !hi || !(camera.m00 > 0.f) || !(camera.m11 > 0.f)) return -1;
    float smin[3] = {3.4028235e38f, 3.4028235e38f, 3.4028235e38f}, smax[3] = {-3.4028235e38f, -3.4028235e38f, -3.4028235e38f};
    for (unsigned corner = 0; corner < 8; ++corner) {
        const float x = (corner & 1) ? hi[0] : lo[0], y = (corner & 2) ? hi[1] : lo[1], z = (corner & 4) ? hi[2] : lo[2];
        const float cx = rows[0] * x + rows[1] * y + rows[2] * z + rows[3];
        const float cy = rows[4] * x + rows[5] * y + rows[6] * z + rows[7];
        const float cw = rows[12] * x + rows[13] * y + rows[14] * z + rows[15];
        const float v[3] = {cx / camera.m00, cy / camera.m11, cw};
        for (unsigned a = 0; a < 3; ++a) {
            const float s = view_rows[a * 4] * v[0] + view_rows[a * 4 + 1] * v[1] + view_rows[a * 4 + 2] * v[2] + view_rows[a * 4 + 3];
            if (!std::isfinite(s)) return -1;
            if (s < smin[a]) smin[a] = s;
            if (s > smax[a]) smax[a] = s;
        }
    }
    const bool meets = smax[0] >= -1.f && smin[0] <= 1.f && smax[1] >= -1.f && smin[1] <= 1.f && smax[2] >= 0.f && smin[2] <= 1.f;
    return meets ? 1 : 0;
}
} // namespace x3m::renderer
