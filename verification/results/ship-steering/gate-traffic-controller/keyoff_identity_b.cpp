// The gate traffic controller's stage B key-off identity (gate-traffic-controller.md B.1): with ship_steering_gates_traffic = 0 the DLL
// never sets SiteE.small_gap_ms (Run161 item 5) and runs none of the controller (no board, no sequencer, no stage-B steering, no small-ship
// B rewrite, site W as before). Site E's verdict, the avoidance's resolve, the steer set and site B's clamp must then equal main ad0e131a
// (stage A + amendment 18, installed as Run161), the base commit's sources keyoff_identity_b.sh extracts into the namespace sa. The
// sweeps are amendment 18's (docking-approach/keyoff_identity.cpp) against the new base: 400,000 SiteE (every field both builds share
// drawn, the traffic fields included), 20,000 obstacle scenes of 3 resolves (floats bitwise), the order policy of all 65,536 order
// words, 200,000 site-B clamps; question 3 is amendment 18's key-on/off comparison, unchanged. Host c++ -O2; no game, no Wine.
#include "ship_avoidance_core.h"
#include "sa/ship_avoidance_core.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace av = x3m::ship_avoidance::core;
namespace sa_av = sa::x3m::ship_avoidance::core;
namespace sc = x3m::ship_steering::core;
namespace sa_sc = sa::x3m::ship_steering::core;

struct Lcg {
    std::uint32_t s;
    std::uint32_t next() { return (s = s * 1103515245u + 12345u) >> 1; }
    unsigned pick(unsigned k) { return next() % k; }
    float uni(float lo, float hi) { return lo + (hi - lo) * float(next() % 1000001u) / 1000000.0f; }
};

static unsigned e_sweep() {
    Lcg rng{20261013u};
    unsigned diff = 0;
    const std::uint16_t orders[9] = {0, 4, 5, 0x12, 0x14, 0x15, 0x1f, 0x1e, 0x1d};
    const std::uint32_t ages[6] = {0, 100, 240, 340, 2000, 0xffffffffu};
    for (unsigned it = 0; it < 400000; ++it) {
        av::SiteE in;
        sa_av::SiteE o;
#define SET(f, v) \
    do { \
        const auto val_ = (v); \
        in.f = val_; \
        o.f = val_; \
    } while (0)
        SET(a_order, orders[rng.pick(9)]);
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
        SET(b_order, orders[rng.pick(9)]);
        SET(b_class_masked, rng.pick(8) == 0);
        SET(small_dock_legs, rng.pick(2) != 0);
        SET(b_is_a_leader, rng.pick(6) == 0);
        SET(b_member_current, rng.pick(3) == 0);
        SET(d, rng.pick(3) == 0 ? -1.0f : rng.pick(2) ? 500.0f : 3000.0f);
        SET(avoidance_on, rng.pick(4) != 0);
        SET(b_resolved, rng.pick(3) == 0);
        SET(arrival_gate, rng.pick(4) == 0);
        SET(ctl_pair, rng.pick(4) == 0);
        SET(ctl_certified, rng.pick(2) != 0);
        SET(a_arrival, rng.pick(5) == 0);
#undef SET
        if (std::strcmp(av::e_verdict_name(av::site_e_verdict(in)), sa_av::e_verdict_name(sa_av::site_e_verdict(o))) != 0) ++diff;
    }
    return diff;
}

