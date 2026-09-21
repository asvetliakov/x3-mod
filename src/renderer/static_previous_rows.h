#pragma once
// Previous clip rows of a draw under the static-world assumption
// (X3M_TAA_UNMATCHED_STATIC, docs/architecture/temporal-integration.md,
// "Unmatched draws: static-world previous rows"). Pure arithmetic: no D3D, no
// Windows headers; shared by the route, the fixture and the host unit test
// (verification/analysis/test_static_previous_rows.py).
//
// `rows` are the draw's submitted object->clip rows (column-vector: clip_i =
// rows[4i..4i+3] . (pos, 1)) under the CURRENT camera latch. With the engine's
// conventions (camera_reprojection.h: row-vector view, clip.x = m00 vx + m20 vz,
// clip.y = m11 vy + m21 vz, clip.w = vz) the view-space rows follow from the
// clip rows, the world placement from the current view, and the previous rows
// from the same placement through the PREVIOUS view and projection.
// The depth row is per-submission (P[10]/P[14] are scratch in the engine), so
// its law z = a vz + b is recovered from the draw's own rows and reapplied; a
// draw whose depth row is not of that form, or whose view-z row is degenerate,
// is refused (the caller keeps the missing-history sentinel).
#include "camera_reprojection.h"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace x3m::renderer {
namespace static_rows_detail {
// SSE2-only helpers: this header runs on the draw path, where check_no_x87.py
// refuses the MinGW <math.h> x87 fabs inline and any libm call returning in st(0).
inline double magnitude(double x) noexcept {
    std::uint64_t bits; std::memcpy(&bits, &x, sizeof bits);
    bits &= ~(std::uint64_t(1) << 63);
    std::memcpy(&x, &bits, sizeof x);
    return x;
}
} // namespace static_rows_detail
// cos of an angle in [0, 180] degrees by half-angle Taylor series (error below
// 1e-9), so a rotation bound can be compared as a cosine without acos/cos calls.
inline double cosine_of_degrees(double degrees) noexcept {
    const double h = degrees * (3.14159265358979323846 / 360.), h2 = h * h; // half angle, at most pi/2
    double term = h, sine = h;
    for (unsigned n = 1; n <= 9; ++n) { term *= -h2 / double((2 * n) * (2 * n + 1)); sine += term; }
    return 1. - 2. * sine * sine;
}
// True when the rotation between two views is at most the bound whose cosine is
// given: the rotation angle's cosine is (trace(Ra^T Rb) - 1) / 2.
inline bool camera_rotation_within(const CameraState& a, const CameraState& b, double cosine_bound) noexcept {
    double trace = 0;
    for (unsigned i = 0; i < 9; ++i) trace += double(a.r[i]) * double(b.r[i]);
    return (trace - 1.) * .5 >= cosine_bound;
}
inline bool static_previous_rows(const CameraState& current, const CameraState& previous,
                                 const float rows[16], float out[16]) noexcept {
    if (!current.valid || !previous.valid || !rows || !out) return false;
    if (!(current.m00 > 0.f) || !(current.m11 > 0.f)) return false;
    for (unsigned i = 0; i < 16; ++i) if (!std::isfinite(rows[i])) return false;
    // Depth law from the rows: the 3-vector parts of the z and w rows are parallel.
    unsigned pivot = 0; double scale = 0;
    for (unsigned k = 0; k < 3; ++k) { const double m = static_rows_detail::magnitude(double(rows[12 + k])); if (m > scale) { scale = m; pivot = k; } }
    if (!(scale > 1e-12)) return false;
    const double a = double(rows[8 + pivot]) / double(rows[12 + pivot]);
    for (unsigned k = 0; k < 3; ++k)
        if (static_rows_detail::magnitude(double(rows[8 + k]) - a * double(rows[12 + k])) > 1e-4 * (static_rows_detail::magnitude(a) + 1.) * scale) return false;
    const double b = double(rows[11]) - a * double(rows[15]);
    double result[16];
    for (unsigned k = 0; k < 4; ++k) {
        const double vz = double(rows[12 + k]);
        double view[3] = {(double(rows[k]) - double(current.m20) * vz) / double(current.m00),
                          (double(rows[4 + k]) - double(current.m21) * vz) / double(current.m11), vz};
        if (k == 3) for (unsigned j = 0; j < 3; ++j) view[j] -= double(current.t[j]);
        double world[3], prev[3];
        for (unsigned i = 0; i < 3; ++i) { world[i] = 0; for (unsigned j = 0; j < 3; ++j) world[i] += view[j] * double(current.r[i * 3 + j]); }
        for (unsigned j = 0; j < 3; ++j) { prev[j] = k == 3 ? double(previous.t[j]) : 0.; for (unsigned i = 0; i < 3; ++i) prev[j] += world[i] * double(previous.r[i * 3 + j]); }
        result[k] = double(previous.m00) * prev[0] + double(previous.m20) * prev[2];
        result[4 + k] = double(previous.m11) * prev[1] + double(previous.m21) * prev[2];
        result[8 + k] = a * prev[2] + (k == 3 ? b : 0.);
        result[12 + k] = prev[2];
    }
    for (unsigned i = 0; i < 16; ++i) {
        if (!std::isfinite(result[i]) || static_rows_detail::magnitude(result[i]) > 1e15) return false;
        out[i] = float(result[i]);
    }
    return true;
}
} // namespace x3m::renderer
