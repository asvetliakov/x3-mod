// The gate traffic controller's key-off identity (docs/architecture/gate-traffic-controller.md A.1, D.7 item 8): with
// ship_steering_gates_traffic off the DLL leaves the new SiteE fields (arrival_gate, ctl_pair, ctl_certified, a_arrival) and
// Self.n_ctl at their defaults. Site E's verdict and the avoidance's resolve must then equal amendment 17's, the base commit's
// sources that keyoff_identity.sh extracts into the namespace a17. Random inputs: 400,000 SiteE (every amendment-15/16/17 field
// drawn) and 20,000 obstacle scenes of 3 consecutive resolves each (the state carried), compared field by field (floats bitwise).
// Build and run: keyoff_identity.sh (host c++ -O2; no game, no Wine).
#include "ship_avoidance_core.h"
#include "a17/ship_avoidance_core.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace av = x3m::ship_avoidance::core;
namespace a7 = a17::x3m::ship_avoidance::core;

struct Lcg {
    std::uint32_t s;
    std::uint32_t next() { return (s = s * 1103515245u + 12345u) >> 1; }
    unsigned pick(unsigned k) { return next() % k; }
    float uni(float lo, float hi) { return lo + (hi - lo) * float(next() % 1000001u) / 1000000.0f; }
};

static unsigned e_sweep() {
    Lcg rng{20261013u};
    unsigned diff = 0;
    const std::uint16_t orders[8] = {0, 4, 5, 0x12, 0x14, 0x15, 0x1f, 0x1e};
    const std::uint32_t ages[6] = {0, 100, 240, 340, 2000, 0xffffffffu};
    for (unsigned it = 0; it < 400000; ++it) {
        av::SiteE in;
        a7::SiteE o;
#define SET(f, v) \
    do { \
        const auto val_ = (v); \
        in.f = val_; \
        o.f = val_; \
    } while (0)
        SET(a_order, orders[rng.pick(8)]);
        SET(a_target, rng.pick(2) ? 0x4000u : 0u);
        SET(a_in_table, rng.pick(4) != 0);
        SET(a_age_ms, ages[rng.pick(6)]);
        SET(frame_ms, rng.pick(2) ? 16u : 70u);
        SET(a_capital, rng.pick(2) != 0);
        SET(codes_readable, rng.pick(8) != 0);
        SET(r_a, rng.pick(2) ? 1000.0f : 60.0f);
        SET(b_gate, rng.pick(2) != 0);
        SET(gates_live, rng.pick(4) != 0);
        SET(capital_policy, rng.pick(2) != 0);
        SET(gate_state, rng.pick(3) != 0);
        SET(gate_capital, rng.pick(2) != 0);
        SET(gate_join, rng.pick(2) != 0);
        SET(gate_handover, rng.pick(2) != 0);
        SET(p2_age_ms, ages[rng.pick(6)] * 100u);
        SET(lead_age_ms, ages[rng.pick(6)]);
        SET(gate_slow_join, rng.pick(2) != 0);
        SET(gate_fallback, rng.pick(2) != 0);
        SET(gate_window_ok, rng.pick(4) != 0);
        SET(gate_p2_seen, rng.pick(2) != 0);
        SET(gate_path_ok, rng.pick(4) != 0);
        SET(gate_small_guard, rng.pick(2) != 0);
        SET(lane_exempt, rng.pick(3) == 0);
        SET(b_ship, !in.b_gate && rng.pick(4) != 0);
        SET(b_readable, rng.pick(6) != 0);
        SET(b_capital_code, rng.pick(2) != 0);
        SET(r_b, rng.pick(2) ? 20.0f : 900.0f);
        SET(b_flags, rng.pick(2) ? 0x80000u : rng.pick(2) ? 0x80200u : 0u);
        SET(b_docked, rng.pick(8) == 0 ? 1u : 0u);
        SET(b_target, rng.pick(2) ? 0x4000u : 0u);
        SET(b_order, orders[rng.pick(8)]);
        SET(b_class_masked, rng.pick(8) == 0);
        SET(small_dock_legs, rng.pick(2) != 0);
        SET(b_is_a_leader, rng.pick(6) == 0);
        SET(b_member_current, rng.pick(3) == 0);
        SET(d, rng.pick(3) == 0 ? -1.0f : rng.pick(2) ? 500.0f : 3000.0f);
        SET(avoidance_on, rng.pick(4) != 0);
        SET(b_resolved, rng.pick(3) == 0);
#undef SET
        if (std::strcmp(av::e_verdict_name(av::site_e_verdict(in)), a7::e_verdict_name(a7::site_e_verdict(o))) != 0) ++diff;
    }
    return diff;
}

