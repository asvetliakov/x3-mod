#pragma once
// The gate traffic controller: the gate geometry. The pass point Q and the final lines (q_point, lambda4/5, line_carrot, dock_ramp),
// the lane's design constants and geometry (lane_q, lane_lat_at / point_at / order_point, kc_grid_slot, lane_point_*), the join
// (lane_join_*), the lane carrot and unit_row.
#include "ship_gate_board_core.h"
namespace x3m::ship_avoidance::ctl {
// ---- the pass point and the final lines (D.2.2; the lane's) ----------------------------------------------------------------------
// The pass point Q (D.2.2) for a hull (r_s, w, L) at a grid lateral A_lat on a gate of radius r_gate: the point nearest A_lat in
// {|Q| <= l_max, Q_U <= -l_min}, l_max = r_s + v_t / 10 - 100 m (the order-5 trigger at the ramp's floor speed v_t = v_max / 2,
// tf 1, the tracking allowance), l_min = w + (L - w) sin 3 deg - (0.389 r_gate - r_small - 50 m) (the hull's cross-section below the
// upper small-arrival fan); a grid point on the axis column (A_R = 0) is biased toward +R (D.5: the small ships take -R) within the
// chord limit. An empty window (l_min > l_max) keeps Q = (0, -l_max). Lambda_4 (the order-4 final line) is A_lat + (Q - A_lat) clamped
// to the chord limit c = 0.75 r_gate - 150 m: one straight line from the funnel through X to the trigger when |Q - A_lat| <= c (the
// rest, an order-5 lateral S). Returns false for a non-finite input.
// terran: the gate is a Terran ring (D.2.2's table: l + w <= h r_gate - 50 m, h = 0.663); when no l satisfies it, Q is the axis (the
// small side spawn covers small arrivals there).
inline bool q_axis(float r_gate, const float a_lat[2], float q[2], float l4[2]) noexcept;
inline bool q_point(float r_s, float w, float L, float v_max, float r_gate, const float a_lat[2], float q[2], float l4[2], bool terran = false) noexcept {
    if (!(r_s > 0.0f) || !(r_gate > 0.0f) || !sc::finite_f(a_lat[0]) || !sc::finite_f(a_lat[1]) || !sc::finite_f(v_max)) return false;
    const float ww = w > 0.0f && w <= r_s ? w : r_s, LL = L > r_s ? L : r_s;
    float lmax = r_s + 0.05f * (v_max > 0.0f ? v_max : 0.0f) - kTrackM;
    if (lmax < 0.0f) lmax = 0.0f;
    if (terran) {
        const float ring = kTerranHoleR * r_gate - ww - kTerranMarginM;
        if (!(ring > 0.0f)) return q_axis(r_gate, a_lat, q, l4);
        if (ring < lmax) lmax = ring;
    }
    float lmin = ww + (LL - ww) * kSin3 - (kFanLowR * r_gate - kSmallMaxM - kFanMarginM);
    if (lmin < 0.0f) lmin = 0.0f;
    const float c = sc::kGateCrossFraction * r_gate - kChordSlackM;
    if (lmin > lmax) {
        q[0] = 0.0f;
        q[1] = -lmax;
    } else {
        // the nearest point of the convex set: the point itself, its projection on the disc (if below the line), on the line (if
        // inside the disc), or a corner
        const float px = a_lat[0], py = a_lat[1];
        const float dn = sc::sqrt_ss(px * px + py * py);
        if (dn <= lmax && py <= -lmin) {
            q[0] = px;
            q[1] = py;
        } else {
            float best = 1e30f, bx = 0.0f, by = -lmin;
            auto take = [&](float x, float y) {
                const float d = (x - px) * (x - px) + (y - py) * (y - py);
                if (d < best) {
                    best = d;
                    bx = x;
                    by = y;
                }
            };
            if (dn > 1e-3f) {
                const float x = px * lmax / dn, y = py * lmax / dn;
                if (y <= -lmin) take(x, y);
            }
            const float half = sc::sqrt_ss(lmax * lmax - lmin * lmin);
            if (px >= -half && px <= half) take(px, -lmin);
            take(half, -lmin);
            take(-half, -lmin);
            q[0] = bx;
            q[1] = by;
        }
        if (sc::fabs_f(a_lat[0]) < 1.0f) { // the axis column: biased toward +R (D.5), within the disc and the chord limit
            const float rest = lmax * lmax - q[1] * q[1];
            float bias = 0.5f * sc::sqrt_ss(rest > 0.0f ? rest : 0.0f);
            const float dy = q[1] - a_lat[1], room = c * c - dy * dy;
            const float lim = room > 0.0f ? sc::sqrt_ss(room) : 0.0f;
            if (bias > lim) bias = lim;
            if (bias > q[0]) q[0] = bias;
        }
    }
    const float dx = q[0] - a_lat[0], dy = q[1] - a_lat[1], d = sc::sqrt_ss(dx * dx + dy * dy);
    const float k = d > c && d > 1e-3f ? c / d : 1.0f;
    l4[0] = a_lat[0] + dx * k;
    l4[1] = a_lat[1] + dy * k;
    return sc::finite_f(q[0]) && sc::finite_f(q[1]) && sc::finite_f(l4[0]) && sc::finite_f(l4[1]);
}
// The axis variant of the pass point (B.1, measured 2026-10-13 and rejected: about 2.6 s less cross -> launch per ship, about 80 s more
// span in scenario T): Q on the axis (l = 0), Lambda_4 = A_lat + (Q - A_lat) within the chord limit, as q_point. Selected only with
// kQAxisDefault (off) for a gate that is not Terran and a hull the side spawn can clear (w <= 0.68 r_gate); kept for a later experiment.
constexpr bool kQAxisDefault = false;
inline bool q_axis(float r_gate, const float a_lat[2], float q[2], float l4[2]) noexcept {
    if (!(r_gate > 0.0f) || !sc::finite_f(a_lat[0]) || !sc::finite_f(a_lat[1])) return false;
    q[0] = q[1] = 0.0f;
    const float c = sc::kGateCrossFraction * r_gate - kChordSlackM, dx = -a_lat[0], dy = -a_lat[1], d = sc::sqrt_ss(dx * dx + dy * dy);
    const float k = d > c && d > 1e-3f ? c / d : 1.0f;
    l4[0] = a_lat[0] + dx * k;
    l4[1] = a_lat[1] + dy * k;
    return true;
}
// The final lines in world space: Lambda_4 (order 4: through the point at the grid X's depth, along F) and Lambda_5 (order 5: through
// Q on the gate plane, along F).
__attribute__((always_inline)) inline void lambda4(const Gate& g, const Member& m, float X[3]) noexcept {
    for (unsigned i = 0; i < 3; ++i) X[i] = g.G[i] + m.zx * g.F[i] + m.l4[0] * g.R[i] + m.l4[1] * g.U[i];
}
__attribute__((always_inline)) inline void lambda5(const Gate& g, const Member& m, float X[3]) noexcept {
    for (unsigned i = 0; i < 3; ++i) X[i] = g.G[i] + m.q[0] * g.R[i] + m.q[1] * g.U[i];
}
// The look-ahead of the predictor's pursuit (plant_step, the capital lead-in): max(0.75 v, 2 v / W, 0.3 r_gate).
__attribute__((always_inline)) inline float pursuit_look(float v, float W, float r_gate) noexcept {
    const float vv = v > 0.0f ? v : 0.0f;
    float look = 0.75f * vv;
    if (W > 1e-4f && 2.0f * vv / W > look) look = 2.0f * vv / W;
    if (sc::kGateLookMinR * r_gate > look) look = sc::kGateLookMinR * r_gate;
    return look;
}
// The carrot on a line (X, h) for the pursuit at speed v, never beyond a_along from X.
__attribute__((always_inline)) inline void line_carrot(const float X[3], const float h[3], float a_along, float r_gate, float W, const float x[3],
                                                       float v, float P[3]) noexcept {
    const float d[3] = {x[0] - X[0], x[1] - X[1], x[2] - X[2]};
    float along = sc::dot3(d, h) + pursuit_look(v, W, r_gate);
    if (along > a_along) along = a_along;
    for (unsigned i = 0; i < 3; ++i) P[i] = X[i] + h[i] * along;
}
// The plant step of sc::plant_step toward an explicit target (tx, ty) in the plane (the lane rollout's carrot); the same arithmetic. k_in (B.9):
// the in-plane share of the step's travel (1 in the plane; an entering ship flying the 3-D line to an in-plane carrot moves d_in / d0 of it
// in the plane and the rest out of it).
__attribute__((always_inline)) inline void plant_step_to(const sc::JoinParams& p, float v_des, float dt, float k_lag, float tx, float ty, float& x,
                                                         float& y, float& psi, float& upsi, float& w, float& v, float k_in = 1.0f) noexcept {
    const float theta = sc::wrap_pi(sc::atan2_f(ty - y, tx - x) - psi);
    const float a = sc::fabs_f(theta);
    float w_cmd = sc::sqrt_ss(p.alpha * a);
    if (a / sc::kTau < w_cmd) w_cmd = a / sc::kTau;
    if (p.W < w_cmd) w_cmd = p.W;
    float e_wr = w_cmd * w_cmd / p.alpha + w_cmd * sc::kJoinServoTickS;
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
    psi = sc::wrap_pi(psi + sgn * turn);
    w = sgn * wr;
    if (v < v_des) {
        v += p.a_lin * dt;
        if (v > v_des) v = v_des;
    } else {
        v -= p.a_lin * dt;
        if (v < v_des) v = v_des;
    }
    upsi = sc::wrap_pi(upsi + sc::wrap_pi(psi - upsi) * k_lag);
    float su, cu;
    sc::sincos_f(upsi, &su, &cu);
    x += k_in * v * cu * dt;
    y += k_in * v * su * dt;
}
// The order-5 handler's aligned speed toward the gate origin (section 10.5): vmax at r_gate, vmax / 2 at r_gate / 5, linear between.
__attribute__((always_inline)) inline float dock_ramp(float d, float r_gate, float v_max) noexcept {
    if (d >= r_gate) return v_max;
    if (d <= 0.2f * r_gate) return 0.5f * v_max;
    return 0.5f * v_max + 0.5f * v_max * (d - 0.2f * r_gate) / (0.8f * r_gate);
}
// ---- Lane stage 1 (convoy-gate-transit.md sections 6.1-6.5; ship_steering_gates_lane): geometry and the speed laws ------------------
// One departure lane Lambda per gate: G + l_c (R, U) + s F for s in [-kLaneFarM, -kLaneEndM], l_c the middle of the members' finals; over
// the kLaneBlendM before d_end each final moves to its own pass point Q (q_point's window on the lower half; Lambda_5 continues it to
// the plane). Members join Lambda like cars at a lane merge: the lane order ranks by the ETA to d_end (a tie band, the higher sequence
// key yields, a leader ahead of its own escorts), the later ship yields (merge law), then follows (follow law); the ship ahead is never
// slowed for the pair. Each capital's order 4 ends at its lane point L_i (site A's f+0x240 rewrite, lane_order_point), d_end before the
// plane. The design values below are measured in the fixture (scenario C; verification/results/ship-steering/convoy-gate-transit/).
// The X3M_LANE_* macros exist for the fixture's sensitivity builds only (scenario_c.py); production builds take the defaults.
#ifndef X3M_LANE_GAP_M
#define X3M_LANE_GAP_M 500.0f // design 400 m; measured (scenario C): at 400 m the sweep's own margin at its 30 s horizon (50 m + 10 m/s x 30 s
#endif                        // + the rollouts' offset terms, 360-410 m) leaves a follower at g_req unresolved: site E passes the pair
#ifndef X3M_LANE_TAU_FOLLOW_S
#define X3M_LANE_TAU_FOLLOW_S 10.0f
#endif
#ifndef X3M_LANE_STOP_MS
#define X3M_LANE_STOP_MS 20000u
#endif
#ifndef X3M_LANE_LAMBDA_MAX_MS
#define X3M_LANE_LAMBDA_MAX_MS 17000u
#endif
#ifndef X3M_LANE_CRAWL_M
#define X3M_LANE_CRAWL_M 200.0f
#endif
#ifndef X3M_LANE_END_M
#define X3M_LANE_END_M 2300.0f
#endif
#ifndef X3M_LANE_ESC_JOIN_K
#define X3M_LANE_ESC_JOIN_K 1.0f // an escort's join completes this x lambda_max at the lane speed earlier (lane_join_end)
#endif
constexpr float kLaneGapM = X3M_LANE_GAP_M;                // g_req: tail(P) - nose(Y) along F once merged
constexpr float kLaneTauFollowS = X3M_LANE_TAU_FOLLOW_S;   // tau: v_Y = min(v_lane, v_P + (gap_settle - g_req) / tau), floor 10 m/s: the one
                                                           // gap law (round A, rank 4; the merge law's tau_m 4 s and its latch are gone)
constexpr std::uint32_t kLaneStopMs = X3M_LANE_STOP_MS;    // T_stop: a brief stop lasts at most this + the game step (Gate::step_ms)
constexpr std::uint32_t kLaneLambdaMaxMs = X3M_LANE_LAMBDA_MAX_MS; // lambda_max: the script's order 4 comes at most this (+ the frame
                                                                    // step) after the leader's launch (measured +4.1..+16.9 s, section 10.12):
                                                                    // the horizon of the lost-leader wait's order-0 hold; the escort gate's
                                                                    // braking reserve (lane_reserve) holds for any latency
constexpr float kLaneCrawlM = X3M_LANE_CRAWL_M; // the reserve's last stretch before d_end at the 10 m/s floor (then the stop 50 m short)
constexpr float kLaneEndM = X3M_LANE_END_M;                // d_end: order 4 ends this far before the plane (the short final, section 6.4)
// The installed gate rows (ship-navigation-ai.md section 10.9): the ordinary rows 0..3 and 5..14, row 4 (Xenon, a blended disc only) and
// rows 15..20 (inactive hubs, an empty body stub) carry no collision mesh; the Terran rows 21..24 carry the ring.
__attribute__((always_inline)) inline bool gate_no_mesh(std::uint16_t type) noexcept { return type <= 20; }
__attribute__((always_inline)) inline bool gate_terran(std::uint16_t type) noexcept { return type >= 21 && type <= 24; }
// Lane fix round 2 item 4 (review 1, 4, 5, 10): the gate's solid for the lane's clearance tests, a ring about the axis in the gate frame:
// radial (from the axis) in [h, r_out] over z (along F from G) in [z0, z1], in r_gate (gate_hole.txt, section 10.9: the installed Terran
// ring h 0.663, outer 1.026, body z -0.498..+0.179; the ordinary rows none (no collision body: a ship can never touch them); an unknown
// row the most conservative of the measured rings: the stock Argon hole 0.605, the outer 1.026, z -0.651..+0.655).
struct GateSolid {
    bool solid = false;
    float h = 0, r_out = 0, z0 = 0, z1 = 0; // m
};
inline GateSolid gate_solid(std::uint16_t type, float r_gate) noexcept {
    GateSolid s;
    if (gate_no_mesh(type) || !(r_gate > 0.0f)) return s;
    s.solid = true;
    const bool t = gate_terran(type);
    s.h = (t ? 0.663f : 0.605f) * r_gate;
    s.r_out = 1.026f * r_gate;
    s.z0 = (t ? -0.498f : -0.651f) * r_gate;
    s.z1 = (t ? 0.179f : 0.655f) * r_gate;
    return s;
}
// d_end of a gate: kLaneEndM, or on a gate with a solid at least its front + the longest hull (kLaneHullMaxM) + the stop's run-out of a
// hand-over at the lane speed (kLaneRunMaxM: 100 m/s at the slowest capital's 5 m/s^2, 1,000 m, and 1 s) + the sweep margin + the
// tracking allowance, so a member on its final at d_end (the hand-over until order 5, which the pair filter exempts from contact with
// its gate) is clear of the ring with its nose: the installed Terran ring (r 3207.5 m) 1597 + 2550 = 4147 m; every gate without a solid
// kLaneEndM. (Round 2 review item 10: kLaneHullMaxM is the design hull, not the installed roster's longest: 40 of the 131 installed
// capitals exceed 1,300 m in radius or hull half-extent, M2 to 2,998 m, M1 to 4,583 m, TL 1,837 m, M7 1,431 m (ships_by_class.json,
// convoy-gate-transit/round2_review/long_hulls.txt). For those the premise above does not hold; the own-gate row tests each member's own
// hull (L, w) against the solid on its path and over the hand-over's run-out, so such a hull is guarded there, never suppressed into the
// ring: the engine's warn and its 0x12, an eviction from the lane.)
constexpr float kLaneHullMaxM = 1300.0f; // the design hull half-length (installed medians: M1 / TL 1,135 m, M2 1,337 m)
constexpr float kLaneRunMaxM = 1100.0f;
inline float lane_end(float r_gate, std::uint16_t type) noexcept {
    const GateSolid s = gate_solid(type, r_gate);
    const float e = s.solid ? -s.z0 + kLaneHullMaxM + kLaneRunMaxM + 50.0f + 100.0f : 0.0f;
    return e > kLaneEndM ? e : kLaneEndM;
}
// A member's d_end (Member::d_end, lane_frame's copy of its gate's lane_end; 0 before: kLaneEndM)
__attribute__((always_inline)) inline float dend(const Member& m) noexcept { return m.d_end > 0.0f ? m.d_end : kLaneEndM; }
constexpr float kLaneFarM = 15000.0f;              // Lambda's far end
constexpr float kLaneBlendM = 2000.0f;             // the finals' lateral change from l_c to Q (<= 500 m over >= 2 km)
constexpr float kLaneOnM = 100.0f, kLaneOffM = 1500.0f; // on Lambda (its merge point M_i passed) within kLaneOnM laterally with the nose
constexpr float kLaneOnCos = 0.99862953f;               // within 3 deg of F; off beyond kLaneOffM or with the nose away (hysteresis)
constexpr float kLaneShiftMs = 5.0f; // l_c follows the members' middle at most this fast (m/s of game time): a member leaving or joining
                                     // moves the line under the others slowly (a 165 m shift over 33 s, about 0.3 deg)
constexpr float kLaneSideM = 300.0f; // a yielder level with its predecessor closes laterally to w_P + w_Y + this, never nearer, until gap >= g_req - 100 m
constexpr float kLaneHoldCos = 0.99026807f;        // cos 8 deg: site B holds the lane speed on order 0 / 0x1f with the nose this close to F
constexpr float kLaneJoinHoldCos = 0.70710678f;    // cos 45 deg (the steepest join): a joining member's order-0 hold (lane_hold_o0)
constexpr float kLaneJoinEndM = 1500.0f; // ... and completes this far before d_end (steeper when needed, at most 45 deg)
constexpr std::uint32_t kLaneStopEndMs = 2000; // lane fix round 1 item 4: a law stop episode ends this long (+ the step) after its condition
constexpr float kLaneOverApexR = 3.0f;          // ... an overshoot's U-turn reaches at most this x its turn radius beyond its turn point
constexpr float kLaneRedepM = 3000.0f;             // a re-departing ship flies out to d_end + this before it turns in (section 6.1), ...
constexpr float kLaneRedepSideM = 1000.0f;         // ... this far below Lambda (-U, the departure side): never head-on along the trail,
constexpr float kLaneTurnM = 1000.0f;              // ... then turns up and around onto Lambda: the carrot this far ahead (+F) and above (+U)
                                                   // of it (a capital's turn diameter ~1.1-1.4 km ends near Lambda, ~1.5 km below the arrivals)
constexpr float kLaneTieS = 2.0f;                  // the merge rank's tie band (section 6.2)
constexpr float kLaneKeepS = 5.0f;                 // the lane order's hysteresis: a pair keeps its order unless the ETAs differ by more
constexpr float kLaneGridTolM = 1.0f;              // the order-4 point guard: f+0x240 within this of a KC grid point (section 6.4)
constexpr std::uint32_t kLanePollMs = 11000;       // the KC's longest poll after a refused through slot (section 6.4's brake bound)
constexpr float kLaneTrigMarginM = 300.0f;         // ... which keeps the refused ship this far outside its trigger sphere
constexpr float kLaneStopV = 0.5f;                 // a predecessor slower than this is stopped (its follower stops at spacing)
constexpr std::uint32_t kLaneRefuseMs = 5000;      // a member at the hand-over this long without order 5 counts as refused (+ the step)
// The lane speed: the large cap (100 m/s) under the hull's v_max (site B's gate speed without the cornering term).
__attribute__((always_inline)) inline float lane_vmax(const Member& m) noexcept { return sc::gate_speed(m.params.v_max, m.large != 0, 0.0f); }
// A hull's final Q (section 6.1): q_point's window nearest straight below the axis (A_lat far down -U), the lower half of the disc.
__attribute__((always_inline)) inline bool lane_q(float r_s, float w, float L, float v_max, float r_gate, bool terran, float q[2]) noexcept {
    const float a_lat[2] = {0.0f, -4.0f * r_gate};
    float l4[2];
    return q_point(r_s, w, L, v_max, r_gate, a_lat, q, l4, terran);
}
// Lambda's lateral (R, U) for member m at s along F: l_c beyond the blend, m's Q from d_end + its settling distance on, linear between. A member that entered as
// an escort stays on l_c to d_end (no blend; order 5's Lambda_5 takes it to Q): the script's order 4 reaches it near d_end, inside the
// blend (a late blend would turn it off F at P2), and order 4's first tick tests the KC grid point
// before site A's rewrite takes effect (ship-navigation-ai.md section 10.12 item 2), so the escort must not sit in a grid sphere then:
// l_c lies at least 0.75 r_gate + kLaneGridClearM below the axis (outside the spheres of the grid's first row, slots 0-4), a final Q
// of an M7 (|Q| <= 667 m) lies inside slot 0's at the grid depth.
constexpr float kLaneGridClearM = 100.0f;
// Lane fix round 1 item 3 (Run173: the leader's P2 with the nose 22 deg off F): the blend ends a settling distance before d_end, the
// hull's pursuit look-ahead at the lane speed (pursuit_look: 2 v / W, a turn diameter), so the nose is back along F at P2.
__attribute__((always_inline)) inline float lane_blend_settle(const Gate& g, const Member& m) noexcept {
    return m.lane_settle > 0.0f ? m.lane_settle : pursuit_look(lane_vmax(m), m.params.W, g.r_gate);
}
// Section 12.3 (round B): the lane's lateral profile, delta(s) (R, U; m): 0 outside the knots; at most three compares and one division
// (lane_lat_at, every rollout sample)
__attribute__((always_inline)) inline void lane_prof_delta(const LaneProfile& p, float s, float d[2]) noexcept {
    d[0] = d[1] = 0.0f;
    if (!p.n || !(s > p.s[0]) || !(s < p.s[p.n - 1])) return;
    unsigned i = 1;
    while (i + 1 < p.n && !(s < p.s[i])) ++i;
    const float span = p.s[i] - p.s[i - 1], t = span > 1.0f ? (s - p.s[i - 1]) / span : 1.0f;
    d[0] = p.d[i - 1][0] + t * (p.d[i][0] - p.d[i - 1][0]);
    d[1] = p.d[i - 1][1] + t * (p.d[i][1] - p.d[i - 1][1]);
}
// (section 12.3, round B: plus the lane's lateral profile delta(s) for statics on Lambda, 0 from the blend's start on)
__attribute__((always_inline)) inline void lane_lat_at(const Gate& g, const Member& m, float s, float lat[2]) noexcept {
    float k = (s + dend(m) + lane_blend_settle(g, m) + kLaneBlendM) / kLaneBlendM;
    k = m.role == Role::escort || m.lane_esc ? 0.0f : k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k;
    lat[0] = g.lane_c[0] + k * (m.q[0] - g.lane_c[0]);
    lat[1] = g.lane_c[1] + k * (m.q[1] - g.lane_c[1]);
    if (g.prof.n) {
        float d[2];
        lane_prof_delta(g.prof, s, d);
        lat[0] += d[0];
        lat[1] += d[1];
    }
}
__attribute__((always_inline)) inline void lane_point_at(const Gate& g, const Member& m, float s, float P[3]) noexcept {
    float lat[2];
    lane_lat_at(g, m, s, lat);
    for (unsigned i = 0; i < 3; ++i) P[i] = g.G[i] + lat[0] * g.R[i] + lat[1] * g.U[i] + s * g.F[i];
}
// L_i (section 6.4): order 4's own test (dist <= 0.75 r_gate, 0x00454b1a) fires at d_end when its point lies d_end - 0.75 r_gate before
// the plane on the hull's final (the note's "d_end + 0.75 r_gate" read as the arrival depth; Implementation section).
__attribute__((always_inline)) inline float lane_point_depth(float r_gate, std::uint16_t type = 0) noexcept {
    return lane_end(r_gate, type) - sc::kGateCrossFraction * r_gate;
}
inline void lane_order_point(const Gate& g, const Member& m, float L[3]) noexcept {
    const float z = -lane_point_depth(g.r_gate, g.type);
    const float lr = m.lane_esc ? g.lane_c[0] : m.q[0], lu = m.lane_esc ? g.lane_c[1] : m.q[1]; // (an escort's final: l_c, lane_lat_at)
    for (unsigned i = 0; i < 3; ++i) L[i] = g.G[i] + lr * g.R[i] + lu * g.U[i] + z * g.F[i];
}
// The KC grid point guard (section 6.4): p (world, m) within kLaneGridTolM of G - 2 (r_gate + r_s) F + 666 m (a R + b U), a in -2..2,
// b in -3..0 (the 20 slots of GetWarpInPos, ship-navigation-ai.md section 10.2). Returns the slot (0..19) or -1.
inline int kc_grid_slot(const float G[3], const float R[3], const float U[3], const float F[3], float r_gate, float r_s, const float p[3]) noexcept {
    if (!(r_gate > 0.0f) || !(r_s > 0.0f) || !sc::finite_f(p[0]) || !sc::finite_f(p[1]) || !sc::finite_f(p[2])) return -1;
    const float d[3] = {p[0] - G[0], p[1] - G[1], p[2] - G[2]};
    const float x = sc::dot3(d, R), y = sc::dot3(d, U), z = sc::dot3(d, F);
    if (sc::fabs_f(z + 2.0f * (r_gate + r_s)) > kLaneGridTolM) return -1;
    const float pitch = sc::kGateGridPitchM;
    const float af = x / pitch, bf = y / pitch;
    if (!(af > -2.5f && af < 2.5f && bf > -3.5f && bf < 0.5f)) return -1;
    const int a = int(af + (af >= 0.0f ? 0.5f : -0.5f)), b = int(bf + (bf >= 0.0f ? 0.5f : -0.5f));
    if (sc::fabs_f(x - float(a) * pitch) > kLaneGridTolM || sc::fabs_f(y - float(b) * pitch) > kLaneGridTolM) return -1;
    static const int col[5] = {4, 2, 0, 1, 3}; // a = -2, -1, 0, 1, 2 (the KC's column order 0, 1, -1, 2, -2)
    return -b * 5 + col[a + 2];
}
// Whether p (world, m) is a lane point L_i of a hull with the final q (R, U; m) at this gate: its depth within the flight validator's
// 10 % of L_i's depth (sc::kGateGridDepthTol; a KC grid point lies at 2 (r_gate + r_s), never there) and its lateral within its 20 m
// (sc::kGateGridTolM) of q or, when lc is given, of Lambda's l_c (an escort's final; l_c moves at most kLaneShiftMs, so a written point
// read back seconds later is not compared to the live l_c within 1 m: review item 4).
inline bool lane_point_match(const float G[3], const float R[3], const float U[3], const float F[3], float r_gate, const float q[2], const float* lc,
                             const float p[3], std::uint16_t type = 0) noexcept {
    if (!(r_gate > 0.0f) || !sc::finite_f(p[0]) || !sc::finite_f(p[1]) || !sc::finite_f(p[2])) return false;
    const float d[3] = {p[0] - G[0], p[1] - G[1], p[2] - G[2]};
    const float x = sc::dot3(d, R), y = sc::dot3(d, U), z = sc::dot3(d, F), depth = lane_point_depth(r_gate, type);
    if (!(depth > 0.0f) || sc::fabs_f(-z - depth) > sc::kGateGridDepthTol * depth) return false;
    const float tol2 = sc::kGateGridTolM * sc::kGateGridTolM;
    const float dq = (x - q[0]) * (x - q[0]) + (y - q[1]) * (y - q[1]);
    if (dq <= tol2) return true;
    return lc && (x - lc[0]) * (x - lc[0]) + (y - lc[1]) * (y - lc[1]) <= tol2;
}
// Whether p is member m's lane point L_i (a written point read back: the field still holds ours after a slot reset or a load).
inline bool lane_point_ours(const Gate& g, const Member& m, const float p[3]) noexcept {
    return m.q_ok && lane_point_match(g.G, g.R, g.U, g.F, g.r_gate, m.q, g.lane_c_ok ? g.lane_c : nullptr, p, g.type);
}
// Review item 4: whether f+0x240 (p) is this hull's own lane point when the ship has no lane member (a load mid-approach, a GateState
// slot reset after a > 5 s order-0 gap): its final Q from its hull alone (lane_q, the lower half), compared as lane_point_match; lc the
// gate board's l_c when it has one (an escort's point). The caller then gives the order's geometry the KC's slot-0 grid point.
inline bool lane_point_own(const float G[3], const float R[3], const float U[3], const float F[3], float r_gate, float r_s, float w, float L,
                           float v_max, bool terran, const float* lc, const float p[3], std::uint16_t type = 0) noexcept {
    float q[2];
    if (!lane_q(r_s, w, L, v_max, r_gate, terran, q)) return false;
    return lane_point_match(G, R, U, F, r_gate, q, lc, p, type);
}
// Final review item 4: whether p could be an escort's L_i written on some l_c, for a ship with no lane member and no board l_c (a load:
// the board is rebuilt from the members' commits, so lane_point_own has no lc). l_c is the middle of the members' finals, all inside
// the gate's disc in its lower half, and lies at least 0.75 r_gate + kLaneGridClearM below the axis (lane_frame's clamp): p at L_i's
// depth (as lane_point_match: within 10 %, never a KC grid point's 2 (r_gate + r_s)) with its lateral inside the disc and below that
// clearance (+ the 20 m tolerance).
inline bool lane_point_lc_env(const float G[3], const float R[3], const float U[3], const float F[3], float r_gate, const float p[3],
                              std::uint16_t type = 0) noexcept {
    if (!(r_gate > 0.0f) || !sc::finite_f(p[0]) || !sc::finite_f(p[1]) || !sc::finite_f(p[2])) return false;
    const float d[3] = {p[0] - G[0], p[1] - G[1], p[2] - G[2]};
    const float x = sc::dot3(d, R), y = sc::dot3(d, U), z = sc::dot3(d, F), depth = lane_point_depth(r_gate, type);
    if (!(depth > 0.0f) || sc::fabs_f(-z - depth) > sc::kGateGridDepthTol * depth) return false;
    const GateSolid gs = gate_solid(type, r_gate); // (lane fix round 2 item 4: on a gate with a solid l_c lies inside its hole)
    if (gs.solid) return y <= 0.0f && x * x + y * y <= gs.h * gs.h;
    return y <= -(sc::kGateCrossFraction * r_gate + kLaneGridClearM) + sc::kGateGridTolM && x * x + y * y <= r_gate * r_gate;
}
// Where member m's join onto Lambda completes (m before the plane): kLaneJoinEndM before d_end; an escort's (it entered as one) a further
// lambda_max at the lane speed, so that its lost-leader wait (order 0: no leaf, the nose held, lane_hold_o0) begins on Lambda and not in
// the join's last turn (scenario C crossing: an escort joining at 35 deg reached P2 75-100 m off Lambda with the nose beyond 8.1 deg)
__attribute__((always_inline)) inline float lane_join_end(const Member& m) noexcept {
    return dend(m) + kLaneJoinEndM + (m.role == Role::escort || m.lane_esc ? X3M_LANE_ESC_JOIN_K * lane_vmax(m) * float(kLaneLambdaMaxMs) * 0.001f : 0.0f);
}
// Lane fix round 1 item 1 (Run173: the old carrot, kLaneJoinK x the lateral ahead on Lambda, was a fixed point at s = -3.8 km for a ship
// 15 km off Lambda 45 km out: the ships flew ~20 deg off the axis to 3 km out and turned onto Lambda there). The join is a tangent onto
// Lambda: from the ship at (s, y) the reference path is the parabola y_ref(s') = y ((s_m - s') / (s_m - s))^2, tangent to Lambda at its
// merge point s_m (its vertex: the parabola through any later point of itself with the same s_m is the same curve, so the stateless rule
// re-planned every frame flies one path). Its steepest angle is at its start, atan(2 y / D) with D = s_m - s, and its vertex curvature
// 2 y / D^2. The span a join needs: D >= 2 y / tan 30 deg and D >= sqrt(2 y R) (the vertex within the hull's turn radius R = v / W). The
// merge point: Lambda's far end (15 km) when that span fits, else kLaneMergeM (8 km), else the join's end (lane_join_end) with the room
// it leaves (steeper, the start at most 45 deg: D >= 2 y), past it the room to d_end, else 2 y (the residual of an order-0 coast). A ship
// following its parabola keeps the span (both D_need and D shrink as the parabola's D^2), so the merge point changes only toward the
// earlier candidate (a ship far out and wide starts on the join's end and moves to 8 or 15 km once that fits: a step of a few degrees).
// R = 0: no turn term (lane_join_loss).
constexpr float kLaneJoinTan = 0.57735027f; // tan 30 deg: the join's steepest angle where the room allows
constexpr float kLaneMergeM = 8000.0f;      // the merge point when Lambda's far end does not fit
// A member's turn radius at its lane speed (m; 0 without a turn rate): the R of lane_join_span for lane_join_loss.
__attribute__((always_inline)) inline float lane_turn_r(const Member& m) noexcept {
    const float v = lane_vmax(m) > kYieldFloorMs ? lane_vmax(m) : kYieldFloorMs;
    return m.params.W > 1e-4f ? v / m.params.W : 0.0f;
}
// The span the tangent join from the lateral y needs (lane_join_span's D_need: the 30 deg start, the vertex within R).
__attribute__((always_inline)) inline float lane_join_need(float y, float R) noexcept {
    const float yy = y > 0.0f ? y : 0.0f;
    float need = 2.0f * yy / kLaneJoinTan;
    if (2.0f * yy * R > need * need) need = sc::sqrt_ss(2.0f * yy * R);
    return need;
}
// Convoy-gate-transit.md section 12.4 (round C): the loop-back join after a flown 0x12. A member re-committed after its eviction whose
// join from there does not fit (the room to d_end short of 2 y + its settling distance: lane_join_span's residual) flies the
// re-departure's geometry from where it is: onto the out-leg 2.5 R below Lambda (pursued at its settling distance ahead, the turn point
// kept R ahead of the ship until it is on the leg: within kLaneLoopConvR R of the line, the nose within 15 deg of -F), out to the turn
// point, the U-turn up onto Lambda, then the tangent join with the room for a residual lateral of kLaneLoopResR R after the U-turn: the
// turn point lies at least d_end + settle + R + D_need(kLaneLoopResR R) out (kLaneRedepM at least)
constexpr float kLaneLoopResR = 1.0f, kLaneLoopConvR = 0.5f;
constexpr float kLaneLoopConvCos = 0.96592583f; // cos 15 deg
__attribute__((always_inline)) inline float lane_join_span(float s, float y, float R, float join_end, float d_end = kLaneEndM) noexcept {
    const float yy = y > 0.0f ? y : 0.0f;
    float need = 2.0f * yy / kLaneJoinTan;
    if (2.0f * yy * R > need * need) need = sc::sqrt_ss(2.0f * yy * R); // (the turn term only where it binds: small laterals)
    if (-kLaneFarM - s >= need) return -kLaneFarM - s;
    if (-kLaneMergeM - s >= need) return -kLaneMergeM - s;
    const float room = -join_end - s, room_end = -d_end - s;
    return room >= 2.0f * yy ? room : room_end >= 2.0f * yy ? room_end : 2.0f * yy;
}
// The along-track distance a member at s with the lateral y still to join loses against a ship on Lambda at the same speed: the
// parabola's arc length less its span, D f(m) with m = 2 y / D its start slope, f(m) = (m sqrt(1 + m^2) + asinh m) / m^2 - 1 =
// m^2 / 6 - m^4 / 40 + m^6 / 112 - ...: y (m / 3 - m^3 / 20 + m^5 / 56) (within 2 % for m <= 1). (Before round 1: y tan(theta / 2) for
// the straight join; scenario C crossing: an escort joining at 37 deg behind its leader at 34 deg gained 450 m of gap over 130 s.)
// (lane fix round 2 item 6: R, the hull's turn radius at the lane speed (lane_turn_r), as lane_carrot's span: with R = 0 the span of a
// small lateral, where the turn term binds, was the 30 deg one and the loss credited to the pair larger than the join flown)
__attribute__((always_inline)) inline float lane_join_loss(float s, float y, float join_end, float d_end = kLaneEndM, float R = 0.0f) noexcept {
    if (!(y > kLaneOnM)) return 0.0f;
    const float D = lane_join_span(s, y, R, join_end, d_end);
    float m = D > 1.0f ? 2.0f * y / D : 1.0f;
    if (m > 1.0f) m = 1.0f;
    const float m2 = m * m;
    return y * m * (1.0f / 3.0f - m2 * (1.0f / 20.0f) + m2 * m2 * (1.0f / 56.0f));
}
// The lane carrot (section 6.1; lane_steer, the rollout) in the plane of F and the lateral: s along F from G, y the lateral distance from
// Lambda at s (>= 0), hold_lat (a merging yielder still level with its predecessor: its lateral closes to y_clear = w_P + w_Y + kLaneSideM
// at most, the ship ahead's lane footprint never entered; the join onto Lambda once gap >= g_req - 100 m), redep (out along -F to the turn
// point on Lambda: the arrival came out on +U, Lambda lies on -U, so the turn is on the departure side). Returns the carrot (sc, yc).
__attribute__((always_inline)) inline void lane_carrot(float s, float y, float v, float W, float r_gate, bool hold_lat, bool redep, float* sc_out,
                                                       float* yc, float y_clear = 0.0f, float join_end = kLaneEndM + kLaneJoinEndM,
                                                       float out_m = kLaneRedepM, float out_r = 0.0f, float d_end = kLaneEndM,
                                                       float out_look = 0.0f) noexcept {
    const float look = pursuit_look(v, W, r_gate);
    if (redep && s > -(d_end + out_m)) { // out to the turn point (out_m: Member::redep_out), kLaneRedepSideM below Lambda (y_clear:
        // its lateral in the caller's plane); the carrot kLaneBlendM beyond it, an overshoot's (out_r: its turn radius) one radius beyond;
        // a loop's (out_look > 0, section 12.4) out_look ahead of the ship where that is nearer (its out-leg pursued onto the line)
        float so = -(d_end + out_m) - (out_r > 0.0f ? out_r : kLaneBlendM);
        if (out_look > 0.0f && s - out_look > so) so = s - out_look;
        *sc_out = so;
        *yc = y_clear;
        return;
    }
    if (redep) { // past it with the nose still away: the turn on the departure side (y_clear: the caller's lateral of the -U offset)
        *sc_out = s + kLaneTurnM;
        *yc = y + y_clear;
        return;
    }
    // the join (lane fix round 1 item 1): the pursuit of the tangent parabola onto Lambda (lane_join_span), the carrot a look-ahead ahead
    // on it (on Lambda past its merge point); a residual past the join's end (an order-0 coast's drift) ends tangent at Lambda's d_end
    // point, so the nose is along F at P2
    const float R = W > 1e-4f ? (v > kYieldFloorMs ? v : kYieldFloorMs) / W : 0.0f;
    const float D = lane_join_span(s, y, R, join_end, d_end), s_m = s + D;
    float along = s + look, yr = 0.0f;
    if (along < s_m && D > 1.0f) {
        const float u = (s_m - along) / D;
        yr = y * u * u;
    }
    if (along > 0.0f) along = 0.0f; // the plane at Q (order 4 ends at d_end on its own)
    *sc_out = along;
    *yc = hold_lat ? (yr > (y > y_clear ? y_clear : y) ? yr : (y > y_clear ? y_clear : y)) : yr; // holding: never nearer than the band
}
__attribute__((always_inline)) inline bool unit_row(const float f[3]) noexcept {
    const float n2 = sc::dot3(f, f);
    return sc::finite_f(n2) && n2 >= 0.9f && n2 <= 1.1f;
}
// ---- Convoy-gate-transit.md section 12.3 (round B): the lateral profile for statics on Lambda --------------------------------------
// Statics on Lambda (a station's parts, rocks, a fallback body that crosses the column) become a lateral shift delta(s) of the lane,
// not a per-ship detour: every member, rollout and the obstacle row's path fly the same shifted line (lane_lat_at), lane_on measures
// against it, the sweep certifies one plan, the lane speed is kept. Built once per game second at the frame boundary from the module's
// static snapshot (lane_bodies, ship_gate_ctl_live_core.h):
// * the line is the hull's centre line; a body is cleared when the curve (s, l_c + delta(s)) stays w_max + r_body + the margin from its
//   centre (every point of a member's capsule rides on the line, so its half-length does not enter); the margin kMarginM, lowered in
//   kLaneProfMarginStepM steps to 0 when the ramps do not fit (profile.clear_m records the margin kept);
// * one plateau and two ramps (3 segments, 4 knots), the ramps at tan 15 deg tangent to the bodies' clearance circles; the direction -U
//   first (the departure side), then +-R (the smaller shift first), +U only when the whole profile lies beyond kLaneProfNoUpM of the
//   gate; at most kLaneProfMaxM;
// * delta is 0 from the blend's start on (s >= -(d_end + settle + kLaneBlendM) of the members): the blend zone and the finals are never
//   shifted; a body whose clearance the ramps cannot reach before it is left to the module's detour and the engine's warn as today;
// * a fallback body (a station whose part walk failed: its sphere) that contains Lambda's d_end point or the gate (the lane runs inside
//   it) is neither shifted nor detoured: the detour has nowhere to go (d < C everywhere) and the engine tests the real parts, a real part
//   on Lambda then costing a vanilla 0x12 (ship-steering-and-avoidance.md section 3.5 item 3); one that only crosses the column is a body
//   of the module's effective radius.
// The bodies the profile clears and the enclosing fallbacks go to profile.body[]: a lane member on Lambda does not detour around them
// (the avoidance seam, av::Self::skip_static); everything else keeps the sphere avoidance, and the engine's warn stays the backstop.
struct LaneBody {
    float c[3];        // the body's centre (world, m)
    float r;           // its radius (a part sphere, a rock; a fallback body: the module's effective radius)
    float rs;          // a fallback body's bounding sphere (m; 0: not a fallback body)
    std::uint32_t ptr; // the snapshot object (a station's parts share their station's)
};
constexpr float kLaneProfTan = 0.26794919f, kLaneProfCos = 0.96592583f; // tan / cos 15 deg: the ramps' slope
constexpr float kLaneProfMaxM = 1500.0f;          // the largest shift (kLaneOffM)
constexpr float kLaneProfNoUpM = 6000.0f;         // never +U within this of the gate
constexpr float kLaneProfMarginStepM = 25.0f;     // the margin's steps from kMarginM down to 0
constexpr float kLaneProfCheckM = 50.0f;          // the verification's sampling along Lambda
constexpr std::uint32_t kLaneProfPeriodMs = 1000; // the build's period (game time)
constexpr std::uint32_t kLaneProfMaxAgeMs = 5000; // a profile not rebuilt for this long + the step is dropped (no snapshot of the sector)
constexpr unsigned kLaneProfScan = 64;            // bodies one build takes (more near Lambda: overflow, no profile)
// The shift is built only with shift (kLaneProfShiftOn, off in round B): the fixture's lane_belt regressed with it (a 898 m -U plateau at
// the 50 m margin; on SETA frames site E's suppression of the profiled rocks fails closed (pass_not_fresh), the members fly the engine's
// 0x12 and meet each other: 0 of 6 SETA runs green against 5 of 6 with the sphere detour; at tf 1 a member touches a rock at 50 m), and
// lane_station has no feasible shift (its 929 m clearance cannot ramp back at 15 deg before the blend: 133 m short at the 50 m margin);
// convoy-gate-transit.md section 11 "Round B". The enclosing-fallback rule is on.
constexpr bool kLaneProfShiftOn = false;
// Review of rounds A-C item 5: the enclosing-fallback rule takes only a fallback body the lane could not detour around within its largest
// shift (the module's fallback sphere's radius above kLaneProfMaxM): a small station whose part walk failed and whose bounding sphere holds
// d_end (a 700 m fallback body on the final) keeps the members' sphere detour (before: listed for every member, no detour, the engine's
// warn the only backstop). (X3M_LANE_ENCLOSE_MIN_M: the fixture's A/B; 0 is the behaviour before.)
#ifndef X3M_LANE_ENCLOSE_MIN_M
#define X3M_LANE_ENCLOSE_MIN_M kLaneProfMaxM
#endif
constexpr float kLaneEncloseMinM = X3M_LANE_ENCLOSE_MIN_M;
// The build (1 Hz). bd: the bodies near Lambda (overflow: more than the scan takes: no profile, fail closed to today's detour).
inline void lane_profile_build(Gate& g, const LaneBody* bd, unsigned nb, bool overflow, std::uint32_t now, bool shift = kLaneProfShiftOn) noexcept {
    LaneProfile& p = g.prof;
    p.built_ms = now ? now : 1u;
    p.bodies = nb;
    p.n = p.nb = p.ne = 0;
    p.overflow = overflow ? 1 : 0;
    p.clear_m = 0.0f;
    p.st_ok = p.st_n = 0;
    if (!g.lane || !g.lane_c_ok || !(g.r_gate > 0.0f) || overflow) return;
    float w = 0.0f, fin = 0.0f;
    for (unsigned i = 0; i < g.n; ++i) {
        const Member& m = g.m[i];
        if (!m.ship || !m.ln || !(m.role == Role::committed || m.role == Role::escort)) continue;
        if (m.w > w) w = m.w;
        const float f = dend(m) + lane_blend_settle(g, m) + kLaneBlendM;
        if (f > fin) fin = f;
    }
    if (!(w > 0.0f) || !sc::finite_f(fin)) return;
    const float s_fin = -fin, d_end = lane_end(g.r_gate, g.type);
    float sk[kLaneProfScan], ek[kLaneProfScan][2], rk[kLaneProfScan];
    std::uint32_t pk[kLaneProfScan];
    unsigned n = 0;
    p.st_ok = 1; // (round C: the envelope below, from the same scan)
    p.st_n = 0;
    for (unsigned k = 0; k < nb && n < kLaneProfScan; ++k) {
        const LaneBody& b = bd[k];
        const float d[3] = {b.c[0] - g.G[0], b.c[1] - g.G[1], b.c[2] - g.G[2]};
        const float s = sc::dot3(d, g.F), eR = sc::dot3(d, g.R) - g.lane_c[0], eU = sc::dot3(d, g.U) - g.lane_c[1];
        if (!sc::finite_f(s) || !sc::finite_f(eR) || !sc::finite_f(eU) || !(b.r > 0.0f)) continue;
        if (b.rs > 0.0f && b.r > kLaneEncloseMinM) { // a large fallback body containing Lambda's d_end point or the gate: neither shifted nor
                                                    // detoured (review item 5: a small one keeps the detour)
            const float sd = s + d_end, in_end = sd * sd + eR * eR + eU * eU;
            if (in_end < b.rs * b.rs || sc::dot3(d, d) < b.rs * b.rs) {
                if (p.nb < kLaneProfBodies) p.body[p.nb++] = b.ptr;
                continue;
            }
        }
        sk[n] = s;
        ek[n][0] = eR;
        ek[n][1] = eU;
        rk[n] = b.r;
        pk[n] = b.ptr;
        ++n;
        if (!p.st_n || s - b.r < p.st_lo) p.st_lo = s - b.r;
        if (!p.st_n || s + b.r > p.st_hi) p.st_hi = s + b.r;
        if (p.st_n < 0xff) ++p.st_n;
    }
    const unsigned nb_enclose = p.nb;
    p.ne = std::uint8_t(nb_enclose);
    if (!shift) return;
    // the bodies the unshifted line meets (before the blend; one in the blend or the finals is never shifted around)
    auto meets = [&](unsigned k, float margin) {
        const float need = w + rk[k] + margin;
        return sk[k] - need < s_fin && ek[k][0] * ek[k][0] + ek[k][1] * ek[k][1] < need * need;
    };
    static constexpr float kDir[4][2] = {{0.0f, -1.0f}, {1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}};
    for (float margin = kMarginM; margin >= 0.0f; margin -= kLaneProfMarginStepM) {
        bool any = false;
        for (unsigned k = 0; k < n && !any; ++k) any = meets(k, margin);
        if (!any) return; // (nothing on Lambda: no profile; at kMarginM the first pass)
        // the shift per direction: the larger root of |e - t d| = need for every body the plateau line meets, iterated (a shift can
        // carry the line into another body)
        float tdir[4];
        for (unsigned di = 0; di < 4; ++di) {
            const float* dv = kDir[di];
            float t = 0.0f;
            for (unsigned it = 0; it <= n; ++it) {
                float tn = t;
                for (unsigned k = 0; k < n; ++k) {
                    if (!(sk[k] - (w + rk[k] + margin) < s_fin)) continue;
                    const float need = w + rk[k] + margin, lx = ek[k][0] - t * dv[0], ly = ek[k][1] - t * dv[1];
                    if (lx * lx + ly * ly >= need * need) continue;
                    const float ep = ek[k][0] * dv[0] + ek[k][1] * dv[1], e2 = ek[k][0] * ek[k][0] + ek[k][1] * ek[k][1];
                    const float root = ep + sc::sqrt_ss(ep * ep - e2 + need * need);
                    if (root > tn) tn = root;
                }
                if (!(tn > t)) break;
                t = tn;
            }
            tdir[di] = t;
        }
        // the order: -U, then +-R (the smaller first), then +U
        unsigned order[4] = {0, tdir[1] <= tdir[2] ? 1u : 2u, tdir[1] <= tdir[2] ? 2u : 1u, 3};
        for (unsigned oi = 0; oi < 4; ++oi) {
            const unsigned di = order[oi];
            const float* dv = kDir[di];
            const float t = tdir[di];
            if (!(t > 0.0f) || t > kLaneProfMaxM) continue;
            // the ramps tangent to each met body's clearance circle in (s, the shift): its centre (s_k, e . d), radius
            // sqrt(need^2 - e_perp^2); a ramp of slope tan clears it when its line lies R / cos above the centre
            float lo = 1e30f, hi = -1e30f;
            for (unsigned k = 0; k < n; ++k) {
                const float need = w + rk[k] + margin;
                if (!(sk[k] - need < s_fin)) continue;
                const float ep = ek[k][0] * dv[0] + ek[k][1] * dv[1], e2 = ek[k][0] * ek[k][0] + ek[k][1] * ek[k][1];
                const float perp2 = e2 - ep * ep, R2 = need * need - perp2;
                if (!(R2 > 0.0f)) continue; // beside the shift's plane: never met
                const float lx = ek[k][0], ly = ek[k][1];
                if (lx * lx + ly * ly >= need * need && !(ep > 0.0f)) continue; // clear at 0 and the shift moves away from it
                if (ep - sc::sqrt_ss(R2) >= t) continue;                          // beyond the plateau: never met
                const float a = (sc::sqrt_ss(R2) / kLaneProfCos + ep - t) / kLaneProfTan;
                if (sk[k] + a > hi) hi = sk[k] + a;
                if (sk[k] - a < lo) lo = sk[k] - a;
            }
            if (!(hi >= lo)) {
                if (!(hi > -1e29f)) continue;
                lo = hi = 0.5f * (lo + hi);
            }
            const float ramp = t / kLaneProfTan, s0 = lo - ramp, s3 = hi + ramp;
            if (s3 > s_fin) continue;                       // the ramp reaches the blend: not this direction
            if (di == 3 && s3 > -kLaneProfNoUpM) continue;  // +U within 6 km of the gate: never
            // verify the curve against every body (sampled every kLaneProfCheckM about each body)
            LaneProfile q;
            q.n = 4;
            q.s[0] = s0;
            q.s[1] = lo;
            q.s[2] = hi;
            q.s[3] = s3 > hi + 1.0f ? s3 : hi + 1.0f;
            q.d[1][0] = q.d[2][0] = t * dv[0];
            q.d[1][1] = q.d[2][1] = t * dv[1];
            bool ok = true;
            for (unsigned k = 0; k < n && ok; ++k) {
                const float need = w + rk[k] + margin;
                for (float s = sk[k] - need; s <= sk[k] + need && ok; s += kLaneProfCheckM) {
                    float dl[2];
                    lane_prof_delta(q, s, dl);
                    const float lx = ek[k][0] - dl[0], ly = ek[k][1] - dl[1], ds = s - sk[k];
                    if (ds * ds + lx * lx + ly * ly < need * need * 0.998f) ok = false; // (0.1 % : the tangent's float slack)
                }
            }
            if (!ok) continue;
            for (unsigned i = 0; i < 4; ++i) {
                p.s[i] = q.s[i];
                p.d[i][0] = q.d[i][0];
                p.d[i][1] = q.d[i][1];
            }
            p.n = 4;
            p.clear_m = margin;
            for (unsigned k = 0; k < n; ++k) { // the bodies the line met: cleared now (a station's parts share their station: once; not
                                               // when one of its parts lies on the unshifted finals, which still need the detour)
                if (!meets(k, margin)) continue;
                bool listed = false;
                for (unsigned j = 0; j < p.nb && !listed; ++j) listed = p.body[j] == pk[k];
                for (unsigned j = 0; j < n && !listed; ++j) {
                    const float nj = w + rk[j] + margin;
                    listed = pk[j] == pk[k] && !(sk[j] - nj < s_fin) && ek[j][0] * ek[j][0] + ek[j][1] * ek[j][1] < nj * nj;
                }
                if (!listed && p.nb < kLaneProfBodies) p.body[p.nb++] = pk[k];
            }
            return;
        }
    }
    p.nb = std::uint8_t(nb_enclose); // infeasible at every margin: no shift; the enclosing fallbacks stay listed
}
} // namespace x3m::ship_avoidance::ctl
