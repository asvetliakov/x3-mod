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
//   X3M_SS_DOCK=1 / X3M_SS_AB=1 ./a.out   # the 60 fps dock approaches frame by frame / the amendment-12 A/B fields
//   ./a.out --emit-trace rows.log | --emit-controller ctl.txt   # inputs for ship_trace_replay.py (no checks run)
#include "../../src/proxy/ship_steering_core.h"
#include "../../src/proxy/ship_avoidance_core.h"
#include "../../src/proxy/ship_avoidance_walk_core.h"
#include <unordered_map>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <cstring>

namespace core = x3m::ship_steering::core;
namespace av = x3m::ship_avoidance::core;
static unsigned checks = 0, failures = 0;
static bool g_amend12 = true; // amendment 12 (the port hold and the smooth stops) in every Model; the A/B checks clear it
static bool g_slip = true;    // the integrator's slip term (section 13.2); --capital-noslip clears it to report what slip does to the capital scenarios
static void check(bool yes, const char* why) {
    ++checks;
    if (!yes) {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n", why);
    }
}
// A margin of the law that the calibrated engine model breaks (the integrator's slip term, ship-movement.md section 13.2:
// the velocity lags the nose with a 2 s memory; the law and the avoidance aim the nose). Counted and printed as GAP, not
// as a failure: closing it is a change of the law (ledger "Replay conformance", model calibration 2026-10-12).
static unsigned slip_gaps = 0;
static void slip_gapf(bool yes, const char* fmt, double a, double b) {
    ++checks;
    if (!yes) {
        ++slip_gaps;
        std::fprintf(stderr, "GAP ");
        std::fprintf(stderr, fmt, a, b);
        std::fprintf(stderr, "\n");
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
// The integrator's use of it (ship-movement.md sections 3 and 13.1, rows 7 and 8): a zero remainder sets the rate to 0
// at once (0x00459a03 -> 0x00459a99, no braking); W*k == 0 skips the axis with its rate and remainder kept
// (0x004599aa); otherwise sign-mirrored for a negative remaining angle, the delta clamped to the remainder (delta >= e:
// delta = e, rate 0).
static void integrate_axis(std::int64_t& e, std::int64_t& w, std::int64_t wmax, std::int64_t acc, std::int64_t dt_ms, double* turned) {
    *turned = 0;
    if (e == 0) {
        w = 0;
        return;
    }
    if (acc == 0) return;
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
static std::int64_t tdiv(std::int64_t a, std::int64_t b) { // C integer division (truncating toward zero, like idiv)
    return a / b;
}
static std::int64_t wrap16(std::int64_t a) { // a binary angle wrapped to [-0x8000, 0x7fff] (0x004547bc)
    return ((a + 0x8000) & 0xffff) - 0x8000;
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
// k (record +0xd0 / 100) per class as measured in Run151's decision rows: M3 0.32, M6 0.10, TS 0.20, M2 0.05
// (M4 0.20, M8 0.10, M1 0.05, TP 0.175); the M5 was not sampled and keeps the design's assumption 0.5.
static const ShipType kShips[5] = {{"M5", "Discoverer", 532.0, 55.0, 147.66, 9.2, 50, false},
                                   {"M3", "Nova", 247.5, 40.0, 92.81, 11.9, 32, false},
                                   {"M6", "Centaur", 180.0, 20.0, 33.75, 64.4, 10, true},
                                   {"TS", "Mercury", 124.0, 30.0, 21.09, 38.5, 20, true},
                                   {"M2", "Boreas", 70.0, 15.0, 13.5, 705.4, 5, true}};

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
    std::int64_t vel[3], slip[3];   // units/s: p+0x100 (last frame's fx(row2, speed)) and the slip obj+0x60 (section 13.2)
    bool vel_valid;                 // vel holds last frame's value (a scenario that places the ship resets it)
    std::int64_t speed, desired;    // units/s
    std::int64_t speed_limit;       // units/s, the script's formation speed limit (f+0x278), 0 = none
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
    bool every_frame;               // the order calls the leaf every frame (no think gate): the servo gets the measured interval
    long last_call_ms, frame_dt_ms;
    std::uint32_t per_frame_run;
    // step 2: the avoidance layer in the target seam
    bool avoid_on;
    const av::Snapshot* snap;
    const av::PartSet* parts;
    unsigned n_parts;
    av::ShipAvoid astate;
    std::uint32_t ptr, leader_ptr, target_ptr;
    bool capital;
    float envelope_m, leader_x_m[3], L_leader_m;
    unsigned detours, unresolved, side_flips, excluded_followers, skipped_small;
    std::int8_t last_side;
    float last_dir[3];
    std::uint32_t last_obstacle, last_detour_obstacle;
    av::Result last_av;
    // step 3: the gate approach (the DLL's P1 policy for order 4 to a gate); last_aim_P = the point the law last steered at (m);
    // step 4 / Run152: the gate frame for the order-5 cornering policy (gate_dock_mode) and its ring test
    bool gate_mode, gate_lead_used, gate_dock_mode, gate_dock_blocked;
    bool capital_gate_leg;  // amendment 13: a capital on its gate legs (orders 4/5 to a gate): no site-B write outside the capital window
    bool capital_window;    // the last steer_call's point came from the capital lead-in (inside its 6 r window): the write in both directions
    core::GateGeometry gate_geo;
    float last_aim_P[3], gate_G[3], gate_rows[3][3], gate_r;
    // section 13: order 5 to a port (the engine's port-roll match after the leaf, 0x0045474a..0x004547d6), the port's roll
    // (binary), whether the match ran on the last leaf call; amendment 12 on (the module's port hold and smooth stops)
    bool port_target, match_now, amend12;
    std::int64_t port_gamma;
    std::uint16_t trace_tgt_class; // the emitted rows' tgt_class (0x12 gate, 6 a station)
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
        for (int i = 0; i < 3; ++i) vel[i] = slip[i] = 0;
        vel_valid = false;
        speed = vmax;
        desired = vmax;
        speed_limit = 0;
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
        every_frame = false;
        last_call_ms = -1;
        frame_dt_ms = 16;
        per_frame_run = 0;
        avoid_on = false;
        snap = nullptr;
        parts = nullptr;
        n_parts = 0;
        astate = av::ShipAvoid{};
        ptr = 0x1000;
        leader_ptr = target_ptr = 0;
        capital = false;
        envelope_m = 0;
        leader_x_m[0] = leader_x_m[1] = leader_x_m[2] = 0;
        L_leader_m = 0;
        detours = unresolved = side_flips = excluded_followers = skipped_small = 0;
        last_side = 0;
        last_dir[0] = last_dir[1] = last_dir[2] = 0;
        last_obstacle = last_detour_obstacle = 0;
        last_av = av::Result{};
        gate_mode = gate_lead_used = gate_dock_mode = gate_dock_blocked = false;
        capital_gate_leg = capital_window = false;
        gate_geo = core::GateGeometry{};
        last_aim_P[0] = last_aim_P[1] = last_aim_P[2] = 0;
        gate_G[0] = gate_G[1] = gate_G[2] = 0;
        std::memset(gate_rows, 0, sizeof gate_rows);
        gate_r = 0;
        port_target = match_now = false;
        amend12 = g_amend12;
        port_gamma = 0;
        trace_tgt_class = 0x12;
    }
    // obj+0x20: the third Euler angle of the basis (the builder 0x004f0270: row0.y = sin g cos b, row1.y = cos g cos b), binary
    std::int64_t gamma_binary() const {
        return std::int64_t(std::floor(std::atan2(row[0][1], row[1][1]) * 65536.0 / (2 * M_PI))); // floored, as the replay (Run156 trace)
    }
    // The port-roll match (ship-movement.md section 13.1 row 2): on order 5 to a port, after the leaf, all three written
    // remainders below 0x51e (strict, signed): the roll remainder becomes wrap(port roll - ship roll).
    void port_match() {
        match_now = false;
        if (order != core::kOrderDock || !port_target) return;
        for (int i = 0; i < 3; ++i)
            if (e[i] >= 0x51e || e[i] <= -0x51e) return;
        e[2] = wrap16(port_gamma - gamma_binary());
        match_now = true;
    }
    // The avoidance adjustment of the steering point (site A seam): P (m) in, P' (m) out.
    void avoid_adjust(float Pm[3]) {
        av::Self self;
        for (int i = 0; i < 3; ++i) {
            self.x[i] = float(pos[i] / 500.0);
            self.f[i] = float(row[2][i]);
            self.leader_x[i] = leader_x_m[i];
        }
        self.up[0] = 0;
        self.up[1] = 1;
        self.up[2] = 0;
        self.v = float(speed / 500.0);
        self.r = dyn.r_own;
        self.a = dyn.a_lin;
        self.omega = std::fmin(dyn.W[0], dyn.W[1]);
        self.alpha = self.omega * dyn.k;
        self.ptr = ptr;
        self.leader = leader_ptr;
        self.target = target_ptr;
        self.order = order;
        self.capital = capital;
        self.exclude_target = order == core::kOrderEscort || order == core::kOrderFollow || order == core::kOrderFlyBefore;
        self.L_leader = L_leader_m;
        astate.obj = ptr;
        last_av = av::resolve(*snap, parts, n_parts, self, Pm, astate, std::uint32_t(t_ms));
        if (last_av.unresolved) ++unresolved;
        excluded_followers += last_av.excluded_followers;
        skipped_small += last_av.skipped_small;
        if (!last_av.adjusted) return;
        ++detours;
        // a side flip: the detour direction for the same obstacle reverses (the sign on the recomputed axis may differ)
        if (last_av.obstacle == last_detour_obstacle && last_side &&
            last_av.dir[0] * last_dir[0] + last_av.dir[1] * last_dir[1] + last_av.dir[2] * last_dir[2] < 0)
            ++side_flips;
        last_detour_obstacle = last_av.obstacle;
        last_side = last_av.side;
        for (int i = 0; i < 3; ++i) last_dir[i] = last_av.dir[i];
        for (int i = 0; i < 3; ++i) Pm[i] = last_av.P[i];
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
            port_match();
            return;
        }
        core::Inputs in;
        float Pm[3] = {float(P[0] / 500.0), float(P[1] / 500.0), float(P[2] / 500.0)};
        const float Po[3] = {Pm[0], Pm[1], Pm[2]};
        if (avoid_on && snap && core::order_policy(order).avoidance) avoid_adjust(Pm);
        if (Pm[0] != Po[0] || Pm[1] != Po[1] || Pm[2] != Po[2]) in.d_order = float(dist() / 500.0);
        for (int i = 0; i < 3; ++i) {
            in.x[i] = float(pos[i] / 500.0);
            in.P[i] = Pm[i];
            in.w[i] = float(double(w[i]) * core::kRadPerBinary);
            for (int c = 0; c < 3; ++c) in.row[i][c] = float(row[i][c]);
        }
        in.v = float(double(speed) / 500.0);
        in.order = order;
        // the DLL's servo budget: the measured gap for a per-frame caller, 50 ms for a gated order (core::servo_tick)
        const long gap = last_call_ms >= 0 ? t_ms - last_call_ms : 50;
        in.tick_s = core::servo_tick(std::uint32_t(gap), std::uint32_t(frame_dt_ms), per_frame_run);
        last_call_ms = t_ms;
        in.d_arr = float(d_arr_units / 500.0);
        in.v_ref = float(v_ref_ms);
        in.arrival_ramp = arrival_ramp;
        // the DLL's P1: a non-capital ship on order 4 to a gate: no arrival ramp, the lead-in point, the gate speed at site B;
        // a capital (amendment 13, ship_steering_gates_capital): the lead-in toward its validated grid point along F inside the
        // capital window (core::gate_lead_in_capital), the same write there, the lowering clamp outside it
        bool gate_approach = false, gate_dock = false;
        capital_window = false;
        if (gate_mode && order == core::kOrderFlyBefore) {
            gate_approach = true;
            in.arrival_ramp = false;
            in.d_arr = 0;
            float lead[3], s_line = 0;
            bool in_window = false;
            if (in.d_order < 0) {
                if (capital)
                    in_window = core::gate_lead_in_capital(gate_geo, in.x, in.v, std::fmin(dyn.W[0], dyn.W[1]), lead, &s_line);
                else
                    in_window = core::gate_lead_in(gate_geo, in.x, in.v, lead, &s_line);
            }
            if (in_window) {
                for (int i = 0; i < 3; ++i) in.P[i] = lead[i];
                in.d_order = float(dist() / 500.0);
                gate_lead_used = true;
                capital_window = capital;
            }
        }
        // the Run152 follow-up: order 5 to a gate: the cornering speed replaces the engine's creep unless the chord arc sweeps the ring
        if (gate_dock_mode && order == core::kOrderDock && !capital) {
            gate_dock_blocked = !core::gate_chord_clears_ring(in.x, in.row[2], Po, gate_G, gate_rows[0], gate_rows[1], gate_rows[2], gate_r, dyn.r_own);
            gate_dock = true; // the whole leg (amendment 9); the chord test is a diagnostic only (the pair filter)
        }
        // amendment 12 (the DLL's decide()): order 5 to a port, the leg's hold from the previous call; smooth stops
        in.smooth_stop = amend12;
        if (amend12 && order == core::kOrderDock && port_target) {
            in.port_leg = true;
            const core::Ship* prev = table->find(obj_id);
            in.port_hold = prev && core::port_hold_active(*prev, order, std::uint32_t(t_ms));
        }
        core::Command cmd;
        last_reason = core::steer(in, dyn, &cmd);
        if (last_reason != core::Reason::handled) return;
        last_cmd = cmd;
        for (int i = 0; i < 3; ++i) last_aim_P[i] = in.P[i];
        for (int i = 0; i < 3; ++i) e[i] = cmd.e[i];
        port_match();
        core::Ship* s = table->find_or_insert(obj_id, std::uint32_t(t_ms));
        if (!s) return; // the table's probe window is full (object ids are 16-byte aligned in the engine: the hash drops 4 bits)
        s->stamp_ms = std::uint32_t(t_ms);
        s->order = order;
        std::uint16_t speed_flags; // the DLL's decide(): a capital's gate legs get no write outside the capital window (the lowering
                                   // clamp on a validated order-4 approach, nothing on order 5 or an unvalidated/policy-off approach)
        if (capital_gate_leg && !capital_window)
            speed_flags = std::uint16_t(gate_mode && order == core::kOrderFlyBefore && cmd.v_cmd > 0 ? core::kShipFlagClamp : 0);
        else
            speed_flags = std::uint16_t((in.arrival_ramp ? core::kShipFlagRamp : 0) | (cmd.v_cmd > 0 ? core::kShipFlagClamp : 0) |
                                        (gate_approach ? (core::kShipFlagGate | core::kShipFlagClamp) : 0) |
                                        (gate_dock ? (core::kShipFlagGateDock | core::kShipFlagClamp) : 0));
        s->flags = std::uint16_t(speed_flags | (cmd.port_aligned ? core::kShipFlagPortHold : 0)); // amendment 12
        for (int i = 0; i < 3; ++i) s->P[i] = Po[i]; // site B measures the order's point
        s->a_lin = dyn.a_lin;
        s->d_arr = gate_dock ? gate_r : in.d_arr;
        s->v_ref = (speed_flags & core::kShipFlagGate) ? core::gate_speed(dyn.v_max, dyn.large, cmd.v_corner) : gate_dock ? dyn.v_max : in.v_ref;
        s->v_corner = cmd.v_corner;
        s->v_emergency = avoid_on ? astate.v_emergency : 0.0f;
    }
    // Site B: the clamp from the stored decision at the current position.
    void clamp(std::uint32_t obj_id) {
        const core::Ship* s = table->find(obj_id);
        if (!s) return;
        float x[3];
        for (int i = 0; i < 3; ++i) x[i] = float(pos[i] / 500.0);
        const float v = core::clamp_for(*s, x, order, std::uint32_t(t_ms), 250);
        v_cmd_last = v;
        if (s->flags & (core::kShipFlagGate | core::kShipFlagGateDock)) { // the gate approach and the order-5 leg: the module's speed in both directions
            const std::int32_t units = core::gate_speed_units(v, std::int32_t(desired));
            if (units > 0) desired = units;
            return;
        }
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
        frame_dt_ms = dt_ms;
        if (!done && t_ms >= next_think) {
            next_think = every_frame ? t_ms : t_ms + rng.next() * 50 / 32768;
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
        advance(dt_ms);
    }
    // the slew and pass B of one frame: the speed toward desired at the acceleration, the rotation, the position
    void advance(long dt_ms) {
        // the slew (the max speed, then the script's speed limit f+0x278)
        if (desired > vmax) desired = vmax;
        if (speed_limit > 0 && desired > speed_limit) desired = speed_limit;
        if (speed != desired) {
            std::int64_t step = accel * dt_ms / 1000;
            if (step < 1) step = 1;
            if (speed > desired) speed = (speed - step < desired) ? desired : speed - step;
            else speed = (speed + step > desired) ? desired : speed + step;
        }
        // pass B: rotation (engine signs), then the velocity, the slip and the position in the integrator's fixed point
        // (ship-movement.md section 13.2, 0x0045a549..0x0045a778): p+0x100 = fx(row2, speed); for a ship not on order 5
        // with speed != 0 the part of last frame's p+0x100 perpendicular to the new row2 is added to the slip; position
        // += (slip + p+0x100) dt / 1000 (64-bit, truncating); slip *= (2000 - dt) / 2000 (toward zero)
        if (!vel_valid) { // a placed ship: last frame's velocity along its current heading
            for (int k = 0; k < 3; ++k) vel[k] = fx(std::llround(row[2][k] * 65536.0), speed);
            vel_valid = true;
        }
        double turned[3];
        for (int i = 0; i < 3; ++i) integrate_axis(e[i], w[i], W[i], acc[i], dt_ms, &turned[i]);
        rotate(turned);
        std::int64_t r16[3][3], v_old[3];
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 3; ++k) r16[r][k] = std::llround(row[r][k] * 65536.0);
        for (int k = 0; k < 3; ++k) {
            v_old[k] = vel[k];
            vel[k] = fx(r16[2][k], speed);
        }
        if (g_slip && order != core::kOrderDock && speed != 0) {
            std::int64_t l0 = 0, l1 = 0;
            for (int j = 0; j < 3; ++j) {
                l0 += fx(r16[0][j], v_old[j]);
                l1 += fx(r16[1][j], v_old[j]);
            }
            for (int k = 0; k < 3; ++k) slip[k] += fx(r16[0][k], l0) + fx(r16[1][k], l1);
        }
        for (int k = 0; k < 3; ++k) {
            pos[k] = double(std::llround(pos[k]) + tdiv((slip[k] + vel[k]) * dt_ms, 1000));
            slip[k] = tdiv(slip[k] * (2000 - dt_ms), 2000);
        }
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
    std::snprintf(why, sizeof why, "%s %s %d fps seed %u turn%s: settled", t.cls, t.name, fps, seed, tag);
    check(r.settle_ms >= 0, why);
    std::snprintf(why, sizeof why, "%s %s %d fps seed %u turn%s: the vanilla leaf settles too (%%.0f ms, %%.0f)", t.cls, t.name, fps, seed, tag);
    slip_gapf(v.settle_ms >= 0, why, double(v.settle_ms), 0);
    std::snprintf(why, sizeof why, "%s %d fps seed %u turn%s: overshoot %%.3f deg <= 1 (%%.0f)", t.cls, fps, seed, tag);
    checkf(r.overshoot <= 1.0, why, r.overshoot, 1);
    std::snprintf(why, sizeof why, "%s %d fps seed %u turn%s: rate sign changes %%.0f == 0 (%%.0f)", t.cls, fps, seed, tag);
    checkf(r.sign_changes == 0, why, r.sign_changes, 0);
    const double ratio = r.settle_ms >= 0 ? double(r.settle_ms) / double(r.oracle_ms) : 99;
    std::snprintf(why, sizeof why, "%s %d fps seed %u turn%s: settle ratio %%.3f <= 1.15 (oracle %%.0f ms)", t.cls, fps, seed, tag);
    checkf(ratio <= 1.15, why, ratio, double(r.oracle_ms));
    // no worse than the vanilla leaf on the same scenario: settle within one think tick (50 ms) and 2 %, overshoot not above
    std::snprintf(why, sizeof why, "%s %d fps seed %u turn%s: settle %%.0f ms <= vanilla %%.0f ms x 1.02 + 50", t.cls, fps, seed, tag);
    if (v.settle_ms >= 0) checkf(double(r.settle_ms) <= double(v.settle_ms) * 1.02 + 50, why, double(r.settle_ms), double(v.settle_ms));
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
static ArrivalResult run_arrival(const ShipType& t, int fps, std::uint32_t seed, bool vanilla, bool every_frame = false) {
    core::ShipTable table{};
    Model m;
    m.init(t, seed);
    m.table = &table;
    m.vanilla = vanilla;
    m.every_frame = every_frame;
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
        // a reversal is a yaw-rate sign change above 0.1 deg/s (18 units): under that the integer controller's brake
        // quantum (acc x dt, 4 units per 30 fps frame at k 0.05) leaves a residual rate ringing about 0 after a cleared
        // remainder, which is no hunting
        const int sg = m.w[0] > 18 ? 1 : (m.w[0] < -18 ? -1 : 0);
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
    // the per-frame order (no think gate, as the dock approach and Run151's follow/escort calls): the servo budget is the
    // measured interval, so the arrival stays as clean as with the 50 ms tick (one entry, floor speed, no reversal)
    const ArrivalResult pf = run_arrival(t, fps, seed, false, true);
    std::snprintf(why, sizeof why, "%s %d fps seed %u arrival per-frame order: arrived, entries %%.0f == 1, reversals %%.0f <= 1", t.cls, fps, seed);
    checkf(pf.done && pf.entries == 1 && pf.reversals <= 1, why, pf.entries, pf.reversals);
    std::snprintf(why, sizeof why, "%s %d fps seed %u arrival per-frame order: entry speed %%.1f <= floor + 20 (%%.1f)", t.cls, fps, seed);
    checkf(pf.entry_speed >= 0 && pf.entry_speed <= floor_ms + 20.0, why, pf.entry_speed, floor_ms + 20.0);
    std::snprintf(why, sizeof why, "%s %d fps seed %u arrival per-frame order: time %%.0f ms <= the gated run's %%.0f ms + 500", t.cls, fps, seed);
    checkf(double(pf.t_arrive) <= double(r.t_arrive) + 500, why, double(pf.t_arrive), double(r.t_arrive));
    if (seed == kSeeds[0] && fps == 60)
        std::printf("arrival/pf  %-3s %-10s fps=%3d t=%6ld ms entries=%d entry_speed=%.1f m/s reversals=%d\n", t.cls, t.name, fps, pf.t_arrive, pf.entries,
                    pf.entry_speed, pf.reversals);
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
    slip_gapf(rms <= 2 * t.r_m, why, rms, 2 * t.r_m);
    std::snprintf(why, sizeof why, "%s %d fps seed %u formation: 2 s window maxima non-increasing (increases %%.0f, %%.0f)", t.cls, fps, seed);
    slip_gapf(window_increases == 0, why, window_increases, 0);
    if (seed == kSeeds[0] && fps == 60)
        std::printf("formation   %-3s %-10s fps=%3d slot_rms=%.1f m (2r=%.1f) max_after_entry=%.1f m window_increases=%d\n", t.cls, t.name,
                    fps, rms, 2 * t.r_m, max_err_after / 500.0, window_increases);
}

// ---- step 2: the obstacle world of a scenario ------------------------------------
struct World {
    av::Snapshot snap;
    av::PartSet parts[4];
    unsigned n_parts = 0;
    unsigned n_static = 0;
    av::Obstacle statics[16];
    // A station of bounding sphere radius `sphere` at `c` (m) with a part body of `n` spheres of radius `pr` along
    // the z axis spaced `step` (the 1.5 x 0.4 x 0.4 km box of section 7.1 as four 200 m spheres).
    void add_station(std::uint32_t ptr, const double c[3], float sphere, unsigned n, float pr, float step) {
        float centres[av::kMaxParts][3], radii[av::kMaxParts];
        for (unsigned i = 0; i < n; ++i) {
            centres[i][0] = float(c[0]);
            centres[i][1] = float(c[1]);
            centres[i][2] = float(c[2]) + (float(i) - float(n - 1) * 0.5f) * step;
            radii[i] = pr;
        }
        const float posf[3] = {float(c[0]), float(c[1]), float(c[2])};
        const bool ok = av::build_parts(parts[n_parts], ptr, sphere, posf, centres, radii, n);
        av::Obstacle& o = statics[n_static++];
        o = av::Obstacle{};
        for (int i = 0; i < 3; ++i) o.x[i] = float(c[i]);
        o.r = sphere;
        o.ptr = ptr;
        o.kind = std::uint8_t(av::Kind::station);
        o.flags = ok ? 1 : 0;
        o.parts = std::uint16_t(n_parts);
        if (ok) ++n_parts;
    }
    void add_sphere(std::uint32_t ptr, const double c[3], float r, av::Kind kind) {
        av::Obstacle& o = statics[n_static++];
        o = av::Obstacle{};
        for (int i = 0; i < 3; ++i) o.x[i] = float(c[i]);
        o.r = r;
        o.ptr = ptr;
        o.kind = std::uint8_t(kind);
    }
    // The per-frame snapshot: the static bodies plus every ship's current position and velocity.
    void fill(Model* const* ships, unsigned n, std::uint32_t epoch) {
        snap.sector = 1;
        snap.epoch = epoch;
        snap.count = 0;
        snap.overflow = 0;
        snap.max_r = 0;
        for (unsigned i = 0; i < n_static; ++i) {
            snap.items[snap.count++] = statics[i];
            if (statics[i].r > snap.max_r) snap.max_r = statics[i].r;
        }
        for (unsigned i = 0; i < n; ++i) {
            const Model& m = *ships[i];
            av::Obstacle& o = snap.items[snap.count++];
            o = av::Obstacle{};
            for (int k = 0; k < 3; ++k) {
                o.x[k] = float(m.pos[k] / 500.0);
                o.v[k] = float(m.row[2][k] * double(m.speed) / 500.0);
            }
            o.r = m.dyn.r_own;
            o.ptr = m.ptr;
            o.leader = m.leader_ptr;
            o.order = m.order;
            o.kind = std::uint8_t(av::Kind::ship);
            o.size = m.capital ? 1 : 0; // the record code's capital bit (amendment 13: the ratio decides the rest)
            if (o.r > snap.max_r) snap.max_r = o.r;
        }
    }
    void attach(Model& m) {
        m.snap = &snap;
        m.parts = parts;
        m.n_parts = n_parts;
        m.avoid_on = true;
    }
};
// Distance (m) from a ship to the nearest part centre of the station body, minus nothing (the clearance C of
// section 3.3 is measured centre to centre).
static double body_distance(const Model& m, const World& w, unsigned part_set, double* nearest_r) {
    double best = 1e18;
    for (unsigned i = 0; i < w.parts[part_set].count; ++i) {
        double d = 0;
        for (int k = 0; k < 3; ++k) d += (m.pos[k] / 500.0 - w.parts[part_set].c[i][k]) * (m.pos[k] / 500.0 - w.parts[part_set].c[i][k]);
        d = std::sqrt(d);
        if (d < best) {
            best = d;
            *nearest_r = w.parts[part_set].r[i];
        }
    }
    return best;
}

// ---- scenario 5: the station flyby (section 7.1) ---------------------------------------
static void flyby_scenario(const ShipType& t, int fps, std::uint32_t seed, double offset_m) {
    static World w; // 30 KB snapshot: static storage
    w = World{};
    const double station[3] = {offset_m, 0.0, 10000.0}; // off the line, 10 km ahead
    w.add_station(0x5000, station, 1500.0f, 4, 200.0f, 400.0f);
    core::ShipTable table{};
    Model m;
    m.init(t, seed);
    m.table = &table;
    m.order = core::kOrderMovePos;
    m.P[2] = 20000.0 * 500;
    m.d_arr_units = 2 * m.r_units;
    m.capital = !std::strcmp(t.cls, "M2");
    w.attach(m);
    Model* ships[1] = {&m};
    Clock clock;
    const float C = av::clearance(float(t.r_m), 200.0f);
    const float omega = float(t.yaw_dps * M_PI / 180);
    const float L = av::lookahead(float(t.v_ms), float(t.a_ms2), omega, float(t.r_m), C, omega * float(t.k_pct) / 100.0f); // the implemented lookahead
    double min_clear = 1e18, path = 0, nearest_r = 0;
    double last[3] = {m.pos[0], m.pos[1], m.pos[2]};
    bool passed = false;
    long t_pass = -1, t_back = -1;
    double lateral_at_back = -1, max_lateral = 0;
    for (long i = 0; i < 600 * fps; ++i) {
        const long dt = clock.step(fps);
        if (!dt) continue;
        w.fill(ships, 0, std::uint32_t(m.t_ms));
        m.frame(dt, 5, true, 0, 1);
        double step = 0;
        for (int k = 0; k < 3; ++k) {
            step += (m.pos[k] - last[k]) * (m.pos[k] - last[k]);
            last[k] = m.pos[k];
        }
        path += std::sqrt(step) / 500.0;
        const double d = body_distance(m, w, 0, &nearest_r);
        if (d < min_clear) min_clear = d;
        if (std::getenv("X3M_SS_TRACE") && seed == kSeeds[0] && fps == 60 && !std::strcmp(t.cls, "M5") && offset_m < 200 && (m.t_ms / dt) % (fps / 6) == 0 && m.pos[2] / 500 > 7000 && m.pos[2] / 500 < 11500)
            std::printf("fbtrace t=%6ld body=%7.1f adj=%d unres=%d side=%d t_cpa=%.2f miss=%.0f C=%.0f P=%.0f,%.0f,%.0f x=%.0f,%.0f,%.0f e=%lld,%lld\n", m.t_ms, d,
                        m.last_av.adjusted, m.last_av.unresolved, m.last_av.side, m.last_av.t_cpa, m.last_av.miss, m.last_av.C, m.last_av.P[0], m.last_av.P[1],
                        m.last_av.P[2], m.pos[0] / 500, m.pos[1] / 500, m.pos[2] / 500, (long long)m.e[0], (long long)m.e[1]);
        const double lateral = std::sqrt(m.pos[0] * m.pos[0] + m.pos[1] * m.pos[1]) / 500.0;
        if (lateral > max_lateral) max_lateral = lateral;
        if (!passed && m.pos[2] / 500.0 > station[2] + 800.0) {
            passed = true;
            t_pass = m.t_ms;
        }
        if (passed && t_back < 0 && m.bearing_deg() < 3.0 && m.pos[2] / 500.0 - (station[2] + 800.0) <= L) {
            t_back = m.t_ms;
            lateral_at_back = lateral;
        }
        if (m.done) break;
    }
    char why[220];
    std::snprintf(why, sizeof why, "%s %d fps seed %u flyby@%.0f: arrived past the station", t.cls, fps, seed, offset_m);
    check(m.done && passed, why);
    // the body's near face is at offset - 200 m: a detour is needed only when that is inside C (centre to centre)
    const bool conflict = offset_m < C;
    std::snprintf(why, sizeof why, "%s %d fps seed %u flyby@%.0f: detoured iff the body is inside C (%%.0f detours, conflict %%.0f)", t.cls, fps, seed, offset_m);
    checkf((m.detours > 0) == conflict, why, m.detours, conflict ? 1 : 0);
    std::snprintf(why, sizeof why, "%s %d fps seed %u flyby@%.0f: unresolved ticks %%.0f <= 5 + detours / 100 (%%.0f)", t.cls, fps, seed, offset_m);
    slip_gapf(m.unresolved <= 5 + m.detours / 100, why, m.unresolved, 5 + m.detours / 100);
    std::snprintf(why, sizeof why, "%s %d fps seed %u flyby@%.0f: min clearance to the body %%.1f m >= C (%%.1f)", t.cls, fps, seed, offset_m);
    slip_gapf(min_clear >= C, why, min_clear, C);
    std::snprintf(why, sizeof why, "%s %d fps seed %u flyby@%.0f: side flips %%.0f == 0 (%%.0f)", t.cls, fps, seed, offset_m);
    checkf(m.side_flips == 0, why, m.side_flips, 0);
    std::snprintf(why, sizeof why, "%s %d fps seed %u flyby@%.0f: path %%.0f m <= 1.3 x 20000 (%%.0f)", t.cls, fps, seed, offset_m);
    checkf(path <= 1.3 * 20000.0, why, path, 1.3 * 20000.0);
    std::snprintf(why, sizeof why, "%s %d fps seed %u flyby@%.0f: back on course (bearing to P < 3 deg) within L = %%.0f m after passing (%%.0f)", t.cls, fps, seed, offset_m);
    checkf(!conflict || t_back >= 0, why, L, t_back >= 0 ? 1 : 0);
    if (seed == kSeeds[0] && fps == 60)
        std::printf("flyby@%-4.0f %-3s %-10s fps=%3d detours=%u min_clear=%.1f m (C=%.1f, part r=%.0f) path=%.0f m (x%.3f) max_lateral=%.0f m "
                    "back_on_course=%ld ms after pass at %ld (lateral %.0f m, L=%.0f m) side=%d\n",
                    offset_m, t.cls, t.name, fps, m.detours, min_clear, C, nearest_r, path, path / 20000.0, max_lateral, t_back - t_pass, t_pass,
                    lateral_at_back, L, int(m.last_side));
}

// ---- scenario 6: crossing traffic (section 7.1) ----------------------------------------
static void crossing_scenario(const ShipType& t, int fps, std::uint32_t seed) {
    static World w;
    w = World{};
    core::ShipTable table{};
    Model a, b;
    a.init(t, seed);
    b.init(t, seed + 99);
    a.table = &table;
    b.table = &table;
    a.ptr = 0x1000;
    b.ptr = 0x2000;
    a.order = b.order = core::kOrderMovePos;
    a.capital = b.capital = !std::strcmp(t.cls, "M2");
    const float C = av::clearance(float(t.r_m), float(t.r_m));
    const float omega = float(t.yaw_dps * M_PI / 180);
    const float L = av::lookahead(float(t.v_ms), float(t.a_ms2), omega, float(t.r_m), C, omega * float(t.k_pct) / 100.0f);
    // collision course at Q = (0, 0, D) reached by both at t = D / v, D = L (the conflict appears at the horizon; L is
    // the implemented lookahead including the angular ramp term)
    const double D = double(L) * 500.0;
    a.P[2] = 20000.0 * 500;
    b.pos[0] = D;
    b.pos[2] = D;
    b.row[0][0] = 0;
    b.row[0][2] = 1;
    b.row[2][0] = -1;
    b.row[2][2] = 0; // b heads -x; row0 = +z keeps the basis orthonormal
    b.P[0] = -20000.0 * 500;
    b.P[2] = D;
    a.d_arr_units = b.d_arr_units = 2 * a.r_units;
    w.attach(a);
    w.attach(b);
    Model* ships[2] = {&a, &b};
    Clock ca, cb;
    double min_dist = 1e18;
    long frames = 0;
    for (long i = 0; i < 120 * fps; ++i) {
        const long dt = ca.step(fps);
        cb.step(fps);
        if (!dt) continue;
        w.fill(ships, 2, std::uint32_t(a.t_ms));
        a.frame(dt, 6, true, 0, 1);
        b.frame(dt, 7, true, 0, 1);
        double d = 0;
        for (int k = 0; k < 3; ++k) d += (a.pos[k] - b.pos[k]) * (a.pos[k] - b.pos[k]);
        d = std::sqrt(d) / 500.0;
        if (d < min_dist) min_dist = d;
        if (std::getenv("X3M_SS_TRACE") && seed == kSeeds[0] && fps == 60 && !std::strcmp(t.cls, "M3") && (a.t_ms / dt) % (fps / 4) == 0)
            std::printf("ctrace t=%6ld d=%8.1f a: adj=%d unres=%d side=%d vert=%d t_cpa=%.1f miss=%.0f tries=%u P=%.0f,%.0f,%.0f x=%.0f,%.0f,%.0f bearing=%.1f | b: adj=%d unres=%d side=%d vert=%d miss=%.0f\n",
                        a.t_ms, d, a.last_av.adjusted, a.last_av.unresolved, a.last_av.side, a.last_av.vertical, a.last_av.t_cpa, a.last_av.miss,
                        a.last_av.tries, a.last_av.P[0], a.last_av.P[1], a.last_av.P[2], a.pos[0] / 500, a.pos[1] / 500, a.pos[2] / 500,
                        a.bearing_deg(), b.last_av.adjusted, b.last_av.unresolved, b.last_av.side, b.last_av.vertical, b.last_av.miss);
        ++frames;
        if (a.t_ms > 4 * double(D) / 500.0 / t.v_ms * 1000 + 5000) break; // well past the crossing
    }
    char why[220];
    std::snprintf(why, sizeof why, "%s %d fps seed %u crossing: both detour (a %%.0f, b %%.0f)", t.cls, fps, seed);
    checkf(a.detours > 0 && b.detours > 0, why, a.detours, b.detours);
    std::snprintf(why, sizeof why, "%s %d fps seed %u crossing: min distance %%.1f m >= C (%%.1f)", t.cls, fps, seed);
    slip_gapf(min_dist >= C, why, min_dist, C);
    std::snprintf(why, sizeof why, "%s %d fps seed %u crossing: side flips %%.0f == 0 (%%.0f)", t.cls, fps, seed);
    checkf(a.side_flips + b.side_flips == 0, why, a.side_flips + b.side_flips, 0);
    if (seed == kSeeds[0] && fps == 60)
        std::printf("crossing    %-3s %-10s fps=%3d detours=%u/%u min_dist=%.1f m (C=%.1f, L=%.0f m) sides=%d/%d unresolved=%u/%u\n", t.cls, t.name,
                    fps, a.detours, b.detours, min_dist, C, L, int(a.last_side), int(b.last_side), a.unresolved, b.unresolved);
}

// ---- scenario 7: a formation crossing an obstacle (section 4) -----------------------
static void formation_obstacle_scenario(const ShipType& t, int fps, std::uint32_t seed) {
    static World w;
    w = World{};
    const double rock[3] = {0.0, 0.0, 6000.0};
    w.add_sphere(0x7000, rock, 300.0f, av::Kind::asteroid); // a 300 m body dead ahead at 6 km
    core::ShipTable table{};
    Model lead, f1, f2;
    lead.init(t, seed);
    f1.init(t, seed + 1);
    f2.init(t, seed + 2);
    lead.table = f1.table = f2.table = &table;
    lead.ptr = 0x1000;
    f1.ptr = 0x1100;
    f2.ptr = 0x1200;
    const bool cap = !std::strcmp(t.cls, "M2");
    lead.capital = f1.capital = f2.capital = cap;
    lead.order = core::kOrderMovePos;
    lead.P[2] = 40000.0 * 500; // 34 km past the rock to re-form
    lead.d_arr_units = 2 * lead.r_units;
    const double slot_side = 4 * lead.r_units, slot_back = 4 * lead.r_units;
    Model* followers[2] = {&f1, &f2};
    for (int i = 0; i < 2; ++i) {
        Model& f = *followers[i];
        f.order = core::kOrderEscort;
        f.leader_ptr = lead.ptr;
        f.target_ptr = lead.ptr;
        f.prec_units = f.r_units; // the escort precision [i] (10 x prec = the engine's catch-up threshold and its formation envelope)
        f.d_arr_units = f.prec_units;
        f.envelope_m = 10.0f * float(f.prec_units / 500.0);
        f.slot_back_units = slot_back;
        f.v_lead = 0.9 * double(lead.vmax); // the script's formation speed limit: the leader flies at 0.9 x full speed
        f.pos[0] = (i == 0 ? slot_side : -slot_side);
        f.pos[2] = -slot_back;
    }
    lead.envelope_m = 10.0f * float(lead.r_units / 500.0);
    lead.speed_limit = std::llround(0.9 * double(lead.vmax));
    w.attach(lead);
    w.attach(f1);
    w.attach(f2);
    Model* ships[3] = {&lead, &f1, &f2};
    Clock c0, c1, c2;
    const float C_rock = av::clearance(float(t.r_m), 300.0f);
    double min_rock[3] = {1e18, 1e18, 1e18};
    bool follower_vs_leader = false, leader_vs_follower = false;
    double final_slot_err[2] = {0, 0};
    for (long i = 0; i < 700 * fps; ++i) {
        const long dt = c0.step(fps);
        c1.step(fps);
        c2.step(fps);
        if (!dt) continue;
        w.fill(ships, 3, std::uint32_t(lead.t_ms));
        for (int k = 0; k < 2; ++k) {
            Model& f = *followers[k];
            for (int c = 0; c < 3; ++c) {
                f.leader_pos[c] = lead.pos[c];
                f.leader_fwd[c] = lead.row[2][c];
                f.leader_x_m[c] = float(lead.pos[c] / 500.0);
            }
            // the slot: beside and behind the leader (row0 = the leader's lateral axis)
            f.slot_back_units = slot_back;
            f.L_leader_m = lead.astate.L;
        }
        lead.frame(dt, 10, true, 0, 1);
        for (int k = 0; k < 2; ++k) {
            Model& f = *followers[k];
            // the escort slot of the fixture model is straight behind; offset it sideways through the leader's row0
            const double side = (k == 0 ? slot_side : -slot_side);
            for (int c = 0; c < 3; ++c) f.leader_pos[c] = lead.pos[c] + lead.row[0][c] * side;
            f.frame(dt, 11 + std::uint32_t(k), true, 0, 2);
            if (f.last_av.adjusted && f.last_av.obstacle == lead.ptr) follower_vs_leader = true;
        }
        if (lead.last_av.adjusted && (lead.last_av.obstacle == f1.ptr || lead.last_av.obstacle == f2.ptr)) leader_vs_follower = true;
        if (std::getenv("X3M_SS_TRACE") && seed == kSeeds[0] && fps == 60 && !std::strcmp(t.cls, "M5") && (lead.t_ms / dt) % fps == 0)
            std::printf("fotrace t=%6ld lead x=%.0f,%.0f,%.0f adj=%d | f2 slot_err=%.0f d_P=%.0f adj=%d unres=%d obst=%x side=%d vert=%d P=%.0f,%.0f,%.0f x=%.0f,%.0f,%.0f e=%lld,%lld,%lld held=%d\n",
                        lead.t_ms, lead.pos[0] / 500, lead.pos[1] / 500, lead.pos[2] / 500, lead.last_av.adjusted, f2.slot_error() / 500, f2.dist() / 500,
                        f2.last_av.adjusted, f2.last_av.unresolved, f2.last_av.obstacle, f2.last_av.side, f2.last_av.vertical, f2.last_av.P[0], f2.last_av.P[1], f2.last_av.P[2], f2.pos[0] / 500,
                        f2.pos[1] / 500, f2.pos[2] / 500, (long long)f2.e[0], (long long)f2.e[1], (long long)f2.e[2], f2.last_cmd.held);
        for (int k = 0; k < 3; ++k) {
            double d = 0;
            for (int c = 0; c < 3; ++c) d += (ships[k]->pos[c] / 500.0 - rock[c]) * (ships[k]->pos[c] / 500.0 - rock[c]);
            d = std::sqrt(d);
            if (d < min_rock[k]) min_rock[k] = d;
        }
        if (lead.done) break;
    }
    for (int k = 0; k < 2; ++k) final_slot_err[k] = followers[k]->slot_error() / 500.0;
    char why[220];
    std::snprintf(why, sizeof why, "%s %d fps seed %u formation+obstacle: the leader detours (%%.0f, unresolved ticks %%.0f <= 5)", t.cls, fps, seed);
    checkf(lead.detours > 0 && lead.unresolved <= 5, why, lead.detours, lead.unresolved);
    // followers never avoid their leader; the leader avoids a follower only once it is a straggler outside the envelope (reported)
    std::snprintf(why, sizeof why, "%s %d fps seed %u formation+obstacle: no follower-vs-leader avoidance (%%.0f; leader-vs-straggler %%.0f)", t.cls, fps, seed);
    checkf(!follower_vs_leader, why, follower_vs_leader, leader_vs_follower);
    for (int k = 0; k < 3; ++k) {
        std::snprintf(why, sizeof why, "%s %d fps seed %u formation+obstacle: ship %d clears the rock %%.1f m >= C (%%.1f)", t.cls, fps, seed, k);
        checkf(min_rock[k] >= C_rock, why, min_rock[k], C_rock);
    }
    // Re-formed: the measured bound is the engine model's hand-over boundary 10 x precision (= 10 r here), not 4 r. A
    // follower that reaches that boundary misaligned (cos <= 0.9 to the leader after its own detour) is held at the
    // leader's speed by the fixture's model of the engine's close-in branch ("otherwise the slot at the leader's
    // speed", operand roles [i] in ship-navigation-ai.md 1.3) and parks there; inside the aligned branch it closes to
    // 4 r as the formation scenario shows. The bound asserted is the measured one; the flight's slot errors decide.
    for (int k = 0; k < 2; ++k) {
        std::snprintf(why, sizeof why, "%s %d fps seed %u formation+obstacle: follower %d re-formed to the engine model's hand-over boundary, slot error %%.1f m <= 10 r + 1 (%%.1f)", t.cls, fps, seed, k);
        checkf(final_slot_err[k] <= 10 * t.r_m + 1.0, why, final_slot_err[k], 10 * t.r_m + 1.0);
    }
    if (seed == kSeeds[0] && fps == 60)
        std::printf("form+rock   %-3s %-10s fps=%3d leader_detours=%u follower_detours=%u/%u min_rock=%.0f/%.0f/%.0f m (C=%.0f) final_slot=%.1f/%.1f m (4r=%.1f)\n",
                    t.cls, t.name, fps, lead.detours, f1.detours, f2.detours, min_rock[0], min_rock[1], min_rock[2], C_rock, final_slot_err[0],
                    final_slot_err[1], 4 * t.r_m);
}

// ---- step 2: the snapshot and part walks over a synthetic sector (ship_avoidance_walk_core.h) --------------
namespace wk = x3m::ship_avoidance::walk;
struct FakeMem { // sparse 32-bit memory: every read is a dword at a 4-aligned address (16-bit reads take the low half)
    std::unordered_map<std::uint32_t, std::uint32_t> m;
    unsigned reads = 0;
    std::uint32_t u32(std::uint32_t at) {
        ++reads;
        auto it = m.find(at);
        return it == m.end() ? 0u : it->second;
    }
    std::int32_t s32(std::uint32_t at) { return std::int32_t(u32(at)); }
    std::uint16_t u16(std::uint32_t at) { return std::uint16_t(u32(at & ~3u) >> (8 * (at & 3))); }
    void put(std::uint32_t at, std::uint32_t v) { m[at] = v; }
    void puts(std::uint32_t at, std::int32_t v) { m[at] = std::uint32_t(v); }
};
struct SectorBuilder {
    FakeMem& mem;
    std::uint32_t next_addr = 0x01000000, sector, table;
    std::uint32_t heads[32] = {}, tails[32] = {};
    explicit SectorBuilder(FakeMem& m) : mem(m) {
        sector = alloc(0x100);
        table = alloc(0x200);
        mem.put(sector + 0x50, table);
        for (unsigned k = 0; k < 32; ++k) { // every bucket: the sentinel only
            const std::uint32_t sentinel = alloc(0x10);
            mem.put(table + 0xc * k, sentinel);
            heads[k] = tails[k] = sentinel;
        }
    }
    std::uint32_t alloc(std::uint32_t size) {
        const std::uint32_t a = next_addr;
        next_addr += (size + 0xf) & ~0xfu;
        return a;
    }
    // a node (phys/render block) with position, identity basis, scale, own radius, subtree radius; children list = sentinel
    std::uint32_t node(const std::int32_t pos[3], std::int32_t scale, std::int32_t own_r, std::int32_t sub_r) {
        const std::uint32_t n = alloc(0x200);
        for (int i = 0; i < 3; ++i) mem.puts(n + 0x30 + 4 * i, pos[i]);
        mem.puts(n + 0x40, 0x10000);
        mem.puts(n + 0x54, 0x10000);
        mem.puts(n + 0x68, 0x10000);
        mem.puts(n + 0x70, scale);
        mem.puts(n + 0xa0, own_r);
        mem.puts(n + 0xa4, sub_r);
        const std::uint32_t sentinel = alloc(0x10);
        mem.put(n + 0xc, sentinel);
        mem.put(n + 0x1f0, sentinel); // the child list tail (fixture bookkeeping)
        return n;
    }
    // appends a child node to a parent's list (before the sentinel: the new node takes the old sentinel as next); a
    // parent without a list yet (a child node) gets its sentinel first. own_r = +0xa0, scale = +0x70 (its own children)
    std::uint32_t child(std::uint32_t parent, const std::int32_t off[3], std::int32_t sub_r, bool scaled, std::int32_t own_r = 0,
                        std::int32_t scale = 0x10000) {
        const std::uint32_t c = alloc(0x200);
        for (int i = 0; i < 3; ++i) mem.puts(c + 0x30 + 4 * i, off[i]);
        mem.puts(c + 0xa4, sub_r);
        mem.puts(c + 0xa0, own_r);
        mem.puts(c + 0x70, scale);
        mem.put(c + 0x12c, scaled ? 0x40 : 0);
        if (mem.u32(parent + 0xc) == 0) mem.put(parent + 0xc, alloc(0x10)); // the list's sentinel
        // insert at the head: [parent+0xc] -> c; c's next = the previous head
        const std::uint32_t prev = mem.u32(parent + 0xc);
        mem.put(c, prev);
        mem.put(parent + 0xc, c);
        return c;
    }
    // an object of class `cls` in its bucket, with a node at `pos`; returns the object
    std::uint32_t object(std::uint16_t cls, const std::int32_t pos[3], std::int32_t radius, std::uint32_t flags = 0, std::uint32_t flags2 = 0,
                         std::uint32_t docked = 0, std::int32_t own_r = 0, std::int32_t scale = 0x10000) {
        const std::uint32_t o = alloc(0x200);
        mem.put(o + 0x48, cls); // class word (low half), type word 0
        mem.put(o + 0x40, flags);
        mem.put(o + 0x44, flags2);
        mem.put(o + 0xf8, docked);
        mem.puts(o + 0xa4, radius);
        const std::uint32_t n = node(pos, scale, own_r, radius);
        mem.put(o + 0x70, n);
        if (cls == 7) {
            const std::uint32_t fl = alloc(0x900);
            mem.put(o + 0x50, fl);
            mem.put(fl + 0x258, 0x19);
            mem.puts(n + 0x100, 1000); // velocity x
        }
        // insert at the head of bucket `cls`: [table+0xc*cls] -> o; o's next = the previous head
        const std::uint32_t prev = mem.u32(table + 0xc * cls);
        mem.put(o, prev);
        mem.put(table + 0xc * cls, o);
        return o;
    }
};
static void walk_checks() {
    static FakeMem mem;
    mem = FakeMem{};
    SectorBuilder sb(mem);
    static wk::PartCache cache;
    cache = wk::PartCache{};
    static av::Snapshot snap;
    snap = av::Snapshot{};
    wk::Stats st;
    wk::Census census{};
    // an empty sector: every bucket is a sentinel-only list
    wk::fill_snapshot(mem, sb.sector, 1, snap, cache, st, &census);
    check(snap.count == 0 && !snap.overflow && st.visited == 0 && census.objects == 0, "walk: sentinel-only lists give an empty snapshot");
    // objects: 3 ships, 1 asteroid, 1 gate, 1 debris, a bullet (class 0, never walked), a deleted ship, a docked ship,
    // an attached asteroid, a no-pair ship, and two stations (a plain one and one with scaled and unscaled children)
    const std::int32_t p0[3] = {0, 0, 0}, p1[3] = {500000, 0, 0}, p2[3] = {0, 500000, 0}, p3[3] = {0, 0, 500000};
    sb.object(7, p1, 5000);
    sb.object(7, p2, 5000);
    sb.object(7, p3, 5000);
    sb.object(17, p1, 30000);
    sb.object(18, p2, 600000);
    sb.object(28, p3, 2000);
    sb.object(0, p0, 100);
    sb.object(7, p1, 5000, 0x8000000);
    sb.object(7, p1, 5000, 0, 0, 0x12345678);
    sb.object(17, p1, 30000, 0x200);
    sb.object(7, p1, 5000, 0, 0x20000);
    const std::int32_t ps[3] = {5000000, 0, 0};
    const std::uint32_t plain = sb.object(6, ps, 750000, 0, 0, 0, 750000); // root sphere 1500 m = its hull, no children
    const std::uint32_t station = sb.object(5, ps, 750000, 0, 0, 0, 100000, 0x10000); // unit scale; bucket 5 walks first: the census station
    const std::uint32_t snode = mem.u32(station + 0x70);
    const std::int32_t o1[3] = {200000, 0, 0}, o2[3] = {0, 200000, 0}, o3[3] = {0, 0, 300000}, o4[3] = {0, 0, -600000};
    sb.child(snode, o1, 100000, false); // unscaled: at +400 m, r 200 m
    sb.child(snode, o2, 50000, true);   // flagged, scale 1.0: at +400 m up, r 100 m
    // an unfilled cache at two depths (the engine's getter 0x00488170 never ran): the child at +600 m with own radius
    // 50 m and a grandchild at a further (0, 0, +150 m) of radius 100 m, itself unfilled with own radius 100 m: the
    // replicated getter gives the grandchild 100 m and the child max(50, 150 + 100) = 250 m
    const std::uint32_t unfilled = sb.child(snode, o3, -1, false, 25000);
    const std::int32_t og[3] = {0, 0, 75000};
    sb.child(unfilled, og, -1, false, 50000);
    sb.child(snode, o4, 150000, false); // at -1200 m, r 300 m: the parts reach the 1500 m root sphere
    wk::fill_snapshot(mem, sb.sector, 2, snap, cache, st, &census);
    check(st.part_dirty == 1 && st.part_invalid == 0 && st.part_built == 2, "walk: a station with unfilled caches builds its model from the replicated getter");
    check(census.station == station && census.station_children == 4 && census.station_dirty == 1,
          "walk: the census reports the station's four children and the one unfilled cache");
    {
        bool got = false;
        for (unsigned k = 0; k < 4; ++k)
            if (census.part_cache[k] == -1 && census.part_r[k] == 125000 && census.part_offset[k][2] == 300000) got = true;
        check(got, "walk: the replicated subtree radius of the unfilled child is max(own 50 m, |150 m| + 100 m) = 250 m");
    }
    check(snap.count == 8 && !snap.overflow, "walk: 3 ships + asteroid + gate + debris + 2 stations = 8 obstacles; bullet, deleted, docked, attached and no-pair skipped");
    check(census.classes[7] == 6 && census.classes[0] == 0 && census.classes[5] == 1 && census.classes[6] == 1 && census.classes[17] == 2 &&
              census.buckets[7] == 6 && census.buckets[0] == 0,
          "walk: the census counts the walked buckets only (the bullet bucket is never read)");
    unsigned ships = 0, stations_with_parts = 0;
    const av::Obstacle* station_item = nullptr;
    for (unsigned i = 0; i < snap.count; ++i) {
        if (snap.items[i].kind == std::uint8_t(av::Kind::ship)) ++ships;
        if (snap.items[i].kind == std::uint8_t(av::Kind::station) && (snap.items[i].flags & 1)) ++stations_with_parts;
        if (snap.items[i].ptr == station) station_item = &snap.items[i];
    }
    check(ships == 3 && stations_with_parts == 2 && st.part_built == 2 && st.part_cached == 0 && st.part_invalid == 0, "walk: both stations hold a part model on the first snapshot");
    check(station_item && station_item->flags == 1 && cache.sets[station_item->parts].count == 5, "walk: the station with children has 5 parts (hull + 4 children)");
    if (station_item) {
        const av::PartSet& set = cache.sets[station_item->parts];
        bool unscaled = false, scaled = false;
        for (unsigned p = 0; p < set.count; ++p) {
            if (std::fabs(set.c[p][0] - 10400.0f) < 0.5f && std::fabs(set.r[p] - 200.0f) < 0.1f) unscaled = true;
            if (std::fabs(set.c[p][1] - 400.0f) < 0.5f && std::fabs(set.c[p][0] - 10000.0f) < 0.5f && std::fabs(set.r[p] - 100.0f) < 0.1f) scaled = true;
        }
        bool replicated = false;
        for (unsigned p = 0; p < set.count; ++p)
            if (std::fabs(set.c[p][2] - 600.0f) < 0.5f && std::fabs(set.r[p] - 250.0f) < 0.1f) replicated = true;
        check(unscaled && scaled && replicated, "walk: the unscaled child sits at its raw offset, the flagged child at offset x the unit scale, the unfilled child carries its replicated radius");
    }
    {
        // a flagged child under a non-unit parent scale (Run151: +0x70 = 0xa5 on a station node): the engine's rule applies as
        // it is (offset x 0xa5 >> 16 ~ 0: the part sits at the centre), the model is kept and reaches the root through an
        // unscaled child; the replication's depth limit: a chain of unfilled caches deeper than kRadiusDepth falls back
        const std::int32_t pq[3] = {-5000000, 0, 0};
        const std::uint32_t odd = sb.object(5, pq, 750000, 0, 0, 0, 100000, 0xa5);
        const std::int32_t oq[3] = {300000, 0, 0}, oz[3] = {-650000, 0, 0};
        sb.child(mem.u32(odd + 0x70), oq, 100000, true);
        sb.child(mem.u32(odd + 0x70), oz, 100000, false);
        const std::int32_t pd[3] = {0, 5000000, 0};
        const std::uint32_t deep = sb.object(5, pd, 750000, 0, 0, 0, 100000, 0x10000);
        const std::int32_t step[3] = {0, 0, 150000};
        std::uint32_t chain = sb.child(mem.u32(deep + 0x70), step, -1, false, 50000);
        for (unsigned k = 0; k < 4; ++k) chain = sb.child(chain, step, -1, false, 50000); // depth 5 below the root: beyond kRadiusDepth
        // a collapsed model: the root radius says 1500 m but every part sits at the centre with a small radius
        const std::int32_t pc[3] = {0, -5000000, 0};
        const std::uint32_t collapsed = sb.object(5, pc, 750000, 0, 0, 0, 50000, 0x10000);
        const std::int32_t zero[3] = {0, 0, 0};
        sb.child(mem.u32(collapsed + 0x70), zero, 50000, false);
        wk::fill_snapshot(mem, sb.sector, 2, snap, cache, st, nullptr);
        bool odd_fb = false, col_fb = false, deep_fb = false;
        const av::Obstacle* odd_item = nullptr;
        for (unsigned i = 0; i < snap.count; ++i) {
            if (snap.items[i].ptr == odd) {
                odd_fb = !(snap.items[i].flags & 1);
                odd_item = &snap.items[i];
            }
            if (snap.items[i].ptr == collapsed) col_fb = !(snap.items[i].flags & 1);
            if (snap.items[i].ptr == deep) deep_fb = !(snap.items[i].flags & 1);
        }
        bool at_centre = false;
        if (!odd_fb && odd_item)
            for (unsigned p = 0; p < cache.sets[odd_item->parts].count; ++p)
                if (std::fabs(cache.sets[odd_item->parts].c[p][0] + 10000.0f) < 5.0f && std::fabs(cache.sets[odd_item->parts].r[p] - 200.0f) < 0.1f)
                    at_centre = true;
        check(!odd_fb && at_centre && st.part_scaled >= 1, "walk: a flagged child under the non-unit scale 0xa5 sits where the engine's rule puts it (the centre); the model is kept");
        check(col_fb, "walk: parts that do not reach the root radius (a collapsed model) fail validation");
        check(deep_fb && st.part_invalid >= 1, "walk: unfilled caches deeper than the replication's depth limit leave the station on the fallback body (retried)");
    }
    // the sentinel is never read as a part: a sentinel-only child list gives the hull only
    const av::Obstacle* plain_item = nullptr;
    for (unsigned i = 0; i < snap.count; ++i)
        if (snap.items[i].ptr == plain) plain_item = &snap.items[i];
    check(plain_item && (plain_item->flags & 1) && cache.sets[plain_item->parts].count == 1, "walk: a station without children keeps its hull sphere only (the sentinel is not a part)");
    // the cache: a second fill reuses both models; a moved station rebuilds; a child outside the root sphere invalidates
    wk::fill_snapshot(mem, sb.sector, 3, snap, cache, st, nullptr);
    check(st.part_cached == 3 && st.part_built == 0, "walk: the part models are cached by object, radius and position stamp");
    mem.puts(snode + 0x30, 5100000);
    wk::fill_snapshot(mem, sb.sector, 4, snap, cache, st, nullptr);
    check(st.part_built == 1 && st.part_cached == 2 && cache.count == 3, "walk: a moved station rebuilds into the same slot");
    const std::int32_t far[3] = {1300000, 0, 0};
    sb.child(snode, far, 100000, false); // |c - pos| + r = 2800 m > sqrt(3) x 1500 m root
    mem.puts(snode + 0x34, 1000);        // the model is keyed on position/basis/radius, not on the child list: move it to rebuild
    wk::fill_snapshot(mem, sb.sector, 5, snap, cache, st, nullptr);
    bool fallback = false;
    for (unsigned i = 0; i < snap.count; ++i)
        if (snap.items[i].ptr == station) fallback = !(snap.items[i].flags & 1);
    check(fallback && st.part_invalid >= 1 && st.part_fallback >= 1, "walk: a part outside the root sphere fails validation: the fallback body");
    unsigned live = 0;
    for (unsigned i = 0; i < cache.count; ++i)
        if (cache.sets[i].station == station) ++live;
    check(live == 0, "walk: the failed rebuild invalidates the stale cached model");
    // more than 64 children: the fallback body
    const std::uint32_t big = sb.object(5, p2, 750000, 0, 0, 0, 100000);
    const std::uint32_t bnode = mem.u32(big + 0x70);
    for (unsigned i = 0; i < 70; ++i) {
        const std::int32_t off[3] = {std::int32_t(1000 * i), 0, 0};
        sb.child(bnode, off, 1000, false);
    }
    wk::fill_snapshot(mem, sb.sector, 6, snap, cache, st, nullptr);
    bool big_fallback = false;
    for (unsigned i = 0; i < snap.count; ++i)
        if (snap.items[i].ptr == big) big_fallback = !(snap.items[i].flags & 1);
    check(big_fallback, "walk: a station with more than 64 children uses the fallback body");
    // the walk limit: a bucket longer than the limit marks the snapshot overflow and empties it
    for (unsigned i = 0; i < wk::kWalkLimit + 5; ++i) sb.object(17, p3, 1000);
    wk::fill_snapshot(mem, sb.sector, 7, snap, cache, st, nullptr);
    check(snap.overflow == 1 && snap.count == 0 && st.overflow, "walk: beyond the walk limit the snapshot is overflow and empty (pass-through)");
    // a sector without a bucket table
    FakeMem empty;
    wk::fill_snapshot(empty, 0x2000, 8, snap, cache, st, nullptr);
    check(snap.overflow == 1 && snap.count == 0, "walk: no bucket table = pass-through");
}

// The snapshot's record-code read (amendment 11 review, amendment 13): a class-7 item's size from its TShips record code through
// the type table slot 0x00606fd4 ([table + type x 0xdb8 + 0x14]): 1 capital-coded, 0 any other code, 2 (the ratio alone in
// av::small_for_capital) when the slot is empty, the type word negative or the code outside the 16 classes.
static void walk_record_code_checks() {
    static FakeMem mem;
    mem = FakeMem{};
    SectorBuilder sb(mem);
    static wk::PartCache cache;
    cache = wk::PartCache{};
    static av::Snapshot snap;
    snap = av::Snapshot{};
    wk::Stats st;
    const std::uint32_t types = sb.alloc(8 * 0xdb8);
    mem.put(wk::kShipTypeTableSlot, types);
    const std::uint32_t codes[4] = {4, 7, 0x40, 11}; // M2 (capital), M5, out of range, M6 (large but not capital)
    for (unsigned k = 0; k < 4; ++k) mem.put(types + (k + 1) * 0xdb8 + 0x14, codes[k]);
    const std::int32_t p[7][3] = {{100000, 0, 0}, {200000, 0, 0}, {300000, 0, 0}, {400000, 0, 0}, {500000, 0, 0}, {600000, 0, 0}, {700000, 0, 0}};
    std::uint32_t o[7];
    for (unsigned k = 0; k < 7; ++k) o[k] = sb.object(7, p[k], k == 4 ? 200000 : k == 3 ? 150000 : 20000); // r 40 m; the unreadable large one 400 m; the M6 300 m
    for (unsigned k = 0; k < 4; ++k) mem.put(o[k] + 0x48, 7u | ((k + 1) << 16)); // types 1..4
    mem.put(o[4] + 0x48, 7u | (0xffffu << 16));                                 // type -1: unreadable
    mem.put(o[5] + 0x48, 7u | (3u << 16));                                      // type 3: code 0x40, unreadable
    mem.put(o[6] + 0x48, 7u | (4u << 16));                                      // type 4: M6 at 40 m
    wk::fill_snapshot(mem, sb.sector, 1, snap, cache, st, nullptr);
    std::uint8_t size[7] = {9, 9, 9, 9, 9, 9, 9};
    const av::Obstacle* item[7] = {};
    for (unsigned i = 0; i < snap.count; ++i)
        for (unsigned k = 0; k < 7; ++k)
            if (snap.items[i].ptr == o[k]) {
                size[k] = snap.items[i].size;
                item[k] = &snap.items[i];
            }
    check(size[0] == 1, "walk: a capital-coded record (M2 4) reads size 1");
    check(size[1] == 0 && size[3] == 0 && size[6] == 0, "walk: a non-capital record code (M5 7, M6 11) reads size 0 (the engine's large list no longer decides)");
    check(size[2] == 2 && size[4] == 2 && size[5] == 2, "walk: a code outside the classes or a negative type word reads size 2 (unreadable)");
    av::Self cap{};
    cap.capital = true;
    cap.r = 700.0f; // r / 3 = 233 m
    bool ok = true;
    for (unsigned k = 0; k < 7; ++k) ok = ok && item[k];
    if (ok) {
        check(!av::small_for_capital(cap, *item[0]) && av::small_for_capital(cap, *item[1]) && !av::small_for_capital(cap, *item[3]) && av::small_for_capital(cap, *item[6]),
              "walk: a capital keeps the capital-coded ship (40 m) and the 300 m M6, skips the 40 m M5 and the 40 m M6 (the ratio, not the large list)");
        check(av::small_for_capital(cap, *item[2]) && !av::small_for_capital(cap, *item[4]),
              "walk: an unreadable code uses the ratio r < r_own / 3 (40 m skipped, 400 m kept)");
    } else
        check(false, "walk: every class-7 item of the record-code sector is in the snapshot");
    mem.put(wk::kShipTypeTableSlot, 0); // an empty slot: every ship unreadable
    wk::fill_snapshot(mem, sb.sector, 2, snap, cache, st, nullptr);
    unsigned unreadable = 0;
    for (unsigned i = 0; i < snap.count; ++i)
        if (snap.items[i].kind == std::uint8_t(av::Kind::ship) && snap.items[i].size == 2) ++unreadable;
    check(unreadable == 7, "walk: an empty type table slot reads size 2 for every ship");
}
// ---- step 2 unit checks -----------------------------------------------------------------
static void avoidance_unit_checks() {
    // the design's lookahead numbers (design_numbers.txt): L vs the same class, a 1.5 km station, a 60 m asteroid
    struct Row {
        const char* cls;
        double v, a, om, r, same, station, asteroid;
    };
    const Row rows[5] = {{"M5", 532, 55, 147.66, 9.2, 442, 2426, 539}, {"M3", 247.5, 40, 92.81, 11.9, 288, 1549, 371},
                         {"M6", 180, 20, 33.75, 64.4, 531, 1661, 523}, {"TS", 124, 30, 21.09, 38.5, 382, 1602, 382},
                         {"M2", 70, 15, 13.5, 705.4, 2469, 3264, 1824}};
    for (const Row& r : rows) {
        const float om = float(r.om * M_PI / 180);
        const double same = av::lookahead(float(r.v), float(r.a), om, float(r.r), av::clearance(float(r.r), float(r.r)));
        const double st = av::lookahead(float(r.v), float(r.a), om, float(r.r), av::clearance(float(r.r), 1500.0f));
        const double as = av::lookahead(float(r.v), float(r.a), om, float(r.r), av::clearance(float(r.r), 60.0f));
        // An independent reimplementation of the section 3.3 formula: the design numbers (reaction 0.5 s) must come out
        // of it, and the implemented lookahead (reaction kReactionS, no ramp here: alpha = 0) must equal it with that term.
        auto formula = [&](double r_obs, double reaction) {
            const double C = r.r + r_obs + std::fmax(25.0, 0.5 * r.r), R = r.v / (r.om * M_PI / 180);
            const double L = reaction * r.v + 1.25 * std::fmin(std::sqrt(C * (C + 2 * R)), r.v * r.v / (2 * r.a));
            return std::fmin(std::fmax(L, C + r.r), 30000.0);
        };
        char why[220];
        std::snprintf(why, sizeof why, "%s design lookahead same/station/asteroid %.0f/%.0f/%.0f = formula %.0f/%.0f/%.0f (reaction 0.5 s)", r.cls, r.same, r.station,
                      r.asteroid, formula(r.r, 0.5), formula(1500, 0.5), formula(60, 0.5));
        check(std::fabs(formula(r.r, 0.5) - r.same) <= 1.0 && std::fabs(formula(1500, 0.5) - r.station) <= 1.0 && std::fabs(formula(60, 0.5) - r.asteroid) <= 1.0, why);
        std::snprintf(why, sizeof why, "%s implemented lookahead %.0f/%.0f/%.0f = formula %.0f/%.0f/%.0f (reaction %.1f s)", r.cls, same, st, as,
                      formula(r.r, av::kReactionS), formula(1500, av::kReactionS), formula(60, av::kReactionS), double(av::kReactionS));
        check(std::fabs(same - formula(r.r, av::kReactionS)) <= 1.0 && std::fabs(st - formula(1500, av::kReactionS)) <= 1.0 &&
                  std::fabs(as - formula(60, av::kReactionS)) <= 1.0, why);
    }
    // closest approach: a head-on static sphere, a crossing ship, a sphere off the path
    av::Self s{};
    s.f[2] = 1;
    s.up[1] = 1;
    s.v = 100;
    s.r = 10;
    s.a = 40; // d_stop 125 m: L = 100 + 1.25 x 125 = 256 m for C = 85 (reaction 1 s)
    s.omega = 1;
    const float c1[3] = {0, 0, 1000}, w0[3] = {0, 0, 0};
    float tt, mm, cat[3], e[3];
    check(av::closest_approach(s, c1, w0, 20.0f, 100.0f, &tt, &mm, cat, e) && std::fabs(tt - 10.0f) < 0.01f && mm < 0.01f, "cpa: head-on at 10 s, miss 0");
    const float c2[3] = {500, 0, 500}, w2[3] = {-100, 0, 0};
    check(av::closest_approach(s, c2, w2, 20.0f, 100.0f, &tt, &mm, cat, e) && std::fabs(tt - 5.0f) < 0.01f && mm < 0.01f, "cpa: crossing ship meets at 5 s");
    const float c3[3] = {300, 0, 1000};
    check(!av::closest_approach(s, c3, w0, 20.0f, 100.0f, &tt, &mm, cat, e), "cpa: 300 m off the path with C = 100 is no conflict");
    check(av::closest_approach(s, c3, w0, 20.0f, 350.0f, &tt, &mm, cat, e) && std::fabs(mm - 300.0f) < 0.01f && e[0] < -0.99f,
          "cpa: 300 m off the path with C = 350: miss 300, e points from the obstacle to the path");
    check(!av::closest_approach(s, c1, w0, 5.0f, 100.0f, &tt, &mm, cat, e) || tt == 5.0f, "cpa: the horizon bounds t");
    // the part model validation
    av::PartSet set;
    const float origin[3] = {0, 0, 0};
    float centres[3][3] = {{0, 0, 0}, {100, 0, 0}, {0, 100, 0}}, radii[3] = {50, 60, 70};
    check(av::build_parts(set, 0x5000, 500, origin, centres, radii, 3) && set.count == 3, "parts: a valid model builds");
    radii[1] = 900;
    check(!av::build_parts(set, 0x5000, 500, origin, centres, radii, 3) && set.count == 0, "parts: a part larger than sqrt(3) x the root sphere is refused");
    radii[1] = 60;
    centres[1][0] = 820; // |c - pos| + r = 880 > 866 = sqrt(3) x 500: outside the root sphere beyond the per-axis slack
    check(!av::build_parts(set, 0x5000, 500, origin, centres, radii, 3), "parts: a part reaching outside sqrt(3) x the root sphere is refused");
    centres[1][0] = 460; // |c - pos| + r = 520: inside the slack (the engine's per-axis radius admits it)
    check(av::build_parts(set, 0x5000, 500, origin, centres, radii, 3) && set.count == 3, "parts: a part within the per-axis slack of the root sphere is kept");
    centres[1][0] = 100;
    centres[2][1] = __builtin_inff();
    check(!av::build_parts(set, 0x5000, 500, origin, centres, radii, 3), "parts: a non-finite centre is refused");
    check(!av::build_parts(set, 0x5000, 500, origin, centres, radii, 0), "parts: an empty model is refused");
    // suppression aging and membership
    av::ShipAvoid a{};
    a.obj = 0x1000;
    a.stamp_ms = 1000;
    a.resolved[0] = 0x2000;
    a.n_resolved = 1;
    check(av::suppressed(a, 0x1000, 0x2000, 1050) && !av::suppressed(a, 0x1000, 0x3000, 1050) && !av::suppressed(a, 0x1000, 0x2000, 1200) &&
              !av::suppressed(a, 0x1001, 0x2000, 1050),
          "E: resolved pair within 100 ms only, and only for the state's own ship");
    // exclusions: self, leader, the order's target, the formation in every direction (amendment 11), own followers
    av::Self me{};
    me.ptr = 1;
    me.leader = 2;
    me.target = 2;
    me.exclude_target = true;
    me.leader_x[0] = 1000;
    av::Obstacle o{};
    o.kind = std::uint8_t(av::Kind::ship);
    o.ptr = 1;
    check(av::excluded(me, o), "excluded: self");
    o.ptr = 2;
    check(av::excluded(me, o), "excluded: the leader");
    o.ptr = 3;
    o.leader = 2;
    o.order = core::kOrderEscort;
    o.x[0] = 1050;
    check(av::excluded(me, o), "excluded: a wingmate near the leader");
    o.x[0] = 20000;
    check(av::excluded(me, o), "excluded: a wingmate of the same leader far from it (every direction)");
    o.order = core::kOrderFollow;
    check(av::excluded(me, o), "excluded: a wingmate on order 0x15");
    o.order = core::kOrderMovePos;
    check(!av::excluded(me, o), "not excluded: a ship whose stale f+0x224 is our leader but is not on 0x14/0x15");
    o.x[0] = 0;
    o.leader = 1;
    o.order = core::kOrderEscort;
    check(av::excluded(me, o), "excluded: our own follower");
    o.x[0] = 30000;
    check(av::excluded(me, o), "excluded: our own follower far away");
    {
        av::Self lead{}; // the leader's view: its follower is excluded, the follower's view: the leader is (both directions)
        lead.ptr = 2;
        lead.order = core::kOrderMovePos;
        av::Obstacle f{};
        f.kind = std::uint8_t(av::Kind::ship);
        f.ptr = 1;
        f.leader = 2;
        f.order = core::kOrderFollow;
        f.x[0] = 15000;
        check(av::excluded(lead, f), "excluded: the leader never avoids its follower, wherever it is");
        av::Self fol{};
        fol.ptr = 1;
        fol.leader = 2;
        fol.order = core::kOrderFollow;
        av::Obstacle l{};
        l.kind = std::uint8_t(av::Kind::ship);
        l.ptr = 2;
        check(av::excluded(fol, l), "excluded: the follower never avoids its leader");
    }
    // the horizon cap (amendment 11): min(L / v, 30 s) of flight; the Boreas's L / v is 43.9 s uncapped
    {
        const ShipType& b = kShips[4];
        const float om = float(b.yaw_dps * M_PI / 180);
        const float Cb = av::clearance(float(b.r_m), 50.0f);
        const float Lb = av::lookahead(float(b.v_ms), float(b.a_ms2), om, float(b.r_m), Cb, om * float(b.k_pct) / 100.0f);
        const float Tb = av::horizon_s(Lb, float(b.v_ms));
        std::printf("boreas horizon: L=%.0f m v=%.0f m/s L/v=%.1f s horizon=%.1f s swept=%.0f m (C vs a 50 m asteroid %.0f m)\n", Lb, b.v_ms,
                    Lb / b.v_ms, Tb, Tb * float(b.v_ms), Cb);
        checkf(Lb / b.v_ms > av::kHorizonMaxS && Tb == av::kHorizonMaxS, "boreas: the uncapped horizon %.1f s is capped to %.0f s", Lb / b.v_ms, Tb);
        check(av::horizon_s(Lb, 0.0f) == av::kHorizonMaxS && av::horizon_s(100.0f, 50.0f) == 2.0f, "horizon: v = 0 gives the cap, a short L its own time");
        static av::Snapshot sb;
        av::Self cap{};
        cap.f[2] = 1;
        cap.up[1] = 1;
        cap.v = float(b.v_ms);
        cap.r = float(b.r_m);
        cap.a = float(b.a_ms2);
        cap.omega = om;
        cap.alpha = om * float(b.k_pct) / 100.0f;
        cap.ptr = 0x9000;
        cap.order = core::kOrderMovePos;
        cap.capital = true;
        const float Pc[3] = {0, 0, 20000};
        for (int pass = 0; pass < 2; ++pass) {
            sb = av::Snapshot{};
            sb.count = 1;
            sb.max_r = 50;
            av::Obstacle& a = sb.items[0];
            a = av::Obstacle{};
            a.kind = std::uint8_t(av::Kind::asteroid);
            a.ptr = 0x7000;
            a.r = 50;
            a.x[2] = pass == 0 ? Tb * float(b.v_ms) + Cb + 200.0f : Tb * float(b.v_ms) + Cb - 200.0f; // 200 m beyond / inside the capped reach
            av::ShipAvoid st{};
            st.obj = cap.ptr;
            av::Result rr = av::resolve(sb, nullptr, 0, cap, Pc, st, 1);
            if (pass == 0)
                checkf(rr.conflicts == 0 && a.x[2] < Lb + Cb, "horizon: an asteroid %.0f m ahead (inside the uncapped L + C %.0f m) is no conflict", a.x[2], Lb + Cb);
            else
                checkf(rr.conflicts == 1 && rr.L == Tb * float(b.v_ms), "horizon: an asteroid %.0f m ahead is a conflict, L_m reports the swept %.0f m", a.x[2], rr.L);
        }
    }
    // the snapshot overflow passes the point through
    static av::Snapshot snap;
    snap = av::Snapshot{};
    snap.overflow = 1;
    av::ShipAvoid st{};
    const float P[3] = {0, 0, 10000};
    av::Result r = av::resolve(snap, nullptr, 0, s, P, st, 1);
    check(!r.adjusted && r.overflow, "overflow: pass-through");
    // a head-on asteroid at 500 m: a detour on a constant side, the resolved set holds it
    snap.overflow = 0;
    snap.count = 1;
    snap.items[0] = av::Obstacle{};
    snap.items[0].x[2] = 240; // inside the horizon (256 m), beyond d_turn (155 m), D_stop (125 m) and 2.4 C (204 m): flyable, the straight path clears
    snap.items[0].r = 50;
    snap.items[0].ptr = 0x7000;
    snap.items[0].kind = std::uint8_t(av::Kind::asteroid);
    snap.max_r = 50;
    s.ptr = 1;
    r = av::resolve(snap, nullptr, 0, s, P, st, 1);
    const av::Result r2 = av::resolve(snap, nullptr, 0, s, P, st, 2);
    check(r.adjusted && r2.adjusted && r.side == r2.side && std::fabs(r.P[0]) + std::fabs(r.P[1]) > 50 && st.n_resolved == 1 && st.resolved[0] == 0x7000,
          "detour: head-on asteroid, constant side, resolved for site E");
    snap.items[0].kind = std::uint8_t(av::Kind::station);
    snap.items[0].r = 2000;
    snap.max_r = 2000;
    r = av::resolve(snap, nullptr, 0, s, P, st, 3);
    check(r.adjusted && st.n_resolved == 0, "station fallback body: detoured, never suppressed at E");
    // review finding 2: an M5 at 100 m/s head-on to a ship 55 m ahead (d_turn 72 m, D_stop 250 m) detours but is
    // never suppressed at E: the pair is inside the stopping distance and the turn-in distance
    {
        av::Self m5{};
        m5.f[2] = 1;
        m5.up[1] = 1;
        m5.v = 100;
        m5.r = 9.2f;
        m5.a = 20;
        m5.omega = 2.58f;
        m5.alpha = 1.29f;
        m5.ptr = 0x1000;
        for (float dd : {45.0f, 55.0f, 70.0f, 100.0f, 200.0f, 400.0f}) {
            snap = av::Snapshot{};
            snap.count = 1;
            snap.max_r = 9.2f;
            snap.items[0] = av::Obstacle{};
            snap.items[0].ptr = 0x2000;
            snap.items[0].r = 9.2f;
            snap.items[0].x[2] = dd;
            av::ShipAvoid sa{};
            sa.obj = 0x1000;
            const av::Result rr = av::resolve(snap, nullptr, 0, m5, P, sa, 1000);
            const float C = av::clearance(9.2f, 9.2f), R = 100.0f / 2.58f;
            const bool flyable = dd > std::sqrt(C * (C + 2 * R)) && dd > 100.0f * 100.0f / 40.0f;
            char why[200];
            std::snprintf(why, sizeof why, "head-on ship at %.0f m: suppressed only when flyable (d_turn %.0f, D_stop 250): resolved %u, adjusted %d", dd,
                          std::sqrt(C * (C + 2 * R)), sa.n_resolved, rr.adjusted);
            check(av::suppressed(sa, 0x1000, 0x2000, 1050) == (flyable && sa.n_resolved > 0) && (!flyable ? sa.n_resolved == 0 : true), why);
        }
    }
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

// ---- scenario 8b: a capital through a cloud of fighters (amendment 11) ------------------------------------------------
// The M2 flies a straight 20 km leg; 12 M5 fighters cross its path in front of it (a wall of crossing courses, each
// on a collision course with the capital at its own time). The capital must not deviate (the engine's own proximity
// filter lets a capital ignore small ships: it skips them, `skipped_small`), the fighters detour around it with
// clearance >= C. A second capital crossing the first's path is still detoured.
static void capital_cloud_scenario(int fps, std::uint32_t seed) {
    static World w;
    w = World{};
    core::ShipTable table{};
    const ShipType& cap = kShips[4]; // M2
    const ShipType& fighter = kShips[0]; // M5
    Model c;
    c.init(cap, seed);
    c.table = &table;
    c.ptr = 0x9000;
    c.order = core::kOrderMovePos;
    c.capital = true;
    c.P[2] = 20000.0 * 500;
    c.d_arr_units = 2 * c.r_units;
    w.attach(c);
    Model fighters[12];
    Model* ships[13];
    ships[0] = &c;
    const double v_c = cap.v_ms, v_f = fighter.v_ms;
    for (unsigned i = 0; i < 12; ++i) {
        Model& m = fighters[i];
        m.init(fighter, seed + 7 * i + 1);
        m.table = &table;
        m.ptr = 0x4000 + 0x100 * i;
        m.order = core::kOrderMovePos;
        // the crossing point on the capital's line at z = 3 km + i x 600 m; the fighter starts to the side so that both
        // reach it at the same time (a collision course), alternating sides and a vertical offset of +-30 m
        const double z = 2000.0 + 400.0 * i, t_meet = z / v_c, side = (i & 1) ? -1.0 : 1.0;
        m.pos[0] = side * v_f * t_meet * 500.0;
        m.pos[1] = (int(i % 3) - 1) * 30.0 * 500.0;
        m.pos[2] = z * 500.0;
        m.row[0][0] = 0;
        m.row[0][2] = side; // heading -side x: row2 = (-side, 0, 0), row0 = (0, 0, side) keeps the basis orthonormal
        m.row[2][0] = -side;
        m.row[2][2] = 0;
        m.P[0] = -side * 20000.0 * 500.0;
        m.P[1] = m.pos[1];
        m.P[2] = m.pos[2];
        m.d_arr_units = 2 * m.r_units;
        w.attach(m);
        ships[i + 1] = &m;
    }
    Clock clocks[13];
    const float C = av::clearance(float(fighter.r_m), float(cap.r_m));
    double min_clear = 1e18, max_heading_deg = 0, per_min[12];
    for (unsigned k = 0; k < 12; ++k) per_min[k] = 1e18;
    for (long i = 0; i < 200 * fps; ++i) {
        const long dt = clocks[0].step(fps);
        for (unsigned k = 1; k < 13; ++k) clocks[k].step(fps);
        if (!dt) continue;
        w.fill(ships, 13, std::uint32_t(c.t_ms));
        c.frame(dt, 0x1000, true, 0, 1); // object ids 16 bytes apart, as the engine's objects are (the table's hash drops 4 bits)
        for (unsigned k = 0; k < 12; ++k) fighters[k].frame(dt, 0x1010 + 0x10 * k, true, 0, 1);
        double hc = std::acos(std::fmax(-1.0, std::fmin(1.0, c.row[2][2]))) * 180 / M_PI; // the capital's heading off +z
        if (hc > max_heading_deg) max_heading_deg = hc;
        for (unsigned k = 0; k < 12; ++k) {
            double d = 0;
            for (int j = 0; j < 3; ++j) d += (c.pos[j] - fighters[k].pos[j]) * (c.pos[j] - fighters[k].pos[j]);
            d = std::sqrt(d) / 500.0;
            if (d < min_clear) min_clear = d;
            if (d < per_min[k]) per_min[k] = d;
        }
        if (c.pos[2] / 500.0 > 2000.0 + 400.0 * 11 + 2000.0) break; // past the last crossing
    }
    unsigned detouring = 0, skipped = c.skipped_small;
    for (unsigned k = 0; k < 12; ++k)
        if (fighters[k].detours > 0) ++detouring;
    char why[220];
    std::snprintf(why, sizeof why, "%d fps seed %u capital through 12 fighters: the capital never deviates (heading change %%.2f deg <= 1), 0 detours, small ships skipped (%%.0f)", fps, seed);
    checkf(max_heading_deg <= 1.0 && c.detours == 0 && skipped > 0, why, max_heading_deg, double(skipped));
    std::snprintf(why, sizeof why, "%d fps seed %u capital through 12 fighters: every fighter detours (%%.0f of 12) with clearance >= C (min %%.1f m)", fps, seed);
    checkf(detouring == 12 && min_clear >= C, why, double(detouring), min_clear);
    if (seed == kSeeds[0] && fps == 60) {
        std::printf("capital cloud fps=%3d heading_change=%.2f deg capital_detours=%u skipped_small=%u fighters_detouring=%u/12 min_clear=%.1f m (C=%.1f)\n", fps,
                    max_heading_deg, c.detours, skipped, detouring, min_clear, C);
        for (unsigned k = 0; k < 12; ++k)
            std::printf("capital cloud   fighter %2u detours=%u min_dist=%.0f m unresolved=%u\n", k, fighters[k].detours, per_min[k], fighters[k].unresolved);
    }
}

// ---- scenario 9: the gate approach (amendment 7; ship-navigation-ai.md section 10) ------------------------------
// The engine's gate transit for one ship: order 4 to A = G + R(0, -0.8, -1) r (the leaf every frame, the arrival test
// dist(A) <= 0.75 r and the speed law at the think tick: vmax beyond d_slow = 0.75 r + vmax^2/a, ramp(dist; 0 -> 5000,
// d_slow -> vmax/2) inside, the alignment ramp (0.7 -> 500, 1.0 -> desired) inside d_slow + 3 r + 2 r_ship, large hulls
// capped at 50000), P2 at the arrival, one frame of order 0, order 5 to B = G + R(0, -0.55 cos th, 0.55 sin th) r
// every frame (500 while forward . dir < 0.95, else ramp(dist; r/5 -> vmax/2, r -> vmax); arrival at dist <= |v|/10 +
// r_ship), P4 at the arrival, one frame of order 0, order 8 for 900 ms along the axis with s = v0 t / 1000 + 1.665 t^2.
// gates_on && !capital = the module's path (P1 lead-in and speed, P2/P4 keep the speed through the shared rule
// core::gate_stop_keeps_speed over core::gate_target_noncapital on a synthetic object, P5 through gate_launch_units);
// otherwise the vanilla stops and the launch from rest. The gate frame R (rows) and the start (gate-local, in r)
// are parameters; the disc (local z = 0, radius r) must never be crossed.
struct GateResult {
    bool arrived_A, arrived_B, launched, lead_used, disc_crossed;
    double cross_v, cross_err_deg, min_v_after_cross, v_after_A, v0, launch_first_v, max_aim_jump_deg;
    double cross_pos[3], cross_dA, off[2]; // the crossing point (m, world), its distance to A, the spread offset (m, gate x / y)
    long t_cross, t_A, t_B, t_launch;
    int misaligned_ticks;
    unsigned ring;
};
static double norm3d(const double a[3]) {
    return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}
struct GateFrame {
    double R[3][3]; // rows: local x, y (up), z (the axis) in world
};
static const GateFrame kGateIdentity = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
// a gate whose axis points along world +x and whose up is world +z (a right-handed frame: x_local = y x z)
static const GateFrame kGateRotated = {{{0, 1, 0}, {0, 0, 1}, {1, 0, 0}}};
static void local_to_world(const GateFrame& F, const double l[3], double w[3]) {
    for (int k = 0; k < 3; ++k) w[k] = l[0] * F.R[0][k] + l[1] * F.R[1][k] + l[2] * F.R[2][k];
}
// --emit-trace: the gate scenario's frames as ship_trace rows in the DLL's format (src/proxy/ship_steering.cpp), one row
// per frame at site B (after the module's speed write, before the slew and pass B), for ship_trace_replay.py's
// conformance check of its Python port of this model. The launch (order 8) frames move the ship by gate_launch_units,
// not by the integrator, and the DLL frees the trace slot at P5, so they are not emitted.
static std::FILE* g_trace = nullptr;
static unsigned long long g_trace_frame = 0;
static unsigned long g_trace_rows = 0;
static void emit_trace_row(const Model& m, const char* cls, std::uint32_t obj, long dt, bool leaf_call, std::int64_t des_engine, int rot_pct) {
    if (!g_trace) return;
    long point[3], given[3];
    const bool handed = leaf_call && !m.vanilla && m.last_reason == core::Reason::handled;
    for (int k = 0; k < 3; ++k) {
        given[k] = long(std::llround(m.P[k]));
        point[k] = handed ? long(std::llround(double(m.last_aim_P[k]) * 500.0)) : given[k];
    }
    std::fprintf(g_trace,
                 "ship_trace frame=%llu t_ms=%lu obj=0x%08lx class=%s order=0x%lx tgt=0x%08lx tgt_class=%u pos=%ld,%ld,%ld "
                 "fwd=%.5f,%.5f,%.5f up=%.5f,%.5f,%.5f rate=%ld,%ld,%ld v=%ld des=%ld des_engine=%ld point=%ld,%ld,%ld given=%ld,%ld,%ld ",
                 ++g_trace_frame, static_cast<unsigned long>(m.t_ms), static_cast<unsigned long>(obj), cls, static_cast<unsigned long>(m.order),
                 0x4000ul, unsigned(m.trace_tgt_class), long(std::llround(m.pos[0])), long(std::llround(m.pos[1])), long(std::llround(m.pos[2])), m.row[2][0], m.row[2][1],
                 m.row[2][2], m.row[1][0], m.row[1][1], m.row[1][2], long(m.w[0]), long(m.w[1]), long(m.w[2]), long(m.speed), long(m.desired),
                 long(des_engine), point[0], point[1], point[2], given[0], given[1], given[2]);
    if (leaf_call)
        std::fprintf(g_trace, "e=%ld,%ld,%ld", long(m.e[0]), long(m.e[1]), long(m.e[2]));
    else
        std::fprintf(g_trace, "e=-");
    std::fprintf(g_trace, " dt_ms=%lu W=%ld,%ld,%ld rot=0x%08lx rot_pct=%ld vmax=%ld acc=%ld emg=%u omega_cmd=%.2f,%.2f,%.2f obst=0x%08lx\n",
                 static_cast<unsigned long>(dt), long(m.W[0]), long(m.W[1]), long(m.W[2]), 0x10000ul, long(rot_pct), long(m.vmax), long(m.accel),
                 m.last_av.emergency ? 1u : 0u, m.last_cmd.omega_axis[0] * 180 / M_PI, m.last_cmd.omega_axis[1] * 180 / M_PI,
                 m.last_cmd.omega_axis[2] * 180 / M_PI, static_cast<unsigned long>(m.last_av.adjusted ? m.last_av.obstacle : 0u));
    ++g_trace_rows;
}
// obj_id selects the ship's ring position (the spread); capital_slot the grid point of a capital (-1 = slot 7)
static GateResult run_gate(const ShipType& t, int fps, std::uint32_t seed, double theta_deg, bool capital, bool vanilla, bool gates_on,
                           const double start_local_r[3], const GateFrame& F, double a_error_r = 0.0, std::uint32_t obj_id = 9, int capital_slot = -1,
                           double dock_start_m = 0.0, double dock_err_deg = 0.0, std::uint16_t gate_subtype = 0) { // dock_start_m > 0: begin on order 5
                           // that far from B, heading off by dock_err_deg; subtypes 5..8: the pass offset 0.05 r
    core::ShipTable table{};
    Model m;
    m.init(t, seed);
    m.table = &table;
    m.capital = capital;
    m.capital_gate_leg = capital;
    m.vanilla = vanilla;
    const double r = 1105.0, ru = r * 500;
    // a capital's point is its grid slot (KC GetWarpInPos: 666 m pitch, 2 (r_gate + r_ship) in front) and its pass point the
    // gate's origin (section 10.5); a small ship's the shared A and the arc point B
    const int slot = capital_slot < 0 ? 7 : capital_slot;
    const int grid_a[5] = {0, 1, -1, 2, -2};
    const double A_l[3] = {capital ? grid_a[slot % 5] * 666.0 * 500 : 0.0, capital ? -(slot / 5) * 666.0 * 500 : -0.8 * ru,
                           capital ? -2.0 * (r + t.r_m) * 500 : -1.0 * ru};
    const double th = theta_deg * M_PI / 180;
    // the pass point on the gate disc (amendment 10, Run154 / the listing): (y0 sin theta, -y0 cos theta, -0.01) r
    const double y0 = double(core::gate_pass_y0(gate_subtype));
    const double B_l[3] = {capital ? 0.0 : y0 * std::sin(th) * ru, capital ? 0.0 : -y0 * std::cos(th) * ru, capital ? 0.0 : -0.01 * ru};
    double A[3], B[3], start[3];
    local_to_world(F, A_l, A);
    local_to_world(F, B_l, B);
    const double start_l[3] = {start_local_r[0] * ru, start_local_r[1] * ru, start_local_r[2] * ru};
    local_to_world(F, start_l, start);
    // the geometry the DLL derives from the target's phys block (A as the engine wrote it, or off by a_error_r: rejected)
    float Af[3], G[3] = {0, 0, 0}, row0[3], row1[3], row2[3];
    for (int k = 0; k < 3; ++k) {
        Af[k] = float(A[k] / 500 + a_error_r * r * F.R[0][k]);
        row0[k] = float(F.R[0][k]);
        row1[k] = float(F.R[1][k]);
        row2[k] = float(F.R[2][k]);
    }
    GateResult res{};
    if (capital)
        m.gate_mode = gates_on && core::gate_geometry_capital(Af, G, row0, row1, row2, float(r), float(t.r_m), &m.gate_geo);
    else {
        m.gate_mode = gates_on && core::gate_geometry(Af, G, row0, row1, row2, float(r), &m.gate_geo);
        if (m.gate_mode) { // the per-ship spread (the DLL's decision): the lead-in line shifted on the ring, A unchanged
            float off[3];
            res.ring = core::gate_spread_index(obj_id);
            core::gate_spread_offset(res.ring, core::kGateSpreadR * m.gate_geo.r, row0, row1, off);
            for (int k = 0; k < 3; ++k) m.gate_geo.X[k] += off[k];
            res.off[0] = double(off[0] * row0[0] + off[1] * row0[1] + off[2] * row0[2]);
            res.off[1] = double(off[0] * row1[0] + off[1] * row1[1] + off[2] * row1[2]);
        }
    }
    // the synthetic object the shared gate-site rule reads: a class-7 ship of a type whose record code is the class's
    // (M2 = 4 capital, M5 = 7, M3 = 5, M6 = 11, TS = 1), its flight block targeting a class-0x12 gate object
    FakeMem mem;
    const std::uint32_t ship = 0x2000, flight = 0x3000, gate = 0x4000, types = 0x5000;
    const unsigned code = capital ? 4 : (!std::strcmp(t.cls, "M5") ? 7 : !std::strcmp(t.cls, "M3") ? 5 : !std::strcmp(t.cls, "M6") ? 11 : 1);
    mem.put(ship + 0x48, 7u | (3u << 16)); // class word 7, type 3
    mem.put(ship + 0x50, flight);
    mem.put(flight + 0x224, gate);
    mem.put(gate + 0x48, 0x12);
    mem.put(types + 3 * 0xdb8 + 0x14, code);
    const bool noncapital_gate = core::gate_target_noncapital(mem, ship, types);
    const bool keep = core::gate_stop_keeps_speed(noncapital_gate, m.gate_mode && !capital); // the gate state exists only when the decision ran P1
                                                                                            // (a capital's path: capital_transit_scenario)
    for (int k = 0; k < 3; ++k) {
        m.gate_G[k] = G[k];
        m.gate_rows[0][k] = row0[k];
        m.gate_rows[1][k] = row1[k];
        m.gate_rows[2][k] = row2[k];
    }
    m.gate_r = float(r);
    {
        float Bf[3];
        for (int k = 0; k < 3; ++k) Bf[k] = float(B[k] / 500);
        m.gate_dock_mode = gates_on && !capital && core::gate_pass_point_valid(Bf, G, row0, row1, row2, float(r), gate_subtype); // the DLL's order-5 validation
    }
    const bool dock_start = dock_start_m > 0.0;
    if (dock_start) { // the order-5 start: dock_start_m from B along the direction from B back toward A, heading rotated in the gate's y-z plane
        double ub[3] = {A[0] - B[0], A[1] - B[1], A[2] - B[2]};
        const double un0 = norm3d(ub);
        for (int i = 0; i < 3; ++i) ub[i] /= un0;
        for (int i = 0; i < 3; ++i) start[i] = B[i] + dock_start_m * 500.0 * ub[i];
    }
    // the start, heading at A (or at B rotated by the dock error) at full speed
    for (int i = 0; i < 3; ++i) m.pos[i] = start[i];
    double u[3] = {A[0] - start[0], A[1] - start[1], A[2] - start[2]};
    if (dock_start) {
        // the heading: the direction to B rotated by dock_err_deg about the gate's x axis
        double tb[3] = {B[0] - start[0], B[1] - start[1], B[2] - start[2]};
        const double tn = norm3d(tb);
        for (int i = 0; i < 3; ++i) tb[i] /= tn;
        const double ly = tb[0] * row1[0] + tb[1] * row1[1] + tb[2] * row1[2], lz = tb[0] * row2[0] + tb[1] * row2[1] + tb[2] * row2[2];
        const double lx = tb[0] * row0[0] + tb[1] * row0[1] + tb[2] * row0[2];
        const double ce = std::cos(dock_err_deg * M_PI / 180), se = std::sin(dock_err_deg * M_PI / 180);
        const double ry = ly * ce - lz * se, rz = ly * se + lz * ce;
        for (int i = 0; i < 3; ++i) u[i] = lx * row0[i] + ry * row1[i] + rz * row2[i];
    }
    const double un = norm3d(u);
    for (int i = 0; i < 3; ++i) u[i] /= un;
    double up[3] = {0, 1, 0};
    if (std::fabs(u[1]) > 0.9) {
        up[1] = 0;
        up[2] = 1;
    }
    for (int i = 0; i < 3; ++i) m.row[2][i] = u[i];
    m.row[0][0] = up[1] * u[2] - up[2] * u[1];
    m.row[0][1] = up[2] * u[0] - up[0] * u[2];
    m.row[0][2] = up[0] * u[1] - up[1] * u[0];
    const double r0n = norm3d(m.row[0]);
    for (int i = 0; i < 3; ++i) m.row[0][i] /= r0n;
    m.row[1][0] = u[1] * m.row[0][2] - u[2] * m.row[0][1];
    m.row[1][1] = u[2] * m.row[0][0] - u[0] * m.row[0][2];
    m.row[1][2] = u[0] * m.row[0][1] - u[1] * m.row[0][0];
    m.order = core::kOrderFlyBefore;
    for (int i = 0; i < 3; ++i) m.P[i] = A[i];
    m.d_arr_units = 0.75 * ru; // the DLL's d_arr for a gate target (3 r / 4): the capital's ramp
    res.min_v_after_cross = 1e18;
    res.t_cross = res.t_A = res.t_B = res.t_launch = -1;
    res.cross_err_deg = -1;
    Clock clock;
    int phase = dock_start ? 2 : 0; // 0 order 4, 1 order 0 after A, 2 order 5, 3 order 0 after B, 4 order 8
    if (dock_start) {
        m.order = core::kOrderDock;
        for (int k = 0; k < 3; ++k) m.P[k] = B[k];
        res.arrived_A = true;
        res.t_cross = 0;
    }
    long phase_t = 0;
    double launch_start[3] = {0, 0, 0}, last_pos[3] = {m.pos[0], m.pos[1], m.pos[2]}, last_aim[3] = {0, 0, 0};
    bool have_aim = false;
    long long v0 = 0;
    const std::uint32_t obj = obj_id;
    for (long i = 0; i < 400 * fps; ++i) {
        const long dt = clock.step(fps);
        if (!dt) continue;
        m.frame_dt_ms = dt;
        const double dA_v[3] = {A[0] - m.pos[0], A[1] - m.pos[1], A[2] - m.pos[2]}, dB_v[3] = {B[0] - m.pos[0], B[1] - m.pos[1], B[2] - m.pos[2]};
        const double dA = norm3d(dA_v), dB = norm3d(dB_v);
        if (phase == 4) { // order 8 (ship-movement.md section 13.3): u = now - f+0x214 + 900 with the deadline 900 ms after the order
            const long u = m.t_ms - phase_t;
            if (u >= 900) {
                res.launched = true;
                break;
            }
            // pos = f+0x240 + fx(gate row2, s(u)), written absolutely; s = trunc(u^2 1665000 / 1e6) (+ v0 u / 1000: the P5 stub)
            const std::int32_t s = core::gate_launch_units(std::int32_t(v0), std::int32_t(u));
            for (int k = 0; k < 3; ++k) m.pos[k] = launch_start[k] + double(fx(std::llround(F.R[2][k] * 65536.0), s));
            m.speed = m.desired = 0;
            if (u > 0 && res.launch_first_v == 0) res.launch_first_v = double(s) / double(u) * 1000.0 / 500.0; // m/s over the first launch frame
            if (!keep) emit_trace_row(m, t.cls, obj, dt, false, 0, t.k_pct); // the DLL frees the slot at P5 for a kept speed only
            m.advance(dt); // the integrator still runs: a left remainder turns the ship, the slip decays (its offset is overwritten next frame)
            continue;
        }
        const int frame_phase = phase; // the leaf runs on this frame's order (0 and 2), before any transition below
        if (phase == 0) {
            if (res.t_cross < 0 && dA <= 0.75 * ru) { // the crossing of the arrival sphere
                res.t_cross = m.t_ms;
                res.cross_v = double(m.speed) / 500.0;
                res.cross_dA = dA / 500.0;
                for (int k = 0; k < 3; ++k) res.cross_pos[k] = m.pos[k] / 500.0;
                // the heading error: to the pass point for a small ship, to the gate axis (order 0x1f's target) for a capital
                double c = 0;
                for (int k = 0; k < 3; ++k) c += capital ? m.row[2][k] * F.R[2][k] : m.row[2][k] * dB_v[k] / dB;
                if (c > 1) c = 1;
                if (c < -1) c = -1;
                res.cross_err_deg = std::acos(c) * 180 / M_PI;
            }
            m.steer_call(obj, true, 0); // the leaf every frame (Run151)
            if (!vanilla && m.last_reason == core::Reason::handled) { // the aim direction's per-frame jump (the switch at the join point)
                double d[3] = {double(m.last_aim_P[0]) - m.pos[0] / 500.0, double(m.last_aim_P[1]) - m.pos[1] / 500.0, double(m.last_aim_P[2]) - m.pos[2] / 500.0};
                const double dn = norm3d(d);
                if (dn > 1) {
                    for (int k = 0; k < 3; ++k) d[k] /= dn;
                    if (have_aim) {
                        double c = d[0] * last_aim[0] + d[1] * last_aim[1] + d[2] * last_aim[2];
                        if (c > 1) c = 1;
                        if (c < -1) c = -1;
                        const double jump = std::acos(c) * 180 / M_PI;
                        if (jump > res.max_aim_jump_deg) res.max_aim_jump_deg = jump;
                    }
                    for (int k = 0; k < 3; ++k) last_aim[k] = d[k];
                    have_aim = true;
                }
            }
            if (m.t_ms >= m.next_think) {
                m.next_think = m.t_ms + m.rng.next() * 50 / 32768;
                if (dA <= 0.75 * ru) { // the arrival: P2 through the shared rule
                    m.desired = 0;
                    if (!keep) m.speed = 0;
                    res.arrived_A = true;
                    res.t_A = m.t_ms;
                    res.v_after_A = double(m.speed) / 500.0;
                    phase = 1;
                    phase_t = m.t_ms;
                    m.order = core::kOrderNone;
                } else { // the engine's speed law to a gate (section 10.3)
                    const double d_slow = 0.75 * ru + double(m.vmax) * double(m.vmax) / double(m.accel);
                    std::int64_t des = dA > d_slow ? m.vmax : Model::ramp(dA, 5000, d_slow, m.vmax / 2);
                    if (des < 5000) des = 5000;
                    if (dA < d_slow + 3 * ru + 2 * m.r_units) {
                        double c = 0;
                        for (int k = 0; k < 3; ++k) c += m.row[2][k] * dA_v[k] / dA;
                        des = c <= 0.7 ? 500 : Model::ramp(c - 0.7, 500, 0.3, des);
                    }
                    if (t.large && des > 50000) des = 50000;
                    m.desired = des;
                }
            }
        } else if (phase == 1) { // the KC's hand-over: one frame of order 0
            m.desired = 0;
            if (m.t_ms > phase_t) {
                phase = 2;
                m.order = core::kOrderDock;
                for (int k = 0; k < 3; ++k) m.P[k] = B[k];
            }
        } else if (phase == 2) { // order 5 every frame (section 10.5)
            m.steer_call(obj, false, 0);
            double c = 0;
            for (int k = 0; k < 3; ++k) c += m.row[2][k] * dB_v[k] / dB;
            if (c < 0.95) {
                m.desired = 500;
                ++res.misaligned_ticks;
            } else
                m.desired = dB <= ru / 5 ? m.vmax / 2 : (dB >= ru ? m.vmax : m.vmax / 2 + std::llround(double(m.vmax / 2) * (dB - ru / 5) / (ru - ru / 5)));
            if (dB <= double(m.speed) / 10.0 + m.r_units) { // the arrival: P4 through the shared rule
                m.desired = 0;
                v0 = keep ? m.speed : 0;
                if (!keep) m.speed = 0;
                res.arrived_B = true;
                res.t_B = m.t_ms;
                res.v0 = double(v0) / 500.0;
                phase = 3;
                phase_t = m.t_ms;
                m.order = core::kOrderNone;
            }
        } else if (phase == 3) {
            m.desired = 0;
            if (m.t_ms > phase_t) {
                phase = 4;
                m.order = core::kOrderWarpAway;
                for (int k = 0; k < 3; ++k) launch_start[k] = m.P[k] = double(std::llround(m.pos[k])); // f+0x240 (the row's `given`)
                phase_t = m.t_ms;
                res.t_launch = m.t_ms;
                continue; // the launch moves the ship from the next frame
            }
        }
        const std::int64_t des_engine = m.desired;
        m.clamp(obj); // site B every frame
        emit_trace_row(m, t.cls, obj, dt, frame_phase == 0 || frame_phase == 2, des_engine, t.k_pct);
        m.advance(dt);
        if (res.t_cross >= 0 && !res.arrived_B && double(m.speed) / 500.0 < res.min_v_after_cross) res.min_v_after_cross = double(m.speed) / 500.0;
        // the ring: during the approach to A (the lead-in's domain) the segment of this frame must not cross the gate
        // plane (local z = 0) inside the ring; the pass points lie on a 0.55 r arc, so the hole is at least that wide
        // (section 10.8 leaves the model's hole open) and the ring is [0.55 r, r]
        if (phase <= 1 || (phase == 2 && dock_start)) { // the approach, and the order-5 leg of the dock scenario
            double z0 = 0, z1 = 0;
            for (int k = 0; k < 3; ++k) {
                z0 += last_pos[k] * F.R[2][k];
                z1 += m.pos[k] * F.R[2][k];
            }
            if ((z0 < 0) != (z1 < 0) && z0 != z1) {
                const double f = z0 / (z0 - z1);
                double px = 0, py = 0;
                for (int k = 0; k < 3; ++k) {
                    const double p = last_pos[k] + f * (m.pos[k] - last_pos[k]);
                    px += p * F.R[0][k];
                    py += p * F.R[1][k];
                }
                const double lat = std::sqrt(px * px + py * py);
                if (lat >= 0.65 * ru && lat < ru) res.disc_crossed = true; // the hole: the 0.55 r pass arc plus the largest small hull
            }
        }
        for (int k = 0; k < 3; ++k) last_pos[k] = m.pos[k];
    }
    res.lead_used = m.gate_lead_used;
    return res;
}

// ---- the gate law's unit checks (review fixes 1 and 2) --------------------------------------------------------------
static void gate_unit_checks() {
    const float r = 1105.0f, G[3] = {0, 0, 0}, row0[3] = {1, 0, 0}, row1[3] = {0, 1, 0}, row2[3] = {0, 0, 1};
    const float A[3] = {0, -0.8f * r, -1.0f * r};
    core::GateGeometry g;
    check(core::gate_geometry(A, G, row0, row1, row2, r, &g) && std::fabs(g.h[1] - core::kGateSin) < 1e-5f && std::fabs(g.h[2] - core::kGateCos) < 1e-5f &&
              std::fabs(g.X[2] - (A[2] - 0.75f * r * core::kGateCos)) < 0.1f,
          "gate: the pinned geometry validates; h = (0, 0.2448, 0.9696) (14.17 deg: A toward the disc arc's centre), X = A - 0.75 r h");
    const float A_off[3] = {0.05f * r, -0.8f * r, -1.0f * r}, row_bad[3] = {0, 0, 2};
    core::GateGeometry g2;
    check(!core::gate_geometry(A_off, G, row0, row1, row2, r, &g2) && !core::gate_geometry(A, G, row0, row1, row_bad, r, &g2) && !core::gate_geometry(A, G, row0, row1, row2, 0.5f, &g2) && !core::gate_geometry(A, G, row1, row1, row2, r, &g2),
          "gate: A off by 5 % of r, a non-unit row, a tiny radius and a non-orthogonal frame are refused (the vanilla point)");
    float P[3], s = 0, e = 0;
    // beside, behind, above, laterally off at A's depth: s > 0, refused
    const float beside[3] = {3 * r, 0, 0}, behind[3] = {0, 0, 2 * r}, above[3] = {0, 3 * r, 0}, lateral_A[3] = {3 * r, -0.8f * r, -1.0f * r};
    bool refused = true;
    for (const float* x : {beside, behind, above})
        if (core::gate_lead_in(g, x, 100.0f, P, &s, &e) || !(s > 0.75f * r)) refused = false;
    const bool lat_ok = core::gate_lead_in(g, lateral_A, 100.0f, P, &s, &e) && std::fabs(P[0] - A[0]) < 1.0f && std::fabs(P[1] - A[1]) < 1.0f &&
                        std::fabs(P[2] - A[2]) < 1.0f;
    check(refused && lat_ok, "gate lead-in: beside, behind and above lie past the line's end (s > 0.75 r): aim at A; laterally off at A's depth: the line's end = A");
    // far before the join point: refused; on the line inside the window: the carrot ahead, never beyond A
    float far_x[3], mid[3], near[3];
    for (int k = 0; k < 3; ++k) {
        far_x[k] = g.X[k] - 4.0f * r * g.h[k];
        mid[k] = g.X[k] - 1.0f * r * g.h[k];
        near[k] = g.X[k] - 0.05f * r * g.h[k];
    }
    check(!core::gate_lead_in(g, far_x, 100.0f, P, &s, &e) && s < -3.9f * r, "gate lead-in: 4 r before the crossing: the straight approach (aim at A)");
    bool mid_ok = core::gate_lead_in(g, mid, 100.0f, P, &s, &e);
    float along = 0;
    for (int k = 0; k < 3; ++k) along += (P[k] - g.X[k]) * g.h[k];
    check(mid_ok && std::fabs(along - (-1.0f * r + 0.3f * r)) < 1.0f && e < 1.0f, "gate lead-in: 1 r before the crossing at 100 m/s: the carrot 0.3 r ahead on the line");
    mid_ok = core::gate_lead_in(g, near, 2000.0f, P, &s, &e);
    along = 0;
    for (int k = 0; k < 3; ++k) along += (P[k] - g.X[k]) * g.h[k];
    check(mid_ok && std::fabs(along - 0.75f * r) < 1.0f, "gate lead-in: a fast ship just before the crossing aims at A at most, never beyond");
    // the blended switch: at 3 r the carrot is A, at 2 r the near carrot
    float at3[3], at2[3];
    for (int k = 0; k < 3; ++k) {
        at3[k] = g.X[k] - 2.999f * r * g.h[k];
        at2[k] = g.X[k] - 2.0f * r * g.h[k];
    }
    core::gate_lead_in(g, at3, 100.0f, P, &s, &e);
    along = 0;
    for (int k = 0; k < 3; ++k) along += (P[k] - g.X[k]) * g.h[k];
    const bool at3_A = std::fabs(along - 0.75f * r) < 5.0f;
    core::gate_lead_in(g, at2, 100.0f, P, &s, &e);
    along = 0;
    for (int k = 0; k < 3; ++k) along += (P[k] - g.X[k]) * g.h[k];
    check(at3_A && std::fabs(along - (-2.0f * r + 0.3f * r)) < 1.0f, "gate lead-in: the carrot slides from A at the join point (3 r) to the near carrot at 2 r (a continuous switch)");
    // a ship laterally off the line inside the window: the carrot is on the line (e reported), never past A
    const float off[3] = {3 * r + g.X[0], g.X[1] - 2.0f * r * g.h[1], g.X[2] - 2.0f * r * g.h[2]};
    mid_ok = core::gate_lead_in(g, off, 100.0f, P, &s, &e);
    check(mid_ok && std::fabs(e - 3 * r) < 1.0f && std::fabs(P[0] - g.X[0]) < 0.01f, "gate lead-in: 3 r off the line at 2 r before the crossing: the carrot is on the line (the ship turns toward it)");
    // site B: the emergency ramp wins over the gate speed
    core::Ship ship{};
    ship.obj = 1;
    ship.order = core::kOrderFlyBefore;
    ship.flags = std::uint16_t(core::kShipFlagGate | core::kShipFlagClamp);
    ship.v_ref = 532.0f;
    ship.stamp_ms = 1000;
    const float x0[3] = {0, 0, 0};
    check(std::fabs(core::clamp_for(ship, x0, core::kOrderFlyBefore, 1100, 250) - 532.0f) < 0.01f, "site B: the gate approach returns the gate speed");
    ship.v_emergency = 25.0f;
    check(std::fabs(core::clamp_for(ship, x0, core::kOrderFlyBefore, 1100, 250) - 25.0f) < 0.01f, "site B: an avoidance emergency wins over the gate speed (25 m/s, not 532)");
    check(core::gate_speed_units(25.0f, 266000) == 12500 && core::gate_speed_units(25.0f, 12500) == 0, "site B: the gate write lowers to the emergency speed and writes nothing when equal");
    // the shared gate-site rule over a synthetic object
    FakeMem mem;
    const std::uint32_t ship_o = 0x2000, flight = 0x3000, gate = 0x4000, types = 0x5000, rock = 0x6000;
    mem.put(ship_o + 0x48, 7u | (2u << 16));
    mem.put(ship_o + 0x50, flight);
    mem.put(flight + 0x224, gate);
    mem.put(gate + 0x48, 0x12);
    mem.put(rock + 0x48, 17);
    mem.put(types + 2 * 0xdb8 + 0x14, 7); // M5
    check(core::gate_target_noncapital(mem, ship_o, types), "gate rule: a class-7 M5 with a class-0x12 target is a non-capital gate approach");
    mem.put(types + 2 * 0xdb8 + 0x14, 4); // M2
    check(!core::gate_target_noncapital(mem, ship_o, types), "gate rule: an M2 (capital mask) is refused");
    mem.put(types + 2 * 0xdb8 + 0x14, 7);
    mem.put(flight + 0x224, rock);
    check(!core::gate_target_noncapital(mem, ship_o, types), "gate rule: a non-gate target is refused");
    mem.put(flight + 0x224, gate);
    check(!core::gate_target_noncapital(mem, ship_o, 0) && !core::gate_target_noncapital(mem, 0, types), "gate rule: no type table or no object: refused");
    check(core::gate_stop_keeps_speed(true, true) && !core::gate_stop_keeps_speed(true, false) && !core::gate_stop_keeps_speed(false, true),
          "gate rule: the speed is kept only with a validated gate state (P2 fails closed like P4)");
    check(core::gate_launch_units(0, 900) == 1348650 && core::gate_launch_units(100000, 900) == 1348650 + 90000 && core::gate_launch_units(100000, -5) == 0,
          "P5: 1.665 t^2 from rest, + v0 t / 1000 with a kept speed, a negative t clamped");
    // order 5: the pass point must be the pinned arc point (|theta| <= 45 deg, 0.55 r in the gate's y-z plane); subtypes 5..8 vanilla
    {
        // the disc arc (amendment 10): (0.55 sin theta, -0.55 cos theta, -0.01) r; the old vertical-arc point is refused
        const float B0[3] = {0, -0.55f * r, -0.01f * r}, B45[3] = {0.389f * r, -0.389f * r, -0.01f * r}, Bvert[3] = {0, -0.389f * r, 0.389f * r},
                    Bz[3] = {0, -0.55f * r, 0.1f * r}, Bfar[3] = {0, -0.7f * r, -0.01f * r}, Bup[3] = {0, 0.55f * r, -0.01f * r}, B60[3] = {0.476f * r, -0.275f * r, -0.01f * r};
        const float B5[3] = {0, -0.05f * r, -0.01f * r};
        check(core::gate_pass_point_valid(B0, G, row0, row1, row2, r, 0) && core::gate_pass_point_valid(B45, G, row0, row1, row2, r, 2) &&
                  !core::gate_pass_point_valid(Bvert, G, row0, row1, row2, r, 0) && !core::gate_pass_point_valid(Bz, G, row0, row1, row2, r, 0) &&
                  !core::gate_pass_point_valid(Bfar, G, row0, row1, row2, r, 0) && !core::gate_pass_point_valid(Bup, G, row0, row1, row2, r, 0) &&
                  !core::gate_pass_point_valid(B60, G, row0, row1, row2, r, 0) && !core::gate_pass_point_valid(B0, G, row0, row1, row2, r, 5) &&
                  core::gate_pass_point_valid(B5, G, row0, row1, row2, r, 5) && !core::gate_pass_point_valid(B5, G, row0, row1, row2, r, 0) &&
                  !core::gate_pass_point_valid(B0, G, row0, row1, row2, 0.0f, 0),
              "order 5 validation: the disc arc passes (0.55 r for subtypes 0..4, 0.05 r for 5..8, |theta| <= 50 deg); the vertical arc, off the plane, off the radius, above the centre, 60 deg, the wrong arc and r = 0 are refused");
    }
    // order 5: the engine's ramp (r/5 -> vmax/2, r -> vmax) and the chord arc's ring test
    check(std::fabs(core::gate_dock_ramp(100.0f, 1105.0f, 124.0f) - 62.0f) < 0.01f && std::fabs(core::gate_dock_ramp(2000.0f, 1105.0f, 124.0f) - 124.0f) < 0.01f &&
              std::fabs(core::gate_dock_ramp(663.0f, 1105.0f, 124.0f) - 93.0f) < 0.1f,
          "order 5 ramp: vmax/2 inside r/5, vmax beyond r, linear between");
    {
        const float x0[3] = {0, 0, 0}, row0_[3] = {1, 0, 0}, row1_[3] = {0, 1, 0}, row2_[3] = {0, 0, 1};
        // a ship in front of the gate heading away from B: the chord arc to B (at 0.55 r in front, below) stays in front of the plane
        const float xs[3] = {0, -0.8f * r, -2.5f * r}, fwd[3] = {0, 0.8f, 0.6f}, Bf[3] = {0, -0.389f * r, -0.389f * r};
        check(core::gate_chord_clears_ring(xs, fwd, Bf, x0, row0_, row1_, row2_, r, 40.0f), "chord ring test: an arc that stays in front of the gate plane is clear");
        // a ship beside the gate on the plane, heading along the plane toward the far side: the arc crosses the plane inside the ring
        const float xb[3] = {1.2f * r, 0, 0.2f * r}, fb[3] = {-1, 0, 0}, Bb[3] = {0.4f * r, 0, -0.2f * r};
        check(!core::gate_chord_clears_ring(xb, fb, Bb, x0, row0_, row1_, row2_, r, 40.0f), "chord ring test: an arc through the ring band is blocked");
        // through the hole: a ship behind the gate on the axis heading at the gate, B just in front: clear (the hole)
        const float xh[3] = {0, 0, 1.5f * r}, fh[3] = {0, 0, -1}, Bh[3] = {0, 0.1f * r, -0.5f * r};
        check(core::gate_chord_clears_ring(xh, fh, Bh, x0, row0_, row1_, row2_, r, 40.0f), "chord ring test: a path through the hole is not the ring");
    }
}
// The Run152 follow-up: order 5 started 1.7 km from B with a 65 deg heading error (the measured start of the leg in
// flight). The engine creeps at 1 m/s until aligned; the module writes the chord-rule cornering speed, so the ship
// flies a curve to B without the speed dropping below 20 m/s and without sweeping the ring. TS and M6 from the
// class table; an M4 (Buster: 180 m/s, 25 m/s^2, 129 deg/s, 12 m, k 0.2 from Run151) for the fast small hull.
static void gate_dock_scenario(const ShipType& t, int fps, std::uint32_t seed) {
    char why[260];
    const double std_start[3] = {0, -0.8, -3.0};
    // the whole pass arc (amendment 9: the ship-gate pair is exempt from collision on order 5, the ring bound is visual),
    // the ordinary subtype and a subtype-5 gate (its pass offset 0.05 r)
    for (unsigned sub_i = 0; sub_i < 2; ++sub_i) {
        const std::uint16_t subtype = sub_i ? 5 : 0;
        for (double theta : {-45.0, 0.0, 45.0}) {
            if (subtype && theta != 0.0) continue;
            const GateResult g = run_gate(t, fps, seed, theta, false, false, true, std_start, kGateIdentity, 0.0, 9, -1, 1700.0, 65.0, subtype);
            const GateResult v = run_gate(t, fps, seed, theta, false, true, false, std_start, kGateIdentity, 0.0, 9, -1, 1700.0, 65.0, subtype);
            std::snprintf(why, sizeof why, "%s %d fps seed %u gate order-5 start 65 deg off at 1.7 km (theta %+.0f, subtype %u): reaches B and launches", t.cls, fps, seed, theta, subtype);
            check(g.arrived_B && g.launched, why);
            std::snprintf(why, sizeof why, "%s %d fps seed %u gate order-5 start 65 deg off (theta %+.0f, subtype %u): the speed never drops below 20 m/s (min %%.1f >= %%.0f)", t.cls, fps, seed, theta, subtype);
            checkf(g.min_v_after_cross >= 20.0, why, g.min_v_after_cross, 20.0);
            if (seed == kSeeds[0] && fps == 60)
                std::printf("gate dock %-3s fps=%3d theta %+3.0f sub=%u min_v=%.1f m/s (vanilla %.1f) t_B=%ld ms (vanilla %ld) creep_ticks=%d (vanilla %d) v0=%.1f ring=%d\n", t.cls,
                            fps, theta, subtype, g.min_v_after_cross, v.min_v_after_cross, g.t_B, v.t_B, g.misaligned_ticks, v.misaligned_ticks, g.v0, g.disc_crossed ? 1 : 0);
        }
    }
}
// ---- scenario 10: order 5 to a station port (ship-movement.md sections 9 and 13; amendment 12) ---------------------------
// The port at the origin, its orientation the level frame rolled by gamma about its forward axis (+z), the rendezvous point
// R 1.5 km in front of it on -z (section 9: the target is R while the ship is farther from the port than R is plus its
// radius, then the port). The ship starts at its approach speed leg_s seconds of flight before R on a line approach_deg
// off the port's axis, heading at R, level: the match engages at once, the roll is still turning when the target switches
// at R and the bearing error jumps by about approach_deg (above the match limit 7.2 deg, below the hold's 14.4 deg: the
// previous handler drops the match there and cuts the roll). Order 5 every frame: the leaf (the module
// or the vanilla 0x0044ccc0), then the engine's port-roll match (Model::port_match); the engine's speed law (500 units/s while
// forward . direction < 0.95, else a ramp from 5000 at the final zone to min(vmax, 75000 or 50000 by class) 1 km beyond
// it, [i]); site B. The final zone (distance <= 2 r + 25000 units, 0x004543da): no leaf call, remainders, speed and slip
// 0, the kinematic turn toward the port's orientation at 0x38e or 0x71c binary/s by class with the step's Euler angles
// in the rate fields (unsigned), the glide toward the port at a ramp 500 .. min(vmax, 20000) units/s ([i]); docked at
// 0x22b0 units with the turn finished.
static unsigned class_code(const ShipType& t) {
    for (unsigned i = 0; i < core::kClassCount; ++i)
        if (!std::strcmp(core::kClassNames[i], t.cls)) return i;
    return 0;
}
static bool slow_turn_code(unsigned code) { // the jump table at 0x004542c4: 0x38e binary/s (5 deg/s), else 0x71c
    return code == 0 || code == 1 || code == 3 || code == 4 || code == 8 || code == 9 || (code >= 0xb && code <= 0xf);
}
static void euler_of(const double m[3][3], double out[3]) { // the builder 0x004f0270's angles: yaw, pitch, roll (rad)
    out[0] = std::atan2(-m[2][0], m[2][2]);
    out[1] = std::asin(std::fmax(-1.0, std::fmin(1.0, m[2][1])));
    out[2] = std::atan2(m[0][1], m[1][1]);
}
struct DockResult {
    bool docked, final_zone;
    int engagements, toggles, roll_cuts, match_end_stops, nonmonotone, leaf_frames, matched_frames;
    double gamma_err_at_zone_deg, max_gamma_rise_deg, roll_start_err_deg;
    long t_engage, t_zone, t_docked;
};
static DockResult run_dock(const ShipType& t, int fps, std::uint32_t seed, double gamma_deg, bool amend12, bool vanilla, double leg_s = 2.5,
                           double approach_deg = 10.0, std::uint32_t obj = 0x6000) {
    core::ShipTable table{};
    Model m;
    m.init(t, seed);
    m.table = &table;
    m.vanilla = vanilla;
    m.amend12 = amend12;
    m.every_frame = true;
    m.port_target = true;
    m.trace_tgt_class = 6;
    m.order = core::kOrderDock;
    const double g = gamma_deg * M_PI / 180;
    const double port[3][3] = {{std::cos(g), std::sin(g), 0}, {-std::sin(g), std::cos(g), 0}, {0, 0, 1}};
    m.port_gamma = std::int64_t(std::floor(g * 65536.0 / (2 * M_PI)));
    const unsigned code = class_code(t);
    const std::int64_t v_cap = std::min<std::int64_t>(m.vmax, slow_turn_code(code) ? 75000 : 50000);
    const double rdv = 1500.0 * 500, phi = approach_deg * M_PI / 180, leg_m = leg_s * double(v_cap) / 500.0;
    const double R[3] = {0, 0, -rdv};
    m.speed = m.desired = v_cap;
    m.pos[0] = R[0] - leg_m * 500 * std::sin(phi);
    m.pos[1] = 0;
    m.pos[2] = R[2] - leg_m * 500 * std::cos(phi);
    for (int k = 0; k < 3; ++k) m.P[k] = R[k];
    double to[3] = {R[0] - m.pos[0], R[1] - m.pos[1], R[2] - m.pos[2]};
    const double tn = norm3d(to);
    for (int k = 0; k < 3; ++k) to[k] /= tn;
    const double f[3] = {to[0], 0, to[2]}; // heading at R, level
    for (int k = 0; k < 3; ++k) m.row[2][k] = f[k];
    m.row[1][0] = 0;
    m.row[1][1] = 1;
    m.row[1][2] = 0;
    m.row[0][0] = m.row[1][1] * f[2] - m.row[1][2] * f[1]; // row0 = row1 x row2
    m.row[0][1] = m.row[1][2] * f[0] - m.row[1][0] * f[2];
    m.row[0][2] = m.row[1][0] * f[1] - m.row[1][1] * f[0];
    const double turn_cap = double(slow_turn_code(code) ? 0x38e : 0x71c) * core::kRadPerBinary; // rad/s
    const double zone = 2.0 * m.r_units + 25000.0;
    DockResult res{};
    res.t_engage = res.t_zone = res.t_docked = -1;
    Clock clock;
    bool zone_on = false, was_match = false, engaged = false;
    std::int64_t last_gerr = -1;
    for (long i = 0; i < 300L * fps; ++i) {
        const long dt = clock.step(fps);
        if (!dt) continue;
        m.frame_dt_ms = dt;
        const double d_port = std::sqrt(m.pos[0] * m.pos[0] + m.pos[1] * m.pos[1] + m.pos[2] * m.pos[2]);
        if (d_port <= rdv + m.r_units) // the rendezvous point, then the port (f+0x230, f+0x240)
            for (int k = 0; k < 3; ++k) m.P[k] = 0;
        const double d = m.dist();
        if (!zone_on && d_port <= zone) {
            zone_on = res.final_zone = true;
            res.t_zone = m.t_ms;
            res.gamma_err_at_zone_deg = std::fabs(double(wrap16(m.port_gamma - m.gamma_binary()))) * 360.0 / 65536.0;
        }
        const std::int64_t w_roll_before = m.w[2];
        bool leaf = false;
        if (!zone_on) {
            m.steer_call(obj, false, 0);
            leaf = true;
            ++res.leaf_frames;
            if (m.match_now) ++res.matched_frames;
            if (m.match_now && !was_match) {
                if (!engaged) {
                    engaged = true;
                    res.t_engage = m.t_ms;
                    res.roll_start_err_deg = std::fabs(double(wrap16(m.port_gamma - m.gamma_binary()))) * 360.0 / 65536.0;
                } else
                    ++res.toggles; // a re-engagement
                ++res.engagements;
            } else if (!m.match_now && was_match)
                ++res.toggles; // a disengagement
            was_match = m.match_now;
            if (std::getenv("X3M_SS_DOCK") && fps == 60 && seed == kSeeds[0])
                std::printf("dockdbg %s a12=%d van=%d t=%ld d_port=%.0f m theta=%.2f deg e=%lld,%lld,%lld match=%d w=%lld,%lld,%lld gerr=%lld v=%lld\n", t.cls,
                            amend12 ? 1 : 0, vanilla ? 1 : 0, m.t_ms, d_port / 500, m.bearing_deg(), (long long)m.e[0], (long long)m.e[1], (long long)m.e[2],
                            m.match_now ? 1 : 0, (long long)m.w[0], (long long)m.w[1], (long long)m.w[2], (long long)wrap16(m.port_gamma - m.gamma_binary()),
                            (long long)m.speed);
            double c = 0;
            for (int k = 0; k < 3; ++k) c += m.row[2][k] * (m.P[k] - m.pos[k]) / d;
            const double far = zone + 1000.0 * 500;
            m.desired = c < 0.95 ? 500 : (d_port >= far ? v_cap : 5000 + std::llround(double(v_cap - 5000) * (d_port - zone) / (far - zone)));
            m.clamp(obj);
        } else { // the final zone (pass A): the kinematic turn and the glide
            for (int k = 0; k < 3; ++k) {
                m.e[k] = 0;
                m.slip[k] = 0;
            }
            m.speed = m.desired = 0;
            double Q[3][3] = {}; // Q = sum_i port_i (x) row_i: the world rotation taking the ship's rows onto the port's
            for (int r = 0; r < 3; ++r)
                for (int a = 0; a < 3; ++a)
                    for (int b = 0; b < 3; ++b) Q[a][b] += port[r][a] * m.row[r][b];
            const double cth = std::fmax(-1.0, std::fmin(1.0, (Q[0][0] + Q[1][1] + Q[2][2] - 1.0) / 2.0));
            const double ang = std::acos(cth);
            double w_step[3] = {0, 0, 0};
            if (ang > 1e-9) {
                double ax[3] = {Q[2][1] - Q[1][2], Q[0][2] - Q[2][0], Q[1][0] - Q[0][1]};
                const double an = norm3d(ax);
                if (an > 1e-12) {
                    for (int k = 0; k < 3; ++k) ax[k] /= an;
                    const double s = std::fmin(turn_cap * double(dt) / 1000.0, ang);
                    double old_rows[3][3];
                    std::memcpy(old_rows, m.row, sizeof old_rows);
                    const double cs = std::cos(s), sn = std::sin(s);
                    for (int r = 0; r < 3; ++r) { // Rodrigues about ax
                        const double* v = old_rows[r];
                        const double dv = ax[0] * v[0] + ax[1] * v[1] + ax[2] * v[2];
                        const double cr[3] = {ax[1] * v[2] - ax[2] * v[1], ax[2] * v[0] - ax[0] * v[2], ax[0] * v[1] - ax[1] * v[0]};
                        for (int k = 0; k < 3; ++k) m.row[r][k] = v[k] * cs + cr[k] * sn + ax[k] * dv * (1 - cs);
                    }
                    double L[3][3]; // the step in the ship's previous frame: L[i][j] = new_row_i . old_row_j
                    for (int a = 0; a < 3; ++a)
                        for (int b = 0; b < 3; ++b) L[a][b] = m.row[a][0] * old_rows[b][0] + m.row[a][1] * old_rows[b][1] + m.row[a][2] * old_rows[b][2];
                    euler_of(L, w_step);
                } else
                    std::memcpy(m.row, port, sizeof m.row);
            }
            for (int k = 0; k < 3; ++k) m.w[k] = std::llround(w_step[k] * 65536.0 / (2 * M_PI)) & 0xffff; // stored unsigned (0x0045463f)
            const double v_g = 500.0 + (double(std::min<std::int64_t>(m.vmax, 20000)) - 500.0) * std::fmin(1.0, std::fmax(0.0, (d - 0x22b0) / (zone - 0x22b0)));
            const double stepu = std::fmin(d, v_g * double(dt) / 1000.0);
            for (int k = 0; k < 3; ++k) m.pos[k] = double(std::llround(m.pos[k] + (m.P[k] - m.pos[k]) / d * stepu));
            if (d <= 0x22b0 && ang <= 1e-9) {
                res.docked = true;
                res.t_docked = m.t_ms;
                break;
            }
        }
        emit_trace_row(m, t.cls, obj, dt, leaf, m.desired, t.k_pct);
        m.advance(dt);
        // a one-frame roll cut: a roll rate of more than two frames' braking set to 0 in one frame on the approach, where the
        // module's roll stands (the match did not run); with the match running it is the engine's own stop as the port roll
        // is reached (its remainder wrap(port - ship) becomes 0: row 8), counted apart
        if (leaf && std::llabs(w_roll_before) > 2 * m.acc[2] * dt / 1000 && m.w[2] == 0) ++(m.match_now ? res.match_end_stops : res.roll_cuts);
        if (leaf && engaged) { // the roll error to the port after the first engagement: non-increasing (2 binary of Euler coupling)
            const std::int64_t gerr = std::llabs(wrap16(m.port_gamma - m.gamma_binary()));
            if (last_gerr >= 0 && gerr > last_gerr + 2) {
                ++res.nonmonotone;
                res.max_gamma_rise_deg = std::fmax(res.max_gamma_rise_deg, double(gerr - last_gerr) * 360.0 / 65536.0);
            }
            last_gerr = gerr;
        }
    }
    return res;
}
static void dock_scenario(const ShipType& t, int fps, std::uint32_t seed) {
    char why[300];
    const DockResult a = run_dock(t, fps, seed, 30.0, true, false);
    const DockResult o = run_dock(t, fps, seed, 30.0, false, false); // the handler before amendment 12
    const DockResult v = run_dock(t, fps, seed, 30.0, false, true);  // the vanilla leaf
    std::snprintf(why, sizeof why, "%s %d fps seed %u dock at a port rolled 30 deg (amendment 12): the match engages once and stays engaged "
                                   "(engagements %%.0f, toggles %%.0f)", t.cls, fps, seed);
    checkf(a.engagements == 1 && a.toggles == 0, why, double(a.engagements), double(a.toggles));
    std::snprintf(why, sizeof why, "%s %d fps seed %u dock (amendment 12): no one-frame roll cut on the approach (%%.0f), the roll error to the "
                                   "port non-increasing after the engagement (%%.0f rises)", t.cls, fps, seed);
    checkf(a.roll_cuts == 0 && a.nonmonotone == 0, why, double(a.roll_cuts), double(a.nonmonotone));
    std::snprintf(why, sizeof why, "%s %d fps seed %u dock (amendment 12): the roll reaches the port's within 0.5 deg before the final zone "
                                   "(%%.2f deg), docked (%%.0f)", t.cls, fps, seed);
    checkf(a.final_zone && a.gamma_err_at_zone_deg <= 0.5 && a.docked, why, a.gamma_err_at_zone_deg, a.docked ? 1.0 : 0.0);
    std::snprintf(why, sizeof why, "%s %d fps seed %u dock: the previous handler and the vanilla leaf dock too (%%.0f, %%.0f)", t.cls, fps, seed);
    checkf(o.docked && v.docked, why, o.docked ? 1.0 : 0.0, v.docked ? 1.0 : 0.0);
    if (seed == kSeeds[0] && fps == 60)
        std::printf("dock %-3s fps=%3d gamma=30 engagements=%d toggles=%d roll_cuts=%d match_end_stops=%d nonmonotone=%d err_at_zone=%.2f deg "
                    "t_zone=%ld t_docked=%ld | previous handler: engagements=%d toggles=%d roll_cuts=%d nonmonotone=%d err_at_zone=%.2f "
                    "t_docked=%ld | vanilla: engagements=%d toggles=%d roll_cuts=%d err_at_zone=%.2f t_docked=%ld\n",
                    t.cls, fps, a.engagements, a.toggles, a.roll_cuts, a.match_end_stops, a.nonmonotone, a.gamma_err_at_zone_deg, a.t_zone,
                    a.t_docked, o.engagements, o.toggles, o.roll_cuts, o.nonmonotone, o.gamma_err_at_zone_deg, o.t_docked, v.engagements,
                    v.toggles, v.roll_cuts, v.gamma_err_at_zone_deg, v.t_docked);
}
// Amendment 12's A/B over the existing scenarios (the handler with and without the port hold and the smooth stops): the
// gate legs (order 4 to a gate, order 5 to a gate: no port, no stopped axis) must be identical; the 180 degree turn and the
// MovePos arrival are reported and held to the rounding level (two frames).
static bool same_gate(const GateResult& a, const GateResult& b) {
    return a.arrived_A == b.arrived_A && a.arrived_B == b.arrived_B && a.launched == b.launched && a.t_cross == b.t_cross && a.t_A == b.t_A &&
           a.t_B == b.t_B && a.t_launch == b.t_launch && a.cross_v == b.cross_v && a.cross_err_deg == b.cross_err_deg &&
           a.min_v_after_cross == b.min_v_after_cross && a.v0 == b.v0 && a.misaligned_ticks == b.misaligned_ticks && a.cross_pos[0] == b.cross_pos[0] &&
           a.cross_pos[1] == b.cross_pos[1] && a.cross_pos[2] == b.cross_pos[2];
}
static void amend12_ab_checks() {
    char why[260];
    const double std_start[3] = {-3000.0 / 1105.0 * std::sin(30 * M_PI / 180), -0.8, -1.0 - 3000.0 / 1105.0 * std::cos(30 * M_PI / 180)};
    const double dock_start[3] = {0, -0.8, -3.0};
    long worst_turn = 0, worst_arrival = 0;
    int turn_changed = 0, arrival_changed = 0, runs = 0;
    for (const ShipType& t : kShips) {
        if (!std::strcmp(t.cls, "M2")) continue; // the capital transit is capital_transit_scenario (amendment 13); run_gate steers the small ships only
        for (int fps : kFps) {
            const std::uint32_t seed = kSeeds[0];
            g_amend12 = false;
            const GateResult g0 = run_gate(t, fps, seed, 45, false, false, true, std_start, kGateIdentity, 0.0, 9);
            const GateResult d0 = run_gate(t, fps, seed, 0, false, false, true, dock_start, kGateIdentity, 0.0, 9, -1, 1700.0, 65.0);
            g_amend12 = true;
            const GateResult g1 = run_gate(t, fps, seed, 45, false, false, true, std_start, kGateIdentity, 0.0, 9);
            const GateResult d1 = run_gate(t, fps, seed, 0, false, false, true, dock_start, kGateIdentity, 0.0, 9, -1, 1700.0, 65.0);
            std::snprintf(why, sizeof why, "%s %d fps amendment 12 leaves the gate legs unchanged: the approach (t_A %%.0f) and the order-5 leg (t_B %%.0f) "
                                           "identical with and without it", t.cls, fps);
            checkf(same_gate(g0, g1) && same_gate(d0, d1), why, double(g1.t_A - g0.t_A), double(d1.t_B - d0.t_B));
            if (std::getenv("X3M_SS_AB"))
                std::printf("ab %s %d: g cross_err %.6f/%.6f cross_pos %.3f,%.3f,%.3f / %.3f,%.3f,%.3f min_v %.4f/%.4f mis %d/%d | d t_B %ld/%ld min_v %.4f/%.4f mis %d/%d\n",
                            t.cls, fps, g0.cross_err_deg, g1.cross_err_deg, g0.cross_pos[0], g0.cross_pos[1], g0.cross_pos[2], g1.cross_pos[0],
                            g1.cross_pos[1], g1.cross_pos[2], g0.min_v_after_cross, g1.min_v_after_cross, g0.misaligned_ticks, g1.misaligned_ticks,
                            d0.t_B, d1.t_B, d0.min_v_after_cross, d1.min_v_after_cross, d0.misaligned_ticks, d1.misaligned_ticks);
        }
    }
    for (const ShipType& t : kShips)
        for (int fps : kFps) {
            const long frame = 1000 / fps + 1;
            g_amend12 = false;
            const TurnResult t0 = run_turn(t, fps, kSeeds[0], false, false);
            const ArrivalResult a0 = run_arrival(t, fps, kSeeds[0], false);
            g_amend12 = true;
            const TurnResult t1 = run_turn(t, fps, kSeeds[0], false, false);
            const ArrivalResult a1 = run_arrival(t, fps, kSeeds[0], false);
            ++runs;
            const long dt = std::labs(t1.settle_ms - t0.settle_ms), da = std::labs(a1.t_arrive - a0.t_arrive);
            if (dt) ++turn_changed;
            if (da || a1.min_d != a0.min_d) ++arrival_changed;
            worst_turn = std::max(worst_turn, dt);
            worst_arrival = std::max(worst_arrival, da);
            std::snprintf(why, sizeof why, "%s %d fps amendment 12's smooth stops: the 180 deg turn settles within two frames of the previous handler "
                                           "(%%.0f ms) and the arrival time too (%%.0f ms)", t.cls, fps);
            checkf(dt <= 2 * frame && da <= 2 * frame && t1.overshoot <= t0.overshoot + 0.01 && a1.done == a0.done && a1.reversals == a0.reversals, why,
                   double(dt), double(da));
        }
    std::printf("amendment 12 A/B: gate legs identical; 180 deg turn settle changed in %d of %d runs (max %ld ms), arrival changed in %d of %d "
                "(max %ld ms)\n", turn_changed, runs, worst_turn, arrival_changed, runs, worst_arrival);
}
static void capital_transit_scenario(const ShipType& t, int fps, std::uint32_t seed);
static void gate_scenario(const ShipType& t, int fps, std::uint32_t seed) {
    char why[260];
    const bool capital = !std::strcmp(t.cls, "M2");
    const double thetas[3] = {-45, 0, 45};
    // the standard approach: 3 km before A, 30 deg off the axis (local coordinates in r)
    const double std_start[3] = {-3000.0 / 1105.0 * std::sin(30 * M_PI / 180), -0.8, -1.0 - 3000.0 / 1105.0 * std::cos(30 * M_PI / 180)};
    for (double theta : thetas) {
        if (capital) break; // the capital's cases follow (its point is a grid slot, its start the 6 km off-axis one)
        const GateResult v = run_gate(t, fps, seed, theta, capital, true, false, std_start, kGateIdentity);
        const GateResult g = run_gate(t, fps, seed, theta, capital, false, true, std_start, kGateIdentity);
        std::snprintf(why, sizeof why, "%s %d fps seed %u gate theta %+.0f: arrived at A and B and launched (vanilla too), the disc never crossed", t.cls, fps, seed, theta);
        check(g.arrived_A && g.arrived_B && g.launched && v.arrived_A && v.arrived_B && v.launched && !g.disc_crossed && !v.disc_crossed, why);
        std::snprintf(why, sizeof why, "%s %d fps seed %u gate theta %+.0f: crossing speed %%.1f m/s >= 0.5 vmax (%%.1f)", t.cls, fps, seed, theta);
        checkf(g.cross_v >= 0.5 * t.v_ms, why, g.cross_v, 0.5 * t.v_ms);
        // amendment 10: B lies on the disc, so the crossing error to it is lateral (13.0 deg on the line, 20.7 with the spread)
        // and the order-5 cornering absorbs it: the leg never slows to the engine's creep
        std::snprintf(why, sizeof why, "%s %d fps seed %u gate theta %+.0f: heading error to B at the crossing %%.1f deg <= 22 (%%.1f)", t.cls, fps, seed, theta);
        check(g.cross_err_deg >= 0, why);
        slip_gapf(g.cross_err_deg <= 22.0, why, g.cross_err_deg, 22.0);
        std::snprintf(why, sizeof why, "%s %d fps seed %u gate theta %+.0f: the order-5 leg corners instead of creeping (min speed %%.1f >= 20)", t.cls, fps, seed, theta);
        checkf(g.min_v_after_cross >= 20.0, why, g.min_v_after_cross, 20.0);
        std::snprintf(why, sizeof why, "%s %d fps seed %u gate theta %+.0f: no stop between the crossing and the pass point (min speed %%.1f m/s >= 0.25 vmax %%.1f)", t.cls, fps, seed, theta);
        checkf(g.min_v_after_cross >= 0.25 * t.v_ms, why, g.min_v_after_cross, 0.25 * t.v_ms);
        std::snprintf(why, sizeof why, "%s %d fps seed %u gate theta %+.0f: the launch continues the pass-point speed (v0 %%.1f m/s, first launch frame %%.1f m/s)", t.cls, fps, seed, theta);
        checkf(g.v0 > 0 && g.launch_first_v >= 0.95 * g.v0, why, g.v0, g.launch_first_v);
        if (seed == kSeeds[0] && fps == 60)
            std::printf("gate theta %+3.0f %-3s %-10s fps=%3d cross_v=%.1f m/s (vanilla %.1f) heading_err=%.1f deg (vanilla %.1f) creep_ticks=%d (vanilla %d) "
                        "min_v=%.1f v0=%.1f (vanilla %.1f) t_cross->launch=%ld ms (vanilla %ld) lead_in=%d aim_jump=%.1f deg\n",
                        theta, t.cls, t.name, fps, g.cross_v, v.cross_v, g.cross_err_deg, v.cross_err_deg, g.misaligned_ticks, v.misaligned_ticks,
                        g.min_v_after_cross, g.v0, v.v0, g.t_launch - g.t_cross, v.t_launch - v.t_cross, g.lead_used ? 1 : 0, g.max_aim_jump_deg);
    }
    if (capital) { // amendment 13: the capital's gate transit is capital_transit_scenario (the Boreas record too)
        capital_transit_scenario(t, fps, seed);
        return;
    }
    // the spread: four ships on the same vanilla line (distinct ring positions from their object pointers) cross at distinct
    // points >= 2 r_ship apart, each within 0.75 r of A and within 18.2 deg of its pass point (theta -45: the worst arc end)
    {
        std::uint32_t ids[4];
        unsigned rings[4], n = 0;
        for (std::uint32_t id = 0x381f0000; n < 4; id += 0x1c8) {
            const unsigned k = core::gate_spread_index(id);
            bool dup = false;
            for (unsigned j = 0; j < n; ++j) dup = dup || rings[j] == k;
            if (!dup) {
                ids[n] = id;
                rings[n++] = k;
            }
        }
        GateResult g[4];
        bool ok = true;
        double min_sep = 1e18, max_dA = 0, max_err = 0;
        for (unsigned i = 0; i < 4; ++i) {
            g[i] = run_gate(t, fps, seed, -45, false, false, true, std_start, kGateIdentity, 0.0, ids[i]);
            ok = ok && g[i].arrived_A && g[i].arrived_B && g[i].launched && g[i].min_v_after_cross >= 20.0 && g[i].ring == rings[i];
            if (g[i].cross_dA > max_dA) max_dA = g[i].cross_dA;
            if (g[i].cross_err_deg > max_err) max_err = g[i].cross_err_deg;
            const double o = std::sqrt(g[i].off[0] * g[i].off[0] + g[i].off[1] * g[i].off[1]);
            ok = ok && std::fabs(o - core::kGateSpreadR * 1105.0) < 1.0;
        }
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned j = i + 1; j < 4; ++j) {
                const double d[3] = {g[i].cross_pos[0] - g[j].cross_pos[0], g[i].cross_pos[1] - g[j].cross_pos[1], g[i].cross_pos[2] - g[j].cross_pos[2]};
                const double sep = norm3d(d);
                if (sep < min_sep) min_sep = sep;
            }
        std::snprintf(why, sizeof why, "%s %d fps seed %u gate spread: four ships cross >= 2 r_ship apart (min %%.0f m >= %%.0f), each within 0.75 r of A and <= 24 deg to its pass point (20.7 geometric + the servo lag)", t.cls, fps, seed);
        checkf(ok && min_sep >= 2.0 * t.r_m && max_dA <= 0.75 * 1105.0 && max_err <= 24.0, why, min_sep, 2.0 * t.r_m);
        if (seed == kSeeds[0] && fps == 60)
            std::printf("gate spread %-3s fps=%3d rings=%u,%u,%u,%u min_sep=%.0f m max_dA=%.0f m max_heading_err=%.1f deg\n", t.cls, fps, rings[0], rings[1], rings[2], rings[3],
                        min_sep, max_dA, max_err);
        // the exit spread through the shared site-W rule (core::gate_warp_in_offset, the DLL handler's decision and write
        // value) on a synthetic ship and gate in a FakeMem: the written position carries the ring offset in the gate plane,
        // the case's copy (f+0x240 = phys+0x30) and its write (copy + 1348650 x row 2) keep it, so two ships warping in a
        // second apart end >= 2 r_ship apart laterally; the player's ship (no autopilot flag), a capital record, a ship
        // already on order 0x21 and a non-gate target get nothing
        FakeMem wm;
        const std::uint32_t gate = 0x4000, gphys = 0x4800, types = 0x5000, player = 0x7000;
        wm.put(gate + 0x48, 0x12);
        wm.put(gate + 0x70, gphys);
        wm.puts(gate + 0xa4, 552500); // r = 1105 m
        wm.puts(gphys + 0x40, 0x10000);
        wm.puts(gphys + 0x54, 0x10000);
        wm.puts(gphys + 0x68, 0x10000);
        double final_pos[2][3];
        const unsigned code = !std::strcmp(t.cls, "M5") ? 7 : !std::strcmp(t.cls, "M3") ? 5 : !std::strcmp(t.cls, "M6") ? 11 : 1;
        wm.put(types + 3 * 0xdb8 + 0x14, code);
        bool exit_ok = true;
        for (unsigned i = 0; i < 2; ++i) {
            const std::uint32_t ship = ids[i], flight = ids[i] + 0x1000, phys = ids[i] + 0x2000;
            wm.put(ship + 0x48, 7u | (3u << 16));
            wm.put(ship + 0x40, 0x80000); // autopilot
            wm.put(ship + 0x50, flight);
            wm.put(ship + 0x70, phys);
            wm.put(flight + 0x258, 5);
            wm.puts(phys + 0x30, 1000);
            wm.puts(phys + 0x34, -2000);
            wm.puts(phys + 0x38, 300000);
            std::int32_t pos[3];
            const bool ok = core::gate_warp_in_offset(wm, ship, gate, types, 0, pos);
            float off[3];
            const float r0[3] = {1, 0, 0}, r1[3] = {0, 1, 0};
            core::gate_spread_offset(rings[i], core::kGateExitSpreadR * 1105.0f, r0, r1, off);
            exit_ok = exit_ok && ok && std::abs(pos[0] - (1000 + core::round_to_int(off[0] * 500))) <= 1 && std::abs(pos[1] - (-2000 + core::round_to_int(off[1] * 500))) <= 1 &&
                      pos[2] == 300000;
            // the DLL writes pos; the case then copies it into f+0x240 and writes phys+0x30 = copy + 1348650 x row 2
            for (int k = 0; k < 3; ++k) wm.puts(phys + 0x30 + 4 * k, pos[k]);
            for (int k = 0; k < 3; ++k) wm.puts(flight + 0x240 + 4 * k, wm.s32(phys + 0x30 + 4 * k));
            for (int k = 0; k < 3; ++k) final_pos[i][k] = (wm.s32(flight + 0x240 + 4 * k) + (k == 2 ? 1348650 : 0)) / 500.0;
            // a second SA_WarpObjectIn while on order 0x21: no further offset
            wm.put(flight + 0x258, 0x21);
            exit_ok = exit_ok && !core::gate_warp_in_offset(wm, ship, gate, types, 0, pos);
            wm.put(flight + 0x258, 5);
            exit_ok = exit_ok && !core::gate_warp_in_offset(wm, ship, gate, types, 1u << code, pos); // the class excluded
        }
        const double lat = std::sqrt((final_pos[0][0] - final_pos[1][0]) * (final_pos[0][0] - final_pos[1][0]) + (final_pos[0][1] - final_pos[1][1]) * (final_pos[0][1] - final_pos[1][1]));
        wm.put(player + 0x48, 7u | (3u << 16));
        wm.put(player + 0x40, 0); // the player's ship: no autopilot flag
        wm.put(player + 0x50, player + 0x1000);
        wm.put(player + 0x70, player + 0x2000);
        wm.put(player + 0x1000 + 0x258, 5);
        std::int32_t pp[3];
        const bool player_skipped = !core::gate_warp_in_offset(wm, player, gate, types, 0, pp);
        wm.put(player + 0x40, 0x80000 | 0x8000000); // deleted
        const bool deleted_skipped = !core::gate_warp_in_offset(wm, player, gate, types, 0, pp);
        wm.put(types + 3 * 0xdb8 + 0x14, 4); // M2: capital
        wm.put(player + 0x40, 0x80000);
        const bool capital_skipped = !core::gate_warp_in_offset(wm, player, gate, types, 0, pp);
        wm.put(types + 3 * 0xdb8 + 0x14, code);
        wm.put(gate + 0x48, 17);
        const bool rock_skipped = !core::gate_warp_in_offset(wm, player, gate, types, 0, pp);
        wm.put(gate + 0x48, 0x12);
        std::snprintf(why, sizeof why, "%s %d fps seed %u gate exit spread (site W rule): two ships a second apart end >= 2 r_ship apart laterally (%%.0f m >= %%.0f); no offset for the player, a deleted ship, a capital, a non-gate, a re-issued warp-in, an excluded class", t.cls, fps, seed);
        checkf(exit_ok && lat >= 2.0 * t.r_m && player_skipped && deleted_skipped && capital_skipped && rock_skipped, why, lat, 2.0 * t.r_m);
    }
    // the far start (6 km before A, 20 deg off the axis: the straight approach, the blended switch at 3 r, the lead-in),
    // in the rotated gate frame (axis +x, up +z)
    {
        const double far_start[3] = {-6000.0 / 1105.0 * std::sin(20 * M_PI / 180), -0.8, -1.0 - 6000.0 / 1105.0 * std::cos(20 * M_PI / 180)};
        const GateResult g = run_gate(t, fps, seed, 0, false, false, true, far_start, kGateRotated);
        const GateResult v = run_gate(t, fps, seed, 0, false, true, false, far_start, kGateRotated);
        std::snprintf(why, sizeof why, "%s %d fps seed %u gate far start, rotated frame: crossing %%.1f m/s >= 0.5 vmax, the leg corners, no disc, launched (heading error %%.1f deg)", t.cls, fps, seed);
        checkf(g.arrived_A && g.arrived_B && g.launched && !g.disc_crossed && g.cross_v >= 0.5 * t.v_ms && g.cross_err_deg >= 0 &&
                   g.min_v_after_cross >= 20.0 && g.lead_used,
               why, g.cross_v, g.cross_err_deg);
        std::snprintf(why, sizeof why, "%s %d fps seed %u gate far start, rotated frame: heading error at the crossing %%.1f deg <= 22 (%%.0f)", t.cls, fps, seed);
        slip_gapf(g.cross_err_deg <= 22.0, why, g.cross_err_deg, 22.0);
        std::snprintf(why, sizeof why, "%s %d fps seed %u gate far start: the aim direction never jumps more than 12 deg between frames (%%.1f <= %%.1f)", t.cls, fps, seed);
        checkf(g.max_aim_jump_deg <= 12.0, why, g.max_aim_jump_deg, 12.0);
        std::snprintf(why, sizeof why, "%s %d fps seed %u gate far start: at A within 1.3 x the vanilla time (%%.0f ms <= %%.0f)", t.cls, fps, seed);
        checkf(g.t_A >= 0 && v.t_A >= 0 && double(g.t_A) <= 1.3 * double(v.t_A) + 500, why, double(g.t_A), 1.3 * double(v.t_A) + 500);
        if (seed == kSeeds[0] && fps == 60)
            std::printf("gate far/rot %-3s fps=%3d cross_v=%.1f heading_err=%.1f creep=%d t_A=%ld (vanilla %ld) aim_jump=%.1f deg\n", t.cls, fps, g.cross_v, g.cross_err_deg,
                        g.misaligned_ticks, g.t_A, v.t_A, g.max_aim_jump_deg);
    }
    // the lateral approach in front of the crossing: at the join point's depth, 3 r off the line (local +x)
    {
        const double lat_start[3] = {3.0, -0.8 - 0.75 * core::kGateSin - 3.0 * core::kGateSin, -1.0 - 0.75 * core::kGateCos - 3.0 * core::kGateCos};
        const GateResult g = run_gate(t, fps, seed, 0, false, false, true, lat_start, kGateIdentity);
        const GateResult v = run_gate(t, fps, seed, 0, false, true, false, lat_start, kGateIdentity);
        std::snprintf(why, sizeof why, "%s %d fps seed %u gate lateral 3 r at the join depth: the leg corners, no disc (heading error %%.1f deg, vanilla %%.1f deg)", t.cls, fps, seed);
        checkf(g.arrived_A && g.arrived_B && g.launched && !g.disc_crossed && g.cross_err_deg >= 0 && g.min_v_after_cross >= 20.0, why,
               g.cross_err_deg, v.cross_err_deg);
        std::snprintf(why, sizeof why, "%s %d fps seed %u gate lateral 3 r at the join depth: heading error at the crossing %%.1f deg <= 22 (vanilla %%.1f deg)", t.cls, fps, seed);
        slip_gapf(g.cross_err_deg <= 22.0, why, g.cross_err_deg, v.cross_err_deg);
        std::snprintf(why, sizeof why, "%s %d fps seed %u gate lateral 3 r: at A within 1.3 x the vanilla time (%%.0f ms <= %%.0f)", t.cls, fps, seed);
        checkf(g.t_A >= 0 && v.t_A >= 0 && double(g.t_A) <= 1.3 * double(v.t_A) + 500, why, double(g.t_A), 1.3 * double(v.t_A) + 500);
        if (seed == kSeeds[0] && fps == 60)
            std::printf("gate lateral %-3s fps=%3d heading_err=%.1f (vanilla %.1f) creep=%d (vanilla %d) t_A=%ld (vanilla %ld)\n", t.cls, fps, g.cross_err_deg, v.cross_err_deg,
                        g.misaligned_ticks, v.misaligned_ticks, g.t_A, v.t_A);
    }
    // starts the lead-in must refuse (s > 0): beside the gate, behind it, above it, laterally off at A's depth: the ship aims
    // at A as vanilla does, arrives within 1.3 x the vanilla time and never crosses the disc
    {
        const double starts[4][3] = {{3.0, 0.0, 0.0}, {0.0, 0.0, 2.0}, {0.0, 3.0, 0.0}, {3.0, -0.8, -1.0}};
        const char* names[4] = {"beside 3 r", "behind 2 r", "above 3 r", "laterally off 3 r at A's depth"};
        for (unsigned k = 0; k < 4; ++k) {
            const GateResult g = run_gate(t, fps, seed, 0, false, false, true, starts[k], kGateIdentity);
            const GateResult v = run_gate(t, fps, seed, 0, false, true, false, starts[k], kGateIdentity);
            std::snprintf(why, sizeof why, "%s %d fps seed %u gate start %s: at A within 1.3 x the vanilla time (%%.0f ms <= %%.0f), the disc never crossed, launched", t.cls, fps, seed, names[k]);
            checkf(g.arrived_A && g.arrived_B && g.launched && !g.disc_crossed && v.arrived_A && !v.disc_crossed && double(g.t_A) <= 1.3 * double(v.t_A) + 500, why,
                   double(g.t_A), 1.3 * double(v.t_A) + 500);
        }
    }
    // a mismatched A (the engine's point 5 % of r off the pinned geometry): the lead-in is refused, the vanilla point steers
    if (fps == 60 && seed == kSeeds[0]) {
        const GateResult g = run_gate(t, fps, seed, 0, false, false, true, std_start, kGateIdentity, 0.05);
        std::snprintf(why, sizeof why, "%s gate: a mismatched A (5 %% of r off) refuses the lead-in, keeps the vanilla stops at A and B (no v0), still arrives", t.cls);
        check(!g.lead_used && g.arrived_A && g.arrived_B && g.launched && g.v_after_A == 0 && g.v0 == 0, why);
    }
}

// ---- scenario 11: the capital gate transit (capital-gate-transit.md section 6; amendment 13) ----------------------------
// The Run156 capitals (ship_trace W/rot/rot_pct/vmax/acc rows, r_ship from the grid depth 2 (r_gate + r_ship)): W 1843 / 1638
// binary/s, f+0x2b0 0.5 x rot_pct 10 = k 0.05, vmax 55000 / 56500, acc 5000 / 2500 units/s^2, r 1091 / 1022 m.
static const ShipType kCapitals[2] = {{"M2", "Run156 M2", 110.0, 10.0, 1843.0 / 65536.0 * 360.0, 1091.0, 5, true},
                                      {"M1", "Run156 M1", 113.0, 5.0, 1638.0 / 65536.0 * 360.0, 1022.0, 5, true}};
static const double kGateRM = 1105.0;
static const double kCos0xfd70 = double(0xfd70) / 65536.0; // the 0x1f end test (0x004592d9): forward . F >= cos 8.1 deg
// budget.py's phase model (inferred, SETA 1): order 5 along the axis from the crossing X (depth_x in front of the origin) at v0
// to the arrival sphere |v|/10 + r_ship: desired vmax beyond r_gate, the ramp r/5 -> vmax/2, r -> vmax inside, slewed at acc.
static double budget_order5_s(double depth_x, double v0, double vmax, double acc, double r_ship) {
    double d = depth_x, v = v0, t = 0;
    const double dt = 1.0 / 60.0;
    while (t < 600.0) {
        if (d <= v / 10.0 + r_ship) return t;
        const double want = d >= kGateRM ? vmax : (d <= kGateRM / 5 ? vmax / 2 : vmax / 2 + (vmax / 2) * (d - kGateRM / 5) / (kGateRM - kGateRM / 5));
        v = want > v ? std::fmin(want, v + acc * dt) : std::fmax(want, v - acc * dt);
        d -= v * dt;
        t += dt;
    }
    return t;
}
// budget.py's engine rotate (0x1f with the vanilla leaf: brake when e < w^2/a, else accelerate to W) from theta deg to the tolerance.
static double budget_rotate_s(double theta, double W_dps, double a_dps2) {
    double e = theta, w = 0, t = 0;
    const double dt = 1.0 / 60.0;
    while (e > double(core::kGateRotateTolDeg) && t < 600.0) {
        const double w2 = e < w * w / a_dps2 ? std::fmax(0.0, w - a_dps2 * dt) : std::fmin(W_dps, w + a_dps2 * dt);
        e -= std::fmin(e, 0.5 * (w + w2) * dt);
        w = w2;
        t += dt;
    }
    return t;
}
struct CapitalResult {
    bool arrived_A = false, arrived_B = false, launched = false, lead_used = false, kept = false, validated = false, nan_seen = false;
    double cross_v = 0, cross_err_deg = -1, p2_err_deg = -1, cross_dA = 0, cross_lat = 0, v_after_A = 0, v_o5_start = 0, v0 = 0, min_v_o5 = 1e18;
    double rot_start_err_deg = -1;
    long t_3r = -1, t_cross = -1, t_A = -1, rot_ms = 0, rot_frames = 0, t_o5_start = -1, t_B = -1, t_launch = -1, t_end = -1;
    int misaligned_ticks = 0;
    // the companions (scenario A / C): the capital's bookkeeping against them, the steered M6's dodge, the site-E verdicts
    unsigned cap_detours = 0, cap_detours_m6 = 0, cap_skipped_small = 0, cap_conflicts = 0;
    unsigned m6_detours = 0, m6_unresolved = 0;
    double m6_min_clear = 1e18, m6_manual_min = 1e18;
    unsigned e_suppress = 0, e_pass_manual = 0, e_pass_order = 0, e_pass_stale = 0, e_other = 0;
    bool e_small_pair_capitals = false;
    // the trailing / converging capital (scenario A / B)
    unsigned cap2_conflicts = 0, cap2_detours = 0, cap2_unresolved = 0, cap2_emergency = 0, cap2_skipped_small = 0;
    double cap2_min_dist = 1e18;
    bool cap2_arrived = false, cap2_skipped_first = false; // the pair policy on the snapshot items: a capital never small for the other
    std::uint32_t cap_last_obstacle = 0;
};
struct CapitalSetup {
    int slot = 0;                 // the KC grid slot of the capital (0..19)
    const GateFrame* F = &kGateIdentity;
    double start_lat_r = 3.0;     // the start: this far off the lead-in line (local x, in r_gate) ...
    double start_back_r = 6.0;    // ... and this far before X along F (in r_gate), heading at A
    double a_error_r = 0.0;       // the engine's point off the grid by this (in r_gate): the lead-in is refused (vanilla)
    bool policy = true;           // ship_steering_gates_capital
    bool m6s = false;             // two M6 escorts of another leader crossing the path: one steered (autopilot), one manual
    int cap2 = -1;                // a second capital (index into kCapitals) on the adjacent slot: -1 none, else its mode below
    bool cap2_converge = false;   // false: trailing by cap2_back_m (the queue's spacing); true: timed to arrive while the first rotates
    double cap2_back_m = 7000.0;  // the queue keeps the next holder behind the window (design section 5.2: > 3 km behind it)
};
// One ship's order-4 engine speed law to a gate (ship-navigation-ai.md 10.3): vmax beyond d_slow, the ramp inside, the alignment
// creep inside d_slow + 3 r + 2 r_ship, the large cap.
static std::int64_t engine_order4_speed(const Model& m, double dA, const double dA_v[3], bool large) {
    const double ru = kGateRM * 500;
    const double d_slow = 0.75 * ru + double(m.vmax) * double(m.vmax) / double(m.accel);
    std::int64_t des = dA > d_slow ? m.vmax : Model::ramp(dA, 5000, d_slow, m.vmax / 2);
    if (des < 5000) des = 5000;
    if (dA < d_slow + 3 * ru + 2 * m.r_units) {
        double c = 0;
        for (int k = 0; k < 3; ++k) c += m.row[2][k] * dA_v[k] / dA;
        des = c <= 0.7 ? 500 : Model::ramp(c - 0.7, 500, 0.3, des);
    }
    if (large && des > 50000) des = 50000;
    return des;
}
static void place_heading(Model& m, const double u_in[3]) { // the basis from a forward direction (world up unless nearly vertical)
    double u[3] = {u_in[0], u_in[1], u_in[2]};
    const double un = norm3d(u);
    for (int i = 0; i < 3; ++i) u[i] /= un;
    double up[3] = {0, 1, 0};
    if (std::fabs(u[1]) > 0.9) {
        up[1] = 0;
        up[2] = 1;
    }
    for (int i = 0; i < 3; ++i) m.row[2][i] = u[i];
    m.row[0][0] = up[1] * u[2] - up[2] * u[1];
    m.row[0][1] = up[2] * u[0] - up[0] * u[2];
    m.row[0][2] = up[0] * u[1] - up[1] * u[0];
    const double r0n = norm3d(m.row[0]);
    for (int i = 0; i < 3; ++i) m.row[0][i] /= r0n;
    m.row[1][0] = u[1] * m.row[0][2] - u[2] * m.row[0][1];
    m.row[1][1] = u[2] * m.row[0][0] - u[0] * m.row[0][2];
    m.row[1][2] = u[0] * m.row[0][1] - u[1] * m.row[0][0];
    m.vel_valid = false;
}
// The capital's grid point for a slot (KC GetWarpInPos) and the lead-in geometry the DLL derives (gate_geometry_capital).
static void capital_grid(const ShipType& t, int slot, const GateFrame& F, double a_error_r, double A[3], float Af[3], core::GateGeometry* geo, bool* valid) {
    const double ru = kGateRM * 500;
    const int grid_a[5] = {0, 1, -1, 2, -2};
    const double A_l[3] = {grid_a[slot % 5] * 666.0 * 500, -(slot / 5) * 666.0 * 500, -2.0 * (kGateRM + t.r_m) * 500};
    local_to_world(F, A_l, A);
    float G[3] = {0, 0, 0}, row0[3], row1[3], row2[3];
    for (int k = 0; k < 3; ++k) {
        Af[k] = float(A[k] / 500 + a_error_r * kGateRM * F.R[0][k]);
        row0[k] = float(F.R[0][k]);
        row1[k] = float(F.R[1][k]);
        row2[k] = float(F.R[2][k]);
    }
    *valid = core::gate_geometry_capital(Af, G, row0, row1, row2, float(kGateRM), float(t.r_m), geo);
    (void)ru;
}
// The transit of one capital through the whole gate sequence (order 4 -> P2 -> order 0 -> 0x1f -> order 0 -> order 5 -> P4 ->
// order 0 -> order 8), with the DLL's rules (the lead-in and the speed write under the policy, P2 keeping the speed of an aligned
// validated capital, P4/P5 on the validated approach, the engine's 0x1f with the vanilla leaf otherwise) and the optional
// companions. Two passes when companions must meet the capital: the first records its trajectory.
static CapitalResult run_capital_transit(const ShipType& t, int fps, std::uint32_t seed, const CapitalSetup& su) {
    const double ru = kGateRM * 500;
    const GateFrame& F = *su.F;
    double A[3];
    float Af[3];
    core::GateGeometry geo;
    bool valid = false;
    capital_grid(t, su.slot, F, su.a_error_r, A, Af, &geo, &valid);
    double X[3], Fw[3];
    for (int k = 0; k < 3; ++k) {
        Fw[k] = F.R[2][k];
        X[k] = A[k] - 0.75 * ru * Fw[k];
    }
    // pass 1 (when needed): the capital alone, its position every 100 ms for the companions' collision courses
    static double traj[4096][3];
    static double traj_dir[4096][3];
    unsigned traj_n = 0;
    long pass0_t_A = -1, pass0_t_3r = -1; // the capital alone (pass 0): its arrival time (the converging capital's timing) and its
                                          // entry into 3 r before X on the line (the escorts cross the line ahead of it from there)
    for (int pass = (su.m6s || su.cap2_converge ? 0 : 1); pass < 2; ++pass) {
        static World w;
        w = World{};
        core::ShipTable table{};
        Model m;
        m.init(t, seed);
        m.table = &table;
        m.capital = true;
        m.capital_gate_leg = true;
        m.ptr = 0x9000;
        m.gate_mode = su.policy && valid;
        m.gate_geo = geo;
        m.gate_r = float(kGateRM);
        for (int k = 0; k < 3; ++k) {
            m.gate_G[k] = 0;
            for (int r = 0; r < 3; ++r) m.gate_rows[r][k] = float(F.R[r][k]);
        }
        // the start: start_lat_r off the line (local x), start_back_r before X, heading at A
        const double start_l[3] = {su.start_lat_r * ru, 0.0, -su.start_back_r * ru};
        double start_w[3];
        local_to_world(F, start_l, start_w);
        for (int k = 0; k < 3; ++k) m.pos[k] = X[k] + start_w[k];
        {
            const double u[3] = {A[0] - m.pos[0], A[1] - m.pos[1], A[2] - m.pos[2]};
            place_heading(m, u);
        }
        m.order = core::kOrderFlyBefore;
        for (int k = 0; k < 3; ++k) m.P[k] = A[k];
        m.d_arr_units = 0.75 * ru;
        w.attach(m);
        Model* ships[4] = {&m, nullptr, nullptr, nullptr};
        unsigned n_ships = 1;
        // the companions
        Model m6s[2], cap2;
        const ShipType& m6t = kShips[2]; // Centaur: 180 m/s, 64.4 m (below r_capital / 3: an ignored pair)
        for (unsigned i = 0; i < 2; ++i) {
            m6s[i].init(m6t, seed + 11 * i + 3);
            m6s[i].ptr = 0x4000 + 0x100 * i;
        }
        cap2.init(kCapitals[su.cap2 >= 0 ? su.cap2 : 0], seed + 97);
        long m6_switch_ms = -1, m6_stale_ms = -1;
        if (pass == 1 && su.m6s) {
            // the escorts cross the capital's line ahead of it once it is on the line (design: 2 km and 1 km ahead): the steered one
            // where the capital is 10 s after entering 3 r before X, the manual one 20 s after (passing 12 s before the capital)
            const long t3 = pass0_t_3r > 0 ? pass0_t_3r : 40000;
            const long meet_ms[2] = {t3 + 10000, t3 + 20000};
            for (unsigned i = 0; i < 2; ++i) {
                Model& e = m6s[i];
                e.init(m6t, seed + 11 * i + 3);
                e.table = &table;
                e.ptr = 0x4000 + 0x100 * i;
                e.order = core::kOrderEscort;
                e.leader_ptr = 0x7777; // escorts of another leader (not in the sector): wingmates of each other
                const unsigned ti = unsigned(meet_ms[i] / 100);
                const double* P = traj[ti < traj_n ? ti : traj_n - 1];
                const double* vd = traj_dir[ti < traj_n ? ti : traj_n - 1];
                const double upv[3] = {0, 1, 0};
                double d[3] = {vd[1] * upv[2] - vd[2] * upv[1], vd[2] * upv[0] - vd[0] * upv[2], vd[0] * upv[1] - vd[1] * upv[0]};
                if (norm3d(d) < 1e-6) {
                    d[0] = 1;
                    d[1] = d[2] = 0;
                }
                const double dn = norm3d(d);
                for (int k = 0; k < 3; ++k) d[k] /= dn;
                const double side = i == 0 ? 1.0 : -1.0;
                // the manual one passes the point 12 s before the capital gets there (no contact without the engine's own warn)
                const double lead_s = i == 0 ? 0.0 : 12.0;
                const double dist_u = m6t.v_ms * (double(meet_ms[i]) / 1000.0 - lead_s) * 500.0;
                for (int k = 0; k < 3; ++k) {
                    e.pos[k] = P[k] - side * d[k] * dist_u;
                    e.P[k] = P[k] + side * d[k] * 20000.0 * 500.0;
                }
                const double u[3] = {side * d[0], side * d[1], side * d[2]};
                place_heading(e, u);
                e.d_arr_units = 2 * e.r_units;
                if (i == 0) {
                    w.attach(e); // steered: the module's avoidance (the pair's small side dodges)
                } else {
                    e.vanilla = true; // not under the module: the vanilla leaf, no avoidance, no table entry
                    e.avoid_on = false;
                }
                ships[n_ships++] = &e;
            }
            m6_switch_ms = meet_ms[0] + 15000; // the steered M6 is switched to order 0 (stopped by its script) here ...
            m6_stale_ms = m6_switch_ms + 400;   // ... and back to its escort order without a decision: stale
        }
        bool cap2_on = false;
        double A2[3] = {0, 0, 0};
        if (su.cap2 >= 0 && pass == 1) {
            const ShipType& t2 = kCapitals[su.cap2];
            cap2.init(t2, seed + 97);
            cap2.table = &table;
            cap2.capital = true;
            cap2.capital_gate_leg = true;
            cap2.ptr = 0x9800;
            float Af2[3];
            core::GateGeometry geo2;
            bool valid2 = false;
            const int slot2 = (su.slot + 1) % 20;
            capital_grid(t2, slot2, F, 0.0, A2, Af2, &geo2, &valid2);
            cap2.gate_mode = su.policy && valid2;
            cap2.gate_geo = geo2;
            cap2.gate_r = float(kGateRM);
            for (int k = 0; k < 3; ++k) {
                cap2.gate_G[k] = 0;
                for (int r = 0; r < 3; ++r) cap2.gate_rows[r][k] = float(F.R[r][k]);
            }
            double X2[3];
            for (int k = 0; k < 3; ++k) X2[k] = A2[k] - 0.75 * ru * Fw[k];
            double s2[3];
            if (!su.cap2_converge) { // trailing: the mirrored lateral offset, cap2_back_m further back (the queue's spacing)
                const double l[3] = {-su.start_lat_r * ru, 0.0, -su.start_back_r * ru - su.cap2_back_m * 500.0};
                local_to_world(F, l, s2);
            } else { // converging: on its own line, timed to reach X2 about 10 s after the first capital's arrival (pass 0's t_A)
                const double back_u = (double(pass0_t_A > 0 ? pass0_t_A : 60000) / 1000.0 + 10.0) * 100.0 * 500.0;
                const double l[3] = {0.0, 0.0, -back_u};
                local_to_world(F, l, s2);
            }
            for (int k = 0; k < 3; ++k) cap2.pos[k] = X2[k] + s2[k];
            {
                const double u[3] = {A2[0] - cap2.pos[0], A2[1] - cap2.pos[1], A2[2] - cap2.pos[2]};
                place_heading(cap2, u);
            }
            cap2.order = core::kOrderFlyBefore;
            for (int k = 0; k < 3; ++k) cap2.P[k] = A2[k];
            cap2.d_arr_units = 0.75 * ru;
            w.attach(cap2);
            ships[n_ships++] = &cap2;
            cap2_on = true;
        }
        CapitalResult res;
        res.validated = valid && su.policy;
        // the synthetic objects the shared gate-site rules read: the capital (code M2 4 / M1 3) with a class-0x12 target
        FakeMem mem;
        const std::uint32_t ship = 0x2000, flight = 0x3000, gate = 0x4000, types = 0x5000;
        const unsigned code = !std::strcmp(t.cls, "M1") ? 3 : 4;
        mem.put(ship + 0x48, 7u | (3u << 16));
        mem.put(ship + 0x50, flight);
        mem.put(flight + 0x224, gate);
        mem.put(gate + 0x48, 0x12);
        mem.put(types + 3 * 0xdb8 + 0x14, code);
        const bool cap_target = core::gate_target_capital(mem, ship, types);
        const bool noncap_target = core::gate_target_noncapital(mem, ship, types);
        check(cap_target && !noncap_target, "capital transit: the capital's gate target reads as a capital gate approach and not as a non-capital one");
        Clock clocks[4];
        int phase = 0; // 0 order 4, 1 order 0 after P2, 5 order 0x1f, 6 order 0 after 0x1f, 2 order 5, 3 order 0 after P4, 4 order 8
        long phase_t = 0;
        double launch_start[3] = {0, 0, 0};
        long long v0 = 0;
        const std::uint32_t obj = 0x1000, obj2 = 0x1010, obj_m6[2] = {0x1020, 0x1030};
        bool cap2_done = false;
        traj_n = 0;
        const double r_cap = t.r_m;
        const float C_m6 = av::clearance(float(m6t.r_m), float(r_cap));
        for (long i = 0; i < 400 * fps; ++i) {
            const long dt = clocks[0].step(fps);
            for (unsigned k = 1; k < 4; ++k) clocks[k].step(fps);
            if (!dt) continue;
            m.frame_dt_ms = dt;
            if (pass == 0 && traj_n < 4096 && m.t_ms >= long(traj_n) * 100) {
                for (int k = 0; k < 3; ++k) {
                    traj[traj_n][k] = m.pos[k];
                    traj_dir[traj_n][k] = m.row[2][k];
                }
                ++traj_n;
            }
            w.fill(ships, n_ships, std::uint32_t(m.t_ms));
            const double dA_v[3] = {A[0] - m.pos[0], A[1] - m.pos[1], A[2] - m.pos[2]}, dB_v[3] = {-m.pos[0], -m.pos[1], -m.pos[2]};
            const double dA = norm3d(dA_v), dB = norm3d(dB_v);
            double s_line = 0, lat2 = 0;
            {
                double dx[3];
                for (int k = 0; k < 3; ++k) dx[k] = (m.pos[k] - X[k]) / 500.0;
                s_line = dx[0] * Fw[0] + dx[1] * Fw[1] + dx[2] * Fw[2];
                lat2 = dx[0] * dx[0] + dx[1] * dx[1] + dx[2] * dx[2] - s_line * s_line;
            }
            if (res.t_3r < 0 && s_line >= -3.0 * kGateRM) {
                res.t_3r = m.t_ms;
                if (pass == 0) pass0_t_3r = m.t_ms;
            }
            double c_F = 0;
            for (int k = 0; k < 3; ++k) c_F += m.row[2][k] * Fw[k];
            if (c_F > 1) c_F = 1;
            if (c_F < -1) c_F = -1;
            const double err_F = std::acos(c_F) * 180 / M_PI;
            bool leaf_call = false;
            if (phase == 4) { // order 8 (ship-movement.md section 13.3)
                const long u = m.t_ms - phase_t;
                if (u >= 900) {
                    res.launched = true;
                    res.t_end = m.t_ms;
                    break;
                }
                const std::int32_t sl = core::gate_launch_units(std::int32_t(v0), std::int32_t(u));
                for (int k = 0; k < 3; ++k) m.pos[k] = launch_start[k] + double(fx(std::llround(Fw[k] * 65536.0), sl));
                m.speed = m.desired = 0;
                m.advance(dt);
            } else if (phase == 0) {
                if (res.t_cross < 0 && dA <= 0.75 * ru) {
                    res.t_cross = m.t_ms;
                    res.cross_v = double(m.speed) / 500.0;
                    res.cross_dA = dA / 500.0;
                    res.cross_lat = lat2 > 0 ? std::sqrt(lat2) : 0;
                    res.cross_err_deg = err_F;
                }
                m.steer_call(obj, true, 0); // the leaf every frame
                leaf_call = true;
                res.cap_detours = m.detours;
                res.cap_skipped_small = m.skipped_small;
                if (m.last_av.adjusted) res.cap_last_obstacle = m.last_av.obstacle;
                if (m.last_av.conflicts) ++res.cap_conflicts;
                if (m.last_av.adjusted && (m.last_av.obstacle == m6s[0].ptr || m.last_av.obstacle == m6s[1].ptr)) ++res.cap_detours_m6;
                if (m.t_ms >= m.next_think) {
                    m.next_think = m.t_ms + m.rng.next() * 50 / 32768;
                    if (dA <= 0.75 * ru) { // P2 through the shared rules: the capital keeps its speed only when aligned on a validated approach
                        m.desired = 0;
                        res.p2_err_deg = err_F;
                        res.kept = core::gate_capital_keeps_speed(cap_target, su.policy, m.gate_mode, float(err_F));
                        if (!res.kept) m.speed = 0;
                        res.arrived_A = true;
                        res.t_A = m.t_ms;
                        if (pass == 0) pass0_t_A = m.t_ms;
                        res.v_after_A = double(m.speed) / 500.0;
                        phase = 1;
                        phase_t = m.t_ms;
                        m.order = core::kOrderNone;
                    } else
                        m.desired = engine_order4_speed(m, dA, dA_v, t.large);
                }
            } else if (phase == 1 || phase == 6) { // the KC's hand-over frames: order 0, desired 0
                m.desired = 0;
                if (m.t_ms > phase_t) {
                    if (phase == 1) {
                        phase = 5;
                        m.order = core::kOrderRotate;
                        res.rot_start_err_deg = err_F;
                        res.rot_frames = 0;
                    } else {
                        phase = 2;
                        m.order = core::kOrderDock;
                        for (int k = 0; k < 3; ++k) m.P[k] = 0; // the gate origin (section 10.5)
                        res.t_o5_start = m.t_ms;
                        res.v_o5_start = double(m.speed) / 500.0;
                    }
                    phase_t = m.t_ms;
                }
            } else if (phase == 5) { // RotateToAngles 0x1f (0x0045920e): ends on the first frame forward . F >= 0xfd70, else the vanilla leaf at position + F
                ++res.rot_frames;
                m.desired = 0;
                if (c_F >= kCos0xfd70) {
                    res.rot_ms = m.t_ms - phase_t;
                    phase = 6;
                    m.order = core::kOrderNone;
                    phase_t = m.t_ms;
                } else {
                    for (int k = 0; k < 3; ++k) m.P[k] = m.pos[k] + Fw[k] * 1.0e7;
                    m.vanilla_leaf();
                    leaf_call = true;
                }
            } else if (phase == 2) { // order 5 every frame (section 10.5): the module passes a capital's order 5 through (no site-B write)
                m.steer_call(obj, false, 0);
                leaf_call = true;
                double c = 0;
                for (int k = 0; k < 3; ++k) c += m.row[2][k] * dB_v[k] / dB;
                if (c < 0.95) {
                    m.desired = 500;
                    ++res.misaligned_ticks;
                } else
                    m.desired = dB <= ru / 5 ? m.vmax / 2 : (dB >= ru ? m.vmax : m.vmax / 2 + std::llround(double(m.vmax / 2) * (dB - ru / 5) / (ru - ru / 5)));
                if (double(m.speed) / 500.0 < res.min_v_o5) res.min_v_o5 = double(m.speed) / 500.0;
                if (dB <= double(m.speed) / 10.0 + m.r_units) { // P4: the capital keeps v0 on a validated approach under the policy
                    m.desired = 0;
                    const bool keep4 = cap_target && su.policy && m.gate_mode;
                    v0 = keep4 ? m.speed : 0;
                    if (!keep4) m.speed = 0;
                    res.arrived_B = true;
                    res.t_B = m.t_ms;
                    res.v0 = double(v0) / 500.0;
                    phase = 3;
                    phase_t = m.t_ms;
                    m.order = core::kOrderNone;
                }
            } else if (phase == 3) {
                m.desired = 0;
                if (m.t_ms > phase_t) {
                    phase = 4;
                    m.order = core::kOrderWarpAway;
                    for (int k = 0; k < 3; ++k) launch_start[k] = m.P[k] = double(std::llround(m.pos[k]));
                    phase_t = m.t_ms;
                    res.t_launch = m.t_ms;
                }
            }
            (void)leaf_call;
            if (phase != 4) {
                m.clamp(obj); // site B every frame
                m.advance(dt);
            }
            // the companions
            if (pass == 1 && su.m6s) {
                if (m6_switch_ms >= 0 && m6s[0].t_ms >= m6_switch_ms && m6s[0].order == core::kOrderEscort && !m6s[0].done) {
                    m6s[0].order = core::kOrderNone; // its script stopped it: no longer in the steer set
                    m6s[0].done = true;
                    m6s[0].desired = 0;
                }
                if (m6_stale_ms >= 0 && m6s[0].t_ms >= m6_stale_ms && m6s[0].order == core::kOrderNone) m6s[0].order = core::kOrderEscort; // back in the set, no decision: stale
                for (unsigned e = 0; e < 2; ++e) m6s[e].frame(dt, obj_m6[e], true, 0, 1);
                res.m6_detours = m6s[0].detours;
                res.m6_unresolved = m6s[0].unresolved;
                for (unsigned e = 0; e < 2; ++e) {
                    double d = 0;
                    for (int k = 0; k < 3; ++k) d += (m.pos[k] - m6s[e].pos[k]) * (m.pos[k] - m6s[e].pos[k]);
                    d = std::sqrt(d) / 500.0;
                    if (e == 0 && d < res.m6_min_clear) res.m6_min_clear = d;
                    if (e == 1 && d < res.m6_manual_min) res.m6_manual_min = d;
                }
                // site E's small-pair verdict for (capital, M6) each frame, as the DLL computes it from B's fields
                if (phase == 0 || phase == 2) {
                    const std::uint32_t now = std::uint32_t(m.t_ms);
                    const core::Ship* sb = table.find(obj_m6[0]);
                    const av::SmallPair v0s = av::small_pair_verdict(0x80000u, m6s[0].order, 0, false, sb != nullptr, sb ? sb->stamp_ms : 0u, now);
                    const av::SmallPair v1s = av::small_pair_verdict(0x40000u, m6s[1].order, 0, false, false, 0u, now); // manual: no autopilot bit
                    const bool pair0 = av::small_pair(true, float(r_cap), true, false, float(m6t.r_m));
                    if (!pair0) ++res.e_other;
                    else if (v0s == av::SmallPair::suppress) ++res.e_suppress;
                    else if (v0s == av::SmallPair::order_not_steered) ++res.e_pass_order;
                    else if (v0s == av::SmallPair::stale) ++res.e_pass_stale;
                    else ++res.e_other;
                    if (v1s == av::SmallPair::no_autopilot) ++res.e_pass_manual;
                    else ++res.e_other;
                }
            }
            if (cap2_on && !cap2_done) {
                const double dA2_v[3] = {A2[0] - cap2.pos[0], A2[1] - cap2.pos[1], A2[2] - cap2.pos[2]};
                const double dA2 = norm3d(dA2_v);
                cap2.frame_dt_ms = dt;
                cap2.steer_call(obj2, true, 0);
                if (cap2.last_av.conflicts) ++res.cap2_conflicts;
                if (cap2.last_av.emergency) ++res.cap2_emergency;
                res.cap2_detours = cap2.detours;
                res.cap2_unresolved = cap2.unresolved;
                res.cap2_skipped_small = cap2.skipped_small;
                if (cap2.t_ms >= cap2.next_think) {
                    cap2.next_think = cap2.t_ms + cap2.rng.next() * 50 / 32768;
                    if (dA2 <= 0.75 * ru) {
                        cap2.desired = 0;
                        cap2.speed = 0;
                        cap2.order = core::kOrderNone;
                        cap2_done = true;
                        res.cap2_arrived = true;
                    } else
                        cap2.desired = engine_order4_speed(cap2, dA2, dA2_v, true);
                }
                cap2.clamp(obj2);
                cap2.advance(dt);
                double d = 0;
                for (int k = 0; k < 3; ++k) d += (m.pos[k] - cap2.pos[k]) * (m.pos[k] - cap2.pos[k]);
                d = std::sqrt(d) / 500.0;
                if (d < res.cap2_min_dist) res.cap2_min_dist = d;
                // the capital pair is never a small pair in either direction, and neither capital skips the other in the snapshot
                if (av::small_pair(true, float(r_cap), true, true, float(kCapitals[su.cap2].r_m)) ||
                    av::small_pair(true, float(kCapitals[su.cap2].r_m), true, true, float(r_cap)))
                    res.e_small_pair_capitals = true;
                av::Self s1{}, s2{};
                s1.capital = s2.capital = true;
                s1.r = float(r_cap);
                s2.r = cap2.dyn.r_own;
                for (unsigned q = 0; q < w.snap.count; ++q) {
                    if (w.snap.items[q].ptr == m.ptr && av::small_for_capital(s2, w.snap.items[q])) res.cap2_skipped_first = true;
                    if (w.snap.items[q].ptr == cap2.ptr && av::small_for_capital(s1, w.snap.items[q])) res.cap2_skipped_first = true;
                }
            }
            for (int k = 0; k < 3; ++k)
                if (!std::isfinite(m.pos[k]) || (cap2_on && !std::isfinite(cap2.pos[k]))) res.nan_seen = true;
        }
        (void)C_m6;
        res.lead_used = m.gate_lead_used;
        if (pass == 1) return res;
    }
    return CapitalResult{};
}
// The scenario: A (the budget with the escorts crossing and a trailing capital), B (misaligned: the grid mismatch, and a
// validated approach too short to align), C (the escort pair rules), per Run156 record, fps and seed.
static void capital_transit_scenario(const ShipType& t, int fps, std::uint32_t seed) {
    char why[320];
    const bool print = seed == kSeeds[0] && fps == 60;
    const double a_rot = t.yaw_dps * double(t.k_pct) / 100.0; // deg/s^2: W x k
    for (int slot_i = 0; slot_i < 2; ++slot_i) {
        CapitalSetup su;
        su.slot = slot_i == 0 ? 0 : 19;
        su.F = slot_i == 0 ? &kGateIdentity : &kGateRotated;
        su.m6s = true;
        su.cap2 = t.v_ms < 100.0 ? -1 : !std::strcmp(t.cls, "M2") ? 1 : 0; // the installed records trail each other; the slower Boreas fixture type alone
        const CapitalResult g = run_capital_transit(t, fps, seed, su);
        const double depth_x = 2.0 * (kGateRM + t.r_m) + 0.75 * kGateRM; // slot 0: X is this far in front of the origin along the axis
        const double lat = su.slot == 0 ? 0.0 : std::sqrt(2.0 * 666.0 * 666.0 + 3.0 * 666.0 * 3.0 * 666.0); // slot 19: a = -2, b = -3
        const double dist_x = std::sqrt(depth_x * depth_x + lat * lat);
        const double b_o5 = budget_order5_s(dist_x, g.cross_v > 0 ? std::fmin(g.cross_v, 100.0) : 100.0, t.v_ms, t.a_ms2, t.r_m);
        const double b_approach = 3.0 * kGateRM / std::fmin(t.v_ms, 100.0); // 33.2 s at the large cap (the Boreas fixture type: 47 s at 70 m/s)
        const double approach_bound = t.v_ms >= 100.0 ? 36.0 : 1.1 * b_approach; // the design's 36 s for the installed capitals
        const double b_total = b_approach + 1.0 / 60.0 + b_o5 + 0.9;
        const double cap_v = std::fmin(t.v_ms, double(core::kGateLargeCapMs)); // the crossing speed the policy aims at
        const double t_approach = double(g.t_cross - g.t_3r) / 1000.0, t_o5 = double(g.t_B - g.t_o5_start) / 1000.0,
                     t_total = double(g.t_end - g.t_3r) / 1000.0;
        const char* tag = su.slot == 0 ? "slot 0" : "slot 19 (rotated frame)";
        std::snprintf(why, sizeof why, "%s %d fps seed %u capital transit %s: validated lead-in, arrived inside 0.75 r (dA %%.0f m), launched, no NaN (lat at X %%.0f m)", t.cls, fps, seed, tag);
        checkf(g.validated && g.lead_used && g.arrived_A && g.arrived_B && g.launched && !g.nan_seen && g.cross_dA <= 0.75 * kGateRM, why, g.cross_dA, g.cross_lat);
        std::snprintf(why, sizeof why, "%s %d fps seed %u capital transit %s: aligned at P2 within the 0x1f tolerance (%%.2f deg <= %%.2f), the speed kept", t.cls, fps, seed, tag);
        checkf(g.p2_err_deg >= 0 && g.p2_err_deg <= double(core::kGateRotateTolDeg) && g.kept && g.v_after_A > 0, why, g.p2_err_deg, double(core::kGateRotateTolDeg));
        std::snprintf(why, sizeof why, "%s %d fps seed %u capital transit %s: crossing speed %%.1f m/s >= 0.9 x the cap (%%.0f; design 90 of 100)", t.cls, fps, seed, tag);
        checkf(g.cross_v >= 0.9 * cap_v, why, g.cross_v, cap_v);
        std::snprintf(why, sizeof why, "%s %d fps seed %u capital transit %s: 0x1f ends on its first frame (%%.0f frame, %%.0f ms)", t.cls, fps, seed, tag);
        checkf(g.rot_frames == 1, why, double(g.rot_frames), double(g.rot_ms));
        std::snprintf(why, sizeof why, "%s %d fps seed %u capital transit %s: order 5 starts at %%.1f m/s >= 0.85 x the cap (%%.0f; design 85 of 100), v0 kept at P4", t.cls, fps, seed, tag);
        checkf(g.v_o5_start >= 0.85 * cap_v && g.v0 > 0, why, g.v_o5_start, cap_v);
        std::snprintf(why, sizeof why, "%s %d fps seed %u capital transit %s: approach from 3 r before X to the crossing %%.1f s <= %%.1f (design 36 s; model 3 r / v)", t.cls, fps, seed, tag);
        checkf(g.t_3r >= 0 && t_approach <= approach_bound, why, t_approach, approach_bound);
        if (su.slot == 0) {
            std::snprintf(why, sizeof why, "%s %d fps seed %u capital transit %s: order 5 %%.1f s within 20 %% of budget.py's %%.1f s", t.cls, fps, seed, tag);
            checkf(std::fabs(t_o5 - b_o5) <= 0.2 * b_o5, why, t_o5, b_o5);
            std::snprintf(why, sizeof why, "%s %d fps seed %u capital transit %s: total from 3 r before X to the launch end %%.1f s <= 1.2 x %%.1f s (design: M2 72, M1 70)", t.cls, fps, seed, tag);
            checkf(t_total <= 1.2 * b_total, why, t_total, b_total);
        } else { // the outer slot: the origin is 24.7 deg off the axis, the engine's misaligned order-5 branch (1 m/s) runs until within 18.2 deg
            std::snprintf(why, sizeof why, "%s %d fps seed %u capital transit %s: order 5 %%.1f s <= 1.6 x the aligned budget %%.1f s (the misaligned branch's turn)", t.cls, fps, seed, tag);
            checkf(t_o5 <= 1.6 * b_o5, why, t_o5, b_o5);
        }
        // scenario A's escorts and C's pair rules
        std::snprintf(why, sizeof why, "%s %d fps seed %u capital transit %s: no detour against the M6s (%%.0f), skipped_small > 0 (%%.0f), no conflict at all: the line held", t.cls, fps, seed, tag);
        checkf(g.cap_detours_m6 == 0 && g.cap_detours == 0 && g.cap_conflicts == 0 && g.cap_skipped_small > 0, why, double(g.cap_detours_m6), double(g.cap_skipped_small));
        const double C = double(av::clearance(float(kShips[2].r_m), float(t.r_m)));
        std::snprintf(why, sizeof why, "%s %d fps seed %u capital transit %s: the steered M6 dodges the capital (detours %%.0f > 0) with clearance >= C (min %%.0f m)", t.cls, fps, seed, tag);
        checkf(g.m6_detours > 0 && g.m6_min_clear >= C, why, double(g.m6_detours), g.m6_min_clear);
        std::snprintf(why, sizeof why, "%s %d fps seed %u capital transit %s: site E suppresses the (capital, steered M6) pair every steered frame (%%.0f), passes it through on order 0 (%%.0f) and when stale, the manual M6 always (no autopilot)", t.cls, fps, seed, tag);
        checkf(g.e_suppress > 0 && g.e_pass_order > 0 && g.e_pass_stale > 0 && g.e_pass_manual > 0 && g.e_other == 0, why, double(g.e_suppress), double(g.e_pass_order));
        std::snprintf(why, sizeof why, "%s %d fps seed %u capital transit %s: the trailing capital is never a small pair and never skipped in either direction (its own skipped_small %%.0f are the M6s), no conflict at the queue's spacing, no NaN (min dist %%.0f m)", t.cls, fps, seed, tag);
        if (su.cap2 >= 0) checkf(!g.e_small_pair_capitals && !g.cap2_skipped_first && g.cap2_conflicts == 0 && !g.nan_seen, why, double(g.cap2_skipped_small), g.cap2_min_dist);
        if (print)
            std::printf("capital transit %-3s %s fps=%3d: t_3r=%.1f cross=%.1f s (approach %.1f s, model %.1f) cross_v=%.1f err=%.2f deg lat=%.0f m kept=%d "
                        "rot=%ld frames/%ld ms o5 start v=%.1f o5=%.1f s (budget %.1f, mis_ticks=%d, min_v=%.1f) v0=%.1f launch=%.1f s total=%.1f s (budget %.1f) | "
                        "cap detours=%u skipped_small=%u conflicts=%u | m6 detours=%u clear=%.0f m (C %.0f) manual_min=%.0f | E sup=%u order=%u stale=%u manual=%u | "
                        "cap2 conflicts=%u detours=%u unresolved=%u min=%.0f m arrived=%d | cap last obstacle=0x%x\n",
                        t.cls, tag, fps, double(g.t_3r) / 1000.0, double(g.t_cross) / 1000.0, t_approach, b_approach, g.cross_v, g.p2_err_deg, g.cross_lat,
                        g.kept ? 1 : 0, g.rot_frames, g.rot_ms, g.v_o5_start, t_o5, b_o5, g.misaligned_ticks, g.min_v_o5, g.v0,
                        double(g.t_end - g.t_launch) / 1000.0, t_total, b_total, g.cap_detours, g.cap_skipped_small, g.cap_conflicts, g.m6_detours,
                        g.m6_min_clear, C, g.m6_manual_min, g.e_suppress, g.e_pass_order, g.e_pass_stale, g.e_pass_manual, g.cap2_conflicts,
                        g.cap2_detours, g.cap2_unresolved, su.cap2 >= 0 ? g.cap2_min_dist : -1.0, g.cap2_arrived ? 1 : 0, unsigned(g.cap_last_obstacle));
    }
    // B1: the grid mismatch (the engine's point 5 % of r off the grid): the lead-in is refused, the vanilla stop, the engine's
    // rotate, order 5 from rest; a second capital converging on the adjacent slot while the first rotates registers its conflict
    // (never a small pair), is not resolved inside C (the known limit handed to the queue) and never NaNs.
    {
        CapitalSetup su;
        su.slot = 7;
        su.a_error_r = 0.05;
        su.cap2 = !std::strcmp(t.cls, "M2") ? 1 : 0;
        su.cap2_converge = true;
        const CapitalResult g = run_capital_transit(t, fps, seed, su);
        const double b_rot = g.rot_start_err_deg > 0 ? budget_rotate_s(g.rot_start_err_deg, t.yaw_dps, a_rot) : 0.0;
        std::snprintf(why, sizeof why, "%s %d fps seed %u capital transit B1 (grid mismatch): the lead-in refused, vanilla stop at X (v %%.1f), rotate > 1 frame (%%.0f frames), order 5 from rest, launched from rest", t.cls, fps, seed);
        checkf(!g.validated && !g.lead_used && g.arrived_A && !g.kept && g.v_after_A == 0 && g.rot_frames > 1 && g.v_o5_start == 0 && g.arrived_B && g.v0 == 0 && g.launched && !g.nan_seen,
               why, g.v_after_A, double(g.rot_frames));
        std::snprintf(why, sizeof why, "%s %d fps seed %u capital transit B1: the engine's rotate from %%.1f deg takes %%.1f s, within 1.3 x the controller model + 2 s", t.cls, fps, seed);
        checkf(double(g.rot_ms) / 1000.0 <= 1.3 * b_rot + 2.0, why, g.rot_start_err_deg, double(g.rot_ms) / 1000.0);
        std::snprintf(why, sizeof why, "%s %d fps seed %u capital transit B1: the converging capital keeps the pair (conflicts %%.0f > 0, never small), reports it unresolved or detours (%%.0f), no NaN", t.cls, fps, seed);
        checkf(g.cap2_conflicts > 0 && !g.e_small_pair_capitals && g.cap2_skipped_small == 0 && (g.cap2_unresolved + g.cap2_detours) > 0 && !g.nan_seen,
               why, double(g.cap2_conflicts), double(g.cap2_unresolved + g.cap2_detours));
        if (print)
            std::printf("capital transit %-3s B1 fps=%3d: cross_v=%.1f err=%.1f deg v_after_A=%.1f rot=%ld frames %.1f s (model %.1f) o5 start v=%.1f t_o5=%.1f s v0=%.1f | "
                        "cap2 conflicts=%u detours=%u unresolved=%u emergency=%u min=%.0f m arrived=%d\n",
                        t.cls, fps, g.cross_v, g.rot_start_err_deg, g.v_after_A, g.rot_frames, double(g.rot_ms) / 1000.0, b_rot, g.v_o5_start,
                        double(g.t_B - g.t_o5_start) / 1000.0, g.v0, g.cap2_conflicts, g.cap2_detours, g.cap2_unresolved, g.cap2_emergency, g.cap2_min_dist,
                        g.cap2_arrived ? 1 : 0);
    }
    // B2: a validated approach too short to align (3 r off the line only 2 r before X): the lead-in runs, the crossing is
    // misaligned, P2 takes the vanilla stop, the engine rotates, order 5 from rest; P4 keeps v0 on the validated approach.
    // The installed records only (the Boreas fixture type turns at 13.5 deg/s and aligns from 2 r).
    if (t.v_ms >= 100.0) {
        CapitalSetup su;
        su.slot = 0;
        su.start_lat_r = 3.0;
        su.start_back_r = 2.0;
        const CapitalResult g = run_capital_transit(t, fps, seed, su);
        std::snprintf(why, sizeof why, "%s %d fps seed %u capital transit B2 (join too short): validated but misaligned at P2 (%%.1f deg > 8.11): the vanilla stop, rotate %%.0f frames, order 5 from rest, launched", t.cls, fps, seed);
        checkf(g.validated && g.lead_used && g.arrived_A && g.p2_err_deg > double(core::kGateRotateTolDeg) && !g.kept && g.v_after_A == 0 && g.rot_frames > 1 &&
                   g.v_o5_start == 0 && g.arrived_B && g.launched && !g.nan_seen,
               why, g.p2_err_deg, double(g.rot_frames));
        if (print)
            std::printf("capital transit %-3s B2 fps=%3d: cross_v=%.1f err=%.1f deg v_after_A=%.1f rot=%ld frames %.1f s v0=%.1f\n", t.cls, fps, g.cross_v, g.p2_err_deg,
                        g.v_after_A, g.rot_frames, double(g.rot_ms) / 1000.0, g.v0);
    }
    // the policy off (ship_steering_gates_capital = 0): the amendment-10 path, no lead-in, the vanilla stops
    if (print) {
        CapitalSetup su;
        su.slot = 0;
        su.policy = false;
        const CapitalResult g = run_capital_transit(t, fps, seed, su);
        std::snprintf(why, sizeof why, "%s capital transit, policy off: no lead-in, the vanilla stop at X and at the origin (v0 %%.1f), launched (rotate %%.0f frames)", t.cls);
        checkf(!g.validated && !g.lead_used && g.arrived_A && g.v_after_A == 0 && g.v0 == 0 && g.launched && !g.nan_seen, why, g.v0, double(g.rot_frames));
    }
}
// The pair policy and the site-E small-pair rule, the capital keep rules and the capital lead-in window (amendment 13).
static void capital_unit_checks() {
    // small_for_capital: capital-coded never, otherwise the ratio; non-capital self never
    av::Self cap{};
    cap.capital = true;
    cap.r = 1091.0f;
    av::Obstacle o{};
    o.kind = std::uint8_t(av::Kind::ship);
    o.r = 64.4f;
    o.size = 0;
    const bool m6_small = av::small_for_capital(cap, o);
    o.r = 300.0f;
    const bool m6_300 = av::small_for_capital(cap, o);
    o.r = 400.0f;
    const bool m6_400 = av::small_for_capital(cap, o);
    o.r = 40.0f;
    o.size = 1;
    const bool cap_coded = av::small_for_capital(cap, o);
    o.size = 2;
    const bool unreadable_small = av::small_for_capital(cap, o);
    o.r = 400.0f;
    const bool unreadable_large = av::small_for_capital(cap, o);
    av::Self small_self{};
    small_self.capital = false;
    small_self.r = 1091.0f;
    o.size = 0;
    o.r = 10.0f;
    check(m6_small && m6_300 && !m6_400 && !cap_coded && unreadable_small && !unreadable_large && !av::small_for_capital(small_self, o),
          "pair policy: an M2 of 1091 m skips ships under 364 m (M6 64, 300) but keeps 400 m, every capital-coded ship and the unreadable 400 m; a non-capital self skips nothing");
    const float r_m2 = 307.0f; // an M2 of 307 m keeps an M6 of 140 m (design section 1)
    check(!av::small_pair(true, r_m2, true, false, 140.0f) && av::small_pair(true, 1091.0f, true, false, 140.0f) && !av::small_pair(true, 1091.0f, true, true, 40.0f) &&
              !av::small_pair(false, 1091.0f, true, false, 40.0f) && !av::small_pair(true, 1091.0f, false, false, 40.0f) &&
              !av::small_pair(true, std::numeric_limits<float>::quiet_NaN(), true, false, 40.0f),
          "site E small pair: the ratio on the pair (1091 vs 140 yes, 307 vs 140 no), never for a capital-coded B, a non-capital A, a non-ship B or a NaN radius");
    // the verdict, reasons in the design's order
    const std::uint32_t ap = 0x80000u;
    check(av::small_pair_verdict(ap, core::kOrderMovePos, 0, false, true, 1000, 1200) == av::SmallPair::suppress &&
              av::small_pair_verdict(ap, core::kOrderDock, 0, false, true, 1000, 1250) == av::SmallPair::suppress &&
              av::small_pair_verdict(0x40000u, core::kOrderMovePos, 0, false, true, 1000, 1200) == av::SmallPair::no_autopilot &&
              av::small_pair_verdict(ap, core::kOrderNone, 0, false, true, 1000, 1200) == av::SmallPair::order_not_steered &&
              av::small_pair_verdict(ap, core::kOrderAvoid, 0, false, true, 1000, 1200) == av::SmallPair::order_not_steered &&
              av::small_pair_verdict(ap, core::kOrderRotate, 0, false, true, 1000, 1200) == av::SmallPair::order_not_steered &&
              av::small_pair_verdict(ap, core::kOrderMovePos, 0, true, true, 1000, 1200) == av::SmallPair::class_masked &&
              av::small_pair_verdict(ap | 0x8000000u, core::kOrderMovePos, 0, false, true, 1000, 1200) == av::SmallPair::not_steerable &&
              av::small_pair_verdict(ap | 0x200u, core::kOrderMovePos, 0, false, true, 1000, 1200) == av::SmallPair::not_steerable &&
              av::small_pair_verdict(ap, core::kOrderMovePos, 0x1234, false, true, 1000, 1200) == av::SmallPair::not_steerable &&
              av::small_pair_verdict(ap, core::kOrderMovePos, 0, false, false, 0, 1200) == av::SmallPair::stale &&
              av::small_pair_verdict(ap, core::kOrderMovePos, 0, false, true, 1000, 1251) == av::SmallPair::stale,
          "site E verdict: suppress only with the autopilot bit, a steer-set order other than 0x12, the class unmasked, not deleted/attached/docked and a decision within 250 ms; each failure named");
    // the keep rules
    check(core::gate_capital_keeps_speed(true, true, true, 8.1f) && !core::gate_capital_keeps_speed(true, true, true, 8.2f) &&
              !core::gate_capital_keeps_speed(true, true, true, -1.0f) && !core::gate_capital_keeps_speed(true, false, true, 1.0f) &&
              !core::gate_capital_keeps_speed(true, true, false, 1.0f) && !core::gate_capital_keeps_speed(false, true, true, 1.0f),
          "P2 capital rule: the speed is kept only with the policy on, a validated approach, a capital gate target and the crossing within 8.11 deg");
    // the gate-site rule over a synthetic object: M2 and M1 records are capital gate approaches; the M5 is not; W skips both capitals
    FakeMem mem;
    const std::uint32_t ship_o = 0x2000, flight = 0x3000, gate = 0x4000, types = 0x5000, gphys = 0x4800, phys = 0x2800;
    mem.put(ship_o + 0x48, 7u | (2u << 16));
    mem.put(ship_o + 0x50, flight);
    mem.put(ship_o + 0x70, phys);
    mem.put(ship_o + 0x40, 0x80000);
    mem.put(flight + 0x224, gate);
    mem.put(flight + 0x258, 5);
    mem.put(gate + 0x48, 0x12);
    mem.put(gate + 0x70, gphys);
    mem.puts(gate + 0xa4, 552500);
    mem.puts(gphys + 0x40, 0x10000);
    mem.puts(gphys + 0x54, 0x10000);
    mem.puts(gphys + 0x68, 0x10000);
    bool ok = true;
    std::int32_t pos[3];
    for (unsigned code : {4u, 3u, 0u, 2u, 13u}) { // M2, M1, TL, M0, M7
        mem.put(types + 2 * 0xdb8 + 0x14, code);
        ok = ok && core::gate_target_capital(mem, ship_o, types) && !core::gate_target_noncapital(mem, ship_o, types) &&
             !core::gate_warp_in_offset(mem, ship_o, gate, types, 0, pos);
    }
    mem.put(types + 2 * 0xdb8 + 0x14, 7); // M5
    ok = ok && !core::gate_target_capital(mem, ship_o, types) && core::gate_target_noncapital(mem, ship_o, types) && core::gate_warp_in_offset(mem, ship_o, gate, types, 0, pos);
    mem.put(types + 2 * 0xdb8 + 0x14, 11); // M6: large but not capital
    ok = ok && !core::gate_target_capital(mem, ship_o, types);
    mem.put(types + 2 * 0xdb8 + 0x14, 4);
    mem.put(flight + 0x224, 0x6000);
    mem.put(0x6000 + 0x48, 17);
    ok = ok && !core::gate_target_capital(mem, ship_o, types) && !core::gate_target_capital(mem, ship_o, 0) && !core::gate_target_capital(mem, 0, types);
    check(ok, "gate rule: TL/M0/M1/M2/M7 with a class-0x12 target are capital gate approaches and site W never offsets them; M5 is non-capital (offset), M6 is neither; a non-gate target, no table, no object refused");
    // the capital lead-in window: 6 r (blended from 5 r) and the turning-radius look-ahead
    const float r = 1105.0f, G[3] = {0, 0, 0}, row0[3] = {1, 0, 0}, row1[3] = {0, 1, 0}, row2[3] = {0, 0, 1};
    const float A[3] = {666.0f, -666.0f, -2.0f * (r + 1091.0f)};
    core::GateGeometry g;
    check(core::gate_geometry_capital(A, G, row0, row1, row2, r, 1091.0f, &g) && std::fabs(g.h[2] - 1.0f) < 1e-5f && std::fabs(g.X[2] - (A[2] - 0.75f * r)) < 0.1f,
          "capital lead-in: the grid point (a 1, b -1, depth 2 (r_gate + r_ship)) validates; h = F, X = A - 0.75 r F");
    float P[3], s = 0, e = 0;
    const float omega = 1843.0f / 65536.0f * core::kTwoPi; // rad/s, the Run156 M2
    const float x61[3] = {A[0], A[1], g.X[2] - 6.1f * r}, x59[3] = {A[0], A[1], g.X[2] - 5.9f * r}, x40[3] = {A[0] + 3 * r, A[1], g.X[2] - 4.0f * r};
    const bool refused = !core::gate_lead_in_capital(g, x61, 100.0f, omega, P, &s, &e) && s < -6.0f * r;
    const bool near_join = core::gate_lead_in_capital(g, x59, 100.0f, omega, P, &s, &e) && std::fabs(P[2] - (A[2] - 0.1f * (A[2] - (g.X[2] - 5.9f * r + 2.0f * 100.0f / omega)))) < 2.0f;
    const bool inside = core::gate_lead_in_capital(g, x40, 100.0f, omega, P, &s, &e) && std::fabs(e - 3 * r) < 1.0f && std::fabs(P[0] - A[0]) < 0.01f &&
                        std::fabs(P[2] - (g.X[2] - 4.0f * r + 2.0f * 100.0f / omega)) < 1.0f;
    const bool small_window = !core::gate_lead_in(g, x40, 100.0f, P, &s, &e); // the small-ship window (3 r) refuses the same point
    check(refused && near_join && inside && small_window && 2.0f * 100.0f / omega > 1100.0f && 2.0f * 100.0f / omega < 1200.0f,
          "capital lead-in window: refused beyond 6 r; at 5.9 r the carrot is 90 % of the way to A; at 4 r, 3 r off the line, the carrot is on the line 2 v / omega (1136 m) ahead; the 3 r small-ship window refuses it");
}

// --emit-controller: the controller and the integrator's axis step over a deterministic grid of inputs (every branch:
// W = 0, w < 0, braking, accelerating, cruising, the frame split, the clamp to the remainder, e = 0 with a rate), one
// line per case "ctl e w W acc dt -> e' w' d" for ship_trace_replay.py's port-equality test.
static int emit_controller(const char* path) {
    std::FILE* f = std::fopen(path, "w");
    if (!f) return 2;
    Lcg rng{12345};
    unsigned n = 0;
    for (int i = 0; i < 4000; ++i) {
        const std::int64_t wmax = (i % 23 == 0) ? 0 : 500 + rng.next() % 30000;
        const std::int64_t acc = (i % 17 == 0) ? 0 : fx(wmax, (0x10000 * (1 + rng.next() % 60)) / 100);
        std::int64_t w = std::int64_t(rng.next() % 40000) - 8000;
        if (i % 5 == 0) w = wmax;
        std::int64_t e = std::int64_t(rng.next() % 65536) - 32768;
        if (i % 7 == 0) e = std::int64_t(rng.next() % 200) - 100;
        if (i % 11 == 0) e = 0;
        const std::int64_t dt = (i % 13 == 0) ? 1000 : 1 + rng.next() % 250;
        std::int64_t e1 = e, w1 = w;
        double turned = 0;
        integrate_axis(e1, w1, wmax, acc, dt, &turned);
        std::fprintf(f, "ctl %lld %lld %lld %lld %lld -> %lld %lld\n", (long long)e, (long long)w, (long long)wmax, (long long)acc, (long long)dt,
                     (long long)e1, (long long)w1);
        ++n;
    }
    std::fclose(f);
    std::printf("ship_steering_host controller_rows=%u\n", n);
    return 0;
}
// --emit-trace: three gate runs as ship_trace rows (TS 60 fps seed 1 theta 0 with the module; M5 144 fps seed 7 theta -45 with
// the module on the rotated gate; M6 30 fps seed 1234567 theta +45 vanilla from the order-5 start 1.7 km from B at 65 deg, with
// its order-8 launch) and the TS 60 fps seed 1 dock at a station port rolled 30 deg (scenario 10, the module).
static int emit_trace(const char* path) {
    g_trace = std::fopen(path, "w");
    if (!g_trace) return 2;
    const double std_start[3] = {-3000.0 / 1105.0 * std::sin(30 * M_PI / 180), -0.8, -1.0 - 3000.0 / 1105.0 * std::cos(30 * M_PI / 180)};
    const double dock_start[3] = {0, -0.8, -3.0};
    run_gate(kShips[3], 60, 1, 0, false, false, true, std_start, kGateIdentity, 0.0, 0x8e19d750u);
    run_gate(kShips[0], 144, 7, -45, false, false, true, std_start, kGateRotated, 0.0, 0x3741e840u);
    run_gate(kShips[2], 30, 1234567, 45, false, true, false, dock_start, kGateIdentity, 0.0, 0x436289c8u, -1, 1700.0, 65.0);
    run_dock(kShips[3], 60, 1, 30.0, true, false, 2.5, 10.0, 0x5d0c4a90u); // order 5 to a station port: the match, the final zone
    std::fclose(g_trace);
    g_trace = nullptr;
    std::printf("ship_steering_host trace_rows=%lu\n", g_trace_rows);
    return 0;
}
int main(int argc, char** argv) {
    if (argc == 3 && !std::strcmp(argv[1], "--emit-trace")) return emit_trace(argv[2]);
    if (argc == 3 && !std::strcmp(argv[1], "--emit-controller")) return emit_controller(argv[2]);
    if (argc == 2 && (!std::strcmp(argv[1], "--capital") || !std::strcmp(argv[1], "--capital-noslip"))) {
        // the amendment-13 checks alone (an iteration aid; the full run is the acceptance); --capital-noslip without the slip term
        g_slip = std::strcmp(argv[1], "--capital-noslip") != 0;
        capital_unit_checks();
        for (const ShipType& t : kCapitals)
            for (int fps : kFps)
                for (std::uint32_t seed : kSeeds) capital_transit_scenario(t, fps, seed);
        capital_transit_scenario(kShips[4], 60, kSeeds[0]);
        std::printf("ship_steering_host checks=%u failures=%u slip_gaps=%u\n", checks, failures, slip_gaps);
        std::printf(failures ? "checks FAIL\n" : "checks PASS\n");
        return failures ? 1 : 0;
    }
    controller_regression();
    unit_checks();
    avoidance_unit_checks();
    walk_checks();
    walk_record_code_checks();
    gate_unit_checks();
    capital_unit_checks();
    for (const ShipType& t : kShips) {
        degraded_scenario(t);
        for (int fps : kFps)
            for (std::uint32_t seed : kSeeds) {
                turn_scenario(t, fps, seed, false);
                turn_scenario(t, fps, seed, true);
                arrival_scenario(t, fps, seed);
                formation_scenario(t, fps, seed);
                flyby_scenario(t, fps, seed, 600.0); // the design's geometry
                flyby_scenario(t, fps, seed, 150.0); // a close pass: the body is inside every class's C
                crossing_scenario(t, fps, seed);
                formation_obstacle_scenario(t, fps, seed);
                gate_scenario(t, fps, seed);
                if (!std::strcmp(t.cls, "TS") || !std::strcmp(t.cls, "M6")) gate_dock_scenario(t, fps, seed);
                if (!std::strcmp(t.cls, "TS") || !std::strcmp(t.cls, "M6") || !std::strcmp(t.cls, "M3")) dock_scenario(t, fps, seed);
            }
    }
    for (int fps : kFps)
        for (std::uint32_t seed : kSeeds) capital_cloud_scenario(fps, seed);
    for (const ShipType& t : kCapitals) // amendment 13: the Run156 M2 and M1 through the gate (scenarios A, B, C)
        for (int fps : kFps)
            for (std::uint32_t seed : kSeeds) capital_transit_scenario(t, fps, seed);
    {
        static const ShipType m4 = {"M4", "Buster", 180.0, 25.0, 129.375, 12.0, 20, false};
        for (int fps : kFps)
            for (std::uint32_t seed : kSeeds) gate_dock_scenario(m4, fps, seed);
    }
    amend12_ab_checks();
    std::printf("ship_steering_host checks=%u failures=%u slip_gaps=%u\n", checks, failures, slip_gaps);
    std::printf(failures ? "checks FAIL\n" : "checks PASS\n");
    return failures ? 1 : 0;
}
