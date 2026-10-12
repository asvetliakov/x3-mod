// Host double of the smooth ship steering law (src/proxy/ship_steering_core.h)
// against a kinematic model of the engine (docs/architecture/
// ship-steering-and-avoidance.md section 7.1; engine facts from
// docs/reverse-engineering/ship-movement.md): the per-axis controller
// 0x0043ad20 in its integer 16.16 arithmetic (ported from
// verification/results/ship-movement/axis_controller_sim.py and regression-
// checked against its twelve settle times), the integrator's rotation and
// position update, the speed slew, the 0-50 ms think tick and MovePos's own
// ramp and arrival test. Five representative installed ships, 30/60/144 fps,
// three tick seeds. No Wine, no game.
//
//   c++ -std=c++17 -O2 -Wall -Wextra -Werror ship_steering_host.cpp && ./a.out
//   X3M_SS_TRACE=1 ./a.out   # adds the 30 fps diagonal-turn and 60 fps formation time series (seed 1)
#include "../../src/proxy/ship_steering_core.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <cstring>

namespace core = x3m::ship_steering::core;
static unsigned checks = 0, failures = 0;
static void check(bool yes, const char* why) {
    ++checks;
    if (!yes) {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n", why);
    }
}
static void checkf(bool yes, const char* fmt, double a, double b) {
    ++checks;
    if (!yes) {
        ++failures;
        std::fprintf(stderr, "FAIL ");
        std::fprintf(stderr, fmt, a, b);
        std::fprintf(stderr, "\n");
    }
}

// ---- the engine's per-axis controller, 16.16 integer (axis_controller_sim.py) ----
static std::int64_t fx(std::int64_t a, std::int64_t b) { // (a*b + 0x8000) >> 16, arithmetic shift
    const std::int64_t p = a * b + 0x8000;
    return p >> 16;
}
static std::int64_t div16(std::int64_t a, std::int64_t b) { // (a << 16) / b truncating toward zero
    if (b == 0) return 0;
    const std::int64_t n = a << 16;
    std::int64_t q = (n < 0 ? -n : n) / (b < 0 ? -b : b);
    return ((a < 0) == (b < 0)) ? q : -q;
}
struct Step {
    std::int64_t w, d;
};
static Step controller(std::int64_t e, std::int64_t w, std::int64_t wmax, std::int64_t acc, std::int64_t dt_ms) {
    const std::int64_t t = div16(dt_ms, 1000);
    if (wmax == 0) return {0, 0};
    if (acc < 1) acc = 1;
    if (w < 0) {
        const std::int64_t ts = div16(w, acc);
        if (-ts < t) return {0, fx(w, -ts)};
        return {fx(t, acc) + w, fx(w, t)};
    }
    const std::int64_t ts = div16(w, acc);
    const std::int64_t dstop = fx(w, ts);
    if (e < dstop) {
        if (ts < t) return {0, dstop};
        return {w - fx(t, acc), fx(w, t)};
    }
    const std::int64_t tc = div16(e - dstop, wmax);
    if (t <= tc) {
        const std::int64_t ta = div16(wmax - w, acc);
        if (t < ta) return {w + fx(t, acc), fx(w, t)};
        return {wmax, fx(w, ta) + fx(wmax, t - ta)};
    }
    std::int64_t nw = fx(tc, acc) + w, ta;
    if (nw < wmax)
        ta = tc;
    else {
        ta = div16(wmax - w, acc);
        nw = wmax;
    }
    const std::int64_t d0 = fx(w, ta);
    std::int64_t rest = e - d0 - dstop;
    if (rest < 0) rest = 0;
    const std::int64_t tr = nw ? div16(rest, nw) : 0;
    if (t <= tr + ta) return {nw, fx(nw, t - ta) + d0};
    const std::int64_t tb = t - tr - ta;
    return {nw - fx(tb, acc), fx(nw, tb) + rest + d0};
}
// The integrator's use of it (ship-movement.md section 3): sign-mirrored for a
// negative remaining angle, the delta clamped to the remainder.
static void integrate_axis(std::int64_t& e, std::int64_t& w, std::int64_t wmax, std::int64_t acc, std::int64_t dt_ms, double* turned) {
    *turned = 0;
    if (e == 0) {
        if (w != 0) { // a cleared remainder: the controller brakes (w < 0 branch mirrored) - the model lets the rate decay
            const Step s = controller(0, w < 0 ? -w : w, wmax, acc, dt_ms);
            w = w < 0 ? -s.w : s.w;
        }
        return;
    }
    const bool neg = e < 0;
    Step s = controller(neg ? -e : e, neg ? -w : w, wmax, acc, dt_ms);
    const std::int64_t mag = neg ? -e : e;
    if (s.d >= mag) {
        s.d = mag;
        s.w = 0;
    }
    e += neg ? s.d : -s.d;
    w = neg ? -s.w : s.w;
    *turned = double(neg ? -s.d : s.d) * core::kRadPerBinary;
}
// axis_controller_sim.run(): one command, settle when the remainder is < 0.5 deg.
struct Oracle {
    long t_ms;
    std::int64_t peak;
};
static Oracle oracle_run(double w_turn_per_s, int accel_pct, double angle_deg, int dt_ms) {
    const std::int64_t wmax = std::llround(w_turn_per_s * 65536);
    const std::int64_t acc = fx(wmax, (0x10000 * accel_pct) / 100);
    std::int64_t e = std::llround(angle_deg / 360 * 65536), w = 0, peak = 0;
    long t_ms = 0;
    while (e > 91 && t_ms < 600000) {
        Step s = controller(e, w, wmax, acc, dt_ms);
        if (e == s.d || e - s.d < 0) {
            s.w = 0;
            s.d = e;
        }
        e -= s.d;
        w = s.w;
        if (w > peak) peak = w;
        t_ms += dt_ms;
    }
    return {t_ms, peak};
}
static void controller_regression() {
    struct Row {
        const char* ship;
        double w;
        int pct;
        double angle;
        const char* settle;
        const char* peak;
    };
    // verification/results/ship-movement/axis_controller_sim.txt (60 fps, dt 16 ms).
    const Row rows[12] = {{"TL", 0.0375, 10, 10, "5.47", "3.0"},    {"TS", 0.058594, 30, 10, "2.54", "6.5"},
                          {"M4", 0.359375, 50, 10, "0.80", "20.5"}, {"TL", 0.0375, 10, 45, "12.91", "6.4"},
                          {"TS", 0.058594, 30, 45, "6.00", "13.7"}, {"M4", 0.359375, 50, 45, "1.87", "44.2"},
                          {"TL", 0.0375, 10, 90, "18.74", "9.0"},   {"TS", 0.058594, 30, 90, "8.72", "19.5"},
                          {"M4", 0.359375, 50, 90, "2.72", "62.7"}, {"TL", 0.0375, 10, 180, "26.96", "12.8"},
                          {"TS", 0.058594, 30, 180, "13.04", "21.1"}, {"M4", 0.359375, 50, 180, "3.92", "88.8"}};
    for (const Row& r : rows) {
        const Oracle o = oracle_run(r.w, r.pct, r.angle, 16);
        char settle[16], peak[16];
        std::snprintf(settle, sizeof settle, "%.2f", o.t_ms / 1000.0);
        std::snprintf(peak, sizeof peak, "%.1f", double(o.peak) / 65536 * 360);
        char why[128];
        std::snprintf(why, sizeof why, "oracle %s %.0f deg: settle %s (expected %s) peak %s (expected %s)", r.ship, r.angle, settle,
                      r.settle, peak, r.peak);
        check(!std::strcmp(settle, r.settle) && !std::strcmp(peak, r.peak), why);
    }
}

