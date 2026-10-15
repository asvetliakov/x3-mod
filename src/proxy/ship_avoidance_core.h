#pragma once
#include "ship_steering_core.h"

// The survivors of the amendment-16 lane board (ship_capital_lane_core.h, deleted with stage B on 2026-10-15: its board, verdicts
// and pass ran only under ship_steering_gates_traffic = 0): the hull width source and its retry, the yield cap that applies a
// controller decision at site B (gate-traffic-controller.md A.1) and the gate path guards of site E. The namespace keeps its name.
namespace x3m::ship_avoidance::lane {
namespace sc = x3m::ship_steering::core;
// The hull half-width's source (section 16.1 with the addendum: the LOD records of the hull parts, ship_avoidance_walk_core.h
// hull_width; or w = r: unread = no read made, fail = a guard failed).
enum class WidthSrc : std::uint8_t { unread = 0, lod, failed, count };
inline const char* width_src_name(WidthSrc s) noexcept {
    static const char* const names[unsigned(WidthSrc::count)] = {"unread", "lod", "fail"};
    return unsigned(s) < unsigned(WidthSrc::count) ? names[unsigned(s)] : "?";
}
// Site B (review F2): the pass's cap (the (b) follow cap only since amendment 17) is in force on the module's own writes, and it
// only ever lowers them. On the gate write (kShipFlagGate, both directions) it lowers v_ref when v_ref is a write (> 0); otherwise
// nothing. On the lowering clamp path
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

// Portable core of the predictive collision avoidance (docs/architecture/
// ship-steering-and-avoidance.md sections 3, 4 and 6; step 2). It sits in the
// steering handler's target seam: the order's point P in, the point P' the law
// steers to out, over a per-sector per-frame obstacle snapshot the DLL fills
// from the bucket lists (ship_steering.cpp) and the host fixture fills from
// its scenario. SSE2 scalar float only (the helpers of ship_steering_core.h),
// no allocation, no CRT math: it runs inside the leaf on the light envelope.
//
// Model (section 3.3, amendment 14): the ship moves along its velocity u = v f + slip
// (obj+0x60, the integrator's lag) for L metres; a ship obstacle moves at its
// actual velocity p+0x100 + obj+0x60; a conflict is a closest approach within
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
constexpr unsigned kSnapshotCapacity = 2048; // obstacles per sector snapshot (beyond it: pass-through, counted); the gate traffic
                                             // controller (gate-traffic-controller.md, stage A) raised it from 1024 (measured maximum 186)
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
constexpr float kRootSlack = 1.7321f; // sqrt(3): the engine's subtree radius is per axis (|offset|_inf + r, 0x00488170), so a
                                      // part may sit up to sqrt(3) x the root radius out in the Euclidean metric
constexpr float kAheadS = 1.0f;        // the detour point sits at least this flight time ahead along the heading: a point
                                       // abeam (the part just passed, the next part's CPA a few metres ahead) would ask
                                       // for a near-right-angle aim that a slow-ramping ship (k 0.05..0.2) overshoots
constexpr float kCommitFactor = 2.0f;  // a detour in progress (direction remembered, obstacle ahead) keeps its conflict until the
                                       // predicted miss exceeds 2 C (beyond both detour offsets): no chatter between the detour point
                                       // and the order's point before the detour is complete
constexpr std::uint32_t kResolvedAgeMs = 100; // suppression window at site E (two think intervals)
constexpr float kHorizonMaxS = 30.0f;  // amendment 11: the swept-sphere horizon is min(L / v, 30 s) of flight (Run155: a
                                       // stalled Boreas at 2 m/s saw conflicts 222 s out and never left its detour point)
constexpr float kDockMarginFactor = 1.2f, kDockMarginM = 50.0f; // amendment 18: E's clearance of every part sphere of the ship's own
                                                                 // container, 1.2 r_ship + 50 m (docking-approach.md item 1)
constexpr unsigned kDockSet = 0xffffu;
constexpr float kDockCapCos = 0.98480775f; // cos 10 deg: the order-4 horizon cap only while u points at the approach point
constexpr float kDockSlipLagS = 2.0f;     // the velocity's lag behind the nose (slip-aware-steering.md: the 2 s decay of obj+0x60) // Candidate.part_set of a carrier's part (Self.dock_set, not the snapshot's part store)
constexpr float kTieMissFrac = 0.25f;  // amendment 14: a moving obstacle's miss below this fraction of C takes its side from the
                                       // relative velocity (closest_approach), not from the noise of a near-centre miss

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
    std::uint8_t size;    // ships: 1 = capital-coded (0x0043fd50: the record code in kCapitalMask), 0 = any other code, 2 = the code
                          // could not be read (capital-gate-transit.md section 1: the engine's "large" list no longer decides)
    std::uint8_t pad;
    std::uint32_t id;        // ships: the engine's native object id [obj+0x08] (amendment 15: the membership memory's identity)
    std::uint32_t member_of; // ships: the effective leader: the live f+0x224 on 0x14/0x15, else the remembered one (update_members), 0 = none
};
// Obstacle.flags bits for ships (stations use bit 0 for the part model).
constexpr std::uint8_t kObstacleRemembered = 2; // member_of comes from the membership memory, not the live order
constexpr std::uint8_t kObstacleAutopilot = 4;  // [obj+0x40] & 0x80000 at the walk
struct Snapshot {
    std::uint32_t sector = 0, epoch = 0;
    unsigned count = 0, overflow = 0;
    // the arrival round item 6: the player's ship of the walk ([obj+0x40] & 0x2, SA_SetAsPlayerObject) and its position (m); 0 none
    std::uint32_t player = 0;
    float player_x[3] = {0, 0, 0};
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
    float x[3], f[3];      // m, unit forward (the detour plane and the side memory)
    float slip[3];         // m/s, world: the integrator's slip obj+0x60 (amendment 14): the swept path is along u = v f + slip
    float v, r, a, omega;  // m/s, m, m/s^2, rad/s (the slower of yaw/pitch)
    float alpha;           // rad/s^2, the angular acceleration of that axis (0 = ignore the ramp)
    float up[3];           // world up for the capital preference
    std::uint32_t ptr, leader, target; // self, f+0x224 when it is a leader/partner, the order's target object
    float leader_x[3];     // the leader's position (valid when leader != 0; diagnostics only since amendment 11)
    std::uint16_t order;
    bool capital;
    bool exclude_target;   // the order's target object is not an obstacle (orders 4/5/0x14/0x15/0x10/0x11/0x18)
    float L_leader;        // the leader's last lookahead (0 = none): the follower sees at least as far
    // The gate traffic controller (gate-traffic-controller.md D.1, ship_steering_gates_traffic): the member partners of this capital
    // whose pair the controller owns (a clear verdict, fresh), by object and native id; dropped from the sphere avoidance in this
    // direction. n_ctl = 0: no controller pair (amendment 17's rules).
    const std::uint32_t* ctl_ptr = nullptr;
    const std::uint32_t* ctl_id = nullptr;
    unsigned n_ctl = 0;
    // Amendment 18 (docking-approach.md items 2-4; ship_steering_dock): the order's container whose part model stays an obstacle (a
    // station's from the snapshot, a carrier's in dock_set); its root sphere is not one. 0 = today's rules (exclude_target decides).
    std::uint32_t dock_target = 0;
    const PartSet* dock_set = nullptr; // a carrier's hull parts (ship obstacles carry no part index); stations: the snapshot's model
    float dock_omega[3] = {0, 0, 0};   // the carrier's angular velocity (rad/s, world): its parts move at v + omega x (c - centre)
    bool dock_cap_P = false;           // order 4: the target's parts count only up to the order's point (the ship stops there)
    // Convoy-gate-transit.md section 12.3 (round B): for a lane member, the statics its gate's lateral profile owns (gc::LaneProfile::body):
    // no sphere detour against them. The first n_skip_enclose are fallback spheres enclosing the lane (left to the engine's warn: never
    // resolved); the rest, the bodies the profile's shift clears (every lane member: a joiner flies to the shifted line), enter the
    // resolved set as a detour's would (site E suppresses the engine's warn against them: the lane's shifted line is their resolution).
    const std::uint32_t* skip_static = nullptr;
    unsigned n_skip_static = 0, n_skip_enclose = 0;
};
// Section 12.3: whether a static obstacle is one the lane's profile owns for this ship.
__attribute__((always_inline)) inline bool lane_skipped(const Self& s, const Obstacle& o) noexcept {
    if (o.kind != std::uint8_t(Kind::station) && o.kind != std::uint8_t(Kind::asteroid)) return false;
    for (unsigned i = 0; i < s.n_skip_static; ++i)
        if (s.skip_static[i] == o.ptr) return true;
    return false;
}
// The controller's exclusion for one obstacle: a capital-coded ship among the clear member partners (pointer and id).
__attribute__((always_inline)) inline bool ctl_excluded(const Self& s, const Obstacle& o) noexcept {
    if (!s.n_ctl || o.kind != std::uint8_t(Kind::ship) || o.size != 1) return false;
    for (unsigned i = 0; i < s.n_ctl; ++i)
        if (s.ctl_ptr[i] == o.ptr && s.ctl_id[i] == o.id) return true;
    return false;
}
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
    std::uint32_t obstacle = 0; // the current detour's obstacle (0 = no detour on the last call; the trace row carries it)
    float P[3] = {0, 0, 0};     // lane fix round 2 item 1: that detour's point (valid with obstacle != 0; site E's lane obstacle row)
    bool unresolved = false;
    bool in_conflict = false; // a conflict was seen on the last call (episode counting: an edge is one episode)
};
// Why a call with a conflict ended without a detour (the summary counts each; per call and per episode).
enum class Unresolved : std::uint8_t {
    none = 0,
    inside_stop,     // the earliest body inside the stopping distance v^2/2a (the emergency ramp applies)
    inside_turn,     // the earliest body inside the turn-in distance sqrt(C(C + 2R)): no tangent exists
    blocked,         // every detour point tried sits inside another part or crosses another candidate's body
    behind,          // every detour point tried lies behind the ship or past the order's point
    degenerate,      // no detour axis (the heading, the body direction and up are parallel)
    count
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
    bool dock;             // amendment 18: a part of the ship's own container (dock_part_clearance; part_set kDockSet for a carrier)
};
struct Result {
    bool adjusted = false, unresolved = false, overflow = false;
    unsigned candidates = 0, conflicts = 0, tries = 0;
    float P[3] = {0, 0, 0};
    float t_cpa = 0, miss = 0, C = 0, L = 0;
    unsigned excluded_followers = 0; // this ship's own escort/follow partners skipped as obstacles (amendment 10)
    unsigned skipped_small = 0;      // small ships a capital left to their own avoidance (amendment 11)
    unsigned excluded_members = 0;   // formation members excluded through the membership memory alone (amendment 15)
    unsigned ctl_excluded = 0; // gate traffic controller: member pairs with a clear verdict dropped (the controller owns them)
    unsigned dock_kept = 0;    // amendment 18: the order's container kept as a part obstacle (0 or 1)
    unsigned lane_skipped = 0; // convoy-gate-transit.md section 12.3: statics the lane's lateral profile clears, dropped for a lane member
    std::uint32_t obstacle = 0;
    std::uint32_t obstacle_leader = 0; // the obstacle's f+0x224 and f+0x258 (ships), for the detour row (amendment 11)
    std::uint16_t obstacle_order = 0;
    std::uint8_t obstacle_kind = 0;
    std::int8_t side = 0;
    float dir[3] = {0, 0, 0}; // the detour direction (world, unit)
    bool vertical = false, emergency = false;
    std::uint8_t reason = 0;  // Unresolved
    float v_emergency = 0;
};

