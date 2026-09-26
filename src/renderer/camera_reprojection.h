#pragma once
// Camera state of one engine view and the far-plane reprojection the temporal
// resolve's depth-sentinel policy 2 consumes. Pure arithmetic: no D3D, no
// Windows headers; shared by the route, the fixtures and the host unit test
// (verification/analysis/test_camera_reprojection.py).
//
// Conventions (docs/reverse-engineering/camera-state-and-frame-routine.md):
// the engine's projection and view buffers are 16 floats, row-major,
// ROW-VECTOR (v' = v * M), left-handed. view = world * V; V's upper-left 3x3
// is the rotation R (world axis i -> view axis j at r[i*3+j]); V[12..14] is the
// translation. P[0] = m00, P[5] = m11 are the projection scales, P[8] = m20,
// P[9] = m21 the off-center terms (zero unless a jitter is injected there) and
// P[11] = m23 = 1 makes w_clip = z_view. P[10]/P[14] are per-submission scratch:
// latched as m22/m32 for the depth law only (far gate, camera_depth_parallax).
//
// The resolve (src/temporal/resolve.hlsl) forms currentClip = (x, y, z, 1) in
// D3D NDC (x right, y UP) and applies clip_to_previous as four rows dotted
// with that vector (COLUMN-vector multiplication, row-major storage), then
// divides by w and reads z/w as the expected previous depth. The far-plane
// builder below produces exactly that matrix for a direction at infinity:
// translation is ignored (a background at infinity has no parallax; for a far
// but finite unrouted object the error is its parallax, documented as a limit).
#include <cmath>
#include <cstdint>
#include <limits>

