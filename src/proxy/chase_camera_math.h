#pragma once
// Portable pose pipeline of the chase camera (docs/architecture/chase-camera.md):
// vanilla pose + ship transform -> critically damped springs on orientation
// (rotation vector) and on the boom offset -> lag clamps -> fixed-point camera
// fields. No Windows dependency and no engine access, so the host unit tests
// (verification/analysis/test_chase_camera.py through
// verification/probe/chase_camera_host.cpp) run the exact production code.
// Double precision throughout: the proxy is built with -msse2 -mfpmath=sse so
// this is SSE2 arithmetic; positions are int32 engine units and stay exact in
// a double. The engine's fixed-point convention: basis rows at +0x40/+0x50/+0x60
// of a camera node (three int32 per row, 65536 = 1.0), rows = the camera's
// right/up/forward axes in world space (row-vector convention, left-handed);
// position at +0x30/+0x34/+0x38 (int32).
#include <cmath>
#include <cstdint>

namespace x3m::chase {
constexpr double pi = 3.14159265358979323846;
constexpr double fixed_one = 65536.0;

struct Vec3 { double x = 0, y = 0, z = 0; };
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline bool finite(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

// Row-major 3x3; rows are axes (row-vector convention: world = local * M).
struct Mat3 { double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; };
inline Vec3 row(const Mat3& a, int i) { return {a.m[i][0], a.m[i][1], a.m[i][2]}; }
inline Mat3 transpose(const Mat3& a) { Mat3 r; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[j][i]; return r; }
inline Mat3 mul(const Mat3& a, const Mat3& b) { // (a*b): rows of a re-expressed through b
    Mat3 r;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    return r;
}
inline Vec3 mul(Vec3 v, const Mat3& b) { return {v.x * b.m[0][0] + v.y * b.m[1][0] + v.z * b.m[2][0], v.x * b.m[0][1] + v.y * b.m[1][1] + v.z * b.m[2][1], v.x * b.m[0][2] + v.y * b.m[1][2] + v.z * b.m[2][2]}; }
inline bool finite(const Mat3& a) { for (auto& r : a.m) for (double v : r) if (!std::isfinite(v)) return false; return true; }
inline double determinant(const Mat3& a) { return dot(row(a, 0), cross(row(a, 1), row(a, 2))); }
// Gram-Schmidt on the forward/up rows (forward kept), right = up x forward for a
// left-handed frame (right x up = forward). Returns false when degenerate.
inline bool orthonormalize(Mat3& a) {
    Vec3 f = row(a, 2), u = row(a, 1);
    const double lf = length(f); if (!(lf > 1e-9)) return false;
    f = f * (1.0 / lf);
    u = u - f * dot(u, f);
    const double lu = length(u); if (!(lu > 1e-9)) return false;
    u = u * (1.0 / lu);
    const Vec3 r = cross(u, f);
    a.m[0][0] = r.x; a.m[0][1] = r.y; a.m[0][2] = r.z;
    a.m[1][0] = u.x; a.m[1][1] = u.y; a.m[1][2] = u.z;
    a.m[2][0] = f.x; a.m[2][1] = f.y; a.m[2][2] = f.z;
    return finite(a);
}
// How far from a proper rotation: max |a*a^T - I| and det-1.
inline double orthonormality_error(const Mat3& a) {
    double e = std::fabs(determinant(a) - 1.0);
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) e = std::fmax(e, std::fabs(dot(row(a, i), row(a, j)) - (i == j ? 1.0 : 0.0)));
    return e;
}
// Rotation about a world axis-angle vector r (Rodrigues), as a row-vector
// matrix W so that B_new = B * W rotates every axis row of B by r.
inline Mat3 exp_rotation(Vec3 r) {
    const double angle = length(r);
    Mat3 w;
    if (!(angle > 1e-12)) return w;
    const Vec3 k = r * (1.0 / angle);
    const double c = std::cos(angle), s = std::sin(angle), t = 1.0 - c;
    // Row-vector form of R(k, angle): v' = v * W, W = R^T of the column form.
    w.m[0][0] = t * k.x * k.x + c;       w.m[0][1] = t * k.x * k.y + s * k.z; w.m[0][2] = t * k.x * k.z - s * k.y;
    w.m[1][0] = t * k.x * k.y - s * k.z; w.m[1][1] = t * k.y * k.y + c;       w.m[1][2] = t * k.y * k.z + s * k.x;
    w.m[2][0] = t * k.x * k.z + s * k.y; w.m[2][1] = t * k.y * k.z - s * k.x; w.m[2][2] = t * k.z * k.z + c;
    return w;
}
// Inverse of exp_rotation: the world axis-angle vector of a row-vector rotation W.
inline Vec3 log_rotation(const Mat3& w) {
    const double trace = w.m[0][0] + w.m[1][1] + w.m[2][2];
    const double c = std::fmin(1.0, std::fmax(-1.0, (trace - 1.0) * 0.5));
    const double angle = std::acos(c);
    if (angle < 1e-9) return {};
    // Axis from the antisymmetric part (row-vector form: k_x = W[1][2]-W[2][1]).
    Vec3 axis = {w.m[1][2] - w.m[2][1], w.m[2][0] - w.m[0][2], w.m[0][1] - w.m[1][0]};
    const double s = length(axis);
    if (s < 1e-9) {
        // angle ~ pi: take the axis from the diagonal.
        int i = 0; if (w.m[1][1] > w.m[i][i]) i = 1; if (w.m[2][2] > w.m[i][i]) i = 2;
        Vec3 k; double* kk[3] = {&k.x, &k.y, &k.z};
        *kk[i] = std::sqrt(std::fmax(0.0, (w.m[i][i] + 1.0) * 0.5));
        for (int j = 0; j < 3; ++j) if (j != i) *kk[j] = (w.m[i][j] + w.m[j][i]) * 0.25 / (*kk[i] > 1e-9 ? *kk[i] : 1.0);
        const double lk = length(k); if (!(lk > 1e-9)) return {};
        return k * (angle / lk);
    }
    return axis * (angle / s);
}
// Local pitch (about the camera's own right axis): positive tilts the forward
// axis toward the up axis (camera looks up; the ship sinks on screen).
inline Mat3 local_pitch(double radians) {
    const double c = std::cos(radians), s = std::sin(radians);
    Mat3 l; l.m[1][1] = c; l.m[1][2] = -s; l.m[2][1] = s; l.m[2][2] = c;
    return l;
}
inline Mat3 from_fixed(const std::int32_t* rows /* 12 ints, stride 4 per row */) {
    Mat3 a;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) a.m[i][j] = rows[i * 4 + j] / fixed_one;
    return a;
}
inline bool to_fixed(const Mat3& a, std::int32_t* rows /* writes 3 of each 4 */) {
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
        const double v = std::nearbyint(a.m[i][j] * fixed_one);
        if (!(v >= -2147483648.0 && v <= 2147483647.0)) return false;
        rows[i * 4 + j] = static_cast<std::int32_t>(v);
    }
    return true;
}
inline bool to_int(Vec3 v, std::int32_t* out) {
    const double c[3] = {std::nearbyint(v.x), std::nearbyint(v.y), std::nearbyint(v.z)};
    for (double x : c) if (!(x >= -2147483648.0 && x <= 2147483647.0)) return false;
    out[0] = static_cast<std::int32_t>(c[0]); out[1] = static_cast<std::int32_t>(c[1]); out[2] = static_cast<std::int32_t>(c[2]);
    return true;
}

