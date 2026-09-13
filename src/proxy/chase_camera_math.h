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
// The cockpit-scene camera already contains this frame's vanilla view basis.
// Re-express it through the replacement view, preserving its shake transform.
inline Mat3 relative_view_correction(const Mat3& replacement, const Mat3& vanilla) {
    return mul(replacement, transpose(vanilla));
}
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

// Softer follow after the corrected user flight: retain the existing lag
// bounds while increasing settling time. Elevated framing is independent of
// spring lag; see docs/architecture/elevated-chase-camera.md.
struct Tunables {
    double rot_tau = 0.28;          // s, orientation spring time constant (X3M_CHASE_ROT_TAU)
    double pos_tau = 0.38;          // s, boom-offset spring time constant (X3M_CHASE_POS_TAU)
    double offset_y = 0.45;         // 72.5% screen height from a centred native anchor (X3M_CHASE_OFFSET_Y)
    double pitch_down_deg = 13.0;   // 0 keeps legacy framing; (0,30] sets ship-relative downward look
    double distance_scale = 0.90;   // multiplies the vanilla boom offset (X3M_CHASE_DISTANCE_SCALE)
    double lag_clamp_deg = 8.0;     // max orientation lag (X3M_CHASE_LAG_CLAMP_DEG)
    double pos_lag_clamp = 0.10;    // max |offset lag| as a fraction of the boom length (X3M_CHASE_POS_LAG_CLAMP)
    double combat_tightness = 0.0;  // 0..1: while a target is locked (Input::target_locked) both time constants are scaled by (1 - tightness); 1 = rigid follow (X3M_CHASE_COMBAT_TIGHTNESS)
    double max_dt = 0.10;           // s, dt clamp after pauses/loads (X3M_CHASE_MAX_DT)
    double snap_ratio = 20.0;       // ship displacement per frame above snap_ratio * boom length = teleport
    double max_orthonormality_error = 0.02; // input basis rejection
    unsigned snap_coalesce_frames = 3; // a sector-only snap within this many applied frames of the previous snap re-seats the springs but raises no second cut (A4: gate jump = teleport, then the sector follows a frame later)
};
inline bool valid(const Tunables& t) {
    return t.rot_tau > 0 && t.rot_tau <= 10 && t.pos_tau > 0 && t.pos_tau <= 10 && std::isfinite(t.offset_y) && std::fabs(t.offset_y) <= 1 &&
           t.pitch_down_deg >= 0 && t.pitch_down_deg <= 30 &&
           t.distance_scale > 0 && t.distance_scale <= 10 && t.lag_clamp_deg >= 0 && t.lag_clamp_deg <= 90 && t.pos_lag_clamp >= 0 && t.pos_lag_clamp <= 1 &&
           t.combat_tightness >= 0 && t.combat_tightness <= 1 && t.max_dt > 0 && t.max_dt <= 5 && t.snap_ratio >= 1 && t.max_orthonormality_error > 0 &&
           t.snap_coalesce_frames <= 60;
}

// One frame of engine state, already converted from the engine's integers.
struct Input {
    Vec3 ship_pos;            // native follow anchor: node +0x30 or +0xb0 (chase_camera_native.h)
    Mat3 vanilla_cam;         // camera +0x40 rows as the cockpit update left them
    Vec3 vanilla_pos;         // camera +0x30
    Mat3 view_rel;            // cockpit +0xf0: camera basis relative to the ship (vanilla_cam = view_rel * ship_basis)
    double half_vfov_tan = 0.75; // tan of half the vertical FOV (0.75 = the 73.74 deg default)
    std::uint32_t view_mode = 0, connect_mode = 0; // cockpit +0x150, +0x1c0
    std::uint32_t flags_1a0 = 0;                    // cockpit +0x1a0: bit 2 makes the engine write the camera basis verbatim (A2)
    std::uintptr_t ref_object = 0, sector = 0;      // cockpit +0xc, +0x1fc
    bool target_locked = false;                     // cockpit +0x1e4 tracking mode 1/4 with a valid +0x1e0 object (A9; unverified in game)
};
struct Pose { Vec3 pos; Mat3 basis; Mat3 view_rel; };
enum class Verdict : std::uint32_t {
    Applied = 0,
    InternalView = 1,      // view mode 1: the cockpit
    NotBackView = 2,       // an external view that is not behind the ship
    SpecialConnect = 3,    // connect mode other than 0 (4/5/6/8/9 scripted/cinematic; 1/2/7 without study semantics pass through too)
    InvalidInput = 4,      // non-finite or non-orthonormal input
    Degenerate = 5,        // zero boom, or the derived ship basis failed
    NumericFailure = 6,    // state became non-finite (reset)
    VerbatimBasis = 7,     // connect mode 3 or +0x1a0 & 4: the engine wrote camera.basis = +0xf0 verbatim, so the derived ship basis would be the identity (A2)
};
struct Step {
    Verdict verdict = Verdict::InvalidInput;
    bool snapped = false;      // the springs were re-seated and the frame is a cut
    bool coalesced = false;    // re-seated within snap_coalesce_frames of the previous snap: no second cut (A4)
    bool target_locked = false; // the combat-tightness scaling was in effect this frame
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
    std::uint32_t applied_since_snap = 0;
    std::uint64_t snaps = 0, coalesced = 0, applied = 0, refused = 0, clamps = 0;
    std::uint64_t rotation_clamps = 0, position_clamps = 0;
};