inline float clearance(float r_own, float r_obs) noexcept {
    return r_own + r_obs + sc::fmax_f(kClearanceMinM, 0.5f * r_own);
}
inline float dock_margin_m(float r_ship) noexcept {
    return kDockMarginFactor * r_ship + kDockMarginM;
}
// Amendment 18: a part of the ship's own container is kept at E's margin plus the usual minimum (the path the avoidance flies must
// pass E's row: docking-approach.md items 1-2), never below the plain clearance.
inline float dock_part_clearance(float r_own, float r_part) noexcept {
    return sc::fmax_f(clearance(r_own, r_part), r_part + dock_margin_m(r_own) + kClearanceMinM);
}
// Lookahead for a clearance C (section 3.3): the swept path length. `alpha` adds the distance flown while the
// rate ramps up to omega and back (2 omega / alpha seconds; the design's formula assumes the full rate at once,
// which the fixture showed leaves slow-ramping ships (TS, M6, capitals) no room to complete a detour). Amendment 14
// (slip-aware-steering.md section 3): plus the lag distance v tau, the detour starting 2 s later than the nose model assumed.
inline float lookahead(float v, float a, float omega, float r_own, float C, float alpha = 0.0f) noexcept {
    const float R = omega > 1e-4f ? v / omega : 1e6f;
    const float d_turn = sc::sqrt_ss(C * (C + 2.0f * R));
    const float d_stop = a > 1e-3f ? v * v / (2.0f * a) : 1e9f;
    const float ramp = (alpha > 1e-6f && omega > 1e-4f) ? v * (2.0f * omega / alpha) : 0.0f;
    float L = kReactionS * v + ramp + kRampMargin * sc::fmin_f(d_turn, d_stop) + sc::kSlipTau * v;
    if (L < C + r_own) L = C + r_own;
    if (L > kLookaheadMaxM) L = kLookaheadMaxM;
    return L;
}
// The slip-aware turn-in distance (amendment 14, section 3): the forward distance at which the flown path reaches a lateral
// offset C. The nose path under the angular ramp alpha from rest (lateral v alpha t^3 / 6, so t = (6 C / (v alpha))^(1/3))
// or the constant-rate arc sqrt(C (C + 2 R)), whichever is longer; plus the slip's share v tau (1 - e^(-t_nose / tau)):
// the velocity keeps its old direction while the nose turns. Fitted to the calibrated port within 10 % (design table 2).
inline float turn_in_slip(float v, float omega, float alpha, float C) noexcept {
    const float R = omega > 1e-4f ? v / omega : 1e6f;
    float d_nose = sc::sqrt_ss(C * (C + 2.0f * R));
    if (alpha > 1e-6f && v > 1e-3f) {
        const float d_ramp = v * sc::cbrt_f(6.0f * C / (v * alpha));
        if (d_ramp > d_nose) d_nose = d_ramp;
    }
    const float t_nose = v > 1e-3f ? d_nose / v : 0.0f;
    return d_nose + sc::kSlipTau * v * sc::one_minus_exp_neg(t_nose / sc::kSlipTau);
}
// The ship's velocity v f + slip (amendment 14): the swept path and the "behind" tests use it; the nose stays the detour plane.
inline float velocity(const Self& s, float u[3]) noexcept { // returns |u|
    for (int i = 0; i < 3; ++i) u[i] = s.v * s.f[i] + s.slip[i];
    return sc::sqrt_ss(sc::dot3(u, u));
}
// The swept-sphere time horizon for a lookahead L at speed v: L / v, at most kHorizonMaxS (amendment 11). A body already
// inside C is found at t = 0 whatever the horizon, so the cap only drops far, slow conflicts.
inline float horizon_s(float L, float v) noexcept {
    if (!(v > 1e-3f)) return kHorizonMaxS;
    const float t = L / v;
    return t < kHorizonMaxS ? t : kHorizonMaxS;
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
// Closest approach of our point x + u t (u = v f + slip: what the ship does if the nose does nothing, amendment 14) to a
// sphere centre c moving at w, over t in [0, T].
// Returns false when the miss distance is not below C; else fills t, miss, the centre at t and e.
inline bool closest_approach(const Self& s, const float c[3], const float w[3], float T, float C, float* t_out, float* miss,
                             float c_at[3], float e[3]) noexcept {
    float dx[3], dv[3];
    sub3(c, s.x, dx);
    for (int i = 0; i < 3; ++i) dv[i] = w[i] - (s.v * s.f[i] + s.slip[i]);
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
    // Amendment 14: a near-centre miss on a moving obstacle (below kTieMissFrac C) carries no usable direction once the slip
    // enters the prediction (a metre either way between two ships on a symmetric collision course), and the two ships'
    // lateral axes differ on a crossing: e's sign there picks "both pass in front" as often as not, which keeps the
    // collision (the calibrated fixture's M5 crossing, 0.2-5 m). The side then comes from the relative velocity, e = dv x up:
    // the obstacle computes -dv, so the pair always takes opposite sides of the relative motion and the miss grows.
    if (m < kTieMissFrac * C && sc::dot3(w, w) > 1.0f) {
        cross3(dv, s.up, e);
        if (norm3(e) == 0.0f) e[0] = e[1] = e[2] = 0.0f;
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
// (finite, every part sphere inside the bounding sphere up to the per-axis slack: |c - pos| + r <= kRootSlack x root_r,
// at most kMaxParts). Returns false when the layout does not validate.
inline bool build_parts(PartSet& set, std::uint32_t station, float root_r, const float pos[3], const float centres[][3], const float radii[],
                        unsigned n) noexcept {
    set.station = 0;
    set.count = 0;
    if (n == 0 || n > kMaxParts || !(root_r > 0.0f)) return false;
    for (unsigned i = 0; i < n; ++i) {
        if (!sc::finite_f(centres[i][0]) || !sc::finite_f(centres[i][1]) || !sc::finite_f(centres[i][2]) || !sc::finite_f(radii[i])) return false;
        if (!(radii[i] > 0.0f) || radii[i] > kRootSlack * root_r) return false;
        const float dc[3] = {centres[i][0] - pos[0], centres[i][1] - pos[1], centres[i][2] - pos[2]};
        if (sc::sqrt_ss(sc::dot3(dc, dc)) + radii[i] > kRootSlack * root_r + 0.5f) return false;
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
// Amendment 18: a carrier's part (kDockSet) is in dock_set; a part of the ship's own container keeps dock_part_clearance.
inline const PartSet* cand_set(const Candidate& c, const PartSet* parts, unsigned part_sets, const PartSet* dock_set) noexcept {
    if (!c.part) return nullptr;
    if (c.part_set == kDockSet) return dock_set;
    return c.part_set < part_sets ? &parts[c.part_set] : nullptr;
}
inline bool clears_body(const float x[3], const float P[3], const Candidate& c, float r_own, const PartSet* parts, unsigned part_sets,
                        const PartSet* dock_set = nullptr) noexcept {
    if (const PartSet* set = cand_set(c, parts, part_sets, dock_set)) {
        for (unsigned p = 0; p < set->count; ++p)
            if (segment_hits(x, P, set->c[p], c.dock ? dock_part_clearance(r_own, set->r[p]) : clearance(r_own, set->r[p]))) return false;
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
// A ship on escort/follow whose partner (f+0x224) is this ship: its leader never treats it as an obstacle (the follower
// excludes its leader likewise), so a formation does not detour against itself (Run154: 453 leader detours). Amendment 15
// (capital-gate-transit.md section 15.2): the partner is the effective leader `member_of`, the live f+0x224 on 0x14/0x15 or
// the membership memory's leader while the follower is in the engine's transient (0x12, or order 0 without a target).
inline bool own_follower(const Self& s, const Obstacle& o) noexcept {
    return o.kind == std::uint8_t(Kind::ship) && o.member_of && o.member_of == s.ptr;
}
inline bool wingmate(const Self& s, const Obstacle& o) noexcept {
    return o.kind == std::uint8_t(Kind::ship) && s.leader && o.member_of == s.leader;
}
// Amendment 11 as amended by capital-gate-transit.md section 1 (amendment 13): a capital does not avoid small ships; the
// small ship dodges it. A steered capital's obstacle set keeps stations, asteroids, gates, every capital-coded ship
// (0x0043fd50: TL, M0, M1, M2, TL_P, M7; pinned by the engine's own filter 0x0044d2f0, never ignored) and any ship with
// r_candidate >= r_own / 3; every other ship is skipped (`skipped_small`). The engine's "large" list (0x0043fd00, which
// keeps M6/M8/TM) no longer overrides the ratio: the engine still applies it on its own side, which is why site E acts
// (small_pair_verdict below). An unreadable code (size 2) uses the ratio alone.
constexpr float kSmallShipRatio = 1.0f / 3.0f;
inline bool small_for_capital(const Self& s, const Obstacle& o) noexcept {
    if (!s.capital || o.kind != std::uint8_t(Kind::ship)) return false;
    if (o.size == 1) return false; // capital-coded: always an obstacle
    return o.r < s.r * kSmallShipRatio;
}
// Site E's small-ship row (capital-gate-transit.md section 15.1, amendment 15): for a pair the capital ignores (A capital-coded
// and freshly decided on, B a class-7 ship with a readable non-capital record code and r_B < r_A / 3) the engine's warn is
// suppressed whatever B's order (0x12, 0, 0x1f, ...), except: B without the autopilot bit (the manually flown player ship, or
// any ship the integrator does not drive), B deleted/attached/docked, B's class masked (leave its pairs vanilla), and (scenario
// audit G4, main-session decision 2026-10-12) B on a leg the module does not avoid on: order 5 (dock or gate pass), 0x1e
// (undock), 0x20 (glide): there the pair must stay with the engine, or neither side avoids. Amendment 17 (section 17 item 4) withdraws
// that dock-leg exception under ship_steering_gates_capital (`dock_legs`): the small ship on its leg is the engine's to steer and its
// contact with a capital is vanilla's elastic bounce; the other exceptions stay.
inline bool small_pair(bool a_capital, float r_a, bool b_ship, bool b_capital_code, float r_b) noexcept {
    return a_capital && b_ship && !b_capital_code && sc::finite_f(r_a) && sc::finite_f(r_b) && r_b < r_a * kSmallShipRatio;
}
enum class SmallPair : std::uint8_t {
    suppress = 0,  // the engine's warn for (A, B) is dropped
    no_autopilot,  // [B+0x40] without 0x80000 (the player under manual control, or a ship the integrator does not drive)
    not_steerable, // B deleted, attached or docked
    class_masked,  // B's class is excluded by ship_steering_classes
    b_dock_leg,    // B on order 5, 0x1e or 0x20: the module does not avoid on those legs (audit G4; not under amendment 17)
    own_leader,    // B is A's own leader (live or remembered): the escort's warn against its leader is never suppressed
    count
};
constexpr std::uint32_t kSteeredFreshMs = 250; // A's last decision at most this old (the site-B clamp window), at least 4 frames
constexpr std::uint32_t kObjFlagAutopilot = 0x80000, kObjFlagDeleted = 0x8000000, kObjFlagAttached = 0x200;
constexpr std::uint16_t kOrderUndock = 0x1e, kOrderGlide = 0x20;
inline SmallPair small_pair_verdict(std::uint32_t b_flags, std::uint16_t b_order, std::uint32_t b_docked, bool b_class_masked,
                                    bool dock_legs = false) noexcept {
    if (!(b_flags & kObjFlagAutopilot)) return SmallPair::no_autopilot;
    if ((b_flags & (kObjFlagDeleted | kObjFlagAttached)) || b_docked) return SmallPair::not_steerable;
    if (b_class_masked) return SmallPair::class_masked;
    if (!dock_legs && (b_order == sc::kOrderDock || b_order == kOrderUndock || b_order == kOrderGlide)) return SmallPair::b_dock_leg;
    return SmallPair::suppress;
}
inline const char* small_pair_name(SmallPair v) noexcept {
    static const char* const names[unsigned(SmallPair::count)] = {"suppress", "no_autopilot", "not_steerable", "class", "b_dock_leg", "own_leader"};
    return unsigned(v) < unsigned(SmallPair::count) ? names[unsigned(v)] : "?";
}
// The decision-age window at site E (scenario audit G2, main-session decision): max(base, 4 x the current frame step). Under
// SETA the game-time frame step is 60-70 ms and the 0x12 leaf runs every 200 ms, so a fixed 250 ms window lapses on the
// re-plan cadence. The frame step is clamped to 250 ms (a load hitch must not stretch the window beyond 1 s). replan_ms > 0 adds
// replan + 2 frame steps (A's decision age at E reaches the 200 ms re-plan plus up to two frames after a jittery frame; review).
constexpr std::uint32_t kSteeredReplanMs = 200;
// B.3 (Run163): the frame's game-time step as the DLL reads it ([root+0x714]), clamped to 5 s. Amendment 15's reader returned 0 above
// 1 s, which dropped the window's scaling on exactly the long SETA frames; fresh_window_ms still caps its own term at 250 ms (a window
// of at most 1 s), the controller's age_bound uses the step itself (ctl::kStepClampMs, the same 5 s).
constexpr std::uint32_t kFrameStepMaxMs = 5000;
__attribute__((always_inline)) inline std::uint32_t frame_step_clamp(std::uint32_t raw_ms) noexcept {
    return raw_ms <= kFrameStepMaxMs ? raw_ms : kFrameStepMaxMs;
}
inline std::uint32_t fresh_window_ms(std::uint32_t base_ms, std::uint32_t frame_ms, std::uint32_t replan_ms = 0) noexcept {
    if (frame_ms > 250) frame_ms = 250;
    std::uint32_t w = 4 * frame_ms;
    if (replan_ms && replan_ms + 2 * frame_ms > w) w = replan_ms + 2 * frame_ms;
    return w > base_ms ? w : base_ms;
}
// ---- Amendment 15 (capital-gate-transit.md section 15.2): the formation membership memory ------------------------------
// A fixed table of 256 entries keyed by the follower pointer (probe window 8, no allocation), filled by the sector snapshot
// (update_members, after the walk): set or refreshed only while the follower is on 0x14/0x15 with a leader that is a ship of the
// same snapshot; kept without refresh while the follower is in the engine's transient (0x12, or order 0 without a target) or on
// any other order without a target; cleared at once by any other order with a target; dropped the same snapshot when the leader
// is missing from it, either object id [obj+0x08] changed, the roles reversed, either ship lacks the autopilot bit (the player
// took it over), or 20 s of game time passed since the last 0x14/0x15 confirmation. Membership alone never suppresses the
// engine's warn (site E's member row also needs the live transient order and the hulls apart).
constexpr unsigned kMemberCapacity = 256, kMemberProbe = 8, kMemberIndex = 2048;
constexpr std::uint32_t kMemberExpiryMs = 20000;
struct Member {
    std::uint32_t follower, leader, leader_id, follower_id, confirmed_ms;
};
struct MemberStats {
    unsigned set = 0, refresh = 0, replace = 0, expire = 0, leader_gone = 0, id_mismatch = 0, reversed = 0, player = 0,
             cleared = 0, full = 0;
};
inline unsigned member_hash(std::uint32_t p) noexcept {
    return ((p >> 4) * 0x9e3779b1u) >> 24; // 0..255
}
inline bool member_order(std::uint16_t order) noexcept {
    return order == sc::kOrderEscort || order == sc::kOrderFollow;
}
struct Members {
    Member e[kMemberCapacity] = {};
    std::uint16_t index[kMemberIndex] = {}; // scratch of update_members: a ship's snapshot item + 1, keyed on its pointer
    Member* find(std::uint32_t follower) noexcept {
        if (!follower) return nullptr;
        unsigned i = member_hash(follower);
        for (unsigned k = 0; k < kMemberProbe; ++k, i = (i + 1) & (kMemberCapacity - 1))
            if (e[i].follower == follower) return &e[i];
        return nullptr;
    }
    const Member* find(std::uint32_t follower) const noexcept { return const_cast<Members*>(this)->find(follower); }
    // A free or expired slot in the follower's probe window (an expired one counts as expire), nullptr when the window is full.
    Member* slot_for(std::uint32_t follower, std::uint32_t now, MemberStats& st) noexcept {
        unsigned i = member_hash(follower);
        Member* expired = nullptr;
        for (unsigned k = 0; k < kMemberProbe; ++k, i = (i + 1) & (kMemberCapacity - 1)) {
            if (!e[i].follower) return &e[i];
            if (!expired && now - e[i].confirmed_ms > kMemberExpiryMs) expired = &e[i];
        }
        if (expired) ++st.expire;
        return expired;
    }
    // The current membership of `follower` in `leader`'s formation (site E's member row, the trace rows): the entry exists, both
    // ids match the live objects and the last confirmation is at most kMemberExpiryMs old.
    const Member* current(std::uint32_t follower, std::uint32_t leader, std::uint32_t follower_id, std::uint32_t leader_id,
                          std::uint32_t now) const noexcept {
        const Member* m = find(follower);
        if (!m || m->leader != leader || m->follower_id != follower_id || m->leader_id != leader_id) return nullptr;
        return now - m->confirmed_ms <= kMemberExpiryMs ? m : nullptr;
    }
};
inline unsigned item_hash(std::uint32_t p) noexcept {
    return ((p >> 4) * 0x9e3779b1u) >> 21; // 0..2047
}
// The membership pass over a filled snapshot (the DLL's walk and the fixture's World call it): the effective leader of every
// ship item (member_of, kObstacleRemembered) and the table's updates. One index insert, one table probe and a few compares
// per ship; the index is cleared per call (4 KB).
inline void update_members(Snapshot& snap, Members& m, std::uint32_t now, MemberStats& st) noexcept {
    std::memset(m.index, 0, sizeof m.index);
    for (unsigned k = 0; k < snap.count; ++k) {
        Obstacle& o = snap.items[k];
        if (o.kind != std::uint8_t(Kind::ship) || !o.ptr) continue;
        o.member_of = member_order(o.order) ? o.leader : 0;
        o.flags = std::uint8_t(o.flags & ~kObstacleRemembered);
        unsigned i = item_hash(o.ptr);
        for (unsigned probe = 0; probe < kMemberIndex; ++probe, i = (i + 1) & (kMemberIndex - 1))
            if (!m.index[i]) {
                m.index[i] = std::uint16_t(k + 1);
                break;
            }
    }
    auto lookup = [&](std::uint32_t ptr) -> const Obstacle* {
        if (!ptr) return nullptr;
        unsigned i = item_hash(ptr);
        for (unsigned probe = 0; probe < kMemberIndex; ++probe, i = (i + 1) & (kMemberIndex - 1)) {
            const unsigned v = m.index[i];
            if (!v) return nullptr;
            if (snap.items[v - 1].ptr == ptr) return &snap.items[v - 1];
        }
        return nullptr;
    };
    auto drop = [](Member* e, unsigned& counter) {
        if (!e) return;
        *e = Member{};
        ++counter;
    };
    for (unsigned k = 0; k < snap.count; ++k) {
        Obstacle& F = snap.items[k];
        if (F.kind != std::uint8_t(Kind::ship) || !F.ptr) continue;
        Member* E = m.find(F.ptr);
        const bool f_ap = (F.flags & kObstacleAutopilot) != 0;
        if (member_order(F.order) && F.leader) { // rule 1: set or refresh
            const Obstacle* L = lookup(F.leader);
            if (!L) {
                drop(E, st.leader_gone);
                continue;
            }
            if (member_order(L->order) && L->leader == F.ptr) {
                drop(E, st.reversed);
                continue;
            }
            if (!f_ap || !(L->flags & kObstacleAutopilot)) {
                drop(E, st.player);
                continue;
            }
            if (E) {
                if (E->leader == L->ptr && E->leader_id == L->id && E->follower_id == F.id) ++st.refresh;
                else ++st.replace;
            } else {
                E = m.slot_for(F.ptr, now, st);
                if (!E) {
                    ++st.full;
                    continue;
                }
                ++st.set;
            }
            *E = Member{F.ptr, L->ptr, L->id, F.id, now};
            continue;
        }
        if (!E) continue;
        if (F.order != sc::kOrderAvoid && F.leader) { // rule 2: any other order with a target clears at once
            drop(E, st.cleared);
            continue;
        }
        // rule 3 (0x12, order 0 without a target, any other order without a target): keep without refresh while still valid
        const Obstacle* L = lookup(E->leader);
        if (!L) drop(E, st.leader_gone);
        else if (L->id != E->leader_id || F.id != E->follower_id) drop(E, st.id_mismatch);
        else if (member_order(L->order) && L->leader == F.ptr) drop(E, st.reversed);
        else if (!f_ap || !(L->flags & kObstacleAutopilot)) drop(E, st.player);
        else if (now - E->confirmed_ms > kMemberExpiryMs) drop(E, st.expire);
        else {
            F.member_of = E->leader;
            F.flags = std::uint8_t(F.flags | kObstacleRemembered);
        }
    }
}
// Site E's member row (section 15.1): a non-small B is suppressed for a capital A when B is a current member of A's formation,
// B is in the engine's transient (0x12, or order 0 without a target: the partner filter 0x0044d385.. no longer drops it) and the
// hulls do not touch (d > r_A + r_B).
enum class MemberPair : std::uint8_t { suppress = 0, not_member, not_transient, touching, count };
inline MemberPair member_pair_verdict(bool member_current, std::uint16_t b_order, std::uint32_t b_target, float d, float r_a, float r_b) noexcept {
    if (!member_current) return MemberPair::not_member;
    if (!(b_order == sc::kOrderAvoid || (b_order == sc::kOrderNone && !b_target))) return MemberPair::not_transient;
    if (!(d > r_a + r_b)) return MemberPair::touching;
    return MemberPair::suppress;
}
// ---- Amendment 18: the docking approach (docking-approach.md; ship_steering_dock) ---------------------------------------------------
// The own-container row at site E (item 1): A on order 4 to its container B (f+0x224: a station of class 5/6 or a carrier) with B's
// part model valid and current; E suppresses the engine's warn when A's swept path over the engine's reach (along u = v f + slip,
// relative to B's parts moving at B's velocity) AND A's forward ray over the same reach clear every part sphere by 1.2 r_ship + 50 m,
// and A is outside the final zone. The reach is the mesh test's |v| (one second of travel) + b, b = v^2 / 2a capped at 600 m
// (0x0044dd90, ship-navigation-ai.md section 11.2). Part spheres contain the part boxes [i]; the engine enlarges each box to
// 1.2 (e + r_ship).
constexpr float kDockBrakeCapM = 600.0f;   // the warn test's braking term cap (300000 units)
constexpr float kDockCarrierInner = 0.8f;  // a ship within 0.8 r_carrier of its carrier is left alone (the engine skips its warns on undock)
constexpr float kDockIdleNear = 5.0f;      // a 0x1d ship within 5 r_C of its container is "queued near it" (__LandInHangar's 5 size)
enum class DockE : std::uint8_t { none = 0, suppress, no_parts, path, ray, zone, count };
struct DockClear {
    float path_m = 1e30f, ray_m = 1e30f; // the smallest (centre distance - r_part) over the parts, m
    float reach_m = 0.0f;
    unsigned part_path = 0, part_ray = 0; // the parts that set them
};
// v_close: the braking term's speed (a station: |v|; a ship container: dock_closing_speed, never below |v|).
inline float dock_reach_m(float v, float a, float v_close = -1.0f) noexcept {
    const float vc = v_close >= 0.0f ? v_close : v;
    const float b = a > 1e-3f ? sc::fmin_f(vc * vc / (2.0f * a), kDockBrakeCapM) : kDockBrakeCapM;
    return v + b;
}
// The warn test's closing speed against a ship B (section 3.1): |v_A| cos(theta) plus B's speed toward A when A heads at B within
// cos 0.8 (or away beyond -0.8), theta between A's nose and A -> B; never below |v_A| (the station form), so the reach is at least both.
inline float dock_closing_speed(const float x[3], const float f[3], float v, const float centre[3], const float w[3]) noexcept {
    float d[3] = {centre[0] - x[0], centre[1] - x[1], centre[2] - x[2]};
    const float n = sc::sqrt_ss(sc::dot3(d, d));
    if (!(n > 1e-3f)) return v;
    for (int i = 0; i < 3; ++i) d[i] /= n;
    const float c = sc::dot3(f, d);
    const float toward = -sc::dot3(w, d); // B's speed toward A
    const float vc = v * c + ((c > 0.8f || c < -0.8f) ? toward : 0.0f);
    const float a = vc < 0.0f ? -vc : vc;
    return a > v ? a : v;
}
// The distance from c to the segment x + d t, t in [0, 1].
inline float dock_seg_dist(const float x[3], const float d[3], float dd, const float c[3]) noexcept {
    float dc[3];
    sub3(c, x, dc);
    float t = dd > 1e-6f ? sc::dot3(dc, d) / dd : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float q[3];
    for (int i = 0; i < 3; ++i) q[i] = dc[i] - d[i] * t;
    return sc::sqrt_ss(sc::dot3(q, q));
}
// Both clearances of A (x, unit forward f, velocity u = v f + slip, speed v, acceleration a) against B's parts (moving at w, m/s).
// The path: x + u t for t in [0, reach / |u|] relative to the parts (x + (u - w) t against the current centres); the ray: x + f s for
// s in [0, reach] against the current centres (the engine's instantaneous slab test).
// A part's velocity on a turning container: the centre's w plus omega x (c_p - centre) (omega null or zero: w).
inline void dock_part_velocity(const float w[3], const float* omega, const float* centre, const float c[3], float out[3]) noexcept {
    out[0] = w[0];
    out[1] = w[1];
    out[2] = w[2];
    if (!omega || !centre || (omega[0] == 0.0f && omega[1] == 0.0f && omega[2] == 0.0f)) return;
    const float r[3] = {c[0] - centre[0], c[1] - centre[1], c[2] - centre[2]};
    float t[3];
    cross3(omega, r, t);
    for (int i = 0; i < 3; ++i) out[i] += t[i];
}
inline DockClear dock_clearance(const PartSet& set, const float w[3], const float x[3], const float f[3], const float u[3], float v, float a,
                                const float* omega = nullptr, const float* centre = nullptr, float v_close = -1.0f) noexcept {
    DockClear out;
    out.reach_m = dock_reach_m(v, a, v_close);
    const float u_len = sc::sqrt_ss(sc::dot3(u, u));
    const float T = u_len > 1e-3f ? out.reach_m / u_len : 0.0f;
    float dp[3], dr[3];
    for (int i = 0; i < 3; ++i) {
        dp[i] = (u[i] - w[i]) * T;
        dr[i] = f[i] * out.reach_m;
    }
    float ddp = sc::dot3(dp, dp);
    const float ddr = sc::dot3(dr, dr);
    const bool turning = omega && centre && (omega[0] != 0.0f || omega[1] != 0.0f || omega[2] != 0.0f);
    for (unsigned p = 0; p < set.count; ++p) {
        if (turning) { // each part's own relative motion
            float wp[3];
            dock_part_velocity(w, omega, centre, set.c[p], wp);
            for (int i = 0; i < 3; ++i) dp[i] = (u[i] - wp[i]) * T;
            ddp = sc::dot3(dp, dp);
        }
        const float gp = dock_seg_dist(x, dp, ddp, set.c[p]) - set.r[p];
        const float gr = dock_seg_dist(x, dr, ddr, set.c[p]) - set.r[p];
        if (gp < out.path_m) {
            out.path_m = gp;
            out.part_path = p;
        }
        if (gr < out.ray_m) {
            out.ray_m = gr;
            out.part_ray = p;
        }
    }
    return out;
}
// The row's verdict from the reads: no model -> the warn stays live; the final zone (the engine owns the ship); then the path and the
// ray against the margin. A non-finite clearance never suppresses.
inline DockE dock_verdict(bool parts_ok, bool in_final_zone, const DockClear& c, float r_ship) noexcept {
    if (!parts_ok) return DockE::no_parts;
    if (in_final_zone) return DockE::zone;
    const float m = dock_margin_m(r_ship);
    if (!(c.path_m >= m)) return DockE::path;
    if (!(c.ray_m >= m)) return DockE::ray;
    return DockE::suppress;
}
inline const char* dock_e_name(DockE v) noexcept {
    static const char* const names[unsigned(DockE::count)] = {"-", "suppress_dock", "pass_dock_parts", "pass_dock_path", "pass_dock_ray",
                                                              "pass_dock_zone"};
    return unsigned(v) < unsigned(DockE::count) ? names[unsigned(v)] : "?";
}
// The order's container for the docking rows (items 1-3): order 4 to a station (class 5/6) or a carrier (class 7), 0x1d to a station
// (the queue's __Idle keeps f+0x224 = the station from SA_SetDestination [i]; a stale ship target on 0x1d is not a container).
// A ship counts only when it is dock-capable by the engine's own test, [obj+0x40] & 0x100000 ("has a hangar or dock": the slew loop's
// carrier rule 0x0045951f..0x00459558 and SA_FlyObjectBeforeContainer's port path, ship-navigation-ai.md sections 5 and 11.6); any
// other ship target on order 4 keeps today's rules.
constexpr std::uint32_t kObjFlagDock = 0x100000;
inline bool dock_container(std::uint16_t order, std::uint16_t target_class, std::uint32_t target_flags = kObjFlagDock) noexcept {
    if (order == sc::kOrderFlyBefore)
        return target_class == 5 || target_class == 6 || (target_class == 7 && (target_flags & kObjFlagDock) != 0);
    if (order == sc::kOrderIdle) return target_class == 5 || target_class == 6;
    return false;
}
// ---- Site E's whole decision (amendment 15 with the scenario audit's G1-G4), pure over the reads the DLL makes ---------------
enum class EVerdict : std::uint8_t {
    none = 0, suppress_small, suppress_member, suppress_gate, suppress_gate_small, suppress_resolved, pass_no_autopilot,
    pass_not_steerable, pass_class, pass_b_dock_leg, pass_own_leader, pass_not_fresh, pass_unreadable, pass_not_member,
    pass_not_transient, pass_touching, pass_a_avoid, pass_not_resolved, pass_not_ship,
    suppress_ctl, pass_ctl, suppress_arrival_gate, // the gate traffic controller (D.1, D.4; ship_steering_gates_traffic)
    suppress_dock, pass_dock_parts, pass_dock_path, pass_dock_ray, pass_dock_zone, // amendment 18 (ship_steering_dock)
    suppress_lane_obst, // lane fix round 2 item 1: a lane member's path clears a non-member obstacle (ctl::lane_obst_why)
    suppress_gate_ring, // the arrival round item 4: an AI ship's path clears its gate's solid (ctl::gate_ring_why), any order
    pass_player, // review of the arrival round item 2: B is the player's ship ([B+0x40] & 2): no suppression row applies (the engine's warn)
    count
};
inline const char* e_verdict_name(EVerdict v) noexcept {
    static const char* const names[unsigned(EVerdict::count)] = {
        "-", "suppress_small", "suppress_member", "suppress_gate", "suppress_gate_small", "suppress_resolved", "pass_no_autopilot",
        "pass_not_steerable", "pass_class", "pass_b_dock_leg", "pass_own_leader", "pass_not_fresh", "pass_unreadable", "pass_not_member",
        "pass_not_transient", "pass_touching", "pass_a_avoid", "pass_not_resolved", "pass_not_ship",
        "suppress_ctl", "pass_ctl", "suppress_arrival_gate", "suppress_dock", "pass_dock_parts", "pass_dock_path", "pass_dock_ray",
        "pass_dock_zone", "suppress_lane_obst", "suppress_gate_ring", "pass_player"};
    return unsigned(v) < unsigned(EVerdict::count) ? names[unsigned(v)] : "?";
}
inline bool e_suppresses(EVerdict v) noexcept {
    return v == EVerdict::suppress_small || v == EVerdict::suppress_member || v == EVerdict::suppress_gate ||
           v == EVerdict::suppress_gate_small || v == EVerdict::suppress_resolved ||
           v == EVerdict::suppress_ctl || v == EVerdict::suppress_arrival_gate || v == EVerdict::suppress_dock || v == EVerdict::suppress_lane_obst ||
           v == EVerdict::suppress_gate_ring;
}
inline EVerdict dock_e_verdict(DockE v) noexcept {
    switch (v) {
    case DockE::suppress: return EVerdict::suppress_dock;
    case DockE::no_parts: return EVerdict::pass_dock_parts;
    case DockE::path: return EVerdict::pass_dock_path;
    case DockE::ray: return EVerdict::pass_dock_ray;
    case DockE::zone: return EVerdict::pass_dock_zone;
    default: return EVerdict::none;
    }
}
constexpr std::uint32_t kHandoverMaxMs = 120000; // the capital gate row's hand-over bound after P2 (the through-slot wait is 0-60 s)
// B.4 review: the any-age small-ship row's horizon on A's last decision (the formation memory's expiry)
constexpr std::uint32_t kSmallAnyAgeMaxMs = kMemberExpiryMs;
struct SiteE {
    // A (class 7 checked by the caller), its table entry and the frame step
    std::uint16_t a_order = 0;
    std::uint32_t a_target = 0;      // f+0x224
    bool a_in_table = false;
    std::uint32_t a_age_ms = 0;      // now - A's last decision
    std::uint32_t frame_ms = 0;      // the frame's game-time step ([root+0x714])
    bool a_capital = false;          // A's record code is capital (0x0043fd50)
    bool codes_readable = true;      // the type table, both type words (class-7 B) read
    float r_a = 0;
    // B = a gate (class 0x12) and A's gate state
    bool b_gate = false;
    bool gates_live = false, capital_policy = false; // ship_steering_gates live, ship_steering_gates_capital on
    bool gate_state = false;         // A's gate state is A's, its gate is B and the approach validated (A matched)
    bool gate_capital = false;       // the state is a capital's
    bool gate_join = false;          // the capital's join mode
    bool gate_handover = false;      // P2 kept the speed (cleared at order 5 / any other handled order)
    std::uint32_t p2_age_ms = 0xffffffffu;
    std::uint32_t lead_age_ms = 0xffffffffu; // a non-capital's last order-4 call with the lead-in inside its window
    // amendment 16 (sections 16.3, 16.4); the defaults reproduce amendment 15
    bool gate_fallback = false;      // the capital's aim-at-A fallback (pred_none or past_x)
    bool gate_window_ok = true;      // s >= -6 r_gate along F from X at the last decision (mode `out` passes)
    bool gate_p2_seen = false;       // P2 on this approach, kept or not (the hand-over of section 16.3)
    bool gate_path_ok = true;        // dist(G, segment[x_A, P_A]) >= r_gate + r_A + 0.25 r_gate
    std::uint32_t small_gap_ms = 0;  // Run161 item 5 (ship_steering_gates_traffic; 0 = off): the small-ship gate row also on order 0 while
                                     // the order-4 decision (or the lead-in) is at most this old (the hand-over gap)
    bool gate_small_guard = false;   // a validated small approach decided on within the window with dist(G, segment[x, A]) >=
                                     // r_gate + r_ship + 0.1 r_gate (the G1 extension; ship_steering_gates)
    // B = a ship (class 7)
    bool b_ship = false;
    bool b_readable = false;         // B's record code < 16
    bool b_capital_code = false;
    float r_b = 0;
    std::uint32_t b_flags = 0, b_docked = 0, b_target = 0;
    std::uint16_t b_order = 0;
    bool b_class_masked = false;
    bool small_dock_legs = false;    // amendment 17 (ship_steering_gates_capital): the small-ship row on B's dock legs 5/0x1e/0x20 too
    bool small_any_age = false;      // B.4 (ship_steering_gates_capital): the small-ship row needs no fresh decision of A (A in the table
                                     // or an arrival; the script's order-0 gap: capital-gate-transit.md "Site E branch order (B.4)")
    bool a_id_ok = true;             // B.4 review: A's table entry was stamped for A's native id [obj+0x08] (a recycled pointer is not A)
    bool b_is_a_leader = false;      // B is A's live (0x14/0x15 target) or remembered leader
    bool b_member_current = false;   // B is a current remembered member of A's formation (Members::current)
    float d = -1.0f;                 // the centre distance, m (< 0 unreadable: never apart)
    bool avoidance_on = false;
    bool b_resolved = false;         // B in A's resolved set (ShipAvoid) within max(kResolvedAgeMs, 4 frame steps)
    // The gate traffic controller (gate-traffic-controller.md D.1 and D.4; ship_steering_gates_traffic). The defaults (key off)
    // reproduce amendment 17 exactly.
    bool arrival_gate = false;       // B is A's arrival gate and the arrival-gate row holds (ctl::arrival_gate_row)
    bool ctl_pair = false;           // A and B are live members of one gate board: the controller owns the pair
    bool ctl_certified = false;      // ... and ctl::e_certified holds (clear, <= 250 ms, both forward rows, neither on 0x12)
    bool a_arrival = false;          // A is a capital arrival member (< 60 s): the small-ship row takes it as fresh (its path is the
                                     // engine's 0x21 / 0x1e line)
    bool ctl_gate = false;           // B.2 item 3: A is a stage-B member in funnel / handover / dock / launch against its own gate B, under
                                     // the path guard (ctl::sb_gate_row; any order: 4, 0, 0x1f, 5)
    // Amendment 18 (ship_steering_dock): A on order 4 and B its container (dock_container), the row's verdict from the DLL's reads
    // (dock_verdict); none = key off or not A's container (today's rules)
    DockE dock = DockE::none;
    // Lane fix round 2 item 1 (ship_steering_gates_lane): A is a lane member whose path as site A flies it clears B (a station, an
    // asteroid or a ship that is not its board partner) by the sweep margin (ctl::lane_obst_why: its own freshness bounds, never the
    // player's ship); false = today's rules
    bool lane_obst = false;
    // The arrival round item 4 (ship_steering_gates_lane): B is a gate and A an AI ship (never the player's, never manual) whose predicted
    // path with its hull clears the gate's solid (ctl::gate_ring_why: no solid without a collision tree), the member row not deciding
    bool gate_ring = false;
    // Review of the arrival round item 2: B is the player's ship (class 7, [B+0x40] & 2, SA_SetAsPlayerObject): every suppression row is
    // skipped and the engine's warn stands (the resolved set, the small-ship and member rows included)
    bool b_player = false;
};
// The capital small-ship row (section 15.1, amendment 17 item 4) for a small B (small_pair holds): the exceptions, else suppress_small.
__attribute__((always_inline)) inline EVerdict small_row_verdict(const SiteE& in) noexcept {
    if (in.b_is_a_leader) return EVerdict::pass_own_leader;
    switch (small_pair_verdict(in.b_flags, in.b_order, in.b_docked, in.b_class_masked, in.small_dock_legs)) {
    case SmallPair::suppress: return EVerdict::suppress_small;
    case SmallPair::no_autopilot: return EVerdict::pass_no_autopilot;
    case SmallPair::not_steerable: return EVerdict::pass_not_steerable;
    case SmallPair::class_masked: return EVerdict::pass_class;
    case SmallPair::b_dock_leg: return EVerdict::pass_b_dock_leg;
    default: return EVerdict::pass_own_leader;
    }
}
inline EVerdict site_e_verdict(const SiteE& in) noexcept {
    if (in.b_player) return EVerdict::pass_player; // (review of the arrival round item 2: never the player's ship as B)
    // the gate traffic controller first (its pairs need no steering decision of A: arrivals and hand-overs are on engine orders)
    if (in.b_gate && in.arrival_gate) return EVerdict::suppress_arrival_gate;
    if (in.b_gate && in.ctl_gate) return EVerdict::suppress_gate; // B.2 item 3: the stage-B member against its own gate
    if (in.b_gate && in.gate_ring) return EVerdict::suppress_gate_ring; // the arrival round item 4 (its own freshness: the live reads)
    if (in.b_ship && in.ctl_pair) return in.ctl_certified ? EVerdict::suppress_ctl : EVerdict::pass_ctl;
    if (in.lane_obst && !in.b_gate) return EVerdict::suppress_lane_obst; // (its own freshness: the order-0 gaps have no decision of A)
    if (!in.a_in_table && !in.a_arrival) return EVerdict::pass_not_fresh;
    // B.4: under the capital key the small-ship row comes before every freshness requirement (a capital ignores any ship under r/3
    // whatever its order, its own decision's age included); a fresh A gets the verdict the row below gives (dock and lane rows excluded).
    // The table entry must be A's (its native id) and its decision within the formation memory's horizon (kSmallAnyAgeMaxMs; Run164's
    // gaps reached 2.9 s, the fixture's 8.5 s); else the freshness check below decides (pass_not_fresh for a stale A)
    if (in.small_any_age && (in.a_arrival || (in.a_in_table && in.a_id_ok && in.a_age_ms <= kSmallAnyAgeMaxMs)) && in.b_ship && in.a_capital &&
        in.dock == DockE::none && in.codes_readable && in.b_readable && small_pair(true, in.r_a, true, in.b_capital_code, in.r_b))
        return small_row_verdict(in);
    const std::uint32_t fresh = fresh_window_ms(kSteeredFreshMs, in.frame_ms, kSteeredReplanMs);
    // the gate rows: B is A's own target gate
    if (in.b_gate && in.gates_live && in.gate_state) {
        if (in.gate_capital) {
            // amendment 16 (section 16.3): any validated approach on order 4 (join, fallback; inside the window extended past X) and
            // the hand-over after P2 kept or not, under the path guard
            const bool approach = in.a_order == sc::kOrderFlyBefore && in.a_age_ms <= fresh && in.gate_window_ok &&
                                  (in.gate_join || in.gate_fallback);
            const bool hand = (in.a_order == sc::kOrderNone || in.a_order == sc::kOrderRotate) && (in.gate_handover || in.gate_p2_seen) &&
                              in.p2_age_ms <= kHandoverMaxMs;
            if (in.capital_policy && (approach || hand) && in.gate_path_ok) return EVerdict::suppress_gate;
        } else if (in.a_order == sc::kOrderFlyBefore && (in.lead_age_ms <= fresh || (in.gate_small_guard && in.a_age_ms <= fresh)))
            return EVerdict::suppress_gate_small; // G1, and its section-16.4 extension to the whole order-4 leg under the guard
        else if (in.small_gap_ms && in.a_order == sc::kOrderNone &&
                 (in.lead_age_ms <= in.small_gap_ms || (in.gate_small_guard && in.a_age_ms <= in.small_gap_ms)))
            return EVerdict::suppress_gate_small; // Run161 item 5 (traffic key): order 0 in the hand-over gap, the gate state fresh
    }
    if (in.a_age_ms > fresh && !in.a_arrival) return EVerdict::pass_not_fresh;
    const bool a_fresh = in.a_in_table && in.a_age_ms <= fresh; // false only for an arrival accepted by the small-ship row (D.4)
    // amendment 18: B is A's own container on order 4 (a station or a carrier): the row decides, nothing below applies
    if (in.dock != DockE::none && a_fresh && in.a_order == sc::kOrderFlyBefore) return dock_e_verdict(in.dock);
    EVerdict v = EVerdict::none;
    if (in.b_ship && in.a_capital) {
        if (!in.codes_readable) return EVerdict::pass_unreadable;
        if (in.b_readable) {
            if (small_pair(true, in.r_a, true, in.b_capital_code, in.r_b)) return small_row_verdict(in);
            if (!a_fresh) return EVerdict::pass_not_fresh;
            if (in.a_order != sc::kOrderAvoid && in.avoidance_on) {
                const MemberPair mv = member_pair_verdict(in.b_member_current, in.b_order, in.b_target, in.d, in.r_a, in.r_b);
                if (mv == MemberPair::suppress) return EVerdict::suppress_member;
                v = mv == MemberPair::not_transient ? EVerdict::pass_not_transient : mv == MemberPair::touching ? EVerdict::pass_touching
                                                                                                                 : EVerdict::pass_not_member;
            }
        }
    }
    if (!a_fresh) return EVerdict::pass_not_fresh;
    // the first branch: B in A's fresh resolved set, A not on the engine's own avoid order
    if (in.a_order == sc::kOrderAvoid) return v == EVerdict::none ? EVerdict::pass_a_avoid : v;
    if (in.b_resolved) return EVerdict::suppress_resolved;
    return v == EVerdict::none ? EVerdict::pass_not_resolved : v;
}
// Whether the obstacle item is excluded for this ship (section 3.2 and 4). The formation is excluded in every direction
// (amendment 11): the follower never avoids its leader, the leader never avoids its followers, and wingmates of one
// leader never avoid each other, wherever they are (the amendment-10 envelope condition is gone: Run155's Boreas leader
// still detoured 453 times against its own formation outside it). Amendment 15: followers and wingmates by member_of (live
// or remembered), so escort capitals on 0x12/0 stay excluded (Run157: 195 leader detours against them).
inline bool excluded_base(const Self& s, const Obstacle& o) noexcept {
    if (o.ptr == s.ptr) return true;
    if (s.leader && o.ptr == s.leader) return true;
    if (s.exclude_target && s.target && o.ptr == s.target) return true;
    if (wingmate(s, o)) return true;     // a wingmate of the same leader
    if (own_follower(s, o)) return true; // our own followers, wherever they are (amendment 10: the leader detoured against them)
    if (small_for_capital(s, o)) return true; // amendment 11 / 13: the ratio rule, the small ship dodges
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
    for (unsigned i = s.n_skip_enclose; i < s.n_skip_static; ++i) // section 12.3: the bodies the lane's profile clears (resolved by it)
        if (state.n_resolved < kMaxResolved) state.resolved[state.n_resolved++] = s.skip_static[i];
    state.v_emergency = 0.0f;
    state.obstacle = 0;
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
        // amendment 18 (items 2-4): the order's container with a valid part model stays an obstacle by its parts (its root sphere
        // only selects); without a model the target rule of today stands
        const PartSet* dock = nullptr;
        if (s.dock_target && o.ptr == s.dock_target) {
            if (o.kind == std::uint8_t(Kind::station)) {
                if ((o.flags & 1) && o.parts < part_sets && parts[o.parts].station == o.ptr && parts[o.parts].count) dock = &parts[o.parts];
            } else if (o.kind == std::uint8_t(Kind::ship) && s.dock_set && s.dock_set->station == o.ptr && s.dock_set->count)
                dock = s.dock_set;
            if (dock) ++res.dock_kept;
        }
        if (!dock && excluded_base(s, o)) {
            if (own_follower(s, o)) ++res.excluded_followers;
            else if (small_for_capital(s, o)) ++res.skipped_small;
            if ((o.flags & kObstacleRemembered) && (own_follower(s, o) || wingmate(s, o))) ++res.excluded_members;
            continue;
        }
        if (s.n_ctl && ctl_excluded(s, o)) { // the gate traffic controller owns the pair (D.1)
            ++res.ctl_excluded;
            continue;
        }
        if (s.n_skip_static && lane_skipped(s, o)) { // the lane's lateral profile owns the body (section 12.3)
            ++res.lane_skipped;
            continue;
        }
        float dx[3];
        sub3(o.x, s.x, dx);
        const float d = sc::sqrt_ss(sc::dot3(dx, dx));
        const float commit = remembered(state, o.ptr) ? kCommitFactor : 1.0f; // the detour in progress
        Candidate c;
        c.index = k;
        c.part = false;
        c.part_set = kStationCapacity;
        c.d = d;
        c.dock = dock != nullptr;
        if (o.kind == std::uint8_t(Kind::station) || dock) {
            // broad phase on the sphere (a plain reach test: the sphere's centre may already be abeam while parts
            // are still ahead), narrow phase on the parts (or the fallback body)
            const float C_sel = clearance(s.r, o.r);
            const float L_sel = sc::fmax_f(lookahead(s.v, s.a, s.omega, s.r, C_sel, s.alpha), s.L_leader);
            if (!dock && d > L_sel + C_sel) continue;
            float T_cap = 1e30f; // amendment 18: the time to the order's point on order 4 (the ship stops at the approach point)
            float T_turn = -1.0f; // amendment 18: the line to the order's point clears the body: a part conflict later than the turn onto it is not one
            if (dock) {
                // the container's own parts: the reach widened by the dock margin, by the body's slack beyond its sphere and, for a
                // carrier, by its travel over the horizon
                const float C_dock = dock_part_clearance(s.r, o.r);
                const float L_dock = sc::fmax_f(lookahead(s.v, s.a, s.omega, s.r, C_dock, s.alpha), s.L_leader);
                const float w_len = sc::sqrt_ss(sc::dot3(o.v, o.v));
                if (d > L_dock + kRootSlack * C_dock + w_len * horizon_s(L_dock, s.v)) continue;
                if (s.dock_cap_P) {
                    float toP[3], uu[3];
                    sub3(P, s.x, toP);
                    const float u_len = velocity(s, uu);
                    const float tl0 = sc::sqrt_ss(sc::dot3(toP, toP));
                    // the cap only while the velocity points at the point (within 10 deg): otherwise the swept path passes beside it
                    if (u_len > 0.1f && tl0 > 1.0f && sc::dot3(uu, toP) >= kDockCapCos * u_len * tl0) T_cap = tl0 / u_len;
                    // the engine flies at the point: when the straight line clears the whole body (the sphere about the centre that
                    // encloses every part, which no turn of a carrier changes) by the dock clearance, a conflict on the current velocity
                    // that comes later than the turn onto that line (plus the reaction time) is the turn's business
                    float Rb = 0.0f;
                    for (unsigned p = 0; p < dock->count; ++p) {
                        const float dp[3] = {dock->c[p][0] - o.x[0], dock->c[p][1] - o.x[1], dock->c[p][2] - o.x[2]};
                        Rb = sc::fmax_f(Rb, sc::sqrt_ss(sc::dot3(dp, dp)) + dock->r[p]);
                    }
                    const bool line_clear = !segment_hits(s.x, P, o.x, dock_part_clearance(s.r, Rb));
                    const float tl = tl0;
                    // a stationary container only (a moving or turning one changes the line while the ship turns onto it); the turn
                    // time adds the velocity's lag behind the nose
                    const bool still = sc::dot3(o.v, o.v) == 0.0f && s.dock_omega[0] == 0.0f && s.dock_omega[1] == 0.0f && s.dock_omega[2] == 0.0f;
                    if (still && line_clear && tl > 1.0f && s.omega > 1e-3f) {
                        float c = sc::dot3(toP, s.f) / tl;
                        c = c > 1.0f ? 1.0f : c < -1.0f ? -1.0f : c;
                        T_turn = sc::acos_deg(c) * 0.0174532925f / s.omega + (s.alpha > 1e-4f ? s.omega / s.alpha : 0.0f) + kReactionS + kDockSlipLagS;
                    }
                }
            }
            float t, m, cat[3], e[3];
            const PartSet* set = dock;
            if (!set && (o.flags & 1) && o.parts < part_sets && parts[o.parts].station == o.ptr && parts[o.parts].count) set = &parts[o.parts];
            if (set) {
                c.part_set = set == s.dock_set && o.kind == std::uint8_t(Kind::ship) ? kDockSet : o.parts;
                float best_t = 1e30f;
                for (unsigned p = 0; p < set->count; ++p) {
                    const float Cp = dock ? dock_part_clearance(s.r, set->r[p]) : clearance(s.r, set->r[p]);
                    const float Lp = sc::fmax_f(lookahead(s.v, s.a, s.omega, s.r, Cp, s.alpha), s.L_leader);
                    float tp, mp, catp[3], ep[3];
                    const float Tp = dock ? sc::fmin_f(horizon_s(Lp, s.v), T_cap) : horizon_s(Lp, s.v);
                    float vp[3];
                    dock_part_velocity(o.v, dock ? s.dock_omega : nullptr, o.x, set->c[p], vp);
                    if (!closest_approach(s, set->c[p], vp, Tp, Cp * commit, &tp, &mp, catp, ep)) continue;
                    if (T_turn >= 0.0f && tp > T_turn) continue;
                    if (tp < best_t) {
                        best_t = tp;
                        c.t_cpa = tp;
                        c.miss = mp;
                        c.C = Cp;
                        c.L = Tp * s.v; // the swept length actually tested
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
                const float Tf = horizon_s(Lf, s.v);
                if (!closest_approach(s, o.x, o.v, Tf, Cf * commit, &t, &m, cat, e)) continue;
                c.t_cpa = t;
                c.miss = m;
                c.C = Cf;
                c.L = Tf * s.v;
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
            const float T = horizon_s(L, s.v);
            float t, m, cat[3], e[3];
            if (!closest_approach(s, o.x, o.v, T, C * commit, &t, &m, cat, e)) continue;
            c.t_cpa = t;
            c.miss = m;
            c.C = C;
            c.L = T * s.v;
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
    float u_hat[3];
    const float u_len = velocity(s, u_hat);
    if (u_len > 1e-3f) {
        for (int i = 0; i < 3; ++i) u_hat[i] /= u_len;
    } else
        for (int i = 0; i < 3; ++i) u_hat[i] = s.f[i];
    res.obstacle = o.ptr;
    res.obstacle_kind = o.kind;
    res.obstacle_leader = o.leader;
    res.obstacle_order = o.order;
    res.t_cpa = first.t_cpa;
    res.miss = first.miss;
    res.C = first.C;
    res.L = first.L;
    // The detour: the predicted body position plus 1.1 C along the chosen lateral axis. The plane of least turn
    // is spanned by f and the direction to the body's current position (its predicted position lies on our own
    // path when the course is a collision course and would leave the plane undefined).
    // Amendment 18: a part of the ship's own container is detoured as the whole body, the sphere about the container's centre that
    // encloses every part, with the dock clearance (the conflict stays the part's): a tangent per part aims into the pockets between
    // parts (scenario N: the far-side port). The side is the approach point's (from the body's centre at the closest approach).
    float det_c[3] = {first.x_cpa[0], first.x_cpa[1], first.x_cpa[2]}, det_e[3] = {first.e[0], first.e[1], first.e[2]};
    float det_px[3] = {first.px[0], first.px[1], first.px[2]};
    float det_C = first.C;
    if (first.dock) {
        float Rb = o.r;
        if (const PartSet* bs = cand_set(first, parts, part_sets, s.dock_set)) {
            Rb = 0.0f;
            for (unsigned p = 0; p < bs->count; ++p) {
                const float dp[3] = {bs->c[p][0] - o.x[0], bs->c[p][1] - o.x[1], bs->c[p][2] - o.x[2]};
                Rb = sc::fmax_f(Rb, sc::sqrt_ss(sc::dot3(dp, dp)) + bs->r[p]);
            }
        }
        det_C = dock_part_clearance(s.r, Rb);
        float dx[3], dv[3];
        sub3(o.x, s.x, dx);
        for (int i = 0; i < 3; ++i) dv[i] = o.v[i] - (s.v * s.f[i] + s.slip[i]);
        const float vv = sc::dot3(dv, dv);
        float tb = vv > 1e-6f ? -sc::dot3(dx, dv) / vv : 0.0f;
        const float Tb = horizon_s(sc::fmax_f(lookahead(s.v, s.a, s.omega, s.r, det_C, s.alpha), s.L_leader), s.v);
        if (tb < 0.0f) tb = 0.0f;
        if (tb > Tb) tb = Tb;
        for (int i = 0; i < 3; ++i) {
            det_c[i] = o.x[i] + o.v[i] * tb;
            det_e[i] = -(dx[i] + dv[i] * tb);
            det_px[i] = o.x[i];
        }
        // the natural side is the order's point's (the note: around the parts toward the approach point), unless the point sits on
        // the line through the centre; then the path's own side
        float toP[3];
        sub3(P, det_c, toP);
        const float ref = sc::dot3(toP, toP);
        if (ref > 1.0f) {
            float side[3];
            float n_e[3] = {det_e[0], det_e[1], det_e[2]};
            const float e_len = norm3(n_e);
            for (int i = 0; i < 3; ++i) side[i] = toP[i];
            if (norm3(side) > 0.0f && (e_len < 1e-3f || sc::dot3(side, n_e) > -0.999f))
                for (int i = 0; i < 3; ++i) det_e[i] = side[i];
        }
        // heading at the centre the plane of the nose and the centre is undefined and the detour axis would fall to the vertical: then
        // (only then) the plane through the order's point when it lies off the nose line
        {
            float tc[3], n1[3];
            sub3(det_px, s.x, tc);
            cross3(s.f, tc, n1);
            const float tcl = sc::dot3(tc, tc);
            if (sc::dot3(n1, n1) < 0.0025f * tcl) {
                float toPx[3], n2[3];
                sub3(P, s.x, toPx);
                cross3(s.f, toPx, n2);
                if (sc::dot3(n2, n2) > 0.0025f * sc::dot3(toPx, toPx))
                    for (int i = 0; i < 3; ++i) det_px[i] = P[i];
            }
        }
        if (norm3(det_e) < 1e-3f) { // dead centre: the lateral side perpendicular to the nose in the up plane
            cross3(s.f, s.up, det_e);
            if (norm3(det_e) == 0.0f) {
                det_e[0] = 1.0f;
                det_e[1] = det_e[2] = 0.0f;
            }
        }
    }
    float dir[3];
    sub3(det_px, s.x, dir);
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
        const float along = sc::dot3(det_e, axis);
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
    Unresolved why = Unresolved::degenerate;
    for (unsigned attempt = 0; attempt < n_tries; ++attempt) {
        ++res.tries;
        float axis[3];
        if (!lateral_axis(s, dir, tries[attempt].vertical, axis)) continue;
        const std::int8_t side = tries[attempt].side;
        const float factor = o.kind == std::uint8_t(Kind::ship) ? kDetourFactorMoving : kDetourFactor;
        float Pp[3], ddir[3];
        for (int i = 0; i < 3; ++i) {
            ddir[i] = axis[i] * float(side);
            Pp[i] = det_c[i] + ddir[i] * det_C * factor;
        }
        float toP[3], toPp[3];
        sub3(P, s.x, toP);
        sub3(Pp, s.x, toPp);
        {
            // keep the point at least kAheadS of flight ahead along the velocity (amendment 14: along u_hat, not the nose),
            // and at least the slip-aware turn-in distance for the point's lateral offset (the lateral offset unchanged): the
            // aim angle stays bounded as the ship draws level with the body, and the point is one the flown path, which
            // keeps its old direction for ~tau while the nose turns, can reach. The moved point must still be reached
            // without crossing the body it was built for (every part of it); otherwise the un-pushed point stands (the
            // review's case: the push into a body behind the CPA)
            const float along = sc::dot3(toPp, u_hat);
            // bounded: |u| at most 3 v (the DLL refuses a slip beyond 2 v_max) and the push at most the lookahead's cap
            float ahead = kAheadS * sc::fmin_f(u_len, 3.0f * s.v);
            const float d_turn = turn_in_slip(s.v, s.omega, s.alpha, det_C * factor);
            if (d_turn > ahead) ahead = d_turn;
            if (ahead > kLookaheadMaxM) ahead = kLookaheadMaxM;
            if (along < ahead) {
                float moved[3];
                for (int i = 0; i < 3; ++i) moved[i] = Pp[i] + u_hat[i] * (ahead - along);
                if (clears_body(s.x, moved, first, s.r, parts, part_sets, s.dock_set))
                    for (int i = 0; i < 3; ++i) {
                        Pp[i] = moved[i];
                        toPp[i] = Pp[i] - s.x[i];
                    }
            }
        }
        // not behind the ship relative to P, not more than 120 degrees off the velocity (amendment 14; an obstacle abeam
        // inside C gets a detour point abeam, away from it), not through another candidate's sphere (a committed detour
        // keeps its direction: the other candidate gets its turn when it becomes the earliest)
        bool ok = sc::dot3(toP, toPp) > 0.0f && sc::dot3(toPp, u_hat) > -0.5f * sc::sqrt_ss(sc::dot3(toPp, toPp));
        if (!ok) why = Unresolved::behind;
        const bool committed = mem && attempt == 0;
        // the detour point must not sit inside another part of the same body either (the candidate holds only the
        // earliest-hit part): every part of every candidate body is tested
        const PartSet* first_set = ok && first.part && !committed ? cand_set(first, parts, part_sets, s.dock_set) : nullptr;
        if (first_set) {
            const PartSet& set = *first_set;
            for (unsigned p = 0; p < set.count && ok; ++p) {
                const float dp[3] = {Pp[0] - set.c[p][0], Pp[1] - set.c[p][1], Pp[2] - set.c[p][2]};
                const float Cp = first.dock ? dock_part_clearance(s.r, set.r[p]) : clearance(s.r, set.r[p]);
                if (sc::dot3(dp, dp) < Cp * Cp) {
                    ok = false;
                    why = Unresolved::blocked;
                }
            }
        }
        for (unsigned j = 1; j < n_cand && ok && !committed; ++j)
            if (!clears_body(s.x, Pp, cand[j], s.r, parts, part_sets, s.dock_set)) {
                ok = false;
                why = Unresolved::blocked;
            }
        if (ok) {
            res.adjusted = true;
            state.obstacle = o.ptr;
            state.P[0] = Pp[0];
            state.P[1] = Pp[1];
            state.P[2] = Pp[2];
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
        // the geometric reasons rank above the try failures: inside the stopping or the turn-in distance no tangent exists
        {
            const float R_t = s.omega > 1e-4f ? s.v / s.omega : 1e6f;
            const float D_st = s.a > 1e-3f ? s.v * s.v / (2.0f * s.a) : 1e9f;
            if (!(first.d > D_st))
                why = Unresolved::inside_stop;
            else if (!(first.d > sc::sqrt_ss(first.C * (first.C + 2.0f * R_t))))
                why = Unresolved::inside_turn;
        }
        res.reason = std::uint8_t(why);
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
    // never suppressed), the velocity's swept path already clears the body by C (amendment 14: the candidates are found
    // on u with the commit factor, so only a detour in progress whose velocity has swung clear, C <= miss < 2 C,
    // qualifies; a conflict the velocity still carries the ship into stays with the engine's warn, which is itself on the
    // nose ray) and the straight path to the detour point clears its whole body by C; station fallbacks never.
    const float R_turn = s.omega > 1e-4f ? s.v / s.omega : 1e6f;
    const float D_stop = s.a > 1e-3f ? s.v * s.v / (2.0f * s.a) : 1e9f;
    for (unsigned j = 0; j < n_cand; ++j) {
        const Obstacle& oj = snap.items[cand[j].index];
        if (oj.kind == std::uint8_t(Kind::station) && !cand[j].part) continue;
        if (!(cand[j].miss >= cand[j].C)) continue;
        const float d_turn = sc::sqrt_ss(cand[j].C * (cand[j].C + 2.0f * R_turn));
        if (!(cand[j].d > d_turn) || !(cand[j].d > D_stop)) continue;
        if (!clears_body(s.x, res.P, cand[j], s.r, parts, part_sets, s.dock_set)) continue;
        if (state.n_resolved < kMaxResolved) state.resolved[state.n_resolved++] = oj.ptr;
    }
    return res;
}
// Site E (section 3.6): suppress the reactive warn for (A, B) when the state is A's and B is in its fresh resolved set.
// The age window is kResolvedAgeMs, or max(that, 4 frame steps) from the DLL (scenario audit G2: under SETA a 60-70 ms game step
// leaves a fixed 100 ms window covering about one frame).
inline bool suppressed(const ShipAvoid& a, std::uint32_t A, std::uint32_t B, std::uint32_t now_ms, std::uint32_t age_ms = kResolvedAgeMs) noexcept {
    if (!a.obj || a.obj != A || now_ms - a.stamp_ms > age_ms) return false;
    for (unsigned i = 0; i < a.n_resolved; ++i)
        if (a.resolved[i] == B) return true;
    return false;
}
}