template <class S>
static void fill_snapshot(S& s, Lcg& rng, unsigned n, const float c[3]) {
    s.sector = 1;
    s.epoch = 1;
    s.count = n;
    s.overflow = 0;
    s.max_r = 0;
    for (unsigned k = 0; k < n; ++k) {
        auto& o = s.items[k];
        for (unsigned i = 0; i < 3; ++i) {
            o.x[i] = c[i] + rng.uni(-6000.0f, 6000.0f);
            o.v[i] = rng.pick(3) ? rng.uni(-120.0f, 120.0f) : 0.0f;
        }
        const unsigned kind = rng.pick(5);
        o.kind = std::uint8_t(kind);
        o.r = kind == 0 ? (rng.pick(3) ? rng.uni(20.0f, 200.0f) : rng.uni(500.0f, 1200.0f)) : rng.uni(50.0f, 3000.0f);
        if (kind != 0)
            for (float& v : o.v) v = 0.0f;
        o.ptr = 0x10000u + 0x40u * k;
        o.leader = kind == 0 && rng.pick(4) == 0 ? 0x10000u + 0x40u * rng.pick(n) : 0u;
        o.order = std::uint16_t(kind == 0 ? (rng.pick(2) ? 4 : rng.pick(2) ? 0x14 : 0x12) : 0);
        o.flags = std::uint8_t(kind == 0 ? (rng.pick(2) ? 4 : 0) | (rng.pick(4) == 0 ? 2 : 0) : 0);
        o.parts = 0;
        o.size = std::uint8_t(kind == 0 ? rng.pick(3) : 0);
        o.pad = 0;
        o.id = o.ptr ^ 0x5a000000u;
        o.member_of = o.leader;
        if (o.r > s.max_r) s.max_r = o.r;
    }
}

template <class Self>
static void fill_self(Self& s, Lcg& rng, const float c[3]) {
    for (unsigned i = 0; i < 3; ++i) {
        s.x[i] = c[i];
        s.slip[i] = rng.pick(2) ? rng.uni(-5.0f, 5.0f) : 0.0f;
    }
    float f[3] = {rng.uni(-1.0f, 1.0f), rng.uni(-1.0f, 1.0f), rng.uni(-1.0f, 1.0f)};
    float n = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (n < 1e-3f) {
        f[2] = 1.0f;
        n = 1.0f;
    }
    for (unsigned i = 0; i < 3; ++i) s.f[i] = f[i] / n;
    s.up[0] = 0.0f;
    s.up[1] = 1.0f;
    s.up[2] = 0.0f;
    s.v = rng.pick(8) ? rng.uni(5.0f, 400.0f) : 0.0f;
    s.capital = rng.pick(2) != 0;
    s.r = s.capital ? rng.uni(300.0f, 1200.0f) : rng.uni(20.0f, 200.0f);
    s.a = rng.uni(2.0f, 40.0f);
    s.omega = rng.uni(0.02f, 0.8f);
    s.alpha = s.omega * rng.uni(0.05f, 0.3f);
    s.ptr = 0x9000;
    s.leader = rng.pick(4) == 0 ? 0x10000u : 0u;
    s.target = rng.pick(2) ? 0x10040u : 0u;
    for (float& v : s.leader_x) v = 0.0f;
    const std::uint16_t orders[5] = {4, 5, 0x14, 0x19, 0x15};
    s.order = orders[rng.pick(5)];
    s.exclude_target = rng.pick(2) != 0;
    s.L_leader = rng.pick(4) == 0 ? rng.uni(0.0f, 3000.0f) : 0.0f;
}

static bool same_f(float a, float b) { return std::memcmp(&a, &b, 4) == 0; }

