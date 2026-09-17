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
// forward_offset, half-extent in sun-space x/y, z within the depth range,
// texel-snapped in sun space (the sun is world-fixed, so snapping removes
// camera-translation swim). Production defaults per the note; half_extent
// (X3M_SHADOW_REPLAY_EXTENT), the depth half range (X3M_SHADOW_REPLAY_DEPTH_HALF)
// and size (X3M_SHADOW_REPLAY_SIZE) are read once at device creation within
// the ranges below; the seam fixture narrows them to its unit-size geometry.
// The world texel is 2 half_extent / size (legacy-sun-application.md, section 2).
constexpr float shadow_replay_extent_default = 250.f, shadow_replay_extent_min = 50.f, shadow_replay_extent_max = 4000.f;
constexpr float shadow_replay_depth_half_default = 512.f, shadow_replay_depth_half_min = 128.f, shadow_replay_depth_half_max = 8192.f;
constexpr unsigned shadow_replay_size_default = 1024, shadow_replay_size_min = 64, shadow_replay_size_max = 4096;
constexpr float shadow_replay_forward_offset_default = 128.f;
// The sun-space depth range is asymmetric (docs/architecture/shadow-cascades.md,
// "Depth range towards the light"): z = 0 lies depth_toward_light units from
// the centre towards the light, z = 1 depth_behind units beyond it. The
// single-map path keeps both at the half range (set_depth_half), which is
// bit-identical to the former symmetric law (L + R = 2 D exactly in double).
struct ShadowReplayCascade {
    float half_extent = shadow_replay_extent_default, forward_offset = shadow_replay_forward_offset_default;
    float depth_toward_light = shadow_replay_depth_half_default, depth_behind = shadow_replay_depth_half_default;
    unsigned size = shadow_replay_size_default;
    void set_depth_half(float half) noexcept { depth_toward_light = depth_behind = half; }
    double depth_range() const noexcept { return double(depth_toward_light) + double(depth_behind); }
    double depth_half() const noexcept { return .5 * depth_range(); } // what sun_shadow_apply_bias doubles again
};
inline double shadow_replay_world_texel(const ShadowReplayCascade& c) noexcept { return c.size ? 2. * double(c.half_extent) / double(c.size) : 0.; }
struct ShadowReplayBasis {
    bool valid = false;
    float right[3]{}, up[3]{}, forward[3]{}; // sun-space axes in world space (forward = direction the light travels)
    float center[3]{};                        // snapped cascade centre in world space
    // The same in double: what every row is built from. Narrowing the snapped
    // centre to float quantises it (0.0078 units at |c| = 7e4, a visible swim
    // of a 0.06-unit texel), so the centre and the axes are folded into the
    // rows in double and only the finished rows are narrowed; the floats above
    // are for the logs and the seam readback.
    double axes[3][3]{};                      // right, up, forward
    double center_d[3]{};
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
    if (!(cascade.half_extent > 0.f) || !(cascade.depth_toward_light > 0.f) || !(cascade.depth_behind > 0.f) || !std::isfinite(cascade.forward_offset)) return false;
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
        out.center_d[i] = c; out.axes[0][i] = right[i]; out.axes[1][i] = up[i]; out.axes[2][i] = f[i];
    }
    out.valid = true;
    return true;
}
// The per-draw light matrix: clip = rows . pos; p_view = (clip.x / m00,
// clip.y / m11, clip.w); world = (p_view - t) R^T; sun-space NDC x, y in
// [-1, 1] over the half-extent, z in [0, 1] over [-depth_toward_light, +depth_behind].
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
    const double E = double(cascade.half_extent), L = double(cascade.depth_toward_light), R = cascade.depth_range();
    double S[4][4] = {};
    const auto& axes = basis.axes;
    for (unsigned a = 0; a < 3; ++a) {
        double dot = 0;
        for (unsigned j = 0; j < 3; ++j) dot += axes[a][j] * basis.center_d[j];
        const double scale = a == 2 ? 1. / R : 1. / E;
        for (unsigned j = 0; j < 3; ++j) S[a][j] = axes[a][j] * scale;
        S[a][3] = a == 2 ? (L - dot) / R : -dot / E;
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
    const double E = double(cascade.half_extent), L = double(cascade.depth_toward_light), R = cascade.depth_range();
    if (!(E > 0.) || !(L > 0.) || !(R > L)) return false;
    const auto& axes = basis.axes;
    for (unsigned a = 0; a < 3; ++a) {
        double dot = 0;
        for (unsigned j = 0; j < 3; ++j) dot += axes[a][j] * basis.center_d[j];
        const double scale = a == 2 ? 1. / R : 1. / E, offset = a == 2 ? (L - dot) / R : -dot / E;
        for (unsigned j = 0; j < 4; ++j) {
            double m = j == 3 ? offset : 0.;
            for (unsigned k = 0; k < 3; ++k) m += axes[a][k] * scale * W[k][j];
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
// AABB meets the map box when it overlaps [-1, 1]^2 and does not lie wholly
// beyond z = 1: the light side is open, because the replay pancakes a caster
// nearer the light than the near plane onto it (it still shadows the box).
// Conservative for a rotated box. 1 meets, 0 misses, -1 unknown (nonfinite input).
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
    const bool meets = smax[0] >= -1.f && smin[0] <= 1.f && smax[1] >= -1.f && smin[1] <= 1.f && smin[2] <= 1.f;
    return meets ? 1 : 0;
}

// ---- cascades (docs/architecture/shadow-cascades.md) --------------------------
// N <= 4 camera-centred, texel-snapped cascades sharing the sun basis. Every
// default is a single named constant; the launcher options override them
// (X3M_SHADOW_CASCADES and companions, tools/manage.py). Cascade 0 keeps the
// own-ship forward offset scaled to its extent; the others centre on the
// camera. Every cascade's depth range reaches depth_light_factor x the largest
// extent towards the light (so the smallest cascade containing a pixel contains
// every occluder of it) and max(the single-map half range, depth_behind_factor
// x its own extent) behind its centre.
constexpr unsigned shadow_cascade_max = 4;
constexpr float shadow_cascade_extent_defaults[shadow_cascade_max] = {250.f, 1500.f, 7500.f, 25000.f}; // the intended set
constexpr float shadow_cascade_extent_min = 50.f, shadow_cascade_extent_max = 50000.f;
constexpr unsigned shadow_cascade_size_default = 4096;
constexpr unsigned shadow_cascade_cap_defaults[shadow_cascade_max] = {128, 512, 1024, 1024};
constexpr unsigned shadow_cascade_cap_max = 1024; // = shadow_replay::record_capacity (one record list)
constexpr unsigned shadow_cascade_budget_default = 640, shadow_cascade_budget_min = 1, shadow_cascade_budget_max = 4096; // draw issues per frame
constexpr float shadow_cascade_depth_light_factor = 2.f, shadow_cascade_depth_behind_factor = 2.f;
constexpr float shadow_cascade_select_margin = .95f; // a pixel belongs to the first cascade with max(|x|, |y|) <= margin (room for the 3x3 kernel)
constexpr float shadow_cascade_blend_band = .10f;    // the outer band of that margin blends into the next cascade (the last one fades to lit)
struct ShadowCascadeSet {
    unsigned count = 0; // 0: the single-map path
    ShadowReplayCascade cascades[shadow_cascade_max]{};
    unsigned caps[shadow_cascade_max]{};
    unsigned budget = shadow_cascade_budget_default;
};
// Builds the set from ascending half-extents; sizes/caps may be null (defaults).
// `checked` false is the fixture seam (unit-size geometry below the production
// minimum: no forward offset, depth behind exactly the factor). False on a
// non-ascending, nonfinite or out-of-range input.
inline bool shadow_cascade_set(const float* extents, unsigned count, const unsigned* sizes, const unsigned* caps, unsigned budget,
                               ShadowCascadeSet& out, bool checked = true) noexcept {
    out = ShadowCascadeSet{};
    if (!extents || count < 1 || count > shadow_cascade_max) return false;
    if (budget < shadow_cascade_budget_min || budget > shadow_cascade_budget_max) return false;
    for (unsigned i = 0; i < count; ++i) {
        const float e = extents[i];
        if (!std::isfinite(e) || !(e > 0.f) || (i && !(e > extents[i - 1]))) return false;
        if (checked && (e < shadow_cascade_extent_min || e > shadow_cascade_extent_max)) return false;
        const unsigned size = sizes ? sizes[i] : shadow_cascade_size_default, cap = caps ? caps[i] : shadow_cascade_cap_defaults[i];
        if (size < shadow_replay_size_min || size > shadow_replay_size_max || cap < 1 || cap > shadow_cascade_cap_max) return false;
        auto& c = out.cascades[i];
        c.half_extent = e; c.size = size; out.caps[i] = cap;
        const float forward = e * (shadow_replay_forward_offset_default / shadow_replay_extent_default);
        c.forward_offset = i || !checked ? 0.f : forward < shadow_replay_forward_offset_default ? forward : shadow_replay_forward_offset_default;
        const float behind = e * shadow_cascade_depth_behind_factor;
        c.depth_behind = checked && behind < shadow_replay_depth_half_default ? shadow_replay_depth_half_default : behind;
        c.depth_toward_light = extents[count - 1] * shadow_cascade_depth_light_factor;
    }
    out.count = count; out.budget = budget;
    return true;
}
// The far-cascade policy: the last cascade of a multi-cascade set replays every
// frame while the frame's issues fit the budget, otherwise on even frames only
// and always in full; the others replay every frame.
inline bool shadow_cascade_replays(unsigned cascade, unsigned count, unsigned issues, unsigned budget, std::uint64_t frame) noexcept {
    return count < 2 || cascade + 1 != count || issues <= budget || (frame & 1u) == 0;
}
// The frame's box test for every cascade at once: the view -> sun-space rows in
// world units relative to the camera position (three dp3; the camera position
// maps to 0, so the magnitudes stay near the extents) and each cascade's box in
// those coordinates. One corner transform per draw, then 6 compares per cascade.
struct ShadowCascadeBounds {
    unsigned count = 0;
    float rows[9]{};
    float lo[shadow_cascade_max][3]{}, hi[shadow_cascade_max][3]{};
};
inline bool shadow_cascade_bounds(const CameraState& camera, const float sun[4], const ShadowCascadeSet& set, ShadowCascadeBounds& out) noexcept {
    out = ShadowCascadeBounds{};
    if (!set.count || set.count > shadow_cascade_max) return false;
    double position[3];
    for (unsigned i = 0; i < 3; ++i) { position[i] = 0; for (unsigned j = 0; j < 3; ++j) position[i] -= double(camera.t[j]) * double(camera.r[i * 3 + j]); }
    for (unsigned c = 0; c < set.count; ++c) {
        ShadowReplayBasis basis{};
        if (!shadow_replay_basis(camera, sun, set.cascades[c], basis)) return false;
        const auto& axes = basis.axes;
        for (unsigned a = 0; a < 3; ++a) {
            if (!c) for (unsigned j = 0; j < 3; ++j) { // sun_rel[a] = sum_j (sum_k axes[a][k] r[k*3+j]) view_j
                double m = 0;
                for (unsigned k = 0; k < 3; ++k) m += axes[a][k] * double(camera.r[k * 3 + j]);
                out.rows[a * 3 + j] = float(m);
            }
            double centre = 0;
            for (unsigned k = 0; k < 3; ++k) centre += axes[a][k] * (basis.center_d[k] - position[k]);
            const auto& s = set.cascades[c];
            const double below = a == 2 ? double(s.depth_toward_light) : double(s.half_extent), above = a == 2 ? double(s.depth_behind) : double(s.half_extent);
            if (!std::isfinite(centre)) return false;
            out.lo[c][a] = float(centre - below); out.hi[c][a] = float(centre + above);
        }
    }
    out.count = set.count;
    return true;
}
// Bit i set: the draw's object-space AABB (eight corners through its clip rows,
// as shadow_replay_bounds_verdict) meets cascade i's box, whose light side is
// open as there. -1: unknown.
inline int shadow_cascade_bounds_mask(const CameraState& camera, const float rows[16], const ShadowCascadeBounds& bounds,
                                      const float lo[3], const float hi[3]) noexcept {
    if (!camera.valid || !rows || !lo || !hi || !bounds.count || !(camera.m00 > 0.f) || !(camera.m11 > 0.f)) return -1;
    float smin[3] = {3.4028235e38f, 3.4028235e38f, 3.4028235e38f}, smax[3] = {-3.4028235e38f, -3.4028235e38f, -3.4028235e38f};
    const float ix = 1.f / camera.m00, iy = 1.f / camera.m11;
    for (unsigned corner = 0; corner < 8; ++corner) {
        const float x = (corner & 1) ? hi[0] : lo[0], y = (corner & 2) ? hi[1] : lo[1], z = (corner & 4) ? hi[2] : lo[2];
        const float v[3] = {(rows[0] * x + rows[1] * y + rows[2] * z + rows[3]) * ix, (rows[4] * x + rows[5] * y + rows[6] * z + rows[7]) * iy,
                            rows[12] * x + rows[13] * y + rows[14] * z + rows[15]};
        for (unsigned a = 0; a < 3; ++a) {
            const float s = bounds.rows[a * 3] * v[0] + bounds.rows[a * 3 + 1] * v[1] + bounds.rows[a * 3 + 2] * v[2];
            if (!std::isfinite(s)) return -1;
            if (s < smin[a]) smin[a] = s;
            if (s > smax[a]) smax[a] = s;
        }
    }
    int mask = 0;
    for (unsigned c = 0; c < bounds.count; ++c) {
        const float* l = bounds.lo[c]; const float* h = bounds.hi[c];
        if (smax[0] >= l[0] && smin[0] <= h[0] && smax[1] >= l[1] && smin[1] <= h[1] && smin[2] <= h[2]) mask |= 1 << c; // the light side is open (pancaked)
    }
    return mask;
}
// The per-draw light rows of several cascades without repeating the matrix
// products: `base` is the draw's object -> absolute sun-space rows in world
// units (double; the basis axes are shared by every cascade of a frame);
// shadow_cascade_light_rows scales and offsets them into one cascade's NDC
// (c0-c2 of the authored vertex program; c3 is (0, 0, 0, 1): the projection is orthographic).
inline bool shadow_cascade_draw_rows(const CameraState& camera, const float rows[16], const ShadowReplayBasis& basis, double base[3][4]) noexcept {
    if (!camera.valid || !basis.valid || !rows || !base) return false;
    for (unsigned i = 0; i < 16; ++i) if (!std::isfinite(rows[i])) return false;
    double WA[3][4];
    for (unsigned i = 0; i < 3; ++i) for (unsigned k = 0; k < 4; ++k) {
        // world_i = sum_j (view_j - t_j) r[i*3+j], view = (clip.x / m00, clip.y / m11, clip.w)
        const double vx = double(rows[k]) / camera.m00, vy = double(rows[4 + k]) / camera.m11, vz = double(rows[12 + k]);
        double m = vx * double(camera.r[i * 3]) + vy * double(camera.r[i * 3 + 1]) + vz * double(camera.r[i * 3 + 2]);
        if (k == 3) for (unsigned j = 0; j < 3; ++j) m -= double(camera.t[j]) * double(camera.r[i * 3 + j]);
        WA[i][k] = m;
    }
    const auto& axes = basis.axes;
    for (unsigned a = 0; a < 3; ++a) for (unsigned k = 0; k < 4; ++k) {
        const double m = axes[a][0] * WA[0][k] + axes[a][1] * WA[1][k] + axes[a][2] * WA[2][k];
        if (!std::isfinite(m)) return false;
        base[a][k] = m;
    }
    return true;
}
inline bool shadow_cascade_light_rows(const double base[3][4], const ShadowReplayBasis& basis, const ShadowReplayCascade& cascade, float out[12]) noexcept {
    if (!basis.valid || !base || !out) return false;
    const double E = double(cascade.half_extent), L = double(cascade.depth_toward_light), R = cascade.depth_range();
    if (!(E > 0.) || !(L > 0.) || !(R > L)) return false;
    const auto& axes = basis.axes;
    for (unsigned a = 0; a < 3; ++a) {
        double dot = 0;
        for (unsigned j = 0; j < 3; ++j) dot += axes[a][j] * basis.center_d[j];
        const double scale = a == 2 ? 1. / R : 1. / E, offset = a == 2 ? (L - dot) / R : -dot / E;
        for (unsigned k = 0; k < 4; ++k) {
            const double m = base[a][k] * scale + (k == 3 ? offset : 0.);
            if (!std::isfinite(m) || m > 1e15 || m < -1e15) return false;
            out[a * 4 + k] = float(m);
        }
    }
    return true;
}
} // namespace x3m::renderer
