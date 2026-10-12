#pragma once
#include <cstdint>
#include <cstring>

// Portable core of the smooth ship steering law (docs/architecture/
// ship-steering-and-avoidance.md, sections 2, 4, 5 and 6; engine facts from
// docs/reverse-engineering/ship-movement.md). No Windows, no allocation, no
// CRT math: the DLL handler runs it inside the engine's steering leaves on the
// light CPU envelope (MXCSR and LastError only; check_no_x87.py walks it), so
// every float operation here is SSE2 (`sqrt_ss`, the polynomial `atan2`) and
// nothing returns a float through st(0). The host fixture
// verification/probe/ship_steering_host.cpp drives exactly this header against
// a kinematic model of the engine (the per-axis controller 0x0043ad20 in its
// integer 16.16 arithmetic, the integrator, the think tick).
//
// Sign conventions, pinned from the leaves' own fpatan operands
// (ship_steering_sites.h): with the target in the ship's basis rows as
// (t0, t1, t2) = (d.row0, d.row1, d.row2), the engine writes
//   yaw   obj+0x30 = atan2(-t0, t2)      (positive: the nose moves toward -row0)
//   pitch obj+0x34 = atan2( t1, t2')     (positive: the nose moves toward +row1)
//   roll  obj+0x38 = atan2( t1, t0)      (positive: row0 moves toward row1)
// The core never names "right"/"up": every angle is expressed in these rows,
// so whether row 0 is the ship's right or left (unresolved, [i]) does not
// change the written angles. World up (+y) is assumed for the level roll
// policy only ([i], first flight).
#if defined(__SSE2__) && (defined(__i386__) || defined(__x86_64__))
#include <emmintrin.h>
#define X3M_SHIP_STEERING_SSE2 1
#else
#include <cmath>
#define X3M_SHIP_STEERING_SSE2 0
#endif

namespace x3m::ship_steering::core {
constexpr float kUnitsPerMetre = 500.0f;     // ship-movement.md section 7: 1 position unit = 1/500 m
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;
constexpr float kBinaryPerRad = 65536.0f / kTwoPi; // binary angle: 65536 = one turn
constexpr float kRadPerBinary = kTwoPi / 65536.0f;
constexpr float kTau = 0.15f;       // s, the linear tail time constant (section 2.3 [p] 0.3; 0.15 keeps the final approach no slower than the vanilla leaf)
constexpr float kTickS = 0.05f;     // s, the worst think interval (reaction delay 1 x 50 ms)
constexpr float kFloorMs = 10.0f;   // m/s = the engine's 5000 units/s MovePos floor (section 2.5)
constexpr float kBankMaxRad = 35.0f * kPi / 180.0f; // section 2.6 [p]
constexpr float kCornerSinMin = 0.05f;             // below it the cornering term is infinite
constexpr float kDegenerateSin = 1e-6f;            // no lateral component at all: the axis defaults to yaw
constexpr float kBehindCos = -0.99996192f;         // cos 179.5 deg: straight behind turns by yaw (section 2.2)
constexpr float kArrivalHoldM = 1.0f;              // orders without an arrival distance: the hold radius (section 2.1 holds inside d_arr)
constexpr float kStuckMs = 30000.0f;               // ship_steering_stuck: no progress for 30 s
constexpr float kStuckProgressM = 1.0f;

// SG_SH_* codes (record +0x14; ship-navigation-ai.md section 5) and the engine's
// "large" predicate 0x0043fd00 as a mask over them (the byte table at
// 0x0043fd3c; verify_ship_steering_sites.py re-derives the mask).
constexpr unsigned kClassCount = 16;
constexpr const char* kClassNames[kClassCount] = {"TL", "TS", "M0", "M1", "M2", "M3", "M4", "M5",
                                                  "TL_P", "TS_P", "GO", "M6", "TP", "M7", "TM", "M8"};
constexpr std::uint32_t kLargeMask = 0xe91d; // {TL, M0, M1, M2, TL_P, M6, M7, TM, M8}
constexpr std::uint32_t kCapitalMask = 0x211d; // {TL, M0, M1, M2, TL_P, M7} (0x0043fd50; step 2 uses it)

// Order words (flight+0x258) and the steer set of section 1.4.
enum Order : std::uint16_t {
    kOrderNone = 0, kOrderFlyBefore = 4, kOrderDock = 5, kOrderTurnTo = 6, kOrderWarpAway = 8,
    kOrderAttack = 0x10, kOrderAttackBig = 0x11, kOrderAvoid = 0x12, kOrderEscort = 0x14, kOrderFollow = 0x15,
    kOrderCapture = 0x18, kOrderMovePos = 0x19, kOrderPlayerLand = 0x1b, kOrderIdle = 0x1d, kOrderUndock = 0x1e,
    kOrderRotate = 0x1f, kOrderGlide = 0x20, kOrderWarpIn = 0x21, kOrderDefensive = 0x22, kOrderMoveCont = 0x23,
    kOrderCollect = 0x24
};
// The per-order policy (sections 1.4, 2.5, 2.6, 3.1 and the ratification amendments): which orders the law
// steers, which get the arrival ramp toward d_arr (the others keep the engine's speed law and only the cornering
// term applies; 0x23 MovePosContinue writes vmax x f(alignment) with no distance term and completes at f+0x260, so
// a ramp would stop patrol ships at every waypoint: amendment 4), which write roll 0 (orders 4 and 5: the vanilla
// leaf writes 0 there and the docking match 0x00454767.. needs all three angles < 0x51e), and which the step-2
// avoidance layer may adjust (never 0x12, the engine's own evasion; never 5).
struct OrderPolicy {
    bool steered, arrival_ramp, roll_zero, avoidance;
};
inline OrderPolicy order_policy(std::uint16_t order) noexcept {
    switch (order) {
    case kOrderMovePos: return {true, true, false, true};
    case kOrderMoveCont: return {true, false, false, true};
    case kOrderFollow: return {true, true, false, true};
    case kOrderEscort: return {true, true, false, true};
    case kOrderFlyBefore: return {true, true, true, true};
    case kOrderDock: return {true, false, true, false};
    case kOrderAvoid: return {true, false, false, false};
    case kOrderTurnTo: case kOrderAttack: case kOrderAttackBig: case kOrderCapture: case kOrderIdle:
    case kOrderDefensive: case kOrderCollect:
        return {true, false, false, true};
    default:
        return {false, false, false, false};
    }
}
inline bool order_steered(std::uint16_t order) noexcept {
    return order_policy(order).steered;
}
inline bool order_has_arrival_ramp(std::uint16_t order) noexcept {
    return order_policy(order).arrival_ramp;
}
inline bool order_roll_zero(std::uint16_t order) noexcept {
    return order_policy(order).roll_zero;
}

enum class Reason : std::uint8_t {
    handled = 0,
    not_ship,          // class word != 7 (missiles, class 0x13 also enter 0x0044ccc0)
    no_autopilot,      // [obj+0x40] without 0x80000
    deleted,           // 0x8000000, attached 0x200 or docked [obj+0xf8]
    no_flight,         // [obj+0x50] == 0
    no_phys,           // [obj+0x70] == 0
    order_not_steered, // pass-through orders (8, 0x21, 0x1e, 0x1b, 0x1f, 0x20, 0, ...)
    class_excluded,    // ship_steering_classes mask
    no_angular_accel,  // record +0xd0 == 0 (the integrator skips the axis)
    no_rotation,       // yaw and pitch caps both 0
    not_finite,        // a non-finite intermediate
    table_full,        // no free state slot
    no_record,         // the class-7 type table slot is empty or the type index negative
    count
};
constexpr unsigned kReasonCount = unsigned(Reason::count);
inline const char* reason_name(Reason r) noexcept {
    static const char* const names[kReasonCount] = {"handled",    "not_ship",         "no_autopilot",  "deleted",
                                                   "no_flight",  "no_phys",          "order_not_steered", "class_excluded",
                                                   "no_angular_accel", "no_rotation", "not_finite",  "table_full",
                                                   "no_record"};
    return unsigned(r) < kReasonCount ? names[unsigned(r)] : "?";
}

// ---- SSE-safe scalar helpers --------------------------------------------
inline float sqrt_ss(float v) noexcept {
#if X3M_SHIP_STEERING_SSE2
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(v)));
#else
    return std::sqrt(v);