static unsigned resolve_sweep(unsigned* resolves, unsigned* adjusted) {
    Lcg rng{777u};
    static av::Snapshot snap;
    static a7::Snapshot snap7;
    unsigned diff = 0;
    for (unsigned scene = 0; scene < 20000; ++scene) {
        const float c[3] = {rng.uni(-1e5f, 1e5f), rng.uni(-1e4f, 1e4f), rng.uni(-1e5f, 1e5f)};
        const unsigned n = 1 + rng.pick(120);
        const std::uint32_t seed = rng.next();
        Lcg r1{seed}, r2{seed};
        fill_snapshot(snap, r1, n, c);
        fill_snapshot(snap7, r2, n, c);
        av::Self s;
        a7::Self s7;
        const std::uint32_t seed2 = rng.next();
        Lcg q1{seed2}, q2{seed2};
        fill_self(s, q1, c);
        fill_self(s7, q2, c);
        av::ShipAvoid st;
        a7::ShipAvoid st7;
        st.obj = st7.obj = s.ptr;
        for (unsigned call = 0; call < 3; ++call) {
            float P[3];
            for (unsigned i = 0; i < 3; ++i) P[i] = c[i] + s.f[i] * rng.uni(1000.0f, 30000.0f);
            const std::uint32_t now = 1000u + 200u * call + scene * 5000u;
            const av::Result r = av::resolve(snap, nullptr, 0, s, P, st, now);
            const a7::Result r7 = a7::resolve(snap7, nullptr, 0, s7, P, st7, now);
            ++*resolves;
            *adjusted += r.adjusted ? 1u : 0u;
            bool same = r.adjusted == r7.adjusted && r.unresolved == r7.unresolved && r.overflow == r7.overflow && r.candidates == r7.candidates &&
                        r.conflicts == r7.conflicts && r.tries == r7.tries && same_f(r.t_cpa, r7.t_cpa) && same_f(r.miss, r7.miss) &&
                        same_f(r.C, r7.C) && same_f(r.L, r7.L) && r.excluded_followers == r7.excluded_followers &&
                        r.skipped_small == r7.skipped_small && r.excluded_members == r7.excluded_members && r.lane_exempt == r7.lane_exempt &&
                        r.lane_follow == r7.lane_follow && r.lane_cross == r7.lane_cross && r.lane_seq == r7.lane_seq &&
                        r.lane_apart == r7.lane_apart && r.obstacle == r7.obstacle && r.obstacle_leader == r7.obstacle_leader &&
                        r.obstacle_order == r7.obstacle_order && r.obstacle_kind == r7.obstacle_kind && r.side == r7.side &&
                        r.vertical == r7.vertical && r.emergency == r7.emergency && r.reason == r7.reason &&
                        same_f(r.v_emergency, r7.v_emergency) && r.ctl_excluded == 0 && st.n_resolved == st7.n_resolved &&
                        same_f(st.v_emergency, st7.v_emergency) && st.obstacle == st7.obstacle && same_f(st.L, st7.L);
            for (unsigned i = 0; i < 3; ++i) same = same && same_f(r.P[i], r7.P[i]) && same_f(r.dir[i], r7.dir[i]);
            for (unsigned i = 0; i < st.n_resolved && i < av::kMaxResolved; ++i) same = same && st.resolved[i] == st7.resolved[i];
            if (!same) ++diff;
            for (unsigned i = 0; i < 3; ++i) { // the ship moves on between calls
                s.x[i] += s.f[i] * s.v * 0.2f;
                s7.x[i] = s.x[i];
            }
        }
    }
    return diff;
}

int main() {
    const unsigned e_diff = e_sweep();
    unsigned resolves = 0, adjusted = 0;
    const unsigned r_diff = resolve_sweep(&resolves, &adjusted);
    std::printf("site E (traffic fields at their defaults) vs amendment 17: 400000 random inputs, differences %u\n", e_diff);
    std::printf("avoidance resolve (n_ctl = 0, snapshot capacity %u vs %u) vs amendment 17: %u resolves in 20000 scenes (%u adjusted), "
                "differences %u\n",
                av::kSnapshotCapacity, a7::kSnapshotCapacity, resolves, adjusted, r_diff);
    std::printf(e_diff || r_diff ? "RESULT FAIL\n" : "RESULT PASS\n");
    return e_diff || r_diff ? 1 : 0;
}