static bool build_parts_any(av::PartSet& p, std::uint32_t st, float r, const float pos[3], const float c[][3], const float rr[], unsigned n) {
    return av::build_parts(p, st, r, pos, c, rr, n);
}
static bool build_parts_any(sa_av::PartSet& p, std::uint32_t st, float r, const float pos[3], const float c[][3], const float rr[], unsigned n) {
    return sa_av::build_parts(p, st, r, pos, c, rr, n);
}
// One scene: obstacles around c; stations get a part model with probability 2/3 (the same parts in both namespaces).
template <class S, class P>
static void fill_scene(S& s, P* parts, unsigned* n_parts, Lcg& rng, unsigned n, const float c[3]) {
    s.sector = 1;
    s.epoch = 1;
    s.count = n;
    s.overflow = 0;
    s.max_r = 0;
    *n_parts = 0;
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
        if (kind == 1 && *n_parts < 16 && rng.pick(3) != 0) {
            const unsigned np = 1 + rng.pick(12);
            float pc[12][3], pr[12];
            for (unsigned p = 0; p < np; ++p) {
                for (unsigned i = 0; i < 3; ++i) pc[p][i] = o.x[i] + rng.uni(-0.55f, 0.55f) * o.r;
                pr[p] = rng.uni(0.05f, 0.4f) * o.r;
            }
            if (build_parts_any(parts[*n_parts], o.ptr, o.r, o.x, pc, pr, np)) {
                o.flags = 1;
                o.parts = std::uint16_t(*n_parts);
                ++*n_parts;
            }
        }
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
    const std::uint16_t orders[6] = {4, 5, 0x14, 0x19, 0x15, 0x1d};
    s.order = orders[rng.pick(6)];
    s.exclude_target = rng.pick(2) != 0;
    s.L_leader = rng.pick(4) == 0 ? rng.uni(0.0f, 3000.0f) : 0.0f;
}

static bool same_f(float a, float b) { return std::memcmp(&a, &b, 4) == 0; }
template <class R, class RS, class St, class StS>
static bool same_result(const R& r, const RS& r7, const St& st, const StS& st7) {
    bool same = r.adjusted == r7.adjusted && r.unresolved == r7.unresolved && r.overflow == r7.overflow && r.candidates == r7.candidates &&
                r.conflicts == r7.conflicts && r.tries == r7.tries && same_f(r.t_cpa, r7.t_cpa) && same_f(r.miss, r7.miss) &&
                same_f(r.C, r7.C) && same_f(r.L, r7.L) && r.excluded_followers == r7.excluded_followers &&
                r.skipped_small == r7.skipped_small && r.excluded_members == r7.excluded_members && r.lane_exempt == r7.lane_exempt &&
                r.lane_follow == r7.lane_follow && r.lane_cross == r7.lane_cross && r.lane_seq == r7.lane_seq && r.lane_apart == r7.lane_apart &&
                r.ctl_excluded == r7.ctl_excluded && r.obstacle == r7.obstacle && r.obstacle_leader == r7.obstacle_leader &&
                r.obstacle_order == r7.obstacle_order && r.obstacle_kind == r7.obstacle_kind && r.side == r7.side && r.vertical == r7.vertical &&
                r.emergency == r7.emergency && r.reason == r7.reason && same_f(r.v_emergency, r7.v_emergency) && st.n_resolved == st7.n_resolved &&
                same_f(st.v_emergency, st7.v_emergency) && st.obstacle == st7.obstacle && same_f(st.L, st7.L);
    for (unsigned i = 0; i < 3; ++i) same = same && same_f(r.P[i], r7.P[i]) && same_f(r.dir[i], r7.dir[i]);
    for (unsigned i = 0; i < st.n_resolved && i < av::kMaxResolved; ++i) same = same && st.resolved[i] == st7.resolved[i];
    return same;
}
static av::Snapshot snap;
static sa_av::Snapshot snap7;
static av::PartSet parts[16];
static sa_av::PartSet parts7[16];

