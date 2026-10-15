#pragma once
// The gate traffic controller: the arrival side. D.4's placement (capital_offset, small_side_spawn, capital_at_plane), the arrival
// records and site E's arrival-gate row (Arrivals, arrival_gate_row), and D.5's small-ship pass point while a capital is in the funnel
// (funnel_active, small_b_*).
#include "ship_gate_lane_core.h"
namespace x3m::ship_avoidance::ctl {
// ---- D.4: the arrival placement ------------------------------------------------------------------------------------------------
// The installed gate rows (section 10.9): no collision mesh on the ordinary rows 0..3 and 5..14; the Terran ring on 21..24.
// Section 10.9: the ordinary rows 0..3 and 5..14, row 4 (Xenon, a blended disc only) and rows 15..20 (inactive hubs, an empty body stub)
// carry no collision mesh; the Terran rows 21..24 carry the ring.
// (gate_no_mesh, gate_terran: with the lane's gate solid, above)
// A capital's upward exit offset (m): 0.6 r_gate, on a Terran ring min(0.6 r, h r - w - 50 m) (never negative); an unknown row
// (neither ordinary nor Terran): 0 (vanilla).
inline float capital_offset(float r_gate, float w, std::uint16_t type) noexcept {
    if (!(r_gate > 0.0f) || !sc::finite_f(r_gate)) return 0.0f;
    const float base = kArrivalOffsetR * r_gate;
    if (gate_terran(type)) {
        const float lim = kTerranHoleR * r_gate - w - kTerranMarginM;
        return lim <= 0.0f ? 0.0f : lim < base ? lim : base;
    }
    return gate_no_mesh(type) ? base : 0.0f;
}
// A small ship's side spawn (D.4) when a capital hull is at the plane: local (x, y) = (+-0.87 r, +0.2 r), the side alternating with
// the exit slot index (odd: +R). False (vanilla) when the capital's half-width is above 0.68 r (no free spawn point).
inline bool small_side_spawn(float r_gate, int slot, float w_capital, float* x_local, float* y_local) noexcept {
    if (!(r_gate > 0.0f) || !(w_capital <= kSideSpawnMaxWR * r_gate)) return false;
    *x_local = (slot & 1 ? 1.0f : -1.0f) * kSideSpawnR * r_gate;
    *y_local = kSideSpawnUpR * r_gate;
    return true;
}
// Whether a capital hull is at the gate's plane for a small arrival: a capital launched here within 6 s, or a departing capital on
// order 8 or within 3 s of its order-5 trigger (cap_depart_ms is refreshed every order-8 frame). *w the widest of them (m).
inline bool capital_at_plane(const Gate& g, std::uint32_t now, float* w) noexcept {
    bool at = false;
    *w = 0.0f;
    if (g.cap_arrival_ms && now - g.cap_arrival_ms <= kPlaneArrivalMs) {
        at = true;
        *w = g.cap_arrival_w;
    }
    if (g.cap_depart_ms && now - g.cap_depart_ms <= kPlaneTriggerMs) {
        at = true;
        if (g.cap_depart_w > *w) *w = g.cap_depart_w;
    }
    return at;
}
// The exit point P in the gate frame and back (world m; local = (R, U, F) components of P - G).
__attribute__((always_inline)) inline void to_local(const Gate& g, const float p[3], float l[3]) noexcept {
    const float d[3] = {p[0] - g.G[0], p[1] - g.G[1], p[2] - g.G[2]};
    l[0] = sc::dot3(d, g.R);
    l[1] = sc::dot3(d, g.U);
    l[2] = sc::dot3(d, g.F);
}
__attribute__((always_inline)) inline void to_world(const Gate& g, const float l[3], float p[3]) noexcept {
    for (unsigned i = 0; i < 3; ++i) p[i] = g.G[i] + l[0] * g.R[i] + l[1] * g.U[i] + l[2] * g.F[i];
}

// ---- D.4: the arrival records and site E's arrival-gate row ---------------------------------------------------------------------
constexpr unsigned kArrivals = 64;
struct Arrival {
    std::uint32_t ship = 0, id = 0, gate = 0, launch_ms = 0;
    float w = 0;            // the hull's half-width used by the row (w = r: no width read at a launch)
    std::uint8_t capital = 0;
};
struct Arrivals {
    Arrival a[kArrivals];
    unsigned next = 0, overwritten = 0;
};
inline void record_arrival(Arrivals& t, std::uint32_t ship, std::uint32_t id, std::uint32_t gate, std::uint32_t now, float w, bool capital) noexcept {
    unsigned k = kArrivals;
    for (unsigned i = 0; i < kArrivals && k == kArrivals; ++i)
        if (t.a[i].ship == ship) k = i;
    for (unsigned i = 0; i < kArrivals && k == kArrivals; ++i)
        if (!t.a[i].ship || now - t.a[i].launch_ms > kArrivalWindowMs) k = i;
    if (k == kArrivals) { // the oldest live record goes (counted): its row falls back to the engine's warn
        k = t.next;
        t.next = (t.next + 1) % kArrivals;
        ++t.overwritten;
    }
    t.a[k] = Arrival{ship, id, gate, now ? now : 1, w, std::uint8_t(capital ? 1 : 0)};
}
inline const Arrival* find_arrival(const Arrivals& t, std::uint32_t ship, std::uint32_t now) noexcept {
    for (const Arrival& a : t.a)
        if (a.ship == ship && now - a.launch_ms <= kArrivalWindowMs) return &a;
    return nullptr;
}
// The arrival-gate row (D.4): A launched at B within 60 s, in front of the plane ((x - G) . F < 0) and not closing on it (u . F <= 0),
// and the gate has no collision mesh (rows 0..3, 5..14), or the Terran ring with A's lateral offset + w <= h r - 50 m.
inline bool arrival_gate_row(const Arrival& a, std::uint32_t gate, std::uint32_t now, const float x[3], const float u[3], const float G[3],
                             const float F[3], float r_gate, std::uint16_t type) noexcept {
    if (a.gate != gate || now - a.launch_ms > kArrivalWindowMs) return false;
    const float d[3] = {x[0] - G[0], x[1] - G[1], x[2] - G[2]};
    const float dz = sc::dot3(d, F), uz = sc::dot3(u, F);
    if (!(dz < 0.0f) || !(uz <= 0.0f)) return false;
    if (gate_no_mesh(type)) return true;
    if (!gate_terran(type)) return false;
    const float lat2 = sc::dot3(d, d) - dz * dz;
    const float lat = lat2 > 0.0f ? sc::sqrt_ss(lat2) : 0.0f;
    return sc::finite_f(lat) && lat + a.w <= kTerranHoleR * r_gate - kTerranMarginM;
}

// ---- D.5 (with D.12): a small ship's order-5 pass point while a capital is in the funnel -------------------------------------------
// The funnel is active (D.5: a member released and not yet launched: on its final, the hand-over, order 5 or 8); *side = the side of
// that capital's Q along R (+1 when on the axis: the bias of q_point).
inline bool funnel_active(const Gate& g, float* side) noexcept {
    for (unsigned i = 0; i < g.n; ++i) {
        const Member& m = g.m[i];
        if (m.role != Role::committed || !(m.sb == Sb::funnel || m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch)) continue;
        if (g.lane && m.sb == Sb::funnel && m.lane_s < -6000.0f) continue; // lane stage 1 (section 6.8): Lambda's last 6 km or order 5 / 8
        *side = m.q[0] < 0.0f ? -1.0f : 1.0f;
        return true;
    }
    return false;
}
// B in the gate frame (b: R, U, F components of f+0x240 - G, m). True when B is the engine's pass point (SA_FlyObjectIntoContainer,
// section 10.5: radius 0.55 r_gate, or 0.05 r_gate for subtypes 5..8, on the disc at z = -0.01 r_gate, the lower arc within +-45 deg
// of -U) on the capital's side (sign of b_R = side, |b_R| >= 1 m): out = B mirrored across U (theta -> -theta), the arc radius, the
// height and the engine's spread kept, in the free half of the lower arc.
constexpr float kSmallBTolM = 2.0f, kSmallBTolR = 0.005f;
inline bool small_b_mirror(float r_gate, const float b[3], float side, float out[3]) noexcept {
    if (!(r_gate > 0.0f) || !sc::finite_f(b[0]) || !sc::finite_f(b[1]) || !sc::finite_f(b[2])) return false;
    const float rho = sc::sqrt_ss(b[0] * b[0] + b[1] * b[1]), tol = kSmallBTolM + kSmallBTolR * r_gate;
    const bool arc = sc::fabs_f(rho - 0.55f * r_gate) <= tol || sc::fabs_f(rho - 0.05f * r_gate) <= tol;
    if (!arc || sc::fabs_f(b[2] + 0.01f * r_gate) > tol || !(b[1] < 0.0f) || sc::fabs_f(b[0]) > -b[1] + tol) return false;
    if (sc::fabs_f(b[0]) < 1.0f || (b[0] > 0.0f) != (side > 0.0f)) return false;
    out[0] = -b[0];
    out[1] = b[1];
    out[2] = b[2];
    return true;
}
// The small ship's B decision (D.5 with the review fixes; the DLL's site-A path, unit-checked by the fixture). b: f+0x240 in the gate
// frame (R, U, F; m); d_order: the small ship's distance to B; v its speed; r_small its radius. The capitals that count are the funnel's
// members (released, not yet gone) whose predicted plane passage (their launch less order 8's 0.9 s) comes before the small ship's
// ETA. B stays when it is clear of all of them (w + r_small + 50 m laterally); else its mirror across U when that is clear; else
// nothing (both sides taken). Nothing within 0.55 r_gate of B (Fable 4: the trigger may already fire) or off the engine's arc.
enum class SmallB : std::uint8_t { no_funnel = 0, near, not_arc, clear, mirrored, both_taken, count };
inline const char* small_b_name(SmallB v) noexcept {
    static const char* const n[unsigned(SmallB::count)] = {"no_funnel", "near", "not_arc", "clear", "mirrored", "both_taken"};
    return unsigned(v) < unsigned(SmallB::count) ? n[unsigned(v)] : "?";
}
constexpr float kSmallBNearR = 0.55f, kSmallBMarginM = 50.0f;
inline SmallB small_b_decide(const Gate& g, std::uint32_t now, const float b[3], float d_order, float v, float r_small, float out[3]) noexcept {
    if (!(g.r_gate > 0.0f) || !(d_order > kSmallBNearR * g.r_gate)) return SmallB::near;
    const std::uint32_t eta = now + std::uint32_t(int(1000.0f * d_order / (v > 1.0f ? v : 1.0f))); // via int (x87 audit)
    const Member* cap[kMembers];
    unsigned n = 0;
    for (unsigned i = 0; i < g.n; ++i) {
        const Member& m = g.m[i];
        if (m.role != Role::committed || !m.q_ok) continue;
        if (!(m.sb == Sb::funnel || m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch)) continue;
        const std::uint32_t t_launch = eta_launch(g, m, now);
        if (std::int32_t(t_launch - now) < 0) continue;                // gone
        if (std::int32_t(t_launch - 900u - eta) > 0) continue;         // reaches the plane after the small ship
        cap[n++] = &m;
    }
    if (!n) return SmallB::no_funnel;
    float t[3];
    if (!small_b_mirror(g.r_gate, b, b[0] > 0.0f ? 1.0f : -1.0f, t)) return SmallB::not_arc;
    auto clear = [&](const float p[3]) {
        for (unsigned k = 0; k < n; ++k) {
            const float dx = p[0] - cap[k]->q[0], dy = p[1] - cap[k]->q[1], need = cap[k]->w + r_small + kSmallBMarginM;
            if (dx * dx + dy * dy < need * need) return false;
        }
        return true;
    };
    if (clear(b)) return SmallB::clear;
    if (clear(t)) {
        for (unsigned i = 0; i < 3; ++i) out[i] = t[i];
        return SmallB::mirrored;
    }
    return SmallB::both_taken;
}
// The DLL's memory of its last write per ship: a field still holding it is ours (left alone); any other value is a KC re-issue.
struct SmallBWrite {
    std::int32_t w[3] = {0, 0, 0};
    std::uint8_t ok = 0;
};
__attribute__((always_inline)) inline bool small_b_ours(const SmallBWrite& m, const std::int32_t cur[3]) noexcept {
    return m.ok && cur[0] == m.w[0] && cur[1] == m.w[1] && cur[2] == m.w[2];
}
// Final review item 4: site A's negative adoption memo for a capital on order 4 with no lane member (lane_point_own, lane_point_lc_env):
// the field value found not to be the hull's lane point, the type-width table's generation (a new width read changes the hull's Q) and
// whether the gate's board had an l_c then. A call with the same three costs one compare (no width lookup, hull, plant or lane_q); a
// positive result is not memoised (the adoption records the field as ours).
struct AdoptMemo {
    std::int32_t w[3] = {0, 0, 0};
    std::uint32_t gen = 0;
    std::uint8_t lc = 0, ok = 0;
};
template <class Eval>
inline bool lane_adopt_memo(AdoptMemo& m, const std::int32_t cur[3], std::uint32_t gen, bool lc, Eval&& eval) noexcept {
    if (m.ok && m.gen == gen && m.lc == std::uint8_t(lc) && cur[0] == m.w[0] && cur[1] == m.w[1] && cur[2] == m.w[2]) return false;
    const bool yes = eval();
    m.ok = yes ? 0 : 1;
    if (!yes) {
        for (unsigned i = 0; i < 3; ++i) m.w[i] = cur[i];
        m.gen = gen;
        m.lc = std::uint8_t(lc);
    }
    return yes;
}
} // namespace x3m::ship_avoidance::ctl
