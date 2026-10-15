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
    unsigned next = 0, overwritten = 0, overwritten_cap = 0; // (overwritten_cap: of them a capital's record: section 13's test lost, counted)
};
inline void record_arrival(Arrivals& t, std::uint32_t ship, std::uint32_t id, std::uint32_t gate, std::uint32_t now, float w, bool capital) noexcept {
    unsigned k = kArrivals;
    for (unsigned i = 0; i < kArrivals && k == kArrivals; ++i)
        if (t.a[i].ship == ship) k = i;
    // (section 13: a capital's record is reused only past the vanilla test's reach, kArrModeMs + the largest step; any other past the
    // arrival-gate row's 60 s, as before)
    for (unsigned i = 0; i < kArrivals && k == kArrivals; ++i)
        if (!t.a[i].ship || now - t.a[i].launch_ms > (t.a[i].capital ? kArrModeMs + kStepClampMs : kArrivalWindowMs)) k = i;
    if (k == kArrivals) { // a live record goes, round-robin over the slots (not the oldest; counted): its row falls back to the engine's warn,
                          // a capital's also its arrival-side test (that ship back on today's lane)
        k = t.next;
        t.next = (t.next + 1) % kArrivals;
        ++t.overwritten;
        if (t.a[k].capital) ++t.overwritten_cap;
    }
    t.a[k] = Arrival{ship, id, gate, now ? now : 1, w, std::uint8_t(capital ? 1 : 0)};
}
inline const Arrival* find_arrival(const Arrivals& t, std::uint32_t ship, std::uint32_t now) noexcept {
    for (const Arrival& a : t.a)
        if (a.ship == ship && now - a.launch_ms <= kArrivalWindowMs) return &a;
    return nullptr;
}
// Convoy-gate-transit.md section 13.1 (orchestrator decision 2026-10-16: the arrival side is vanilla): whether the ship (its object id)
// launched at this gate within kArrModeMs + the step of now (its own arrival record). The DLL's decide() latches it per approach
// (GateState::arr_vanilla): such a capital takes the vanilla path (no capital lead-in, no commit, no escort entry: never a board member as
// committed or escort). A backward clock or no record: false. <= 64 compares, behind the arr_mode_ix_ bitmap's test.
inline bool arr_record_mode(const Arrivals& t, std::uint32_t ship, std::uint32_t id, std::uint32_t gate, std::uint32_t now,
                            std::uint32_t step_ms) noexcept {
    const std::uint32_t reach = kArrModeMs + (step_ms < kStepClampMs ? step_ms : kStepClampMs);
    for (const Arrival& a : t.a)
        if (a.ship == ship && a.id == id && a.gate == gate && a.capital && now - a.launch_ms <= reach) return true;
    return false;
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
// The funnel is active (D.5: a member released and not yet launched: on its final, the hand-over, order 5 or 8); funnel_member: the
// first such member; *side (funnel_active) = the side of that capital's Q along R (+1 when on the axis: the bias of q_point).
inline const Member* funnel_member(const Gate& g) noexcept {
    for (unsigned i = 0; i < g.n; ++i) {
        const Member& m = g.m[i];
        if (m.role != Role::committed || !(m.sb == Sb::funnel || m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch)) continue;
        if (g.lane && m.sb == Sb::funnel && m.lane_s < -6000.0f) continue; // lane stage 1 (section 6.8): Lambda's last 6 km or order 5 / 8
        return &m;
    }
    return nullptr;
}
inline bool funnel_active(const Gate& g, float* side) noexcept {
    const Member* m = funnel_member(g);
    if (!m) return false;
    *side = m->q[0] < 0.0f ? -1.0f : 1.0f;
    return true;
}
// Convoy-gate-transit.md section 12.2 (round B; a lane gate only, the key off keeps D.5 as it was): the lane's column runs through the
// small ships' A, so while the lane is in use a small ship's order-4 lead-in ends on the free side's edge of A's arrival sphere,
// A + side 0.68 r_gate R (the engine's 0.75 r test fires there; A itself is not written), its order-5 B goes to the free side's arc edge
// (|theta| = 45 deg) when B and its mirror are both taken, and while a capital passes first the ship is held (small_hold_cap). The
// free side: away from the lane capital's Q when Q lies off the axis by
// more than kSmallSideTieM, else the ship's own side (no column crossing; -R on the axis itself, D.5's side for small ships).
// Deviation from the design's "while the funnel is active" (fixture): the lead-in is 3 r long and a capital 6 km out on Lambda (or an
// escort capital on Lambda, not yet committed) reaches the column's end while a small ship flies it; a lead-in that switched back to
// the spread line between two capitals ran the small ship into the column. So the side lead-in starts with any lane member of the gate
// (committed or an escort, on Lambda or past it) and, with its side, is kept for the rest of the approach (*latch, per ship).
// The per-ship spread is kept along U (four levels from the spread index's low two bits, 0.107 r = 118 m apart, at most kSmallSideUR r
// off the approach height), so two small ships on one side do not converge on one line and warn each other (the fixture's witness: a
// TS trader and a TS escort on the +R line, their warns' 0x12 into the column); and the lateral is 0.68 r instead of the design's
// 0.7 r so that the line's end stays 0.054 r inside A's 0.75 r arrival sphere with the U offset (sqrt(0.68^2 + 0.16^2) = 0.699).
constexpr float kSmallSideR = 0.68f, kSmallSideUR = 0.16f, kSmallSideTieM = 50.0f, kSmallEdgeCos = 0.70710678f;
__attribute__((always_inline)) inline float small_free_side(float q_r, float x_r) noexcept {
    if (q_r > kSmallSideTieM) return -1.0f;
    if (q_r < -kSmallSideTieM) return 1.0f;
    return x_r > 0.0f ? 1.0f : -1.0f;
}
// The lane capital the side is taken from: the funnel's (funnel_member), else the first lane member with its final known.
inline const Member* lane_side_member(const Gate& g) noexcept {
    if (const Member* f = funnel_member(g)) return f;
    for (unsigned i = 0; i < g.n; ++i) {
        const Member& m = g.m[i];
        if (m.ship && m.ln && m.q_ok && (m.role == Role::committed || m.role == Role::escort) && m.sb != Sb::none) return &m;
    }
    return nullptr;
}
// The side lead-in: true while it is on for this approach (*latch: 0 = not yet, else the side kept); off = the lead-in line's offset
// (world, m) that replaces the per-ship spread, *side the free side. x_r: the ship's R coordinate in the gate frame (m); k its spread
// index (sc::gate_spread_index).
inline bool small_side_lead(const Gate& g, float x_r, unsigned k, std::int8_t* latch, float off[3], float* side) noexcept {
    if (!g.lane || !(g.r_gate > 0.0f) || !sc::finite_f(x_r)) return false;
    if (!*latch) {
        const Member* m = lane_side_member(g);
        if (!m) return false;
        *latch = small_free_side(m->q[0], x_r) > 0.0f ? 1 : -1;
    }
    *side = *latch > 0 ? 1.0f : -1.0f;
    const float u = (float(k & 3u) - 1.5f) * (kSmallSideUR / 1.5f) * g.r_gate; // (four levels: k's low two bits)
    for (unsigned i = 0; i < 3; ++i) off[i] = *side * kSmallSideR * g.r_gate * g.R[i] + u * g.U[i];
    return true;
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
// frame (R, U, F; m); d_order: the small ship's distance to B; v its speed; r_small its radius; xl its lateral (R, U) in the gate frame;
// side_latch the side its order-4 lead-in kept (0: none, derived as small_side_lead does).
// The capitals that count are the funnel's members (released, not yet gone) whose predicted plane passage (their launch less order 8's
// 0.9 s) comes before the small ship's ETA. B stays when it is clear of all of them (w + r_small + 50 m laterally); else its mirror
// across U when that is clear; else nothing (both sides taken). Nothing within 0.55 r_gate of B (Fable 4: the trigger may already fire)
// or off the engine's arc.
// Section 12.2 (round B, a lane gate only; the key off keeps the rule above):
// * a capital counts while its hull's front reaches the plane before the small ship has left it (its passage less (L + r_small + 50 m)
//   / v, against the small ship's ETA + order 8's 0.9 s: in the fixture a TS met an M1 whose centre was still 1 km out), and a lane
//   member not yet released (an escort or a joiner on Lambda) counts with its passage at its own speed, on Lambda's l_c;
// * "clear" holds for the small ship's whole lateral path from xl to the point (its order-5 leg, which otherwise crosses the column from
//   the free side to a B beyond it) with kSmallPathMarginM;
// * with B and its mirror taken, B goes to the free side's arc edge (|theta| = 45 deg, the arc radius and height kept): `edge` when its
//   path is clear, else `hold` (the edge written all the same, *t_launch the latest predicted launch among the capitals taking it, *f_in
//   the fraction of the path before the first taken point).
enum class SmallB : std::uint8_t { no_funnel = 0, near, not_arc, clear, mirrored, both_taken, edge, hold, count };
inline const char* small_b_name(SmallB v) noexcept {
    static const char* const n[unsigned(SmallB::count)] = {"no_funnel", "near", "not_arc", "clear", "mirrored", "both_taken", "edge", "hold"};
    return unsigned(v) < unsigned(SmallB::count) ? n[unsigned(v)] : "?";
}
constexpr float kSmallBNearR = 0.55f, kSmallBMarginM = 50.0f;
// Section 12.2's lateral path test (the lane) takes a wider margin than D.5's point test: in the fixture a small ship reaches its order-5
// leg still converging on the side line and its velocity's lag carries it 170-360 m further in while it turns toward B (an M6, a TS),
// so with 50 m the path test passed and the ship met the column; with 200 m a Titan's (w 579 m) edge path is taken, and the small ship
// holds instead until the capital has launched.
constexpr float kSmallPathMarginM = 200.0f;
// The warn zone at the lead-in's end (order 4): the engine's box test of a small A against a capital's hull, 1.2 (w + r_small) beside
// the hull's axis plus the reach |v_A| + b (kSmallZoneM: about 100 m at the gate plus 50 m); X_s at 0.68 r lies inside it for a Titan or
// an M1 passing on the column, and the warned small ship's 0x12 escape met the capital in the fixture. While a counted capital's zone
// holds X_s the small ship is held (below).
constexpr float kSmallZoneM = 150.0f, kSmallZoneBox = 1.2f;
constexpr std::uint32_t kSmallHullLeadMaxMs = 60000; // (the hull lead's cap: a capital crawling at the floor speed)
// (the lane's gap margin: a small ship goes between two capitals only with this much more than its ETA, which assumes its maximum speed
// from the start; fixture: an M6 released 2 s ahead of a Titan's hull overshot its side line by 260 m while converging and was warned)
constexpr std::uint32_t kSmallGapMs = 3000;
// The counted capitals for a small ship reaching the plane at eta (game ms): cap[], their predicted launches t[] and the lateral (R, U;
// m) their hulls pass the plane at, ql[]; returns the count. lane: section 12.2's rule (above).
inline unsigned small_caps(const Gate& g, std::uint32_t now, std::uint32_t eta, float r_small, bool lane, const Member* cap[], std::uint32_t t[],
                           float ql[][2]) noexcept {
    unsigned n = 0;
    for (unsigned i = 0; i < g.n; ++i) {
        const Member& m = g.m[i];
        if (!m.ship || !m.q_ok) continue;
        std::uint32_t t_launch;
        bool on_lc = false;
        if (m.role == Role::committed && (m.sb == Sb::funnel || m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch)) {
            t_launch = eta_launch(g, m, now);
            on_lc = lane && m.lane_esc;
        } else if (lane && m.ln && m.st.ms && (m.role == Role::committed || m.role == Role::escort) && (m.sb == Sb::inbound || m.sb == Sb::funnel)) {
            const float dg[3] = {m.st.x[0] - g.G[0], m.st.x[1] - g.G[1], m.st.x[2] - g.G[2]};
            const float vs = m.st.v > kYieldFloorMs ? m.st.v : kYieldFloorMs, ms = 1000.0f * sc::sqrt_ss(sc::dot3(dg, dg)) / vs;
            if (!(ms < 600000.0f)) continue;
            t_launch = now + std::uint32_t(int(ms)) + 900u; // via int (x87 audit)
            on_lc = g.lane_c_ok && (m.role == Role::escort || m.lane_esc);
        } else
            continue;
        if (std::int32_t(t_launch - now) < 0) continue; // gone
        std::uint32_t lead = 0;
        if (lane) {
            const float vc = m.st.v > kYieldFloorMs ? m.st.v : kYieldFloorMs, ms = 1000.0f * (m.L + r_small + kSmallBMarginM) / vc;
            lead = 900u + (ms < float(kSmallHullLeadMaxMs) ? std::uint32_t(int(ms)) : kSmallHullLeadMaxMs); // via int (x87 audit)
        }
        if (std::int32_t(t_launch - 900u - lead - eta - (lane ? kSmallGapMs : 0u)) > 0) continue; // reaches the plane after the small ship
        t[n] = t_launch;
        ql[n][0] = on_lc ? g.lane_c[0] : m.q[0];
        ql[n][1] = on_lc ? g.lane_c[1] : m.q[1];
        cap[n++] = &m;
    }
    return n;
}
// Whether the lateral segment p0 -> p1 (gate frame: R, U; m; p0 = nullptr: the point p1 alone, D.5's test) is clear of the counted
// capitals' laterals by w + r_small + 50 m (a point; the lane's path: kSmallPathMarginM); *t_last (when given) the latest launch among
// those it is not clear of, *f_in (when given) the fraction of the segment from p0 where the first of them begins (its circle's entry;
// 1 for a point).
inline bool small_clear(const Member* const cap[], const std::uint32_t t[], const float ql[][2], unsigned n, const float* p0, const float p1[2],
                        float r_small, std::uint32_t* t_last, float* f_in = nullptr) noexcept {
    bool ok = true;
    const float sx = p0 ? p1[0] - p0[0] : 0.0f, sy = p0 ? p1[1] - p0[1] : 0.0f, ss = sx * sx + sy * sy;
    if (f_in) *f_in = 1.0f;
    for (unsigned k = 0; k < n; ++k) {
        const float ex = p1[0] - ql[k][0], ey = p1[1] - ql[k][1];
        float dx = ex, dy = ey, u = 0.0f, ul = 0.0f;
        if (ss > 1.0f) { // the segment's closest point to the lateral (u from p1 back toward p0; review of rounds A-C item 3: the sign was
                         // reversed and took p0 for the closest point whenever the line's closest point lay beyond p1)
            ul = (ex * sx + ey * sy) / ss;
            u = ul < 0.0f ? 0.0f : ul > 1.0f ? 1.0f : ul;
            dx -= u * sx;
            dy -= u * sy;
        }
        const float need = cap[k]->w + r_small + (p0 ? kSmallPathMarginM : kSmallBMarginM), d2 = dx * dx + dy * dy;
        if (d2 < need * need) {
            if (t_last && (ok || std::int32_t(t[k] - *t_last) > 0)) *t_last = t[k];
            ok = false;
            if (f_in && ss > 1.0f) { // the chord's entry on the line (its closest point ul, half-chord h; review item 3: the clamped u and
                                     // the end point's distance gave a negative entry for an end point inside the circle)
                const float lx = ex - ul * sx, ly = ey - ul * sy, h2 = (need * need - (lx * lx + ly * ly)) / ss;
                float f = 1.0f - ul - (h2 > 0.0f ? sc::sqrt_ss(h2) : 0.0f);
                f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
                if (f < *f_in) *f_in = f;
            }
        }
    }
    return ok;
}
// Review of rounds A-C item 3: whether a small ship at the lateral xl (gate frame R, U; m) is inside a counted capital's warn zone
// (kSmallZoneBox (w + r_small) + kSmallZoneM) or its path margin (w + r_small + kSmallPathMarginM) about the lateral it passes the plane
// at: held there it would sit in that capital's way (fixture: a TS on order 5 held at 820 m beside the column, 11 m of lateral clearance
// to the passing Titan as it braked); it goes on instead (order 5: to its B, the edge; order 4: small_zone_hold, to its side line).
__attribute__((always_inline)) inline bool small_inside(const Member* const cap[], const float ql[][2], unsigned n, const float xl[2], float r_small) noexcept {
    for (unsigned k = 0; k < n; ++k) {
        const float zone = kSmallZoneBox * (cap[k]->w + r_small) + kSmallZoneM, path = cap[k]->w + r_small + kSmallPathMarginM;
        const float need = zone > path ? zone : path, dx = xl[0] - ql[k][0], dy = xl[1] - ql[k][1];
        if (!(dx * dx + dy * dy >= need * need)) return true;
    }
    return false;
}
// The free side's arc edge for a pass radius rho (m): (side rho sin 45, -rho cos 45).
__attribute__((always_inline)) inline void small_edge_point(float rho, float side, float e[2]) noexcept {
    e[0] = side * kSmallEdgeCos * rho;
    e[1] = -kSmallEdgeCos * rho;
}
__attribute__((always_inline)) inline std::uint32_t small_eta(std::uint32_t now, float d, float v) noexcept {
    return now + std::uint32_t(int(1000.0f * d / (v > 1.0f ? v : 1.0f))); // via int (x87 audit)
}
inline SmallB small_b_decide(const Gate& g, std::uint32_t now, const float b[3], float d_order, float v, float r_small, const float xl[2],
                             std::int8_t side_latch, float out[3], std::uint32_t* t_launch = nullptr, float* f_in = nullptr) noexcept {
    if (!(g.r_gate > 0.0f) || !(d_order > kSmallBNearR * g.r_gate)) return SmallB::near;
    const bool lane = g.lane && sc::finite_f(xl[0]) && sc::finite_f(xl[1]);
    const Member* cap[kMembers];
    std::uint32_t tl[kMembers];
    float ql[kMembers][2];
    const unsigned n = small_caps(g, now, small_eta(now, d_order, v), r_small, lane, cap, tl, ql);
    float t[3];
    // section 12.2: a ship whose lead-in kept a side (side_latch) passes on that side: B beyond the axis is mirrored to it even with no
    // capital counted (fixture: two escorts from either side, released together after the last capital, crossed at the arc's middle)
    const bool own = lane && side_latch && (b[0] > 0.0f) != (side_latch > 0) && small_b_mirror(g.r_gate, b, b[0] > 0.0f ? 1.0f : -1.0f, t);
    if (!n) {
        if (!own) return SmallB::no_funnel;
        for (unsigned i = 0; i < 3; ++i) out[i] = t[i];
        return SmallB::mirrored;
    }
    if (!own && !small_b_mirror(g.r_gate, b, b[0] > 0.0f ? 1.0f : -1.0f, t)) return SmallB::not_arc;
    const float* p0 = lane ? xl : nullptr;
    if (!own && small_clear(cap, tl, ql, n, p0, b, r_small, nullptr)) return SmallB::clear;
    if ((!lane || !side_latch || own) && small_clear(cap, tl, ql, n, p0, t, r_small, nullptr)) {
        for (unsigned i = 0; i < 3; ++i) out[i] = t[i];
        return SmallB::mirrored;
    }
    if (!lane) return SmallB::both_taken;
    const Member* fm = lane_side_member(g);
    const float side = side_latch ? (side_latch > 0 ? 1.0f : -1.0f) : small_free_side(fm ? fm->q[0] : ql[0][0], xl[0]);
    float e[2];
    small_edge_point(sc::sqrt_ss(b[0] * b[0] + b[1] * b[1]), side, e);
    out[0] = e[0];
    out[1] = e[1];
    out[2] = b[2];
    std::uint32_t last = now;
    float f = 1.0f;
    if (small_clear(cap, tl, ql, n, p0, e, r_small, &last, &f)) return SmallB::edge;
    // (review of rounds A-C item 3: the path's first taken circle begins where the ship is (f_in 0), or the ship is inside a counted
    // capital's warn zone: not held there; the edge written, it goes on)
    if (!(f > 0.0f) || small_inside(cap, ql, n, xl, r_small)) return SmallB::edge;
    if (t_launch) *t_launch = last;
    if (f_in) *f_in = f;
    return SmallB::hold;
}
// Section 12.2 on order 5 after the write (B ours: the mirror or the edge): the remaining lateral path xl -> b (gate frame, m) re-tested
// on every decision against the capitals counted now (a capital that was not counted at the write may catch up with a small ship slowed
// by its turn); true (hold) with *t_launch and *f_in as small_b_decide's.
inline bool small_path_hold(const Gate& g, std::uint32_t now, const float xl[2], const float b[2], float d, float v, float r_small,
                            std::uint32_t* t_launch, float* f_in) noexcept {
    if (!g.lane || !(g.r_gate > 0.0f) || !sc::finite_f(xl[0]) || !sc::finite_f(xl[1]) || !(d > kSmallBNearR * g.r_gate)) return false;
    const Member* cap[kMembers];
    std::uint32_t tl[kMembers];
    float ql[kMembers][2];
    const unsigned n = small_caps(g, now, small_eta(now, d, v), r_small, true, cap, tl, ql);
    std::uint32_t last = now;
    if (!n || small_clear(cap, tl, ql, n, xl, b, r_small, &last, f_in)) return false;
    if (!(*f_in > 0.0f) || small_inside(cap, ql, n, xl, r_small)) return false; // (review of rounds A-C item 3: never held inside)
    *t_launch = last;
    return true;
}
// Section 12.2 on order 4 (the side lead-in on): whether the lead-in's end X_s (side kSmallSideR r_gate, the approach height) lies in a
// counted capital's warn zone (kSmallZoneBox (w + r_small) + kSmallZoneM from its lateral) for a small ship d (m) from the plane at
// speed v; then *t_launch the latest predicted launch among those capitals. Not while the ship itself (xl: its lateral R, U in the gate
// frame, m) is inside such a zone: held there it would stay in the capital's way (fixture: an M6 thrown inside the column by an earlier
// 0x12, held at 1 m/s 500 m beside the axis, warned by the next capital); it flies out to its side line at once instead.
inline bool small_zone_hold(const Gate& g, std::uint32_t now, float side, const float xl[2], float d, float v, float r_small, std::uint32_t* t_launch) noexcept {
    if (!(g.r_gate > 0.0f)) return false;
    const Member* cap[kMembers];
    std::uint32_t tl[kMembers];
    float ql[kMembers][2];
    const unsigned n = small_caps(g, now, small_eta(now, d, v), r_small, true, cap, tl, ql);
    const float x = side * kSmallSideR * g.r_gate, y = sc::kGateApproachY * g.r_gate;
    bool hold = false;
    if (small_inside(cap, ql, n, xl, r_small)) return false; // (review of rounds A-C item 3: the zone or the path margin)
    for (unsigned k = 0; k < n; ++k) {
        const float zone = kSmallZoneBox * (cap[k]->w + r_small) + kSmallZoneM, dx = x - ql[k][0], dy = y - ql[k][1];
        if (!(dx * dx + dy * dy < zone * zone)) continue;
        if (!hold || std::int32_t(tl[k] - *t_launch) > 0) *t_launch = tl[k];
        hold = true;
    }
    return hold;
}
// The bounded hold (section 12.2; the existing small-ship clamp is its only write): a decision that finds the small ship's way taken
// (order 4: X_s in a counted capital's warn zone, small_zone_hold; order 5: its path to B, small_b_decide / small_path_hold) holds it
// where it is, at kSmallHoldFloorMs; a decision that finds the way free goes at once (the ETA at the ship's maximum: it goes in a gap
// between two capitals), as does one past the latest counted launch + 5 s + the step (the capital predicted gone). The holds of one
// approach (GateState) end for good 60 s + the step after the first began (then the ship goes and the engine's warn is its backstop).
// An order-4 hold begins only inside the lead-in's range (kSmallHoldRangeR r of A) and short of the line's end by kSmallParkBackM (a
// ship nearer goes on: its order 5, in the engine's ignore mode, then holds at its start); one in progress stays past it.
// Deviations from the design's pacing (v <= d / (t_launch + 2.9 s - now), floor 10 m/s), each a fixture witness: at 10 m/s a TS held
// 50-60 s drifted 600 m past the line's end and turned into the column (past the end the lead-in hands the ship back to the engine's A);
// paced to a park short of the end it reached the park early as the predicted launch slipped and circled there, its nose toward the
// next capital (the engine's warn then doubles the closing term: a 600 m reach), and its 0x12 met that capital. Held where it is, a
// small escort stays 1.2 km beside the column (the design's own words for escorts) with its nose along the line.
// While held on order 4 the leaf's point is kSmallHoldLookM ahead of the ship along the line's direction (small_hold_carrot): at 1 m/s
// the lead-in's pursuit turned the nose square to the line and back toward the next capital (fixture: a 0.81 cosine, the doubled
// closing term, a warn and a 0x12 into that capital); held, the nose stays along the line.
constexpr float kSmallParkBackM = 300.0f, kSmallHoldRangeR = 3.0f, kSmallHoldLookM = 500.0f;
__attribute__((always_inline)) inline void small_hold_carrot(const sc::GateGeometry& geo, const float x[3], float P[3]) noexcept {
    for (unsigned i = 0; i < 3; ++i) P[i] = x[i] + kSmallHoldLookM * geo.h[i];
}
constexpr std::uint32_t kSmallHoldMaxMs = 60000, kSmallHoldAfterMs = 5000;
// Review of rounds A-C item 4: a hold whose 60 s + the step end finds the way still taken (this decision's re-test: order 4 the lead-in's
// end in a counted capital's warn zone, order 5 its path) is extended once, to kSmallHoldExtMs + the step from its start (fixture: a
// five-capital convoy with refused slots still passing at 60 s; released there, the small ship flew at full speed toward a lead-in end
// inside a passing capital's zone); a way free at the bound's end ends the holds for good as before. (X3M_SMALL_HOLD_EXT_MS: the fixture's
// A/B; 60000 is the behaviour before.)
#ifndef X3M_SMALL_HOLD_EXT_MS
#define X3M_SMALL_HOLD_EXT_MS 150000u
#endif
constexpr std::uint32_t kSmallHoldExtMs = X3M_SMALL_HOLD_EXT_MS;
constexpr float kSmallHoldFloorMs = 1.0f;
// The release is staggered per ship (kSmallStaggerMs times the spread index's bit 2, the bit the U level above does not use): the
// small ships held behind one
// capital are released together at its launch, and two on one side met on their order-5 legs (fixture: a TS escort turning at the leg's
// start, a TS trader on the same side's line behind it).
constexpr std::uint32_t kSmallStaggerMs = 2500;
struct SmallHold {
    std::uint32_t start_ms = 0, release_ms = 0;
    std::uint8_t done = 0, active = 0; // active: the last decision held (an order-4 hold in progress stays past the park)
};
__attribute__((always_inline)) inline float small_hold_cap(SmallHold& h, const Gate& g, std::uint32_t now, bool hold, std::uint32_t t_launch, unsigned k) noexcept {
    const bool was = h.active != 0;
    h.active = 0;
    if (h.done) return -1.0f;
    if (!hold && h.start_ms && now - h.start_ms > age_bound(g, kSmallHoldMaxMs)) { // (review item 4: the way free past the bound: for good)
        h.done = 1;
        return -1.0f;
    }
    if (!hold) {
        const std::uint32_t delay = ((k >> 2) & 1u) * kSmallStaggerMs;
        if (was && !h.release_ms && delay) h.release_ms = (now + delay) | 1u; // (| 1: never 0)
        if (!h.release_ms || std::int32_t(now - h.release_ms) >= 0) return -1.0f;
    } else
        h.release_ms = 0;
    if (!h.start_ms) h.start_ms = now ? now : 1;
    const std::uint32_t held = now - h.start_ms;
    if (held > age_bound(g, kSmallHoldMaxMs) && (!hold || held > age_bound(g, kSmallHoldExtMs))) { // (review item 4: extended once while taken)
        h.done = 1;
        return -1.0f;
    }
    if (hold && std::int32_t(now - (t_launch + kSmallHoldAfterMs + g.step_ms)) > 0) return -1.0f;
    h.active = 1;
    return kSmallHoldFloorMs;
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