static unsigned resolve_sweep(unsigned* resolves, unsigned* adjusted, unsigned* with_parts) {
    Lcg rng{777u};
    unsigned diff = 0;
    for (unsigned scene = 0; scene < 20000; ++scene) {
        const float c[3] = {rng.uni(-1e5f, 1e5f), rng.uni(-1e4f, 1e4f), rng.uni(-1e5f, 1e5f)};
        const unsigned n = 1 + rng.pick(120);
        const std::uint32_t seed = rng.next();
        Lcg r1{seed}, r2{seed};
        unsigned np = 0, np7 = 0;
        fill_scene(snap, parts, &np, r1, n, c);
        fill_scene(snap7, parts7, &np7, r2, n, c);
        *with_parts += np ? 1u : 0u;
        av::Self s;
        sa_av::Self s7;
        const std::uint32_t seed2 = rng.next();
        Lcg q1{seed2}, q2{seed2};
        fill_self(s, q1, c);
        fill_self(s7, q2, c);
        if (np && rng.pick(2)) s.target = s7.target = snap.items[rng.pick(n)].ptr; // the order's target is any item, a station too
        av::ShipAvoid st;
        sa_av::ShipAvoid st7;
        st.obj = st7.obj = s.ptr;
        for (unsigned call = 0; call < 3; ++call) {
            float P[3];
            for (unsigned i = 0; i < 3; ++i) P[i] = c[i] + s.f[i] * rng.uni(1000.0f, 30000.0f);
            const std::uint32_t now = 1000u + 200u * call + scene * 5000u;
            const av::Result r = av::resolve(snap, parts, np, s, P, st, now);
            const sa_av::Result r7 = sa_av::resolve(snap7, parts7, np7, s7, P, st7, now);
            ++*resolves;
            *adjusted += r.adjusted ? 1u : 0u;
            if (!same_result(r, r7, st, st7) || r.dock_kept != 0) ++diff;
            for (unsigned i = 0; i < 3; ++i) {
                s.x[i] += s.f[i] * s.v * 0.2f;
                s7.x[i] = s.x[i];
            }
        }
    }
    return diff;
}

// Question 3: 0x1d to a station with a part model, key on (dock_target set, as dock_bind does) against key off, per distance band.
struct IdleBands {
    unsigned out_n = 0, out_diff = 0;   // beyond the avoidance's reach toward its container (L_dock + sqrt(3) C_dock): "not near"
    unsigned far_n = 0, far_diff = 0;   // within that reach but beyond 5 r_C (fast ships whose lookahead reaches the station)
    unsigned near_n = 0, near_diff = 0; // within 5 r_C (the queue's radius)
};
static void idle_sweep(IdleBands& b5) {
    Lcg rng{4242u};
    for (unsigned scene = 0; scene < 20000; ++scene) {
        const float c[3] = {rng.uni(-1e5f, 1e5f), rng.uni(-1e4f, 1e4f), rng.uni(-1e5f, 1e5f)};
        const unsigned n = 2 + rng.pick(60);
        const std::uint32_t seed = rng.next();
        Lcg r1{seed};
        unsigned np = 0;
        fill_scene(snap, parts, &np, r1, n, c);
        // the container: a station with a model at a random distance along a random bearing, 1..60 km
        av::Obstacle& o = snap.items[0];
        o = av::Obstacle{};
        o.kind = std::uint8_t(av::Kind::station);
        o.r = rng.uni(800.0f, 3500.0f);
        o.ptr = 0x10000u;
        const float dist = rng.uni(1.0f, 60.0f) * 1000.0f;
        float b[3] = {rng.uni(-1.0f, 1.0f), rng.uni(-0.3f, 0.3f), rng.uni(-1.0f, 1.0f)};
        const float bn = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]) + 1e-6f;
        for (unsigned i = 0; i < 3; ++i) o.x[i] = c[i] + b[i] / bn * dist;
        if (np < 16) {
            float pc[12][3], pr[12];
            const unsigned k = 1 + rng.pick(12);
            for (unsigned p = 0; p < k; ++p) {
                for (unsigned i = 0; i < 3; ++i) pc[p][i] = o.x[i] + rng.uni(-0.55f, 0.55f) * o.r;
                pr[p] = rng.uni(0.05f, 0.4f) * o.r;
            }
            if (av::build_parts(parts[np], o.ptr, o.r, o.x, pc, pr, k)) {
                o.flags = 1;
                o.parts = std::uint16_t(np);
                ++np;
            }
        }
        if (o.r > snap.max_r) snap.max_r = o.r;
        av::Self off;
        const std::uint32_t seed2 = rng.next();
        Lcg q1{seed2};
        fill_self(off, q1, c);
        off.order = 0x1d;
        off.exclude_target = false;
        off.target = o.ptr;
        off.leader = 0;
        off.L_leader = 0.0f;
        if (rng.pick(2)) { // heading at the station half the time (the queue's case)
            for (unsigned i = 0; i < 3; ++i) off.f[i] = b[i] / bn;
            const float fn = std::sqrt(off.f[0] * off.f[0] + off.f[1] * off.f[1] + off.f[2] * off.f[2]);
            for (float& v : off.f) v /= fn;
        }
        av::Self on = off;
        on.dock_target = o.ptr; // dock_bind on 0x1d at a station: the snapshot's model, no horizon cap
        av::ShipAvoid st_on, st_off;
        st_on.obj = st_off.obj = off.ptr;
        for (unsigned call = 0; call < 3; ++call) {
            float dx[3];
            for (unsigned i = 0; i < 3; ++i) dx[i] = o.x[i] - on.x[i];
            const float d_now = std::sqrt(dx[0] * dx[0] + dx[1] * dx[1] + dx[2] * dx[2]);
            const float C_dock = av::dock_part_clearance(on.r, o.r);
            const float reach = av::lookahead(on.v, on.a, on.omega, on.r, C_dock, on.alpha) + av::kRootSlack * C_dock;
            float P[3];
            for (unsigned i = 0; i < 3; ++i) P[i] = c[i] + off.f[i] * 2200.0f;
            const std::uint32_t now = 1000u + 200u * call + scene * 5000u;
            const av::Result a = av::resolve(snap, parts, np, on, P, st_on, now);
            const av::Result z = av::resolve(snap, parts, np, off, P, st_off, now);
            bool same = a.adjusted == z.adjusted && a.unresolved == z.unresolved && a.conflicts == z.conflicts && a.candidates == z.candidates &&
                        a.obstacle == z.obstacle && st_on.n_resolved == st_off.n_resolved && same_f(st_on.v_emergency, st_off.v_emergency);
            for (unsigned i = 0; i < 3; ++i) same = same && same_f(a.P[i], z.P[i]);
            if (d_now > reach) {
                ++b5.out_n;
                b5.out_diff += same ? 0u : 1u;
            } else if (d_now > av::kDockIdleNear * o.r) {
                ++b5.far_n;
                b5.far_diff += same ? 0u : 1u;
            } else {
                ++b5.near_n;
                b5.near_diff += same ? 0u : 1u;
            }
            for (unsigned i = 0; i < 3; ++i) {
                on.x[i] += on.f[i] * on.v * 0.2f;
                off.x[i] = on.x[i];
            }
        }
    }
}