// Geometric back-view test on the vanilla pose: the boom points behind the ship
// (ship-frame z < 0, mostly along the axis) and the camera looks along the
// ship's forward axis. The external views are script-defined (view position
// and angles), so the mode integer alone cannot tell them apart. Hysteresis
// (A6): a view enters at the tight thresholds and, once tracked, leaves only at
// the wide ones, so a transition animating through the boundary cannot flip
// the verdict every frame (each flip would refuse, and the return would snap).
inline bool back_view(Vec3 boom_local, const Mat3& view_rel, bool tracked) {
    const double side = tracked ? 0.8 : 0.6, above = tracked ? 2.0 : 1.5, forward = tracked ? 0.5 : 0.7;
    const double behind = -boom_local.z;
    if (!(behind > 0)) return false;
    if (std::fabs(boom_local.x) > side * behind) return false;
    if (std::fabs(boom_local.y) > above * behind) return false;
    return view_rel.m[2][2] > forward; // forward axes aligned within ~45 deg (enter) / 60 deg (leave)
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
    // A2: connect mode 3 and flag +0x1a0 & 4 make the engine write the basis
    // verbatim (camera.basis = +0xf0 at 0x00420c0c), so ship = view_rel^T *
    // camera below would be the identity and the back-view test would run in
    // world axes. Every other non-zero connect mode is scripted or unstudied.
    if (in.connect_mode == 3 || (in.flags_1a0 & 4)) return refuse(Verdict::VerbatimBasis);
    if (in.connect_mode != 0) return refuse(Verdict::SpecialConnect);
    // Effective ship basis from the vanilla identity camera = view_rel * ship:
    // ship = view_rel^T * camera. This follows the engine's selected native
    // basis on either position-domain branch (chase_camera_native.h).
    Mat3 ship = mul(transpose(in.view_rel), in.vanilla_cam);
    if (!orthonormalize(ship)) return refuse(Verdict::Degenerate);
    const Vec3 boom_world = in.vanilla_pos - in.ship_pos;
    const double boom = length(boom_world);
    if (!(boom > 0)) return refuse(Verdict::Degenerate);
    const Vec3 boom_local = mul(boom_world, transpose(ship));
    if (!back_view(boom_local, in.view_rel, s.tracking)) return refuse(Verdict::NotBackView);

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
    // A4: a gate jump is a teleport snap (16) followed by the sector snap (4)
    // one frame later, when +0x1fc catches up; that second, sector-only snap
    // re-seats the springs (a fraction of a degree of lag at most that soon
    // after a snap) but must not be a second TAA cut. Every other reason moves
    // the world or the view and always cuts.
    const bool coalesce = snap == 4 && s.tracking && s.applied_since_snap < t.snap_coalesce_frames;
    if (snap) reset(s);

    const double target_length = boom * t.distance_scale;
    Mat3 target;
    Vec3 target_boom;
    if (t.pitch_down_deg == 0) {
        // Explicit compatibility mode: retain the old boom and its pitch-up
        // framing, including native elevation/roll and their screen offset.
        target = mul(local_pitch(std::atan(t.offset_y * in.half_vfov_tan)), in.vanilla_cam);
        target_boom = mul(boom_local * t.distance_scale, ship);
    } else {
        // Ship-up frame, retaining native view yaw. The ship's own world roll
        // is preserved; native local camera roll/pitch are replaced. Construct
        // the boom from the desired projected anchor, rather than adding a
        // pitch to a native boom that may already be elevated.
        const Vec3 native_forward = row(in.view_rel, 2);
        const double horizontal = std::hypot(native_forward.x, native_forward.z);
        const double down = t.pitch_down_deg * pi / 180.0;
        const double screen_slope = t.offset_y * in.half_vfov_tan;
        const double elevation = down + std::atan(screen_slope);
        // Keep the vertical framing plane well away from a vertical/forward
        // boom. This also rejects extreme FOV/tunable combinations per frame.
        if (!(horizontal > 1e-9) || !std::isfinite(screen_slope) || std::fabs(elevation) >= 80.0 * pi / 180.0)
            return refuse(Verdict::InvalidInput);
        const Vec3 heading = {native_forward.x / horizontal, 0, native_forward.z / horizontal};
        const Vec3 right = {heading.z, 0, -heading.x};
        const Vec3 forward = heading * std::cos(down) + Vec3{0, -std::sin(down), 0};
        const Vec3 up = cross(forward, right);
        Mat3 relative = {{{right.x, right.y, right.z}, {up.x, up.y, up.z}, {forward.x, forward.y, forward.z}}};
        // Preserve the native horizontal anchor slope, but refuse native rays
        // beyond 60 degrees from forward: those are not an ordinary back view.
        const Vec3 native_ray = mul(boom_local * -1.0, transpose(in.view_rel));
        if (!(native_ray.z > 1e-9) || std::fabs(native_ray.x) > std::sqrt(3.0) * native_ray.z)
            return refuse(Verdict::NotBackView);
        const Vec3 camera_boom = {-native_ray.x / native_ray.z, screen_slope, -1};
        const Vec3 local_target_boom = mul(camera_boom * (target_length / length(camera_boom)), relative);
        // Native lateral offsets/yaw must not place the elevated target in
        // front of the ship despite passing the original native-view gate.
        if (!(local_target_boom.z < -0.1 * target_length)) return refuse(Verdict::NotBackView);
        target = mul(relative, ship);
        target_boom = mul(local_target_boom, ship);
    }
    if (!orthonormalize(target)) return refuse(Verdict::Degenerate);

    const double step_dt = std::fmin(std::fmax(dt, 0.0), t.max_dt);
    // A9: with a target locked both time constants shrink by (1 - tightness);
    // tightness 1 gives tau 0, which spring_step treats as a rigid follow.
    const double tight = in.target_locked ? 1.0 - t.combat_tightness : 1.0;
    r.target_locked = in.target_locked;
    if (snap) {
        s.basis = target; s.offset = target_boom; s.rot = {}; s.pos = {};
        s.tracking = true; ++s.snaps; r.snap_reason = snap; s.applied_since_snap = 0;
        if (coalesce) { r.coalesced = true; ++s.coalesced; } else r.snapped = true;
    } else {
        ++s.applied_since_snap;
        // Orientation: x = rotation vector from target to current (world frame),
        // current = target * exp(x). Re-measure against the new target, then relax.
        // (O1: the velocity v is a world-frame vector that is not transported
        // when the target rotates between frames; second order at the 8 deg
        // clamp, so it is left as is.)
        s.rot.x = log_rotation(mul(transpose(target), s.basis));
        spring_step(s.rot, t.rot_tau * tight, step_dt);
        if (clamp_spring(s.rot, t.lag_clamp_deg * pi / 180.0)) { ++s.clamps; ++s.rotation_clamps; }
        s.basis = mul(target, exp_rotation(s.rot.x));
        if (!orthonormalize(s.basis)) { r.snap_reason = 32; ++s.snaps; return refuse(Verdict::NumericFailure); }
        // Boom: x = previous boom - target boom (both ship-relative, so a
        // constant ship velocity leaves no lag; turns and boom changes swing),
        // relaxes toward zero.
        s.pos.x = s.offset - target_boom;
        spring_step(s.pos, t.pos_tau * tight, step_dt);
        if (clamp_spring(s.pos, t.pos_lag_clamp * target_length)) { ++s.clamps; ++s.position_clamps; }
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
