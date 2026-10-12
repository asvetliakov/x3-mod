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
// Amendment 14 (slip-aware-steering.md): the integrator's slip term obj+0x60 (ship-movement.md section 13.2) makes the
// velocity u = v f + slip a first-order lag of the nose with tau = 2 s (x (2000 - dt)/2000 per frame). The law steers
// the velocity: the aim leads the lateral velocity by tau, the schedule is written for the closing speed u . t, the
// cornering rate is bounded by the fastest the velocity can rotate (sin 60 deg / tau), a formation follower matches
// the leader's velocity. On order 5 the integrator adds nothing to the slip (it only decays) and the slip is treated as 0.
constexpr float kSlipTau = 2.0f;             // s
constexpr float kSlipCornerRate = 0.43f;     // rad/s = sin 60 deg / tau (section 2.2)
constexpr float kSlipLeadTan60 = 1.7320508f; // the aim stays within 60 deg of the line of sight (section 2.1)
constexpr float kSlipMinForwardCos = 0.5f;   // the schedule's nose-speed solve divides by max(f . t, this) (section 2.3)
constexpr float kFormationTf = 2.0f;         // s: u_des = u_lead + (P - x) / T_f (section 2.5)
constexpr float kFormationGain = 2.0f;       // G = tau / tau_c, tau_c = 1 s: N = u + G (u_des - u)
constexpr float kFormationAimMinM = 50.0f;   // the aim point along N is at least this far (a near slot: the direction still counts)
constexpr float kFormationMinSpeed = 1.0f;   // |N| below this: the pursuit of P stands

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
// acos in degrees for a cosine in [-1, 1] (the crossing heading error of the --debug row, the velocity lag): the identity
// acos(c) = 2 atan2(sqrt(1 - c), sqrt(1 + c)) over the module's own atan2, no CRT call.
// always inlined: an out-of-line float return would travel through st(0) on i386 (the x87 audit)
__attribute__((always_inline)) inline float acos_deg(float c) noexcept {
    if (c >= 1.0f) return 0.0f;
    if (c <= -1.0f) return 180.0f;
    return 2.0f * atan2_f(sqrt_ss(1.0f - c), sqrt_ss(1.0f + c)) * (180.0f / kPi);
}
// 1 - e^(-x) for x >= 0: e^(-x) = (e^(-x/4))^4 with the (2,2) Pade form of e^(-y), y = x/4 <= 2 (within 1e-4 over 0..8;
// 1 beyond 8, where e^(-x) < 3.4e-4), no CRT call; the slip's settled fraction after x time constants (amendment 14).
__attribute__((always_inline)) inline float one_minus_exp_neg(float x) noexcept {
    if (!(x > 0.0f)) return 0.0f;
    if (x > 8.0f) return 1.0f;
    const float y = 0.25f * x, q = y * y * (1.0f / 12.0f);
    float r = (1.0f - 0.5f * y + q) / (1.0f + 0.5f * y + q);
    r *= r;
    r *= r;
    return 1.0f - r;
}
// Cube root of x >= 0 (the ramp-phase turn-in distance of amendment 14): the exponent-third seed, three Newton steps
// (relative error under 1e-6 over the floats used here), no CRT call.
__attribute__((always_inline)) inline float cbrt_f(float x) noexcept {
    if (!(x > 0.0f) || !finite_f(x)) return 0.0f;
    std::uint32_t bits;
    std::memcpy(&bits, &x, 4);
    bits = bits / 3u + 0x2a5137a0u;
    float y;
    std::memcpy(&y, &bits, 4);
    for (int i = 0; i < 3; ++i) y = y - (y - x / (y * y)) * (1.0f / 3.0f);
    return y;
}