// Site B's clamp (amendment 18's relative ramp): a stationary container (velocity null with the key off, zero with it on) must give stage
// A's clamp bit for bit, on random decisions of every flag combination; and a zero container velocity gives the reference speed 0.
static unsigned clamp_sweep(unsigned* n) {
    Lcg rng{9001u};
    unsigned diff = 0;
    const std::uint16_t flags[8] = {0, 1, 2, 3, 2 | 32, 3 | 16, 4 | 2, 8 | 2};
    const std::uint16_t orders[5] = {4, 5, 0x19, 0x14, 0x1d};
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    for (unsigned it = 0; it < 200000; ++it) {
        sc::Ship a;
        sa_sc::Ship b;
        a.obj = b.obj = 0x9000;
        a.stamp_ms = b.stamp_ms = 100000u + rng.pick(1000);
        a.order = b.order = orders[rng.pick(5)];
        a.flags = b.flags = flags[rng.pick(8)];
        for (unsigned i = 0; i < 3; ++i) a.P[i] = b.P[i] = rng.uni(-20000.0f, 20000.0f);
        a.a_lin = b.a_lin = rng.pick(8) ? rng.uni(1.0f, 60.0f) : 0.0f;
        a.d_arr = b.d_arr = rng.uni(0.0f, 500.0f);
        a.v_ref = b.v_ref = rng.pick(3) ? 0.0f : rng.uni(0.0f, 300.0f);
        a.v_corner = b.v_corner = rng.pick(2) ? 0.0f : rng.uni(5.0f, 300.0f);
        a.v_slip = b.v_slip = rng.pick(2) ? 0.0f : rng.uni(-20.0f, 20.0f);
        a.v_emergency = b.v_emergency = rng.pick(4) ? 0.0f : rng.uni(5.0f, 100.0f);
        const float x[3] = {rng.uni(-20000.0f, 20000.0f), rng.uni(-2000.0f, 2000.0f), rng.uni(-20000.0f, 20000.0f)};
        const std::uint16_t order = rng.pick(4) ? a.order : orders[rng.pick(5)];
        const std::uint32_t now = a.stamp_ms + rng.pick(400);
        const float vb = sa_sc::clamp_for(b, x, order, now, 250);
        const float v0 = sc::clamp_for(a, x, order, now, 250);
        const float vz = sc::clamp_for(a, x, order, now, 250, zero);
        ++*n;
        if (!same_f(v0, vb) || !same_f(vz, vb)) ++diff;
        float f[3] = {rng.uni(-1.0f, 1.0f), rng.uni(-1.0f, 1.0f), rng.uni(-1.0f, 1.0f)};
        if (sc::container_ref_speed(zero, f, 300.0f) != 0.0f) ++diff;
    }
    return diff;
}
// Review 2: a ship target on order 4 is a container only with the dock bit [obj+0x40] & 0x100000; every other flag word keeps today's rules.
static unsigned dock_flag_sweep() {
    Lcg rng{31337u};
    unsigned diff = 0;
    for (unsigned it = 0; it < 100000; ++it) {
        const std::uint32_t flags = (rng.next() << 1) ^ rng.next();
        const bool want = (flags & 0x100000u) != 0;
        if (av::dock_container(4, 7, flags) != want || av::dock_container(0x1d, 7, flags) || !av::dock_container(4, 6, flags)) ++diff;
    }
    return diff;
}
static unsigned policy_sweep() {
    unsigned diff = 0;
    for (unsigned o = 0; o < 65536; ++o) {
        const sc::OrderPolicy a = sc::order_policy(std::uint16_t(o));
        const sa_sc::OrderPolicy b = sa_sc::order_policy(std::uint16_t(o));
        if (a.steered != b.steered || a.arrival_ramp != b.arrival_ramp || a.roll_zero != b.roll_zero || a.avoidance != b.avoidance) ++diff;
    }
    return diff;
}

