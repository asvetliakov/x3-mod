#pragma once
#include "ship_steering_core.h"

// Portable core of the predictive collision avoidance (docs/architecture/
// ship-steering-and-avoidance.md sections 3, 4 and 6; step 2). It sits in the
// steering handler's target seam: the order's point P in, the point P' the law
// steers to out, over a per-sector per-frame obstacle snapshot the DLL fills
// from the bucket lists (ship_steering.cpp) and the host fixture fills from
// its scenario. SSE2 scalar float only (the helpers of ship_steering_core.h),
// no allocation, no CRT math: it runs inside the leaf on the light envelope.
//
// Model (section 3.3): the ship moves along its nose at v for L metres; an
// obstacle moves at its velocity; a conflict is a closest approach within
// C = r_own + r_obs + max(25 m, 0.5 r_own) inside the horizon T = L/v with
// L = clamp(0.5 s v + 1.25 min(sqrt(C(C + 2R)), v^2/2a), C + r_own, 30 km),
// R = v/omega. The earliest conflict gets a tangent detour P' at the
// obstacle's predicted position plus 1.1 C sideways in the plane of least
// turn (capitals: over/under first), the side kept per (ship, obstacle) until
// the obstacle is behind; a detour that points backward or crosses another
// candidate's sphere tries the other sides, after four tries the call keeps P
// and reports unresolved (the reactive loop stays). Stations: the bounding
// sphere selects, the child-node part spheres decide and the hit part is the
// body detoured (section 3.5, the part-sphere refinement of the path-planning
// note); without a validated part model the fallback body is 0.35 x sphere
// (floor 500 m) and the pair is never suppressed at site E.
namespace x3m::ship_avoidance::core {
namespace sc = x3m::ship_steering::core;
constexpr unsigned kSnapshotCapacity = 1024; // obstacles per sector snapshot (beyond it: pass-through, counted)
constexpr unsigned kMaxCandidates = 8;
constexpr unsigned kMaxResolved = 8;
constexpr unsigned kMaxSides = 4;      // per-ship side memory
constexpr unsigned kMaxParts = 64;     // station part spheres kept
constexpr unsigned kStationCapacity = 64; // station part models cached
constexpr float kReactionS = 1.0f, kRampMargin = 1.25f, kLookaheadMaxM = 30000.0f; // reaction 1 s: the design's 0.5 s plus one think tick and the servo tail
constexpr float kDetourFactor = 1.3f, kDetourFactorMoving = 1.75f; // sideways offset of the detour point in units of C (the design's 1.1
                                                                    // leaves a 1 % shortfall after the servo lag on capitals; moving obstacles
                                                                    // react too, with their own lag: a wider margin)
constexpr float kClearanceMinM = 25.0f, kFallbackSphereFactor = 0.35f, kFallbackFloorM = 500.0f;
constexpr float kCommitFactor = 2.0f;  // a detour in progress (direction remembered, obstacle ahead) keeps its conflict until the
                                       // predicted miss exceeds 2 C (beyond both detour offsets): no chatter between the detour point
                                       // and the order's point before the detour is complete
constexpr std::uint32_t kResolvedAgeMs = 100; // suppression window at site E (two think intervals)
constexpr std::uint32_t kFormationEnvelopeFactor = 10; // wingmates inside 10 x f+0x260 of the leader are not obstacles

enum class Kind : std::uint8_t { ship = 0, station = 1, asteroid = 2, gate = 3, debris = 4 };
struct Obstacle {
    float x[3];        // m, world
    float v[3];        // m/s
    float r;           // m, the bounding sphere ([obj+0xa4])
    std::uint32_t ptr; // the engine object (identity only)
    std::uint32_t leader; // ships: f+0x224 (escort/follow partner), else 0
    std::uint16_t order;  // ships: f+0x258
    std::uint8_t kind;    // Kind
    std::uint8_t flags;   // bit 0: station part model available (parts index valid)
    std::uint16_t parts;  // index into the part store
    std::uint16_t pad;
};
struct Snapshot {
    std::uint32_t sector = 0, epoch = 0;
    unsigned count = 0, overflow = 0;
    float max_r = 0;
    Obstacle items[kSnapshotCapacity];
};
// One station's part spheres (world space), built once per station per lifetime generation.
struct PartSet {
    std::uint32_t station = 0, generation = 0; // generation: the root sphere radius bits (changes on a new model)
    std::uint32_t stamp = 0;                   // the station's position and basis (a moved station rebuilds)
    unsigned count = 0;
    float c[kMaxParts][3];
    float r[kMaxParts];
};
struct Self {
    float x[3], f[3];      // m, unit forward
    float v, r, a, omega;  // m/s, m, m/s^2, rad/s (the slower of yaw/pitch)
    float alpha;           // rad/s^2, the angular acceleration of that axis (0 = ignore the ramp)
    float up[3];           // world up for the capital preference
    std::uint32_t ptr, leader, target; // self, f+0x224 when it is a leader/partner, the order's target object
    float leader_x[3];     // the leader's position (valid when leader != 0)
    float envelope;        // 10 x f+0x260, m
    std::uint16_t order;
    bool capital;
    bool exclude_target;   // the order's target object is not an obstacle (orders 4/5/0x14/0x15/0x10/0x11/0x18)
    float L_leader;        // the leader's last lookahead (0 = none): the follower sees at least as far
};
struct SideMemory {
    std::uint32_t obstacle;
    float dir[3];          // the detour direction (world, unit), 0 = free: the side is re-derived as sign(axis . dir)
                           // each call, so the (f, obstacle)-plane axis may flip its orientation without flipping the side
    std::uint8_t vertical; // 1 = the detour is in the vertical family (capital)
    std::uint8_t used;
    std::uint16_t pad;
    std::uint32_t tick;    // the call that last used it (the oldest is evicted)
};
struct ShipAvoid {
    std::uint32_t obj = 0, stamp_ms = 0, tick = 0;
    std::uint32_t resolved[kMaxResolved] = {};
    unsigned n_resolved = 0;
    SideMemory sides[kMaxSides] = {};
    float L = 0;           // the last lookahead, m
    float v_emergency = 0; // m/s, 0 = none (the braking ramp when no detour exists)
    bool unresolved = false;
};
struct Candidate {
    unsigned index;        // snapshot item
    unsigned part_set;     // the station's part set (kStationCapacity = none: a sphere body)
    float t_cpa, miss, C, L;
    float x_cpa[3];        // the obstacle's position at t_cpa
    float e[3];            // unit vector obstacle -> our closest point
    float d;               // current distance
    bool part;             // a station part (x/r below are the part's)
    float px[3], pr;
};
struct Result {
    bool adjusted = false, unresolved = false, overflow = false;
    unsigned candidates = 0, conflicts = 0, tries = 0;
    float P[3] = {0, 0, 0};
    float t_cpa = 0, miss = 0, C = 0, L = 0;
    std::uint32_t obstacle = 0;
    std::uint8_t obstacle_kind = 0;
    std::int8_t side = 0;
    float dir[3] = {0, 0, 0}; // the detour direction (world, unit)
    bool vertical = false, emergency = false;
    float v_emergency = 0;
};

inline float clearance(float r_own, float r_obs) noexcept {
    return r_own + r_obs + sc::fmax_f(kClearanceMinM, 0.5f * r_own);
}
// Lookahead for a clearance C (section 3.3): the swept path length. `alpha` adds the distance flown while the
// rate ramps up to omega and back (2 omega / alpha seconds; the design's formula assumes the full rate at once,
// which the fixture showed leaves slow-ramping ships (TS, M6, capitals) no room to complete a detour).
inline float lookahead(float v, float a, float omega, float r_own, float C, float alpha = 0.0f) noexcept {
    const float R = omega > 1e-4f ? v / omega : 1e6f;
    const float d_turn = sc::sqrt_ss(C * (C + 2.0f * R));
    const float d_stop = a > 1e-3f ? v * v / (2.0f * a) : 1e9f;
    const float ramp = (alpha > 1e-6f && omega > 1e-4f) ? v * (2.0f * omega / alpha) : 0.0f;
    float L = kReactionS * v + ramp + kRampMargin * sc::fmin_f(d_turn, d_stop);
    if (L < C + r_own) L = C + r_own;
    if (L > kLookaheadMaxM) L = kLookaheadMaxM;
    return L;
}
inline void sub3(const float a[3], const float b[3], float out[3]) noexcept {
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}
inline void cross3(const float a[3], const float b[3], float out[3]) noexcept {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
inline float norm3(float v[3]) noexcept { // normalises in place, returns the length (0 when degenerate)
    const float n = sc::sqrt_ss(sc::dot3(v, v));
    if (n > 1e-9f) {
        v[0] /= n;
        v[1] /= n;
        v[2] /= n;
        return n;
    }
    v[0] = v[1] = v[2] = 0.0f;
    return 0.0f;
}
// Closest approach of our point x + v f t to a sphere centre c moving at w, over t in [0, T].
// Returns false when the miss distance is not below C; else fills t, miss, the centre at t and e.
inline bool closest_approach(const Self& s, const float c[3], const float w[3], float T, float C, float* t_out, float* miss,
                             float c_at[3], float e[3]) noexcept {
    float dx[3], dv[3];
    sub3(c, s.x, dx);
    for (int i = 0; i < 3; ++i) dv[i] = w[i] - s.v * s.f[i];
    const float vv = sc::dot3(dv, dv);
    float t = vv > 1e-6f ? -sc::dot3(dx, dv) / vv : 0.0f;
    if (t < 0.0f) { // already separating (the closest approach is in the past): nothing to avoid
        if (sc::dot3(dx, dv) > 0.0f) return false;
        t = 0.0f;
    }
    if (t > T) t = T;
    float rel[3];
    for (int i = 0; i < 3; ++i) rel[i] = dx[i] + dv[i] * t;
    const float m = sc::sqrt_ss(sc::dot3(rel, rel));
    if (!(m < C)) return false;
    *t_out = t;
    *miss = m;
    for (int i = 0; i < 3; ++i) {
        c_at[i] = c[i] + w[i] * t;
        e[i] = -rel[i];
    }
    if (norm3(e) == 0.0f) { // dead centre: any lateral direction; take the one perpendicular to f in the up plane
        cross3(s.f, s.up, e);
        if (norm3(e) == 0.0f) {
            e[0] = 1;
            e[1] = e[2] = 0;
        }
    }
    return true;
}
// Whether the segment x -> P passes within C of the sphere (c, moving at w is ignored: the check is geometric).
inline bool segment_hits(const float x[3], const float P[3], const float c[3], float C) noexcept {
    float d[3], dc[3];
    sub3(P, x, d);
    sub3(c, x, dc);
    const float dd = sc::dot3(d, d);
    float t = dd > 1e-6f ? sc::dot3(dc, d) / dd : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float q[3];
    for (int i = 0; i < 3; ++i) q[i] = dc[i] - d[i] * t;
    return sc::dot3(q, q) < C * C;
}
// The station part model from the walk results the DLL (or the fixture) provides: validated per section 3.5
// (finite, every part sphere inside the bounding sphere: |c - pos| + r <= root_r, at most kMaxParts). Returns false
// when the layout does not validate.
inline bool build_parts(PartSet& set, std::uint32_t station, float root_r, const float pos[3], const float centres[][3], const float radii[],
                        unsigned n) noexcept {
    set.station = 0;
    set.count = 0;
    if (n == 0 || n > kMaxParts || !(root_r > 0.0f)) return false;
    for (unsigned i = 0; i < n; ++i) {
        if (!sc::finite_f(centres[i][0]) || !sc::finite_f(centres[i][1]) || !sc::finite_f(centres[i][2]) || !sc::finite_f(radii[i])) return false;
        if (!(radii[i] > 0.0f) || radii[i] > root_r) return false;
        const float dc[3] = {centres[i][0] - pos[0], centres[i][1] - pos[1], centres[i][2] - pos[2]};
        if (sc::sqrt_ss(sc::dot3(dc, dc)) + radii[i] > root_r + 0.5f) return false;
        set.c[i][0] = centres[i][0];
        set.c[i][1] = centres[i][1];
        set.c[i][2] = centres[i][2];
        set.r[i] = radii[i];
    }
    set.station = station;
    set.count = n;
    std::memcpy(&set.generation, &root_r, 4);
    return true;
}
inline const SideMemory* remembered(const ShipAvoid& a, std::uint32_t obstacle) noexcept {
    for (unsigned i = 0; i < kMaxSides; ++i)
        if (a.sides[i].obstacle == obstacle && a.sides[i].used) return &a.sides[i];
    return nullptr;
}
// Whether the straight segment x -> P clears the candidate's whole body by its clearance: every part sphere of a
// station with a part model, else the one sphere.
inline bool clears_body(const float x[3], const float P[3], const Candidate& c, float r_own, const PartSet* parts, unsigned part_sets) noexcept {
    if (c.part && c.part_set < part_sets) {
        const PartSet& set = parts[c.part_set];
        for (unsigned p = 0; p < set.count; ++p)
            if (segment_hits(x, P, set.c[p], clearance(r_own, set.r[p]))) return false;
        return true;
    }
    return !segment_hits(x, P, c.x_cpa, c.C);
}
inline void remember_side(ShipAvoid& a, std::uint32_t obstacle, const float dir[3], bool vertical) noexcept {
    SideMemory* slot = nullptr;
    for (unsigned i = 0; i < kMaxSides && !slot; ++i)
        if (a.sides[i].used && a.sides[i].obstacle == obstacle) slot = &a.sides[i];
    for (unsigned i = 0; i < kMaxSides && !slot; ++i)
        if (!a.sides[i].used) slot = &a.sides[i];
    if (!slot) { // evict the least recently used
        slot = &a.sides[0];
        for (unsigned i = 1; i < kMaxSides; ++i)
            if (a.sides[i].tick < slot->tick) slot = &a.sides[i];
    }
    slot->tick = a.tick;
    slot->obstacle = obstacle;
    slot->dir[0] = dir[0];
    slot->dir[1] = dir[1];
    slot->dir[2] = dir[2];
    slot->vertical = vertical ? 1 : 0;
    slot->used = 1;
}
// Forget the sides of obstacles that are behind the ship (section 3.4 hysteresis ends there).
inline void forget_passed(ShipAvoid& a, const Snapshot& snap, const Self& s) noexcept {
    for (unsigned i = 0; i < kMaxSides; ++i) {
        if (!a.sides[i].used) continue;
        bool present = false, behind = true;
        for (unsigned k = 0; k < snap.count; ++k)
            if (snap.items[k].ptr == a.sides[i].obstacle) {
                float dx[3];
                sub3(snap.items[k].x, s.x, dx);
                // most of the body behind the ship by more than the pair's clearance (two ships abeam still inside C are
                // not "passed": a new opposite-side detour would reverse the turn); a part still ahead re-triggers its own detour
                behind = sc::dot3(dx, s.f) < -(0.5f * snap.items[k].r + clearance(s.r, snap.items[k].r));
                present = true;
                break;
            }
        if (!present || behind) a.sides[i] = SideMemory{};
    }
}
// Whether the obstacle item is excluded for this ship (section 3.2 and 4).
inline bool excluded(const Self& s, const Obstacle& o) noexcept {
    if (o.ptr == s.ptr) return true;
    if (s.leader && o.ptr == s.leader) return true;
    if (s.exclude_target && s.target && o.ptr == s.target) return true;
    if (o.kind == std::uint8_t(Kind::ship) && s.leader && o.leader == s.leader &&
        (o.order == sc::kOrderEscort || o.order == sc::kOrderFollow)) {
        float d[3];
        sub3(o.x, s.leader_x, d);
        if (sc::dot3(d, d) <= s.envelope * s.envelope) return true; // a wingmate inside the formation envelope
    }
    if (o.kind == std::uint8_t(Kind::ship) && o.leader == s.ptr && (o.order == sc::kOrderEscort || o.order == sc::kOrderFollow)) {
        float d[3];
        sub3(o.x, s.x, d);
        if (sc::dot3(d, d) <= s.envelope * s.envelope) return true; // our own followers inside the envelope
    }
    return false;
}
// The effective body of a station without a part model (section 3.5 fallback).
inline float fallback_radius(float sphere) noexcept {
    return sc::fmax_f(kFallbackSphereFactor * sphere, kFallbackFloorM);
}
// Detour axis. Lateral family: perpendicular to our heading f inside the plane of least turn (the plane of f
// and the direction to the obstacle's current position); perpendicular to f, not to that direction, so that an
// obstacle beside the ship still gets a sideways detour point and not one ahead of or behind it. Vertical
// family (capitals): world up itself (an up component perpendicular to a steeply pitched heading would point
// backward and put the detour point behind the ship); a heading within 18 degrees of vertical uses the lateral
// family instead. sign = side. False when degenerate.
inline bool lateral_axis(const Self& s, const float dir[3], bool vertical, float axis[3]) noexcept {
    if (vertical && sc::fabs_f(sc::dot3(s.up, s.f)) < 0.95f) {
        axis[0] = s.up[0];
        axis[1] = s.up[1];
        axis[2] = s.up[2];
        return norm3(axis) > 0.0f;
    }
    float n[3];
    cross3(s.f, dir, n);
    if (norm3(n) < 0.05f) { // ahead (or behind): any plane; take the one containing up
        cross3(s.f, s.up, n);
        if (norm3(n) == 0.0f) return false;
    }
    cross3(n, s.f, axis); // in the plane, perpendicular to our heading
    return norm3(axis) > 0.0f;
}
// The decision for one ship (section 3): the point P' the law steers to.
inline Result resolve(const Snapshot& snap, const PartSet* parts, unsigned part_sets, const Self& s, const float P[3],
                      ShipAvoid& state, std::uint32_t now_ms) noexcept {
    Result res;
    res.P[0] = P[0];
    res.P[1] = P[1];
    res.P[2] = P[2];
    state.unresolved = false;
    state.n_resolved = 0;
    state.v_emergency = 0.0f;
    state.stamp_ms = now_ms;
    ++state.tick;
    if (snap.overflow) {
        res.overflow = true;
        return res;
    }
    if (!(s.v > 0.1f)) return res; // not moving: nothing to predict
    forget_passed(state, snap, s);
    // Pre-filter bound: the lookahead for the largest body in the snapshot (the per-item L is at most this).
    const float C_max = clearance(s.r, snap.max_r);
    float L_max = lookahead(s.v, s.a, s.omega, s.r, C_max, s.alpha);
    if (s.L_leader > L_max) L_max = s.L_leader;
    const float reach = L_max + snap.max_r + s.r;
    state.L = L_max;
    // Candidates: conflicts sorted by t_cpa, at most kMaxCandidates.
    Candidate cand[kMaxCandidates];
    unsigned n_cand = 0;
    for (unsigned k = 0; k < snap.count; ++k) {
        const Obstacle& o = snap.items[k];
        if (sc::fabs_f(o.x[0] - s.x[0]) > reach || sc::fabs_f(o.x[1] - s.x[1]) > reach || sc::fabs_f(o.x[2] - s.x[2]) > reach) continue;
        if (excluded(s, o)) continue;
        float dx[3];
        sub3(o.x, s.x, dx);
        const float d = sc::sqrt_ss(sc::dot3(dx, dx));
        const float commit = remembered(state, o.ptr) ? kCommitFactor : 1.0f; // the detour in progress
        Candidate c;
        c.index = k;
        c.part = false;
        c.part_set = kStationCapacity;
        c.d = d;
        if (o.kind == std::uint8_t(Kind::station)) {
            // broad phase on the sphere (a plain reach test: the sphere's centre may already be abeam while parts
            // are still ahead), narrow phase on the parts (or the fallback body)
            const float C_sel = clearance(s.r, o.r);
            const float L_sel = sc::fmax_f(lookahead(s.v, s.a, s.omega, s.r, C_sel, s.alpha), s.L_leader);
            if (d > L_sel + C_sel) continue;
            float t, m, cat[3], e[3];
            const PartSet* set = nullptr;
            if ((o.flags & 1) && o.parts < part_sets && parts[o.parts].station == o.ptr && parts[o.parts].count) set = &parts[o.parts];
            if (set) {
                c.part_set = o.parts;
                float best_t = 1e30f;
                for (unsigned p = 0; p < set->count; ++p) {
                    const float Cp = clearance(s.r, set->r[p]);
                    const float Lp = sc::fmax_f(lookahead(s.v, s.a, s.omega, s.r, Cp, s.alpha), s.L_leader);
                    float tp, mp, catp[3], ep[3];
                    if (!closest_approach(s, set->c[p], o.v, Lp / s.v, Cp * commit, &tp, &mp, catp, ep)) continue;
                    if (tp < best_t) {
                        best_t = tp;
                        c.t_cpa = tp;
                        c.miss = mp;
                        c.C = Cp;
                        c.L = Lp;
                        c.part = true;
                        for (int i = 0; i < 3; ++i) {
                            c.x_cpa[i] = catp[i];
                            c.e[i] = ep[i];
                            c.px[i] = set->c[p][i];
                        }
                        c.pr = set->r[p];
                    }
                }
                if (best_t >= 1e29f) continue; // the sphere is hit, no part is: no conflict
            } else {
                const float rf = fallback_radius(o.r);
                const float Cf = clearance(s.r, rf);
                const float Lf = sc::fmax_f(lookahead(s.v, s.a, s.omega, s.r, Cf, s.alpha), s.L_leader);
                if (!closest_approach(s, o.x, o.v, Lf / s.v, Cf * commit, &t, &m, cat, e)) continue;
                c.t_cpa = t;
                c.miss = m;
                c.C = Cf;
                c.L = Lf;
                for (int i = 0; i < 3; ++i) {
                    c.x_cpa[i] = cat[i];
                    c.e[i] = e[i];
                    c.px[i] = o.x[i];
                }
                c.pr = rf;
            }
        } else {
            const float C = clearance(s.r, o.r);
            const float L = sc::fmax_f(lookahead(s.v, s.a, s.omega, s.r, C, s.alpha), s.L_leader);
            float t, m, cat[3], e[3];
            if (!closest_approach(s, o.x, o.v, L / s.v, C * commit, &t, &m, cat, e)) continue;
            c.t_cpa = t;
            c.miss = m;
            c.C = C;
            c.L = L;
            for (int i = 0; i < 3; ++i) {
                c.x_cpa[i] = cat[i];
                c.e[i] = e[i];
                c.px[i] = o.x[i];
            }
            c.pr = o.r;
        }
        ++res.conflicts;
        // insert by t_cpa
        unsigned pos = n_cand;
        while (pos > 0 && cand[pos - 1].t_cpa > c.t_cpa) --pos;
        if (pos >= kMaxCandidates) continue;
        if (n_cand < kMaxCandidates) ++n_cand;
        for (unsigned j = n_cand - 1; j > pos; --j) cand[j] = cand[j - 1];
        cand[pos] = c;
    }
    res.candidates = n_cand;
    if (!n_cand) return res;
    const Candidate& first = cand[0];
    const Obstacle& o = snap.items[first.index];
    res.obstacle = o.ptr;
    res.obstacle_kind = o.kind;
    res.t_cpa = first.t_cpa;
    res.miss = first.miss;
    res.C = first.C;
    res.L = first.L;
    // The detour: the predicted body position plus 1.1 C along the chosen lateral axis. The plane of least turn
    // is spanned by f and the direction to the body's current position (its predicted position lies on our own
    // path when the course is a collision course and would leave the plane undefined).
    float dir[3];
    sub3(first.px, s.x, dir);
    if (norm3(dir) == 0.0f) {
        dir[0] = s.f[0];
        dir[1] = s.f[1];
        dir[2] = s.f[2];
    }
    // Sides to try, in order: the remembered direction (its sign re-derived on the current axis), then the natural
    // side (toward e) and its opposite in the primary axis family (vertical for capitals, lateral otherwise), then
    // the other family; four tries at most. A symmetric conflict (e has no component along the axis: two ships on
    // an exact collision course) is broken by the object identities so that the two sides differ.
    struct Try {
        bool vertical;
        std::int8_t side;
    };
    Try tries[5];
    unsigned n_tries = 0;
    const SideMemory* mem = remembered(state, o.ptr);
    float axis_mem[3] = {0, 0, 0};
    if (mem && lateral_axis(s, dir, mem->vertical != 0, axis_mem)) {
        const float along = sc::dot3(axis_mem, mem->dir);
        tries[n_tries++] = Try{mem->vertical != 0, std::int8_t(along >= 0.0f ? 1 : -1)};
    }
    // A committed detour never changes side mid-manoeuvre: when its point is refused this tick (behind the nose
    // for a moment, or past P), the call reports unresolved rather than reversing the turn.
    for (unsigned fam = 0; fam < 2 && n_tries < 5 && !mem; ++fam) {
        const bool vertical = (fam == 0) ? s.capital : !s.capital;
        float axis[3];
        if (!lateral_axis(s, dir, vertical, axis)) continue;
        const float along = sc::dot3(first.e, axis);
        std::int8_t natural;
        if (!vertical || sc::fabs_f(along) > 0.05f)
            natural = along >= 0.0f ? 1 : -1; // lateral: e's fallback (f x up) is "turn left" for both ships of a collision
                                              // course, the same handedness, so their detours never cancel
        else
            natural = (s.ptr < o.ptr) ? 1 : -1; // vertical tie-break: the lower identity goes over, the other under
        for (int k = 0; k < 2 && n_tries < 5; ++k) {
            const std::int8_t side = k == 0 ? natural : std::int8_t(-natural);
            if (mem && n_tries && vertical == tries[0].vertical && side == tries[0].side) continue;
            tries[n_tries++] = Try{vertical, side};
        }
    }
    if (n_tries > 4) n_tries = 4;
    for (unsigned attempt = 0; attempt < n_tries; ++attempt) {
        ++res.tries;
        float axis[3];
        if (!lateral_axis(s, dir, tries[attempt].vertical, axis)) continue;
        const std::int8_t side = tries[attempt].side;
        const float factor = o.kind == std::uint8_t(Kind::ship) ? kDetourFactorMoving : kDetourFactor;
        float Pp[3], ddir[3];
        for (int i = 0; i < 3; ++i) {
            ddir[i] = axis[i] * float(side);
            Pp[i] = first.x_cpa[i] + ddir[i] * first.C * factor;
        }
        float toP[3], toPp[3];
        sub3(P, s.x, toP);
        sub3(Pp, s.x, toPp);
        // not behind the ship relative to P, not more than 120 degrees off the nose (an obstacle abeam inside C gets a
        // detour point abeam, away from it), not through another candidate's sphere (a committed detour keeps its
        // direction: the other candidate gets its turn when it becomes the earliest)
        bool ok = sc::dot3(toP, toPp) > 0.0f && sc::dot3(toPp, s.f) > -0.5f * sc::sqrt_ss(sc::dot3(toPp, toPp));
        const bool committed = mem && attempt == 0;
        // the detour point must not sit inside another part of the same body either (the candidate holds only the
        // earliest-hit part): every part of every candidate body is tested
        if (ok && first.part && !committed) {
            const PartSet& set = parts[first.part_set];
            for (unsigned p = 0; p < set.count && ok; ++p) {
                const float dp[3] = {Pp[0] - set.c[p][0], Pp[1] - set.c[p][1], Pp[2] - set.c[p][2]};
                const float Cp = clearance(s.r, set.r[p]);
                if (sc::dot3(dp, dp) < Cp * Cp) ok = false;
            }
        }
        for (unsigned j = 1; j < n_cand && ok && !committed; ++j)
            if (!clears_body(s.x, Pp, cand[j], s.r, parts, part_sets)) ok = false;
        if (ok) {
            res.adjusted = true;
            res.side = side;
            res.dir[0] = ddir[0];
            res.dir[1] = ddir[1];
            res.dir[2] = ddir[2];
            res.vertical = tries[attempt].vertical;
            res.P[0] = Pp[0];
            res.P[1] = Pp[1];
            res.P[2] = Pp[2];
            remember_side(state, o.ptr, ddir, tries[attempt].vertical);
            break;
        }
    }
    if (!res.adjusted) {
        res.unresolved = true;
        state.unresolved = true;
        // Emergency ramp (section 3.4): no detour and the obstacle inside the braking distance.
        const float d_body = first.d;
        if (d_body > first.C && s.a > 1e-3f) {
            const float v_e = sc::sqrt_ss(s.a * (d_body - first.C));
            if (v_e < s.v) {
                res.emergency = true;
                res.v_emergency = sc::fmax_f(v_e, sc::kFloorMs);
                state.v_emergency = res.v_emergency;
            }
        }
        return res;
    }
    // The resolved set (section 3.6): a candidate is suppressed at site E only when its detour is flyable (the body
    // beyond the turn-in distance sqrt(C(C + 2R)) and beyond the stopping distance v^2/2a: pairs inside D_stop are
    // never suppressed) and the straight path to the detour point clears its whole body by C; station fallbacks never.
    const float R_turn = s.omega > 1e-4f ? s.v / s.omega : 1e6f;
    const float D_stop = s.a > 1e-3f ? s.v * s.v / (2.0f * s.a) : 1e9f;
    for (unsigned j = 0; j < n_cand; ++j) {
        const Obstacle& oj = snap.items[cand[j].index];
        if (oj.kind == std::uint8_t(Kind::station) && !cand[j].part) continue;
        const float d_turn = sc::sqrt_ss(cand[j].C * (cand[j].C + 2.0f * R_turn));
        if (!(cand[j].d > d_turn) || !(cand[j].d > D_stop)) continue;
        if (!clears_body(s.x, res.P, cand[j], s.r, parts, part_sets)) continue;
        if (state.n_resolved < kMaxResolved) state.resolved[state.n_resolved++] = oj.ptr;
    }
    return res;
}
// Site E (section 3.6): suppress the reactive warn for (A, B) when the state is A's and B is in its fresh resolved set.
inline bool suppressed(const ShipAvoid& a, std::uint32_t A, std::uint32_t B, std::uint32_t now_ms) noexcept {
    if (!a.obj || a.obj != A || now_ms - a.stamp_ms > kResolvedAgeMs) return false;
    for (unsigned i = 0; i < a.n_resolved; ++i)
        if (a.resolved[i] == B) return true;
    return false;
}
}