// Amendment 14: the slip obj+0x60 is an int32 the integrator, the contact response, glide and undock all write; a value
// beyond kSlipMaxFactor x v_max (the integrator's own increment is bounded by the nose speed) is not the lag the law models
// and is treated as 0 (fail closed to the nose model). v_max <= 0 (unknown) refuses any slip.
constexpr float kSlipMaxFactor = 2.0f;
__attribute__((always_inline)) inline bool slip_usable(const float slip[3], float v_max) noexcept {
    if (!finite_f(slip[0]) || !finite_f(slip[1]) || !finite_f(slip[2]) || !(v_max > 0.0f)) return false;
    const float b = kSlipMaxFactor * v_max;
    return slip[0] * slip[0] + slip[1] * slip[1] + slip[2] * slip[2] <= b * b;
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
    // Amendment 12 (ship-movement.md section 13.4): order 5 to a port (not a gate) and the hysteresis state of its
    // port-roll match (the previous call of this leg wrote a triple below kPortMatchLimit: port_hold_active()).
    bool port_leg = false;
    bool port_hold = false;
    bool smooth_stop = true;  // a stopped axis gets its stopping angle instead of 0 (the DLL never clears it; the fixture's A/B)
    // Amendment 14: the integrator's slip velocity obj+0x60..+0x68 (m/s, world; read once per call, finite); steer() treats
    // it as 0 on order 5. For a formation follower (orders 0x14/0x15 with a ship target) the leader's actual velocity
    // v_lead f_lead + slip_lead (m/s, world): the velocity-matching law replaces the pursuit of P and the arrival ramp.
    float slip[3] = {0, 0, 0};
    bool detour = false;    // P is the avoidance layer's detour point: built on the velocity's swept path (x + u t_cpa, pushed
                            // along u), it already carries the slip, so the lateral-velocity lead is not applied again
    bool formation = false;
    float u_lead[3] = {0, 0, 0};
    float nose_lead[3] = {0, 0, 0}; // the leader's nose velocity v_lead f_lead (m/s, world): nose_lead - u_lead = tau du_lead/dt, the
                                    // feed-forward that holds the slot on a turning leader (its own lag equation; the DLL reads both)
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
    bool port_aligned = false; // a port leg whose written triple is below kPortMatchLimit (the engine's match runs this tick)
    // Amendment 14 diagnostics and site-B inputs: the velocity used (m/s; its lag behind the nose is lag_deg(), off the hot path),
    // the cosine of the lead's angle between the aim and the line of sight (the degrees: acos_deg, off the hot path), the
    // additive nose-speed correction site B applies to the schedule (the nose speed that closes at the scheduled speed minus
    // the schedule; 0 without a schedule), and whether the speed is the formation law's |N| (no arrival ramp at site B: v_cmd
    // is the whole command).
    float u[3] = {0, 0, 0};
    float aim_cos = 1;
    float v_slip = 0;
    bool formation_speed = false;
};
// Amendment 12. The engine's port-roll match (0x0045474a..0x004547d6) replaces the roll remainder when all three written
// angles are below kPortMatchLimit (strict, signed); on a port leg the module keeps the triple below it once aligned while
// the bearing error stays under kPortHoldRad, so the match is not toggled on and off as yaw/pitch cross the limit.
constexpr std::int32_t kPortMatchLimit = 0x51e;
constexpr std::int32_t kPortHoldClamp = kPortMatchLimit - 1;
constexpr float kPortHoldRad = float(2 * kPortMatchLimit) * kRadPerBinary; // 14.4 deg
constexpr std::uint32_t kPortHoldGapMs = 250; // a previous call older than this ends the leg's hold (order 5 calls every frame)
// The stopping angle of a rate w (rad/s) at the axis acceleration a (rad/s^2): w^2 / 2a in binary angle, rounded up,
// signed like w, at least 1. The integrator sets the rate to 0 at once on a zero remainder (0x00459a99); this remainder
// makes the controller 0x0043ad20 brake at a (it brakes while e < w^2/a) and end with delta = e and w = 0.
inline std::int32_t stop_angle(float w, float a) noexcept {
    if (!(a > 0.0f) || w == 0.0f || !finite_f(w)) return 0;
    float b = w * w / (2.0f * a) * kBinaryPerRad;
    if (!(b < 32767.0f)) b = 32767.0f;
    std::int32_t n = std::int32_t(b);
    if (float(n) < b) ++n;
    if (n < 1) n = 1;
    return w < 0.0f ? -n : n;
}
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
    // Amendment 14: the velocity u = v f + slip (the slip is 0 on order 5: the integrator adds nothing to it there and the
    // leg keeps its amendment-12 behaviour), its lag behind the nose.
    const bool slip_on = in.order != kOrderDock && slip_usable(in.slip, dyn.v_max);
    const float slip[3] = {slip_on ? in.slip[0] : 0.0f, slip_on ? in.slip[1] : 0.0f, slip_on ? in.slip[2] : 0.0f};
    for (unsigned i = 0; i < 3; ++i) c.u[i] = in.v * in.row[2][i] + slip[i];
    // Target in the basis rows. Inside the order's arrival distance (section 2.1) the heading is held: all three
    // angles 0, the engine's own arrival test runs on the next tick. A detour point (step 2) never holds: the
    // hold and the ramp below measure the order's own point.
    const float hold = (in.arrival_ramp && in.d_arr > kArrivalHoldM) ? in.d_arr : kArrivalHoldM;
    float t[3] = {0, 0, 0};   // the aim direction in the rows
    float tP[3] = {0, 0, 0};  // the line of sight to P in the rows (the cornering circle passes through P)
    float ft = 1.0f, st = 0.0f; // f . t and slip . t along the line of sight (the schedule's closing-speed solve)
    float v_N = 0.0f;         // the formation law's nose speed |N| (0 = not in force)
    if (c.d_order > hold && c.d > kArrivalHoldM) {
        const float inv = 1.0f / c.d;
        const float th[3] = {d[0] * inv, d[1] * inv, d[2] * inv}; // t_hat, world
        tP[0] = dot3(d, in.row[0]) * inv;
        tP[1] = dot3(d, in.row[1]) * inv;
        tP[2] = dot3(d, in.row[2]) * inv;
        ft = tP[2];
        st = dot3(slip, th);
        float aim[3] = {d[0], d[1], d[2]};
        if (in.formation) {
            // Section 2.5: u_des = u_lead + (P - x) / T_f (|u_des| <= v_max), N = u + G (u_des - u) + (v_lead f_lead - u_lead);
            // the nose along N at speed |N| (clamped to [floor, v_max] below, written at site B lower only). The last term is
            // tau du_lead/dt by the leader's own lag equation: without it a follower on a turning leader holds the slot only
            // with the error that supplies the nose lead (|u_lead| sin(lag) metres: 90 m for an M5 on the fixture's circle).
            float u_des[3];
            for (unsigned i = 0; i < 3; ++i) u_des[i] = in.u_lead[i] + d[i] * (1.0f / kFormationTf);
            const float m = sqrt_ss(dot3(u_des, u_des));
            if (dyn.v_max > 0.0f && m > dyn.v_max)
                for (unsigned i = 0; i < 3; ++i) u_des[i] *= dyn.v_max / m;
            float N[3];
            for (unsigned i = 0; i < 3; ++i) N[i] = c.u[i] + kFormationGain * (u_des[i] - c.u[i]) + (in.nose_lead[i] - in.u_lead[i]);
            const float n = sqrt_ss(dot3(N, N));
            // The speed is also capped by the same law with the slot-closing term braked to sqrt(2 a (d - d_arr)), the relative
            // speed the follower can still stop from before the point (a stopped leader, a far slot): the direction stays N's.
            float n_cap = n;
            if (in.arrival_ramp && dyn.a_lin > 0.0f) {
                const float over = c.d - in.d_arr, closing = c.d * (1.0f / kFormationTf);
                const float brake = over > 0.0f ? sqrt_ss(2.0f * dyn.a_lin * over) : 0.0f;
                if (brake < closing) {
                    float Nb[3];
                    for (unsigned i = 0; i < 3; ++i)
                        Nb[i] = c.u[i] + kFormationGain * (in.u_lead[i] + d[i] * (brake / c.d) - c.u[i]) + (in.nose_lead[i] - in.u_lead[i]);
                    n_cap = sqrt_ss(dot3(Nb, Nb));
                }
            }
            const float reach = c.d > kFormationAimMinM ? c.d : kFormationAimMinM;
            const float lead_fwd = dot3(in.u_lead, in.row[2]);
            if (n > kFormationMinSpeed && dot3(N, in.row[2]) > 0.0f) { // N ahead of the nose: steer along it
                for (unsigned i = 0; i < 3; ++i) aim[i] = N[i] * (reach / n);
                v_N = n_cap < n ? n_cap : n;
            } else if (lead_fwd > kFormationMinSpeed || dot3(u_des, in.row[2]) > 0.0f) {
                // N behind the nose while the leader (or the wanted velocity) still moves ahead: the follower is faster than its slot
                // needs (e.g. ahead of it). The nose speed acts at once (only the direction lags), so it brakes at the floor speed along
                // the leader's velocity and lets the slot come back, instead of turning round (the loop of the review's case)
                const float* dir = lead_fwd > kFormationMinSpeed ? in.u_lead : u_des;
                const float m2 = sqrt_ss(dot3(dir, dir));
                if (m2 > kFormationMinSpeed)
                    for (unsigned i = 0; i < 3; ++i) aim[i] = dir[i] * (reach / m2);
                v_N = kFloorMs;
            } else if (n > kFormationMinSpeed) // the leader stopped (or coming back) and the slot behind: turn toward the slot itself
                v_N = n_cap < n ? n_cap : n;   // (aim = P - x); the cornering term below brakes the turn
        } else if (slip_on && !in.detour) {
            // Section 2.1: the aim leads the velocity's component across the line of sight by T_d = tau, so the velocity
            // (not the nose) ends on the line; for an order without an arrival distance (the attack orders and the rest
            // of the no-ramp set, ratification) T_d = min(tau, t_go) so a near target is aimed at directly. The lead is
            // bounded to 60 deg off the line of sight.
            const float ut = dot3(c.u, th);
            float Td = kSlipTau;
            if (!order_has_arrival_ramp(in.order)) {
                const float t_go = c.d / (in.v > 1.0f ? in.v : 1.0f);
                if (t_go < Td) Td = t_go;
            }
            float lead[3];
            for (unsigned i = 0; i < 3; ++i) lead[i] = (c.u[i] - ut * th[i]) * Td;
            const float ln2 = dot3(lead, lead), lmax = kSlipLeadTan60 * c.d;
            if (ln2 > lmax * lmax) { // rare: the square root only when the clamp applies
                const float k = lmax / sqrt_ss(ln2);
                for (unsigned i = 0; i < 3; ++i) lead[i] *= k;
            }
            for (unsigned i = 0; i < 3; ++i) aim[i] = d[i] - lead[i];
        }
        const float an = sqrt_ss(dot3(aim, aim));
        if (an > 1e-6f) {
            const float ia = 1.0f / an;
            t[0] = dot3(aim, in.row[0]) * ia;
            t[1] = dot3(aim, in.row[1]) * ia;
            t[2] = dot3(aim, in.row[2]) * ia;
            c.aim_cos = dot3(aim, th) * ia;
        } else {
            t[0] = tP[0];
            t[1] = tP[1];
            t[2] = tP[2];
        }
    } else
        c.held = true;
    const float sin_t = sqrt_ss(t[0] * t[0] + t[1] * t[1]);
    const float sin_P = sqrt_ss(tP[0] * tP[0] + tP[1] * tP[1]);
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
    // Amendment 12, smooth stops: an axis the law stops (its angle exactly 0: the roll of orders 4 and 5, the arrival
    // hold, a zero command) while it still turns brakes instead of being cut. A servo angle below half a binary unit that
    // rounds to 0 is the geometric remainder, not a stop, and keeps the engine's clamp.
    if (in.smooth_stop) {
        const bool live[3] = {yaw_ok, pitch_ok, roll_ok};
        for (unsigned i = 0; i < 3; ++i)
            if (c.e_rad[i] == 0.0f && live[i] && in.w[i] != 0.0f) c.e[i] = stop_angle(in.w[i], dyn.W[i] * dyn.k);
    }
    // Amendment 12, port legs: roll stays 0 (or its stopping angle) below the match limit, and once the leg is aligned
    // yaw and pitch are held below it while the bearing error is under kPortHoldRad (the clamp moves only the braking
    // point of the engine's trapezoid, not the turn direction).
    if (in.port_leg) {
        const bool hold = in.port_hold && c.theta < kPortHoldRad;
        for (unsigned i = 0; i < 3; ++i) {
            if (i < 2 && !hold) continue;
            if (c.e[i] > kPortHoldClamp) c.e[i] = kPortHoldClamp;
            if (c.e[i] < -kPortHoldClamp) c.e[i] = -kPortHoldClamp;
        }
        c.port_aligned = c.e[0] < kPortMatchLimit && c.e[0] > -kPortMatchLimit && c.e[1] < kPortMatchLimit && c.e[1] > -kPortMatchLimit &&
                         c.e[2] < kPortMatchLimit && c.e[2] > -kPortMatchLimit;
    }
    // Cornering (section 2.5): the turning circle at the slower axis must pass through P; amendment 14 (section 2.2): off
    // order 5 the rate is bounded by kSlipCornerRate, the fastest the velocity direction can rotate whatever the nose does.
    // A point behind the nose (f . t < 0) takes the tightest circle, d / 2 (sin taken as 1): the chord's d / (2 sin) grows
    // without bound toward 180 deg, which would leave a ship that overshot its point at full speed (the orbit). The formation
    // law's cornering is the same rule on its slot direction, applied only when N points behind the nose (a follower
    // ahead of its slot or a stopping leader: it brakes instead of looping); ahead, |N| is the speed.
    float omega_min = fmin_f(yaw_ok ? dyn.W[0] : 1e30f, pitch_ok ? dyn.W[1] : 1e30f);
    if (slip_on && omega_min > kSlipCornerRate) omega_min = kSlipCornerRate;
    if (c.held || !(omega_min < 1e29f) || (in.formation && dot3(in.u_lead, in.u_lead) >= kFloorMs * kFloorMs && t[2] >= 0.0f))
        c.v_corner = 0.0f;
    else if (ft < 0.0f || (in.formation && t[2] < 0.0f))
        c.v_corner = omega_min * c.d * 0.5f;
    else
        c.v_corner = sin_P > kCornerSinMin ? omega_min * c.d / (2.0f * sin_P) : 0.0f;
    // Section 6.2: a record without a max speed or an acceleration gets no clamp at all (steering still runs).
    const bool clamp_ok = dyn.v_max > 0.0f && dyn.a_lin > 0.0f;
    // The arrival ramp alone (the cornering term is a cap on the nose speed, applied after the closing-speed solve).
    float ramp = clamp_ok ? schedule_speed(c.d_order, in.d_arr, dyn.a_lin, in.v_ref, in.arrival_ramp, 0.0f) : 0.0f;
    // Section 2.3: the ramp is a closing speed u . t; the nose speed that closes at it is (v_ramp - slip . t) / max(f . t, 0.5),
    // clamped to [floor, v_max]; with the point more than 60 deg off the nose (f . t <= 0.5) the solve only lowers. Site B
    // recomputes the ramp with the distance of its frame and adds the difference measured here (v_slip) to the ramp term only.
    if (slip_on && !in.formation && !c.held && ramp > 0.0f) {
        const float fwd = ft > kSlipMinForwardCos ? ft : kSlipMinForwardCos;
        float v_c = (ramp - st) / fwd;
        if (ft <= kSlipMinForwardCos && v_c > ramp) v_c = ramp;
        if (v_c > dyn.v_max) v_c = dyn.v_max;
        if (v_c < kFloorMs) v_c = kFloorMs;
        c.v_slip = v_c - ramp;
        ramp = v_c;
    }
    if (clamp_ok && v_N > 0.0f) { // the formation law's nose speed (section 2.5), lowered by the cornering term on a turn toward the slot behind
        float v = v_N > dyn.v_max ? dyn.v_max : v_N;
        if (c.v_corner > 0.0f && c.v_corner < v) v = c.v_corner;
        c.v_cmd = v < kFloorMs ? kFloorMs : v;
        c.formation_speed = true;
    } else if (clamp_ok) {
        float v = ramp;
        if (c.v_corner > 0.0f && (v <= 0.0f || c.v_corner < v)) v = c.v_corner;
        c.v_cmd = v > 0.0f && v < kFloorMs ? kFloorMs : v;
    } else
        c.v_cmd = 0.0f;
    if (!finite_f(c.v_cmd) || !finite_f(c.v_corner) || !finite_f(c.v_slip)) return Reason::not_finite;
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
    float v_slip = 0;             // m/s, amendment 14: site B adds it to the schedule (the nose speed for the scheduled closing speed)
    float v_emergency = 0;        // m/s, the avoidance layer's braking ramp when no detour exists (0 = none)
    std::uint32_t last_call_ms = 0; // game time of the previous handled call (the servo's measured interval)
    std::uint32_t per_frame_run = 0; // consecutive calls one frame apart (servo_tick)
};
static_assert(sizeof(Ship) == 64, "one cache line per ship");
constexpr unsigned kShipCapacity = 1024;
constexpr unsigned kProbeLimit = 8;
constexpr std::uint32_t kStaleMs = 5000; // an entry older than this may be reused
constexpr std::uint16_t kShipFlagRamp = 1, kShipFlagClamp = 2, kShipFlagGate = 4, // Ship.flags: arrival ramp, clamp enabled, gate approach
                        kShipFlagGateDock = 8, // order 5 to a gate: the cornering speed toward B replaces the engine's 1 m/s creep
                        kShipFlagPortHold = 16; // amendment 12: order 5 to a port, the last call wrote a triple below the match limit