#endif
}
inline float fabs_f(float v) noexcept {
    return __builtin_fabsf(v);
}
inline float fmin_f(float a, float b) noexcept {
    return a < b ? a : b;
}
inline float fmax_f(float a, float b) noexcept {
    return a > b ? a : b;
}
inline bool finite_f(float v) noexcept {
    std::uint32_t bits;
    std::memcpy(&bits, &v, 4);
    return (bits & 0x7f800000u) != 0x7f800000u;
}
// atan on [-1, 1]: odd minimax polynomial of degree 11, |error| < 1.5e-5 rad
// (the engine's own angles are quantised to 1/65536 turn = 9.6e-5 rad).
__attribute__((always_inline)) inline float atan_unit(float x) noexcept {
    const float x2 = x * x;
    return x * (0.99997726f +
                x2 * (-0.33262347f + x2 * (0.19354346f + x2 * (-0.11643287f + x2 * (0.05265332f + x2 * -0.01172120f)))));
}
// atan2(y, x) in (-pi, pi], no CRT call. (0, 0) -> 0.
__attribute__((always_inline)) inline float atan2_f(float y, float x) noexcept {
    const float ax = fabs_f(x), ay = fabs_f(y);
    if (ax == 0.0f && ay == 0.0f) return 0.0f;
    float r;
    if (ay <= ax)
        r = atan_unit(ay / ax);
    else
        r = kPi * 0.5f - atan_unit(ax / ay);
    if (x < 0.0f) r = kPi - r;
    return y < 0.0f ? -r : r;
}
inline std::int32_t round_to_int(float v) noexcept {
    return v >= 0.0f ? std::int32_t(v + 0.5f) : -std::int32_t(0.5f - v);
}
inline float dot3(const float a[3], const float b[3]) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// ---- The law ----------------------------------------------------------------
struct Dynamics {
    float W[3] = {0, 0, 0}; // rate caps, rad/s (yaw, pitch, roll) = getters 0x00450d60/cf0/dd0
    float k = 0;            // angular acceleration factor: a_axis = W_axis * k (f+0x2b0 * record+0xd0 / 100)
    float v_max = 0;        // m/s (0x00450980)
    float a_lin = 0;        // m/s^2 (0x00450b80)
    float r_own = 0;        // m ([obj+0xa4])
    bool large = false;     // 0x0043fd00 predicate (level roll)
};
struct Inputs {
    float x[3] = {0, 0, 0};   // position, m
    float row[3][3] = {};     // the flight basis rows (f+0x870, 16.16 -> unit vectors), world frame
    float v = 0;              // current speed, m/s ([obj+0x10])
    float w[3] = {0, 0, 0};   // current rates, rad/s ([obj+0x24..])
    float P[3] = {0, 0, 0};   // the target point (after the avoidance seam), m
    std::uint16_t order = 0;  // f+0x258
    float d_arr = 0;          // arrival distance, m (section 2.5)
    float v_ref = 0;          // reference speed at arrival (the leader's projected speed), m/s
    bool arrival_ramp = false;
    float d_order = -1.0f;    // distance to the order's own point when P is a detour (m; < 0 = P is the order's point):
                              // the arrival hold, the ramp and the stuck witness measure progress to the order, not to the detour
    float tick_s = kTickS;    // the servo's rate-surplus horizon (s): servo_tick() below
};
struct Command {
    std::int32_t e[3] = {0, 0, 0}; // the remaining angles to write (binary)
    float e_rad[3] = {0, 0, 0};
    float theta = 0;         // bearing error, rad
    float n[2] = {0, 0};     // rotation axis components in engine sign (yaw, pitch)
    float omega = 0;         // commanded scalar rate, rad/s
    float omega_axis[3] = {0, 0, 0};
    float d = 0;             // distance to P, m
    float d_order = 0;       // distance to the order's point, m (= d without a detour)
    float v_corner = 0;      // m/s (0 = no limit)
    float v_cmd = 0;         // m/s, the clamp for site B (0 = none)
    bool held = false;       // inside the arrival hold: angles 0
};
// The rate servo of section 2.4: the remaining angle that makes the engine's
// controller accelerate toward |omega| and brake above it, never beyond the
// geometric remainder.
// The servo's horizon: the interval the rate surplus must cover until the next call. An order that calls the leaf
// every frame (Run151: follow/escort/dock, calls_per_ship_s 152 at 173 fps, carried a 50 ms surplus) gets the
// measured gap since the ship's previous call, clamped to [one frame, kTickS], once its cadence is established
// (kPerFrameRun consecutive gaps within 1.5 frames). A gated order, whose next call is a random 0..50 ms away, keeps
// the 50 ms worst case: its previous gap does not predict the next one, and a short budget lets the engine's
// controller brake to a stop before the next call (fixture: yaw reversals at 144 fps under the raw measured gap).
constexpr std::uint32_t kPerFrameRun = 8;
inline float servo_tick(std::uint32_t gap_ms, std::uint32_t frame_ms, std::uint32_t& run) noexcept {
    if (frame_ms < 1) frame_ms = 1;
    if (gap_ms <= frame_ms + frame_ms / 2) {
        if (run < kPerFrameRun) ++run;
    } else {
        run = 0;
    }
    if (run < kPerFrameRun) return kTickS;
    if (gap_ms < frame_ms) gap_ms = frame_ms;
    if (gap_ms > 50) gap_ms = 50;
    return float(gap_ms) * 0.001f;
}
inline float servo_angle(float theta_axis, float omega_axis, float a_axis, float tick_s = kTickS) noexcept {
    const float mag = fabs_f(theta_axis);
    if (omega_axis == 0.0f || mag == 0.0f) return 0.0f;
    const float ow = fabs_f(omega_axis);
    float e = ow * ow / a_axis + ow * tick_s;
    if (e > mag) e = mag;
    return omega_axis < 0.0f ? -e : e;
}
// The scalar rate command of section 2.3 along a plane with caps W_n, a_n.
inline float rate_command(float theta, float W_n, float a_n) noexcept {
    float w = sqrt_ss(a_n * theta);
    const float lin = theta / kTau;
    if (lin < w) w = lin;
    if (W_n < w) w = W_n;
    return w;
}
// The speed schedule of section 2.5 at distance d (m): v_arr from the
// arrival ramp (when the order has one) and the cornering limit, floored.
__attribute__((always_inline)) inline float schedule_speed(float d, float d_arr, float a_lin, float v_ref, bool arrival_ramp, float v_corner) noexcept {
    float v = 0.0f;
    bool limited = false;
    if (arrival_ramp && a_lin > 0.0f) {
        const float over = d - d_arr;
        v = v_ref + (over > 0.0f ? sqrt_ss(a_lin * over) : 0.0f);
        limited = true;
    }
    if (v_corner > 0.0f && (!limited || v_corner < v)) {
        v = v_corner;
        limited = true;
    }
    if (!limited) return 0.0f;
    return v < kFloorMs ? kFloorMs : v;
}
// One steering decision. Returns handled with *out filled, or the reason the
// call passes through to the original leaf.
inline Reason steer(const Inputs& in, const Dynamics& dyn, Command* out) noexcept {
    Command c;
    const bool yaw_ok = dyn.W[0] > 0.0f, pitch_ok = dyn.W[1] > 0.0f, roll_ok = dyn.W[2] > 0.0f;
    if (!yaw_ok && !pitch_ok) return Reason::no_rotation;
    if (dyn.k <= 0.0f) return Reason::no_angular_accel;
    float d[3] = {in.P[0] - in.x[0], in.P[1] - in.x[1], in.P[2] - in.x[2]};
    c.d = sqrt_ss(dot3(d, d));
    if (!finite_f(c.d)) return Reason::not_finite;
    c.d_order = in.d_order >= 0.0f ? in.d_order : c.d;
    // Target in the basis rows. Inside the order's arrival distance (section 2.1) the heading is held: all three
    // angles 0, the engine's own arrival test runs on the next tick. A detour point (step 2) never holds: the
    // hold and the ramp below measure the order's own point.
    const float hold = (in.arrival_ramp && in.d_arr > kArrivalHoldM) ? in.d_arr : kArrivalHoldM;
    float t[3] = {0, 0, 0};
    if (c.d_order > hold && c.d > kArrivalHoldM) {
        const float inv = 1.0f / c.d;
        t[0] = dot3(d, in.row[0]) * inv;
        t[1] = dot3(d, in.row[1]) * inv;
        t[2] = dot3(d, in.row[2]) * inv;
    } else
        c.held = true;
    const float sin_t = sqrt_ss(t[0] * t[0] + t[1] * t[1]);
    c.theta = c.held ? 0.0f : atan2_f(sin_t, t[2]);
    // Rotation axis in engine signs: yaw -t0/sin, pitch t1/sin (section 2.2).
    float n_y = 0.0f, n_p = 0.0f;
    if (!c.held) {
        if (sin_t > kDegenerateSin && t[2] > kBehindCos) {
            n_y = -t[0] / sin_t;
            n_p = t[1] / sin_t;
        } else
            n_y = t[0] > 0.0f ? -1.0f : 1.0f; // no lateral component, or straight behind: turn by yaw (toward the target's side)
    }
    const float geo_y = c.theta * n_y, geo_p = c.theta * n_p; // the geometric remainders on each axis
    // Dropped axes (section 6.2): the rate split renormalises over the remaining ones.
    float s_y = yaw_ok ? n_y : 0.0f, s_p = pitch_ok ? n_p : 0.0f;
    const float norm = sqrt_ss(s_y * s_y + s_p * s_p);
    if (norm > 0.0f) {
        s_y /= norm;
        s_p /= norm;
    }
    c.n[0] = s_y;
    c.n[1] = s_p;
    // The cap of the slowest axis in this plane (section 2.3).
    float W_n = 1e30f, a_n = 1e30f;
    if (fabs_f(s_y) > 1e-3f) {
        W_n = fmin_f(W_n, dyn.W[0] / fabs_f(s_y));
        a_n = fmin_f(a_n, dyn.W[0] * dyn.k / fabs_f(s_y));
    }
    if (fabs_f(s_p) > 1e-3f) {
        W_n = fmin_f(W_n, dyn.W[1] / fabs_f(s_p));
        a_n = fmin_f(a_n, dyn.W[1] * dyn.k / fabs_f(s_p));
    }
    if (W_n >= 1e29f) { // no axis carries the rotation (both tiny): hold
        W_n = 0.0f;
        a_n = 1.0f;
    }
    const float theta_eff = c.held ? 0.0f : c.theta * norm; // the part of the turn the live axes can execute
    c.omega = rate_command(theta_eff, W_n, a_n);
    c.omega_axis[0] = c.omega * s_y;
    c.omega_axis[1] = c.omega * s_p;
    const float tick = (in.tick_s > 0.0f && in.tick_s < kTickS) ? in.tick_s : kTickS;
    c.e_rad[0] = yaw_ok ? servo_angle(geo_y, c.omega_axis[0], dyn.W[0] * dyn.k, tick) : 0.0f;
    c.e_rad[1] = pitch_ok ? servo_angle(geo_p, c.omega_axis[1], dyn.W[1] * dyn.k, tick) : 0.0f;
    // Roll policy (section 2.6): the roll from level (row0 horizontal, row1 up)
    // plus the bank, through the same servo.
    float e_roll = 0.0f;
    if (roll_ok && !order_roll_zero(in.order) && !c.held) {
        const float phi_level = atan2_f(-in.row[0][1], in.row[1][1]);
        float phi_cmd = phi_level;
        if (!dyn.large && yaw_ok)
            phi_cmd += kBankMaxRad * (c.omega_axis[0] / dyn.W[0]); // a turn toward +row0 (yaw < 0) banks row1 toward row0
        // Shortest way.
        if (phi_cmd > kPi) phi_cmd -= kTwoPi;
        if (phi_cmd < -kPi) phi_cmd += kTwoPi;
        const float a_r = dyn.W[2] * dyn.k;
        const float w_r = rate_command(fabs_f(phi_cmd), dyn.W[2], a_r);
        c.omega_axis[2] = phi_cmd < 0.0f ? -w_r : w_r;
        e_roll = servo_angle(phi_cmd, c.omega_axis[2], a_r, tick);
    }
    c.e_rad[2] = e_roll;
    for (unsigned i = 0; i < 3; ++i) {
        if (!finite_f(c.e_rad[i])) return Reason::not_finite;
        float b = c.e_rad[i] * kBinaryPerRad;
        if (b > 32767.0f) b = 32767.0f;
        if (b < -32768.0f) b = -32768.0f;
        c.e[i] = round_to_int(b);
    }
    // Cornering (section 2.5): the turning circle at the slower axis must pass through P.
    const float omega_min = fmin_f(yaw_ok ? dyn.W[0] : 1e30f, pitch_ok ? dyn.W[1] : 1e30f);
    c.v_corner = (!c.held && sin_t > kCornerSinMin && omega_min < 1e29f) ? omega_min * c.d / (2.0f * sin_t) : 0.0f;
    // Section 6.2: a record without a max speed or an acceleration gets no clamp at all (steering still runs).
    c.v_cmd = (dyn.v_max > 0.0f && dyn.a_lin > 0.0f) ? schedule_speed(c.d_order, in.d_arr, dyn.a_lin, in.v_ref, in.arrival_ramp, c.v_corner) : 0.0f;
    if (!finite_f(c.v_cmd) || !finite_f(c.v_corner)) return Reason::not_finite;
    *out = c;
    return Reason::handled;
}