// Critically damped spring, closed form for a step of dt with omega = 1/tau:
// x(t) = (x0 + (v0 + w x0) t) e^{-w t}. Exact for a fixed target, so the step
// is stable for any dt and independent of the frame rate (SETA does not enter:
// dt is wall-clock time).
struct Spring { Vec3 x, v; };
inline void spring_step(Spring& s, double tau, double dt) {
    if (!(tau > 0)) { s.x = {}; s.v = {}; return; }
    if (!(dt > 0)) return; // no time passed: the state stands
    const double w = 1.0 / tau, e = std::exp(-w * dt);
    const Vec3 c2 = s.v + s.x * w;
    const Vec3 x = (s.x + c2 * dt) * e;
    const Vec3 v = (s.v - c2 * (w * dt)) * e;
    s.x = x; s.v = v;
}
// |x| <= limit; the outward velocity component is removed so the clamp does
// not fight the spring next frame. Returns true when clamped.
inline bool clamp_spring(Spring& s, double limit) {
    const double l = length(s.x);
    if (!(l > limit) || !(limit >= 0)) return false;
    const Vec3 n = s.x * (1.0 / l);
    s.x = n * limit;
    const double outward = dot(s.v, n);
    if (outward > 0) s.v = s.v - n * outward;
    return true;
}

