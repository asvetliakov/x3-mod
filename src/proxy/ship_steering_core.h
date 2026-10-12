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
inline float atan_unit(float x) noexcept {
    const float x2 = x * x;
    return x * (0.99997726f +
                x2 * (-0.33262347f + x2 * (0.19354346f + x2 * (-0.11643287f + x2 * (0.05265332f + x2 * -0.01172120f)))));
}
// atan2(y, x) in (-pi, pi], no CRT call. (0, 0) -> 0.
inline float atan2_f(float y, float x) noexcept {
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
};
struct Command {
    std::int32_t e[3] = {0, 0, 0}; // the remaining angles to write (binary)
    float e_rad[3] = {0, 0, 0};
    float theta = 0;         // bearing error, rad
    float n[2] = {0, 0};     // rotation axis components in engine sign (yaw, pitch)
    float omega = 0;         // commanded scalar rate, rad/s
    float omega_axis[3] = {0, 0, 0};
    float d = 0;             // distance to P, m
    float v_corner = 0;      // m/s (0 = no limit)
    float v_cmd = 0;         // m/s, the clamp for site B (0 = none)
    bool held = false;       // inside the arrival hold: angles 0
};
// The rate servo of section 2.4: the remaining angle that makes the engine's
// controller accelerate toward |omega| and brake above it, never beyond the
// geometric remainder.
inline float servo_angle(float theta_axis, float omega_axis, float a_axis) noexcept {
    const float mag = fabs_f(theta_axis);
    if (omega_axis == 0.0f || mag == 0.0f) return 0.0f;
    const float ow = fabs_f(omega_axis);
    float e = ow * ow / a_axis + ow * kTickS;
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
inline float schedule_speed(float d, float d_arr, float a_lin, float v_ref, bool arrival_ramp, float v_corner) noexcept {
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
    // Target in the basis rows. Inside the order's arrival distance (section 2.1) the heading is held: all three
    // angles 0, the engine's own arrival test runs on the next tick.
    const float hold = (in.arrival_ramp && in.d_arr > kArrivalHoldM) ? in.d_arr : kArrivalHoldM;
    float t[3] = {0, 0, 0};
    if (c.d > hold) {
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
    c.e_rad[0] = yaw_ok ? servo_angle(geo_y, c.omega_axis[0], dyn.W[0] * dyn.k) : 0.0f;
    c.e_rad[1] = pitch_ok ? servo_angle(geo_p, c.omega_axis[1], dyn.W[1] * dyn.k) : 0.0f;
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
        e_roll = servo_angle(phi_cmd, c.omega_axis[2], a_r);
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
    c.v_cmd = (dyn.v_max > 0.0f && dyn.a_lin > 0.0f) ? schedule_speed(c.d, in.d_arr, dyn.a_lin, in.v_ref, in.arrival_ramp, c.v_corner) : 0.0f;
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
    std::uint32_t reserved[3] = {0, 0, 0}; // step 2: the resolved-pair stamp and detour side
};
static_assert(sizeof(Ship) == 64, "one cache line per ship");
constexpr unsigned kShipCapacity = 1024;
constexpr unsigned kProbeLimit = 8;
constexpr std::uint32_t kStaleMs = 5000; // an entry older than this may be reused
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
inline float clamp_for(const Ship& s, const float x[3], std::uint16_t order, std::uint32_t now_ms, std::uint32_t max_age_ms) noexcept {
    if (!(s.flags & 2) || s.order != order || now_ms - s.stamp_ms > max_age_ms) return 0.0f;
    const float d[3] = {s.P[0] - x[0], s.P[1] - x[1], s.P[2] - x[2]};
    const float dist = sqrt_ss(dot3(d, d));
    const float v = schedule_speed(dist, s.d_arr, s.a_lin, s.v_ref, (s.flags & 1) != 0, s.v_corner);
    return finite_f(v) ? v : 0.0f;
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