// ---- Per-ship state (section 8: 1024 entries, open addressing on obj) ------
struct Ship {
    std::uint32_t obj = 0;
    std::uint32_t stamp_ms = 0;   // game time of the last handled call
    std::uint16_t order = 0;
    std::uint16_t flags = 0;      // bit 0 arrival ramp, bit 1 clamp enabled (v_max and a both > 0)
    float P[3] = {0, 0, 0};       // the last target, m
    float a_lin = 0;              // m/s^2 at the last call
    float d_arr = 0;              // m
    float v_ref = 0;              // m/s
    float v_corner = 0;           // m/s (0 = none)
    float best_d = -1.0f;         // the closest the ship has been to its MovePos point, m (-1 = not started)
    std::uint32_t best_stamp = 0; // when best_d last improved
    std::uint32_t calls = 0;
    float v_emergency = 0;        // m/s, the avoidance layer's braking ramp when no detour exists (0 = none)
    std::uint32_t last_call_ms = 0; // game time of the previous handled call (the servo's measured interval)
    std::uint32_t per_frame_run = 0; // consecutive calls one frame apart (servo_tick)
};
static_assert(sizeof(Ship) == 64, "one cache line per ship");
constexpr unsigned kShipCapacity = 1024;
constexpr unsigned kProbeLimit = 8;
constexpr std::uint32_t kStaleMs = 5000; // an entry older than this may be reused
constexpr std::uint16_t kShipFlagRamp = 1, kShipFlagClamp = 2, kShipFlagGate = 4, // Ship.flags: arrival ramp, clamp enabled, gate approach
                        kShipFlagGateDock = 8; // order 5 to a gate: the cornering speed toward B replaces the engine's 1 m/s creep