// ---- the five ships (design note section 5; k assumed per class) --------------
struct ShipType {
    const char* cls;
    const char* name;
    double v_ms, a_ms2, yaw_dps, r_m;
    int k_pct;
    bool large;
};
static const ShipType kShips[5] = {{"M5", "Discoverer", 532.0, 55.0, 147.66, 9.2, 50, false},
                                   {"M3", "Nova", 247.5, 40.0, 92.81, 11.9, 50, false},
                                   {"M6", "Centaur", 180.0, 20.0, 33.75, 64.4, 30, true},
                                   {"TS", "Mercury", 124.0, 30.0, 21.09, 38.5, 30, true},
                                   {"M2", "Boreas", 70.0, 15.0, 13.5, 705.4, 10, true}};

// ---- the engine model ----------------------------------------------------
struct Lcg { // the think tick's rand(): 0..32767 like the CRT
    std::uint32_t s;
    int next() {
        s = s * 214013u + 2531011u;
        return int((s >> 16) & 0x7fff);
    }
};
struct Model {
    // type
    std::int64_t W[3], acc[3];      // binary/s, binary/s^2
    std::int64_t vmax, accel;       // units/s, units/s^2
    double r_units;
    // state
    double pos[3];                  // units
    double row[3][3];               // basis rows
    std::int64_t speed, desired;    // units/s
    std::int64_t w[3], e[3];        // binary/s, binary
    Lcg rng;
    long t_ms, next_think;
    // the ship's own law state
    core::ShipTable* table;
    core::Dynamics dyn;
    std::uint16_t order;
    double P[3];                    // units
    double d_arr_units;
    bool done;
    double v_cmd_last;              // m/s
    core::Reason last_reason;
    core::Command last_cmd;
    bool vanilla;                   // the engine's own yaw+pitch leaf 0x0044ccc0 instead of the core (the baseline)
    void init(const ShipType& t, std::uint32_t seed) {
        const std::int64_t yaw = std::llround(t.yaw_dps / 360.0 * 65536);
        W[0] = yaw;
        W[1] = yaw;
        W[2] = std::llround(0.8 * double(yaw));
        for (int i = 0; i < 3; ++i) acc[i] = fx(W[i], (0x10000 * t.k_pct) / 100);
        vmax = std::llround(t.v_ms * 500);
        accel = std::llround(t.a_ms2 * 500);
        r_units = t.r_m * 500;
        pos[0] = pos[1] = pos[2] = 0;
        double id[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        std::memcpy(row, id, sizeof row);
        speed = vmax;
        desired = vmax;
        for (int i = 0; i < 3; ++i) w[i] = e[i] = 0;
        rng.s = seed;
        t_ms = 0;
        next_think = 0;
        dyn = core::Dynamics{};
        for (int i = 0; i < 3; ++i) dyn.W[i] = float(W[i]) * core::kRadPerBinary;
        dyn.k = float(t.k_pct) / 100.0f;
        dyn.v_max = float(t.v_ms);
        dyn.a_lin = float(t.a_ms2);
        dyn.r_own = float(t.r_m);
        dyn.large = t.large;
        order = core::kOrderMovePos;
        P[0] = P[1] = P[2] = 0;
        d_arr_units = 0;
        done = false;
        v_cmd_last = 0;
        last_reason = core::Reason::handled;
        last_cmd = core::Command{};
        vanilla = false;
    }
    // The vanilla leaf 0x0044ccc0 (ship-movement.md section 5.1): the 100-unit dead band, yaw = atan2(-t0, t2),
    // pitch = atan2(t1, F') with F' the forward component after the yaw, pitch held at 0 while |yaw| > 90 deg,
    // roll 0; the engine's own speed law only (no site-B clamp).
    void vanilla_leaf() {
        const double d[3] = {P[0] - pos[0], P[1] - pos[1], P[2] - pos[2]};
        if (std::fabs(d[0]) < 100 && std::fabs(d[1]) < 100 && std::fabs(d[2]) < 100) {
            e[0] = e[1] = e[2] = 0;
            return;
        }
        double tt[3];
        for (int r = 0; r < 3; ++r) tt[r] = d[0] * row[r][0] + d[1] * row[r][1] + d[2] * row[r][2];
        const double yaw = std::atan2(-tt[0], tt[2]);
        const double pitch = std::atan2(tt[1], std::sqrt(tt[0] * tt[0] + tt[2] * tt[2]));
        std::int64_t ey = std::llround(yaw * 65536 / (2 * M_PI)), ep = std::llround(pitch * 65536 / (2 * M_PI));
        if (ey > 0x4000 || ey < -0x4000) ep = 0;
        e[0] = ey;
        e[1] = ep;
        e[2] = 0;
    }
    double dist() const {
        const double d[3] = {P[0] - pos[0], P[1] - pos[1], P[2] - pos[2]};
        return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    }
    double bearing_deg() const { // angle between row2 and the target
        const double d = dist();
        if (d < 1) return 0;
        double c = 0;
        for (int i = 0; i < 3; ++i) c += (P[i] - pos[i]) / d * row[2][i];
        if (c > 1) c = 1;
        if (c < -1) c = -1;
        return std::acos(c) * 180 / M_PI;
    }
    // The steering call (site A) through the core; v_ref/arrival per order.
    void steer_call(std::uint32_t obj_id, bool arrival_ramp, double v_ref_ms) {
        if (vanilla) {
            vanilla_leaf();
            return;
        }
        core::Inputs in;
        for (int i = 0; i < 3; ++i) {
            in.x[i] = float(pos[i] / 500.0);
            in.P[i] = float(P[i] / 500.0);
            in.w[i] = float(double(w[i]) * core::kRadPerBinary);
            for (int c = 0; c < 3; ++c) in.row[i][c] = float(row[i][c]);
        }
        in.v = float(double(speed) / 500.0);
        in.order = order;
        in.d_arr = float(d_arr_units / 500.0);
        in.v_ref = float(v_ref_ms);
        in.arrival_ramp = arrival_ramp;
        core::Command cmd;
        last_reason = core::steer(in, dyn, &cmd);
        if (last_reason != core::Reason::handled) return;
        last_cmd = cmd;
        for (int i = 0; i < 3; ++i) e[i] = cmd.e[i];
        core::Ship* s = table->find_or_insert(obj_id, std::uint32_t(t_ms));
        s->stamp_ms = std::uint32_t(t_ms);
        s->order = order;
        s->flags = std::uint16_t((arrival_ramp ? 1 : 0) | (cmd.v_cmd > 0 ? 2 : 0));
        for (int i = 0; i < 3; ++i) s->P[i] = in.P[i];
        s->a_lin = dyn.a_lin;
        s->d_arr = in.d_arr;
        s->v_ref = in.v_ref;
        s->v_corner = cmd.v_corner;
    }
    // Site B: the clamp from the stored decision at the current position.
    void clamp(std::uint32_t obj_id) {
        const core::Ship* s = table->find(obj_id);
        if (!s) return;
        float x[3];
        for (int i = 0; i < 3; ++i) x[i] = float(pos[i] / 500.0);
        const float v = core::clamp_for(*s, x, order, std::uint32_t(t_ms), 250);
        v_cmd_last = v;
        const std::int32_t units = core::clamp_units(v, std::int32_t(desired)); // the DLL's own conversion and compare
        if (units > 0) desired = units;
    }
    static std::int64_t ramp(double d, std::int64_t y0, double x1, std::int64_t y1) { // 0x0042fb20 as a linear map
        if (d <= 0) return y0;
        if (d >= x1) return y1;
        return y0 + std::llround((double(y1 - y0)) * d / x1);
    }
    // One frame of the two passes.
    // Escort (order 0x14) leader state for the think tick (ship-movement.md section 8): the slot behind the
    // leader; beyond 10 x prec the catch-up branch steers at the slot at full speed; inside it and aligned
    // (cos > 0.9) the close-in branch steers at a point 20 r ahead along the leader's heading and matches the
    // leader's speed through a distance ramp (modelled linear over 10 x prec; the point is anchored to the slot
    // here so that a leader on a circle is followed, the operand roles being [i]); otherwise the slot at the
    // leader's speed. Our law sees whichever point the branch hands to the leaf; site B lowers the speed.
    double leader_pos[3], leader_fwd[3], v_lead, prec_units, slot_back_units;
    double slot_error() const {
        double s[3];
        for (int i = 0; i < 3; ++i) s[i] = leader_pos[i] - leader_fwd[i] * slot_back_units - pos[i];
        return std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    }
    void frame(long dt_ms, std::uint32_t obj_id, bool arrival_ramp, double v_ref_ms, int mode) { // 0 fixed, 1 MovePos, 2 escort
        // pass A: the order step at the think tick
        if (!done && t_ms >= next_think) {
            next_think = t_ms + rng.next() * 50 / 32768;
            if (mode == 2) {
                double slot[3];
                for (int i = 0; i < 3; ++i) slot[i] = leader_pos[i] - leader_fwd[i] * slot_back_units;
                const double d_slot = slot_error();
                double along = 0, s_behind = 0;
                for (int i = 0; i < 3; ++i) {
                    along += leader_fwd[i] * row[2][i];
                    s_behind += (slot[i] - pos[i]) * leader_fwd[i];
                }
                v_ref_ms = along > 0 ? v_lead / 500.0 * along : 0;
                if (d_slot > 10 * prec_units) {
                    for (int i = 0; i < 3; ++i) P[i] = slot[i];
                    desired = vmax;
                } else if (along > 0.9) {
                    for (int i = 0; i < 3; ++i) P[i] = slot[i] + leader_fwd[i] * 20 * r_units; // anchored to the slot [i]
                    double f = s_behind / (10 * prec_units);
                    if (f > 1) f = 1;
                    if (f < -1) f = -1;
                    const double v_l = v_lead;
                    desired = std::llround(f >= 0 ? v_l + f * (double(vmax) - v_l) : v_l + f * (v_l - 5000.0));
                    if (desired < 5000) desired = 5000;
                } else {
                    for (int i = 0; i < 3; ++i) P[i] = slot[i];
                    desired = std::llround(v_lead);
                }
                steer_call(obj_id, arrival_ramp, v_ref_ms);
            } else {
                steer_call(obj_id, arrival_ramp, v_ref_ms);
                if (mode == 1) {
                    const double d = dist();
                    const double x1 = double(vmax) * double(vmax) / double(accel);
                    desired = ramp(d, 5000, x1, vmax);
                    if (d <= d_arr_units) {
                        desired = 0;
                        done = true;
                        order = 0;
                    }
                } else
                    desired = vmax;
            }
        }
        // site B, every frame
        if (!done) clamp(obj_id);
        // the slew
        if (desired > vmax) desired = vmax;
        if (speed != desired) {
            std::int64_t step = accel * dt_ms / 1000;
            if (step < 1) step = 1;
            if (speed > desired) speed = (speed - step < desired) ? desired : speed - step;
            else speed = (speed + step > desired) ? desired : speed + step;
        }
        // pass B: rotation (engine signs), then position
        double turned[3];
        for (int i = 0; i < 3; ++i) integrate_axis(e[i], w[i], W[i], acc[i], dt_ms, &turned[i]);
        rotate(turned);
        for (int i = 0; i < 3; ++i) pos[i] += row[2][i] * double(speed) * double(dt_ms) / 1000.0;
        t_ms += dt_ms;
    }
    void rotate(const double a[3]) {
        double r0[3], r1[3], r2[3];
        std::memcpy(r0, row[0], sizeof r0);
        std::memcpy(r1, row[1], sizeof r1);
        std::memcpy(r2, row[2], sizeof r2);
        // yaw: row2 toward -row0
        double c = std::cos(a[0]), s = std::sin(a[0]);
        for (int i = 0; i < 3; ++i) {
            const double n2 = c * r2[i] - s * r0[i], n0 = c * r0[i] + s * r2[i];
            r2[i] = n2;
            r0[i] = n0;
        }
        // pitch: row2 toward +row1
        c = std::cos(a[1]);
        s = std::sin(a[1]);
        for (int i = 0; i < 3; ++i) {
            const double n2 = c * r2[i] + s * r1[i], n1 = c * r1[i] - s * r2[i];
            r2[i] = n2;
            r1[i] = n1;
        }
        // roll: row0 toward row1
        c = std::cos(a[2]);
        s = std::sin(a[2]);
        for (int i = 0; i < 3; ++i) {
            const double n0 = c * r0[i] + s * r1[i], n1 = c * r1[i] - s * r0[i];
            r0[i] = n0;
            r1[i] = n1;
        }
        // re-orthonormalise (the engine does the same after its matrix product)
        auto norm = [](double* v) {
            const double n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            for (int i = 0; i < 3; ++i) v[i] /= n;
        };
        norm(r2);
        double d = r0[0] * r2[0] + r0[1] * r2[1] + r0[2] * r2[2];
        for (int i = 0; i < 3; ++i) r0[i] -= d * r2[i];
        norm(r0);
        d = r1[0] * r2[0] + r1[1] * r2[1] + r1[2] * r2[2];
        double d1 = r1[0] * r0[0] + r1[1] * r0[1] + r1[2] * r0[2];
        for (int i = 0; i < 3; ++i) r1[i] -= d * r2[i] + d1 * r0[i];
        norm(r1);
        std::memcpy(row[0], r0, sizeof r0);
        std::memcpy(row[1], r1, sizeof r1);
        std::memcpy(row[2], r2, sizeof r2);
    }
};
static const int kFps[3] = {30, 60, 144};
static const std::uint32_t kSeeds[3] = {1, 7, 1234567};
// Integer frame times with the fractional carry (the engine's dt is the clock delta in whole ms).
struct Clock {
    double acc = 0;
    long last = 0;
    long step(int fps) {
        acc += 1000.0 / fps;
        const long now = long(acc);
        const long dt = now - last;
        last = now;
        return dt;
    }
};

// ---- scenario 1: the 180 degree turn --------------------------------------
struct TurnResult {
    long settle_ms, oracle_ms, yaw_done, pitch_done;
    double overshoot, peak_rate;
    int sign_changes;
    double min_axis_ratio; // diagonal: min over ticks with bearing > 100 deg of min(|w_p|,|w_y|)/max(...) (both axes engaged)
    long pitch_zero_ticks; // diagonal: ticks with bearing > 100 deg where the written pitch was 0 (vanilla's sequencing)
};
// diagonal: the target 179 degrees behind, offset diagonally in the row0/row1 plane so that the rotation axis
// has equal yaw and pitch components (|n_y| == |n_p|); roll policy on (the design's case).
static TurnResult run_turn(const ShipType& t, int fps, std::uint32_t seed, bool diagonal, bool vanilla) {
    core::ShipTable table{};
    Model m;
    m.init(t, seed);
    m.table = &table;
    m.vanilla = vanilla;
    m.order = core::kOrderTurnTo;
    const double L = 20000.0 * 500;
    if (!diagonal) {
        m.P[2] = -L; // straight behind
    } else {
        const double th = 179.0 * M_PI / 180;
        m.P[0] = L * std::sin(th) / std::sqrt(2.0);
        m.P[1] = L * std::sin(th) / std::sqrt(2.0);
        m.P[2] = L * std::cos(th);
    }
    const double angle = diagonal ? 179.0 : 180.0;
    const Oracle o = oracle_run(t.yaw_dps / 360.0, t.k_pct, angle, 1000 / fps);
    Clock clock;
    TurnResult r{-1, o.t_ms, -1, -1, 0, 0, 0, 1.0, 0};
    int last_sign = 0, yaw_sign0 = 0;
    bool crossed = false;
    const bool trace = std::getenv("X3M_SS_TRACE") && seed == kSeeds[0] && fps == 30 && diagonal && !vanilla;
    long next_trace = 0;
    for (long i = 0; i < 400 * fps; ++i) {
        const long dt = clock.step(fps);
        if (!dt) continue;
        m.frame(dt, 1, false, 0, 0);
        const double b = m.bearing_deg();
        if (trace && m.t_ms >= next_trace) {
            next_trace += 250;
            std::printf("trace %s t=%6ld bearing=%7.3f e=%6lld,%6lld,%6lld w=%6lld,%6lld,%6lld theta=%.2f n=%.3f,%.3f omega=%.3f row1y=%.3f row0y=%.3f\n",
                        t.cls, m.t_ms, b, (long long)m.e[0], (long long)m.e[1], (long long)m.e[2], (long long)m.w[0],
                        (long long)m.w[1], (long long)m.w[2], m.last_cmd.theta * 180 / M_PI, m.last_cmd.n[0], m.last_cmd.n[1],
                        m.last_cmd.omega * 180 / M_PI, m.row[1][1], m.row[0][1]);
        }
        if (r.settle_ms < 0 && b < 0.5) r.settle_ms = m.t_ms;
        // overshoot: the bearing after the nose first crosses the target direction (the yaw component of the
        // target flips sign), measured from then on
        {
            double t0 = 0;
            const double dd = m.dist();
            for (int k = 0; k < 3; ++k) t0 += (m.P[k] - m.pos[k]) / dd * m.row[0][k];
            const int ys = t0 > 1e-9 ? 1 : (t0 < -1e-9 ? -1 : 0);
            if (ys && yaw_sign0 == 0) yaw_sign0 = ys;
            if (ys && yaw_sign0 && ys != yaw_sign0) crossed = true;
            if (crossed && b > r.overshoot) r.overshoot = b;
        }
        // the yaw rate: sign changes (a reversal of the turn direction)
        const std::int64_t wy = m.w[0];
        const int sg = wy > 0 ? 1 : (wy < 0 ? -1 : 0);
        if (sg) {
            if (last_sign && sg != last_sign) ++r.sign_changes;
            last_sign = sg;
        }
        if (double(wy > 0 ? wy : -wy) > r.peak_rate) r.peak_rate = double(wy > 0 ? wy : -wy);
        if (diagonal) { // an axis has finished when its written remainder is under 0.5 deg (91 units)
            if (r.yaw_done < 0 && m.e[0] > -91 && m.e[0] < 91 && m.t_ms > 100) r.yaw_done = m.t_ms;
            if (r.pitch_done < 0 && m.e[1] > -91 && m.e[1] < 91 && m.t_ms > 100) r.pitch_done = m.t_ms;
            if (b > 100.0 && m.t_ms > 500) {
                const double wy = std::fabs(double(m.w[0])), wp = std::fabs(double(m.w[1]));
                const double hi = wy > wp ? wy : wp, lo = wy > wp ? wp : wy;
                if (hi > 0 && lo / hi < r.min_axis_ratio) r.min_axis_ratio = lo / hi;
                if (m.e[1] == 0) ++r.pitch_zero_ticks;
            }
        }
        if (r.settle_ms >= 0 && m.t_ms > r.settle_ms + 5000) break;
    }
    return r;
}
static void turn_scenario(const ShipType& t, int fps, std::uint32_t seed, bool diagonal) {
    const TurnResult v = run_turn(t, fps, seed, diagonal, true);
    const TurnResult r = run_turn(t, fps, seed, diagonal, false);
    const char* tag = diagonal ? "(diag)" : "";
    char why[220];
    std::snprintf(why, sizeof why, "%s %s %d fps seed %u turn%s: settled (vanilla too)", t.cls, t.name, fps, seed, tag);
    check(r.settle_ms >= 0 && v.settle_ms >= 0, why);
    std::snprintf(why, sizeof why, "%s %d fps seed %u turn%s: overshoot %%.3f deg <= 1 (%%.0f)", t.cls, fps, seed, tag);
    checkf(r.overshoot <= 1.0, why, r.overshoot, 1);
    std::snprintf(why, sizeof why, "%s %d fps seed %u turn%s: rate sign changes %%.0f == 0 (%%.0f)", t.cls, fps, seed, tag);
    checkf(r.sign_changes == 0, why, r.sign_changes, 0);
    const double ratio = r.settle_ms >= 0 ? double(r.settle_ms) / double(r.oracle_ms) : 99;
    std::snprintf(why, sizeof why, "%s %d fps seed %u turn%s: settle ratio %%.3f <= 1.15 (oracle %%.0f ms)", t.cls, fps, seed, tag);
    checkf(ratio <= 1.15, why, ratio, double(r.oracle_ms));
    // no worse than the vanilla leaf on the same scenario: settle within one think tick (50 ms) and 2 %, overshoot not above
    std::snprintf(why, sizeof why, "%s %d fps seed %u turn%s: settle %%.0f ms <= vanilla %%.0f ms x 1.02 + 50", t.cls, fps, seed, tag);
    checkf(double(r.settle_ms) <= double(v.settle_ms) * 1.02 + 50, why, double(r.settle_ms), double(v.settle_ms));
    std::snprintf(why, sizeof why, "%s %d fps seed %u turn%s: overshoot past the target %%.3f <= vanilla %%.3f + 0.05", t.cls, fps, seed, tag);
    checkf(r.overshoot <= v.overshoot + 0.05, why, r.overshoot, v.overshoot);
    if (diagonal) {
        // The great-circle law executes both axes in proportion for the whole large-bearing part of the turn
        // (the diagonal axis keeps |w_p| close to |w_y| until the roll policy re-levels the ship), where the
        // vanilla leaf holds the pitch at 0 while |yaw| > 90 deg (yaw-then-pitch sequencing).
        std::snprintf(why, sizeof why, "%s %d fps seed %u turn(diag): both axes engaged while bearing > 100 deg, min |w_p|/|w_y| ratio %%.3f >= 0.6 (%%.1f)", t.cls, fps, seed);
        checkf(r.min_axis_ratio >= 0.6, why, r.min_axis_ratio, 0.6);
        std::snprintf(why, sizeof why, "%s %d fps seed %u turn(diag): vanilla sequencing witnessed: pitch 0 on %%.0f ticks while bearing > 100 deg (ours %%.0f)", t.cls, fps, seed);
        checkf(v.pitch_zero_ticks > 0 && r.pitch_zero_ticks == 0, why, double(v.pitch_zero_ticks), double(r.pitch_zero_ticks));
        const double a = double(r.yaw_done), b = double(r.pitch_done);
        const double rel = (a > 0 && b > 0) ? std::fabs(a - b) / (a > b ? a : b) : 1;
        if (seed == kSeeds[0] && fps == 60)
            std::printf("turn(diag)  %-3s finish yaw=%ld ms pitch=%ld ms (rel %.3f; the roll policy re-splits the axes late in the turn) min_axis_ratio=%.3f vanilla_pitch_zero_ticks=%ld\n",
                        t.cls, r.yaw_done, r.pitch_done, rel, r.min_axis_ratio, v.pitch_zero_ticks);
    }
    if (seed == kSeeds[0] && fps == 60)
        std::printf("turn%-6s %-3s %-10s fps=%3d settle=%6ld ms vanilla=%6ld ms oracle=%6ld ms ratio=%.3f overshoot=%.3f deg (vanilla %.3f) peak=%.1f deg/s\n",
                    tag, t.cls, t.name, fps, r.settle_ms, v.settle_ms, r.oracle_ms, ratio, r.overshoot, v.overshoot,
                    r.peak_rate / 65536 * 360);
}

// ---- scenario 2: arrival at a MovePos point ------------------------------------
struct ArrivalResult {
    bool done;
    int entries, reversals;
    double entry_speed, min_d, beyond;
    long t_entry, t_arrive;
};
static ArrivalResult run_arrival(const ShipType& t, int fps, std::uint32_t seed, bool vanilla) {
    core::ShipTable table{};
    Model m;
    m.init(t, seed);
    m.table = &table;
    m.vanilla = vanilla;
    m.order = core::kOrderMovePos;
    const double L = 20000.0 * 500;
    m.P[0] = L * std::sin(45 * M_PI / 180);
    m.P[2] = L * std::cos(45 * M_PI / 180);
    m.d_arr_units = 2 * m.r_units;
    Clock clock;
    ArrivalResult r{false, 0, 0, -1, 1e18, 0, -1, -1};
    bool inside = false;
    int last_sign = 0;
    long reversal_times[256];
    int reversal_count = 0;
    double approach[3] = {m.P[0] / L, m.P[1] / L, m.P[2] / L};
    for (long i = 0; i < 900 * fps; ++i) {
        const long dt = clock.step(fps);
        if (!dt) continue;
        m.frame(dt, 2, true, 0, 1);
        const double d = m.dist();
        const int sg = m.w[0] > 0 ? 1 : (m.w[0] < 0 ? -1 : 0);
        if (sg && last_sign && sg != last_sign && reversal_count < 256) reversal_times[reversal_count++] = m.t_ms;
        if (sg) last_sign = sg;
        if (d < r.min_d) r.min_d = d;
        const bool in = d <= m.d_arr_units;
        if (in && !inside) {
            ++r.entries;
            if (r.t_entry < 0) {
                r.t_entry = m.t_ms;
                r.entry_speed = double(m.speed) / 500.0;
            }
        }
        inside = in;
        double along = 0;
        for (int k = 0; k < 3; ++k) along += (m.pos[k] - m.P[k]) * approach[k];
        if (along > r.beyond) r.beyond = along;
        if (m.done && r.t_arrive < 0) r.t_arrive = m.t_ms;
        // keep simulating 10 s past the first entry: the engine completes the order at its next think tick
        // (desired 0, the ship stops inside); a second entry or an exit would show orbiting
        if (r.t_entry >= 0 && m.t_ms > r.t_entry + 10000) break;
    }
    r.done = m.done;
    for (int k = 0; k < reversal_count; ++k)
        if (r.t_entry >= 0 && reversal_times[k] >= r.t_entry - 5000 && reversal_times[k] <= r.t_entry) ++r.reversals;
    return r;
}
static void arrival_scenario(const ShipType& t, int fps, std::uint32_t seed) {
    const ArrivalResult v = run_arrival(t, fps, seed, true);
    const ArrivalResult r = run_arrival(t, fps, seed, false);
    const double floor_ms = core::kFloorMs;
    char why[220];
    std::snprintf(why, sizeof why, "%s %d fps seed %u arrival: arrived (vanilla too)", t.cls, fps, seed);
    check(r.done && v.done, why);
    std::snprintf(why, sizeof why, "%s %d fps seed %u arrival: entries %%.0f == 1 over 10 s past the first entry (%%.0f)", t.cls, fps, seed);
    checkf(r.entries == 1, why, r.entries, 1);
    std::snprintf(why, sizeof why, "%s %d fps seed %u arrival: entry speed %%.1f m/s <= floor + 20 (%%.1f)", t.cls, fps, seed);
    checkf(r.entry_speed >= 0 && r.entry_speed <= floor_ms + 20.0, why, r.entry_speed, floor_ms + 20.0);
    std::snprintf(why, sizeof why, "%s %d fps seed %u arrival: beyond P %%.1f m <= d_arr (%%.1f)", t.cls, fps, seed);
    checkf(r.beyond / 500.0 <= 2 * t.r_m, why, r.beyond / 500.0, 2 * t.r_m);
    std::snprintf(why, sizeof why, "%s %d fps seed %u arrival: yaw reversals in the 5 s before entry %%.0f <= 1 (%%.0f)", t.cls, fps, seed);
    checkf(r.reversals <= 1, why, r.reversals, 1);
    // no slower than vanilla beyond the time the arrival ramp spends braking from vanilla's entry speed at a/2
    const double brake_ms = 1000.0 * 2.0 * (v.entry_speed - r.entry_speed) / t.a_ms2;
    std::snprintf(why, sizeof why, "%s %d fps seed %u arrival: time %%.0f ms <= vanilla %%.0f ms + braking + 1000", t.cls, fps, seed);
    checkf(double(r.t_arrive) <= double(v.t_arrive) + brake_ms + 1000, why, double(r.t_arrive), double(v.t_arrive));
    std::snprintf(why, sizeof why, "%s %d fps seed %u arrival: entry speed %%.1f <= vanilla %%.1f + 1", t.cls, fps, seed);
    checkf(r.entry_speed <= v.entry_speed + 1.0, why, r.entry_speed, v.entry_speed);
    if (seed == kSeeds[0] && fps == 60)
        std::printf("arrival     %-3s %-10s fps=%3d t=%6ld ms (vanilla %6ld) entries=%d entry_speed=%.1f m/s (vanilla %.1f) min_d=%.1f m d_arr=%.1f m beyond=%.1f m reversals=%d\n",
                    t.cls, t.name, fps, r.t_arrive, v.t_arrive, r.entries, r.entry_speed, v.entry_speed, r.min_d / 500.0, 2 * t.r_m,
                    r.beyond / 500.0, r.reversals);
}

// ---- scenario 3: a formation slot behind a leader on a 5 km circle ----------
static void formation_scenario(const ShipType& t, int fps, std::uint32_t seed) {
    core::ShipTable table{};
    Model m;
    m.init(t, seed);
    m.table = &table;
    m.order = core::kOrderEscort;
    // The leader flies at 90 % of the follower's full speed: a same-type follower starting behind its slot needs a
    // speed margin to close the gap at all (the section 7.1 table's "at full speed" has no margin for an equal ship).
    // The circle: 5 km, or 40 r for a ship whose close-in lookahead (20 r) would otherwise exceed the circle's radius
    // (the Boreas: 20 r = 14 km; a tangent point that far ahead cannot follow a 5 km circle).
    const double R = std::fmax(5000.0, 40.0 * t.r_m) * 500, v_lead = 0.9 * double(m.vmax), slot_back = 4 * m.r_units;
    const double omega = v_lead / R; // rad/s
    // leader: on the circle; follower starts 2 km behind the slot on the tangent, same heading
    auto leader_at = [&](double t_s, double* p, double* fwd) {
        const double ph = omega * t_s;
        p[0] = R * std::cos(ph);
        p[1] = 0;
        p[2] = R * std::sin(ph);
        fwd[0] = -std::sin(ph);
        fwd[1] = 0;
        fwd[2] = std::cos(ph);
    };
    double lp[3], lf[3];
    leader_at(0, lp, lf);
    for (int i = 0; i < 3; ++i) {
        m.pos[i] = lp[i] - lf[i] * (slot_back + std::fmax(500.0, 3.0 * t.r_m) * 500); // outside 2 r behind the slot, the leader's heading
        m.row[2][i] = lf[i];
        m.leader_pos[i] = lp[i];
        m.leader_fwd[i] = lf[i];
    }
    m.row[0][0] = 1;
    m.row[0][1] = 0;
    m.row[0][2] = 0; // row0 = x (perpendicular to fwd=(0,0,1) at t=0)
    m.row[1][0] = 0;
    m.row[1][1] = 1;
    m.row[1][2] = 0;
    m.d_arr_units = m.r_units;
    m.prec_units = 0.25 * m.r_units; // the script's escort precision [i]: 10 x prec = 2.5 r is the catch-up threshold
    m.slot_back_units = slot_back;
    m.v_lead = v_lead;
    Clock clock;
    const double total_s = 180.0;
    double sum_sq = 0;
    long n_last = 0;
    bool entered = false;
    double window_max = 0, prev_window_max = 1e18;
    long window_start = 0;
    int window_increases = 0;
    double max_err_after = 0;
    for (long i = 0; i < long(total_s) * fps; ++i) {
        const long dt = clock.step(fps);
        if (!dt) continue;
        const double ts = m.t_ms / 1000.0;
        leader_at(ts, m.leader_pos, m.leader_fwd);
        m.frame(dt, 3, true, 0, 2);
        const double err = m.slot_error();
        if (std::getenv("X3M_SS_TRACE") && seed == kSeeds[0] && fps == 60 && (m.t_ms / dt) % (2 * fps) == 0)
            std::printf("ftrace %s t=%6ld err=%8.1f d_P=%8.1f bearing=%6.2f speed=%6.1f desired=%6.1f v_cmd=%6.1f v_lead=%6.1f e=%6lld,%6lld,%6lld\n",
                        t.cls, m.t_ms, err / 500, m.dist() / 500, m.bearing_deg(), m.speed / 500.0, m.desired / 500.0, m.v_cmd_last,
                        v_lead / 500, (long long)m.e[0], (long long)m.e[1], (long long)m.e[2]);
        if (!entered && err <= 4 * m.r_units) {
            entered = true;
            window_start = m.t_ms;
        }
        if (entered) {
            if (err > max_err_after) max_err_after = err;
            if (err > window_max) window_max = err;
            if (m.t_ms - window_start >= 2000) {
                if (window_max > prev_window_max * 1.10 + 0.5 * m.r_units) ++window_increases;
                prev_window_max = window_max;
                window_max = 0;
                window_start = m.t_ms;
            }
        }
        if (ts >= total_s - 20.0) {
            sum_sq += err * err;
            ++n_last;
        }
    }
    const double rms = n_last ? std::sqrt(sum_sq / double(n_last)) / 500.0 : 1e9;
    char why[200];
    std::snprintf(why, sizeof why, "%s %d fps seed %u formation: entered 4r", t.cls, fps, seed);
    check(entered, why);
    std::snprintf(why, sizeof why, "%s %d fps seed %u formation: slot RMS %%.1f m <= 2r (%%.1f)", t.cls, fps, seed);
    checkf(rms <= 2 * t.r_m, why, rms, 2 * t.r_m);
    std::snprintf(why, sizeof why, "%s %d fps seed %u formation: 2 s window maxima non-increasing (increases %%.0f, %%.0f)", t.cls, fps, seed);
    checkf(window_increases == 0, why, window_increases, 0);
    if (seed == kSeeds[0] && fps == 60)
        std::printf("formation   %-3s %-10s fps=%3d slot_rms=%.1f m (2r=%.1f) max_after_entry=%.1f m window_increases=%d\n", t.cls, t.name,
                    fps, rms, 2 * t.r_m, max_err_after / 500.0, window_increases);
}

// ---- scenario 4: degraded records ----------------------------------------------
static void degraded_scenario(const ShipType& t) {
    core::Inputs in;
    in.row[0][0] = 1;
    in.row[1][1] = 1;
    in.row[2][2] = 1;
    in.P[0] = 3000;
    in.P[1] = 2000;
    in.P[2] = 1000;
    in.order = core::kOrderMovePos;
    in.arrival_ramp = true;
    in.d_arr = 20;
    core::Dynamics dyn;
    const float W = float(t.yaw_dps * M_PI / 180);
    dyn.W[0] = W;
    dyn.W[1] = W;
    dyn.W[2] = 0.8f * W;
    dyn.k = float(t.k_pct) / 100.0f;
    dyn.v_max = float(t.v_ms);
    dyn.a_lin = float(t.a_ms2);
    dyn.r_own = float(t.r_m);
    dyn.large = t.large;
    core::Command c;
    char why[160];
    // W_p = 0: the pitch axis is dropped, yaw still carries the turn, no NaN
    core::Dynamics d1 = dyn;
    d1.W[1] = 0;
    core::Reason r = core::steer(in, d1, &c);
    std::snprintf(why, sizeof why, "%s degraded W_p=0: handled, pitch 0, yaw nonzero, finite", t.cls);
    check(r == core::Reason::handled && c.e[1] == 0 && c.e[0] != 0 && core::finite_f(c.v_cmd) && core::finite_f(c.e_rad[0]), why);
    // both yaw and pitch 0: pass through
    core::Dynamics d2 = dyn;
    d2.W[0] = d2.W[1] = 0;
    std::snprintf(why, sizeof why, "%s degraded W_y=W_p=0: no_rotation", t.cls);
    check(core::steer(in, d2, &c) == core::Reason::no_rotation, why);
    // angular acceleration 0: pass through
    core::Dynamics d3 = dyn;
    d3.k = 0;
    std::snprintf(why, sizeof why, "%s degraded k=0: no_angular_accel", t.cls);
    check(core::steer(in, d3, &c) == core::Reason::no_angular_accel, why);
    // v_max = 0 / a = 0: steering runs, the arrival ramp is off (only cornering may clamp), everything finite
    core::Dynamics d4 = dyn;
    d4.v_max = 0;
    d4.a_lin = 0;
    r = core::steer(in, d4, &c);
    std::snprintf(why, sizeof why, "%s degraded v_max=a=0: handled, no clamp at all (section 6.2)", t.cls);
    check(r == core::Reason::handled && c.v_cmd == 0.0f && c.e[0] != 0, why);
    core::Dynamics d4b = dyn;
    d4b.v_max = 0;
    std::snprintf(why, sizeof why, "%s degraded v_max=0 alone: no clamp", t.cls);
    check(core::steer(in, d4b, &c) == core::Reason::handled && c.v_cmd == 0.0f, why);
    // extreme rate (a 720 deg/s record): the written angle never exceeds the geometric remainder
    core::Dynamics d5 = dyn;
    d5.W[0] = d5.W[1] = float(720 * M_PI / 180);
    d5.k = 10;
    r = core::steer(in, d5, &c);
    std::snprintf(why, sizeof why, "%s extreme caps: |e| <= theta", t.cls);
    check(r == core::Reason::handled && std::fabs(c.e_rad[0]) <= c.theta + 1e-4 && std::fabs(c.e_rad[1]) <= c.theta + 1e-4, why);
    // non-finite input: pass through
    core::Inputs bad = in;
    bad.P[0] = __builtin_inff();
    std::snprintf(why, sizeof why, "%s non-finite target: not_finite", t.cls);
    check(core::steer(bad, dyn, &c) == core::Reason::not_finite, why);
}

// ---- unit checks of the pieces -------------------------------------------------
static void unit_checks() {
    // atan2 against the CRT
    double worst = 0;
    for (int i = 0; i < 720; ++i) {
        const double a = (i - 360) * M_PI / 360;
        const float y = float(std::sin(a) * 3), x = float(std::cos(a) * 3);
        const double err = std::fabs(double(core::atan2_f(y, x)) - std::atan2(double(y), double(x)));
        if (err > worst) worst = err;
    }
    checkf(worst < 2e-5, "atan2_f max error %.2e rad < 2e-5 (%.0f)", worst, 0);
    check(core::atan2_f(0, 0) == 0.0f, "atan2_f(0,0) = 0");
    // option parsing
    bool on = true;
    check(core::parse_switch("", 0u, &on) && !on, "switch unset = off");
    check(core::parse_switch("1", 1u, &on) && on, "switch 1 = on");
    check(core::parse_switch("0", 1u, &on) && !on, "switch 0 = off");
    check(!core::parse_switch("on", 2u, &on) && !core::parse_switch("2", 1u, &on), "switch refuses other text");
    std::uint32_t mask = 0;
    check(core::parse_class_mask("M1,M2, TL", 9u, &mask) && mask == ((1u << 3) | (1u << 4) | 1u), "class mask M1,M2,TL");
    check(core::parse_class_mask("tl_p", 4u, &mask) && mask == (1u << 8), "class mask case-insensitive");
    check(core::parse_class_mask("", 0u, &mask) && mask == 0, "class mask empty");
    check(!core::parse_class_mask("M9", 2u, &mask) && !core::parse_class_mask("M1;M2", 5u, &mask), "class mask refuses unknown");
    check(core::kLargeMask == 0xe91d && ((core::kLargeMask >> 11) & 1) && !((core::kLargeMask >> 5) & 1), "large mask M6 in, M3 out");
    // the state table: insertion, lookup, staleness, probe limit
    static core::ShipTable table;
    table = core::ShipTable{};
    core::Ship* a = table.find_or_insert(0x1000, 100);
    check(a && a->obj == 0x1000 && table.find(0x1000) == a, "table insert/find");
    check(!table.find(0x2000), "table miss");
    unsigned inserted = 0;
    for (unsigned i = 0; i < core::kShipCapacity * 2; ++i)
        if (table.find_or_insert(0x10000 + 16 * i, 100)) ++inserted;
    check(inserted >= core::kShipCapacity / 2 && inserted < core::kShipCapacity * 2, "table bounded by the probe limit, no overrun");
    check(table.find_or_insert(0x1000, 100) == a, "existing entry survives the fill");
    const unsigned live_before = table.live(100, 1000);
    core::Ship* b = table.find_or_insert(0x99999990, 100 + core::kStaleMs + 1);
    check(b != nullptr && table.live(100, 1000) <= live_before, "a stale slot is reused");
    // the schedule: floor, ramp, cornering
    check(core::schedule_speed(0, 10, 50, 0, true, 0) == core::kFloorMs, "schedule floor at d <= d_arr");
    checkf(std::fabs(core::schedule_speed(110, 10, 50, 0, true, 0) - std::sqrt(50.0 * 100)) < 0.01, "schedule ramp %.2f (%.2f)",
           core::schedule_speed(110, 10, 50, 0, true, 0), std::sqrt(5000.0));
    check(core::schedule_speed(1000, 0, 50, 0, false, 0) == 0.0f, "no ramp, no corner: no clamp");
    check(core::schedule_speed(1000, 0, 50, 0, false, 40) == 40.0f, "corner only");
    check(core::schedule_speed(1000, 0, 50, 0, false, 3) == core::kFloorMs, "corner floored");
    // the servo: never beyond the geometric remainder, sign follows the rate
    check(core::servo_angle(0.5f, 2.0f, 1.0f) == 0.5f && core::servo_angle(-0.5f, -0.1f, 1.0f) < 0 && core::servo_angle(0.5f, 0, 1) == 0,
          "servo caps and signs");
    // the stuck witness
    core::Ship s{};
    check(!core::stuck_update(s, 1000, 0) && !core::stuck_update(s, 999.5f, 20000) && core::stuck_update(s, 999.5f, 30000) &&
              !core::stuck_update(s, 999.5f, 40000) && !core::stuck_update(s, 10, 41000),
          "stuck after 30 s without 1 m of progress, once per interval, reset on progress");
    // a decision on a plain geometry: a target to +row0 side (engine yaw negative), above (pitch positive)
    core::Inputs in;
    in.row[0][0] = 1;
    in.row[1][1] = 1;
    in.row[2][2] = 1;
    in.P[0] = 1000;
    in.P[1] = 500;
    in.P[2] = 1000;
    in.order = core::kOrderMovePos;
    core::Dynamics dyn;
    dyn.W[0] = dyn.W[1] = 1.0f;
    dyn.W[2] = 0.8f;
    dyn.k = 0.5f;
    dyn.a_lin = 50;
    dyn.v_max = 200;
    dyn.r_own = 10;
    core::Command c;
    check(core::steer(in, dyn, &c) == core::Reason::handled && c.e[0] < 0 && c.e[1] > 0, "engine signs: +row0 -> yaw < 0, +row1 -> pitch > 0");
    check(c.e[2] < 0, "bank: a turn toward +row0 rolls row1 toward row0 (engine roll < 0)");
    dyn.large = true;
    check(core::steer(in, dyn, &c) == core::Reason::handled && c.e[2] == 0, "level policy: a level basis needs no roll");
    // the arrival hold: inside the order's arrival distance (MovePos, d_arr = 40 m) all three angles are 0
    dyn.large = false;
    in.arrival_ramp = true;
    in.d_arr = 40;
    in.P[0] = 20;
    in.P[1] = 5;
    in.P[2] = 30;
    check(core::steer(in, dyn, &c) == core::Reason::handled && c.held && c.e[0] == 0 && c.e[1] == 0 && c.e[2] == 0, "inside d_arr: held, angles 0");
    in.P[0] = 40;
    in.P[2] = 40;
    check(core::steer(in, dyn, &c) == core::Reason::handled && !c.held && c.e[0] != 0, "outside d_arr: steering");
    in.arrival_ramp = false;
    in.d_arr = 0;
    in.P[0] = 0.2f;
    in.P[1] = 0;
    in.P[2] = 0.3f;
    check(core::steer(in, dyn, &c) == core::Reason::handled && c.held && c.e[0] == 0 && c.e[1] == 0, "no arrival distance: held inside 1 m");
    // orders 4 and 5 write roll 0 even from a rolled basis (the docking match needs all three angles small)
    core::Inputs rolled = in;
    rolled.P[0] = 500;
    rolled.P[1] = 200;
    rolled.P[2] = 1000;
    rolled.row[0][0] = 0.7071f;
    rolled.row[0][1] = 0.7071f;
    rolled.row[1][0] = -0.7071f;
    rolled.row[1][1] = 0.7071f;
    for (std::uint16_t order : {core::kOrderFlyBefore, core::kOrderDock}) {
        rolled.order = order;
        check(core::steer(rolled, dyn, &c) == core::Reason::handled && c.e[2] == 0 && c.e[0] != 0, "orders 4/5: roll written 0, heading steered");
    }
    rolled.order = core::kOrderMovePos;
    check(core::steer(rolled, dyn, &c) == core::Reason::handled && c.e[2] != 0, "MovePos from a rolled basis: the roll servo acts");
    // the order policy over the whole order set (section 1.4; amendments 2 and 4)
    {
        const std::uint16_t steer_set[] = {4, 5, 6, 0x10, 0x11, 0x12, 0x14, 0x15, 0x18, 0x19, 0x1d, 0x22, 0x23, 0x24};
        const std::uint16_t ramp_set[] = {4, 0x14, 0x15, 0x19};
        const std::uint16_t roll_zero_set[] = {4, 5};
        const std::uint16_t no_avoid_set[] = {5, 0x12};
        bool ok = true;
        for (unsigned o = 0; o <= 0x30; ++o) {
            auto in_set = [&](const std::uint16_t* set, unsigned n) {
                for (unsigned i = 0; i < n; ++i)
                    if (set[i] == o) return true;
                return false;
            };
            const core::OrderPolicy pol = core::order_policy(std::uint16_t(o));
            const bool steered = in_set(steer_set, 14);
            if (pol.steered != steered) ok = false;
            if (pol.arrival_ramp != in_set(ramp_set, 4)) ok = false;
            if (pol.roll_zero != in_set(roll_zero_set, 2)) ok = false;
            if (pol.avoidance != (steered && !in_set(no_avoid_set, 2))) ok = false;
            if (!steered && (pol.arrival_ramp || pol.roll_zero || pol.avoidance)) ok = false;
        }
        check(ok, "order policy: steer set {4,5,6,0x10,0x11,0x12,0x14,0x15,0x18,0x19,0x1d,0x22,0x23,0x24}; ramp {4,0x14,0x15,0x19} (0x23 none); roll 0 {4,5}; no avoidance {5,0x12}; 8/0x1b/0x1e/0x1f/0x20/0x21 pass through");
    }
    // the speed clamp conversion: lowering only, compared before any conversion, no overflow
    check(core::clamp_units(100.0f, 60000) == 50000 && core::clamp_units(100.0f, 50000) == 0 && core::clamp_units(100.0f, 40000) == 0 &&
              core::clamp_units(0.0f, 60000) == 0 && core::clamp_units(1e7f, 60000) == 0 && core::clamp_units(5.0e6f, 266000) == 0,
          "clamp_units: 100 m/s lowers 60000 -> 50000, never raises, never converts a value beyond 2^31");
    {
        // an M5 at 200 km from its point with sin(theta) just above the cornering floor: v_corner ~ 5e6 m/s
        core::Dynamics m5;
        m5.W[0] = m5.W[1] = float(147.66 * M_PI / 180);
        m5.W[2] = 0.8f * m5.W[0];
        m5.k = 0.5f;
        m5.v_max = 532;
        m5.a_lin = 55;
        m5.r_own = 9.2f;
        core::Inputs far;
        far.row[0][0] = far.row[1][1] = far.row[2][2] = 1;
        far.order = core::kOrderAttack;
        far.P[0] = 200000.0f * 0.0520f;
        far.P[2] = 200000.0f * 0.99865f;
        check(core::steer(far, m5, &c) == core::Reason::handled && c.v_corner > 1.0e6f && core::finite_f(c.v_cmd) &&
                  core::clamp_units(c.v_cmd, 532 * 500) == 0,
              "M5 at 200 km, sin(theta) ~ 0.052: a 5e6 m/s cornering term never lowers the desired speed (no INT_MIN)");
        core::Dynamics fast = m5;
        fast.W[0] = fast.W[1] = float(720 * M_PI / 180);
        far.P[0] = 35000.0f * 0.0520f;
        far.P[2] = 35000.0f * 0.99865f;
        check(core::steer(far, fast, &c) == core::Reason::handled && c.v_corner > 1.0e6f && core::clamp_units(c.v_cmd, 532 * 500) == 0,
              "720 deg/s ship at 35 km: cornering term beyond 2^31 units/s, no write");
        far.P[0] = 300.0f * 0.7071f;
        far.P[2] = 300.0f * 0.7071f;
        far.P[0] = 100.0f * 0.7071f;
        far.P[2] = 100.0f * 0.7071f;
        check(core::steer(far, m5, &c) == core::Reason::handled && c.v_cmd > 0 && core::clamp_units(c.v_cmd, 532 * 500) > 0 &&
                  core::clamp_units(c.v_cmd, 532 * 500) < 532 * 500,
              "M5 at 100 m, 45 deg off: the cornering term (182 m/s) lowers the desired speed");
    }
}

int main() {
    controller_regression();
    unit_checks();
    for (const ShipType& t : kShips) {
        degraded_scenario(t);
        for (int fps : kFps)
            for (std::uint32_t seed : kSeeds) {
                turn_scenario(t, fps, seed, false);
                turn_scenario(t, fps, seed, true);
                arrival_scenario(t, fps, seed);
                formation_scenario(t, fps, seed);
            }
    }
    std::printf("ship_steering_host checks=%u failures=%u\n", checks, failures);
    std::printf(failures ? "checks FAIL\n" : "checks PASS\n");
    return failures ? 1 : 0;
}