// Amendment 12: the port leg's hold for this call: the ship's previous handled call was on order 5 to a port, wrote an
// aligned triple and is at most kPortHoldGapMs old.
__attribute__((always_inline)) inline bool port_hold_active(const Ship& s, std::uint16_t order, std::uint32_t now_ms) noexcept {
    return order == kOrderDock && s.order == kOrderDock && (s.flags & kShipFlagPortHold) && now_ms - s.stamp_ms <= kPortHoldGapMs;
}
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
        if (s.flags & kShipFlagRamp) { // the ramp flag with the gate flag: the lower of the two (unset since capital-gate-transit.md:
                                       // the capital approach has no arrival ramp, the crossing is geometric)
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
        v = schedule_speed(dist, s.d_arr, s.a_lin, s.v_ref, (s.flags & kShipFlagRamp) != 0, 0.0f);
        if (v > 0.0f && s.v_slip != 0.0f) { // amendment 14: the ramp's nose speed for its closing speed (steer() measured the difference)
            v += s.v_slip;
            if (v < kFloorMs) v = kFloorMs;
        }
        if (s.v_corner > 0.0f && (v <= 0.0f || s.v_corner < v)) v = s.v_corner; // the cornering term caps the nose speed
        if (v > 0.0f && v < kFloorMs) v = kFloorMs;
    }
    if (s.v_emergency > 0.0f && (v <= 0.0f || s.v_emergency < v)) v = fmax_f(s.v_emergency, kFloorMs);
    return finite_f(v) ? v : 0.0f;
}
// The velocity's lag behind the nose, deg (amendment 14 diagnostics: the --debug decision row and the fixture's traces).
__attribute__((always_inline)) inline float lag_deg(const float u[3], const float fwd[3]) noexcept {
    const float n = sqrt_ss(dot3(u, u));
    return n > 1e-3f ? acos_deg(dot3(u, fwd) / n) : 0.0f;
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
// (the engine's order-4 ramp to 5000 units/s at A, 0x00455052, is the stop P2 removes). Capitals (capital-gate-transit.md,
// amendment 13): the lead-in along the gate axis toward the validated grid point (gate_geometry_capital, gate_lead_in_capital),
// the same speed write, and P2 keeps the speed when the crossing is within the 0x1f tolerance (gate_capital_keeps_speed).
// Amendment 10 (Run154): the pass points lie on the gate DISC, B = G + R (0.55 sin theta, -0.55 cos theta, -0.01) r
// (|theta| <= 45 deg about straight down); the lead-in line from A aims at the arc's centre point (0, -0.55, -0.01) r:
// direction (0, 0.25, 0.99) normalised, 14.17 deg above the axis in the gate's vertical plane, so the heading error to
// any B at the crossing is lateral, 13.0 deg at most on the line and 20.7 deg with the 0.25 r spread
// (verification/results/ship-steering/spread_bound.py); the order-5 cornering absorbs it.
constexpr float kGateSin = 0.24484f, kGateCos = 0.96956f; // the lead-in direction's y / z components
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
// Order 5 to a gate: B must be the pinned pass point on the gate disc, B - G in gate rows = (y0 sin theta, -y0 cos theta,
// -0.01) r (section 10.5 as corrected by Run154 and the listing 0x004609d5..0x00460a01: the sin lookup feeds x, the
// cos lookup y, z keeps the pass vector's -0.01), |theta| <= 50 deg accepted, y0 = 0.55 (0.05 for gate subtypes 5..8
// on the same scene: amendment 9), the rows orthonormal, r_gate > 0. Anything else keeps the engine's own speed law.
constexpr float kGatePassY0 = 0.55f, kGatePassY0Small = 0.05f, kGatePassTol = 0.03f; // the tolerance is absolute, in r
constexpr float kGatePassZ = -0.01f, kGatePassZTol = 0.05f, kGatePassCos50 = 0.6428f, kGatePassSin50 = 0.7660f;
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
    const float y0 = gate_pass_y0(gate_subtype);
    const float tol = y0 * r * 0.3f < kGatePassTol * r ? y0 * r * 0.3f : kGatePassTol * r; // 3 % of r, at most 30 % of the pass radius
    const float lx = dot3(d, row0), ly = dot3(d, row1), lz = dot3(d, row2);
    if (fabs_f(lz - kGatePassZ * r) > kGatePassZTol * r) return false;
    const float rho = sqrt_ss(lx * lx + ly * ly);
    if (fabs_f(rho - y0 * r) > tol) return false;
    // |theta| <= 50 deg about straight down: ly = -y0 cos theta r <= -0.643 y0 r, |lx| <= 0.766 y0 r
    return ly <= -kGatePassCos50 * y0 * r + tol && fabs_f(lx) <= kGatePassSin50 * y0 * r + tol;
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
// The windowed core: join_r / inner_r the window and its blend (in r), look_m the carrot look-ahead (m, at least
// kGateLookMinR r).
inline bool gate_lead_in_window(const GateGeometry& g, const float x[3], float join_r, float inner_r, float look_m, float P[3], float* s_out,
                                float* e_out) noexcept {
    const float dx[3] = {x[0] - g.X[0], x[1] - g.X[1], x[2] - g.X[2]};
    const float s = dot3(dx, g.h);
    if (s_out) *s_out = s;
    const float d2 = dot3(dx, dx) - s * s;
    if (e_out) *e_out = d2 > 0.0f ? sqrt_ss(d2) : 0.0f;
    const float a_along = kGateCrossFraction * g.r; // A's position along the line from X (the line ends at A, or A + the spread)
    if (s > a_along + 1.0f || s < -join_r * g.r) return false;
    float look = look_m > 0.0f ? look_m : 0.0f;
    if (look < kGateLookMinR * g.r) look = kGateLookMinR * g.r;
    float along = s + look;
    if (along > a_along) along = a_along;
    const float t = (-s - inner_r * g.r) / ((join_r - inner_r) * g.r); // 0 at the inner edge, 1 at the join point
    if (t > 0.0f) along += (t < 1.0f ? t : 1.0f) * (a_along - along);
    for (unsigned i = 0; i < 3; ++i) P[i] = g.X[i] + g.h[i] * along;
    return finite_f(P[0]) && finite_f(P[1]) && finite_f(P[2]);
}
// Amendment 14 (slip-aware-steering.md section 2.4): the carrot sits max(kGateLookS, tau + t_10) of flight ahead, t_10 =
// 2 sqrt(10 deg / alpha) the nose's own time to turn 10 deg at the angular acceleration alpha (rad/s^2) of the slower axis:
// a carrot shorter than the velocity's 2 s memory plus the nose's lag under-damps the line follow (the 27-42 deg crossing
// errors of the calibrated fixture). alpha <= 0 (unknown) keeps kGateLookS. Far off the line the long carrot gives an intercept
// too shallow to reach it inside the window (the spread's start, 1.66 r off its shifted line at 1.55 r before the crossing: an
// M5 crossed 39 deg off, an M6 26.5 deg), so the look-ahead is shortened by kGateLookLateral times the lateral offset e, never
// below kGateLookS of flight: on the line (e -> 0) the full damping carrot, far off it a steeper join.
constexpr float kGateLookTurnRad = 0.17453f; // 10 deg
constexpr float kGateLookLateral = 0.5f;     // m of look-ahead per m of lateral offset
__attribute__((always_inline)) inline float gate_look_s(float alpha) noexcept {
    if (!(alpha > 1e-6f)) return kGateLookS;
    const float t = kSlipTau + 2.0f * sqrt_ss(kGateLookTurnRad / alpha);
    return t > kGateLookS ? t : kGateLookS;
}
// alpha for gate_look_s: the slower of the positive yaw/pitch caps times k (0 when neither is known); the DLL and the fixture.
__attribute__((always_inline)) inline float slower_axis_alpha(const Dynamics& dyn) noexcept {
    const float w = fmin_f(dyn.W[0] > 0.0f ? dyn.W[0] : 1e30f, dyn.W[1] > 0.0f ? dyn.W[1] : 1e30f);
    return w < 1e29f && dyn.k > 0.0f ? w * dyn.k : 0.0f;
}
inline bool gate_lead_in(const GateGeometry& g, const float x[3], float v, float alpha, float P[3], float* s_out, float* e_out = nullptr) noexcept {
    const float vv = v > 0.0f ? v : 0.0f;
    float look = gate_look_s(alpha) * vv;
    const float dx[3] = {x[0] - g.X[0], x[1] - g.X[1], x[2] - g.X[2]};
    const float s = dot3(dx, g.h), e2 = dot3(dx, dx) - s * s;
    look -= kGateLookLateral * (e2 > 0.0f ? sqrt_ss(e2) : 0.0f);
    if (look < kGateLookS * vv) look = kGateLookS * vv;
    return gate_lead_in_window(g, x, kGateJoinR, kGateJoinInnerR, look, P, s_out, e_out);
}
// The capital lead-in (capital-gate-transit.md section 2.1): the same line toward the validated grid point with direction
// F (gate_geometry_capital), a window of kGateCapitalJoinR r before X blended from kGateCapitalJoinInnerR r (a 90 deg turn of an
// installed capital at 0.5 deg/s^2 takes 27-28 s and 2.7-2.8 km of path before any convergence), and a carrot look-ahead
// of max(kGateLookS v, kGateCapitalLookTurns v / omega, kGateLookMinR r): at least the turning radius at the cap, so the
// pursuit converges instead of weaving. omega = the slower of yaw/pitch (rad/s; 0 = unknown, the time term only).
constexpr float kGateCapitalJoinR = 6.0f, kGateCapitalJoinInnerR = 5.0f, kGateCapitalLookTurns = 2.0f;
inline bool gate_lead_in_capital(const GateGeometry& g, const float x[3], float v, float omega, float P[3], float* s_out,
                                 float* e_out = nullptr) noexcept {
    const float vv = v > 0.0f ? v : 0.0f;
    float look = kGateLookS * vv;
    if (omega > 1e-4f && kGateCapitalLookTurns * vv / omega > look) look = kGateCapitalLookTurns * vv / omega;
    if (!finite_f(look)) return false;
    return gate_lead_in_window(g, x, kGateCapitalJoinR, kGateCapitalJoinInnerR, look, P, s_out, e_out);
}
// ---- Amendment 15 (capital-gate-transit.md section 15.3): the capital join's speed predictor and the slow-entry fallback ------
// The amendment-13 pursuit is kept (gate_lead_in_capital). Once per second of game time, at the frame end (never in the
// steering handler), a capital in join mode has the same pursuit simulated in the plane of its lateral offset at each candidate
// speed {100, 60, 40, 25} m/s from its last decision's state (s, e, nose angle and rate, velocity direction, speed), with the
// live caps (W the slower of yaw/pitch, alpha = W k, the hull's acceleration), in 0.25 s steps; a higher speed is taken only
// when its prediction reaches X with the nose <= 4 deg, the velocity <= 6 deg, |e| <= 50 m and no excursion past the axis beyond
// 50 m (the margin over the 8.11 deg acceptance), the current one is kept while it meets P2's acceptance, and otherwise the
// speed is lowered at once (join_choose). No candidate within the acceptance, or the leg's first decision past the crossing
// plane (s > 0), or a join-mode ship past the plane unconverged: the fallback, latched until the order or its point changes:
// aim at A, min(100, sqrt(25^2 + 2 a (d_A - 0.75 r)), omega_eff d_A / (2 sin theta)) floored at 10 m/s (the floor from 90 deg
// off A), written both ways, the vanilla stop at P2 and the engine's rotate. The plant is join_budget.py's (the controller
// 0x0043ad20 with the module's servo, the 2 s slip lag, the speed slew); predictor_dt.py: 0.25 s gives the 1/30 s verdict in
// 56 of 56 cases [i].
constexpr unsigned kJoinCandidateCount = 4;
constexpr float kJoinCandidates[kJoinCandidateCount] = {100.0f, 60.0f, 40.0f, 25.0f};
constexpr float kJoinStepS = 0.25f;
constexpr unsigned kJoinMaxSteps = 1024; // 256 s of predicted flight per candidate at most (fail = not converged): bounds an
                                         // evaluation at 4 x 1024 steps (review: 156 us host worst at 4096 for a 19 km lateral
                                         // state, pred_cost.cpp); a join from the 6.6 km window takes <= 120 s at 60-100 m/s, and
                                         // where only 25 m/s could still converge beyond 256 s (6.4 km of path) the fallback's
                                         // own slow entry is the same speed at the sphere
constexpr float kJoinNoseDeg = 4.0f, kJoinVelDeg = 6.0f, kJoinLatM = 50.0f, kJoinOverM = 50.0f;
constexpr float kJoinConvergedNoseDeg = kGateRotateTolDeg, kJoinConvergedLatM = 100.0f; // a join-mode ship past the plane
constexpr std::uint32_t kJoinEvalMs = 1000;
constexpr float kJoinEntryMs = 25.0f, kJoinFloorMs = 10.0f;
constexpr float kJoinOmegaEff = 0.0401426f;                 // 2.3 deg/s, the measured join rate (Run157 frame 18240)
constexpr float kJoinKeepVelDeg = 10.0f;                    // P2 keeps the speed only with the velocity within 10 deg of F
constexpr float kJoinSetaFactor = 10.0f;                    // P2's coast bound at SETA x10 (the smallest bound; no SETA read)
constexpr float kJoinServoTickS = 0.05f;
constexpr std::uint32_t kJoinNewLegMs = 2000;              // a gap in the order-4 decisions this long starts a new leg
// sin and cos of a in [-pi, pi] (any finite a is wrapped first): odd/even Taylor polynomials to degree 11/10 on [-pi/2, pi/2]
// (|error| < 2e-6), no CRT call.
__attribute__((always_inline)) inline float wrap_pi(float a) noexcept {
    for (int i = 0; i < 4 && a > kPi; ++i) a -= kTwoPi;
    for (int i = 0; i < 4 && a < -kPi; ++i) a += kTwoPi;
    return a;
}
__attribute__((always_inline)) inline void sincos_f(float a, float* s_out, float* c_out) noexcept {
    a = wrap_pi(a);
    float sgn_c = 1.0f;
    if (a > 0.5f * kPi) {
        a = kPi - a;
        sgn_c = -1.0f;
    } else if (a < -0.5f * kPi) {
        a = -kPi - a;
        sgn_c = -1.0f;
    }
    const float x2 = a * a;
    *s_out = a * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f + x2 * (1.0f / 362880.0f + x2 * (-1.0f / 39916800.0f))))));
    *c_out = sgn_c * (1.0f + x2 * (-0.5f + x2 * (1.0f / 24.0f + x2 * (-1.0f / 720.0f + x2 * (1.0f / 40320.0f + x2 * (-1.0f / 3628800.0f))))));
}
// The state in the plane of the lateral offset: s along h from X (< 0 before the crossing), y the distance from the line
// (>= 0: the ship's side is +y), the nose and velocity angles off h (rad, positive toward +y, i.e. away from the line), the nose
// rate about the plane's normal (rad/s, same sign) and the speed.
struct JoinPlane {
    float s = 0, y = 0, psi = 0, upsi = 0, w = 0, v = 0;
};
struct JoinParams {
    float W = 0, alpha = 0, v_max = 0, a_lin = 0, r_gate = 0; // rad/s, rad/s^2, m/s, m/s^2, m
};
// Projects the decision's 3-D state (position x, basis rows, body rates w = yaw/pitch/roll rad/s in the engine's signs: a
// positive yaw turns row 2 toward -row 0, a positive pitch toward +row 1; the velocity u = v row2 + slip) onto the plane
// through the line (X, h) and the ship. False when not finite.
inline bool join_plane(const GateGeometry& g, const float x[3], const float rows[3][3], const float w[3], const float u[3], float v,
                       JoinPlane* out) noexcept {
    float dx[3] = {x[0] - g.X[0], x[1] - g.X[1], x[2] - g.X[2]};
    const float s = dot3(dx, g.h);
    float n[3] = {dx[0] - s * g.h[0], dx[1] - s * g.h[1], dx[2] - s * g.h[2]};
    float e = sqrt_ss(dot3(n, n));
    if (e > 1.0f) {
        for (unsigned i = 0; i < 3; ++i) n[i] /= e;
    } else { // on the line: the plane of the nose
        const float fh = dot3(rows[2], g.h);
        for (unsigned i = 0; i < 3; ++i) n[i] = rows[2][i] - fh * g.h[i];
        const float nn = sqrt_ss(dot3(n, n));
        if (nn > 1e-4f) {
            for (unsigned i = 0; i < 3; ++i) n[i] /= nn;
        } else {
            for (unsigned i = 0; i < 3; ++i) n[i] = rows[0][i];
        }
        e = 0.0f;
    }
    const float psi = atan2_f(dot3(rows[2], n), dot3(rows[2], g.h));
    const float uu = sqrt_ss(dot3(u, u));
    const float upsi = uu > 1e-3f ? atan2_f(dot3(u, n), dot3(u, g.h)) : psi;
    float df[3], sp, cp;
    for (unsigned i = 0; i < 3; ++i) df[i] = -w[0] * rows[0][i] + w[1] * rows[1][i];
    sincos_f(psi, &sp, &cp);
    float m[3];
    for (unsigned i = 0; i < 3; ++i) m[i] = -sp * g.h[i] + cp * n[i];
    out->s = s;
    out->y = e;
    out->psi = psi;
    out->upsi = upsi;
    out->w = dot3(df, m);
    out->v = v > 0.0f ? v : 0.0f;
    return finite_f(out->s) && finite_f(out->y) && finite_f(out->psi) && finite_f(out->upsi) && finite_f(out->w) && finite_f(out->v);
}
// One prediction at the written speed vc: 2 when the simulated pursuit reaches the crossing plane within the margins (nose <= 4,
// velocity <= 6 deg, |e| and the excursion <= 50 m), 1 when only within P2's acceptance (8.11 deg, 10 deg, 100 m, 100 m), 0 when
// neither. nose_out / y_out: the predicted nose error (deg) and |e| (m) at the plane (or where the prediction stopped).
inline int join_predict_grade(const JoinPlane& st, const JoinParams& p, float vc, float* nose_out, float* y_out) noexcept {
    *nose_out = -1.0f;
    *y_out = -1.0f;
    if (!(p.W > 1e-4f) || !(p.alpha > 1e-6f) || !(p.r_gate > 1.0f) || !(vc > 0.0f)) return 0;
    const float dt = kJoinStepS, a_along = kGateCrossFraction * p.r_gate;
    const float k_lag = one_minus_exp_neg(dt / kSlipTau);
    const float v_des = p.v_max > 0.0f && p.v_max < vc ? p.v_max : vc;
    float x = st.s, y = st.y, psi = st.psi, upsi = st.upsi, w = st.w, v = st.v;
    const float ay0 = fabs_f(y);
    float t_max = 3.0f * (-x + ay0) / vc + 60.0f;
    if (t_max > float(kJoinMaxSteps) * dt) t_max = float(kJoinMaxSteps) * dt;
    float ref = y >= 0.0f ? 1.0f : -1.0f, over = 0.0f;
    bool crossed = false;
    float t = 0.0f;
    for (unsigned step = 0; step < kJoinMaxSteps && x < 0.0f && t < t_max; ++step) {
        float look = 0.75f * v;
        if (2.0f * v / p.W > look) look = 2.0f * v / p.W;
        if (kGateLookMinR * p.r_gate > look) look = kGateLookMinR * p.r_gate;
        float along = x + look;
        if (along > a_along) along = a_along;
        const float theta = wrap_pi(atan2_f(-y, along - x) - psi);
        const float a = fabs_f(theta);
        float w_cmd = sqrt_ss(p.alpha * a);
        if (a / kTau < w_cmd) w_cmd = a / kTau;
        if (p.W < w_cmd) w_cmd = p.W;
        float e_wr = w_cmd * w_cmd / p.alpha + w_cmd * kJoinServoTickS;
        if (e_wr > a) e_wr = a;
        const float sgn = theta >= 0.0f ? 1.0f : -1.0f;
        float wr = w * sgn;
        if (wr < 0.0f) {
            wr += p.alpha * dt;
            if (wr > 0.0f) wr = 0.0f;
        } else if (e_wr < wr * wr / p.alpha) {
            wr -= p.alpha * dt;
            if (wr < 0.0f) wr = 0.0f;
        } else {
            wr += p.alpha * dt;
            if (wr > p.W) wr = p.W;
        }
        float turn = wr * dt;
        if (turn > 0.0f && turn > a) turn = a;
        psi = wrap_pi(psi + sgn * turn);
        w = sgn * wr;
        if (v < v_des) {
            v += p.a_lin * dt;
            if (v > v_des) v = v_des;
        } else {
            v -= p.a_lin * dt;
            if (v < v_des) v = v_des;
        }
        upsi = wrap_pi(upsi + wrap_pi(psi - upsi) * k_lag);
        float su, cu;
        sincos_f(upsi, &su, &cu);
        x += v * cu * dt;
        y += v * su * dt;
        const float side = y >= 0.0f ? 1.0f : -1.0f;
        if (side != ref) {
            crossed = true;
            ref = side;
        }
        if (crossed && fabs_f(y) > over) over = fabs_f(y);
        if (over > kJoinConvergedLatM) break; // the excursion only grows: both verdicts are already no
        t += dt;
    }
    *nose_out = fabs_f(psi) * (180.0f / kPi);
    *y_out = fabs_f(y);
    const float vel_deg = fabs_f(upsi) * (180.0f / kPi);
    if (!(x >= 0.0f)) return 0;
    if (*nose_out <= kJoinNoseDeg && vel_deg <= kJoinVelDeg && *y_out <= kJoinLatM && over <= kJoinOverM) return 2;
    if (*nose_out <= kJoinConvergedNoseDeg && vel_deg <= kJoinKeepVelDeg && *y_out <= kJoinConvergedLatM && over <= kJoinConvergedLatM) return 1;
    return 0;
}
inline bool join_predict(const JoinPlane& st, const JoinParams& p, float vc, float* nose_out, float* y_out) noexcept {
    return join_predict_grade(st, p, vc, nose_out, y_out) == 2;
}
// The speed, highest candidate first, from the current written speed v_cur: a higher candidate only when it meets the margins
// (raised only when it passes: a re-acceleration cannot spend the margin); the current one kept while it meets P2's acceptance;
// otherwise lowered at once to the highest lower candidate within the margins, else the highest lower one within the acceptance.
// False = no candidate within the acceptance (the fallback). Keeping on the acceptance is this implementation's reading: the
// strict "highest within the margins" lowered the fixture's M1 from 6.4 km to 60/25 m/s for 10 s before X although its prediction
// (4.9 deg, 70 m at 100 m/s) matched its flown crossing (4.6 deg) and no speed reached the margin that close to X.
// nose_out / y_out: the chosen candidate's prediction, or the last one's; *margin_out: the chosen one met the margins.
inline bool join_choose(const JoinPlane& st, const JoinParams& p, float v_cur, float* v_out, float* nose_out, float* y_out, unsigned* predictions,
                        bool* margin_out = nullptr) noexcept {
    float accept_v = 0.0f, accept_nose = -1.0f, accept_y = -1.0f;
    for (unsigned i = 0; i < kJoinCandidateCount; ++i) {
        const float c = kJoinCandidates[i];
        if (predictions) ++*predictions;
        const int grade = join_predict_grade(st, p, c, nose_out, y_out);
        const bool keep = c > v_cur - 0.5f && c < v_cur + 0.5f;
        if (grade == 2 || (keep && grade == 1)) {
            *v_out = c;
            if (margin_out) *margin_out = grade == 2;
            return true;
        }
        if (grade == 1 && c < v_cur && accept_v == 0.0f) {
            accept_v = c;
            accept_nose = *nose_out;
            accept_y = *y_out;
        }
    }
    if (margin_out) *margin_out = false;
    if (accept_v == 0.0f) return false;
    *v_out = accept_v;
    *nose_out = accept_nose;
    *y_out = accept_y;
    return true;
}
// The fallback speed (m/s): d_A the distance to A, theta the nose's angle off the direction to A (rad), a the hull's acceleration.
__attribute__((always_inline)) inline float join_fallback_speed(float d_A, float theta, float a_lin, float r_gate) noexcept {
    if (!finite_f(d_A) || !finite_f(theta) || !(theta < 0.5f * kPi)) return kJoinFloorMs;
    const float room = d_A - kGateCrossFraction * r_gate;
    float v = sqrt_ss(kJoinEntryMs * kJoinEntryMs + 2.0f * (a_lin > 0.0f ? a_lin : 0.0f) * (room > 0.0f ? room : 0.0f));
    if (v > kGateLargeCapMs) v = kGateLargeCapMs;
    if (theta > 1e-3f) {
        float st, ct;
        sincos_f(theta, &st, &ct);
        const float chord = kJoinOmegaEff * d_A / (2.0f * st);
        if (chord < v) v = chord;
    }
    return v < kJoinFloorMs ? kJoinFloorMs : v;
}
// P2's keep verdict for a capital (section 15.3 item 4): join mode, the nose within 8.11 deg of F, the velocity within 10 deg,
// and v at most the coast bound sqrt(2 a (|X - G| - (r_ship + v/10)(1 + (tf - 1)/4))) at tf = kJoinSetaFactor.
enum class JoinKeep : std::uint8_t { kept = 0, mode, nose, vel, bound, count };
__attribute__((always_inline)) inline float join_coast_bound(float xg_m, float r_ship, float v, float a_lin) noexcept {
    const float room = xg_m - (r_ship + v / 10.0f) * (1.0f + (kJoinSetaFactor - 1.0f) / 4.0f);
    return room > 0.0f && a_lin > 0.0f ? sqrt_ss(2.0f * a_lin * room) : 0.0f;
}
inline JoinKeep join_keep_verdict(bool join_mode, float nose_deg, float vel_deg, float v, float v_bound) noexcept {
    if (!join_mode) return JoinKeep::mode;
    if (!(nose_deg >= 0.0f) || !(nose_deg <= kGateRotateTolDeg)) return JoinKeep::nose;
    if (!(vel_deg >= 0.0f) || !(vel_deg <= kJoinKeepVelDeg)) return JoinKeep::vel;
    if (!(v <= v_bound)) return JoinKeep::bound;
    return JoinKeep::kept;
}
// The per-approach join state machine shared by the DLL's decide() and the fixture.
enum class JoinMode : std::uint8_t { none = 0, out, join, fallback };
enum class JoinReason : std::uint8_t { none = 0, pred_none, past_x, grid_invalid };
struct JoinState {
    JoinMode mode = JoinMode::none;
    JoinReason reason = JoinReason::none;
    std::uint8_t first_done = 0;
    std::uint8_t pad = 0;
    float v_cmd = kGateLargeCapMs;  // the predictor's speed, m/s
    float pred_nose = -1.0f, pred_y = -1.0f;
    float A[3] = {0, 0, 0};         // the given point the latch keys on, m
    std::uint32_t last_call_ms = 0, window_ms = 0, eval_ms = 0, plane_ms = 0;
    JoinPlane plane;                // the last join-mode decision's state (the predictor's start)
    JoinParams params;
};
// One order-4 decision of a capital on a validated approach: s = the ship's projection from X, converged = nose within 8.11 deg
// and e within 100 m now, prev_order_4 = the ship's previous handled call was on order 4 (a 4 -> 0 -> 4 re-issue to the same
// point is the same leg; 0x12 in between is a new one). Returns the mode for this call; `changed` reports a mode change.
inline JoinMode join_on_decision(JoinState& j, const float A[3], float s, float r_gate, bool converged, bool prev_order_4,
                                 std::uint32_t now, bool* changed) noexcept {
    const JoinMode before = j.mode;
    const float dA[3] = {A[0] - j.A[0], A[1] - j.A[1], A[2] - j.A[2]};
    const bool new_leg = j.mode == JoinMode::none || dot3(dA, dA) > 1.0f || !prev_order_4 || now - j.last_call_ms > kJoinNewLegMs;
    if (new_leg) {
        j = JoinState{};
        for (unsigned i = 0; i < 3; ++i) j.A[i] = A[i];
    }
    j.last_call_ms = now;
    if (!j.first_done) {
        j.first_done = 1;
        if (s > 0.0f) {
            j.mode = JoinMode::fallback;
            j.reason = JoinReason::past_x;
        }
    }
    if (j.mode != JoinMode::fallback) {
        if (s < -kGateCapitalJoinR * r_gate) {
            j.mode = JoinMode::out;
        } else {
            if (j.mode != JoinMode::join) {
                j.mode = JoinMode::join;
                j.window_ms = now;
                j.eval_ms = 0; // evaluate at the next frame end
                j.v_cmd = kGateLargeCapMs;
            }
            if (s > 0.0f && !converged) {
                j.mode = JoinMode::fallback;
                j.reason = JoinReason::past_x;
            }
        }
    }
    *changed = j.mode != before;
    return j.mode;
}
// The frame-end evaluation: due when in join mode and never evaluated or kJoinEvalMs since the last one, with a decision state at
// most max_age_ms old. Returns true when it ran; `fell_back` when no candidate converged (the fallback latched), `slowed` when the
// chosen speed is below the previous one.
inline bool join_evaluate(JoinState& j, std::uint32_t now, std::uint32_t max_age_ms, bool* fell_back, bool* slowed, unsigned* predictions) noexcept {
    *fell_back = *slowed = false;
    if (j.mode != JoinMode::join || !j.plane_ms || now - j.plane_ms > max_age_ms) return false;
    if (j.eval_ms && now - j.eval_ms < kJoinEvalMs) return false;
    j.eval_ms = now ? now : 1;
    float v = 0.0f;
    if (!join_choose(j.plane, j.params, j.v_cmd, &v, &j.pred_nose, &j.pred_y, predictions)) {
        j.mode = JoinMode::fallback;
        j.reason = JoinReason::pred_none;
        *fell_back = true;
        return true;
    }
    *slowed = v < j.v_cmd;
    j.v_cmd = v;
    return true;
}
// The frame-end predictor's registry of capitals in join mode (table slot indices, at most kJoinSlots at a time; a capital
// refused while it is full keeps v_cmd 100, the amendment-13 pursuit). A slot leaves it at the sweep before each evaluation
// when the table entry is no longer the registered ship's, the state left join mode, or its last order-4 decision is older than
// kJoinNewLegMs (the transit completed, the ship died or was redirected: no order-4 decision comes any more). The DLL never
// dereferences the ship here.
constexpr unsigned kJoinSlots = 8;
struct JoinRegistry {
    std::uint16_t slots[kJoinSlots] = {};
    unsigned count = 0, next = 0, dropped = 0, refused = 0;
    bool add(unsigned slot) noexcept {
        for (unsigned i = 0; i < count; ++i)
            if (slots[i] == slot) return true;
        if (count >= kJoinSlots) {
            ++refused;
            return false;
        }
        slots[count++] = std::uint16_t(slot);
        return true;
    }
    void remove_at(unsigned i) noexcept {
        slots[i] = slots[--count];
        ++dropped;
    }
};
inline bool join_registry_keep(const JoinState& j, bool same_ship, std::uint32_t now) noexcept {
    return same_ship && j.mode == JoinMode::join && j.plane_ms && now - j.plane_ms <= kJoinNewLegMs;
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
// The capital counterpart (capital-gate-transit.md section 2.3; ship_steering_gates_capital): a class-7 capital-coded ship
// whose order target is a gate.
template <class Mem>
inline bool gate_target_capital(Mem& mem, std::uint32_t obj, std::uint32_t type_table) noexcept {
    if (!obj || mem.u16(obj + kGateObjClass) != kGateClassShip) return false;
    const std::uint32_t fl = mem.u32(obj + kGateObjFlight);
    if (!fl) return false;
    const std::uint32_t tgt = mem.u32(fl + kGateFlightTarget);
    if (!tgt || mem.u16(tgt + kGateObjClass) != kGateClassGate) return false;
    const std::int32_t type = std::int16_t(mem.u16(obj + kGateObjType));
    if (!type_table || type < 0) return false;
    const std::uint32_t code = mem.u32(type_table + std::uint32_t(type) * kGateRecordStride + kGateRecordClassCode);
    return code < kClassCount && ((kCapitalMask >> code) & 1u);
}
// P2 for a capital: `[obj+0x10]` is kept (so RotateToAngles 0x1f ends on its first frame and order 5 starts at the crossing
// speed) only with the capital policy on, a validated approach (A matched the KC grid: the lead-in ran) and the last
// decision's heading error to F within the 0x1f tolerance (0xfd70 = cos 8.1 deg); a misaligned or unvalidated capital takes
// the vanilla stop. P4/P5 keep the speed at the order-5 arrival and launch with it on the same validated approach.
inline bool gate_capital_keeps_speed(bool capital_gate_target, bool capital_policy, bool state_valid, float cross_err_deg) noexcept {
    return capital_gate_target && capital_policy && state_valid && cross_err_deg >= 0.0f && cross_err_deg <= kGateRotateTolDeg;
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