constexpr float kGateDockFloorMs = 20.0f; // order 5 to a gate: the cornering speed's floor (the engine's creep is 1 m/s)
// Order 5 to a gate: the engine's ramp (0x004548f5..0x00454977): vmax/2 at r/5, vmax at r, linear between.
__attribute__((always_inline)) inline float gate_dock_ramp(float d, float r, float v_max) noexcept {
    if (!(r > 0.0f)) return v_max;
    if (d <= 0.2f * r) return 0.5f * v_max;
    if (d >= r) return v_max;
    return 0.5f * v_max + 0.5f * v_max * (d - 0.2f * r) / (0.8f * r);
}
inline unsigned ship_hash(std::uint32_t obj) noexcept {
    return ((obj >> 4) * 0x9e3779b1u) >> (32 - 10);
}
struct ShipTable {
    Ship entries[kShipCapacity];
    // The entry for obj, inserted when absent (a stale slot on the probe path is
    // reused); nullptr when every probed slot is live and foreign.
    Ship* find_or_insert(std::uint32_t obj, std::uint32_t now_ms) noexcept {
        unsigned i = ship_hash(obj);
        Ship* stale = nullptr;
        for (unsigned probe = 0; probe < kProbeLimit; ++probe, i = (i + 1) & (kShipCapacity - 1)) {
            Ship& s = entries[i];
            if (s.obj == obj) return &s;
            if (s.obj == 0) {
                stale = stale ? stale : &s;
                break;
            }
            if (!stale && now_ms - s.stamp_ms > kStaleMs) stale = &s;
        }
        if (!stale) return nullptr;
        *stale = Ship{};
        stale->obj = obj;
        return stale;
    }
    // Read-only lookup for site B: the live entry or nullptr.
    const Ship* find(std::uint32_t obj) const noexcept {
        unsigned i = ship_hash(obj);
        for (unsigned probe = 0; probe < kProbeLimit; ++probe, i = (i + 1) & (kShipCapacity - 1)) {
            const Ship& s = entries[i];
            if (s.obj == obj) return &s;
            if (s.obj == 0) return nullptr;
        }
        return nullptr;
    }
    unsigned live(std::uint32_t now_ms, std::uint32_t window_ms) const noexcept {
        unsigned n = 0;
        for (unsigned i = 0; i < kShipCapacity; ++i)
            if (entries[i].obj && now_ms - entries[i].stamp_ms <= window_ms) ++n;
        return n;
    }
};
// Site B (section 2.5): the clamp for the ship at position x (m) from its last
// decision, recomputed with the current distance; 0 when nothing applies
// (stale, another order, no schedule).
__attribute__((always_inline)) inline float clamp_for(const Ship& s, const float x[3], std::uint16_t order, std::uint32_t now_ms, std::uint32_t max_age_ms) noexcept {
    if (!(s.flags & kShipFlagClamp) && s.v_emergency <= 0.0f) return 0.0f;
    if (s.order != order || now_ms - s.stamp_ms > max_age_ms) return 0.0f;
    float v;
    if (s.flags & kShipFlagGate) { // the gate approach: v_ref holds gate_speed(); the emergency ramp below still wins
        v = finite_f(s.v_ref) && s.v_ref > 0.0f ? s.v_ref : 0.0f;
        if (s.flags & kShipFlagRamp) { // a capital (amendment 8): its arrival ramp to the grid point stays, the lower of the two
            const float d[3] = {s.P[0] - x[0], s.P[1] - x[1], s.P[2] - x[2]};
            const float ramp = schedule_speed(sqrt_ss(dot3(d, d)), s.d_arr, s.a_lin, 0.0f, true, s.v_corner);
            if (ramp > 0.0f && ramp < v) v = ramp;
        }
    } else if (s.flags & kShipFlagGateDock) { // order 5 to a gate: v_ref = vmax, d_arr = r_gate, the cornering term from the decision
        const float d[3] = {s.P[0] - x[0], s.P[1] - x[1], s.P[2] - x[2]};
        const float ramp = gate_dock_ramp(sqrt_ss(dot3(d, d)), s.d_arr, s.v_ref);
        v = ramp;
        if (s.v_corner > 0.0f && s.v_corner < v) v = s.v_corner;
        // the floor: the cornering term collapses near B while the ship is still off heading (the pursuit's singularity);
        // kGateDockFloorMs keeps the leg moving (the engine's arrival radius |v|/10 + r_ship absorbs the small miss), never
        // above the engine's own aligned value (vmax/2 can be below it)
        const float floor = ramp < kGateDockFloorMs ? ramp : kGateDockFloorMs;
        if (v < floor) v = floor;
    } else {
        const float d[3] = {s.P[0] - x[0], s.P[1] - x[1], s.P[2] - x[2]};
        const float dist = sqrt_ss(dot3(d, d));
        v = schedule_speed(dist, s.d_arr, s.a_lin, s.v_ref, (s.flags & kShipFlagRamp) != 0, s.v_corner);
    }
    if (s.v_emergency > 0.0f && (v <= 0.0f || s.v_emergency < v)) v = fmax_f(s.v_emergency, kFloorMs);
    return finite_f(v) ? v : 0.0f;
}
// acos in degrees for a cosine in [-1, 1] (the crossing heading error of the --debug row): the identity
// acos(c) = 2 atan2(sqrt(1 - c), sqrt(1 + c)) over the module's own atan2, no CRT call.
// always inlined: an out-of-line float return would travel through st(0) on i386 (the x87 audit)
__attribute__((always_inline)) inline float acos_deg(float c) noexcept {
    if (c >= 1.0f) return 0.0f;
    if (c <= -1.0f) return 180.0f;
    return 2.0f * atan2_f(sqrt_ss(1.0f - c), sqrt_ss(1.0f + c)) * (180.0f / kPi);
}
// ---- the gate approach (amendment 7; ship-navigation-ai.md section 10.7 P1, P4, P5) -----------------------------
// A non-capital ship on order 4 to a gate (target class 0x12) keeps the engine's point A = G + R(0, -0.8, -1) r in
// f+0x240 (the order's own arrival test, dist(A) <= 0.75 r, is unchanged) but is steered along the lead-in line
// through A with direction h = sin(23.6 deg) row1 + cos(23.6 deg) row2 (the middle of the pass point's 13-34 deg
// bearing spread), so it crosses the 0.75 r sphere heading within 10.4 deg of any pass point B: order 5 then never
// takes its 1 m/s turn. The point handed to the leaf is the carrot on that line kGateLookS of flight (at least
// kGateLookMinR r) ahead of the ship's projection; farther than kGateJoinR r before the crossing the ship aims at A
// itself (a straight approach, the join at the end). The speed is the module's: vmax, lowered by the cornering
// term and the engine's own cap for large hulls (0x004550ed: 50000 units/s), written at site B in both directions
// (the engine's order-4 ramp to 5000 units/s at A, 0x00455052, is the stop P2 removes). Capitals keep every vanilla
// step (their next order, 0x1f RotateToAngles, expects a stationary ship).
constexpr float kGateSin = 0.40035f, kGateCos = 0.91636f; // sin / cos 23.6 deg
constexpr float kGateCrossFraction = 0.75f;               // the order's arrival radius around A, in r (0x00454b1a)
constexpr float kGateApproachY = -0.8f, kGateApproachZ = -1.0f; // the approach vector [gate+0x50]+0x1a0 (0x00440e75..)
constexpr float kGateLookS = 0.75f, kGateLookMinR = 0.3f, kGateJoinR = 3.0f, kGateJoinInnerR = 2.0f;
// Step 4 (amendment 8): the per-ship spread. Eight ring positions in the gate's x-y plane (rows 0 and 1), chosen
// by a hash of the object pointer (stable for the ship's life). The approach ring is kGateSpreadR r: the shifted
// lead-in line still enters the 0.75 r completion sphere (margin 0.5 r) and the worst heading error to a pass
// point on the 0.55 r / +-45 deg arc is 13.9 deg (verification/results/ship-steering/spread_bound.py: 12.1 at 0.20 r,
// 15.8 at 0.30 r, 17.8 at 0.35 r against the 18.2 deg creep threshold); adjacent crossings are 211 m apart. The
// exit ring (the warp-in position) is kGateExitSpreadR r_gate, never along the axis.
constexpr unsigned kGateSpreadRing = 8;
constexpr float kGateSpreadR = 0.25f, kGateExitSpreadR = 0.3f;
constexpr float kGateRingCos[kGateSpreadRing] = {0.92388f, 0.38268f, -0.38268f, -0.92388f, -0.92388f, -0.38268f, 0.38268f, 0.92388f};
constexpr float kGateRingSin[kGateSpreadRing] = {0.38268f, 0.92388f, 0.92388f, 0.38268f, -0.38268f, -0.92388f, -0.92388f, -0.38268f};
inline unsigned gate_spread_index(std::uint32_t obj) noexcept {
    return (obj * 0x9E3779B1u) >> 29; // 0..7 from the top bits of a multiplicative hash
}
inline void gate_spread_offset(unsigned k, float rho, const float row0[3], const float row1[3], float out[3]) noexcept {
    k &= kGateSpreadRing - 1;
    for (unsigned i = 0; i < 3; ++i) out[i] = rho * (kGateRingCos[k] * row0[i] + kGateRingSin[k] * row1[i]);
}
// The capital grid (ship-navigation-ai.md 10.2, KC GetWarpInPos): A = G - 2 (r_gate + r_ship) F + 666 m (a R + b U),
// a in {0, 1, -1, 2, -2}, b in {0, -1, -2, -3}; the rotate step (order 0x1f) then turns the ship to the gate's yaw
// and pitch, i.e. to F (row 2) within 0xfd70 = cos 8.1 deg.
constexpr float kGateGridPitchM = 666.0f, kGateGridTolM = 20.0f, kGateGridDepthTol = 0.1f, kGateRotateTolDeg = 8.11f;
constexpr float kGateLargeCapMs = 100.0f;                 // 50000 units/s, the engine's cap for large hulls on order 4
constexpr float kGateGeometryTol = 0.02f;                 // |A - (G + R a r)| <= this x r, else the vanilla point (fail closed)
struct GateGeometry {
    float h[3];  // the lead-in direction, unit, world
    float X[3];  // the crossing point A - 0.75 r h, m
    float A[3];  // the approach point, m
    float r;     // the gate's collision radius, m
};
// The capital lead-in (amendment 8): A must be one of the 20 grid points in front of the gate (within
// kGateGridDepthTol of the depth and kGateGridTolM of the grid); the line direction is the gate axis F = row2, so the
// ship arrives already facing the gate's angles. No spread.
inline bool gate_geometry_capital(const float A[3], const float G[3], const float row0[3], const float row1[3], const float row2[3],
                                  float r_gate, float r_ship, GateGeometry* out) noexcept {
    if (!(r_gate > 1.0f) || !finite_f(r_gate) || !(r_ship >= 0.0f) || !finite_f(r_ship)) return false;
    float d[3];
    for (unsigned i = 0; i < 3; ++i) {
        if (!finite_f(A[i]) || !finite_f(G[i]) || !finite_f(row0[i]) || !finite_f(row1[i]) || !finite_f(row2[i])) return false;
        d[i] = A[i] - G[i];
    }
    const float lx = dot3(d, row0), ly = dot3(d, row1), lz = dot3(d, row2);
    const float depth = -2.0f * (r_gate + r_ship);
    if (fabs_f(lz - depth) > kGateGridDepthTol * fabs_f(depth)) return false;
    const float a = round_to_int(lx / kGateGridPitchM) * 1.0f, b = round_to_int(ly / kGateGridPitchM) * 1.0f;
    if (a < -2.0f || a > 2.0f || b < -3.0f || b > 0.0f) return false;
    if (fabs_f(lx - a * kGateGridPitchM) > kGateGridTolM || fabs_f(ly - b * kGateGridPitchM) > kGateGridTolM) return false;
    const float n = sqrt_ss(dot3(row2, row2));
    if (!(n > 0.9f) || !(n < 1.1f)) return false;
    for (unsigned i = 0; i < 3; ++i) {
        out->h[i] = row2[i] / n;
        out->A[i] = A[i];
        out->X[i] = A[i] - kGateCrossFraction * r_gate * out->h[i];
    }
    out->r = r_gate;
    return true;
}
// The gate frame's rows (16.16 unit vectors read from the phys block): each within 10 % of unit length and mutually
// orthogonal within 0.1; both spreads and the lead-in use all three.
inline bool gate_rows_valid(const float row0[3], const float row1[3], const float row2[3]) noexcept {
    const float rows[3][3] = {{row0[0], row0[1], row0[2]}, {row1[0], row1[1], row1[2]}, {row2[0], row2[1], row2[2]}};
    for (unsigned r = 0; r < 3; ++r) {
        for (unsigned c = 0; c < 3; ++c)
            if (!finite_f(rows[r][c])) return false;
        const float n2 = dot3(rows[r], rows[r]);
        if (!(n2 > 0.81f) || !(n2 < 1.21f)) return false;
    }
    return fabs_f(dot3(rows[0], rows[1])) <= 0.1f && fabs_f(dot3(rows[1], rows[2])) <= 0.1f && fabs_f(dot3(rows[0], rows[2])) <= 0.1f;
}
// Validates the engine's point A against the gate frame (fail closed: a layout other than the pinned one gives false
// and the ship keeps the vanilla point) and fills the lead-in geometry.
inline bool gate_geometry(const float A[3], const float G[3], const float row0[3], const float row1[3], const float row2[3], float r,
                          GateGeometry* out) noexcept {
    if (!(r > 1.0f) || !finite_f(r) || !gate_rows_valid(row0, row1, row2)) return false;
    float expect[3], d[3];
    for (unsigned i = 0; i < 3; ++i) {
        expect[i] = G[i] + (kGateApproachY * row1[i] + kGateApproachZ * row2[i]) * r;
        d[i] = A[i] - expect[i];
        if (!finite_f(A[i]) || !finite_f(G[i])) return false;
    }
    if (dot3(d, d) > kGateGeometryTol * kGateGeometryTol * r * r) return false;
    for (unsigned i = 0; i < 3; ++i) {
        out->h[i] = kGateSin * row1[i] + kGateCos * row2[i];
        out->A[i] = A[i];
    }
    const float n = sqrt_ss(dot3(out->h, out->h));
    for (unsigned i = 0; i < 3; ++i) {
        out->h[i] /= n;
        out->X[i] = A[i] - kGateCrossFraction * r * out->h[i];
    }
    out->r = r;
    return true;
}
// Order 5 to a gate: B must be the pinned pass point, B - G in gate rows = (0, -0.55 cos theta, 0.55 sin theta) r with
// |theta| <= 45 deg (section 10.5; gate subtypes 5..8 use the pass offset -0.05 r on the same bubble scene: amendment
// 9), the rows orthonormal, r_gate > 0. Anything else keeps the engine's own speed law (fail closed).
constexpr float kGatePassY0 = 0.55f, kGatePassY0Small = 0.05f, kGatePassTol = 0.03f; // the tolerance is absolute, in r
inline float gate_pass_y0(std::uint16_t gate_subtype) noexcept {
    return gate_subtype >= 5 && gate_subtype <= 8 ? kGatePassY0Small : kGatePassY0;
}
inline bool gate_pass_point_valid(const float B[3], const float G[3], const float row0[3], const float row1[3], const float row2[3], float r,
                                  std::uint16_t gate_subtype) noexcept {
    if (!(r > 1.0f) || !finite_f(r) || !gate_rows_valid(row0, row1, row2)) return false;
    float d[3];
    for (unsigned i = 0; i < 3; ++i) {
        if (!finite_f(B[i]) || !finite_f(G[i])) return false;
        d[i] = B[i] - G[i];
    }
    const float y0 = gate_pass_y0(gate_subtype), tol = kGatePassTol * r;
    const float lx = dot3(d, row0), ly = dot3(d, row1), lz = dot3(d, row2);
    if (fabs_f(lx) > tol) return false;
    const float rho = sqrt_ss(ly * ly + lz * lz);
    if (fabs_f(rho - y0 * r) > tol) return false;
    // |theta| <= 45 deg: ly = -y0 cos theta r <= -0.707 y0 r, |lz| <= 0.707 y0 r
    return ly <= -0.7071f * y0 * r + tol && fabs_f(lz) <= 0.7071f * y0 * r + tol;
}
// The point to steer at for a ship at x with speed v (m/s), or false = steer at the engine's A. s = the ship's
// projection along the line from X (< 0 before the crossing), e = its distance from the line. The lead-in applies
// only in front of the crossing plane and within kGateJoinR r of it (-kGateJoinR r <= s <= 0): a ship beside, above
// or behind the gate, or already past the crossing (s > 0), aims at A as the engine does (the reviewed failure: a
// carrot beyond such a ship's own projection drove it away from A; the disc lies at s >= 1.6 r, so no path from
// s <= 0 to a point at or before A crosses it). Inside the window the point is the carrot kGateLookS of flight
// (at least kGateLookMinR r) ahead of the ship's projection, never beyond A; a ship laterally off the line turns
// toward it first (the pursuit converges within ~3 lookaheads). The far end is blended: between kGateJoinInnerR r
// and kGateJoinR r before the crossing the carrot slides from A to the near carrot, so the switch from the straight
// approach is continuous. Envelope (fixture): approaches that enter the window with the lateral offset below ~2 r
// cross within 10 deg of the line; a ship that reaches the arrival sphere from s > 0 crosses as vanilla does.
inline bool gate_lead_in(const GateGeometry& g, const float x[3], float v, float P[3], float* s_out, float* e_out = nullptr) noexcept {
    const float dx[3] = {x[0] - g.X[0], x[1] - g.X[1], x[2] - g.X[2]};
    const float s = dot3(dx, g.h);
    if (s_out) *s_out = s;
    const float d2 = dot3(dx, dx) - s * s;
    if (e_out) *e_out = d2 > 0.0f ? sqrt_ss(d2) : 0.0f;
    const float a_along = kGateCrossFraction * g.r; // A's position along the line from X (the line ends at A, or A + the spread)
    if (s > a_along + 1.0f || s < -kGateJoinR * g.r) return false;
    float look = kGateLookS * (v > 0.0f ? v : 0.0f);
    if (look < kGateLookMinR * g.r) look = kGateLookMinR * g.r;
    float along = s + look;
    if (along > a_along) along = a_along;
    const float t = (-s - kGateJoinInnerR * g.r) / ((kGateJoinR - kGateJoinInnerR) * g.r); // 0 at the inner edge, 1 at the join point
    if (t > 0.0f) along += (t < 1.0f ? t : 1.0f) * (a_along - along);
    for (unsigned i = 0; i < 3; ++i) P[i] = g.X[i] + g.h[i] * along;
    return true;
}
// The gate-site rule shared by the DLL handlers (P2, P4) and the fixture: `obj` is a class-7 ship whose order target
// f+0x224 is a gate (class 0x12) and whose record code (+0x14 of the TShips record) is not capital (0x0043fd50:
// kCapitalMask). Reads through the same memory reader the walk core uses.
constexpr std::uint32_t kGateObjClass = 0x48, kGateObjType = 0x4a, kGateObjFlight = 0x50, kGateFlightTarget = 0x224;
constexpr std::uint32_t kGateRecordStride = 0xdb8, kGateRecordClassCode = 0x14;
constexpr std::uint16_t kGateClassShip = 7, kGateClassGate = 0x12;
template <class Mem>
inline bool ship_noncapital(Mem& mem, std::uint32_t obj, std::uint32_t type_table) noexcept {
    if (!obj || mem.u16(obj + kGateObjClass) != kGateClassShip) return false;
    const std::int32_t type = std::int16_t(mem.u16(obj + kGateObjType));
    if (!type_table || type < 0) return false;
    const std::uint32_t code = mem.u32(type_table + std::uint32_t(type) * kGateRecordStride + kGateRecordClassCode);
    return !(code < kClassCount && ((kCapitalMask >> code) & 1u));
}
template <class Mem>
inline bool gate_target_noncapital(Mem& mem, std::uint32_t obj, std::uint32_t type_table) noexcept {
    if (!obj || mem.u16(obj + kGateObjClass) != kGateClassShip) return false;
    const std::uint32_t fl = mem.u32(obj + kGateObjFlight);
    if (!fl) return false;
    const std::uint32_t tgt = mem.u32(fl + kGateFlightTarget);
    if (!tgt || mem.u16(tgt + kGateObjClass) != kGateClassGate) return false;
    return ship_noncapital(mem, obj, type_table);
}
// P2 / P4: the speed is kept only for a ship the policy steers (a gate state exists: the decision validated the gate
// frame and A) whose target is still a gate and which is not capital; anything else takes the engine's stop.
inline bool gate_stop_keeps_speed(bool noncapital_gate_target, bool state_valid) noexcept {
    return noncapital_gate_target && state_valid;
}
// The exit spread (site W, amendment 8): the ship's position offset by its ring position at kGateExitSpreadR r_gate in
// the gate's x-y plane, for a class-7 non-capital ship under the autopilot flag (never the player's ship), not deleted
// or attached, whose record class is not excluded, not already on order 0x21 (a re-issued SA_WarpObjectIn), at a
// class-0x12 target with a valid frame; the new position in units (int32-guarded). Returns false = no write.
constexpr std::uint32_t kGateObjFlags = 0x40, kGateFlagAutopilot = 0x80000, kGateFlagDeleted = 0x8000000, kGateFlagAttached = 0x200;
constexpr std::uint32_t kGateObjPhys = 0x70, kGateObjRadius = 0xa4, kGatePhysPosition = 0x30, kGatePhysBasis = 0x40, kGateFlightOrder = 0x258;
constexpr std::uint16_t kGateOrderWarpIn = 0x21;
template <class Mem>
inline bool gate_warp_in_offset(Mem& mem, std::uint32_t obj, std::uint32_t target, std::uint32_t type_table, std::uint32_t class_mask,
                                std::int32_t out_units[3]) noexcept {
    if (!target || mem.u16(target + kGateObjClass) != kGateClassGate) return false;
    if (!ship_noncapital(mem, obj, type_table)) return false;
    const std::int32_t type = std::int16_t(mem.u16(obj + kGateObjType));
    const std::uint32_t code = mem.u32(type_table + std::uint32_t(type) * kGateRecordStride + kGateRecordClassCode);
    if (code < kClassCount && ((class_mask >> code) & 1u)) return false;
    const std::uint32_t flags = mem.u32(obj + kGateObjFlags);
    if (!(flags & kGateFlagAutopilot) || (flags & (kGateFlagDeleted | kGateFlagAttached))) return false;
    const std::uint32_t fl = mem.u32(obj + kGateObjFlight);
    if (!fl || mem.u16(fl + kGateFlightOrder) == kGateOrderWarpIn) return false;
    const std::uint32_t ph = mem.u32(obj + kGateObjPhys), gph = mem.u32(target + kGateObjPhys);
    const std::int32_t r_units = mem.s32(target + kGateObjRadius);
    if (!ph || !gph || r_units <= 0) return false;
    float row0[3], row1[3], row2[3], off[3];
    for (unsigned i = 0; i < 3; ++i) {
        row0[i] = float(mem.s32(gph + kGatePhysBasis + 4 * i)) * (1.0f / 65536.0f);
        row1[i] = float(mem.s32(gph + kGatePhysBasis + 0x10 + 4 * i)) * (1.0f / 65536.0f);
        row2[i] = float(mem.s32(gph + kGatePhysBasis + 0x20 + 4 * i)) * (1.0f / 65536.0f);
    }
    if (!gate_rows_valid(row0, row1, row2)) return false;
    gate_spread_offset(gate_spread_index(obj), kGateExitSpreadR * float(r_units) / kUnitsPerMetre, row0, row1, off);
    for (unsigned i = 0; i < 3; ++i) {
        const float u = off[i] * kUnitsPerMetre;
        if (!finite_f(u) || u > 1.0e9f || u < -1.0e9f) return false;
        const std::int64_t sum = std::int64_t(mem.s32(ph + kGatePhysPosition + 4 * i)) + round_to_int(u);
        if (sum > 0x7fffffff || sum < -0x7fffffff) return false;
        out_units[i] = std::int32_t(sum);
    }
    return true;
}
// The approach speed written at site B (m/s): vmax, the large-hull cap, the cornering term when it is lower.
__attribute__((always_inline)) inline float gate_speed(float v_max, bool large, float v_corner) noexcept {
    float v = v_max;
    if (large && kGateLargeCapMs < v) v = kGateLargeCapMs;
    if (v_corner > 0.0f && v_corner < v) v = v_corner;
    return v < kFloorMs ? kFloorMs : v;
}
// Order 5 to a gate (Run152 follow-up): the engine's own speed law is ramp(dist to B; r/5 -> vmax/2, r -> vmax) when
// aligned within 18.2 deg and 500 units/s (1 m/s) while misaligned (0x004548a5). The module writes the cornering
// speed instead of the creep: min(that ramp, omega d / (2 sin theta)), the chord rule (the turning circle at the
// commanded rate passes through B), never above the engine's aligned value, never below the floor.
// The chord arc from x (heading f) to B at the commanded rate sweeps the gate's ring (the torus band between the
// hole and the rim, in the gate plane) when a sampled point of the arc crosses the plane local z = 0 at a lateral
// distance in [hole, r_gate + r_ship]. 16 samples; a nearly aligned ship is a straight segment. Returns true when clear.
constexpr float kGateHoleFraction = 0.6f;  // the stock ring hole's lower bound (section 10.9: stock 0.605-0.654 r, installed Terran
                                           // 0.663 r, installed Mayhem gates a collision-free bubble): every pass point at 0.55 r
                                           // lies inside it. A visual bound only: the engine's collision pair filter
                                           // (0x0045c7b3..0x0045c7e3) skips the ship-gate pair while the ship is on order 5 to
                                           // that gate, so contact on this leg is impossible (amendment 9)
