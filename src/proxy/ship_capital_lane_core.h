#pragma once
#include "ship_steering_core.h"

// Amendment 16 (docs/architecture/capital-gate-transit.md section 16.1): the lane rule for two capitals committed to the same
// gate. Portable and pure over the board the DLL fills at each capital decision (ship_steering.cpp decide(), P2) and the host
// fixture fills from its scenario; SSE2 scalar float only, no allocation, no CRT math.
//
// A capital S is committed to gate G when its board entry is its own (pointer and native id [obj+0x08]), was made from a
// decision on validated capital grid geometry (gate_geometry_capital), its live f+0x224 is G and it is on order 4 or 5 with a
// fresh decision, or on 0x1f / order 0 within the hand-over after P2 (kHandoverMaxMs). Its lane point l is the grid slot of the
// validated point in the gate frame (R = row 0, U = row 1, F = row 2); on order 5 the leg runs from its lateral position q to
// (0, 0). The footprint (the projection of the hull onto the R-U plane while the classification holds):
//   in lane (order 4 in join mode, or the hand-over; |q - l| <= 100 m, nose <= 8.11 deg off F): disc(l, |q - l| + rho + 25 m),
//     rho = min(r, w + r sin theta) for a hull inside the box (w, w, r);
//   joining (order 4 in join or slow join, with a prediction that reaches the crossing plane): the segment l - over n ..
//     l + max(e_max, |q - l|) n inflated by r, n the unit direction from l to q;
//   otherwise (fallback, order 5, no prediction yet, the hand-over off its lane): the segment q -> its destination inflated by r.
// Verdict (with the ratification addendum 2026-10-13 (4)): (a) exempt when the footprints are >= 50 m apart, or when the lane points
// differ and each ship is inside its corridor (settled in lane, or joining with a current prediction whose e_max and excursion are
// <= 100 m and whose nose at the plane is <= 8.11 deg); (b) follow on the same lane point with both settled; (c) cross otherwise.
// Any missing entry, stale state, identity mismatch or uncommitted side is no verdict (today's rules).
// Amendment 16b (section 16.10, ratified 2026-10-13): a pair that the rule above leaves at (c) on different lane points (or on an
// order-5 leg) is (s) "sequenced" when (i) the hulls' lateral projections are apart now by 50 m plus the motion until the next
// classification, or (ii) their projections onto F are apart with the trailer held behind; the trailing ship of every such pair
// whose lateral paths come within reach carries the sequencing cap (D_x = h_T + h_L + 100 m, h = sqrt(r^2 + w^2)). Every verdict
// is computed from the stored board in both directions and the more severe one taken (review F6); one pass per decision feeds both
// the avoidance's exclusion and site B's caps (review F2); site E suppresses only under a geometric certificate (footprint route of
// (a), or (s) by (i) / (ii): review F1).
namespace x3m::ship_avoidance::lane {
namespace sc = x3m::ship_steering::core;
constexpr unsigned kLaneGates = 8, kLaneEntries = 20;
constexpr float kInLaneM = 100.0f;
constexpr float kInLaneCos = 0.98999f;     // cos 8.11 deg (sc::kGateRotateTolDeg): the in-lane nose bound
constexpr float kMarginM = 25.0f;          // one re-classification interval (<= 250 ms) at <= 100 m/s x sin 8.11 deg [i]
// Hysteresis on the in-lane classification (ratified addition, section 16.11): a ship enters in lane at |q - l| <= 100 m and the nose
// <= 8.11 deg off F; once in lane (the entry's in_lane bit) it stays in lane until |q - l| > 150 m or the nose > 12 deg. A state held
// only by the hysteresis takes the exit values in every margin that assumed the entry bound: the footprint's re-classification
// margin (25 m x sin 12 / sin 8.11 = 37 m), a hand-over's nose floor in the lateral certificate (12 deg) and the leader's along-F
// credit (less v (1 - cos 12 deg)). The bit is cleared when the ship is not committed (another order, order 5, a refresh more than
// kHoldGapMs after the previous one: longer than any fresh window).
constexpr float kHoldM = 150.0f, kHoldCos = 0.97815f, kMarginHeldM = 37.0f; // cos 12 deg
constexpr std::uint32_t kHoldGapMs = 1000;
constexpr float kExemptGapM = 50.0f;
constexpr float kFollowExtraM = 100.0f;    // D = the engine's warn reach for the pair + 100 m
constexpr float kFollowFloorMs = 1.0f / sc::kUnitsPerMetre; // the trailing ship's cap floor: 1 unit/s, the smallest site-B write (0 = "no write")
constexpr std::uint32_t kHandoverMaxMs = 120000; // = ship_avoidance_core.h kHandoverMaxMs (the capital gate row's hand-over bound)
enum class Verdict : std::uint8_t { none = 0, exempt, follow, cross, seq, count };
inline const char* verdict_name(Verdict v) noexcept {
    static const char* const names[unsigned(Verdict::count)] = {"-", "a", "b", "c", "s"};
    return unsigned(v) < unsigned(Verdict::count) ? names[unsigned(v)] : "?";
}
// The route that gave the verdict: (a) by the 50 m footprint gap (a certificate) or by the corridor alone (none: the engine's warn
// stays live, review F1); (s) by the lateral (i) or the along-axis (ii) certificate.
enum class Route : std::uint8_t { none = 0, foot, corr, lat, along, count };
inline const char* route_name(Route r) noexcept {
    static const char* const names[unsigned(Route::count)] = {"-", "foot", "corr", "lat", "along"};
    return unsigned(r) < unsigned(Route::count) ? names[unsigned(r)] : "?";
}
// Amendment 16b constants (section 16.10.3).
constexpr float kSeqLatGapM = 50.0f;       // = kExemptGapM: the clearance between the lateral projections, kept apart from the motion
constexpr float kSeqAlongM = 50.0f;        // the along-axis certificate's slack over h_T + h_L (the closing within tau added explicitly)
constexpr float kSeqCapExtraM = 100.0f;    // D_x = h_T + h_L + 100 m: 50 m above the certificate, >= 10 x the cap's kinematic undershoot
constexpr float kSeqTauMaxS = 1.0f;        // tau = min(1 s, age + fresh)
constexpr float kSeqLeaderCos = 0.5f;      // the leader's nose within 60 deg of F (with v.F >= 0): it moves forward until the next classification
constexpr float kSeqBackMs = 1.0f;        // the leader's v.F >= -1 m/s (a stopped hull's slip residue; the closing term carries it)
constexpr float kSeqTieM = 1.0f;           // |ds| below it: the ship not in its corridor trails, then the higher pointer
enum class Foot : std::uint8_t { none = 0, in_lane, joining, segment };
// The hull half-width's source (section 16.1 with the addendum: the LOD records of the hull parts, ship_avoidance_walk_core.h
// hull_width; or w = r: unread = no read made, fail = a guard failed).
enum class WidthSrc : std::uint8_t { unread = 0, lod, failed, count };
inline const char* width_src_name(WidthSrc s) noexcept {
    static const char* const names[unsigned(WidthSrc::count)] = {"unread", "lod", "fail"};
    return unsigned(s) < unsigned(WidthSrc::count) ? names[unsigned(s)] : "?";
}
// One capital's state as of its last refresh (an order-4/5 decision on the validated approach, or P2).
struct Entry {
    std::uint32_t ship = 0, id = 0;      // the object and its native id [obj+0x08]
    std::uint32_t stamp_ms = 0;          // the last order-4/5 decision (game ms)
    std::uint32_t handover_ms = 0;       // P2 on this approach (0 = none)
    std::uint16_t order = 0;             // the order at the last refresh (4, 5; 0 after P2; any other handled order: not committed)
    std::uint8_t mode = 0;               // sc::JoinMode at the last order-4 decision
    std::uint8_t pred = 0;               // a prediction that reaches the crossing plane (e_max / over valid)
    std::uint8_t w_src = 0;              // WidthSrc
    std::uint8_t in_lane = 0;            // the hysteresis bit: in lane at the last refresh / P2 (section 16.11)
    std::uint8_t pad[2] = {};
    float lane[2] = {0, 0};              // l, m (gate frame R, U)
    float q[2] = {0, 0};                 // the lateral position at the last refresh, m
    float cos_t = 1.0f;                  // the nose's cosine to F at the last refresh
    float s = 0, vF = 0;                 // (x - G) . F (m) and u . F (m/s) at the last refresh
    float r = 0, w = 0, a_lin = 0;       // m, m, m/s^2
    float e_max = 0, over = 0;           // the predicted path's largest offset on its own side and its excursion past the line, m
    float pred_nose = -1.0f;             // the prediction's nose error at the plane, deg (the corridor test of the addendum)
    // amendment 16b (section 16.10.2): from the deciding ship's own decision, no other memory read; < 0 = unknown (the routes fail
    // closed). h = sqrt(r^2 + w^2) (w = r when unread); v_perp = |u - (u.F) F|; omega = the yaw-pitch body rate; alpha = the
    // angular acceleration (W k, the larger of the yaw and pitch axes)
    float h = 0, v_perp = -1.0f, omega = -1.0f, alpha = -1.0f;
    float len = 0; // the half-length 16b uses: max(r, l) with l the width read's along-axis half-extent (census l_m; 0 = unknown)
};
struct Gate {
    std::uint32_t gate = 0, stamp_ms = 0;
    float G[3] = {0, 0, 0}, R[3] = {0, 0, 0}, U[3] = {0, 0, 0}, F[3] = {0, 0, 0};
    unsigned n = 0;                      // entries in use (the highest index + 1)
    Entry e[kLaneEntries];
};
struct Board {
    Gate g[kLaneGates];
    unsigned full_gates = 0, full_entries = 0; // refused refreshes (no lane: the pair keeps today's rules)
};
__attribute__((always_inline)) inline bool entry_dead(const Entry& e, std::uint32_t now) noexcept {
    const std::uint32_t last = e.handover_ms && e.handover_ms - e.stamp_ms < 0x80000000u ? e.handover_ms : e.stamp_ms;
    return !e.ship || now - last > kHandoverMaxMs;
}
inline const Gate* gate_of(const Board& b, std::uint32_t gate) noexcept {
    if (!gate) return nullptr;
    for (unsigned i = 0; i < kLaneGates; ++i)
        if (b.g[i].gate == gate) return &b.g[i];
    return nullptr;
}
inline Gate* gate_of(Board& b, std::uint32_t gate) noexcept {
    return const_cast<Gate*>(gate_of(static_cast<const Board&>(b), gate));
}
inline const Entry* find(const Gate& g, std::uint32_t ship) noexcept {
    for (unsigned i = 0; i < g.n; ++i)
        if (g.e[i].ship == ship) return &g.e[i];
    return nullptr;
}
inline Entry* find(Gate& g, std::uint32_t ship) noexcept {
    return const_cast<Entry*>(find(static_cast<const Gate&>(g), ship));
}
// The board slot for a refresh: the gate's (a new gate takes an empty or expired slot), the ship's entry (a new one takes an empty
// or dead entry). nullptr when full (counted): that ship has no lane and its pairs keep today's rules.
inline Entry* acquire(Board& b, std::uint32_t gate, std::uint32_t ship, std::uint32_t now, const float G[3], const float R[3], const float U[3],
                      const float F[3]) noexcept {
    Gate* g = gate_of(b, gate);
    if (!g) {
        for (unsigned i = 0; i < kLaneGates && !g; ++i) {
            Gate& c = b.g[i];
            bool live = false;
            for (unsigned k = 0; k < c.n && !live; ++k) live = !entry_dead(c.e[k], now);
            if (!c.gate || !live) g = &c;
        }
        if (!g) {
            ++b.full_gates;
            return nullptr;
        }
        *g = Gate{};
        g->gate = gate;
    }
    for (unsigned i = 0; i < 3; ++i) {
        g->G[i] = G[i];
        g->R[i] = R[i];
        g->U[i] = U[i];
        g->F[i] = F[i];
    }
    g->stamp_ms = now;
    if (Entry* e = find(*g, ship)) return e;
    for (unsigned i = 0; i < kLaneEntries; ++i) {
        Entry& c = g->e[i];
        if (i >= g->n || entry_dead(c, now)) {
            c = Entry{};
            c.ship = ship;
            if (i >= g->n) g->n = i + 1;
            return &c;
        }
    }
    ++b.full_entries;
    return nullptr;
}
inline void drop(Board& b, std::uint32_t gate, std::uint32_t ship) noexcept {
    if (Gate* g = gate_of(b, gate))
        if (Entry* e = find(*g, ship)) *e = Entry{};
}
// The lateral position and the along-axis position of a world point in the gate frame (m).
__attribute__((always_inline)) inline void lateral(const Gate& g, const float x[3], float q[2], float* s) noexcept {
    const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
    q[0] = sc::dot3(d, g.R);
    q[1] = sc::dot3(d, g.U);
    if (s) *s = sc::dot3(d, g.F);
}
// One order-4/5 decision's refresh (the DLL's decide() after the join bookkeeping; the fixture's steer_call).
struct Decision {
    std::uint32_t ship = 0, id = 0, gate = 0;
    std::uint16_t order = 0;
    std::uint8_t mode = 0, pred = 0;
    float G[3], R[3], U[3], F[3];
    float x[3], f[3], u[3];      // position (m), forward (unit), velocity (m/s)
    float A[3];                  // the validated given (order 4; ignored on order 5)
    float r = 0, w = 0, a_lin = 0;
    std::uint8_t w_src = 0;
    float e_max = 0, over = 0, pred_nose = -1.0f;
    float omega = -1.0f, alpha = -1.0f; // amendment 16b: the yaw-pitch body rate (rad/s) and the angular acceleration (rad/s^2); < 0 unknown
    float l = 0;                        // amendment 16b: the width read's max over parts of |o_z| + e_z (m; 0 = unknown): h covers it
};
// Amendment 16b: the decision's yaw-pitch body rate (rad/s; the roll does not move the nose off F) and the angular acceleration
// (W k of the faster of yaw and pitch: the reach growth bound; equal axes on the capitals); < 0 when unknown.
__attribute__((always_inline)) inline void seq_rates(const sc::Dynamics& dyn, const float w[3], float* omega, float* alpha) noexcept {
    const float om = sc::sqrt_ss(w[0] * w[0] + w[1] * w[1]);
    *omega = sc::finite_f(om) ? om : -1.0f;
    const float W = dyn.W[0] > dyn.W[1] ? dyn.W[0] : dyn.W[1];
    *alpha = W > 0.0f && dyn.k > 0.0f && sc::finite_f(W * dyn.k) ? W * dyn.k : -1.0f;
}
// The in-lane state with hysteresis for an offset and nose: 0 out, 1 in by the entry thresholds, 2 held (within the exit thresholds
// only, with the bit set).
__attribute__((always_inline)) inline std::uint8_t lane_hold_state(bool bit, float off, float cos_t) noexcept {
    if (off <= kInLaneM && cos_t >= kInLaneCos) return 1;
    if (bit && off <= kHoldM && cos_t >= kHoldCos) return 2;
    return 0;
}
__attribute__((always_inline)) inline float lane_offset(const Entry& e, const float q[2]) noexcept {
    const float dq[2] = {q[0] - e.lane[0], q[1] - e.lane[1]};
    return sc::sqrt_ss(dq[0] * dq[0] + dq[1] * dq[1]);
}
inline Entry* refresh(Board& b, const Decision& d, std::uint32_t now) noexcept {
    Entry* e = acquire(b, d.gate, d.ship, now, d.G, d.R, d.U, d.F);
    if (!e) return nullptr;
    const Gate& g = *gate_of(b, d.gate);
    if (e->id != d.id) { // another object behind the same pointer: a fresh entry
        *e = Entry{};
        e->ship = d.ship;
    }
    // the hysteresis bit survives only a continuous commitment on order 4 (no gap longer than any fresh window)
    const bool bit = e->in_lane && e->order == sc::kOrderFlyBefore && e->stamp_ms && now - e->stamp_ms <= kHoldGapMs;
    e->id = d.id;
    e->stamp_ms = now ? now : 1;
    e->order = d.order;
    if (d.order == sc::kOrderFlyBefore) {
        float qa[2];
        lateral(g, d.A, qa, nullptr);
        e->lane[0] = qa[0];
        e->lane[1] = qa[1];
        e->mode = d.mode;
        e->pred = d.pred;
        e->e_max = d.e_max;
        e->over = d.over;
        e->pred_nose = d.pred_nose;
        e->handover_ms = 0; // a new order-4 decision: any earlier P2 belongs to a previous approach
    }
    lateral(g, d.x, e->q, &e->s);
    e->cos_t = sc::dot3(d.f, g.F);
    e->vF = sc::dot3(d.u, g.F);
    e->r = d.r;
    e->w = d.w;
    e->w_src = d.w_src;
    e->a_lin = d.a_lin;
    // amendment 16b: h, the lateral centre speed, the body rate and the angular acceleration of this decision
    const float wh = d.w > 0.0f ? d.w : d.r;
    e->len = sc::finite_f(d.l) && d.l > d.r ? d.l : d.r; // section 16.10.9: h takes max(h, l_m) (one installed capital has l > h)
    e->h = sc::sqrt_ss(e->len * e->len + wh * wh);
    const float uF = sc::dot3(d.u, g.F);
    const float up[3] = {d.u[0] - uF * g.F[0], d.u[1] - uF * g.F[1], d.u[2] - uF * g.F[2]};
    const float vp = sc::sqrt_ss(sc::dot3(up, up));
    e->v_perp = sc::finite_f(vp) ? vp : -1.0f;
    e->omega = sc::finite_f(d.omega) && d.omega >= 0.0f ? d.omega : -1.0f;
    e->alpha = sc::finite_f(d.alpha) && d.alpha >= 0.0f ? d.alpha : -1.0f;
    if (!sc::finite_f(e->h)) e->h = 0.0f;
    e->in_lane = d.order == sc::kOrderFlyBefore && d.mode == std::uint8_t(sc::JoinMode::join) && lane_hold_state(bit, lane_offset(*e, e->q), e->cos_t) ? 1 : 0;
    return e;
}
// P2 (the order-4 arrival, kept or not): the hand-over starts; the state at the crossing (vF after the keep verdict).
inline void handover(Board& b, std::uint32_t gate, std::uint32_t ship, std::uint32_t id, std::uint32_t now, const float x[3], const float f[3], float vF) noexcept {
    Gate* g = gate_of(b, gate);
    Entry* e = g ? find(*g, ship) : nullptr;
    if (!e || e->id != id) return;
    const bool bit = e->in_lane && e->order == sc::kOrderFlyBefore && now - e->stamp_ms <= kHoldGapMs;
    e->handover_ms = now ? now : 1;
    e->order = sc::kOrderNone;
    lateral(*g, x, e->q, &e->s);
    e->cos_t = sc::dot3(f, g->F);
    e->vF = vF;
    e->in_lane = lane_hold_state(bit, lane_offset(*e, e->q), e->cos_t) ? 1 : 0;
}
// Any other handled order (0x12, 0x14, ...): the entry is not committed until the next order-4/5 refresh.
inline void mark(Board& b, std::uint32_t gate, std::uint32_t ship, std::uint16_t order) noexcept {
    Gate* g = gate_of(b, gate);
    if (Entry* e = g ? find(*g, ship) : nullptr) {
        e->order = order;
        e->handover_ms = 0;
        e->in_lane = 0;
    }
}
// Commitment from live reads (site E; the avoidance resolve over the snapshot's order, f+0x224 and id).
inline bool committed(const Entry& e, std::uint32_t now, std::uint32_t fresh_ms, std::uint16_t live_order, std::uint32_t live_target,
                      std::uint32_t gate, std::uint32_t live_id) noexcept {
    if (!e.ship || e.id != live_id || !gate || live_target != gate) return false;
    if (live_order == sc::kOrderFlyBefore || live_order == sc::kOrderDock) return e.order == live_order && now - e.stamp_ms <= fresh_ms;
    if (live_order == sc::kOrderRotate || live_order == sc::kOrderNone) return e.handover_ms && now - e.handover_ms <= kHandoverMaxMs;
    return false;
}
// Commitment from the board alone (decide(): no other ship is dereferenced there).
inline bool committed_stored(const Entry& e, std::uint32_t now, std::uint32_t fresh_ms) noexcept {
    if (!e.ship) return false;
    if (e.order == sc::kOrderFlyBefore || e.order == sc::kOrderDock) return now - e.stamp_ms <= fresh_ms;
    if (e.order == sc::kOrderNone) return e.handover_ms && now - e.handover_ms <= kHandoverMaxMs;
    return false;
}
// The hand-over's along-axis state since P2: the engine's order 0 / 0x1f with desired 0 slews the speed down at the hull's
// acceleration (the stored vF is the speed P2 left). Order 4/5 entries: as stored.
__attribute__((always_inline)) inline void along(const Entry& e, std::uint32_t now, float* s, float* vF) noexcept {
    *s = e.s;
    *vF = e.vF;
    if (e.order != sc::kOrderNone || !e.handover_ms || !(e.vF > 0.0f) || !(e.a_lin > 0.0f)) return;
    const float t = float(now - e.handover_ms) * 0.001f, t_stop = e.vF / e.a_lin;
    const float tt = t < t_stop ? t : t_stop;
    *s = e.s + e.vF * tt - 0.5f * e.a_lin * tt * tt;
    *vF = e.vF - e.a_lin * tt;
}
// One side of a pair as classified now: q and the nose from a live read when given, else the entry's.
struct Side {
    const Entry* e = nullptr;
    float q[2] = {0, 0};
    float cos_t = 1.0f;
    float s = 0, vF = 0;
    std::uint16_t order = 0;
    std::uint32_t age = 0; // ms since the entry's state was sampled (the last refresh, or P2 for the hand-over)
};
// The age of an entry's stored state (ms): since its last refresh, or since P2 when the hand-over is later.
__attribute__((always_inline)) inline std::uint32_t entry_age(const Entry& e, std::uint32_t now) noexcept {
    const std::uint32_t last = e.handover_ms && e.handover_ms - e.stamp_ms < 0x80000000u ? e.handover_ms : e.stamp_ms;
    const std::uint32_t a = now - last;
    return a < 0x80000000u ? a : 0x7fffffffu;
}
inline void side_of(const Gate& g, const Entry& e, const float* x, const float* f, std::uint16_t order, std::uint32_t now, Side* out) noexcept {
    out->e = &e;
    out->order = order;
    out->age = entry_age(e, now);
    along(e, now, &out->s, &out->vF);
    if (x) {
        lateral(g, x, out->q, &out->s);
    } else {
        out->q[0] = e.q[0];
        out->q[1] = e.q[1];
    }
    out->cos_t = f ? sc::dot3(f, g.F) : e.cos_t;
}
// rho(theta) = min(r, w + r sin theta): the hull's reach across the axis (box (w, w, r), nose theta off F).
__attribute__((always_inline)) inline float reach(float r, float w, float cos_t) noexcept {
    const float c = cos_t > 1.0f ? 1.0f : cos_t < -1.0f ? -1.0f : cos_t;
    const float s = sc::sqrt_ss(1.0f - c * c);
    const float v = w + r * (c < 0.0f ? 1.0f : s);
    return v < r ? v : r;
}
// A side's in-lane state (0 out, 1 in, 2 held by the hysteresis): order 4 in join mode or the hand-over, from its entry's bit.
__attribute__((always_inline)) inline std::uint8_t lane_state(const Side& sd) noexcept {
    const Entry& e = *sd.e;
    const bool handover = sd.order == sc::kOrderNone || sd.order == sc::kOrderRotate;
    if (!handover && !(sd.order == sc::kOrderFlyBefore && e.mode == std::uint8_t(sc::JoinMode::join))) return 0;
    return lane_hold_state(e.in_lane != 0, lane_offset(e, sd.q), sd.cos_t);
}
struct Footprint {
    float a[2] = {0, 0}, b[2] = {0, 0}, rad = 0;
    Foot kind = Foot::none;
};
inline Footprint footprint(const Side& sd) noexcept {
    Footprint fp;
    const Entry& e = *sd.e;
    const float dq[2] = {sd.q[0] - e.lane[0], sd.q[1] - e.lane[1]};
    const float off = sc::sqrt_ss(dq[0] * dq[0] + dq[1] * dq[1]);
    const std::uint8_t ls = lane_state(sd);
    if (ls) {
        fp.a[0] = fp.b[0] = e.lane[0];
        fp.a[1] = fp.b[1] = e.lane[1];
        fp.rad = off + reach(e.r, e.w, sd.cos_t) + (ls == 2 ? kMarginHeldM : kMarginM);
        fp.kind = Foot::in_lane;
        return fp;
    }
    if (sd.order == sc::kOrderFlyBefore && e.pred &&
        (e.mode == std::uint8_t(sc::JoinMode::join) || e.mode == std::uint8_t(sc::JoinMode::slow_join))) {
        float n[2] = {1.0f, 0.0f};
        if (off > 1e-3f) {
            n[0] = dq[0] / off;
            n[1] = dq[1] / off;
        }
        const float hi = e.e_max > off ? e.e_max : off, lo = e.over > 0.0f ? e.over : 0.0f;
        for (unsigned i = 0; i < 2; ++i) {
            fp.a[i] = e.lane[i] - lo * n[i];
            fp.b[i] = e.lane[i] + hi * n[i];
        }
        fp.rad = e.r;
        fp.kind = Foot::joining;
        return fp;
    }
    fp.a[0] = sd.q[0];
    fp.a[1] = sd.q[1];
    if (sd.order == sc::kOrderDock) {
        fp.b[0] = fp.b[1] = 0.0f;
    } else {
        fp.b[0] = e.lane[0];
        fp.b[1] = e.lane[1];
    }
    fp.rad = e.r;
    fp.kind = Foot::segment;
    return fp;
}
// Distances in the plane: point-segment and segment-segment (0 when they intersect).
__attribute__((always_inline)) inline float point_seg2(const float p[2], const float a[2], const float b[2]) noexcept {
    const float ab[2] = {b[0] - a[0], b[1] - a[1]}, ap[2] = {p[0] - a[0], p[1] - a[1]};
    const float l2 = ab[0] * ab[0] + ab[1] * ab[1];
    float t = l2 > 1e-6f ? (ap[0] * ab[0] + ap[1] * ab[1]) / l2 : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    const float d[2] = {ap[0] - t * ab[0], ap[1] - t * ab[1]};
    return sc::sqrt_ss(d[0] * d[0] + d[1] * d[1]);
}
__attribute__((always_inline)) inline float cross2(const float o[2], const float a[2], const float b[2]) noexcept {
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
}
__attribute__((always_inline)) inline float seg_seg2(const float a0[2], const float a1[2], const float b0[2], const float b1[2]) noexcept {
    const float d1 = cross2(b0, b1, a0), d2 = cross2(b0, b1, a1), d3 = cross2(a0, a1, b0), d4 = cross2(a0, a1, b1);
    if (((d1 > 0.0f && d2 < 0.0f) || (d1 < 0.0f && d2 > 0.0f)) && ((d3 > 0.0f && d4 < 0.0f) || (d3 < 0.0f && d4 > 0.0f))) return 0.0f;
    float m = point_seg2(a0, b0, b1);
    float v = point_seg2(a1, b0, b1);
    if (v < m) m = v;
    v = point_seg2(b0, a0, a1);
    if (v < m) m = v;
    v = point_seg2(b1, a0, a1);
    return v < m ? v : m;
}
struct PairInfo {
    float gap = 0;            // the footprints' distance, m (negative: overlap)
    Foot foot_a = Foot::none, foot_b = Foot::none;
    float ds = 0;             // (x_B - x_A) . F, m
    bool same_lane = false, corridor_a = false, corridor_b = false;
    Route route = Route::none;
    // amendment 16b (a watch pair: amendment 16's (c) off the same lane point): the lateral certificate's slack lat_now - 50 m -
    // mot_A - mot_B, the motions, the trailer (1 = A, 2 = B, 0 = not reached) and the along certificate's slack s_L - s_T* -
    // (h_T + h_L + 50 m + tau_T closing)
    bool watch = false;
    std::uint8_t trail = 0;
    std::uint8_t why = 0; // a watch pair left at (c): 1 a motion unknown, 2 the trailer on order 5 or a <= 0, 3 the leader turned or
                          // moving back, 4 the along spacing short
    float lat_gap = 0, mot_a = 0, mot_b = 0, along_gap = 0;
};
// Ratification addendum 2026-10-13 (4): the corridor of a ship = settled in lane (the in-lane footprint), or joining (order 4 in
// join / slow join) with a current prediction whose largest offset and excursion are <= 100 m and whose nose at the plane is
// <= 8.11 deg. Two lane points closer than half the grid pitch are the same lane.
constexpr float kCorridorM = 100.0f, kCorridorNoseDeg = 8.11f, kSameLaneM = 0.5f * sc::kGateGridPitchM;
__attribute__((always_inline)) inline bool in_corridor(const Side& sd, Foot kind) noexcept {
    if (kind == Foot::in_lane) return true;
    if (kind != Foot::joining) return false;
    const Entry& e = *sd.e;
    const float dq[2] = {sd.q[0] - e.lane[0], sd.q[1] - e.lane[1]};
    const float off = sc::sqrt_ss(dq[0] * dq[0] + dq[1] * dq[1]);
    const float hi = e.e_max > off ? e.e_max : off;
    return hi <= kCorridorM && e.over <= kCorridorM && e.pred_nose >= 0.0f && e.pred_nose <= kCorridorNoseDeg;
}
// The verdict (section 16.1 with the addendum): (a) footprints >= 50 m apart, or different lane points with both ships inside
// their corridors; (b) the same lane point with both settled in lane; (c) anything else.
inline Verdict pair_verdict(const Side& A, const Side& B, PairInfo* info = nullptr) noexcept {
    const Footprint fa = footprint(A), fb = footprint(B);
    const float gap = seg_seg2(fa.a, fa.b, fb.a, fb.b) - fa.rad - fb.rad;
    const float dl[2] = {A.e->lane[0] - B.e->lane[0], A.e->lane[1] - B.e->lane[1]};
    const bool docking = A.order == sc::kOrderDock || B.order == sc::kOrderDock; // an order-5 leg has no lane point
    const bool same = !docking && dl[0] * dl[0] + dl[1] * dl[1] < kSameLaneM * kSameLaneM;
    const bool ca = in_corridor(A, fa.kind), cb = in_corridor(B, fb.kind);
    if (info) {
        info->gap = gap;
        info->foot_a = fa.kind;
        info->foot_b = fb.kind;
        info->ds = B.s - A.s;
        info->same_lane = same;
        info->corridor_a = ca;
        info->corridor_b = cb;
    }
    if (!sc::finite_f(gap)) return Verdict::cross;
    if (gap >= kExemptGapM) {
        if (info) info->route = Route::foot;
        return Verdict::exempt;
    }
    if (!same && !docking && ca && cb) {
        if (info) info->route = Route::corr;
        return Verdict::exempt;
    }
    if (same && fa.kind == Foot::in_lane && fb.kind == Foot::in_lane) return Verdict::follow;
    return Verdict::cross;
}
// ---- amendment 16b (section 16.10.2) ---------------------------------------------------------------------------------------
// The motions are known when the deciding ship recorded them (v_perp, omega, alpha >= 0) and its acceleration is positive.
__attribute__((always_inline)) inline bool motion_known(const Entry& e) noexcept {
    return e.v_perp >= 0.0f && e.omega >= 0.0f && e.alpha >= 0.0f && sc::finite_f(e.v_perp) && sc::finite_f(e.omega) && sc::finite_f(e.alpha) &&
           e.h > 0.0f && sc::finite_f(e.h);
}
__attribute__((always_inline)) inline float seq_tau(std::uint32_t age, std::uint32_t fresh_ms) noexcept {
    const std::uint32_t ms = age < 2000u ? age + (fresh_ms < 2000u ? fresh_ms : 2000u) : 2000u;
    const float t = float(std::int32_t(ms)) * 0.001f;
    return t < kSeqTauMaxS ? t : kSeqTauMaxS;
}
__attribute__((always_inline)) inline bool handover_side(const Side& sd) noexcept {
    return sd.order == sc::kOrderNone || sd.order == sc::kOrderRotate;
}
// The hull's lateral reach for 16b: the cylinder (radius w, half-length r) at the nose theta off F lies inside the disc of radius
// min(h, w + r sin theta) around its centre's projection (h = sqrt(r^2 + w^2) its bounding radius; the 16.1 reach() caps at r,
// which a tilted cylinder exceeds beyond atan(r / w)).
__attribute__((always_inline)) inline float seq_reach(const Entry& e, float cos_t) noexcept {
    const float c = cos_t > 1.0f ? 1.0f : cos_t < -1.0f ? -1.0f : cos_t;
    const float v = e.w + e.len * (c < 0.0f ? 1.0f : sc::sqrt_ss(1.0f - c * c));
    return v < e.h ? v : e.h;
}
// mot(S) = tau (v_perp + r (|omega| + alpha tau)): the centre's lateral motion plus the reach growth r dtheta until the next
// classification (tau = min(1 s, age + fresh)); the grown reach never beyond h. A hand-over side's lateral state is P2's, possibly
// minutes old: its centre's drift is bounded over the whole time since P2 instead (the speed slews down at a_lin from |u| at P2, the
// lateral part with it: v_perp (T - T^2 / (2 t_stop)), T = min(age + fresh, t_stop)). Returns the reach plus mot; *mot the mot part.
__attribute__((always_inline)) inline float seq_reach_moving(const Side& sd, std::uint32_t fresh_ms, float* mot) noexcept {
    const Entry& e = *sd.e;
    const float tau = seq_tau(sd.age, fresh_ms);
    // a hand-over's nose is P2's: the 0x1f rotate ends within 8.11 deg of F and order 0 holds no attitude, so its reach is taken at
    // no less than 8.11 deg (the in-lane bound of section 16.1), 12 deg when its in-lane state is held by the hysteresis
    const float floor_cos = lane_state(sd) == 2 ? kHoldCos : kInLaneCos;
    const float rho = seq_reach(e, handover_side(sd) && sd.cos_t > floor_cos ? floor_cos : sd.cos_t);
    float grown = rho + tau * e.len * (e.omega + e.alpha * tau);
    if (grown > e.h) grown = e.h > rho ? e.h : rho;
    float drift = tau * e.v_perp;
    if (handover_side(sd)) {
        const float u = sc::sqrt_ss(e.vF * e.vF + e.v_perp * e.v_perp);
        const float t_stop = e.a_lin > 0.0f ? u / e.a_lin : 1e6f;
        const std::uint32_t ms = sd.age < 600000u ? sd.age + (fresh_ms < 2000u ? fresh_ms : 2000u) : 600000u;
        float T = float(std::int32_t(ms)) * 0.001f;
        if (T > t_stop) T = t_stop;
        drift = e.v_perp * (T - (t_stop > 0.0f ? T * T / (2.0f * t_stop) : 0.0f));
        if (drift < tau * e.v_perp) drift = tau * e.v_perp;
    }
    *mot = grown - rho + drift;
    return rho + *mot;
}
// Who trails: the smaller s; within 1 m the ship not in its corridor, then the higher pointer (a total order: no cycle).
__attribute__((always_inline)) inline bool a_trails(const Side& A, const Side& B, bool corr_a, bool corr_b) noexcept {
    const float ds = B.s - A.s;
    if (ds >= kSeqTieM) return true;
    if (ds <= -kSeqTieM) return false;
    if (corr_a != corr_b) return !corr_a;
    return A.e->ship > B.e->ship;
}
// The verdict with sequencing: amendment 16's, and for a watch pair (its (c) off the same lane point, sequencing on) the
// certificates (i) lateral and (ii) along F. Fail-closed: unknown motions, a non-finite field, a trailer on order 5 or with a <= 0,
// a leader turned beyond 60 deg or moving backward along F drop the route and the pair stays (c).
inline Verdict pair_verdict16b(const Side& A, const Side& B, std::uint32_t fresh_ms, bool seq, PairInfo* info = nullptr) noexcept {
    PairInfo local;
    PairInfo& in = info ? *info : local;
    in = PairInfo{};
    const Verdict v = pair_verdict(A, B, &in);
    if (v != Verdict::cross || !seq || in.same_lane) return v;
    in.watch = true;
    const Entry &ea = *A.e, &eb = *B.e;
    if (!motion_known(ea) || !motion_known(eb)) {
        in.why = 1;
        return Verdict::cross;
    }
    const float dq[2] = {A.q[0] - B.q[0], A.q[1] - B.q[1]};
    const float ra = seq_reach_moving(A, fresh_ms, &in.mot_a), rb = seq_reach_moving(B, fresh_ms, &in.mot_b);
    in.lat_gap = sc::sqrt_ss(dq[0] * dq[0] + dq[1] * dq[1]) - ra - rb - kSeqLatGapM;
    if (sc::finite_f(in.lat_gap) && in.lat_gap >= 0.0f) {
        in.route = Route::lat;
        return Verdict::seq;
    }
    const bool at = a_trails(A, B, in.corridor_a, in.corridor_b);
    const Side& T = at ? A : B;
    const Side& L = at ? B : A;
    in.trail = at ? 1 : 2;
    if (T.order == sc::kOrderDock || !(T.e->a_lin > 0.0f)) {
        in.why = 2;
        return Verdict::cross;
    }
    if (!(L.cos_t >= kSeqLeaderCos) || !(L.vF >= -kSeqBackMs)) {
        in.why = 3;
        return Verdict::cross;
    }
    float s_t = T.s;
    if (handover_side(T) && T.vF > 0.0f) s_t += T.vF * T.vF / (2.0f * T.e->a_lin); // its stopping point
    const float closing = T.vF - L.vF > 0.0f ? T.vF - L.vF : 0.0f;
    in.along_gap = L.s - s_t - (T.e->h + L.e->h + kSeqAlongM + seq_tau(T.age, fresh_ms) * closing);
    if (sc::finite_f(in.along_gap) && in.along_gap >= 0.0f) {
        in.route = Route::along;
        return Verdict::seq;
    }
    in.why = 4;
    return Verdict::cross;
}
// The severity order for the symmetric verdict (review F6): an uncertified (a) is more severe than a certified one, and none (no
// verdict) is the most severe (today's rules).
__attribute__((always_inline)) inline unsigned severity(Verdict v, Route r) noexcept {
    switch (v) {
    case Verdict::exempt: return r == Route::foot ? 1u : 3u;
    case Verdict::seq: return 2u;
    case Verdict::follow: return 4u;
    case Verdict::cross: return 5u;
    default: return 6u;
    }
}
// Site E's certificate (review F1): the footprint route of (a) or a sequenced pair (lateral or along F).
__attribute__((always_inline)) inline bool certified(Verdict v, Route r) noexcept {
    return (v == Verdict::exempt && r == Route::foot) || (v == Verdict::seq && (r == Route::lat || r == Route::along));
}
// The pair's verdict from the stored board in both directions, the more severe taken (review F6); *info is A's direction with the
// chosen route.
inline Verdict verdict_sym(const Gate& g, const Entry& ea, const Entry& eb, std::uint32_t now, std::uint32_t fresh_ms, bool seq,
                           PairInfo* info = nullptr) noexcept {
    Side a, b;
    side_of(g, ea, nullptr, nullptr, ea.order, now, &a);
    side_of(g, eb, nullptr, nullptr, eb.order, now, &b);
    PairInfo ia, ib;
    const Verdict va = pair_verdict16b(a, b, fresh_ms, seq, &ia);
    const Verdict vb = pair_verdict16b(b, a, fresh_ms, seq, &ib);
    const bool take_b = severity(vb, ib.route) > severity(va, ia.route);
    if (info) {
        *info = ia;
        if (take_b) info->route = ib.route;
    }
    return take_b ? vb : va;
}
// The sequencing obligation (section 16.10.2, a liveness choice, not part of the proof; widened in the implementation, section
// 16.11): a watch pair whose lateral paths come within reach of each other. A lateral mover (order 4 not in lane, or order 5) is the
// segment q -> its lane point ((0, 0) on order 5) with reach h (its nose along the join is not bounded: the cylinder's bounding
// radius); a settled ship (in lane, or in the hand-over) is its point q with min(h, w + r sin theta) + 25 m. Obliged when the paths
// are closer than the two reaches plus 50 m plus kSeqObligeM (the lateral motion until the next classification). Two settled ships
// of a watch pair (a hand-over off its lane or turned beyond 8.11 deg next to an in-lane neighbour) are obliged by the same test: they
// can still close along F.
constexpr float kSeqObligeM = 100.0f;
inline bool seq_obliged(const Side& A, Foot fa, const Side& B, Foot fb) noexcept {
    const bool ma = A.order == sc::kOrderDock || (A.order == sc::kOrderFlyBefore && fa != Foot::in_lane);
    const bool mb = B.order == sc::kOrderDock || (B.order == sc::kOrderFlyBefore && fb != Foot::in_lane);
    float a0[2] = {A.q[0], A.q[1]}, a1[2] = {A.q[0], A.q[1]}, b0[2] = {B.q[0], B.q[1]}, b1[2] = {B.q[0], B.q[1]};
    float ra, rb;
    if (ma) {
        a1[0] = A.order == sc::kOrderDock ? 0.0f : A.e->lane[0];
        a1[1] = A.order == sc::kOrderDock ? 0.0f : A.e->lane[1];
        ra = A.e->h;
    } else
        ra = seq_reach(*A.e, A.cos_t) + (lane_state(A) == 2 ? kMarginHeldM : kMarginM);
    if (mb) {
        b1[0] = B.order == sc::kOrderDock ? 0.0f : B.e->lane[0];
        b1[1] = B.order == sc::kOrderDock ? 0.0f : B.e->lane[1];
        rb = B.e->h;
    } else
        rb = seq_reach(*B.e, B.cos_t) + (lane_state(B) == 2 ? kMarginHeldM : kMarginM);
    const float d = seg_seg2(a0, a1, b0, b1);
    return !sc::finite_f(d) || d < ra + rb + kSeqLatGapM + kSeqObligeM;
}
// The sequencing cap of the trailer T behind the leader L (section 16.10.2; both from the stored board, so the latency counts
// both ages): D_x = h_T + h_L + 100 m; the leader's along-F credit v_c only in lane or in the hand-over (its state extrapolated to
// now there); the sum, stopping and relative forms; never below 1 unit/s. *ds_out / *Dx_out: s_L - s_T and D_x.
// always_inline: a float returned out of line comes back in st(0) on i686 (check_no_x87).
__attribute__((always_inline)) inline float seq_cap(const Side& T, const Side& L, Foot foot_l, std::uint32_t fresh_ms, float* ds_out = nullptr,
                                                     float* Dx_out = nullptr) noexcept {
    const Entry &et = *T.e, &el = *L.e;
    const float aT = et.a_lin, aL = el.a_lin;
    const float Dx = et.h + el.h + kSeqCapExtraM;
    if (ds_out) *ds_out = L.s - T.s;
    if (Dx_out) *Dx_out = Dx;
    if (!(aT > 0.0f) || !(et.h > 0.0f) || !(el.h > 0.0f)) return kFollowFloorMs;
    const bool l_hand = handover_side(L);
    const std::uint32_t age_l = l_hand ? 0u : (L.age < 2000u ? L.age : 2000u);
    float vc = 0.0f;
    if ((foot_l == Foot::in_lane || l_hand) && aL > 0.0f) {
        vc = L.vF - aL * float(std::int32_t(age_l)) * 0.001f;
        if (lane_state(L) == 2) vc -= (L.vF > 0.0f ? L.vF : 0.0f) * (1.0f - kHoldCos); // held: the turn's loss of v.F at 12 deg
        if (!(vc > 0.0f)) vc = 0.0f;
    }
    const float vT = T.vF > 0.0f ? T.vF : 0.0f;
    const std::uint32_t lat_ms = (fresh_ms < 2000u ? fresh_ms : 2000u) + age_l + (T.age < 2000u ? T.age : 2000u);
    const float room = L.s - T.s - Dx - vT * float(std::int32_t(lat_ms)) * 0.001f;
    if (!sc::finite_f(room) || room <= 0.0f) return kFollowFloorMs;
    float cap = vc + sc::sqrt_ss(2.0f * aT * room);
    if (aL > 0.0f) {
        const float stop = sc::sqrt_ss(vc * vc * aT / aL + 2.0f * aT * room);
        if (stop < cap) cap = stop;
        if (aT > aL && vc > 0.0f) {
            const float v_rel = sc::sqrt_ss(2.0f * (aT - aL) * room);
            if (v_rel * aL <= vc * (aT - aL) && vc + v_rel < cap) cap = vc + v_rel;
        }
    }
    if (!sc::finite_f(cap)) return kFollowFloorMs;
    return cap < kFollowFloorMs ? kFollowFloorMs : cap;
}
// The engine's warn reach for a trailing T behind a leader L (ship-navigation-ai.md section 3.1): b + clamp(r_T + r_L / 10, 30 m,
// 1 km) + (r_T + r_L) + r_T cos theta + (r_T + r_L), b = min(600 m, v_close^2 / 2 a_T), v_close = (u_T - u_L) . F; the cone's
// r_A cos theta term is taken at its bound r_T (the note's sum omits it; without it the engine fires inside D). Plus 100 m.
__attribute__((always_inline)) inline float follow_reach(float vT_F, float vL_F, float a_T, float r_T, float r_L) noexcept {
    const float vc = vT_F - vL_F;
    float b = a_T > 0.0f ? vc * vc / (2.0f * a_T) : 600.0f;
    if (!(b < 600.0f)) b = 600.0f;
    float L = r_T + r_L * 0.1f;
    L = L < 30.0f ? 30.0f : L > 1000.0f ? 1000.0f : L;
    return b + L + 2.0f * (r_T + r_L) + r_T + kFollowExtraM;
}
// The trailing ship's speed cap: max(0, u_L . F) + sqrt(2 a_T max(0, ds - D)) (section 16.1), lowered to the stopping-distance
// form sqrt(v_L^2 a_T / a_L + 2 a_T (ds - D)) when a_L is known (implementation, recorded in section 16.9): the sum alone lets the
// spacing fall below D when the leader brakes at the trailer's own deceleration (the kept P2 coast; scenario M-b measured 167 m),
// and inside D it only matches the leader instead of opening the gap again. Never above the ratified cap.
__attribute__((always_inline)) inline float follow_cap(float vL_F, float a_T, float ds, float D, float a_L = 0.0f) noexcept {
    const float room = ds - D, vL = vL_F > 0.0f ? vL_F : 0.0f;
    float cap = vL + (room > 0.0f && a_T > 0.0f ? sc::sqrt_ss(2.0f * a_T * room) : 0.0f);
    if (a_L > 0.0f && a_T > 0.0f) {
        const float k = vL * vL * a_T / a_L + 2.0f * a_T * room;
        const float kin = k > 0.0f ? sc::sqrt_ss(k) : 0.0f;
        if (kin < cap) cap = kin;
    }
    return cap;
}
// ---- one decision's lane pass (review F2, F5, F6; amendment 16b section 16.10.2 "one pass") -----------------------------------
// The deciding capital's own entry binds when it is its own (id), on the deciding order (4 or 5), fresh (committed_stored: review
// F5) and on the board of its live target. Then every committed neighbour's verdict is computed once from the stored board in both
// directions (verdict_sym), and the trailing caps of this ship on order 4: the (b) follow cap (section 16.1, as built) and the
// sequencing cap over every obliged watch pair in which it trails. The avoidance's exclusion (lane_pair) and site B (cap()) read
// the same pass: a pair is never dropped from avoidance without its cap in force.
struct Pass {
    const Gate* g = nullptr;
    const Entry* self = nullptr;
    std::uint32_t now = 0, fresh_ms = 0;
    bool seq = false;
    unsigned n = 0;                            // the board's entries and their objects as of the pass (refresh() may move them later)
    std::uint32_t ship[kLaneEntries] = {};
    Verdict v[kLaneEntries] = {};
    Route route[kLaneEntries] = {};
    std::uint8_t seq_trail[kLaneEntries] = {}; // 1: an obliged watch pair in which this ship trails (its sequencing cap applies)
    float follow_cap = -1.0f, seq_cap = -1.0f; // m/s, < 0 none
    std::uint32_t follow_leader = 0, seq_leader = 0;
    float follow_ds = 0, follow_D = 0, seq_ds = 0, seq_Dx = 0;
    unsigned n_follow = 0, n_lat = 0, n_along = 0, n_watch_cross = 0, n_obliged = 0;
    bool self_held = false; // this ship's in-lane state held by the hysteresis (counter lane_hysteresis_hold)
    // the lower of the two caps (m/s), < 0 when none applies
    __attribute__((always_inline)) float cap() const noexcept {
        if (follow_cap < 0.0f) return seq_cap;
        if (seq_cap < 0.0f) return follow_cap;
        return follow_cap < seq_cap ? follow_cap : seq_cap;
    }
};
inline bool run_pass(const Board& b, std::uint32_t gate, std::uint32_t ship, std::uint32_t id, std::uint16_t order, std::uint32_t now,
                     std::uint32_t fresh_ms, bool seq, Pass* p) noexcept {
    *p = Pass{};
    if (order != sc::kOrderFlyBefore && order != sc::kOrderDock) return false;
    const Gate* g = gate_of(b, gate);
    const Entry* me = g ? find(*g, ship) : nullptr;
    if (!me || me->id != id || me->order != order || !committed_stored(*me, now, fresh_ms)) return false;
    p->g = g;
    p->self = me;
    p->now = now;
    p->fresh_ms = fresh_ms;
    p->seq = seq;
    Side a;
    side_of(*g, *me, nullptr, nullptr, me->order, now, &a);
    p->self_held = lane_state(a) == 2;
    const bool trailer_on_4 = order == sc::kOrderFlyBefore;
    p->n = g->n;
    for (unsigned i = 0; i < g->n; ++i) {
        const Entry& e = g->e[i];
        if (&e == me || !committed_stored(e, now, fresh_ms)) continue;
        p->ship[i] = e.ship;
        Side o;
        side_of(*g, e, nullptr, nullptr, e.order, now, &o);
        PairInfo ia, ib;
        const Verdict va = pair_verdict16b(a, o, fresh_ms, seq, &ia);
        const Verdict vb = pair_verdict16b(o, a, fresh_ms, seq, &ib);
        const bool take_b = severity(vb, ib.route) > severity(va, ia.route);
        const Verdict v = take_b ? vb : va;
        const Route r = take_b ? ib.route : ia.route;
        p->v[i] = v;
        p->route[i] = r;
        if (v == Verdict::seq) {
            if (r == Route::lat) ++p->n_lat;
            else ++p->n_along;
        } else if (v == Verdict::cross && (ia.watch || ib.watch))
            ++p->n_watch_cross;
        if (!trailer_on_4) continue;
        if (v == Verdict::follow && o.s > a.s) { // this ship trails a (b) pair: the follow cap (section 16.1 as built)
            ++p->n_follow;
            const float ds = o.s - a.s;
            const float D = follow_reach(a.vF, o.vF, me->a_lin, me->r, e.r);
            float cap = follow_cap(o.vF, me->a_lin, ds, D, e.a_lin);
            if (cap < kFollowFloorMs) cap = kFollowFloorMs;
            if (p->follow_cap < 0.0f || cap < p->follow_cap) {
                p->follow_cap = cap;
                p->follow_leader = e.ship;
                p->follow_ds = ds;
                p->follow_D = D;
            }
        }
        if (!seq || !(ia.watch || ib.watch)) continue;
        if (!a_trails(a, o, ia.corridor_a, ia.corridor_b) || !seq_obliged(a, ia.foot_a, o, ia.foot_b)) continue;
        ++p->n_obliged;
        p->seq_trail[i] = 1;
        float ds = 0, Dx = 0;
        const float cap = seq_cap(a, o, ia.foot_b, fresh_ms, &ds, &Dx);
        if (p->seq_cap < 0.0f || cap < p->seq_cap) {
            p->seq_cap = cap;
            p->seq_leader = e.ship;
            p->seq_ds = ds;
            p->seq_Dx = Dx;
        }
    }
    return true;
}
// The pass's verdict for one neighbour by pointer (none when it has no committed entry); *index its board slot.
__attribute__((always_inline)) inline Verdict pass_verdict(const Pass& p, std::uint32_t ship, unsigned* index = nullptr) noexcept {
    if (!p.g || !ship) return Verdict::none;
    for (unsigned i = 0; i < p.n; ++i)
        if (p.ship[i] == ship) {
            if (index) *index = i;
            return p.v[i];
        }
    return Verdict::none;
}
// The lapse memory of a trailer (the flight check of the cap invariant, counter lane_seq_lapse): the leaders of its obliged pairs
// that held the along route at its last pass. A lapse is such a pair now (c).
struct SeqMemory {
    std::uint32_t leader[4] = {0, 0, 0, 0};
};
inline unsigned seq_lapses(const Pass& p, SeqMemory& m) noexcept {
    unsigned lapses = 0;
    for (unsigned k = 0; k < 4; ++k) {
        if (!m.leader[k]) continue;
        unsigned i = 0;
        if (pass_verdict(p, m.leader[k], &i) == Verdict::cross && p.seq_trail[i]) ++lapses;
    }
    SeqMemory next;
    unsigned n = 0;
    for (unsigned i = 0; i < p.n && n < 4; ++i) // the pass's own capture: never the board after refresh()
        if (p.seq_trail[i] && p.v[i] == Verdict::seq && p.route[i] == Route::along) next.leader[n++] = p.ship[i];
    m = next;
    return lapses;
}
// Site B (review F2): the pass's cap is in force on the module's own writes, and it only ever lowers them. On the gate write
// (kShipFlagGate, both directions) it lowers v_ref when v_ref is a write (> 0); otherwise nothing. On the lowering clamp path
// (kShipFlagClamp without kShipFlagGate) it lowers the cornering term v_corner, so site B's own computation (the arrival ramp with
// the amendment-14 v_slip, then v_corner, then the emergency ramp) runs unchanged under it, and kShipFlagLower drops only the final
// 10 m/s floor (the cap may be 1 unit/s). Where the module writes nothing on this path (no clamp flag, or no cornering term and no
// ramp: a capital outside its window on the engine's law) the cap is a lowering-only write of the engine's own speed (clamp_units
// against the engine's desired value at site B): it never raises it. Returns whether the cap lowered the decision's value.
inline bool apply_cap(sc::Ship& s, float cap) noexcept {
    if (!(cap >= 0.0f) || !sc::finite_f(cap)) return false;
    if (s.flags & sc::kShipFlagGate) {
        if (!(s.v_ref > 0.0f) || !sc::finite_f(s.v_ref) || !(cap < s.v_ref)) return false;
        s.v_ref = cap;
        return true;
    }
    if (s.flags & sc::kShipFlagGateDock) return false; // order 5: no lane cap
    if (!(s.flags & sc::kShipFlagClamp)) { // the engine's law, no module write: the cap alone, lowering only
        s.flags = std::uint16_t(s.flags | sc::kShipFlagClamp);
        s.v_corner = 0.0f;
        s.v_slip = 0.0f;
    }
    if (s.v_corner > 0.0f && !(cap < s.v_corner)) return false;
    s.v_corner = cap;
    s.flags = std::uint16_t(s.flags | sc::kShipFlagLower);
    return true;
}
// Review F4: a cached hull width is read again when the cache is for another radius generation or root node, or when the last read
// failed and its retry is due (every kWidthRetryMs, at most kWidthRetries times; then w = r until the generation changes).
constexpr std::uint32_t kWidthRetryMs = 5000;
constexpr unsigned kWidthRetries = 3;
__attribute__((always_inline)) inline bool width_reread(bool cached, std::uint8_t src, unsigned tries, std::uint32_t last_ms, std::uint32_t now) noexcept {
    if (!cached) return true;
    if (src != std::uint8_t(WidthSrc::failed)) return false;
    return tries < kWidthRetries && now - last_ms >= kWidthRetryMs;
}
// Section 16.3 / 16.4 path guards: dist(G, segment[x, P]) >= r_gate + r_ship + k r_gate (k 0.25 capital to its grid point,
// 0.1 small ship to its point A).
__attribute__((always_inline)) inline float point_segment_dist3(const float p[3], const float a[3], const float b[3]) noexcept {
    const float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, ap[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
    const float l2 = sc::dot3(ab, ab);
    float t = l2 > 1e-6f ? sc::dot3(ap, ab) / l2 : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    const float d[3] = {ap[0] - t * ab[0], ap[1] - t * ab[1], ap[2] - t * ab[2]};
    return sc::sqrt_ss(sc::dot3(d, d));
}
constexpr float kCapitalGuardR = 0.25f, kSmallGuardR = 0.1f;
inline bool path_guard(const float G[3], const float x[3], const float P[3], float r_gate, float r_ship, float k) noexcept {
    const float d = point_segment_dist3(G, x, P);
    return sc::finite_f(d) && sc::finite_f(r_gate) && sc::finite_f(r_ship) && d >= r_gate + r_ship + k * r_gate;
}
} // namespace x3m::ship_avoidance::lane