struct Tunables {
    double rot_tau = 0.20;          // s, orientation spring time constant (X3M_CAMERA_ROT_TAU)
    double pos_tau = 0.30;          // s, boom-offset spring time constant (X3M_CAMERA_POS_TAU)
    double offset_y = 0.12;         // fraction of the half screen height the ship sits below centre (X3M_CAMERA_OFFSET_Y)
    double distance_scale = 1.0;    // multiplies the vanilla boom offset (X3M_CAMERA_DISTANCE_SCALE)
    double lag_clamp_deg = 10.0;    // max orientation lag (X3M_CAMERA_LAG_CLAMP_DEG)
    double pos_lag_clamp = 0.20;    // max |offset lag| as a fraction of the boom length
    double combat_tightness = 0.0;  // 0..1, reserved: no readable target-lock state yet (X3M_CAMERA_COMBAT_TIGHTNESS)
    double max_dt = 0.10;           // s, dt clamp after pauses/loads
    double snap_ratio = 20.0;       // ship displacement per frame above snap_ratio * boom length = teleport
    double max_orthonormality_error = 0.02; // input basis rejection
};
inline bool valid(const Tunables& t) {
    return t.rot_tau > 0 && t.rot_tau <= 10 && t.pos_tau > 0 && t.pos_tau <= 10 && std::isfinite(t.offset_y) && std::fabs(t.offset_y) <= 1 &&
           t.distance_scale > 0 && t.distance_scale <= 10 && t.lag_clamp_deg >= 0 && t.lag_clamp_deg <= 90 && t.pos_lag_clamp >= 0 && t.pos_lag_clamp <= 1 &&
           t.combat_tightness >= 0 && t.combat_tightness <= 1 && t.max_dt > 0 && t.max_dt <= 5 && t.snap_ratio >= 1 && t.max_orthonormality_error > 0;
}

// One frame of engine state, already converted from the engine's integers.
struct Input {
    Vec3 ship_pos;            // ship render node +0xb0 (int32 units)
    Mat3 vanilla_cam;         // camera +0x40 rows as the cockpit update left them
    Vec3 vanilla_pos;         // camera +0x30
    Mat3 view_rel;            // cockpit +0xf0: camera basis relative to the ship (vanilla_cam = view_rel * ship_basis)
    double half_vfov_tan = 0.75; // tan of half the vertical FOV (0.75 = the 73.74 deg default)
    std::uint32_t view_mode = 0, connect_mode = 0; // cockpit +0x150, +0x1c0
    std::uintptr_t ref_object = 0, sector = 0;      // cockpit +0xc, +0x1fc
};
struct Pose { Vec3 pos; Mat3 basis; Mat3 view_rel; };
enum class Verdict : std::uint32_t {
    Applied = 0,
    InternalView = 1,      // view mode 1: the cockpit
    NotBackView = 2,       // an external view that is not behind the ship
    SpecialConnect = 3,    // connect mode 4/5/6/8/9: scripted/cinematic camera
    InvalidInput = 4,      // non-finite or non-orthonormal input
    Degenerate = 5,        // zero boom, or the derived ship basis failed
    NumericFailure = 6,    // state became non-finite (reset)
};
struct Step {
    Verdict verdict = Verdict::InvalidInput;
    bool snapped = false;
    double lag_deg = 0, pos_lag = 0, distance = 0, dt = 0;
    std::uint32_t snap_reason = 0; // bit set: 1 first, 2 ship/ref change, 4 sector, 8 mode, 16 teleport, 32 numeric
};
struct State {
    bool tracking = false;      // the springs hold a pose from the previous applied frame
    Mat3 basis;                 // smoothed camera basis (world)
    Vec3 offset;                // smoothed boom: camera position - ship position (world)
    Spring rot, pos;            // rotation vector (rad, world) / boom lag (units, world)
    Vec3 last_ship_pos;
    std::uintptr_t last_ref = 0, last_sector = 0;
    std::uint32_t last_mode = 0, last_connect = 0;
    std::uint64_t snaps = 0, applied = 0, refused = 0, clamps = 0;
};

// Geometric back-view test on the vanilla pose: the boom points behind the ship
// (ship-frame z < 0, mostly along the axis) and the camera looks along the
// ship's forward axis. The external views are script-defined (view position
// and angles), so the mode integer alone cannot tell them apart.
inline bool back_view(Vec3 boom_local, const Mat3& view_rel) {
    const double behind = -boom_local.z;
    if (!(behind > 0)) return false;
    if (std::fabs(boom_local.x) > 0.6 * behind) return false;
    if (std::fabs(boom_local.y) > 1.5 * behind) return false;
    return view_rel.m[2][2] > 0.7; // forward axes aligned within ~45 deg
}

inline void reset(State& s) { s.tracking = false; s.rot = {}; s.pos = {}; }