constexpr float kGateChordFraction = kGateHoleFraction; // the straight chord's plane crossing: the hole bound
constexpr float kGateDockMaxTurnCos = -0.1736f; // cos 100 deg: a sharper turn keeps the engine's turn in place (its creep)
constexpr unsigned kGateArcSamples = 8; // segment tests per order-5 call
inline bool gate_chord_clears_ring(const float x[3], const float f[3], const float B[3], const float G[3], const float row0[3],
                                   const float row1[3], const float row2[3], float r_gate, float r_ship) noexcept {
    float d[3] = {B[0] - x[0], B[1] - x[1], B[2] - x[2]};
    const float D = sqrt_ss(dot3(d, d));
    if (!(D > 1.0f)) return true;
    const float cos_t = dot3(f, d) / D;
    if (cos_t < kGateDockMaxTurnCos) return false; // more than 100 deg off: the chord circle would be wider than the leg
    // the straight chord to B (the aligned ship's path; the ideal arc of the initial turn lies on the ship's side of it
    // and the pair filter exempts the leg from collision: amendment 9), sampled in kGateArcSamples segments
    float prev[3] = {x[0], x[1], x[2]};
    for (unsigned k = 1; k <= kGateArcSamples; ++k) {
        const float u = float(k) / float(kGateArcSamples);
        float p[3];
        for (unsigned i = 0; i < 3; ++i) p[i] = x[i] + d[i] * u;
        float z0 = 0.0f, z1 = 0.0f;
        for (unsigned i = 0; i < 3; ++i) {
            z0 += (prev[i] - G[i]) * row2[i];
            z1 += (p[i] - G[i]) * row2[i];
        }
        if ((z0 < 0.0f) != (z1 < 0.0f) && z0 != z1) {
            const float t = z0 / (z0 - z1);
            float lx = 0.0f, ly = 0.0f;
            for (unsigned i = 0; i < 3; ++i) {
                const float q = prev[i] + t * (p[i] - prev[i]) - G[i];
                lx += q * row0[i];
                ly += q * row1[i];
            }
            const float lat = sqrt_ss(lx * lx + ly * ly);
            if (lat >= kGateChordFraction * r_gate && lat - r_ship <= r_gate) return false;
        }
        for (unsigned i = 0; i < 3; ++i) prev[i] = p[i];
    }
    return true;
}
// P5: the order-8 launch distance after t ms, units: v0 t / 1000 + t^2 x 1665000 / 1000000 (the engine's
// 0x0045526f..0x0045527f with the v0 term added; v0 = the speed kept at the pass point, units/s).
inline std::int32_t gate_launch_units(std::int32_t v0_units, std::int32_t t_ms) noexcept {
    if (t_ms < 0) t_ms = 0;
    if (v0_units < 0) v0_units = 0;
    const std::int64_t t = t_ms;
    std::int64_t s = std::int64_t(v0_units) * t / 1000 + t * t * 1665000 / 1000000;
    if (s > 0x7fffffff) s = 0x7fffffff;
    return std::int32_t(s);
}
// The site-B write for a clamp v (m/s, > 0) against the current desired speed (units/s): the units value to
// store, or 0 for no write. Compared in float before any conversion, so a schedule beyond 2^31 units/s (a
// cornering term with sin(theta) near its floor at hundreds of km) never converts: cvttss2si would yield INT_MIN,
// which is "lower" than everything.
inline std::int32_t clamp_units(float v_ms, std::int32_t desired) noexcept {
    if (!(v_ms > 0.0f)) return 0;
    const float units = v_ms * kUnitsPerMetre;
    if (!(units < float(desired)) || units >= 2147483520.0f) return 0;
    return round_to_int(units);
}
// The site-B write for the gate approach (kShipFlagGate): the speed in both directions (the engine's ramp to A is
// replaced), 0 when nothing changes or the value is not representable.
inline std::int32_t gate_speed_units(float v_ms, std::int32_t desired) noexcept {
    if (!(v_ms > 0.0f) || !finite_f(v_ms)) return 0;
    const float units = v_ms * kUnitsPerMetre;
    if (units >= 2147483520.0f) return 0;
    const std::int32_t u = round_to_int(units);
    return u > 0 && u != desired ? u : 0;
}
// The ship_steering_stuck witness (section 6.3): true once when the distance
// to a MovePos point has not improved for kStuckMs.
inline bool stuck_update(Ship& s, float d, std::uint32_t now_ms) noexcept {
    if (s.best_d < 0.0f || d < s.best_d - kStuckProgressM) {
        s.best_d = d;
        s.best_stamp = now_ms;
        return false;
    }
    if (float(now_ms - s.best_stamp) >= kStuckMs) {
        s.best_stamp = now_ms; // one witness per interval
        return true;
    }
    return false;
}