namespace x3m::renderer {
struct CameraState {
    bool valid = false;
    float m00 = 0, m11 = 0, m20 = 0, m21 = 0; // projection P[0], P[5], P[8], P[9]
    float r[9]{};                             // view rotation V[0..2], V[4..6], V[8..10]
    float t[3]{};                             // view translation V[12..14]
    float m22 = 0, m32 = 0; // projection P[10], P[14]: depth = m22 + m32 / view z (far stabiliser gate,
                            // camera_depth_parallax)
    // The exact world-from-view basis of r, filled once per latch by camera_state_from_matrices (camera_world_basis
    // below). r is not to be edited afterwards; a hand-built state leaves wv_valid 0 and the accessor computes on
    // demand.
    std::uint32_t wv_valid = 0; // 32-bit: no padding before the doubles (the state is memcmp'd)
    double wv[9]{};
};
// World-from-view basis: world_k = sum_j wv[k*3+j] (view_j - t_j), wv[k*3+j] = (R^-1)[j][k], adjugate over determinant
// in double. It stands where the transpose r[k*3+j] used to: the engine's rotation is a 16.16 fixed-point basis,
// orthonormal only to max|R R^T - I| ~1.2e-5 (run222, 6,001 frames; p99 2.1e-5), so the transpose leaves (R R^T - I) t
// of false position, 0.4-1 unit at |t| 33 km, re-rolled by every orientation LSB (directional-shadows.md, "Run 62
// (run222)"). Every site that recovers world space from a latch uses this one basis, so casters, receivers, retained
// rows and the TAA static rows share one world. No fabs (the mingw build emits x87 for it). False: singular /
// non-finite r.
inline bool camera_world_basis_compute(const float r[9], double wv[9]) noexcept {
    const double a[9] = {r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8]};
    const double cof[9] = {
        a[4] * a[8] - a[5] * a[7], a[2] * a[7] - a[1] * a[8], a[1] * a[5] - a[2] * a[4], a[5] * a[6] - a[3] * a[8],
        a[0] * a[8] - a[2] * a[6], a[2] * a[3] - a[0] * a[5], a[3] * a[7] - a[4] * a[6], a[1] * a[6] - a[0] * a[7],
        a[0] * a[4] - a[1] * a[3]}; // the adjugate: R^-1[i][k] = cof[i*3+k] / det
    const double det = a[0] * cof[0] + a[1] * cof[3] + a[2] * cof[6];
    if (!(det > .5 || det < -.5)) return false;
    const double inv = 1. / det;
    for (unsigned k = 0; k < 3; ++k)
        for (unsigned j = 0; j < 3; ++j) wv[k * 3 + j] = cof[j * 3 + k] * inv;
    return true;
}
// The latch's cached basis, or one computed into `scratch` for a hand-built state. A state that passed
// camera_state_from_matrices (Gram within 1e-3) always inverts. A singular or non-finite hand-built r yields a NaN
// basis, never the transpose: every consumer's finite check then refuses the product (fail closed).
inline const double* camera_world_basis(const CameraState& camera, double scratch[9]) noexcept {
    if (camera.wv_valid) return camera.wv;
    if (!camera_world_basis_compute(camera.r, scratch))
        for (unsigned i = 0; i < 9; ++i) scratch[i] = std::numeric_limits<double>::quiet_NaN();
    return scratch;
}
// The eye in world space, -t R^-1.
inline void camera_world_position(const CameraState& camera, double out[3]) noexcept {
    double scratch[9];
    const double* wv = camera_world_basis(camera, scratch);
    for (unsigned i = 0; i < 3; ++i) {
        out[i] = 0;
        for (unsigned j = 0; j < 3; ++j) out[i] -= double(camera.t[j]) * wv[i * 3 + j];
    }
}
enum class CameraFailure : std::uint32_t {
    None = 0,
    NullPointer = 1,
    NonFinite = 2,
    ProjectionScale = 3,
    ProjectionW = 4,
    Orthonormal = 5,
    ViewAffine = 6
};
// Parses and validates the two engine buffers into a CameraState. Every
// failure leaves out.valid false and names the first failed check.
inline bool camera_state_from_matrices(const float* projection, const float* view, CameraState& out,
                                       CameraFailure* why = nullptr) noexcept {
    auto fail = [&](CameraFailure code) {
        out.valid = false;
        if (why) *why = code;
        return false;
    };
    out = CameraState{};
    if (!projection || !view) return fail(CameraFailure::NullPointer);
    for (unsigned i = 0; i < 16; ++i)
        if (!std::isfinite(projection[i]) || !std::isfinite(view[i])) return fail(CameraFailure::NonFinite);
    if (!(projection[0] > 0.f) || !(projection[5] > 0.f)) return fail(CameraFailure::ProjectionScale);
    if (projection[11] != 1.f) return fail(CameraFailure::ProjectionW);
    // Orthonormal upper-left 3x3 within 1e-3 (camera-numerics.md measured
    // C^T C - I up to 3.6e-5 on captured views).
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned j = 0; j < 3; ++j) {
            double dot = 0;
            for (unsigned k = 0; k < 3; ++k) dot += double(view[i * 4 + k]) * double(view[j * 4 + k]);
            if (std::fabs(dot - (i == j ? 1. : 0.)) > 1e-3) return fail(CameraFailure::Orthonormal);
        }
    }
    // Affine view: the identity template supplies elements 3, 7, 11 = 0 and 15 = 1.
    if (view[3] != 0.f || view[7] != 0.f || view[11] != 0.f || view[15] != 1.f) return fail(CameraFailure::ViewAffine);
    out.m00 = projection[0];
    out.m11 = projection[5];
    out.m20 = projection[8];
    out.m21 = projection[9];
    out.m22 = projection[10];
    out.m32 = projection[14];
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j) out.r[i * 3 + j] = view[i * 4 + j];
    for (unsigned i = 0; i < 3; ++i) out.t[i] = view[12 + i];
    if (!camera_world_basis_compute(out.r, out.wv))
        return fail(CameraFailure::Orthonormal); // unreachable past the Gram check; no state is valid without its basis
    out.wv_valid = 1u;
    out.valid = true;
    if (why) *why = CameraFailure::None;
    return true;
}
// Angle in degrees of the rotation between two views (R_a^T R_b).
inline float camera_rotation_degrees(const CameraState& a, const CameraState& b) noexcept {
    double trace = 0;
    for (unsigned i = 0; i < 9; ++i) trace += double(a.r[i]) * double(b.r[i]);
    double c = (trace - 1.) * .5;
    if (c > 1.) c = 1.;
    if (c < -1.) c = -1.;
    return float(std::acos(c) * 180. / 3.14159265358979323846);
}
// Analytic oracle: previous NDC of a current NDC direction at infinity.
// Returns false when the direction lies behind the previous camera.
inline bool camera_far_plane_previous_ndc(const CameraState& current, const CameraState& previous, double x, double y,
                                          double& px, double& py) noexcept {
    if (!current.valid || !previous.valid) return false;
    const double view[3] = {(x - current.m20) / current.m00, (y - current.m21) / current.m11, 1.};
    double world[3], prev[3], scratch[9];
    const double* wv = camera_world_basis(current, scratch);
    for (unsigned i = 0; i < 3; ++i) {
        world[i] = 0;
        for (unsigned j = 0; j < 3; ++j) world[i] += view[j] * wv[i * 3 + j];
    }
    for (unsigned j = 0; j < 3; ++j) {
        prev[j] = 0;
        for (unsigned i = 0; i < 3; ++i) prev[j] += world[i] * previous.r[i * 3 + j];
    }
    if (!(prev[2] > 0.)) return false;
    px = prev[0] * previous.m00 / prev[2] + previous.m20;
    py = prev[1] * previous.m11 / prev[2] + previous.m21;
    return true;
}
// The 4x4 clip_to_previous (row-major storage, column-vector multiplication)
// mapping a current NDC direction at infinity to the previous clip position:
// previous = (X, Y, W (1 - 2^-16), W) so x/w, y/w are the previous NDC and z/w is just
// under 1 (the far plane; see the z row below); the current z column is zero (the map depends on the direction only)
// and w > 0 exactly when the direction is in front of the previous camera.
inline bool camera_far_plane_reprojection(const CameraState& current, const CameraState& previous,
                                          float out[16]) noexcept {
    if (!current.valid || !previous.valid || !out) return false;
    // Row-vector chain (x, y, 1) * A * Q * B = (X, Y, W).
    const double A[3][3] = {{1. / current.m00, 0, 0},
                            {0, 1. / current.m11, 0},
                            {-double(current.m20) / current.m00, -double(current.m21) / current.m11, 1}};
    double Q[3][3], scratch[9]; // R_current^-1 * R_previous (camera_world_basis: identical views give the identity,
                                // which the transpose of a 16.16 basis does not)
    const double* wv = camera_world_basis(current, scratch);
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j) {
            Q[i][j] = 0;
            for (unsigned k = 0; k < 3; ++k) Q[i][j] += wv[k * 3 + i] * double(previous.r[k * 3 + j]);
        }
    const double B[3][3] = {{previous.m00, 0, 0}, {0, previous.m11, 0}, {previous.m20, previous.m21, 1}};
    double AQ[3][3], N[3][3];
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j) {
            AQ[i][j] = 0;
            for (unsigned k = 0; k < 3; ++k) AQ[i][j] += A[i][k] * Q[k][j];
        }
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j) {
            N[i][j] = 0;
            for (unsigned k = 0; k < 3; ++k) N[i][j] += AQ[i][k] * B[k][j];
        }
    // Transpose into column-vector rows; the constant term (row 2 of N, the
    // "1" of the direction) rides on the homogeneous 1 of currentClip.
    // The z row is the w row times (1 - 2^-16), not the w row itself: the resolve forms expectedDepth = z / w on the
    // GPU, whose division need not be IEEE (D3D9 allows a reciprocal multiply), and with bit-identical rows the
    // quotient rounded ABOVE 1 on 8-11 % of the pixels with w < 1, failing validDepth and dropping their history on
    // every pan (run215). The error of such a quotient is a few float ulps (~1e-7 relative, as are the row's own
    // rounding and the two dots going separate ways); 2^-16 = 1.5e-5 is 100x that and 1300x below the 0.02 disocclusion
    // tolerance expectedDepth feeds, on its permissive side.
    constexpr double kFarDepth = 1. - 0x1p-16;
    const double M[4][4] = {{N[0][0], N[1][0], 0, N[2][0]},
                            {N[0][1], N[1][1], 0, N[2][1]},
                            {N[0][2] * kFarDepth, N[1][2] * kFarDepth, 0, N[2][2] * kFarDepth},
                            {N[0][2], N[1][2], 0, N[2][2]}};
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned j = 0; j < 4; ++j) {
            if (!std::isfinite(M[i][j]) || std::fabs(M[i][j]) > 1e15) return false;
            out[i * 4 + j] = float(M[i][j]);
        }
    return true;
}
// Depth and translation of the camera path, the companions of the far-plane matrix above (taa-lattice-crawl.md
// sections 32.3 and 32.4). For a pixel at view z the previous clip position divided by that z is
//   far_plane(x, y) + D / z,   D = (t_prev - t_cur * R_cur^-1 * R_prev) * B_prev,
// the far-plane image of the pixel's direction plus the camera-relative translation between the two views in the
// previous clip rows (X, Y, W). camera_translation_clip() is D, in double, from the DIFFERENCE of the two translations
// so a large sector coordinate costs only the engine's own float quantum of t. It reads the previous view's
// m00 / m11 / m20 / m21, the same latched projection terms c0..c3 are built from, and no depth law.
inline bool camera_translation_clip(const CameraState& current, const CameraState& previous, double K[3]) noexcept {
    if (!current.valid || !previous.valid) return false;
    // The exact inverse of the float R_cur (camera_world_basis), not its transpose: R^T R - I is ~1.2e-5 on this
    // engine's 16.16 fixed-point views (run222) and |t| reaches 1e6, so the transpose would leave units of false
    // translation even between two identical views. With the inverse, identical views give D = 0 to double rounding.
    double scratch[9];
    const double* wv = current.wv_valid                                 ? current.wv
                       : camera_world_basis_compute(current.r, scratch) ? scratch
                                                                        : nullptr;
    if (!wv) return false;
    double D[3];
    for (unsigned j = 0; j < 3; ++j) {
        double moved = 0; // (t_cur * R_cur^-1 * R_prev)[j]
        for (unsigned k = 0; k < 3; ++k) {
            double world = 0;
            for (unsigned i = 0; i < 3; ++i) world += double(current.t[i]) * wv[k * 3 + i];
            moved += world * double(previous.r[k * 3 + j]);
        }
        D[j] = double(previous.t[j]) - moved;
    }
    K[0] = D[0] * previous.m00 + D[2] * previous.m20;
    K[1] = D[1] * previous.m11 + D[2] * previous.m21;
    K[2] = D[2];
    return true;
}
// 1 / z from the device depth d and the current projection's depth law, z = m32 / (d - m22): out = (DX, DY, DW) / m32
// and m22, so the consumer adds out.xyz * (d - out.w), forming d - m22 from the same float m22 the rasteriser's
// projection held (an exact subtraction for d in [0.5, 1]). xyz = 0 is the far-plane path bit for bit. False (out
// zeroed) without a valid pair or a plausible law (m22 > 1, m32 < 0). m22 / m32 are per-submission
// scratch in the engine: a latch from a view with another near plane passes this check and mis-scales the term.
inline bool camera_depth_parallax(const CameraState& current, const CameraState& previous, float out[4]) noexcept {
    if (!out) return false;
    out[0] = out[1] = out[2] = out[3] = 0.f;
    double K[3];
    if (!(current.m22 > 1.f) || !(current.m32 < 0.f) || !camera_translation_clip(current, previous, K)) return false;
    const double inv = 1. / double(current.m32);
    for (double& v : K) {
        v *= inv;
        if (!std::isfinite(v) || std::fabs(v) > 1e15) return false;
    }
    for (unsigned i = 0; i < 3; ++i) out[i] = float(K[i]);
    out[3] = current.m22;
    return true;
}
// The DEPTH-latch-free form for a depth input that carries the linear view z (clip w) per pixel (RT2 .b of the
// four-channel sun-shadow lane, current_depth_ps.hlsl): the consumer adds out.xyz / w, so neither m22 nor m32 enters.
// It is not free of the projection latch: D and c0..c3 still use the latched m00 / m11 / m20 / m21, so a latch from a
// view with a different FOV is not covered by this form either. out = (DX, DY, DW, 1); all zero and false without a
// valid pair.
inline bool camera_lane_parallax(const CameraState& current, const CameraState& previous, float out[4]) noexcept {
    if (!out) return false;
    out[0] = out[1] = out[2] = out[3] = 0.f;
    double K[3];
    if (!camera_translation_clip(current, previous, K)) return false;
    for (double v : K)
        if (!std::isfinite(v) || std::fabs(v) > 1e15) return false;
    for (unsigned i = 0; i < 3; ++i) out[i] = float(K[i]);
    out[3] = 1.f;
    return true;
}
// Policy: auto (the only production policy; X3M_TAA_SENTINEL and
// --taa-sentinel were removed 2026-09-25) reprojects sentinel pixels through
// the camera whenever a valid transform exists and falls back to current-only
// otherwise; 1 and 2 are fixture-only (seam X3M_FIXTURE_TAA_SENTINEL): 1 never reprojects; 2 is the strict diagnostic
// form of auto (the route skips the resolve on a frame whose camera cannot be read or whose transform fails, so a
// broken camera read is visible in gameplay rather than silently current-only; frames without a previous view still
// resolve).
enum class SentinelMode : unsigned { Auto = 0, CurrentOnly = 1, Camera = 2 };
enum class SentinelReason : unsigned {
    CameraPath = 0,
    SwitchOff = 1,
    CurrentInvalid = 2,
    PreviousInvalid = 3,
    RotationCut = 4,
    TransformFailed = 5
};
struct SentinelDecision {
    unsigned policy = 1;    // resolve c7.w: 1 current-only, 2 camera far plane
    bool cut = false;       // rotation between the frames exceeded the bound
    bool transform = false; // `matrix` holds a valid far-plane reprojection
    float rotation_degrees = 0;
    SentinelReason reason = SentinelReason::SwitchOff;
    float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};
inline SentinelDecision camera_sentinel_policy(SentinelMode mode, const CameraState& current,
                                               const CameraState& previous, float cut_degrees) noexcept {
    SentinelDecision d;
    if (mode == SentinelMode::CurrentOnly) {
        d.reason = SentinelReason::SwitchOff;
        return d;
    }
    if (!current.valid) {
        d.reason = SentinelReason::CurrentInvalid;
        return d;
    }
    if (!previous.valid) {
        d.reason = SentinelReason::PreviousInvalid;
        return d;
    }
    d.rotation_degrees = camera_rotation_degrees(current, previous);
    if (!(std::isfinite(cut_degrees) && cut_degrees > 0) || d.rotation_degrees > cut_degrees) {
        d.cut = true;
        d.reason = SentinelReason::RotationCut;
        return d;
    }
    if (!camera_far_plane_reprojection(current, previous, d.matrix)) {
        for (unsigned i = 0; i < 16; ++i) d.matrix[i] = (i % 5 == 0) ? 1.f : 0.f;
        d.reason = SentinelReason::TransformFailed;
        return d;
    }
    d.transform = true;
    d.policy = 2;
    d.reason = SentinelReason::CameraPath;
    return d;
}
} // namespace x3m::renderer