int main() {
    const unsigned e_diff = e_sweep();
    unsigned resolves = 0, adjusted = 0, with_parts = 0;
    const unsigned r_diff = resolve_sweep(&resolves, &adjusted, &with_parts);
    const unsigned p_diff = policy_sweep();
    const unsigned f_diff = dock_flag_sweep();
    unsigned clamps = 0;
    const unsigned c_diff = clamp_sweep(&clamps);
    IdleBands b5;
    idle_sweep(b5);
    std::printf("site E (dock = none) vs main ad0e131a (stage A + amendment 18): 400000 random inputs (traffic fields drawn), differences %u\n", e_diff);
    std::printf("avoidance resolve (dock_target 0) vs main ad0e131a (stage A + amendment 18): %u resolves in 20000 scenes (%u with station part models, %u adjusted), "
                "differences %u\n", resolves, with_parts, adjusted, r_diff);
    std::printf("steer set (order_policy over 65536 order words) vs main ad0e131a (stage A + amendment 18): differences %u\n", p_diff);
    std::printf("site B clamp, a stationary container (no velocity: key off; zero velocity: key on) vs main ad0e131a (stage A + amendment 18): %u random decisions, "
                "differences %u (bitwise); the reference speed of a zero velocity is 0\n", clamps, c_diff);
    std::printf("ship containers: order 4 to a ship counts only with the dock bit 0x100000 (100000 random flag words), differences %u\n", f_diff);
    std::printf("question 3, 0x1d to a station with a model, key on vs key off: beyond the avoidance's reach toward it %u decisions, "
                "differences %u; within that reach but beyond 5 r_C %u, differences %u; within 5 r_C %u, differences %u (the intended change: "
                "the body detour and the dock margin)\n",
                b5.out_n, b5.out_diff, b5.far_n, b5.far_diff, b5.near_n, b5.near_diff);
    const bool pass = !e_diff && !r_diff && !p_diff && !c_diff && !f_diff && !b5.out_diff;
    std::printf(pass ? "RESULT PASS\n" : "RESULT FAIL\n");
    return pass ? 0 : 1;
}