// ---- Option parsing (section 6.3) ---------------------------------------
// X3M_SHIP_STEERING: 1 = on, 0 = off (unset = off). Returns false on any other text.
template <class Char> inline bool parse_switch(const Char* text, unsigned length, bool* on) noexcept {
    if (length == 0) {
        *on = false;
        return true;
    }
    if (length != 1 || (text[0] != Char('0') && text[0] != Char('1'))) return false;
    *on = text[0] == Char('1');
    return true;
}
// X3M_SHIP_STEERING_CLASSES: a comma list of SG_SH names (case-insensitive,
// spaces ignored) excluded from the law, as a mask over the record code.
// Returns false on an unknown name (the mask is then undefined).
template <class Char> inline bool parse_class_mask(const Char* text, unsigned length, std::uint32_t* mask) noexcept {
    *mask = 0;
    unsigned i = 0;
    while (i < length) {
        while (i < length && (text[i] == Char(' ') || text[i] == Char(','))) ++i;
        if (i >= length) break;
        char name[8] = {};
        unsigned n = 0;
        while (i < length && text[i] != Char(',') && text[i] != Char(' ')) {
            if (n >= 7) return false;
            Char ch = text[i++];
            if (ch >= Char('a') && ch <= Char('z')) ch = Char(ch - Char('a') + Char('A'));
            if (ch > Char(0x7e) || ch < Char(0x21)) return false;
            name[n++] = char(ch);
        }
        bool found = false;
        for (unsigned c = 0; c < kClassCount; ++c)
            if (!std::strcmp(name, kClassNames[c])) {
                *mask |= 1u << c;
                found = true;
            }
        if (!found) return false;
    }
    return true;
}
}