// The pipeline. dt in seconds (wall clock; clamped to max_dt). Returns the
// verdict; `out` is written only for Verdict::Applied.
inline Step step(State& s, const Input& in, double dt, const Tunables& t, Pose* out) {
    Step r; r.dt = dt;
    auto refuse = [&](Verdict v) { r.verdict = v; ++s.refused; reset(s); return r; };
    if (!valid(t) || !finite(in.ship_pos) || !finite(in.vanilla_pos) || !finite(in.vanilla_cam) || !finite(in.view_rel) || !std::isfinite(in.half_vfov_tan) || !(in.half_vfov_tan > 0) || !std::isfinite(dt))
        return refuse(Verdict::InvalidInput);
    if (orthonormality_error(in.vanilla_cam) > t.max_orthonormality_error || orthonormality_error(in.view_rel) > t.max_orthonormality_error)
        return refuse(Verdict::InvalidInput);
    if (in.view_mode == 1) return refuse(Verdict::InternalView);
    if (in.connect_mode == 4 || in.connect_mode == 5 || in.connect_mode == 6 || in.connect_mode == 8 || in.connect_mode == 9) return refuse(Verdict::SpecialConnect);
    // Effective ship basis from the vanilla identity camera = view_rel * ship:
    // ship = view_rel^T * camera. (Docked/carried ships use a derived parent
    // basis in the engine; this keeps view_rel consistent whichever it was.)
    Mat3 ship = mul(transpose(in.view_rel), in.vanilla_cam);
    if (!orthonormalize(ship)) return refuse(Verdict::Degenerate);
    const Vec3 boom_world = in.vanilla_pos - in.ship_pos;
    const double boom = length(boom_world);
    if (!(boom > 0)) return refuse(Verdict::Degenerate);
    const Vec3 boom_local = mul(boom_world, transpose(ship));
    if (!back_view(boom_local, in.view_rel)) return refuse(Verdict::NotBackView);

    // Snap conditions.
    std::uint32_t snap = 0;
    if (!s.tracking) snap |= 1;
    else {
        if (in.ref_object != s.last_ref) snap |= 2;
        if (in.sector != s.last_sector) snap |= 4;
        if (in.view_mode != s.last_mode || in.connect_mode != s.last_connect) snap |= 8;
        if (length(in.ship_pos - s.last_ship_pos) > t.snap_ratio * boom) snap |= 16;
    }
    s.last_ref = in.ref_object; s.last_sector = in.sector; s.last_mode = in.view_mode; s.last_connect = in.connect_mode; s.last_ship_pos = in.ship_pos;
    if (snap) reset(s);

    // Target orientation: the vanilla camera pitched up so the ship sits below centre.
    const double pitch = std::atan(t.offset_y * in.half_vfov_tan);
    Mat3 target = mul(local_pitch(pitch), in.vanilla_cam);
    if (!orthonormalize(target)) return refuse(Verdict::Degenerate);
    // Target boom: the vanilla boom in the ship frame, scaled, back in world space.
    const Vec3 target_boom = mul(boom_local * t.distance_scale, ship);
    const double target_length = boom * t.distance_scale;

    const double step_dt = std::fmin(std::fmax(dt, 0.0), t.max_dt);
    if (snap) {
        s.basis = target; s.offset = target_boom; s.rot = {}; s.pos = {};
        s.tracking = true; ++s.snaps; r.snapped = true; r.snap_reason = snap;
    } else {
        // Orientation: x = rotation vector from target to current (world frame),
        // current = target * exp(x). Re-measure against the new target, then relax.
        s.rot.x = log_rotation(mul(transpose(target), s.basis));
        spring_step(s.rot, t.rot_tau, step_dt);
        if (clamp_spring(s.rot, t.lag_clamp_deg * pi / 180.0)) ++s.clamps;
        s.basis = mul(target, exp_rotation(s.rot.x));
        if (!orthonormalize(s.basis)) { r.snap_reason = 32; ++s.snaps; return refuse(Verdict::NumericFailure); }
        // Boom: x = previous boom - target boom (both ship-relative, so a
        // constant ship velocity leaves no lag; turns and boom changes swing),
        // relaxes toward zero.
        s.pos.x = s.offset - target_boom;
        spring_step(s.pos, t.pos_tau, step_dt);
        if (clamp_spring(s.pos, t.pos_lag_clamp * target_length)) ++s.clamps;
        s.offset = target_boom + s.pos.x;
    }
    if (!finite(s.basis) || !finite(s.offset) || !finite(s.rot.x) || !finite(s.rot.v) || !finite(s.pos.x) || !finite(s.pos.v)) { r.snap_reason = 32; ++s.snaps; return refuse(Verdict::NumericFailure); }
    out->basis = s.basis;
    out->pos = in.ship_pos + s.offset;
    out->view_rel = mul(s.basis, transpose(ship)); // camera = view_rel * ship holds for the smoothed pose too
    r.lag_deg = length(s.rot.x) * 180.0 / pi;
    r.pos_lag = length(s.pos.x);
    r.distance = length(out->pos - in.ship_pos);
    r.verdict = Verdict::Applied;
    ++s.applied;
    return r;
}
// Called when a frame was refused or the hook did not run, so the next applied
// frame snaps instead of continuing from a stale state.
inline void note_gap(State& s) { reset(s); }
}
