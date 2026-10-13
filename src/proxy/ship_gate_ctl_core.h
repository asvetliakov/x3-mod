#pragma once
#include <cstdint>
#include "ship_steering_core.h"
// The gate traffic controller, stage A (docs/architecture/gate-traffic-controller.md D.1, D.4, D.7, D.8 and A.1; engine facts
// ship-navigation-ai.md section 10.10). Portable and pure over the state the DLL reads at its sites (ship_steering.cpp) and the
// host fixture fills from its scenario; SSE2 scalar float only, no allocation, no lock, no CRT math.
//
// One board per gate (kGates, the lane board's successor under ship_steering_gates_traffic): up to kMembers members, each a
// capital that is committed to the gate (the lane board's commit test), a capital escort of a committed leader, or a capital that
// arrived through the gate less than 60 s ago. Each member carries its last live state and one rollout over the next 30 s on the
// predictor's plant (sc::plant_step for an order-4 approach, the engine's kinematics for the orders the engine moves), 61 samples at
// 0.5 s from 0.25 s steps. The hull is a capsule: the segment c +- (L - w) f with radius w, L = max(r, l). A pair is swept sample by
// sample (broad: |c_A - c_B| >= L_A + L_B + m_i; narrow: segment distance >= w_A + w_B + m_i; m_i = 50 m + k t_i +
// |u_A - u_B| dt / 2); the verdict (clear, or handed back) sits in a 24 x 24 matrix per gate with its stamp. A conflict makes the
// lower-priority member yield by speed (0.6 v, 25 m/s, the 10 m/s floor) when it is on a steered order; the first candidate whose
// rollout clears every pair is taken (site B's cap); otherwise the pair is handed back (site E passes, the sphere avoidance applies).
// Stage B (funnel, pass point, sequencer, petals, the lateral shift and the go-around) is not here.
namespace x3m::ship_avoidance::ctl {
namespace sc = x3m::ship_steering::core;

constexpr unsigned kGates = 4, kMembers = 24, kSamples = 61;
constexpr float kStepS = 0.25f, kSampleS = 0.5f; // the plant's step and the stored sample interval (2 steps per sample)
constexpr std::uint32_t kSampleMs = 500;
constexpr std::uint32_t kArrivalWindowMs = 60000; // an arrival is a member for 60 s after its launch (D.1)
constexpr std::uint32_t kEvictKeepMs = 30000;     // an evicted member's sequence number is kept 30 s (D.1)
constexpr std::uint32_t kCertMaxAgeMs = 250;      // site E: the pair's clear verdict at most 250 ms old (D.1)
constexpr std::uint32_t kDecisionMaxAgeMs = 1000; // ... and both members decided within 1 s (a starved decision loses its certificate)
constexpr std::uint32_t kRolloutMaxAgeMs = 1000;  // a partner rollout older than 1 s than the sweep's own: no certificate (fail closed)
constexpr unsigned kMinOverlapSamples = 4;        // a sweep needs at least this many paired samples (and the warn reach in time)
constexpr std::uint32_t kExcludeMaxAgeMs = 500;   // the avoidance drops a member pair while its clear verdict is this fresh
constexpr std::uint32_t kStateMaxAgeMs = 1000;    // a member whose live state is older takes part in no pair (no verdict)
constexpr std::uint32_t kStateMs = 100;           // the live state is re-read at site B at most every 100 ms
constexpr std::uint32_t kDecideMs = 200;          // one decision per member every 200 ms at most (the 4 Hz cadence with margin)
constexpr std::uint32_t kCapMaxAgeMs = 1000;      // a yield cap lapses 1 s after the decision that set it
constexpr std::uint32_t kCommitFreshMs = 1000;    // a committed member on order 4/5 needs a commit refresh this recent
constexpr std::uint32_t kHandoverMaxMs = 120000;  // the hand-over bound after P2 (the lane board's)
constexpr float kMarginM = 50.0f;                 // m_i's constant
constexpr float kDriftMs = 10.0f;                 // k (m/s): the rollout drift allowance (gate-traffic-controller.md A.1: 10 m/s, decided
                                                  // 2026-10-13 over the 5 m/s placeholder; measured drift p95 436 m at 5 s, 2,230 m at 30 s)
constexpr float kYieldFrac = 0.6f, kYieldMidMs = 25.0f, kYieldFloorMs = 10.0f; // the speed candidates (D.1, stage A)
constexpr unsigned kMaxYieldsPerDecision = 2;     // yield resolutions tried per decision (the rest: handed back until the next)
// The frame's bound (D.8): at most kDecisionsPerFrame decisions and kRolloutsPerFrame rollouts per frame (a decision needs 1 rollout,
// up to kMaxRolloutsPerDecision with its yields): measured host worst 2 conflict decisions or 6 plain ones per frame.
constexpr unsigned kMaxRolloutsPerDecision = 1 + kMaxYieldsPerDecision * 3;
constexpr unsigned kDecisionsPerFrame = 6, kRolloutsPerFrame = 2 * kMaxRolloutsPerDecision;
// D.4: the arrival placement and the arrival-gate row.
constexpr float kArrivalOffsetR = 0.6f;           // a capital's exit point P += 0.6 r_gate U
constexpr float kTerranHoleR = 0.663f, kTerranMarginM = 50.0f; // installed Terran rows 21..24: h = 0.663 r (section 10.9)
constexpr float kSideSpawnR = 0.87f, kSideSpawnUpR = 0.2f, kSideSpawnMaxWR = 0.68f; // a small ship's side spawn (+-0.87 r, +0.2 r)
constexpr std::uint32_t kPlaneArrivalMs = 6000;   // a capital arrival is at the plane for 6 s after its launch
constexpr std::uint32_t kPlaneTriggerMs = 3000;   // a departing capital: on order 8, or within 3 s of its order-5 trigger (P4)
constexpr float kExitSpeedLargeMs = 120.0f, kExitSpeedSmallMs = 240.0f; // order 0x1e: 60000 / 120000 units/s
constexpr float kWarpInK = 1.665f / sc::kUnitsPerMetre; // order 0x21: p = f+0x240 + 1.665 u^2 F (u ms left, units) -> m per ms^2

enum class Role : std::uint8_t { none = 0, committed, escort, arrival, evicted, count };
inline const char* role_name(Role r) noexcept {
    static const char* const n[unsigned(Role::count)] = {"none", "committed", "escort", "arrival", "evicted"};
    return unsigned(r) < unsigned(Role::count) ? n[unsigned(r)] : "?";
}
enum class Plan : std::uint8_t { drift = 0, line, dock, brake, hold, warp_in, exit, launch, follow, count };
inline const char* plan_name(Plan p) noexcept {
    static const char* const n[unsigned(Plan::count)] = {"drift", "line", "dock", "brake", "hold", "warp_in", "exit", "launch", "follow"};
    return unsigned(p) < unsigned(Plan::count) ? n[unsigned(p)] : "?";
}
enum class PairV : std::uint8_t { none = 0, clear, unresolved, count };
inline const char* pair_name(PairV v) noexcept {
    static const char* const n[unsigned(PairV::count)] = {"none", "clear", "unresolved"};
    return unsigned(v) < unsigned(PairV::count) ? n[unsigned(v)] : "?";
}
// Why a member left the board or changed role (the ship_gate_ctl row).
enum class Why : std::uint8_t { enter = 0, commit, handover, escort, arrival, evict, order, target, expire, identity, leader, window, reenter, count };
inline const char* why_name(Why w) noexcept {
    static const char* const n[unsigned(Why::count)] = {"enter", "commit", "handover", "escort", "arrival", "evict", "order", "target",
                                                        "expire", "identity", "leader", "window", "reenter"};
    return unsigned(w) < unsigned(Why::count) ? n[unsigned(w)] : "?";
}

struct Sample {
    float c[3]; // centre, m (world)
    float f[3]; // the nose (unit)
};
// The live state of a member as read at site B (or the hook that made it a member).
struct State {
    std::uint32_t ms = 0;     // game time of the read (0 = none)
    std::uint16_t order = 0;  // f+0x258
    std::uint8_t f_ok = 0;    // the forward row is a unit vector (|f|^2 in [0.9, 1.1])
    std::uint8_t pad = 0;
    std::uint32_t target = 0; // f+0x224
    float x[3] = {0, 0, 0};
    float rows[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; // the flight basis (row 2 = the nose)
    float w[3] = {0, 0, 0};   // body rates, rad/s (yaw, pitch, roll in the engine's signs)
    float u[3] = {0, 0, 0};   // velocity v f + slip, m/s
    float v = 0;              // speed along the nose, m/s
    float v_des = 0;          // desired speed after site B's write, m/s
    float point[3] = {0, 0, 0}; // f+0x240, m
    std::int32_t left_ms = 0;   // order 0x21: f+0x214 - now
};
struct Member {
    std::uint32_t ship = 0, id = 0;
    Role role = Role::none;
    std::uint8_t large = 1;    // the engine's "large" set (0x1e speed and end test)
    std::uint8_t line_ok = 0;  // X, h and the plant parameters are set (an order-4 commit on a validated approach)
    std::uint8_t n = 0;        // rollout samples valid
    std::uint32_t seq = 0;     // the sequence number (by entry; an evicted member keeps it 30 s)
    std::uint32_t enter_ms = 0, commit_ms = 0, handover_ms = 0, launch_ms = 0, evict_ms = 0;
    std::uint32_t leader = 0;  // an escort's leader
    float r = 0, w = 0, L = 0; // m: the radius, the lateral half-width (w = r when unread) and the capsule half-length max(r, l)
    float X[3] = {0, 0, 0}, h[3] = {0, 0, 1}; // the order-4 line (crossing point and direction)
    sc::JoinParams params;     // W, alpha, v_max, a_lin, r_gate for the plant
    float v_plan = -1.0f;      // the module's own speed for this ship before any yield cap (m/s, < 0 unknown)
    std::uint32_t v_plan_ms = 0;
    State st;
    std::uint32_t roll_ms = 0;  // the rollout's t0 (= the state's ms)
    std::uint32_t decided_ms = 0;
    float v_roll_max = 0;       // the largest speed over the rollout (the time-offset margin)
    float cap = -1.0f;          // the yield cap (m/s, < 0 none)
    std::uint32_t cap_ms = 0;
    Sample s[kSamples];
};
struct Gate {
    std::uint32_t gate = 0, stamp_ms = 0;
    std::uint16_t type = 0xffff; // [gate+0x4a]: the TGates row (no collision mesh: 0..3, 5..14; Terran ring: 21..24)
    float r_gate = 0;
    float G[3] = {0, 0, 0}, R[3] = {1, 0, 0}, U[3] = {0, 1, 0}, F[3] = {0, 0, 1};
    unsigned n = 0;              // members in use (highest index + 1)
    std::uint32_t next_seq = 1;
    Member m[kMembers];
    std::uint8_t v[kMembers][kMembers] = {};  // PairV
    std::uint8_t tc[kMembers][kMembers] = {}; // the first conflict sample of the last sweep (255 none)
    std::uint32_t vms[kMembers][kMembers] = {};
    std::uint8_t logged[kMembers][kMembers] = {}; // the last PairV a ship_gate_ctl_pair row carried
    std::uint32_t logged_ms[kMembers][kMembers] = {};
    // the plane occupancy the small-ship placement reads (D.4): the last capital launch here, the last departing capital trigger
    std::uint32_t cap_arrival_ms = 0, cap_depart_ms = 0;
    float cap_arrival_w = 0, cap_depart_w = 0;
};
struct Board {
    Gate g[kGates];
    unsigned full_gates = 0, full_members = 0;
};

// ---- the membership bitmap: one bit per hashed object pointer, so site B and E probe a ship in two loads ------------------------
struct Index {
    std::uint32_t bits[128] = {}; // 4096 bits
    __attribute__((always_inline)) static unsigned slot(std::uint32_t ptr) noexcept { return ((ptr >> 3) * 2654435761u) >> 20; }
    __attribute__((always_inline)) bool maybe(std::uint32_t ptr) const noexcept {
        const unsigned k = slot(ptr);
        return (bits[k >> 5] >> (k & 31)) & 1u;
    }
    void set(std::uint32_t ptr) noexcept {
        const unsigned k = slot(ptr);
        bits[k >> 5] |= 1u << (k & 31);
    }
};
inline void rebuild(Index& ix, const Board& b) noexcept {
    for (std::uint32_t& w : ix.bits) w = 0;
    for (const Gate& g : b.g)
        if (g.gate)
            for (unsigned i = 0; i < g.n; ++i)
                if (g.m[i].ship && g.m[i].role != Role::none) ix.set(g.m[i].ship);
}

// ---- geometry ------------------------------------------------------------------------------------------------------------------
__attribute__((always_inline)) inline float clamp01(float t) noexcept { return t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t; }
// Squared distance between the segments p1-q1 and p2-q2 (Ericson, Real-Time Collision Detection 5.1.9).
__attribute__((always_inline)) inline float seg_seg_d2(const float p1[3], const float q1[3], const float p2[3], const float q2[3]) noexcept {
    const float d1[3] = {q1[0] - p1[0], q1[1] - p1[1], q1[2] - p1[2]};
    const float d2[3] = {q2[0] - p2[0], q2[1] - p2[1], q2[2] - p2[2]};
    const float r[3] = {p1[0] - p2[0], p1[1] - p2[1], p1[2] - p2[2]};
    const float a = sc::dot3(d1, d1), e = sc::dot3(d2, d2), f = sc::dot3(d2, r);
    constexpr float eps = 1e-6f;
    float s = 0.0f, t = 0.0f;
    if (a <= eps && e <= eps) return sc::dot3(r, r);
    if (a <= eps) {
        t = clamp01(f / e);
    } else {
        const float c = sc::dot3(d1, r);
        if (e <= eps) {
            s = clamp01(-c / a);
        } else {
            const float b = sc::dot3(d1, d2), denom = a * e - b * b;
            s = denom > 0.0f ? clamp01((b * f - c * e) / denom) : 0.0f; // parallel: any s, take 0
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = clamp01(-c / a);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = clamp01((b - c) / a);
            }
        }
    }
    float d[3];
    for (unsigned i = 0; i < 3; ++i) d[i] = (p1[i] + d1[i] * s) - (p2[i] + d2[i] * t);
    return sc::dot3(d, d);
}
// The same for two capsule axes c +- h f with unit f and h > 0 (Ericson's terms with d1 = 2 h_a f_a, d2 = 2 h_b f_b: a = 4 h_a^2,
// e = 4 h_b^2, b = 4 h_a h_b u, c = 2 h_a (f_a . r), f = 2 h_b (f_b . r), u = f_a . f_b): one division per call; inv2ha = 1 / (2 h_a),
// inv2hb = 1 / (2 h_b) per pair.
__attribute__((always_inline)) inline float capsule_d2(const float ca[3], const float fa[3], float ha, float inv2ha, const float cb[3],
                                                       const float fb[3], float hb, float inv2hb) noexcept {
    const float r[3] = {ca[0] - ha * fa[0] - (cb[0] - hb * fb[0]), ca[1] - ha * fa[1] - (cb[1] - hb * fb[1]), ca[2] - ha * fa[2] - (cb[2] - hb * fb[2])};
    const float u = fa[0] * fb[0] + fa[1] * fb[1] + fa[2] * fb[2];
    const float ra = fa[0] * r[0] + fa[1] * r[1] + fa[2] * r[2], rb = fb[0] * r[0] + fb[1] * r[1] + fb[2] * r[2];
    const float den = 1.0f - u * u;
    float s = den > 1e-6f ? clamp01((u * rb - ra) * inv2ha / den) : 0.0f; // parallel: any s, take 0 (Ericson)
    float t = (2.0f * ha * u * s + rb) * inv2hb;
    if (t < 0.0f) {
        t = 0.0f;
        s = clamp01(-ra * inv2ha);
    } else if (t > 1.0f) {
        t = 1.0f;
        s = clamp01((2.0f * hb * u - ra) * inv2ha);
    }
    const float ka = ha * (2.0f * s - 1.0f), kb = hb * (2.0f * t - 1.0f);
    const float d[3] = {ca[0] + ka * fa[0] - cb[0] - kb * fb[0], ca[1] + ka * fa[1] - cb[1] - kb * fb[1], ca[2] + ka * fa[2] - cb[2] - kb * fb[2]};
    return d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
}
// The capsule distance (surface to surface, m; < 0 = overlap) of two hulls at a sample: segments c +- (L - w) f, radii w.
inline float capsule_gap(const float ca[3], const float fa[3], float La, float wa, const float cb[3], const float fb[3], float Lb, float wb) noexcept {
    const float ha = La > wa ? La - wa : 0.0f, hb = Lb > wb ? Lb - wb : 0.0f;
    float p1[3], q1[3], p2[3], q2[3];
    for (unsigned i = 0; i < 3; ++i) {
        p1[i] = ca[i] - ha * fa[i];
        q1[i] = ca[i] + ha * fa[i];
        p2[i] = cb[i] - hb * fb[i];
        q2[i] = cb[i] + hb * fb[i];
    }
    return sc::sqrt_ss(seg_seg_d2(p1, q1, p2, q2)) - wa - wb;
}

// ---- membership --------------------------------------------------------------------------------------------------------------
__attribute__((always_inline)) inline bool pair_role(Role r) noexcept {
    return r == Role::committed || r == Role::escort || r == Role::arrival;
}
// A member takes part in pairs: a pair role and a live state at most kStateMaxAgeMs old.
__attribute__((always_inline)) inline bool live(const Member& m, std::uint32_t now) noexcept {
    return m.ship && pair_role(m.role) && m.st.ms && now - m.st.ms <= kStateMaxAgeMs;
}
inline const Gate* gate_of(const Board& b, std::uint32_t gate) noexcept {
    if (!gate) return nullptr;
    for (const Gate& g : b.g)
        if (g.gate == gate) return &g;
    return nullptr;
}
inline Gate* gate_of(Board& b, std::uint32_t gate) noexcept { return const_cast<Gate*>(gate_of(static_cast<const Board&>(b), gate)); }
inline int find(const Gate& g, std::uint32_t ship) noexcept {
    for (unsigned i = 0; i < g.n; ++i)
        if (g.m[i].ship == ship) return int(i);
    return -1;
}
// The board and member index of a ship on any board (nullptr / -1 when none).
// A ship is on one board at most (enter() removes it from every other); an evicted entry (kept 30 s for its sequence number) is
// returned only when no pair-role entry exists, a none entry never.
inline Gate* locate(Board& b, std::uint32_t ship, int* index) noexcept {
    Gate* evicted = nullptr;
    int ev_i = -1;
    for (Gate& g : b.g) {
        if (!g.gate) continue;
        const int i = find(g, ship);
        if (i < 0 || g.m[i].role == Role::none) continue;
        if (g.m[i].role == Role::evicted) {
            if (!evicted) {
                evicted = &g;
                ev_i = i;
            }
            continue;
        }
        *index = i;
        return &g;
    }
    *index = ev_i;
    return evicted;
}
// A slot is free when empty, or a non-pair role whose keep window ran out, or a member whose state stopped (no read for the
// arrival window: the ship left the sector or was destroyed without our seeing it).
__attribute__((always_inline)) inline bool slot_dead(const Member& m, std::uint32_t now) noexcept {
    if (!m.ship || m.role == Role::none) return true;
    if (m.role == Role::evicted) return now - m.evict_ms > kEvictKeepMs;
    const std::uint32_t last = m.st.ms ? m.st.ms : m.enter_ms;
    return now - last > kArrivalWindowMs;
}
inline void clear_pairs(Gate& g, unsigned i) noexcept {
    for (unsigned j = 0; j < kMembers; ++j) {
        g.v[i][j] = g.v[j][i] = 0;
        g.tc[i][j] = g.tc[j][i] = 255;
        g.vms[i][j] = g.vms[j][i] = 0;
        g.logged[i][j] = g.logged[j][i] = 0;
        g.logged_ms[i][j] = g.logged_ms[j][i] = 0;
    }
}
// The gate's board (a new gate takes an empty slot or one without a live member).
inline Gate* acquire_gate(Board& b, std::uint32_t gate, std::uint32_t now, const float G[3], const float R[3], const float U[3], const float F[3],
                          float r_gate, std::uint16_t type) noexcept {
    Gate* g = gate_of(b, gate);
    if (!g) {
        for (Gate& c : b.g) {
            bool used = c.gate != 0;
            if (used) {
                used = false;
                for (unsigned i = 0; i < c.n && !used; ++i) used = !slot_dead(c.m[i], now);
            }
            if (!used) {
                g = &c;
                break;
            }
        }
        if (!g) {
            ++b.full_gates;
            return nullptr;
        }
        *g = Gate{};
        for (unsigned i = 0; i < kMembers; ++i) clear_pairs(*g, i);
        g->gate = gate;
    }
    g->stamp_ms = now ? now : 1;
    g->type = type;
    g->r_gate = r_gate;
    for (unsigned i = 0; i < 3; ++i) {
        g->G[i] = G[i];
        g->R[i] = R[i];
        g->U[i] = U[i];
        g->F[i] = F[i];
    }
    return g;
}
// The member entry for a ship (its own, or a new one in an empty or dead slot; an evicted entry of the same ship and id within
// 30 s keeps its sequence number). *changed: the role before (Role::count for a new entry). nullptr when full (counted): the ship's
// pairs keep today's rules.
inline Member* enter(Board& b, Gate& g, std::uint32_t ship, std::uint32_t id, Role role, std::uint32_t now, Role* before) noexcept {
    for (Gate& o : b.g) { // one board per ship: an entry on another board (any role) is removed
        if (&o == &g || !o.gate) continue;
        const int k = find(o, ship);
        if (k >= 0) {
            clear_pairs(o, unsigned(k));
            o.m[k] = Member{};
        }
    }
    int i = find(g, ship);
    if (i >= 0 && g.m[i].id != id) { // another object behind the pointer: a new member
        clear_pairs(g, unsigned(i));
        g.m[i] = Member{};
        g.m[i].ship = ship;
        *before = Role::count;
    } else if (i >= 0)
        *before = g.m[i].role;
    if (i < 0) {
        for (unsigned k = 0; k < kMembers; ++k)
            if (k >= g.n || slot_dead(g.m[k], now)) {
                i = int(k);
                break;
            }
        if (i < 0) {
            ++b.full_members;
            return nullptr;
        }
        clear_pairs(g, unsigned(i));
        g.m[i] = Member{};
        g.m[i].ship = ship;
        if (unsigned(i) + 1 > g.n) g.n = unsigned(i) + 1;
        *before = Role::count;
    }
    Member& m = g.m[i];
    if (*before == Role::count) {
        m.seq = g.next_seq++;
        m.enter_ms = now ? now : 1;
    } else if (m.role != role) {
        // an evicted member of the same id re-enters within 30 s with its sequence number; one that left (none) takes a new one;
        // a change between the pair roles keeps it
        const bool lapsed = m.role == Role::none || (m.role == Role::evicted && now - m.evict_ms > kEvictKeepMs);
        if (lapsed || !m.seq) m.seq = g.next_seq++;
        m.enter_ms = now ? now : 1;
        if (m.role == Role::evicted || m.role == Role::none) {
            clear_pairs(g, unsigned(i));
            m.n = 0;
            m.decided_ms = 0;
        }
    }
    m.id = id;
    m.role = role;
    return &m;
}
// The ship leaves pair duty: 0x12 -> evicted (the sequence number kept 30 s), anything else -> none.
inline void leave(Gate& g, unsigned i, bool evict, std::uint32_t now) noexcept {
    Member& m = g.m[i];
    clear_pairs(g, i);
    m.n = 0;
    m.cap = -1.0f;
    m.decided_ms = 0;
    if (evict) {
        m.role = Role::evicted;
        m.evict_ms = now ? now : 1;
    } else {
        m = Member{}; // the slot is free: no pointer left behind for locate() or the bitmap
    }
}

// ---- the member's fields from a steering decision (the DLL's decide(), the fixture's steer_call) ------------------------------
// The state of a decision: position, basis, rates, velocity v f + slip, speed (site B re-reads it every kStateMs).
inline void state_from(State& st, const sc::Inputs& in, std::uint32_t target, std::uint32_t now) noexcept {
    const bool first = !st.ms;
    st.ms = now ? now : 1;
    st.order = in.order;
    st.target = target;
    for (unsigned i = 0; i < 3; ++i) {
        st.x[i] = in.x[i];
        st.w[i] = in.w[i];
        st.u[i] = in.v * in.row[2][i] + in.slip[i];
        for (unsigned c = 0; c < 3; ++c) st.rows[i][c] = in.row[i][c];
    }
    const float n2 = sc::dot3(st.rows[2], st.rows[2]);
    st.f_ok = sc::finite_f(n2) && n2 >= 0.9f && n2 <= 1.1f ? 1 : 0;
    st.v = in.v;
    if (first) st.v_des = in.v; // site B's next read takes the written value
}
// The plant parameters of a decision (the predictor's: the slower of yaw and pitch, alpha = W k).
inline void params_from(sc::JoinParams& p, const sc::Dynamics& dyn, float r_gate) noexcept {
    float omega = dyn.W[0] > 0.0f ? dyn.W[0] : 1e30f;
    if (dyn.W[1] > 0.0f && dyn.W[1] < omega) omega = dyn.W[1];
    if (omega >= 1e29f) omega = 0.0f;
    p.W = omega;
    p.alpha = omega * dyn.k;
    p.v_max = dyn.v_max;
    p.a_lin = dyn.a_lin;
    p.r_gate = r_gate;
}
// The hull of a member: w (the width read's lateral half-width; w = r when unread or implausible) and L = max(r, l).
inline void hull_from(Member& m, float r, float w, float l) noexcept {
    m.r = r;
    m.w = w > 0.0f && w <= r && sc::finite_f(w) ? w : r;
    m.L = sc::finite_f(l) && l > r ? l : r;
}
// A committed member's refresh (the lane board's commit test passed: an order-4/5 decision on the validated grid geometry of its
// target gate): the commit stamp, the hull, the plant, the order-4 line (X, h) and the decision's state.
inline void commit_fill(Member& m, const sc::Inputs& in, const sc::Dynamics& dyn, const sc::GateGeometry* geo, float w, float l, float r_gate,
                        std::uint32_t gate, std::uint32_t now) noexcept {
    m.commit_ms = now ? now : 1;
    if (in.order == sc::kOrderFlyBefore) m.handover_ms = 0; // a new order-4 decision: any earlier P2 belongs to a previous approach
    hull_from(m, dyn.r_own, w, l);
    m.large = dyn.large ? 1 : 0;
    params_from(m.params, dyn, r_gate);
    if (in.order == sc::kOrderFlyBefore) {
        m.line_ok = geo ? 1 : 0;
        if (geo)
            for (unsigned i = 0; i < 3; ++i) {
                m.X[i] = geo->X[i];
                m.h[i] = geo->h[i];
            }
    }
    m.leader = 0;
    state_from(m.st, in, gate, now);
}
// An escort member's refresh (D.1: a capital on 0x14 / 0x15 behind a committed leader on this board).
inline void escort_fill(Member& m, std::uint32_t leader, const sc::Inputs& in, const sc::Dynamics& dyn, float w, float l, float r_gate,
                        std::uint32_t now) noexcept {
    m.leader = leader;
    hull_from(m, dyn.r_own, w, l);
    m.large = dyn.large ? 1 : 0;
    params_from(m.params, dyn, r_gate);
    m.line_ok = 0;
    if (!m.st.ms) state_from(m.st, in, leader, now);
}
// The role's validity from the live order and target (D.1), read at site B every kStateMs: Why::count = the role holds; else why it
// ends (0x12 is the caller's: eviction). A committed member keeps its role to its gate on order 4 / 5 with a commit refresh within
// kCommitFreshMs, on 0 / 0x1f within the hand-over after P2, and on 8 (the launch). An escort keeps it on 0x14 / 0x15 to its leader,
// or on order 0 while the formation memory holds the pair (formation_current: the KC's 3 s gaps), behind a committed live leader on
// the same board. An arrival keeps it for 60 s after its launch (and in front of the plane: the caller, after the state read).
inline Why validity(const Gate& g, const Member& m, std::uint16_t order, std::uint32_t target, std::uint32_t now, bool formation_current) noexcept {
    if (m.role == Role::committed) {
        if (target != g.gate) return Why::target;
        if (order == sc::kOrderFlyBefore || order == sc::kOrderDock) return now - m.commit_ms > kCommitFreshMs ? Why::expire : Why::count;
        if (order == sc::kOrderNone || order == sc::kOrderRotate) return !m.handover_ms || now - m.handover_ms > kHandoverMaxMs ? Why::order : Why::count;
        return order == sc::kOrderWarpAway ? Why::count : Why::order;
    }
    if (m.role == Role::escort) {
        const int li = find(g, m.leader);
        if (li < 0 || g.m[li].role != Role::committed || !live(g.m[li], now)) return Why::leader;
        if (order == sc::kOrderEscort || order == sc::kOrderFollow) return target != m.leader ? Why::leader : Why::count;
        if (order == sc::kOrderNone) return formation_current ? Why::count : Why::order;
        return Why::order;
    }
    if (m.role == Role::arrival) return now - m.launch_ms > kArrivalWindowMs ? Why::window : Why::count;
    return Why::count;
}
__attribute__((always_inline)) inline bool in_front(const Gate& g, const float x[3]) noexcept {
    const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
    return sc::dot3(d, g.F) < 0.0f;
}

// ---- priority (D.1 "When the test fails") ----------------------------------------------------------------------------------------
// 1 arrival; 2 the hand-over, order 5 or order 8 (the slot holder); 3 the approach on order 4 (stage B's funnel); 5 escort.
inline unsigned rank(const Member& m) noexcept {
    if (m.role == Role::arrival) return 1;
    if (m.role == Role::escort) return 5;
    const std::uint16_t o = m.st.order;
    if (o == sc::kOrderNone || o == sc::kOrderRotate || o == sc::kOrderDock || o == sc::kOrderWarpAway) return 2;
    return 3;
}
// Whether a yields to b: the lower rank yields; within a rank the higher sequence number.
inline bool yields_to(const Member& a, const Member& b) noexcept {
    const unsigned ra = rank(a), rb = rank(b);
    if (ra != rb) return ra > rb;
    if (a.seq != b.seq) return a.seq > b.seq;
    return a.ship > b.ship;
}
// The yield needs a steered order whose speed site B writes: a committed approach on order 4 (the line plan) or an escort on
// 0x14 / 0x15.
inline bool can_yield(const Member& m) noexcept {
    if (m.role == Role::committed) return m.st.order == sc::kOrderFlyBefore && m.line_ok;
    if (m.role == Role::escort) return m.st.order == sc::kOrderEscort || m.st.order == sc::kOrderFollow;
    return false;
}

// ---- the rollout ------------------------------------------------------------------------------------------------------------------
inline Plan plan_of(const Member& m) noexcept {
    const std::uint16_t o = m.st.order;
    if (o == sc::kOrderWarpIn) return Plan::warp_in;
    if (o == sc::kOrderUndock) return Plan::exit;
    if (o == sc::kOrderWarpAway) return Plan::launch;
    if (m.role == Role::committed) {
        if (o == sc::kOrderFlyBefore) return m.line_ok ? Plan::line : Plan::drift;
        if (o == sc::kOrderDock) return Plan::dock;
        if (o == sc::kOrderNone) return Plan::brake;
        if (o == sc::kOrderRotate) return Plan::hold;
    }
    if (o == sc::kOrderNone && (m.role == Role::arrival || m.role == Role::escort)) return Plan::brake; // the KC's order-0 gaps too
    if (m.role == Role::escort && (o == sc::kOrderEscort || o == sc::kOrderFollow)) return Plan::follow;
    return Plan::drift;
}
// The plan's speed: the module's own speed before a yield cap when known and fresh, else the desired speed site B left.
inline float plan_speed(const Member& m) noexcept {
    if (m.v_plan > 0.0f && m.v_plan_ms && m.st.ms - m.v_plan_ms <= kCapMaxAgeMs) return m.v_plan;
    return m.st.v_des > 0.0f ? m.st.v_des : 0.0f;
}
__attribute__((always_inline)) inline void put(Sample& s, const float c[3], const float f[3]) noexcept {
    for (unsigned i = 0; i < 3; ++i) {
        s.c[i] = c[i];
        s.f[i] = f[i];
    }
}
// The state in the plane of the member's line (sc::join_plane), with the plane's lateral unit n (the ship's side); false when the
// plane is not finite.
inline bool line_plane(const Member& m, sc::JoinPlane* st, float n[3]) noexcept {
    sc::GateGeometry g;
    for (unsigned i = 0; i < 3; ++i) {
        g.X[i] = m.X[i];
        g.h[i] = m.h[i];
        g.A[i] = m.X[i] + sc::kGateCrossFraction * m.params.r_gate * m.h[i];
    }
    g.r = m.params.r_gate;
    if (!sc::join_plane(g, m.st.x, m.st.rows, m.st.w, m.st.u, m.st.v, st)) return false;
    const float dx[3] = {m.st.x[0] - m.X[0], m.st.x[1] - m.X[1], m.st.x[2] - m.X[2]};
    const float s = sc::dot3(dx, m.h);
    for (unsigned i = 0; i < 3; ++i) n[i] = dx[i] - s * m.h[i];
    float e = sc::sqrt_ss(sc::dot3(n, n));
    if (e > 1.0f) {
        for (unsigned i = 0; i < 3; ++i) n[i] /= e;
        return true;
    }
    const float fh = sc::dot3(m.st.rows[2], m.h); // on the line: the plane of the nose (join_plane's choice)
    for (unsigned i = 0; i < 3; ++i) n[i] = m.st.rows[2][i] - fh * m.h[i];
    e = sc::sqrt_ss(sc::dot3(n, n));
    if (e > 1e-4f) {
        for (unsigned i = 0; i < 3; ++i) n[i] /= e;
    } else {
        for (unsigned i = 0; i < 3; ++i) n[i] = m.st.rows[0][i];
    }
    return true;
}
// One rollout from the member's state over 30 s (61 samples at 0.5 s from 0.25 s steps). v_cmd < 0: the plan's own speed; >= 0: a
// yield candidate (the line and drift plans). Returns the samples written; *vmax the largest speed over the rollout. Roles the engine
// moves follow its kinematics (section 10.10): 0x21 the warp-in path to f+0x240, then 0x1e; 0x1e axis-parallel out at 120 / 240 m/s
// until |dz| > r (r / 4 for small hulls), then straight on; order 8 along +F for 0.9 s, then gone; order 0 braking along the nose at
// a; 0x1f in place; order 5 at its speed toward f+0x240 until the order's arrival radius (|v| / 10 + r), then gone.
inline unsigned rollout(const Member& m, const Gate& g, float v_cmd, Sample* out, float* vmax) noexcept {
    const Plan plan = plan_of(m);
    float f0[3] = {m.st.rows[2][0], m.st.rows[2][1], m.st.rows[2][2]};
    {
        const float n2 = sc::dot3(f0, f0);
        if (sc::finite_f(n2) && n2 > 0.25f) {
            const float inv = 1.0f / sc::sqrt_ss(n2);
            for (float& v : f0) v *= inv;
        } else
            for (unsigned i = 0; i < 3; ++i) f0[i] = g.F[i];
    }
    float c[3] = {m.st.x[0], m.st.x[1], m.st.x[2]};
    float u0 = sc::sqrt_ss(sc::dot3(m.st.u, m.st.u));
    if (!sc::finite_f(u0)) u0 = 0.0f;
    *vmax = u0;
    unsigned n = 0;
    switch (plan) {
    case Plan::line: {
        sc::JoinPlane st;
        float nn[3];
        if (!line_plane(m, &st, nn)) break;
        const float v_des = v_cmd >= 0.0f ? v_cmd : plan_speed(m);
        const float k_lag = sc::one_minus_exp_neg(kStepS / sc::kSlipTau), a_along = sc::kGateCrossFraction * m.params.r_gate;
        float x = st.s, y = st.y, psi = st.psi, upsi = st.upsi, w = st.w, v = st.v;
        // the nose: the live nose turned about the plane's normal h x n by the plant's in-plane turn (the plant has no out-of-plane
        // axis; the out-of-plane part of the live nose is kept, not projected away: the capsule at sample 0 is the true hull)
        float ax[3] = {m.h[1] * nn[2] - m.h[2] * nn[1], m.h[2] * nn[0] - m.h[0] * nn[2], m.h[0] * nn[1] - m.h[1] * nn[0]};
        {
            const float an = sc::sqrt_ss(sc::dot3(ax, ax));
            for (float& q : ax) q = an > 1e-6f ? q / an : 0.0f;
        }
        const float psi0 = psi;
        bool past = false;
        float dir[3] = {0, 0, 0};
        for (unsigned i = 0; i < 3; ++i) c[i] = m.X[i] + x * m.h[i] + y * nn[i];
        for (unsigned k = 0; k < kSamples; ++k) {
            for (unsigned step = 0; k && step < 2; ++step) {
                if (!past && x < 0.0f && m.params.W > 1e-4f && m.params.alpha > 1e-6f) {
                    sc::plant_step(m.params, v_des, kStepS, k_lag, a_along, x, y, psi, upsi, w, v);
                    for (unsigned i = 0; i < 3; ++i) c[i] = m.X[i] + x * m.h[i] + y * nn[i];
                } else { // past the crossing plane (P2): the hand-over brakes on its last velocity (order 0, desired 0; 0x1f in place)
                    if (!past) {
                        past = true;
                        float su, cu;
                        sc::sincos_f(upsi, &su, &cu);
                        for (unsigned i = 0; i < 3; ++i) dir[i] = cu * m.h[i] + su * nn[i];
                    }
                    const float a = m.params.a_lin > 0.0f ? m.params.a_lin : 1.0f;
                    const float v1 = v > a * kStepS ? v - a * kStepS : 0.0f;
                    for (unsigned i = 0; i < 3; ++i) c[i] += dir[i] * 0.5f * (v + v1) * kStepS;
                    v = v1;
                }
            }
            float sp, cp;
            sc::sincos_f(psi - psi0, &sp, &cp); // Rodrigues: f0 cos + (a x f0) sin + a (a . f0)(1 - cos)
            const float axf[3] = {ax[1] * f0[2] - ax[2] * f0[1], ax[2] * f0[0] - ax[0] * f0[2], ax[0] * f0[1] - ax[1] * f0[0]};
            const float af = sc::dot3(ax, f0);
            const float f[3] = {f0[0] * cp + axf[0] * sp + ax[0] * af * (1.0f - cp), f0[1] * cp + axf[1] * sp + ax[1] * af * (1.0f - cp),
                                f0[2] * cp + axf[2] * sp + ax[2] * af * (1.0f - cp)};
            put(out[n++], c, f);
            if (v > *vmax) *vmax = v;
        }
        break;
    }
    case Plan::follow: {
        const int li = find(g, m.leader);
        const Member* L = li >= 0 && live(g.m[li], m.st.ms) && g.m[li].n ? &g.m[li] : nullptr;
        if (L) {
            const std::int32_t dms = std::int32_t(m.st.ms - L->roll_ms);
            const std::int32_t koff = dms <= 0 ? 0 : (dms + std::int32_t(kSampleMs / 2)) / std::int32_t(kSampleMs);
            const unsigned last = L->n - 1u, k0 = unsigned(koff) < L->n ? unsigned(koff) : last;
            const float off[3] = {c[0] - L->s[k0].c[0], c[1] - L->s[k0].c[1], c[2] - L->s[k0].c[2]};
            float lv = 0.0f, lag = 0.0f; // a yield candidate v_cmd: the escort falls back along the leader's path by the leader's speed
                                         // above it (the formation follows the same track, later)
            for (unsigned k = 0; k < kSamples; ++k) {
                const unsigned j = k0 + k < L->n ? k0 + k : last; // the leader's rollout ends (a launch): the escort holds its slot there
                float back[3] = {0, 0, 0};
                if (v_cmd >= 0.0f && k) {
                    const unsigned jp = k0 + k - 1 < L->n ? k0 + k - 1 : last;
                    const float dl[3] = {L->s[j].c[0] - L->s[jp].c[0], L->s[j].c[1] - L->s[jp].c[1], L->s[j].c[2] - L->s[jp].c[2]};
                    const float step = sc::sqrt_ss(sc::dot3(dl, dl));
                    const float excess = step - v_cmd * kSampleS;
                    if (excess > 0.0f) lag += excess;
                    if (step > 1e-3f)
                        for (unsigned q = 0; q < 3; ++q) back[q] = dl[q] / step * lag;
                }
                const float p[3] = {L->s[j].c[0] + off[0] - back[0], L->s[j].c[1] + off[1] - back[1], L->s[j].c[2] + off[2] - back[2]};
                if (k) {
                    const float d[3] = {p[0] - out[n - 1].c[0], p[1] - out[n - 1].c[1], p[2] - out[n - 1].c[2]};
                    lv = sc::sqrt_ss(sc::dot3(d, d)) / kSampleS;
                    if (lv > *vmax) *vmax = lv;
                }
                put(out[n++], p, f0);
            }
            break;
        }
        [[fallthrough]];
    }
    case Plan::drift: {
        float dir[3] = {f0[0], f0[1], f0[2]};
        if (u0 > 0.5f)
            for (unsigned i = 0; i < 3; ++i) dir[i] = m.st.u[i] / u0;
        float v = u0;
        const float v_des = v_cmd >= 0.0f ? v_cmd : v;
        const float a = m.params.a_lin > 0.0f ? m.params.a_lin : 1.0f;
        for (unsigned k = 0; k < kSamples; ++k) {
            if (k)
                for (unsigned step = 0; step < 2; ++step) {
                    if (v > v_des) {
                        v -= a * kStepS;
                        if (v < v_des) v = v_des;
                    }
                    for (unsigned i = 0; i < 3; ++i) c[i] += dir[i] * v * kStepS;
                }
            put(out[n++], c, f0);
        }
        break;
    }
    case Plan::dock: {
        float d[3] = {m.st.point[0] - c[0], m.st.point[1] - c[1], m.st.point[2] - c[2]};
        const float dist = sc::sqrt_ss(sc::dot3(d, d));
        const float v = m.st.v > 0.0f ? m.st.v : 0.0f;
        const float radius = v / 10.0f + m.r;
        if (dist > 1e-3f)
            for (unsigned i = 0; i < 3; ++i) d[i] /= dist;
        for (unsigned k = 0; k < kSamples; ++k) {
            const float t = float(k) * kSampleS, along = v * t;
            if (dist - along <= radius && k) break; // the order's arrival test fired: order 8 and gone within 0.9 s
            for (unsigned i = 0; i < 3; ++i) c[i] = m.st.x[i] + d[i] * along;
            put(out[n++], c, f0);
        }
        break;
    }
    case Plan::brake: {
        const float a = m.params.a_lin > 0.0f ? m.params.a_lin : 1.0f;
        float v = m.st.v > 0.0f ? m.st.v : 0.0f;
        for (unsigned k = 0; k < kSamples; ++k) {
            if (k)
                for (unsigned step = 0; step < 2; ++step) {
                    const float v1 = v > a * kStepS ? v - a * kStepS : 0.0f;
                    for (unsigned i = 0; i < 3; ++i) c[i] += f0[i] * 0.5f * (v + v1) * kStepS;
                    v = v1;
                }
            put(out[n++], c, f0);
        }
        break;
    }
    case Plan::hold:
        for (unsigned k = 0; k < kSamples; ++k) put(out[n++], c, f0);
        break;
    case Plan::warp_in: case Plan::exit: {
        // 0x21: p = point + 1.665 u^2 F (u = ms left) until the deadline, at rest at the point; then 0x1e out along the ship's own
        // axis-parallel line, away from the plane, at 120 / 240 m/s until |dz| > r (r / 4), then straight on at that velocity
        const float ve = m.large ? kExitSpeedLargeMs : kExitSpeedSmallMs;
        float t_left = plan == Plan::warp_in && m.st.left_ms > 0 ? float(m.st.left_ms) * 0.001f : 0.0f;
        if (t_left > 1.0f) t_left = 1.0f;
        const float* P = plan == Plan::warp_in ? m.st.point : m.st.x;
        const float dz0 = (P[0] - g.G[0]) * g.F[0] + (P[1] - g.G[1]) * g.F[1] + (P[2] - g.G[2]) * g.F[2];
        const float sgn = dz0 > 0.0f ? 1.0f : -1.0f; // away from the plane on the side it is on
        if (ve > *vmax) *vmax = ve;
        for (unsigned k = 0; k < kSamples; ++k) {
            const float t = float(k) * kSampleS;
            if (t < t_left) {
                const float u = (t_left - t) * 1000.0f, z = kWarpInK * u * u;
                for (unsigned i = 0; i < 3; ++i) c[i] = P[i] + z * g.F[i];
            } else {
                const float te = t - t_left;
                for (unsigned i = 0; i < 3; ++i) c[i] = P[i] + sgn * ve * te * g.F[i]; // the order's end changes no velocity here
            }
            put(out[n++], c, f0);
        }
        if (plan == Plan::warp_in && 2697.0f / 0.9f > *vmax) *vmax = 2697.0f / 0.9f; // the warp-in path's peak speed bound
        break;
    }
    case Plan::launch: {
        // order 8: 900 ms along +F from where the trigger fired (at most 2697 m plus v0 t), then the ship leaves the sector
        const float v0 = m.st.v > 0.0f ? m.st.v : 0.0f;
        for (unsigned k = 0; k < kSamples; ++k) {
            const float t = float(k) * kSampleS;
            if (t > 0.9f) break;
            const float tm = t * 1000.0f, z = v0 * t + kWarpInK * tm * tm;
            for (unsigned i = 0; i < 3; ++i) c[i] = m.st.x[i] + z * g.F[i];
            put(out[n++], c, f0);
        }
        if (2697.0f / 0.9f + v0 > *vmax) *vmax = 2697.0f / 0.9f + v0;
        break;
    }
    default: break;
    }
    return n;
}

// ---- the pair sweep ---------------------------------------------------------------------------------------------------------------
struct Sweep {
    bool clear = false;
    unsigned t_c = 255;      // the first conflict sample (255 none)
    float min_gap = 1e30f;   // the conflict sample's surface gap minus its margin (m, < 0; 1e30 when clear)
    unsigned narrow = 0;     // samples that needed the narrow test
    bool stale = false;      // fail closed: a partner rollout more than 1 s older, or fewer paired samples than the warn reach needs
};
// One member's rollout (samples sa, na, t0 a_ms, the largest speed a_vmax) against another's. B's sample for A's sample i is
// i + round((a_ms - b_ms) / 500 ms); the residual offset (<= 250 ms) adds |offset| x B's largest speed to the margin. The broad test
// uses the per-pair bound (v_A,max + v_B,max) dt / 2 on |u_A - u_B| dt / 2 (no square root); a sample that fails it is retested
// with the samples' own relative motion, then narrowly.
inline Sweep sweep(const Sample* sa, unsigned na, std::uint32_t a_ms, float a_vmax, float La, float wa, const Sample* sb, unsigned nb,
                   std::uint32_t b_ms, float b_vmax, float Lb, float wb, unsigned min_samples = kMinOverlapSamples) noexcept {
    Sweep out;
    const std::int32_t dms = std::int32_t(a_ms - b_ms);
    if (dms > std::int32_t(kRolloutMaxAgeMs) || dms < -std::int32_t(kRolloutMaxAgeMs)) { // the rollouts are too far apart in time
        out.stale = true;
        return out;
    }
    const std::int32_t koff = dms >= 0 ? (dms + std::int32_t(kSampleMs / 2)) / std::int32_t(kSampleMs)
                                       : -((-dms + std::int32_t(kSampleMs / 2)) / std::int32_t(kSampleMs));
    const float resid = sc::fabs_f(float(dms - koff * std::int32_t(kSampleMs))) * 0.001f;
    const float Lsum = La + Lb, wsum = wa + wb;
    const float ha = La > wa ? La - wa : 0.0f, hb = Lb > wb ? Lb - wb : 0.0f; // the capsules' half segments
    const bool caps = ha > 1.0f && hb > 1.0f;
    const float inv2ha = caps ? 0.5f / ha : 0.0f, inv2hb = caps ? 0.5f / hb : 0.0f;
    const float m_bound = (a_vmax + b_vmax) * (0.5f * kSampleS);
    // A's samples i0 .. i1 - 1 pair with B's i + koff (both on the 0.5 s grid)
    const std::int32_t i0 = koff < 0 ? -koff : 0;
    std::int32_t i1 = std::int32_t(na);
    if (std::int32_t(nb) - koff < i1) i1 = std::int32_t(nb) - koff;
    if (i1 - i0 < std::int32_t(min_samples < kMinOverlapSamples ? kMinOverlapSamples : min_samples)) { // too short an overlap: no
        out.stale = true;                                                                                  // certificate (fail closed)
        return out;
    }
    const Sample* B = sb + koff;
    // the drift charges each side from its own rollout's t0: k (t + |t0_A - t0_B|), t from A's t0
    float base = kMarginM + resid * b_vmax + kDriftMs * (kSampleS * float(i0 > 0 ? i0 : 0) + float(dms >= 0 ? dms : -dms) * 0.001f);
    for (std::int32_t i = i0; i < i1; ++i, base += kDriftMs * kSampleS) {
        const Sample& A = sa[i];
        const Sample& Bi = B[i];
        const float d[3] = {A.c[0] - Bi.c[0], A.c[1] - Bi.c[1], A.c[2] - Bi.c[2]};
        const float d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        const float reach = Lsum + base + m_bound;
        if (d2 >= reach * reach) continue; // broad, with the per-pair bound on the relative motion
        // narrow, with the samples' own relative motion over the interval (forward difference; the last sample backward):
        // |segment_A - segment_B| >= w_A + w_B + base + |du| dt / 2, |du| taken as its L1 norm (an upper bound within sqrt 3: the
        // margin errs wide, never narrow), on squares without a square root
        const std::int32_t nx = i + 1 < i1 ? i + 1 : i > i0 ? i - 1 : i;
        const float du1 = sc::fabs_f((sa[nx].c[0] - A.c[0]) - (B[nx].c[0] - Bi.c[0])) + sc::fabs_f((sa[nx].c[1] - A.c[1]) - (B[nx].c[1] - Bi.c[1])) +
                          sc::fabs_f((sa[nx].c[2] - A.c[2]) - (B[nx].c[2] - Bi.c[2]));
        const float rn = wsum + base + 0.5f * du1;
        ++out.narrow;
        float g2;
        if (caps)
            g2 = capsule_d2(A.c, A.f, ha, inv2ha, Bi.c, Bi.f, hb, inv2hb);
        else { // a sphere hull (L = w): the general segment distance
            const float p1[3] = {A.c[0] - ha * A.f[0], A.c[1] - ha * A.f[1], A.c[2] - ha * A.f[2]};
            const float q1[3] = {A.c[0] + ha * A.f[0], A.c[1] + ha * A.f[1], A.c[2] + ha * A.f[2]};
            const float p2[3] = {Bi.c[0] - hb * Bi.f[0], Bi.c[1] - hb * Bi.f[1], Bi.c[2] - hb * Bi.f[2]};
            const float q2[3] = {Bi.c[0] + hb * Bi.f[0], Bi.c[1] + hb * Bi.f[1], Bi.c[2] + hb * Bi.f[2]};
            g2 = seg_seg_d2(p1, q1, p2, q2);
        }
        if (!(g2 >= rn * rn)) {
            out.t_c = unsigned(i);
            out.min_gap = sc::sqrt_ss(g2) - rn;
            return out;
        }
    }
    out.clear = true;
    return out;
}

// ---- one decision --------------------------------------------------------------------------------------------------------------
// A verdict change for the pair rows (the DLL logs it at most once per pair per second; the fixture counts it).
struct PairEvent {
    std::uint32_t a = 0, b = 0;
    std::uint8_t verdict = 0, t_c = 255, yielder = 0; // yielder: 0 none, 1 a, 2 b
    float gap = 0, cap = -1.0f;
};
struct DecideStats {
    unsigned rollouts = 0, sweeps = 0, narrow = 0, conflicts = 0, yields = 0, unresolved = 0, candidates = 0, released = 0;
};
constexpr unsigned kMaxEvents = 2 * kMembers;
// The paired samples a sweep needs (fail closed below it): the engine's warn reach in time, 1 s plus its braking term min(600 m, v^2 / 2a)
// at the speed v of either ship (section 3.1), and never fewer than kMinOverlapSamples.
inline unsigned warn_reach_samples(const Member& a, const Member& b) noexcept {
    float t = 0.0f;
    const Member* both[2] = {&a, &b};
    for (const Member* m : both) {
        const float v = sc::sqrt_ss(sc::dot3(m->st.u, m->st.u)), acc = m->params.a_lin > 0.1f ? m->params.a_lin : 1.0f;
        if (!sc::finite_f(v) || v <= 0.1f) continue;
        float tb = v / (2.0f * acc);
        if (600.0f / v < tb) tb = 600.0f / v;
        if (tb > t) t = tb;
    }
    unsigned n = unsigned((1.0f + t) / kSampleS) + 2u;
    if (n < kMinOverlapSamples) n = kMinOverlapSamples;
    return n > kSamples ? kSamples : n;
}
// Every pair of member i with the other live members, swept against the rollout s (n samples, t0 = i's state). write: the matrix row
// of i takes the verdicts (clear, or none for a conflict) and *conflicts the bit of each conflicting partner; !write (a yield
// candidate): stops at the first conflict. Returns whether every pair cleared.
inline bool sweep_all(Gate& g, unsigned i, const Sample* s, unsigned n, float vmax, std::uint32_t now, bool write, std::uint32_t* conflicts,
                      DecideStats* st) noexcept {
    const Member& A = g.m[i];
    bool all = true;
    for (unsigned j = 0; j < g.n; ++j) {
        if (j == i || !live(g.m[j], now)) continue;
        const Member& B = g.m[j];
        ++st->sweeps;
        // fail closed: no samples on either side, a rollout more than 1 s from the other, too short an overlap -> no verdict (none:
        // the engine's warn and the sphere avoidance), not a conflict to resolve
        const Sweep w = B.n && n ? sweep(s, n, A.st.ms, vmax, A.L, A.w, B.s, B.n, B.roll_ms, B.v_roll_max, B.L, B.w, warn_reach_samples(A, B))
                                 : Sweep{false, 255, 1e30f, 0, true};
        st->narrow += w.narrow;
        if (write) {
            g.v[i][j] = g.v[j][i] = std::uint8_t(w.clear ? PairV::clear : PairV::none);
            g.tc[i][j] = g.tc[j][i] = std::uint8_t(w.t_c);
            g.vms[i][j] = g.vms[j][i] = now ? now : 1;
        }
        if (!w.clear) {
            all = false;
            if (conflicts && !w.stale) *conflicts |= 1u << j;
            if (!write) return false;
        }
    }
    return all;
}
// Y's yield by speed: Y's current plan at 0.6 v, then 25 m/s, then the 10 m/s floor (each below the plan's speed); a new rollout
// and sweeps against every member each; the first that clears every pair is taken (Y's rollout, Y's cap, Y's row of the matrix).
inline bool try_yield(Gate& g, unsigned y, std::uint32_t now, DecideStats* st) noexcept {
    Member& Y = g.m[y];
    if (!can_yield(Y)) return false;
    const float v = plan_speed(Y) > 0.0f ? plan_speed(Y) : Y.st.v;
    const float cand[3] = {kYieldFrac * v, kYieldMidMs, kYieldFloorMs};
    Sample tmp[kSamples];
    for (float c : cand) {
        if (!(c >= kYieldFloorMs) || !(c < v)) continue;
        ++st->candidates;
        ++st->rollouts;
        float vmax = 0.0f;
        const unsigned n = rollout(Y, g, c, tmp, &vmax);
        if (!n || !sweep_all(g, y, tmp, n, vmax, now, false, nullptr, st)) continue;
        for (unsigned k = 0; k < n; ++k) Y.s[k] = tmp[k];
        Y.n = std::uint8_t(n);
        Y.roll_ms = Y.st.ms;
        Y.v_roll_max = vmax;
        Y.cap = c;
        Y.cap_ms = now ? now : 1;
        sweep_all(g, y, Y.s, Y.n, Y.v_roll_max, now, true, nullptr, st);
        ++st->yields;
        return true;
    }
    return false;
}
// Member i's decision (D.1): its rollout on its plan's speed (a yield cap lapses when that clears every pair), the sweeps against every
// live member, the yield of the lower-priority ship of each conflict pair (at most kMaxYieldsPerDecision resolutions; i itself at most
// once), and the hand-back of what stays in conflict (unresolved: site E passes, the sphere avoidance applies). Verdict changes of
// i's row go to ev (at most kMaxEvents).
inline void decide(Gate& g, unsigned i, std::uint32_t now, DecideStats* st, PairEvent* ev, unsigned* n_ev) noexcept {
    Member& A = g.m[i];
    A.decided_ms = now ? now : 1;
    std::uint8_t before[kMembers];
    for (unsigned j = 0; j < kMembers; ++j) before[j] = g.v[i][j];
    if (!live(A, now)) {
        clear_pairs(g, i);
        A.n = 0;
        return;
    }
    ++st->rollouts;
    float vmax = 0.0f;
    A.n = std::uint8_t(rollout(A, g, -1.0f, A.s, &vmax));
    A.roll_ms = A.st.ms;
    A.v_roll_max = vmax;
    std::uint32_t conflicts = 0;
    sweep_all(g, i, A.s, A.n, vmax, now, true, &conflicts, st); // A.n == 0 (a refused plane, a NaN state): every pair none
    if (!conflicts && A.cap >= 0.0f) {
        A.cap = -1.0f;
        ++st->released;
    }
    unsigned tries = 0;
    bool self_tried = false;
    for (unsigned j = 0; j < g.n && conflicts; ++j) {
        if (!((conflicts >> j) & 1u)) continue;
        ++st->conflicts;
        if (g.v[i][j] == std::uint8_t(PairV::clear)) continue; // a yield earlier in this decision already cleared it
        const bool a_yields = yields_to(A, g.m[j]);
        bool ok = false;
        if (tries < kMaxYieldsPerDecision && (!a_yields || !self_tried)) {
            ++tries;
            if (a_yields) self_tried = true;
            ok = try_yield(g, a_yields ? i : j, now, st);
        }
        if (!ok || g.v[i][j] != std::uint8_t(PairV::clear)) {
            g.v[i][j] = g.v[j][i] = std::uint8_t(PairV::unresolved);
            g.vms[i][j] = g.vms[j][i] = now ? now : 1;
            ++st->unresolved;
        }
    }
    if (!ev || !n_ev) return;
    for (unsigned j = 0; j < g.n; ++j) {
        if (j == i || g.v[i][j] == before[j] || *n_ev >= kMaxEvents) continue;
        PairEvent& e = ev[(*n_ev)++];
        e.a = A.ship;
        e.b = g.m[j].ship;
        e.verdict = g.v[i][j];
        e.t_c = g.tc[i][j];
        e.yielder = A.cap >= 0.0f && A.cap_ms == now ? 1 : g.m[j].cap >= 0.0f && g.m[j].cap_ms == now ? 2 : 0;
        e.cap = e.yielder == 1 ? A.cap : e.yielder == 2 ? g.m[j].cap : -1.0f;
    }
}

// Whether member i is due for a decision: live, and never decided, or decided at least kDecideMs ago with a state newer than its rollout.
__attribute__((always_inline)) inline bool due(const Member& m, std::uint32_t now) noexcept {
    return live(m, now) && (!m.decided_ms || (now - m.decided_ms >= kDecideMs && m.st.ms != m.roll_ms));
}
// Whether the frame may start another decision (the frame's bound): the first always; then within kDecisionsPerFrame and while a
// decision with every yield still fits the rollout budget.
__attribute__((always_inline)) inline bool frame_room(unsigned decisions, unsigned rollouts) noexcept {
    return !decisions || (decisions < kDecisionsPerFrame && rollouts + kMaxRolloutsPerDecision <= kRolloutsPerFrame);
}

// ---- reads for site E and the avoidance ---------------------------------------------------------------------------------------
// The pair verdict for (i, j) as site E takes it: clear only while at most kCertMaxAgeMs old.
inline PairV pair_verdict(const Gate& g, unsigned i, unsigned j, std::uint32_t now, std::uint32_t max_age_ms) noexcept {
    if (i >= g.n || j >= g.n || i == j || !live(g.m[i], now) || !live(g.m[j], now)) return PairV::none;
    const PairV v = PairV(g.v[i][j]);
    if (v == PairV::clear && (now - g.vms[i][j] > max_age_ms || !g.m[i].decided_ms || !g.m[j].decided_ms ||
                              now - g.m[i].decided_ms > kDecisionMaxAgeMs || now - g.m[j].decided_ms > kDecisionMaxAgeMs))
        return PairV::none; // a stale verdict, or a member whose decisions starved beyond 1 s: no certificate
    return v;
}
// Site E for a member pair (D.1): suppress iff the verdict is clear and fresh (<= 250 ms, both members decided within 1 s), both live
// forward rows are readable unit vectors and neither ship is on 0x12 (the live orders).
inline bool e_certified(const Gate& g, unsigned i, unsigned j, std::uint32_t now, bool fa_ok, bool fb_ok, std::uint16_t a_order,
                        std::uint16_t b_order) noexcept {
    if (!fa_ok || !fb_ok || a_order == sc::kOrderAvoid || b_order == sc::kOrderAvoid) return false;
    return pair_verdict(g, i, j, now, kCertMaxAgeMs) == PairV::clear;
}
__attribute__((always_inline)) inline bool unit_row(const float f[3]) noexcept {
    const float n2 = sc::dot3(f, f);
    return sc::finite_f(n2) && n2 >= 0.9f && n2 <= 1.1f;
}

// ---- D.4: the arrival placement ------------------------------------------------------------------------------------------------
// The installed gate rows (section 10.9): no collision mesh on the ordinary rows 0..3 and 5..14; the Terran ring on 21..24.
// Section 10.9: the ordinary rows 0..3 and 5..14, row 4 (Xenon, a blended disc only) and rows 15..20 (inactive hubs, an empty body stub)
// carry no collision mesh; the Terran rows 21..24 carry the ring.
__attribute__((always_inline)) inline bool gate_no_mesh(std::uint16_t type) noexcept { return type <= 20; }
__attribute__((always_inline)) inline bool gate_terran(std::uint16_t type) noexcept { return type >= 21 && type <= 24; }
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
} // namespace x3m::ship_avoidance::ctl
