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
// Stage B (D.2, D.3, D.5, D.8, D.12; gate-traffic-controller.md B.1): the holding petals, the pass point Q and the final lines, the
// funnel, the sequencer and the through-slot model.
constexpr unsigned kPetalsPerRing = 7, kRings = 3, kPetals = kPetalsPerRing * kRings; // 7 petals 40 deg apart on the lower 240 deg
constexpr float kPetalPhi0 = -2.0943951f, kPetalDphi = 0.6981317f; // -120 deg (from -U toward +R), 40 deg
constexpr float kPetalRh = 1000.0f, kPetalSh = 1500.0f;  // the racetrack's turn radius and straight (m)
constexpr float kLeadM = 2500.0f;                        // the petal's front lies kLeadM behind the ship's X (D.2.4: L_lead >= 2.5 km)
constexpr float kPetalVmin = 25.0f, kPetalVmax = 60.0f, kPetalVnom = 40.0f; // the holding speeds (m/s)
constexpr float kRingGapM = 800.0f;   // two adjacent petals' planes are >= 2 w_max + this apart at the ring radius (the sweep's 30 s
                                      // margin 50 m + k 30 s, the tracking): rho = (2 w_max + kRingGapM) / (2 sin(dphi / 2))
constexpr float kRingMinM = 2400.0f;  // the ring radius at least this (D.2.4)
constexpr float kSinHalfDphi = 0.34202014f; // sin 20 deg
constexpr float kChordSlackM = 150.0f; // the final line crosses the grid sphere as a chord: |Lambda - A_lat| <= 0.75 r - 150 m (D.2.2)
constexpr float kTrackM = 100.0f, kFanMarginM = 50.0f, kSmallMaxM = 140.0f; // the trigger's tracking allowance; the upper fan's margin
constexpr float kFanLowR = 0.38890873f; // the upper small-arrival fan's lowest U (0.55 r cos 45 deg)
constexpr float kSin3 = 0.05233596f;  // the hull's cross-section with the nose 3 deg off F: w + (L - w) sin 3 deg
constexpr float kReleaseNoseCos = 0.5f;    // a holder is released with the nose within 60 deg of F (the release window, B.6 item 3) ...
constexpr float kReleaseFrontM = 866.02540f; // ... behind the ring-0 front turn's 60-deg point: a1 + R_h sin 60 deg along F
constexpr float kFinalAlongM = 2500.0f, kFinalLatM = 150.0f; // the funnel's final straight (the next release may come)
constexpr float kGuardM = 300.0f;     // the grid sphere guard (D.2.4): a holder never comes within 0.75 r + 300 m (+ its braking) of A
constexpr std::uint32_t kFunnelGapMs = 2000;    // the funnel's gap after a launch (the KC's hand-over frames)
constexpr std::uint32_t kSlotCooldownMs = 60000; // the through slot after a change of direction (section 10.2)
constexpr std::uint32_t kSlotArrivalMs = 61000;  // a capital launch at the gate blocks our side this long (t_a + 60 + 1 s)
constexpr std::uint32_t kReserveMs = 300000;     // an escort's reserved number holds 300 s after its leader's launch, refreshed while it is
                                                 // on 0x15 to the gate or within kReserveKeepM of it (B.3 item 4; Run163: order 4 at +53 s
                                                 // and +81 s after the leader's launch, outbound legs at +223 / +274 s from 33 km)
constexpr float kReserveKeepM = 15000.0f;        // B.3 item 4: within this distance of the gate any order keeps the reservation alive
constexpr std::uint32_t kBurstMs = 2000;         // members entering within 2 s of each other (a load) are sequenced by distance (D.8)
constexpr std::uint32_t kGapMs = 5000;
constexpr std::uint32_t kGapEscortMs = 3000; // the escort template's wait (2-3 s): an escort's gap ends by then           // the script's order-0 re-issue gap (escort legs: 2-3 s waits; committed: <= 5 s)
constexpr std::uint32_t kFeasMs = 1000;
constexpr std::uint32_t kHeadSkipMs = 60000;      // a head of the sequence deferred this long with the funnel free re-queues at the tail          // the funnel's feasibility prediction at most once per second per ship
constexpr std::uint32_t kEscortSwitchMs = 4500;  // an escort's first order-4 chance after its leader's launch (D.12: 2-3 s wait + overhead)
constexpr float kEscortSwitchM = 10000.0f;       // ... within 10 km of the gate (Mayhem.Task.Move, D.12)
// B.2 (Run162): the arrival to the ring, the release window's horizon, the funnel's last stretch
constexpr float kInboundM = 2000.0f;     // inbound while farther than the ring's extent + 2 km from the gate
constexpr float kInboundHystM = 1000.0f; // ... and back to inbound only beyond a further 1 km (no flapping at the boundary)
constexpr float kHorizonS = float(sc::kJoinMaxSteps) * sc::kJoinStepS; // the predictor's horizon (256 s): the window's dX <= v_max * this
constexpr float kLastStretchR = 1.2f;    // a ship within 1.2 r_gate of X (or beyond X) is past the funnel's last stretch: never re-held
// B.2 (Run162): inbound = committed (or an escort) farther than its ring's extent + kInboundM from the gate: it flies toward its petal's
// entry at the plan speed, is no release candidate and runs no head-skip timer
enum class Sb : std::uint8_t { none = 0, hold, funnel, handover, dock, launch, inbound, count };
inline const char* sb_name(Sb s) noexcept {
    static const char* const n[unsigned(Sb::count)] = {"none", "hold", "funnel", "handover", "dock", "launch", "inbound"};
    return unsigned(s) < unsigned(Sb::count) ? n[unsigned(s)] : "?";
}

enum class Role : std::uint8_t { none = 0, committed, escort, arrival, evicted, count };
inline const char* role_name(Role r) noexcept {
    static const char* const n[unsigned(Role::count)] = {"none", "committed", "escort", "arrival", "evicted"};
    return unsigned(r) < unsigned(Role::count) ? n[unsigned(r)] : "?";
}
enum class Plan : std::uint8_t { drift = 0, line, dock, brake, hold, warp_in, exit, launch, follow, petal, line5, count };
inline const char* plan_name(Plan p) noexcept {
    static const char* const n[unsigned(Plan::count)] = {"drift", "line", "dock", "brake", "hold", "warp_in", "exit", "launch", "follow",
                                                         "petal", "line5"};
    return unsigned(p) < unsigned(Plan::count) ? n[unsigned(p)] : "?";
}
enum class PairV : std::uint8_t { none = 0, clear, unresolved, count };
inline const char* pair_name(PairV v) noexcept {
    static const char* const n[unsigned(PairV::count)] = {"none", "clear", "unresolved"};
    return unsigned(v) < unsigned(PairV::count) ? n[unsigned(v)] : "?";
}
// Why a member left the board or changed role (the ship_gate_ctl row).
enum class Why : std::uint8_t { enter = 0, commit, handover, escort, arrival, evict, order, target, expire, identity, leader, window, reenter,
                                hold, release, go_around, dock, launch, inbound, skip, rejoin, gone, sector, count };
inline const char* why_name(Why w) noexcept {
    static const char* const n[unsigned(Why::count)] = {"enter", "commit", "handover", "escort", "arrival", "evict", "order", "target",
                                                        "expire", "identity", "leader", "window", "reenter", "hold", "release",
                                                        "go_around", "dock", "launch", "inbound", "skip", "rejoin", "gone", "sector"};
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
    // stage B (B.1): the stage, the petal, the sequence key (seq << 4; an escort: its leader's key + 1 + its rank), the reservation
    // (an escort's key holds until res_until; 0 while its leader is present), the release plan, Q and the final lines (gate-frame R, U)
    Sb sb = Sb::none;
    std::int8_t petal = -1;
    std::uint8_t key_res = 0, q_ok = 0, refused = 0;
    std::uint8_t ringed = 0;    // B.2: reached its petal ring (within the ring's extent) since it last flew inbound (the head skip's precondition)
    std::uint8_t unburst = 0;   // B.2 review B1: re-keyed by a head skip: out of its entry burst (the distance order no longer applies)
    std::uint8_t esc_in10 = 0;  // B.6: an escort whose leader launched has come within kEscortSwitchM of the gate since res_until was set
    std::uint32_t burst_key = 0; // B.2 review B1: the lowest key of its entry burst (committed, no reservation, entered within kBurstMs of
                                 // another such member; 0 none), computed per frame by burst_keys()
    std::uint16_t go_arounds = 0;
    std::uint32_t key = 0, res_until = 0, pred = 0; // pred: the released ship ahead (its launch frees the funnel)
    std::uint32_t res_o8_ms = 0; // B.3 item 4: the leader's order-8 stamp the reservation started from (res_until is refreshed)
    // B.3 item 2: the KC's last warn of this member (site B's warned frame: the obstacle list named its own gate) and a 0x12 episode
    // kept through (the funnel state stays; avoid_ms: the episode's first kept observation, keep_ms: its last)
    std::uint32_t warn_ms = 0, avoid_ms = 0, keep_ms = 0;
    std::uint8_t warn_gate = 0, warn_stage = 0;
    std::uint32_t release_ms = 0, plan_x_ms = 0, plan_launch_ms = 0, o8_ms = 0, feas_ms = 0, x_ms = 0, x_pred_ms = 0;
    std::uint32_t head_ms = 0, infeas_ms = 0; // the tie-break: head with its slot due since; in its window without a feasible candidate since
    float enter_d = 0;          // the distance to X at the entry (the burst order, D.8)
    float zx = 0;               // X along F from G (m, < 0): the grid line's, or the class estimate before order 4
    float q[2] = {0, 0}, l4[2] = {0, 0}, a_lat[2] = {0, 0}; // Q, Lambda_4 and A (R, U), m
    float v_sb = -1.0f;         // the controller's speed for this ship (m/s; < 0 none): the hold, the funnel's shaping and feasibility
    float v_feas = -1.0f;       // the funnel's feasible speed (the predictor's highest candidate within P2's acceptance)
    // the order-0 gap (Run161): the last state read on the role's steered order (committed 4 / 5, escort 0x14 / 0x15) and its plan
    std::uint32_t steered_ms = 0;
    Plan last_plan = Plan::drift;
    std::uint32_t t_rel_ms = 0; // B.8 (diagnostics): the holder's lap target at the last sequencer run (game ms; 0 none or not holding)
    // B.5 (ship_gate_ctl_live_core.h): proven live this frame (the frame boundary's live_prove), the last proof (game ms), the sector
    // of the snapshot that listed it, and the record class code site B read in the update (the member row's class; 0xffffffff unread)
    std::uint8_t proven = 0;
    std::uint32_t live_ms = 0, live_sector = 0, code = 0xffffffffu;
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
    // stage B (B.1): the ring radius from the widest member, the petals' holders, the funnel (the last released ship and the end of
    // the previous one), the through-slot model (our last slot release; a capital launch here blocks our side until slot_block_ms)
    float rho = kRingMinM, w_max = 0;
    std::uint32_t petal_ship[kPetals] = {};
    std::uint32_t funnel_ship = 0, funnel_end_ms = 0, slot_block_ms = 0, last_arrival_seen = 0;
    std::uint32_t sb_go_arounds = 0, sb_releases = 0, sb_last_ms = 0, sb_tail_key = 0;
    std::uint32_t sb_skips = 0, seq_row_ms = 0; // B.2: head skips at this gate; the DLL's last ship_gate_ctl_seq row (one per game second)
    std::uint32_t o0_ms[kMembers][kMembers] = {}; // review F5: the last order-0 pair row per pair (one per second; the DLL's --debug)
    std::uint32_t pass_ms[kMembers][kMembers] = {}; // Run161: the last E pass row per pair (one per second; the DLL's --debug)
    // B.3 item 1: the SETA slack of the board's freshness bounds (note_step): the larger of this and the previous frame's game step
    // (clamped to kStepClampMs)
    std::uint32_t step_ms = 0;
};
struct Board {
    Gate g[kGates];
    unsigned full_gates = 0, full_members = 0;
    std::uint32_t step_now_ms = 0, step_cur_ms = 0, step_prev_ms = 0; // B.3 item 1: the frame (game ms) the steps belong to
};
// B.3 item 1 (Run163: the funnel's gate row failed closed on the two SETA frames whose game step was 2,000 and 1,540 ms, beyond the
// 1 s state age), with review F5: every liveness and freshness bound of the controller is nominal + the last game-time frame step
// (the larger of this and the previous frame's: a state read before a long step is two frames old on the frame after it; clamped to
// kStepClampMs). A record within its nominal age before the step stays within the bound across it; one already stale before the step
// stays stale. The verdict-class bounds (nominal below kVerdictCapMs: the state, the certificate, the decisions, the rollouts' offset,
// the commit, the cap) never accept anything older than kVerdictCapMs: a hitch beyond that fails closed until the next read and
// decision. The windows of kVerdictCapMs and more (the order-0 gap, the occupancy's decision) take the step on top. A state written
// this frame (age 0) is always within any bound.
constexpr std::uint32_t kStepClampMs = 5000;
constexpr std::uint32_t kVerdictCapMs = 2000;
__attribute__((always_inline)) inline std::uint32_t age_bound(const Gate& g, std::uint32_t nominal) noexcept {
    const std::uint32_t b = nominal + g.step_ms;
    return nominal >= kVerdictCapMs || b <= kVerdictCapMs ? b : kVerdictCapMs;
}
// The frame's game-time step ([root+0x714] in the DLL; the fixture's dt), noted once per game-time frame from any site (site B, E,
// the frame end): the first call of a new frame shifts the steps and copies the slack to every gate.
inline void note_step(Board& b, std::uint32_t now, std::uint32_t step_ms) noexcept {
    if (now == b.step_now_ms) return;
    if (step_ms > kStepClampMs) step_ms = kStepClampMs;
    b.step_now_ms = now;
    b.step_prev_ms = b.step_cur_ms;
    b.step_cur_ms = step_ms;
    const std::uint32_t last = b.step_cur_ms > b.step_prev_ms ? b.step_cur_ms : b.step_prev_ms;
    for (Gate& g : b.g) g.step_ms = last;
}

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
// A member takes part in pairs: a pair role and a live state at most kStateMaxAgeMs old (B.3: the SETA-aware age_bound; age 0, a
// state written this frame, is always live).
__attribute__((always_inline)) inline bool live(const Gate& g, const Member& m, std::uint32_t now) noexcept {
    return m.ship && pair_role(m.role) && m.st.ms && now - m.st.ms <= age_bound(g, kStateMaxAgeMs);
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
        g->step_ms = b.step_cur_ms > b.step_prev_ms ? b.step_cur_ms : b.step_prev_ms; // B.3 item 1: this frame's slack (note_step)
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
        if (lapsed) m.release_ms = 0; // B.2 review N1: a re-commit past the last stretch is a rejoin only within the 30 s window
        m.enter_ms = now ? now : 1;
        if (m.role == Role::evicted || m.role == Role::none) {
            clear_pairs(g, unsigned(i));
            m.n = 0;
            m.decided_ms = 0;
            m.avoid_ms = m.keep_ms = 0; // review F9: no 0x12 episode or its gap carried into a new membership
            m.warn_gate = 0;
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
        m.avoid_ms = m.keep_ms = 0; // review F9
        m.warn_gate = 0;
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
    if (order == sc::kOrderAvoid) return Why::evict; // 0x12 evicts any member (an escort with a current formation memory too: review F12)
    if (m.role == Role::committed) {
        // the script's order-0 re-issue gap (<= kGapMs after the last order-4 / 5 read; order 0 zeroes f+0x224) and the P2 hand-over on
        // order 0 / 0x1f with the target zeroed keep the member (Run161: eviction only on 0x12)
        const std::uint32_t gap = age_bound(g, kGapMs);
        if (order == sc::kOrderNone && (target == g.gate || !target) && m.steered_ms && now - m.steered_ms <= gap) return Why::count;
        // B.3 item 2: the KC's order 0 after a 0x12 episode the member was kept through (keep_through_avoid) keeps it like the gap
        if (order == sc::kOrderNone && (target == g.gate || !target) && m.keep_ms && now - m.keep_ms <= gap) return Why::count;
        if (!target && (order == sc::kOrderNone || order == sc::kOrderRotate) && m.handover_ms && now - m.handover_ms <= kHandoverMaxMs)
            return Why::count;
        if (target != g.gate) return Why::target;
        if (order == sc::kOrderFlyBefore || order == sc::kOrderDock) // (order 5 right after the hand-over: its first leaf call commits)
            return now - m.commit_ms > age_bound(g, kCommitFreshMs) && !(order == sc::kOrderDock && m.handover_ms && now - m.handover_ms <= kHandoverMaxMs)
                       ? Why::expire : Why::count;
        if (order == sc::kOrderNone || order == sc::kOrderRotate) return !m.handover_ms || now - m.handover_ms > kHandoverMaxMs ? Why::order : Why::count;
        return order == sc::kOrderWarpAway ? Why::count : Why::order;
    }
    if (m.role == Role::escort) {
        // stage B (D.3, D.12): the leader launched (res_until set at its order 8); the escort keeps its petal and its reserved number
        // until the reservation lapses. B.3 item 4 (Run163: the script's order 4 at +53 s and +81 s, B.2's 60 s lapsed at +60 s): on any
        // order but a dock or launch leg to another target (it docks elsewhere or leaves the sector); sb_frame refreshes res_until
        // (kReserveMs) while the escort is on 0x15 to the gate or within kReserveKeepM of it; its own order 4 commits it
        if (m.key_res && m.res_until && std::int32_t(m.res_until - now) >= 0 &&
            !((order == sc::kOrderDock || order == sc::kOrderWarpAway) && target != g.gate))
            return Why::count;
        const int li = find(g, m.leader);
        if (li < 0 || g.m[li].role != Role::committed || !live(g, g.m[li], now)) return Why::leader;
        if (formation_current) return Why::count; // Run161: the formation memory current => a member, whatever the live order
        if (order == sc::kOrderEscort || order == sc::kOrderFollow) return target != m.leader ? Why::leader : Why::count;
        if (order == sc::kOrderNone) return m.steered_ms && now - m.steered_ms <= age_bound(g, kGapMs) ? Why::count : Why::order; // the script's waits
        return Why::order;
    }
    if (m.role == Role::arrival) return now - m.launch_ms > kArrivalWindowMs ? Why::window : Why::count;
    return Why::count;
}
__attribute__((always_inline)) inline bool in_front(const Gate& g, const float x[3]) noexcept {
    const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
    return sc::dot3(d, g.F) < 0.0f;
}

// ---- stage B: the geometry (D.2.2, D.2.4) ------------------------------------------------------------------------------------------
// Gate-frame components of p - G: (R, U, F).
__attribute__((always_inline)) inline void sb_local(const Gate& g, const float p[3], float* r, float* u, float* z) noexcept {
    const float d[3] = {p[0] - g.G[0], p[1] - g.G[1], p[2] - g.G[2]};
    *r = sc::dot3(d, g.R);
    *u = sc::dot3(d, g.U);
    *z = sc::dot3(d, g.F);
}
// The pass point Q (D.2.2) for a hull (r_s, w, L) at a grid lateral A_lat on a gate of radius r_gate: the point nearest A_lat in
// {|Q| <= l_max, Q_U <= -l_min}, l_max = r_s + v_t / 10 - 100 m (the order-5 trigger at the ramp's floor speed v_t = v_max / 2,
// tf 1, the tracking allowance), l_min = w + (L - w) sin 3 deg - (0.389 r_gate - r_small - 50 m) (the hull's cross-section below the
// upper small-arrival fan); a grid point on the axis column (A_R = 0) is biased toward +R (D.5: the small ships take -R) within the
// chord limit. An empty window (l_min > l_max) keeps Q = (0, -l_max). Lambda_4 (the order-4 final line) is A_lat + (Q - A_lat) clamped
// to the chord limit c = 0.75 r_gate - 150 m: one straight line from the funnel through X to the trigger when |Q - A_lat| <= c (the
// rest, an order-5 lateral S). Returns false for a non-finite input.
// terran: the gate is a Terran ring (D.2.2's table: l + w <= h r_gate - 50 m, h = 0.663); when no l satisfies it, Q is the axis (the
// small side spawn covers small arrivals there).
inline bool q_axis(float r_gate, const float a_lat[2], float q[2], float l4[2]) noexcept;
inline bool q_point(float r_s, float w, float L, float v_max, float r_gate, const float a_lat[2], float q[2], float l4[2], bool terran = false) noexcept {
    if (!(r_s > 0.0f) || !(r_gate > 0.0f) || !sc::finite_f(a_lat[0]) || !sc::finite_f(a_lat[1]) || !sc::finite_f(v_max)) return false;
    const float ww = w > 0.0f && w <= r_s ? w : r_s, LL = L > r_s ? L : r_s;
    float lmax = r_s + 0.05f * (v_max > 0.0f ? v_max : 0.0f) - kTrackM;
    if (lmax < 0.0f) lmax = 0.0f;
    if (terran) {
        const float ring = kTerranHoleR * r_gate - ww - kTerranMarginM;
        if (!(ring > 0.0f)) return q_axis(r_gate, a_lat, q, l4);
        if (ring < lmax) lmax = ring;
    }
    float lmin = ww + (LL - ww) * kSin3 - (kFanLowR * r_gate - kSmallMaxM - kFanMarginM);
    if (lmin < 0.0f) lmin = 0.0f;
    const float c = sc::kGateCrossFraction * r_gate - kChordSlackM;
    if (lmin > lmax) {
        q[0] = 0.0f;
        q[1] = -lmax;
    } else {
        // the nearest point of the convex set: the point itself, its projection on the disc (if below the line), on the line (if
        // inside the disc), or a corner
        const float px = a_lat[0], py = a_lat[1];
        const float dn = sc::sqrt_ss(px * px + py * py);
        if (dn <= lmax && py <= -lmin) {
            q[0] = px;
            q[1] = py;
        } else {
            float best = 1e30f, bx = 0.0f, by = -lmin;
            auto take = [&](float x, float y) {
                const float d = (x - px) * (x - px) + (y - py) * (y - py);
                if (d < best) {
                    best = d;
                    bx = x;
                    by = y;
                }
            };
            if (dn > 1e-3f) {
                const float x = px * lmax / dn, y = py * lmax / dn;
                if (y <= -lmin) take(x, y);
            }
            const float half = sc::sqrt_ss(lmax * lmax - lmin * lmin);
            if (px >= -half && px <= half) take(px, -lmin);
            take(half, -lmin);
            take(-half, -lmin);
            q[0] = bx;
            q[1] = by;
        }
        if (sc::fabs_f(a_lat[0]) < 1.0f) { // the axis column: biased toward +R (D.5), within the disc and the chord limit
            const float rest = lmax * lmax - q[1] * q[1];
            float bias = 0.5f * sc::sqrt_ss(rest > 0.0f ? rest : 0.0f);
            const float dy = q[1] - a_lat[1], room = c * c - dy * dy;
            const float lim = room > 0.0f ? sc::sqrt_ss(room) : 0.0f;
            if (bias > lim) bias = lim;
            if (bias > q[0]) q[0] = bias;
        }
    }
    const float dx = q[0] - a_lat[0], dy = q[1] - a_lat[1], d = sc::sqrt_ss(dx * dx + dy * dy);
    const float k = d > c && d > 1e-3f ? c / d : 1.0f;
    l4[0] = a_lat[0] + dx * k;
    l4[1] = a_lat[1] + dy * k;
    return sc::finite_f(q[0]) && sc::finite_f(q[1]) && sc::finite_f(l4[0]) && sc::finite_f(l4[1]);
}
// The axis variant of the pass point (B.1, measured 2026-10-13 and rejected: about 2.6 s less cross -> launch per ship, about 80 s more
// span in scenario T): Q on the axis (l = 0), Lambda_4 = A_lat + (Q - A_lat) within the chord limit, as q_point. Selected only with
// kQAxisDefault (off) for a gate that is not Terran and a hull the side spawn can clear (w <= 0.68 r_gate); kept for a later experiment.
constexpr bool kQAxisDefault = false;
inline bool q_axis(float r_gate, const float a_lat[2], float q[2], float l4[2]) noexcept {
    if (!(r_gate > 0.0f) || !sc::finite_f(a_lat[0]) || !sc::finite_f(a_lat[1])) return false;
    q[0] = q[1] = 0.0f;
    const float c = sc::kGateCrossFraction * r_gate - kChordSlackM, dx = -a_lat[0], dy = -a_lat[1], d = sc::sqrt_ss(dx * dx + dy * dy);
    const float k = d > c && d > 1e-3f ? c / d : 1.0f;
    l4[0] = a_lat[0] + dx * k;
    l4[1] = a_lat[1] + dy * k;
    return true;
}
// X along F of a hull r_s at a gate (the KC's grid: A at -2 (r_gate + r_s) F, X 0.75 r_gate before it): the estimate before order 4.
__attribute__((always_inline)) inline float zx_estimate(float r_gate, float r_s) noexcept {
    return -(2.0f * (r_gate + r_s) + sc::kGateCrossFraction * r_gate);
}
// The final lines in world space: Lambda_4 (order 4: through the point at the grid X's depth, along F) and Lambda_5 (order 5: through
// Q on the gate plane, along F).
__attribute__((always_inline)) inline void lambda4(const Gate& g, const Member& m, float X[3]) noexcept {
    for (unsigned i = 0; i < 3; ++i) X[i] = g.G[i] + m.zx * g.F[i] + m.l4[0] * g.R[i] + m.l4[1] * g.U[i];
}
__attribute__((always_inline)) inline void lambda5(const Gate& g, const Member& m, float X[3]) noexcept {
    for (unsigned i = 0; i < 3; ++i) X[i] = g.G[i] + m.q[0] * g.R[i] + m.q[1] * g.U[i];
}
// The look-ahead of the predictor's pursuit (plant_step, the capital lead-in): max(0.75 v, 2 v / W, 0.3 r_gate).
__attribute__((always_inline)) inline float pursuit_look(float v, float W, float r_gate) noexcept {
    const float vv = v > 0.0f ? v : 0.0f;
    float look = 0.75f * vv;
    if (W > 1e-4f && 2.0f * vv / W > look) look = 2.0f * vv / W;
    if (sc::kGateLookMinR * r_gate > look) look = sc::kGateLookMinR * r_gate;
    return look;
}
// The carrot on a line (X, h) for the pursuit at speed v, never beyond a_along from X.
__attribute__((always_inline)) inline void line_carrot(const float X[3], const float h[3], float a_along, float r_gate, float W, const float x[3],
                                                       float v, float P[3]) noexcept {
    const float d[3] = {x[0] - X[0], x[1] - X[1], x[2] - X[2]};
    float along = sc::dot3(d, h) + pursuit_look(v, W, r_gate);
    if (along > a_along) along = a_along;
    for (unsigned i = 0; i < 3; ++i) P[i] = X[i] + h[i] * along;
}
// ---- the petals (D.2.4): petal p = ring k (p / 7), index i (p % 7) at phi = -120 deg + 40 deg i from -U toward +R; the racetrack
// lies in the plane of F and e = sin phi R - cos phi U: the inner straight heads +F at b = rho, the outer one -F at rho + 2 R_h, the
// front turn's foremost point at zx - kLeadM - k kRingStepM (a, b: the components of p - G along F and e).
constexpr float kRingStepM = kPetalSh + 2.0f * kPetalRh + 2.0f * 1500.0f + 300.0f; // D_ring = S_h + 2 R_h + 2 L_max + 300 m (L_max 1.5 km)
constexpr float kLap = 2.0f * kPetalSh + 2.0f * sc::kPi * kPetalRh;
// B.6 item 3: the release window on the lap (petal_at's arc length): the back turn's last 60 deg, the inner straight and the front turn's
// first 60 deg (3,594 m of 9,283 m), wrapping across s = 0
constexpr float kWinTurnM = kPetalRh * sc::kPi / 3.0f;
constexpr float kWinOpenS = kLap - kWinTurnM, kWinCloseS = kPetalSh + kWinTurnM;
struct PetalGeom {
    float e[3] = {0, 0, 0}, n[3] = {0, 0, 0}; // the radial direction and the plane's normal F x e
    float rho = 0, a1 = 0, a0 = 0;            // the ring radius; the straights' front and back ends along F
};
inline void petal_geom(const Gate& g, unsigned p, float zx, PetalGeom* pg) noexcept {
    const unsigned k = p / kPetalsPerRing, i = p % kPetalsPerRing;
    float sp, cp;
    sc::sincos_f(kPetalPhi0 + kPetalDphi * float(i), &sp, &cp);
    for (unsigned c = 0; c < 3; ++c) pg->e[c] = sp * g.R[c] - cp * g.U[c];
    pg->n[0] = g.F[1] * pg->e[2] - g.F[2] * pg->e[1];
    pg->n[1] = g.F[2] * pg->e[0] - g.F[0] * pg->e[2];
    pg->n[2] = g.F[0] * pg->e[1] - g.F[1] * pg->e[0];
    pg->rho = g.rho;
    pg->a1 = zx - kLeadM - float(k) * kRingStepM - kPetalRh;
    pg->a0 = pg->a1 - kPetalSh;
}
// B.6 (review 1): the petal passes inside the script's order-4 radius: its closest point to G (on the front turn, toward G from the turn's
// centre (a1, rho + R_h)) within 9.49 km, the 0x15 approach's own threshold (0.9 kEscortSwitchM^2). Ring 0 in practice; rings 1-2 lap
// wholly beyond it (a1 about -13.7 km and farther).
__attribute__((always_inline)) inline bool petal_near(const PetalGeom& pg) noexcept {
    const float bc = pg.rho + kPetalRh, dc = sc::sqrt_ss(pg.a1 * pg.a1 + bc * bc) - kPetalRh;
    return dc * dc <= kEscortSwitchM * kEscortSwitchM * 0.9f;
}
// B.7 (Run167): an escort's stage on its petal: the arc length (petal_at's s) of the first point, from the inner straight's back end
// (s = 0) through the inner straight and the front turn, within kEscortStageM of G (inside the script's order-4 radius with 500 m to
// spare); -1 on a far petal (!petal_near: no stage; the release window and the 0x15 approach as in B.6) and when that point lies at or
// past the release window's close (the front turn's 60-deg point: lap_speed then keeps B.6's window). Closed form, no engine reads:
// on the straight (b = rho, a in [a0, a1]) the points within D satisfy |a| <= sqrt(D^2 - rho^2); on the front turn (centre
// c = (a1, rho + R_h), angle t in [0, pi] from the straight's end) |p|^2 = |c|^2 + R_h^2 - 2 R_h |c| cos(t - phi), phi = atan2(-a1, rho + R_h).
constexpr float kEscortStageM = kEscortSwitchM - 500.0f;
__attribute__((always_inline)) inline float escort_stage_s(const PetalGeom& pg) noexcept {
    if (!petal_near(pg)) return -1.0f;
    const float D2 = kEscortStageM * kEscortStageM, r2 = pg.rho * pg.rho;
    if (r2 <= D2) {
        const float h = sc::sqrt_ss(D2 - r2), lo = pg.a0 > -h ? pg.a0 : -h, hi = pg.a1 < h ? pg.a1 : h;
        if (lo <= hi) return lo - pg.a0;
    }
    const float R = kPetalRh, bc = pg.rho + R, c2 = pg.a1 * pg.a1 + bc * bc, c = sc::sqrt_ss(c2);
    float q = (c2 + R * R - D2) / (2.0f * R * c);
    if (q > 1.0f) return -1.0f; // no turn point within D (petal_near's 9.49 km rules this out)
    if (q < -1.0f) q = -1.0f;
    const float phi = sc::atan2_f(-pg.a1, bc), al = sc::atan2_f(sc::sqrt_ss(1.0f - q * q), q);
    const float t = phi - al > 0.0f ? phi - al : 0.0f;
    if (t > phi + al || t > sc::kPi) return -1.0f;
    const float s = kPetalSh + R * t;
    return s < kWinCloseS ? s : -1.0f; // review F1: a stage past the window's 60-deg close is no stage (B.6's window: the dwell stays)
}
// The racetrack's point at arc length s (wrapped into [0, lap)): (a, b) and its unit direction (da, db). s = 0 at the inner straight's
// back end; the inner straight, the front turn, the outer straight, the back turn.
inline void petal_at(const PetalGeom& pg, float s, float* a, float* b, float* da, float* db) noexcept {
    for (int i = 0; i < 4 && s >= kLap; ++i) s -= kLap;
    for (int i = 0; i < 4 && s < 0.0f; ++i) s += kLap;
    const float R = kPetalRh, S = kPetalSh, half = sc::kPi * R;
    if (s < S) {
        *a = pg.a0 + s;
        *b = pg.rho;
        *da = 1.0f;
        *db = 0.0f;
    } else if (s < S + half) {
        float st, ct;
        sc::sincos_f((s - S) / R, &st, &ct);
        *a = pg.a1 + R * st;
        *b = pg.rho + R - R * ct;
        *da = ct;
        *db = st;
    } else if (s < 2.0f * S + half) {
        *a = pg.a1 - (s - S - half);
        *b = pg.rho + 2.0f * R;
        *da = -1.0f;
        *db = 0.0f;
    } else {
        float st, ct;
        sc::sincos_f((s - 2.0f * S - half) / R, &st, &ct);
        *a = pg.a0 - R * st;
        *b = pg.rho + R + R * ct;
        *da = -ct;
        *db = -st;
    }
}
// The arc length of the racetrack point nearest (a, b).
__attribute__((always_inline)) inline float petal_project(const PetalGeom& pg, float a, float b) noexcept {
    const float R = kPetalRh, S = kPetalSh, half = sc::kPi * R, bc = pg.rho + R;
    if (a >= pg.a0 && a <= pg.a1) return b <= bc ? a - pg.a0 : S + half + (pg.a1 - a);
    if (a > pg.a1) {
        float t = sc::atan2_f(a - pg.a1, bc - b); // 0 at the inner end, pi at the outer end
        if (t < 0.0f) t = 0.0f;
        return S + R * t;
    }
    float t = sc::atan2_f(pg.a0 - a, b - bc); // 0 at the outer end, pi at the inner end
    if (t < 0.0f) t = 0.0f;
    return 2.0f * S + half + R * t;
}
// B.8 (Run168): the ship at x on its petal: the arc length of its projection (returned) and the join distance *d_join, the 3D distance from
// x to the racetrack point at that arc (in-plane, along F and e, plus the out-of-plane offset along n). Board state only.
constexpr float kJoinOnM = 300.0f; // on its track: the lap law's dwell applies
__attribute__((always_inline)) inline float petal_join(const Gate& g, const PetalGeom& pg, const float x[3], float* d_join) noexcept {
    const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
    const float a = sc::dot3(d, g.F), b = sc::dot3(d, pg.e), o = sc::dot3(d, pg.n), s = petal_project(pg, a, b);
    float ta, tb, da, db;
    petal_at(pg, s, &ta, &tb, &da, &db);
    *d_join = sc::sqrt_ss((a - ta) * (a - ta) + (b - tb) * (b - tb) + o * o);
    return s;
}
// The petal's carrot for a ship at (a, b) flying at v: the racetrack point the pursuit's look-ahead beyond the nearest one.
__attribute__((always_inline)) inline void petal_carrot(const PetalGeom& pg, float r_gate, float W, float a, float b, float v, float* ta,
                                                        float* tb) noexcept {
    float da, db;
    petal_at(pg, petal_project(pg, a, b) + pursuit_look(v, W, r_gate), ta, tb, &da, &db);
}
// B.2 (Run162 item 2): an inbound ship's carrot on its petal's inner line (+F at b = rho). Farther than kInboundLatM from that line, the
// line's point kInboundBackM beyond the petal's back turn (behind the ring: the ship joins its own line without crossing the other
// petals; at its own depth plus the look-ahead when already ahead of that point); on the line, the petal's entry (the inner straight's
// back end a0) while the look-ahead falls short of it, then the straight up to its front end a1. From far behind the ship flies a straight
// line and arrives heading +F, in its release window. (a, b, off: the ship's components along F, e and the plane's normal.)
constexpr float kInboundLatM = 1000.0f, kInboundBackM = 1500.0f;
__attribute__((always_inline)) inline void inbound_carrot(const PetalGeom& pg, float r_gate, float W, float a, float b, float off, float v, float* ta,
                                                          float* tb) noexcept {
    float t = a + pursuit_look(v, W, r_gate);
    const float lb = b - pg.rho, back = pg.a0 - kPetalRh - kInboundBackM;
    if (lb * lb + off * off > kInboundLatM * kInboundLatM) {
        if (a <= back) t = back;
    } else if (t < pg.a0)
        t = pg.a0;
    if (t > pg.a1) t = pg.a1;
    *ta = t;
    *tb = pg.rho;
}
// The distance from G of the farthest point of a member's petal (its back turn's outer side; ring 0's index 0 without a petal).
__attribute__((always_inline)) inline float ring_extent(const Gate& g, const Member& m) noexcept {
    PetalGeom pg;
    petal_geom(g, m.petal >= 0 ? unsigned(m.petal) : 0u, m.zx, &pg);
    const float bc = pg.rho + kPetalRh;
    return sc::sqrt_ss(pg.a0 * pg.a0 + bc * bc) + kPetalRh;
}
// The plant step of sc::plant_step toward an explicit target (tx, ty) in the plane (the petal's carrot); the same arithmetic.
__attribute__((always_inline)) inline void plant_step_to(const sc::JoinParams& p, float v_des, float dt, float k_lag, float tx, float ty, float& x,
                                                         float& y, float& psi, float& upsi, float& w, float& v) noexcept {
    const float theta = sc::wrap_pi(sc::atan2_f(ty - y, tx - x) - psi);
    const float a = sc::fabs_f(theta);
    float w_cmd = sc::sqrt_ss(p.alpha * a);
    if (a / sc::kTau < w_cmd) w_cmd = a / sc::kTau;
    if (p.W < w_cmd) w_cmd = p.W;
    float e_wr = w_cmd * w_cmd / p.alpha + w_cmd * sc::kJoinServoTickS;
    if (e_wr > a) e_wr = a;
    const float sgn = theta >= 0.0f ? 1.0f : -1.0f;
    float wr = w * sgn;
    if (wr < 0.0f) {
        wr += p.alpha * dt;
        if (wr > 0.0f) wr = 0.0f;
    } else if (e_wr < wr * wr / p.alpha) {
        wr -= p.alpha * dt;
        if (wr < 0.0f) wr = 0.0f;
    } else {
        wr += p.alpha * dt;
        if (wr > p.W) wr = p.W;
    }
    float turn = wr * dt;
    if (turn > 0.0f && turn > a) turn = a;
    psi = sc::wrap_pi(psi + sgn * turn);
    w = sgn * wr;
    if (v < v_des) {
        v += p.a_lin * dt;
        if (v > v_des) v = v_des;
    } else {
        v -= p.a_lin * dt;
        if (v < v_des) v = v_des;
    }
    upsi = sc::wrap_pi(upsi + sc::wrap_pi(psi - upsi) * k_lag);
    float su, cu;
    sc::sincos_f(upsi, &su, &cu);
    x += v * cu * dt;
    y += v * su * dt;
}
// The planar state of a ship in a fixed plane through G spanned by (h, n) (the petal's F and e): (s, y) = the components of x - G,
// psi and upsi the nose's and the velocity's angles from h toward n, w the in-plane body rate (join_plane's projection).
inline bool plane_state(const float G[3], const float h[3], const float n[3], const float x[3], const float rows[3][3], const float w[3],
                        const float u[3], float v, sc::JoinPlane* out) noexcept {
    const float d[3] = {x[0] - G[0], x[1] - G[1], x[2] - G[2]};
    const float psi = sc::atan2_f(sc::dot3(rows[2], n), sc::dot3(rows[2], h));
    const float uu = sc::sqrt_ss(sc::dot3(u, u));
    const float upsi = uu > 1e-3f ? sc::atan2_f(sc::dot3(u, n), sc::dot3(u, h)) : psi;
    float df[3], sp, cp, m[3];
    for (unsigned i = 0; i < 3; ++i) df[i] = -w[0] * rows[0][i] + w[1] * rows[1][i];
    sc::sincos_f(psi, &sp, &cp);
    for (unsigned i = 0; i < 3; ++i) m[i] = -sp * h[i] + cp * n[i];
    out->s = sc::dot3(d, h);
    out->y = sc::dot3(d, n);
    out->psi = psi;
    out->upsi = upsi;
    out->w = sc::dot3(df, m);
    out->v = v > 0.0f ? v : 0.0f;
    return sc::finite_f(out->s) && sc::finite_f(out->y) && sc::finite_f(out->psi) && sc::finite_f(out->upsi) && sc::finite_f(out->w);
}
// The order-5 handler's aligned speed toward the gate origin (section 10.5): vmax at r_gate, vmax / 2 at r_gate / 5, linear between.
__attribute__((always_inline)) inline float dock_ramp(float d, float r_gate, float v_max) noexcept {
    if (d >= r_gate) return v_max;
    if (d <= 0.2f * r_gate) return 0.5f * v_max;
    return 0.5f * v_max + 0.5f * v_max * (d - 0.2f * r_gate) / (0.8f * r_gate);
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
// Whether a yields to b: the lower rank yields; within a rank two funnel members by release (B.6), else the higher sequence number.
inline bool yields_to(const Member& a, const Member& b) noexcept {
    const unsigned ra = rank(a), rb = rank(b);
    if (ra != rb) return ra > rb;
    // B.6: two funnel members go in release order, the later release yields (a head released before an escort that entered earlier and
    // committed later is ahead of it on the final: by entry it would yield to the ship behind it and cannot clear the pair). A member
    // without a release stamp ranks as the later release; two without one, or equal stamps, fall to the sequence order
    if (a.sb == Sb::funnel && b.sb == Sb::funnel && a.release_ms != b.release_ms) {
        if (!a.release_ms || !b.release_ms) return !a.release_ms;
        return std::int32_t(a.release_ms - b.release_ms) > 0;
    }
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
// A member on order 0 inside the script's re-issue gap whose last steered plan is one the rollout can continue.
__attribute__((always_inline)) inline bool gap_plan(const Member& m) noexcept;
// A member rolling out the script's order-0 gap on its last plan (the drift's gap case).
__attribute__((always_inline)) inline bool in_gap(const Member& m) noexcept {
    return m.st.order == sc::kOrderNone && !(m.role == Role::committed && m.handover_ms) && gap_plan(m);
}
// The order-0 gap's expected rest (s): the member brakes at a (order 0: desired 0) until the script re-issues its order, at most 3 s after
// its last steered read for an escort (the template's 2-3 s wait) and 5 s for a committed ship; the rollout then resumes its last plan at
// the plan's speed. 0 outside the gap.
__attribute__((always_inline)) inline float gap_brake_s(const Member& m) noexcept {
    if (!in_gap(m)) return 0.0f;
    const std::uint32_t gap = m.role == Role::escort ? kGapEscortMs : kGapMs, el = m.st.ms - m.steered_ms;
    return el < gap ? float(gap - el) * 0.001f : 0.0f;
}
__attribute__((always_inline)) inline bool gap_plan(const Member& m) noexcept {
    if (!m.steered_ms || m.st.ms - m.steered_ms > kGapMs) return false;
    if (m.last_plan == Plan::petal) return m.petal >= 0;
    if (m.last_plan == Plan::line) return m.line_ok != 0;
    return m.last_plan == Plan::follow;
}
inline Plan plan_of(const Member& m) noexcept;
// The state read's bookkeeping (site B, the decision): a read on the role's steered order stamps steered_ms and records its plan.
inline void note_state(Member& m) noexcept {
    const std::uint16_t o = m.st.order;
    const bool steered = (m.role == Role::committed && (o == sc::kOrderFlyBefore || o == sc::kOrderDock)) ||
                         (m.role == Role::escort && (o == sc::kOrderEscort || o == sc::kOrderFollow));
    if (!steered) return;
    m.steered_ms = m.st.ms;
    m.last_plan = plan_of(m);
}
inline Plan plan_of(const Member& m) noexcept {
    const std::uint16_t o = m.st.order;
    if (o == sc::kOrderWarpIn) return Plan::warp_in;
    if (o == sc::kOrderUndock) return Plan::exit;
    if (o == sc::kOrderWarpAway) return Plan::launch;
    if (m.role == Role::committed) {
        if (o == sc::kOrderFlyBefore) {
            if ((m.sb == Sb::hold || m.sb == Sb::inbound) && m.petal >= 0) return Plan::petal; // stage B: holding (or entering) its petal
            return m.line_ok ? Plan::line : Plan::drift;               // the funnel flies Lambda_4 (line_of)
        }
        if (o == sc::kOrderDock) return m.sb == Sb::dock && m.q_ok ? Plan::line5 : Plan::dock;
        if (o == sc::kOrderRotate) return Plan::hold;
    }
    // order 0 (Run161): the P2 hand-over brakes (order 0 sets the desired speed 0; 0x1f holds); a committed member or an escort in the
    // script's re-issue gap (<= kGapMs after its last steered read) rolls out on its last steered plan at its current speed (an escort:
    // its petal, else following its leader; a committed ship: its petal or its funnel line); anything else brakes along the nose
    if (o == sc::kOrderNone) {
        if (m.role == Role::committed && m.handover_ms) return Plan::brake;
        if ((m.role == Role::committed || m.role == Role::escort) && gap_plan(m)) return m.last_plan;
        return Plan::brake;
    }
    if (m.role == Role::escort && (o == sc::kOrderEscort || o == sc::kOrderFollow))
        return (m.sb == Sb::hold || m.sb == Sb::inbound) && m.petal >= 0 ? Plan::petal : Plan::follow;
    return Plan::drift;
}
// The plan's speed: the module's own speed before a yield cap when known and fresh, else the desired speed site B left.
inline float plan_speed(const Gate& g, const Member& m) noexcept {
    const std::uint32_t cap_age = age_bound(g, kCapMaxAgeMs); // B.3 item 1
    if (m.st.order == sc::kOrderNone) { // the order-0 gap: the last plan's speed (the rollout brakes for the gap's rest first)
        if (m.v_plan > 0.0f && m.v_plan_ms && m.st.ms - m.v_plan_ms <= kGapMs + cap_age) return m.v_plan;
        return m.st.v > 0.0f ? m.st.v : 0.0f;
    }
    if (m.v_plan > 0.0f && m.v_plan_ms && m.st.ms - m.v_plan_ms <= cap_age) return m.v_plan;
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
inline bool line_plane(const Member& m, const float X[3], const float h[3], sc::JoinPlane* st, float n[3]) noexcept {
    sc::GateGeometry g;
    for (unsigned i = 0; i < 3; ++i) {
        g.X[i] = X[i];
        g.h[i] = h[i];
        g.A[i] = X[i] + sc::kGateCrossFraction * m.params.r_gate * h[i];
    }
    g.r = m.params.r_gate;
    if (!sc::join_plane(g, m.st.x, m.st.rows, m.st.w, m.st.u, m.st.v, st)) return false;
    const float dx[3] = {m.st.x[0] - X[0], m.st.x[1] - X[1], m.st.x[2] - X[2]};
    const float s = sc::dot3(dx, h);
    for (unsigned i = 0; i < 3; ++i) n[i] = dx[i] - s * h[i];
    float e = sc::sqrt_ss(sc::dot3(n, n));
    if (e > 1.0f) {
        for (unsigned i = 0; i < 3; ++i) n[i] /= e;
        return true;
    }
    const float fh = sc::dot3(m.st.rows[2], h); // on the line: the plane of the nose (join_plane's choice)
    for (unsigned i = 0; i < 3; ++i) n[i] = m.st.rows[2][i] - fh * h[i];
    e = sc::sqrt_ss(sc::dot3(n, n));
    if (e > 1e-4f) {
        for (unsigned i = 0; i < 3; ++i) n[i] /= e;
    } else {
        for (unsigned i = 0; i < 3; ++i) n[i] = m.st.rows[0][i];
    }
    return true;
}
// The order-4 line a member's rollout flies: the grid line (X, h), or in stage B's funnel Lambda_4 (through the point at X's depth,
// along F).
inline void line_of(const Member& m, const Gate& g, float X[3], float h[3]) noexcept {
    if (m.sb == Sb::funnel && m.q_ok) {
        lambda4(g, m, X);
        for (unsigned i = 0; i < 3; ++i) h[i] = g.F[i];
        return;
    }
    for (unsigned i = 0; i < 3; ++i) {
        X[i] = m.X[i];
        h[i] = m.h[i];
    }
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
        float nn[3], LX[3], Lh[3];
        line_of(m, g, LX, Lh); // the grid line, or (stage B, in the funnel) Lambda_4
        if (!line_plane(m, LX, Lh, &st, nn)) break;
        const float v_des = v_cmd >= 0.0f ? v_cmd : plan_speed(g, m);
        const float t_brake = gap_brake_s(m); // the order-0 gap: desired 0 for its expected rest, then the plan (Opus question 5)
        const float k_lag = sc::one_minus_exp_neg(kStepS / sc::kSlipTau), a_along = sc::kGateCrossFraction * m.params.r_gate;
        float x = st.s, y = st.y, psi = st.psi, upsi = st.upsi, w = st.w, v = st.v;
        // the nose: the live nose turned about the plane's normal h x n by the plant's in-plane turn (the plant has no out-of-plane
        // axis; the out-of-plane part of the live nose is kept, not projected away: the capsule at sample 0 is the true hull)
        float ax[3] = {Lh[1] * nn[2] - Lh[2] * nn[1], Lh[2] * nn[0] - Lh[0] * nn[2], Lh[0] * nn[1] - Lh[1] * nn[0]};
        {
            const float an = sc::sqrt_ss(sc::dot3(ax, ax));
            for (float& q : ax) q = an > 1e-6f ? q / an : 0.0f;
        }
        const float psi0 = psi;
        bool past = false;
        float dir[3] = {0, 0, 0};
        for (unsigned i = 0; i < 3; ++i) c[i] = LX[i] + x * Lh[i] + y * nn[i];
        for (unsigned k = 0; k < kSamples; ++k) {
            for (unsigned step = 0; k && step < 2; ++step) {
                if (!past && x < 0.0f && m.params.W > 1e-4f && m.params.alpha > 1e-6f) {
                    sc::plant_step(m.params, float(k) * kSampleS < t_brake ? 0.0f : v_des, kStepS, k_lag, a_along, x, y, psi, upsi, w, v);
                    for (unsigned i = 0; i < 3; ++i) c[i] = LX[i] + x * Lh[i] + y * nn[i];
                } else { // past the crossing plane (P2): the hand-over brakes on its last velocity (order 0, desired 0; 0x1f in place)
                    if (!past) {
                        past = true;
                        float su, cu;
                        sc::sincos_f(upsi, &su, &cu);
                        for (unsigned i = 0; i < 3; ++i) dir[i] = cu * Lh[i] + su * nn[i];
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
    case Plan::petal: { // stage B: the pursuit of the petal's racetrack carrot in its plane (F, e); the out-of-plane offset kept (B.2: an
                        // inbound member pursues its petal's entry and inner straight)
        PetalGeom pg;
        petal_geom(g, unsigned(m.petal), m.zx, &pg);
        sc::JoinPlane st;
        if (!plane_state(g.G, g.F, pg.e, m.st.x, m.st.rows, m.st.w, m.st.u, m.st.v, &st) || !(m.params.W > 1e-4f) || !(m.params.alpha > 1e-6f))
            break;
        float off = (m.st.x[0] - g.G[0]) * pg.n[0] + (m.st.x[1] - g.G[1]) * pg.n[1] + (m.st.x[2] - g.G[2]) * pg.n[2];
        const float v_des = v_cmd >= 0.0f ? v_cmd : plan_speed(g, m);
        const float t_brake = gap_brake_s(m);
        const float k_lag = sc::one_minus_exp_neg(kStepS / sc::kSlipTau);
        float x = st.s, y = st.y, psi = st.psi, upsi = st.upsi, w = st.w, v = st.v;
        const float psi0 = psi;
        for (unsigned k = 0; k < kSamples; ++k) {
            for (unsigned step = 0; k && step < 2; ++step) {
                float ta, tb;
                const bool entering = m.sb == Sb::inbound || !m.ringed;
                if (entering) inbound_carrot(pg, m.params.r_gate, m.params.W, x, y, off, v, &ta, &tb);
                else petal_carrot(pg, m.params.r_gate, m.params.W, x, y, v, &ta, &tb);
                // review N4: sb_steer's carrot lies in the petal's plane, so an entering ship's out-of-plane offset decays along its path:
                // the 3-D line to the carrot shrinks it with the in-plane distance (the rollout predicts the flown path)
                const float d0 = sc::sqrt_ss((ta - x) * (ta - x) + (tb - y) * (tb - y) + off * off);
                plant_step_to(m.params, float(k) * kSampleS < t_brake ? 0.0f : v_des, kStepS, k_lag, ta, tb, x, y, psi, upsi, w, v);
                if (entering && d0 > 1.0f) {
                    const float travel = v * kStepS;
                    off *= travel < d0 ? 1.0f - travel / d0 : 0.0f;
                }
            }
            for (unsigned i = 0; i < 3; ++i) c[i] = g.G[i] + x * g.F[i] + y * pg.e[i] + off * pg.n[i];
            float sp, cp; // the live nose turned about the plane's normal F x e by the plant's in-plane turn (as the line plan)
            sc::sincos_f(psi - psi0, &sp, &cp);
            const float* ax = pg.n;
            const float axf[3] = {ax[1] * f0[2] - ax[2] * f0[1], ax[2] * f0[0] - ax[0] * f0[2], ax[0] * f0[1] - ax[1] * f0[0]};
            const float af = sc::dot3(ax, f0);
            const float f[3] = {f0[0] * cp + axf[0] * sp + ax[0] * af * (1.0f - cp), f0[1] * cp + axf[1] * sp + ax[1] * af * (1.0f - cp),
                                f0[2] * cp + axf[2] * sp + ax[2] * af * (1.0f - cp)};
            put(out[n++], c, f);
            if (v > *vmax) *vmax = v;
        }
        break;
    }
    case Plan::line5: { // stage B, order 5: the pursuit of Lambda_5 (through Q along F) at the order's aligned ramp (site B's write)
                        // until the trigger |v| / 10 + r around the gate origin, then order 8 along +F for 0.9 s
        float LX[3], nn[3];
        lambda5(g, m, LX);
        sc::JoinPlane st;
        if (!line_plane(m, LX, g.F, &st, nn) || !(m.params.W > 1e-4f) || !(m.params.alpha > 1e-6f)) break;
        const float k_lag = sc::one_minus_exp_neg(kStepS / sc::kSlipTau), a_along = 2.0f * m.params.r_gate;
        float x = st.s, y = st.y, psi = st.psi, upsi = st.upsi, w = st.w, v = st.v;
        float ax[3] = {g.F[1] * nn[2] - g.F[2] * nn[1], g.F[2] * nn[0] - g.F[0] * nn[2], g.F[0] * nn[1] - g.F[1] * nn[0]};
        {
            const float an = sc::sqrt_ss(sc::dot3(ax, ax));
            for (float& q : ax) q = an > 1e-6f ? q / an : 0.0f;
        }
        const float psi0 = psi;
        float t_launch = -1.0f, v0 = 0.0f;
        for (unsigned i = 0; i < 3; ++i) c[i] = LX[i] + x * g.F[i] + y * nn[i];
        for (unsigned k = 0; k < kSamples; ++k) {
            const float t = float(k) * kSampleS;
            if (t_launch >= 0.0f) { // order 8: 900 ms along +F, then gone
                // then the ship is gone: the rollout holds the launch's end point beyond the plane (no hull is there; the pair's overlap stays
                // complete, review F13)
                const float tl = t - t_launch > 0.9f ? 0.9f : t - t_launch;
                const float tm = tl * 1000.0f, z = v0 * tl + kWarpInK * tm * tm;
                float p[3];
                for (unsigned i = 0; i < 3; ++i) p[i] = c[i] + z * g.F[i];
                put(out[n++], p, f0);
                if (2697.0f / 0.9f + v0 > *vmax) *vmax = 2697.0f / 0.9f + v0;
                continue;
            }
            for (unsigned step = 0; k && step < 2; ++step) {
                const float dd[3] = {c[0] - g.G[0], c[1] - g.G[1], c[2] - g.G[2]};
                const float d = sc::sqrt_ss(sc::dot3(dd, dd));
                sc::plant_step(m.params, dock_ramp(d, m.params.r_gate, m.params.v_max), kStepS, k_lag, a_along, x, y, psi, upsi, w, v);
                for (unsigned i = 0; i < 3; ++i) c[i] = LX[i] + x * g.F[i] + y * nn[i];
            }
            float sp, cp;
            sc::sincos_f(psi - psi0, &sp, &cp);
            const float axf[3] = {ax[1] * f0[2] - ax[2] * f0[1], ax[2] * f0[0] - ax[0] * f0[2], ax[0] * f0[1] - ax[1] * f0[0]};
            const float af = sc::dot3(ax, f0);
            const float f[3] = {f0[0] * cp + axf[0] * sp + ax[0] * af * (1.0f - cp), f0[1] * cp + axf[1] * sp + ax[1] * af * (1.0f - cp),
                                f0[2] * cp + axf[2] * sp + ax[2] * af * (1.0f - cp)};
            put(out[n++], c, f);
            if (v > *vmax) *vmax = v;
            const float dd[3] = {c[0] - g.G[0], c[1] - g.G[1], c[2] - g.G[2]};
            if (k && sc::dot3(dd, dd) <= (v / 10.0f + m.r) * (v / 10.0f + m.r)) { // the trigger: P4, one order-0 frame, order 8
                t_launch = t;
                v0 = v;
            }
        }
        break;
    }
    case Plan::follow: {
        const int li = find(g, m.leader);
        const Member* L = li >= 0 && live(g, g.m[li], m.st.ms) && g.m[li].n ? &g.m[li] : nullptr;
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
        for (unsigned k = 0; k < kSamples; ++k) { // then gone: the rollout holds the end point (review F13, as line5)
            const float t = float(k) * kSampleS > 0.9f ? 0.9f : float(k) * kSampleS;
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
// B.3: kLim > 0 compiles the sweep for a constant time-offset bound, which bounds the sample offset for the compiler (measured on the
// host: 73 ns per broad-only sweep against 130 ns compiled for a runtime bound); kLim 0 takes the runtime max_dms (a long step).
template <std::int32_t kLim> // kLim > 0: a compile-time bound; 0: the runtime max_dms
inline Sweep sweep_impl(const Sample* sa, unsigned na, std::uint32_t a_ms, float a_vmax, float La, float wa, const Sample* sb, unsigned nb,
                        std::uint32_t b_ms, float b_vmax, float Lb, float wb, unsigned min_samples, bool ends, float drift,
                        std::uint32_t max_dms) noexcept {
    Sweep out;
    const std::int32_t dms = std::int32_t(a_ms - b_ms), lim = kLim > 0 ? kLim : std::int32_t(max_dms);
    if (dms > lim || dms < -lim) { // the rollouts are too far apart in time
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
    // stage B: a rollout that ends because its ship leaves the sector (order 8's 0.9 s, then gone) is complete: one paired sample suffices
    if (i1 - i0 < (ends ? 1 : std::int32_t(min_samples < kMinOverlapSamples ? kMinOverlapSamples : min_samples))) { // too short: no
        out.stale = true;                                                                                  // certificate (fail closed)
        return out;
    }
    const Sample* B = sb + koff;
    // the drift charges each side from its own rollout's t0: k (t + |t0_A - t0_B|), t from A's t0
    float base = kMarginM + resid * b_vmax + drift * (kSampleS * float(i0 > 0 ? i0 : 0) + float(dms >= 0 ? dms : -dms) * 0.001f);
    for (std::int32_t i = i0; i < i1; ++i, base += drift * kSampleS) {
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
inline Sweep sweep(const Sample* sa, unsigned na, std::uint32_t a_ms, float a_vmax, float La, float wa, const Sample* sb, unsigned nb,
                   std::uint32_t b_ms, float b_vmax, float Lb, float wb, unsigned min_samples = kMinOverlapSamples, bool ends = false,
                   float drift = kDriftMs) noexcept {
    return sweep_impl<std::int32_t(kRolloutMaxAgeMs)>(sa, na, a_ms, a_vmax, La, wa, sb, nb, b_ms, b_vmax, Lb, wb, min_samples, ends, drift, 0);
}
constexpr std::uint32_t kSweepNormalMs = kRolloutMaxAgeMs + 250; // the constant-bound sweep's reach (normal steps up to 250 ms)
inline Sweep sweep_seta(const Sample* sa, unsigned na, std::uint32_t a_ms, float a_vmax, float La, float wa, const Sample* sb, unsigned nb,
                        std::uint32_t b_ms, float b_vmax, float Lb, float wb, unsigned min_samples, bool ends, float drift,
                        std::uint32_t max_dms) noexcept {
    return sweep_impl<0>(sa, na, a_ms, a_vmax, La, wa, sb, nb, b_ms, b_vmax, Lb, wb, min_samples, ends, drift, max_dms);
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
        if (j == i || !live(g, g.m[j], now)) continue;
        const Member& B = g.m[j];
        ++st->sweeps;
        // fail closed: no samples on either side, a rollout more than 1 s from the other, too short an overlap -> no verdict (none:
        // the engine's warn and the sphere avoidance), not a conflict to resolve
        // a rollout that ends because order 8 is live on the leaving ship is complete with one paired sample; one that ends at the order-5
        // trigger (line5) keeps the 4-sample minimum but not the warn reach (review F13)
        const bool launching = (B.n < kSamples && plan_of(B) == Plan::launch) || (n < kSamples && plan_of(A) == Plan::launch);
        const bool triggering = (B.n < kSamples && plan_of(B) == Plan::line5) || (n < kSamples && plan_of(A) == Plan::line5);
        // (a member in the order-0 gap brakes in its rollout for the gap's expected rest: gap_brake_s; the drift k stays kDriftMs)
        const float drift = kDriftMs;
        // B.3: the rollouts' time offset bound is SETA-aware; the nominal bound keeps the constant sweep (sweep_impl)
        const unsigned min_s = triggering && !launching ? kMinOverlapSamples : warn_reach_samples(A, B);
        // review F5: the bound is nominal + step on every frame; at normal steps (bound <= kSweepNormalMs) the exact bound is checked here
        // and the sweep compiled for the constant kSweepNormalMs runs (its own check never fires then)
        const std::uint32_t max_dms = age_bound(g, kRolloutMaxAgeMs);
        const std::int32_t dms = std::int32_t(A.st.ms - B.roll_ms);
        const bool apart = dms > std::int32_t(max_dms) || dms < -std::int32_t(max_dms);
        const Sweep w = !(B.n && n) || apart ? Sweep{false, 255, 1e30f, 0, true}
                        : max_dms <= kSweepNormalMs
                            ? sweep_impl<std::int32_t(kSweepNormalMs)>(s, n, A.st.ms, vmax, A.L, A.w, B.s, B.n, B.roll_ms, B.v_roll_max, B.L, B.w,
                                                                        min_s, launching, drift, 0)
                            : sweep_seta(s, n, A.st.ms, vmax, A.L, A.w, B.s, B.n, B.roll_ms, B.v_roll_max, B.L, B.w, min_s, launching, drift, max_dms);
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
    const float v = plan_speed(g, Y) > 0.0f ? plan_speed(g, Y) : Y.st.v;
    const float cand[3] = {kYieldFrac * v, kYieldMidMs, kYieldFloorMs};
    const float floor = plan_of(Y) == Plan::petal ? kPetalVmin : kYieldFloorMs; // stage B: a petal holder never below 25 m/s (D.7 item 6)
    Sample tmp[kSamples];
    for (float c : cand) {
        if (!(c >= floor) || !(c < v)) continue;
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
    note_state(A); // Run161: the order-0 gap's steered stamp and plan
    std::uint8_t before[kMembers];
    for (unsigned j = 0; j < kMembers; ++j) before[j] = g.v[i][j];
    if (!live(g, A, now)) {
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
__attribute__((always_inline)) inline bool due(const Gate& g, const Member& m, std::uint32_t now) noexcept {
    return live(g, m, now) && (!m.decided_ms || (now - m.decided_ms >= kDecideMs && m.st.ms != m.roll_ms));
}
// Whether the frame may start another decision (the frame's bound): the first always; then within kDecisionsPerFrame and while a
// decision with every yield still fits the rollout budget.
// B.2 review F2: the members that occupy the funnel (committed in funnel, hand-over, order 5 or order 8) are decided with priority: the
// funnel's occupancy needs their decision at most kOccupyDecisionMs (5 s) old, and the round robin of a full board under SETA can take
// longer (96 members at 18 decisions per game second: 5.3 s).
__attribute__((always_inline)) inline bool sb_priority(const Member& m) noexcept {
    return m.ship && m.role == Role::committed && (m.sb == Sb::funnel || m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch);
}
// The frame's decision order (the DLL's ctl_frame, the fixture's frame end): the due priority members first, then the due others in round
// robin from the cursor. idx: k = gate * kMembers + member (kGates * kMembers entries at most); *n_prio: the leading priority entries (the
// cursor advances only on the others).
inline unsigned decide_order(const Board& b, unsigned cursor, std::uint32_t now, std::uint16_t* idx, unsigned* n_prio) noexcept {
    constexpr unsigned total = kGates * kMembers;
    unsigned n = 0;
    for (unsigned k = 0; k < total; ++k) {
        const Gate& g = b.g[k / kMembers];
        const unsigned i = k % kMembers;
        if (g.gate && i < g.n && sb_priority(g.m[i]) && due(g, g.m[i], now)) idx[n++] = std::uint16_t(k);
    }
    *n_prio = n;
    for (unsigned q = 0; q < total; ++q) {
        const unsigned k = (cursor + q) % total;
        const Gate& g = b.g[k / kMembers];
        const unsigned i = k % kMembers;
        if (g.gate && i < g.n && !sb_priority(g.m[i]) && due(g, g.m[i], now)) idx[n++] = std::uint16_t(k);
    }
    return n;
}
__attribute__((always_inline)) inline bool frame_room(unsigned decisions, unsigned rollouts) noexcept {
    return !decisions || (decisions < kDecisionsPerFrame && rollouts + kMaxRolloutsPerDecision <= kRolloutsPerFrame);
}
// B.3 (user decision 2026-10-13): the frame's budget follows game time, not the frame count: decisions = max(kDecisionsPerFrame,
// ceil(members x kRefreshPerS x the frame's game step)), rollouts in the same proportion (kRolloutsPerFrame per kDecisionsPerFrame);
// at 60 fps normal play the budget stays 6 / 14, at SETA x10 and 30 fps with 24 members it is 32 / 75. The DLL's ctl_frame also stops
// at kFrameCeilingUs (1 ms) of measured time (ctl_budget_cut); the funnel's occupants are still decided first (decide_order).
constexpr unsigned kRefreshPerS = 4;             // a member's decision record is refreshed 4 times per game second (250 ms)
constexpr std::uint32_t kRefreshMs = 1000 / kRefreshPerS;
constexpr unsigned kFrameCeilingUs = 1000;       // the measured-time ceiling of one frame's decisions (DLL; 3 % of a 33 ms SETA frame)
struct FrameBudget {
    unsigned decisions = kDecisionsPerFrame, rollouts = kRolloutsPerFrame;
};
inline unsigned members_live(const Board& b) noexcept {
    unsigned n = 0;
    for (const Gate& g : b.g)
        if (g.gate)
            for (unsigned i = 0; i < g.n; ++i) n += g.m[i].ship && pair_role(g.m[i].role) ? 1u : 0u;
    return n;
}
inline FrameBudget frame_budget(unsigned members, std::uint32_t step_ms) noexcept {
    FrameBudget fb;
    if (step_ms > kStepClampMs) step_ms = kStepClampMs;
    const std::uint32_t need = (members * kRefreshPerS * step_ms + 999u) / 1000u; // ceil(members x 4 / s x step)
    if (need > fb.decisions) {
        fb.decisions = need;
        fb.rollouts = (need * kRolloutsPerFrame + kDecisionsPerFrame - 1u) / kDecisionsPerFrame;
    }
    return fb;
}
__attribute__((always_inline)) inline bool frame_room(unsigned decisions, unsigned rollouts, const FrameBudget& fb) noexcept {
    return !decisions || (decisions < fb.decisions && rollouts + kMaxRolloutsPerDecision <= fb.rollouts);
}

// B.4 item 2 (Run164: the three holders untraced, all 8 ship_trace slots held by small ships): under the traffic key the last
// kTraceMemberSlots of the trace_max slots are reserved for board members (pair roles); a member searches them first, then the shared
// slots [first, max - reserve). A non-member: a capital from slot 0, a non-capital from slot 1, up to max - reserve. With fewer than
// kTraceMemberSlots + 2 slots nothing is reserved. Key off: amendment 17's rule (slot 0 for one non-escort capital, others 1..max).
constexpr unsigned kTraceMemberSlots = 2;
struct TraceRange {
    unsigned first = 0, end = 0;       // the shared slots
    unsigned res_first = 0, res_end = 0; // the reserved slots (a member's first search; empty for a non-member)
};
inline TraceRange trace_slot_range(unsigned max, bool traffic, bool capital, bool escort, bool member) noexcept {
    TraceRange r;
    if (!traffic) {
        r.first = capital && !escort ? 0u : 1u;
        r.end = capital && !escort ? 1u : max;
        return r;
    }
    const unsigned reserve = max >= kTraceMemberSlots + 2 ? kTraceMemberSlots : 0u;
    r.first = capital || member ? 0u : 1u;
    r.end = max - reserve;
    if (member) {
        r.res_first = max - reserve;
        r.res_end = max;
    }
    return r;
}

// ---- reads for site E and the avoidance ---------------------------------------------------------------------------------------
// The pair verdict for (i, j) as site E takes it: clear only while at most kCertMaxAgeMs old.
inline PairV pair_verdict(const Gate& g, unsigned i, unsigned j, std::uint32_t now, std::uint32_t max_age_ms) noexcept {
    if (i >= g.n || j >= g.n || i == j || !live(g, g.m[i], now) || !live(g, g.m[j], now)) return PairV::none;
    const PairV v = PairV(g.v[i][j]);
    const std::uint32_t dec = age_bound(g, kDecisionMaxAgeMs); // B.3 item 1: the SETA-aware bounds
    if (v == PairV::clear && (now - g.vms[i][j] > age_bound(g, max_age_ms) || !g.m[i].decided_ms || !g.m[j].decided_ms ||
                              now - g.m[i].decided_ms > dec || now - g.m[j].decided_ms > dec))
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
// Why site E passes a member pair (Run161; the reason of every pass_ctl, counted): certified (suppress_ctl), none (no verdict: a sweep
// that failed closed), unresolved (a conflict handed back), stale (the clear verdict older than 250 ms), starved (a member's decisions
// more than 1 s old), on_0x12, rows (a forward row not a unit vector), order0 (no certificate while one of the pair is on order 0).
enum class EWhy : std::uint8_t { certified = 0, none, unresolved, stale, starved, on_0x12, rows, order0, not_member, count };
inline const char* ewhy_name(EWhy w) noexcept {
    static const char* const n[unsigned(EWhy::count)] = {"certified", "none", "unresolved", "stale", "starved", "on_0x12", "rows", "order0",
                                                         "not_member"};
    return unsigned(w) < unsigned(EWhy::count) ? n[unsigned(w)] : "?";
}
inline EWhy e_reason(const Gate& g, unsigned i, unsigned j, std::uint32_t now, bool fa_ok, bool fb_ok, std::uint16_t a_order,
                     std::uint16_t b_order) noexcept {
    if (a_order == sc::kOrderAvoid || b_order == sc::kOrderAvoid) return EWhy::on_0x12;
    if (!fa_ok || !fb_ok) return EWhy::rows;
    if (i >= g.n || j >= g.n || !live(g, g.m[i], now) || !live(g, g.m[j], now)) return EWhy::not_member;
    const PairV v = PairV(g.v[i][j]);
    if (v == PairV::clear) {
        const std::uint32_t dec = age_bound(g, kDecisionMaxAgeMs);
        if (!g.m[i].decided_ms || !g.m[j].decided_ms || now - g.m[i].decided_ms > dec || now - g.m[j].decided_ms > dec)
            return EWhy::starved;
        return now - g.vms[i][j] > age_bound(g, kCertMaxAgeMs) ? EWhy::stale : EWhy::certified;
    }
    if (a_order == sc::kOrderNone || b_order == sc::kOrderNone) return EWhy::order0;
    return v == PairV::unresolved ? EWhy::unresolved : EWhy::none;
}
__attribute__((always_inline)) inline bool unit_row(const float f[3]) noexcept {
    const float n2 = sc::dot3(f, f);
    return sc::finite_f(n2) && n2 >= 0.9f && n2 <= 1.1f;
}
// B.2 item 3 (Run162: 3 of 4 funnel releases evicted by the engine's 0x12 against their own gate): site E's gate row for a stage-B member.
// A (committed, Q known) in funnel / handover / dock / launch on B's board, B = its own gate as the live target, or the target zeroed on
// order 0 / 0x1f (the script's gap, the P2 hand-over), whatever the order; under the path guard dist(G, segment[x, P]) >= r_gate + r_A +
// 0.25 r_gate with P = Lambda_4's point at X's depth (the funnel's point). True: suppress_gate.
constexpr float kSbGateGuardR = 0.25f; // ship_capital_lane_core.h kCapitalGuardR (the capital rows' path guard)
// Review N6: fails closed unless the member is live, its radius is positive and the position read is finite and within reach of the
// member's last state read ((v_max + 50 m/s) per second of its age + 500 m). The hull radius is the member's own (validated at entry).
// B.3 item 2: also a member in hold or inbound within kSbGateNearR r_gate of its gate (a ship re-committed near the gate after an
// eviction), on the same orders and under the same path guard. B.3 item 6: the reason of a pass (the DLL's E row: gate_row=).
constexpr float kSbGateNearR = 6.0f;
enum class GateRow : std::uint8_t { suppress = 0, not_member, stale, unreadable, reach, stage, order, guard, count };
inline const char* gate_row_name(GateRow w) noexcept {
    static const char* const n[unsigned(GateRow::count)] = {"suppress", "not_member", "stale", "unreadable", "reach", "stage", "order", "guard"};
    return unsigned(w) < unsigned(GateRow::count) ? n[unsigned(w)] : "?";
}
inline GateRow sb_gate_why(const Gate& g, const Member& m, std::uint32_t gate, std::uint16_t order, std::uint32_t target, const float x[3],
                           std::uint32_t now) noexcept {
    if (!gate || g.gate != gate || m.role != Role::committed || !m.q_ok || !(g.r_gate > 0.0f)) return GateRow::not_member;
    // re-review R1: the row's own liveness, uncapped: the 1 s state age + the last step + site B's read interval (the read's reach below
    // bounds the position; the 2 s cap of age_bound stays on the certificates and the exclusion)
    if (!m.st.ms || now - m.st.ms > kStateMaxAgeMs + g.step_ms + kStateMs) return GateRow::stale;
    const float r_a = m.r;
    if (!(r_a > 0.0f) || !sc::finite_f(r_a) || !sc::finite_f(x[0]) || !sc::finite_f(x[1]) || !sc::finite_f(x[2])) return GateRow::unreadable;
    {
        const float dx[3] = {x[0] - m.st.x[0], x[1] - m.st.x[1], x[2] - m.st.x[2]};
        const float reach = ((m.params.v_max > 0.0f ? m.params.v_max : 0.0f) + 50.0f) * float(now - m.st.ms) * 0.001f + 500.0f;
        if (!(sc::dot3(dx, dx) <= reach * reach)) return GateRow::reach;
    }
    if (!(m.sb == Sb::funnel || m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch)) {
        if (!(m.sb == Sb::hold || m.sb == Sb::inbound)) return GateRow::stage;
        const float dg[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]}, near = kSbGateNearR * g.r_gate;
        if (!(sc::dot3(dg, dg) <= near * near)) return GateRow::stage;
    }
    // review F3: never on the engine's own avoid order (a member kept through a 0x12 keeps its board state, not the suppression)
    if (order == sc::kOrderAvoid || !(target == gate || (!target && (order == sc::kOrderNone || order == sc::kOrderRotate)))) return GateRow::order;
    float P[3];
    lambda4(g, m, P);
    const float ap[3] = {g.G[0] - x[0], g.G[1] - x[1], g.G[2] - x[2]}, ab[3] = {P[0] - x[0], P[1] - x[1], P[2] - x[2]};
    const float l2 = sc::dot3(ab, ab);
    float t = l2 > 1e-6f ? sc::dot3(ap, ab) / l2 : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    const float d[3] = {ap[0] - t * ab[0], ap[1] - t * ab[1], ap[2] - t * ab[2]};
    const float dist = sc::sqrt_ss(sc::dot3(d, d));
    return sc::finite_f(dist) && dist >= g.r_gate + r_a + kSbGateGuardR * g.r_gate ? GateRow::suppress : GateRow::guard;
}
inline bool sb_gate_row(const Gate& g, const Member& m, std::uint32_t gate, std::uint16_t order, std::uint32_t target, const float x[3],
                        std::uint32_t now) noexcept {
    return sb_gate_why(g, m, gate, order, target, x, now) == GateRow::suppress;
}
// Review N8: site E's stage-B gate row as the DLL's ctl_e_fields reads it (the fixture's own-gate model calls the same): the membership
// bitmap, the board of A, B its gate, the object id; then sb_gate_row on the live position read (x_ok: the read succeeded).
inline bool e_gate_row(Board& b, const Index& ix, std::uint32_t a, std::uint32_t id_a, std::uint32_t gate, std::uint16_t order,
                       std::uint32_t target, const float x[3], bool x_ok, std::uint32_t now, GateRow* why = nullptr) noexcept {
    if (why) *why = GateRow::not_member;
    if (!a || !ix.maybe(a)) return false;
    int i = -1;
    const Gate* g = locate(b, a, &i);
    if (!g || i < 0 || g->gate != gate || g->m[i].id != id_a) return false;
    if (!x_ok) {
        if (why) *why = GateRow::unreadable;
        return false;
    }
    const GateRow w = sb_gate_why(*g, g->m[i], gate, order, target, x, now);
    if (why) *why = w;
    return w == GateRow::suppress;
}
// B.3 item 2: the KC's warn of a member on the frame its CollisionWarn ran (site B; the warn bits set, the order not yet 0x12): own_gate
// = the obstacle list held the member's own gate and nothing else (the caller's test), and the member's stage then.
inline void note_warn(Gate& g, Member& m, bool own_gate, std::uint32_t now) noexcept {
    (void)g;
    m.warn_ms = now ? now : 1;
    m.warn_gate = own_gate ? 1 : 0;
    m.warn_stage = std::uint8_t(m.sb);
}
// B.3 item 2 (Run163: the funnel escort evicted twice by the engine's 0x12 against its own gate on the SETA frames our row should have
// suppressed): a 0x12 is not an eviction when the KC's last warn of the member (within the SETA-aware 1 s before the episode) named its
// own gate while the member was in funnel or hand-over; the member keeps its role and funnel state through the episode (at most
// kAvoidKeepMaxMs; the order 0 after it keeps it like the script's gap: validity). *first: the episode's first kept observation (the
// caller counts ctl_evict_ignored_gate once per episode).
constexpr std::uint32_t kAvoidKeepMaxMs = 60000;
inline bool keep_through_avoid(const Gate& g, Member& m, std::uint32_t now, bool* first) noexcept {
    *first = false;
    if (m.role != Role::committed) return false;
    if (m.avoid_ms) { // an episode already kept: to its end, within the bound
        if (now - m.avoid_ms > kAvoidKeepMaxMs) return false;
        m.keep_ms = now ? now : 1;
        return true;
    }
    const Sb at = Sb(m.warn_stage);
    if (!m.warn_gate || !m.warn_ms || now - m.warn_ms > age_bound(g, kDecisionMaxAgeMs) || !(at == Sb::funnel || at == Sb::handover) ||
        !(m.sb == Sb::funnel || m.sb == Sb::handover))
        return false;
    m.avoid_ms = m.keep_ms = now ? now : 1;
    *first = true;
    return true;
}
// The episode is over (any order but 0x12 read at site B). Review F6: a kept episode refreshes the commit stamp, so the KC's order 4
// after it does not expire the member (its first order-4 decision commits again).
__attribute__((always_inline)) inline void avoid_over(Member& m, std::uint32_t now) noexcept {
    if (m.avoid_ms) m.commit_ms = now ? now : 1;
    m.avoid_ms = 0;
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

// ---- stage B: the sequencer, the funnel and the steering (D.2.3-D.2.6, D.3, D.8, D.12; gate-traffic-controller.md B.1) -------------
// Once per frame per gate (sb_frame, the frame boundary): the keys, Q and the final lines, the stages from the live orders, the petals,
// the funnel's release, speed shaping and feasibility, the go-around, the holders' lap speeds. Per decision (sb_steer, site A): the
// leaf point and site B's speed of a member the controller flies (a holder or an escort in its petal, the funnel, the order-5 leg).
struct SbEvent {
    std::uint32_t ship = 0, key = 0;
    std::uint8_t from = 0, to = 0, why = 0, petal = 0xff;
};
constexpr unsigned kSbEvents = 16;
// B.2 item 5: the sequencer's state at the frame (the DLL's ship_gate_ctl_seq row, one per gate per game second while a holder exists)
struct SbSeq {
    std::uint32_t cand = 0, key = 0, busy_by = 0; // the candidate (first in sequence) and the member keeping the funnel busy
    std::int32_t free_ms = 0, block_ms = 0;        // t_free - now; the slot block's rest
    std::uint8_t role = 0, stage = 0, window = 2, timing = 2, ringed = 0, holders = 0, inbound = 0, pad = 0; // window, timing: 2 = n/a
    float v_feas = -1.0f, dx = -1.0f;
};
struct SbStats {
    bool seq_on = false; // the caller wants seq filled (the DLL's debug tier)
    SbSeq seq;
    unsigned releases = 0, go_arounds = 0, predictions = 0, deferred = 0, x_refused = 0, guarded = 0, seq_skipped = 0, clock_resets = 0;
    unsigned go_arounds_near = 0; // go-arounds decided within 1 km of X
    std::uint32_t x_wait_ms = 0;
    unsigned n_ev = 0, ev_dropped = 0;
    SbEvent ev[kSbEvents];
};
__attribute__((always_inline)) inline void sb_event(SbStats* st, const Member& m, Sb from, Sb to, Why why) noexcept {
    if (!st) return;
    if (st->n_ev >= kSbEvents) {
        ++st->ev_dropped;
        return;
    }
    SbEvent& e = st->ev[st->n_ev++];
    e.ship = m.ship;
    e.key = m.key;
    e.from = std::uint8_t(from);
    e.to = std::uint8_t(to);
    e.why = std::uint8_t(why);
    e.petal = m.petal >= 0 ? std::uint8_t(m.petal) : 0xff;
}
// The sequence order (D.2.5, D.8; B.2 review B1: a strict total order). Committed members without a reservation that entered within
// kBurstMs of each other (chained: a load rebuilds the board at the members' next decisions) form a burst; burst_keys() gives each burst
// member the burst's lowest key. Each member then has the tuple (group, distance, key, ship, sub, own ship): a committed member (group =
// its burst key or its key; distance = enter_d inside a burst, else 0; sub 0); a reserved escort whose committed leader is on the board
// (or one already committed on its own order 4: the reservation holds) takes its leader's tuple with sub = 1 + its offset behind the
// leader's key (so it is never reordered relative to its leader or its sibling escorts); any other
// member its own key as the group. The tuples compare lexicographically.
inline void burst_keys(Gate& g) noexcept {
    for (unsigned i = 0; i < g.n; ++i) g.m[i].burst_key = 0;
    auto eligible = [](const Member& m) { return m.ship && m.role == Role::committed && !m.key_res && !m.unburst && m.key; };
    bool changed = true;
    for (unsigned pass = 0; changed && pass < kMembers; ++pass) { // the chained bursts: the lowest key spreads through each cluster
        changed = false;
        for (unsigned i = 0; i < g.n; ++i) {
            Member& a = g.m[i];
            if (!eligible(a)) continue;
            for (unsigned j = i + 1; j < g.n; ++j) {
                Member& b = g.m[j];
                if (!eligible(b)) continue;
                const std::uint32_t d = a.enter_ms > b.enter_ms ? a.enter_ms - b.enter_ms : b.enter_ms - a.enter_ms;
                if (d > kBurstMs) continue;
                std::uint32_t lo = a.burst_key ? a.burst_key : a.key;
                const std::uint32_t lb = b.burst_key ? b.burst_key : b.key;
                if (lb < lo) lo = lb;
                if (a.burst_key != lo || b.burst_key != lo) {
                    a.burst_key = b.burst_key = lo;
                    changed = true;
                }
            }
        }
    }
}
struct SeqTuple {
    std::uint32_t group;
    float d;
    std::uint32_t key, ship, sub, own;
};
inline SeqTuple seq_tuple(const Gate& g, const Member& m) noexcept {
    const Member* base = &m;
    std::uint32_t sub = 0;
    if (m.key_res && (m.role == Role::escort || m.role == Role::committed)) { // an escort, or one committed on its order 4 (commit_fill
        int li = m.role == Role::escort ? find(g, m.leader) : -1;                // clears its leader: the leader is the unreserved
        if (li < 0 && (m.key & 15u))                                              // committed member keyed m.key & ~15)
            for (unsigned j = 0; j < g.n && li < 0; ++j)
                if (g.m[j].ship && g.m[j].role == Role::committed && !g.m[j].key_res && g.m[j].key == (m.key & ~15u)) li = int(j);
        if (li >= 0 && g.m[li].role == Role::committed && g.m[li].key) {
            base = &g.m[li];
            sub = 1u + (m.key > base->key ? m.key - base->key : 0u);
        }
    }
    const bool burst = base->burst_key != 0;
    const float d = burst ? (sc::finite_f(base->enter_d) ? base->enter_d : 3.4e38f) : 0.0f; // review: a NaN distance ranks last
    return SeqTuple{burst ? base->burst_key : base->key, d, base->key, base->ship, sub, m.ship};
}
inline bool seq_before(const Gate& g, const Member& a, const Member& b) noexcept {
    const SeqTuple x = seq_tuple(g, a), y = seq_tuple(g, b);
    if (x.group != y.group) return x.group < y.group;
    if (x.d != y.d) return x.d < y.d;
    if (x.key != y.key) return x.key < y.key;
    if (x.ship != y.ship) return x.ship < y.ship;
    if (x.sub != y.sub) return x.sub < y.sub;
    return x.own < y.own;
}
// The bearing of a gate-frame lateral (r, u) around the axis: rad from -U toward +R, clamped to the petals' 240 deg.
__attribute__((always_inline)) inline float sb_bearing(float r, float u) noexcept {
    float phi = sc::atan2_f(r, -u);
    if (phi < kPetalPhi0) phi = kPetalPhi0;
    if (phi > -kPetalPhi0) phi = -kPetalPhi0;
    return phi;
}
// The free petal nearest a bearing, ring 0 first (the back of the shortest column nearest it); a petal with a held neighbour costs
// kNeighbourCost (one free petal between holders while the ring has room: their planes 60 deg apart); -1 when every petal is held.
constexpr float kNeighbourCost = 0.35f; // rad (half a petal: a free gap is taken only next to the bearing; a far petal means a
                                       // crossing of the ring)
inline int petal_pick(const Gate& g, float phi) noexcept {
    for (unsigned k = 0; k < kRings; ++k) {
        int best = -1;
        float bd = 1e30f;
        for (unsigned i = 0; i < kPetalsPerRing; ++i) {
            const unsigned p = k * kPetalsPerRing + i;
            if (g.petal_ship[p]) continue;
            const bool nb = (i > 0 && g.petal_ship[p - 1]) || (i + 1 < kPetalsPerRing && g.petal_ship[p + 1]);
            const float d = sc::fabs_f(kPetalPhi0 + kPetalDphi * float(i) - phi) + (nb ? kNeighbourCost : 0.0f);
            if (d < bd) {
                bd = d;
                best = int(p);
            }
        }
        if (best >= 0) return best;
    }
    return -1;
}
// The order-5 leg from a centre distance d_gate at speed v0 to the end of order 8 (s): the distance to the trigger at the aligned
// ramp (v_max, the acceleration from v0 charged), plus order 8's 0.9 s.
__attribute__((always_inline)) inline float leg5_s(const Member& m, float d_gate, float v0) noexcept {
    const float vmax = m.params.v_max > 1.0f ? m.params.v_max : 1.0f, a = m.params.a_lin > 0.1f ? m.params.a_lin : 0.1f;
    float d = d_gate - (m.r + 0.05f * vmax);
    if (d < 0.0f) d = 0.0f;
    float t = d / vmax;
    if (v0 < vmax) t += (vmax - (v0 > 0.0f ? v0 : 0.0f)) * (vmax - (v0 > 0.0f ? v0 : 0.0f)) / (2.0f * a * vmax);
    return t + 0.9f;
}
// The distance to Lambda_4's point at X's depth (m).
__attribute__((always_inline)) inline float dist_x(const Gate& g, const Member& m) noexcept {
    float X[3];
    lambda4(g, m, X);
    const float d[3] = {m.st.x[0] - X[0], m.st.x[1] - X[1], m.st.x[2] - X[2]};
    return sc::sqrt_ss(sc::dot3(d, d));
}
// B.2 item 4 (review N1): past the funnel's last stretch: within kLastStretchR r_gate of Lambda_4's point at X's depth, or beyond it along
// F; either way within kLastStretchR r_gate of Lambda_4 laterally.
__attribute__((always_inline)) inline bool past_last_stretch(const Gate& g, const Member& m) noexcept {
    float X[3];
    lambda4(g, m, X);
    const float d[3] = {m.st.x[0] - X[0], m.st.x[1] - X[1], m.st.x[2] - X[2]};
    const float r = kLastStretchR * g.r_gate, along = sc::dot3(d, g.F), d2 = sc::dot3(d, d);
    return d2 - along * along <= r * r && (along > 0.0f || d2 <= r * r);
}
// B.6 item 3: the release window (without the predictor's horizon): behind the ring-0 front turn's 60-deg point along F (the ring-0
// straights' front end a1 = zx - kLeadM - R_h, plus R_h sin 60 deg; ring k > 0 lies wholly behind it) with the nose within 60 deg of F
__attribute__((always_inline)) inline bool release_window(const Gate& g, const Member& m) noexcept {
    float r, u, z;
    sb_local(g, m.st.x, &r, &u, &z);
    return z <= m.zx - kLeadM - kPetalRh + kReleaseFrontM + 1.0f && sc::dot3(m.st.rows[2], g.F) >= kReleaseNoseCos;
}
// B.2 review N2, N3: the funnel is occupied by another committed member that is live and decided within kOccupyDecisionMs: one released
// and not yet launched (funnel, hand-over, order 5, order 8 until its end). *busy_by: that member. (The release rule's final-straight
// exception is the caller's.)
constexpr std::uint32_t kOccupyDecisionMs = 5000;
__attribute__((always_inline)) inline bool occupant(const Gate& g, const Member& o, std::uint32_t now) noexcept {
    if (o.role != Role::committed || !o.ship || !live(g, o, now) || !o.decided_ms || now - o.decided_ms > age_bound(g, kOccupyDecisionMs)) return false;
    return o.sb == Sb::funnel || o.sb == Sb::handover || o.sb == Sb::dock || (o.sb == Sb::launch && std::int32_t(now - (o.o8_ms + 900u)) < 0);
}
// The predicted end of a released member's order 8 (game ms): the funnel to X at its speed (>= 10 m/s), the hand-over (1 s), the
// order-5 leg and order 8.
inline std::uint32_t eta_launch(const Gate& g, const Member& m, std::uint32_t now) noexcept {
    if (m.sb == Sb::launch) return (m.o8_ms ? m.o8_ms : now) + 900u;
    const float dg[3] = {m.st.x[0] - g.G[0], m.st.x[1] - g.G[1], m.st.x[2] - g.G[2]};
    if (m.sb == Sb::dock || m.sb == Sb::handover)
        return now + std::uint32_t(1000.0f * (leg5_s(m, sc::sqrt_ss(sc::dot3(dg, dg)), m.st.v) + (m.sb == Sb::handover ? 1.0f : 0.0f)));
    if (m.sb == Sb::funnel) {
        float X[3];
        lambda4(g, m, X);
        const float dx[3] = {m.st.x[0] - X[0], m.st.x[1] - X[1], m.st.x[2] - X[2]}, xg[3] = {X[0] - g.G[0], X[1] - g.G[1], X[2] - g.G[2]};
        const float vx = m.v_sb > kYieldFloorMs ? m.v_sb : kYieldFloorMs;
        std::uint32_t t_x = now + std::uint32_t(1000.0f * sc::sqrt_ss(sc::dot3(dx, dx)) / vx);
        if (m.x_pred_ms && std::int32_t(m.x_pred_ms - t_x) > 0) t_x = m.x_pred_ms; // the predicted path at the feasible speed
        return t_x + std::uint32_t(1000.0f * (1.0f + leg5_s(m, sc::sqrt_ss(sc::dot3(xg, xg)), vx)));
    }
    return now;
}
// The funnel's feasibility (D.2.3: the join predictor on Lambda_4 from the member's state): the highest candidate whose prediction
// meets the margins, else the highest within P2's acceptance, else -1 (no feasible candidate: no release, or the go-around).
__attribute__((always_inline)) inline float funnel_feasible(const Gate& g, const Member& m, unsigned* predictions, float* t_x = nullptr) noexcept {
    sc::GateGeometry geo;
    lambda4(g, m, geo.X);
    for (unsigned i = 0; i < 3; ++i) {
        geo.h[i] = g.F[i];
        geo.A[i] = geo.X[i] + sc::kGateCrossFraction * g.r_gate * g.F[i];
    }
    geo.r = g.r_gate;
    sc::JoinPlane pl;
    if (!sc::join_plane(geo, m.st.x, m.st.rows, m.st.w, m.st.u, m.st.v, &pl)) return -1.0f;
    float best1 = -1.0f, t1 = 0.0f;
    for (unsigned i = 0; i < sc::kJoinCandidateCount; ++i) {
        float nose, y;
        if (predictions) ++*predictions;
        sc::JoinPred pd;
        const int gr = sc::join_predict_grade(pl, m.params, sc::kJoinCandidates[i], &nose, &y, &pd);
        const float c = m.params.v_max > 0.0f && sc::kJoinCandidates[i] > m.params.v_max ? m.params.v_max : sc::kJoinCandidates[i];
        if (gr == 2) {
            if (t_x) *t_x = float(pd.steps) * sc::kJoinStepS; // the predicted flight to X's plane at this candidate
            return c;
        }
        if (gr == 1 && best1 < 0.0f) {
            best1 = c;
            t1 = float(pd.steps) * sc::kJoinStepS;
        }
    }
    if (t_x) *t_x = t1;
    return best1;
}
// B.6 (coordinator's amendment, review 1): an escort whose leader launched, after its first pass inside the script's 10 km on a petal
// that passes inside it, with its lap target past: 25 m/s over the whole lap
__attribute__((always_inline)) inline bool escort_settled(const Gate& g, const Member& m, std::uint32_t now, std::uint32_t t_rel) noexcept {
    if (m.role != Role::escort || !m.res_until || !m.esc_in10 || m.petal < 0 || !t_rel || std::int32_t(t_rel - now) > 0) return false;
    PetalGeom pg;
    petal_geom(g, unsigned(m.petal), m.zx, &pg);
    return petal_near(pg);
}
// B.6 item 1: the lap law, the earliest in-window moment at or after the target T (in [25, 60] m/s). s: the ship on the lap [0, lap);
// the window [s_open, s_close] (wrapping across 0 when s_open > s_close); dt_ms: T - now (<= 0: T past, T = now); target false: no
// target (the nominal 40 m/s); in_ok false: the ship off its track is not in its release window whatever its projection (no dwell). ds is
// the arc to the next window opening ahead (in W: the one after this window; B.6 Implementation). B.8 (Run168): d_join, the distance from
// the ship to its track point at s (petal_join), adds to ds in the arrive-on-time, extra-lap and late branches, and the dwell needs the
// ship on its track (d_join <= kJoinOnM); d_join 0 is B.7's law.
__attribute__((always_inline)) inline float lap_law(float s, float s_open, float s_close, float lap, std::int32_t dt_ms, bool target,
                                                    bool in_ok = true, float d_join = 0.0f) noexcept {
    if (!target) return kPetalVnom;
    float wl = s_close - s_open; // |W|
    if (wl < 0.0f) wl += lap;
    float w = s - s_open; // the ship past the opening (mod lap)
    if (w < 0.0f) w += lap;
    if (w >= lap) w -= lap;
    const float T = dt_ms > 0 ? float(dt_ms) * 0.001f : 0.0f, ds = lap - w + d_join; // B.8: the distance to go counts the join
    if (in_ok && d_join <= kJoinOnM && w <= wl && wl - w >= kPetalVmin * T) return kPetalVmin; // in W (on track) and still in W at T at 25 m/s: dwell
    if (ds + wl >= kPetalVmin * T) {                             // the next window reached by T at 25 m/s: at its opening at T (or dwelling)
        if (ds >= kPetalVmax * T) return kPetalVmax;              // ... or late: there at 60 m/s (T past included)
        const float v = ds / T;
        return v < kPetalVmin ? kPetalVmin : v;
    }
    if (ds + lap <= kPetalVmax * T) { // one extra lap fits: the opening after next at T
        const float v = (ds + lap) / T;
        return v < kPetalVmin ? kPetalVmin : v > kPetalVmax ? kPetalVmax : v;
    }
    // no in-window moment at T: the earlier of the next in-window moments, 25 m/s now (through the next window, then 60) against 60 m/s
    const float t25 = (ds + wl) / kPetalVmin + (lap - wl) / kPetalVmax, t60 = (ds + lap) / kPetalVmax;
    return t25 <= t60 ? kPetalVmin : kPetalVmax;
}
// The holder's lap speed toward its release window at t_rel (B.6: the lap law on the petal; nominal without a target). The dwell needs the
// release window itself (release_window: an off-track holder, e.g. one still joining its petal from ahead of it, projects into W). B.8: and
// counts the join distance (escorts and committed heads alike).
__attribute__((always_inline)) inline float lap_speed(const Gate& g, const Member& m, std::uint32_t now, std::uint32_t t_rel) noexcept {
    if (!t_rel || m.petal < 0) return kPetalVnom;
    PetalGeom pg;
    petal_geom(g, unsigned(m.petal), m.zx, &pg);
    float d_join;
    const float s = petal_join(g, pg, m.st.x, &d_join);
    float open = kWinOpenS;
    if (m.role == Role::escort) { // B.7: an escort's window opens at its stage, inside the script's radius (no stage: W kept)
        const float st = escort_stage_s(pg);
        if (st >= 0.0f) open = st;
    }
    return lap_law(s, open, kWinCloseS, kLap, std::int32_t(t_rel - now), true, release_window(g, m), d_join);
}
// An escort's lap target (D.12; B.6 review F1; B.8 lead): its leader's predicted launch + the script's switch once the leader is released,
// its order-8 stamp + the switch once it launched; 0 (no target: the nominal 40 m/s) while the leader holds. B.8 (coordinator's decision):
// kStageLeadMs plus the last game-time frame step (g.step_ms, SETA-aware) earlier, so the escort reaches its stage a few seconds early and
// dwells there at 25 m/s instead of arriving exactly at the first order-4 chance. A past target is now (B.6: dwell in W, else to W at 60).
constexpr std::uint32_t kStageLeadMs = 5000;
__attribute__((always_inline)) inline std::uint32_t escort_target(const Gate& g, const Member& m, std::uint32_t now) noexcept {
    if (m.role != Role::escort || !m.key_res) return 0;
    const int li = find(g, m.leader);
    std::uint32_t t;
    if (li >= 0 && g.m[li].sb != Sb::hold && g.m[li].sb != Sb::inbound && g.m[li].sb != Sb::none) t = eta_launch(g, g.m[li], now) + kEscortSwitchMs;
    else if (m.res_until) t = m.res_o8_ms + kEscortSwitchMs; // review F1: from live time once the leader launched
    else return 0;
    t -= kStageLeadMs + g.step_ms;
    if (std::int32_t(t - now) < 0 || !t) t = now ? now : 1;
    return t;
}
// A holder's speed at its lap target t_rel (sb_frame's hold branch): the lap law (B.6; an escort settled after its leader's launch at 25 m/s);
// B.8 (Run168 departures): a late holder still joining on the inbound carrot (not at its ring) flies the inbound profile on its join
// distance instead of the petals' 60 m/s cap, braking to 60 m/s kInboundLatM from its line and never above its v_max; then the grid
// sphere guard for a committed member on a validated line (it overrides every speed above)
__attribute__((always_inline)) inline float hold_speed(const Gate& g, const Member& m, std::uint32_t now, std::uint32_t t_rel, SbStats* st) noexcept {
    float v = escort_settled(g, m, now, t_rel) ? kPetalVmin : lap_speed(g, m, now, t_rel);
    if (v >= kPetalVmax && t_rel && !m.ringed && m.petal >= 0) { // B.8 (Run168 departures): late and still joining on the inbound carrot
        PetalGeom pg;                                               // (not at its ring): the inbound profile on the join distance, braking
        petal_geom(g, unsigned(m.petal), m.zx, &pg);                // to the petals' top speed kInboundLatM from its line
        float dj;
        petal_join(g, pg, m.st.x, &dj);
        const float a = m.params.a_lin > 0.1f ? m.params.a_lin : 0.1f, vmax = m.params.v_max > kPetalVmax ? m.params.v_max : kPetalVmax;
        const float vj = sc::sqrt_ss(kPetalVmax * kPetalVmax + 2.0f * a * (dj > kInboundLatM ? dj - kInboundLatM : 0.0f));
        v = vj < vmax ? vj : vmax;
    }
    if (m.role == Role::committed && m.line_ok) { // the grid sphere guard: order 4 never ends outside X (D.2.4)
        const float A[3] = {m.X[0] + sc::kGateCrossFraction * g.r_gate * m.h[0], m.X[1] + sc::kGateCrossFraction * g.r_gate * m.h[1],
                            m.X[2] + sc::kGateCrossFraction * g.r_gate * m.h[2]};
        const float dA[3] = {A[0] - m.st.x[0], A[1] - m.st.x[1], A[2] - m.st.x[2]};
        const float d = sc::sqrt_ss(sc::dot3(dA, dA));
        const float a = m.params.a_lin > 0.1f ? m.params.a_lin : 0.1f;
        if (d < sc::kGateCrossFraction * g.r_gate + kGuardM + m.st.v * m.st.v / (2.0f * a) + 200.0f && sc::dot3(dA, m.st.u) > 0.0f) {
            v = kYieldFloorMs;
            if (st) ++st->guarded;
        }
    }
    return v;
}
// One frame of the gate's stage B (the frame boundary, after the decisions). The arena is the board: no allocation.
inline void sb_frame(Gate& g, std::uint32_t now, SbStats* st) noexcept {
    if (!g.gate || !(g.r_gate > 0.0f)) return;
    if (g.sb_last_ms && std::int32_t(now - g.sb_last_ms) < 0) { // a backward game-clock jump (a load): the slot state is stale (review F4)
        g.slot_block_ms = g.funnel_end_ms = g.last_arrival_seen = 0;
        g.funnel_ship = 0;
        g.rho = kRingMinM;
        for (std::uint32_t& p : g.petal_ship) p = 0;
        for (unsigned i = 0; i < g.n; ++i) {
            Member& m = g.m[i];
            m.x_ms = m.o8_ms = m.head_ms = m.feas_ms = m.x_pred_ms = m.release_ms = m.plan_x_ms = m.plan_launch_ms = 0;
            m.refused = 0;
            m.petal = -1;
            m.ringed = 0;
            if (m.sb == Sb::funnel) m.sb = Sb::hold;
        }
        if (st) ++st->clock_resets;
    }
    g.sb_last_ms = now ? now : 1;
    // the ring radius from the widest pair member (it grows while a petal is held, so no holder's petal moves inward)
    bool held = false;
    for (std::uint32_t s : g.petal_ship) held = held || s != 0;
    float w_max = 0.0f;
    for (unsigned i = 0; i < g.n; ++i)
        if ((g.m[i].role == Role::committed || g.m[i].role == Role::escort) && g.m[i].w > w_max) w_max = g.m[i].w;
    float need = (2.0f * w_max + kRingGapM) / (2.0f * kSinHalfDphi);
    if (need < kRingMinM) need = kRingMinM;
    if (!held || need > g.rho) g.rho = need;
    g.w_max = w_max;
    // a capital launch at the gate (0x00444606): our side is refused 60 s after its transfer (the through slot's direction change)
    if (g.cap_arrival_ms && g.cap_arrival_ms != g.last_arrival_seen) {
        g.last_arrival_seen = g.cap_arrival_ms;
        g.slot_block_ms = g.cap_arrival_ms + kSlotArrivalMs;
    }
    // the petals of members that left their petal
    for (unsigned p = 0; p < kPetals; ++p) {
        if (!g.petal_ship[p]) continue;
        const int k = find(g, g.petal_ship[p]);
        if (k < 0 || g.m[k].petal != int(p) || (g.m[k].sb != Sb::hold && g.m[k].sb != Sb::inbound) || !pair_role(g.m[k].role)) g.petal_ship[p] = 0;
    }
    // keys, Q and the final lines, the stages from the live orders, the petals
    for (unsigned i = 0; i < g.n; ++i) {
        Member& m = g.m[i];
        if (!m.ship || !(m.role == Role::committed || m.role == Role::escort)) {
            if (m.ship && m.role != Role::evicted) {
                m.sb = Sb::none;
                m.petal = -1;
            } else if (m.role == Role::evicted) {
                m.sb = Sb::none;
                m.petal = -1;
            }
            continue;
        }
        if (!m.st.ms) continue;
        if (!m.key) {
            m.key = m.seq << 4;
            const float d[3] = {m.st.x[0] - m.X[0], m.st.x[1] - m.X[1], m.st.x[2] - m.X[2]};
            m.enter_d = m.line_ok ? sc::sqrt_ss(sc::dot3(d, d)) : 0.0f;
        }
        const Sb before = m.sb;
        if (m.role == Role::escort) {
            const int li = find(g, m.leader);
            if (!m.key_res && li >= 0 && g.m[li].role == Role::committed && g.m[li].key) { // the reservation, once (D.3)
                const Member& L = g.m[li];
                unsigned rank = 0;
                for (unsigned j = 0; j < g.n; ++j)
                    if (j != i && g.m[j].role == Role::escort && g.m[j].leader == m.leader && g.m[j].seq < m.seq) ++rank;
                m.key = L.key + 1u + (rank < 14u ? rank : 14u);
                m.key_res = 1;
            }
            if (li >= 0 && g.m[li].role == Role::committed && g.m[li].sb == Sb::launch && g.m[li].o8_ms && !m.res_until) {
                m.res_o8_ms = g.m[li].o8_ms;
                m.res_until = g.m[li].o8_ms + kReserveMs; // the leader's launch: the reservation holds kReserveMs (300 s)
                m.esc_in10 = 0;
            }
            if (m.res_until && !m.esc_in10 && m.petal >= 0) { // B.6: the first pass inside the script's order-4 radius (ship-navigation-ai.md
                PetalGeom pg;                                  // section 10.11), on a petal that passes inside it (review 1)
                petal_geom(g, unsigned(m.petal), m.zx, &pg);
                const float d[3] = {m.st.x[0] - g.G[0], m.st.x[1] - g.G[1], m.st.x[2] - g.G[2]};
                if (petal_near(pg) && sc::dot3(d, d) <= kEscortSwitchM * kEscortSwitchM) m.esc_in10 = 1;
            }
            if (m.res_until && m.key_res) { // B.3 item 4: refreshed while on 0x15 to the gate or within kReserveKeepM of it
                const float d[3] = {m.st.x[0] - g.G[0], m.st.x[1] - g.G[1], m.st.x[2] - g.G[2]};
                if ((m.st.order == sc::kOrderFollow && m.st.target == g.gate) || sc::dot3(d, d) <= kReserveKeepM * kReserveKeepM) {
                    const std::uint32_t until = now + kReserveMs;
                    if (std::int32_t(until - m.res_until) > 0) m.res_until = until;
                }
            }
            m.zx = zx_estimate(g.r_gate, m.r);
            if (m.sb != Sb::inbound) m.sb = Sb::hold; // hold or inbound by the distance below (B.2)
        } else {
            if (m.line_ok) {
                float A[3];
                for (unsigned c = 0; c < 3; ++c) A[c] = m.X[c] + sc::kGateCrossFraction * g.r_gate * m.h[c];
                float z;
                sb_local(g, A, &m.a_lat[0], &m.a_lat[1], &z);
                float xr, xu;
                sb_local(g, m.X, &xr, &xu, &m.zx);
                const bool offset = !kQAxisDefault || gate_terran(g.type) || m.w > kSideSpawnMaxWR * g.r_gate; // the offset Q (D.2.2)
                m.q_ok = (offset ? q_point(m.r, m.w, m.L, m.params.v_max, g.r_gate, m.a_lat, m.q, m.l4, gate_terran(g.type))
                                 : q_axis(g.r_gate, m.a_lat, m.q, m.l4)) ? 1 : 0;
            } else if (!m.q_ok)
                m.zx = zx_estimate(g.r_gate, m.r);
            const std::uint16_t o = m.st.order;
            if (o == sc::kOrderFlyBefore) {
                if (m.sb != Sb::hold && m.sb != Sb::funnel && m.sb != Sb::inbound) {
                    m.x_ms = m.o8_ms = 0;
                    m.refused = 0;
                    bool occupied = g.slot_block_ms && std::int32_t(g.slot_block_ms - now) > 0; // review N2: as a release would
                    for (unsigned j = 0; j < g.n && !occupied; ++j) occupied = j != i && occupant(g, g.m[j], now);
                    // B.2 item 4: a re-commit (released before) past the funnel's last stretch keeps the funnel (never re-held: no
                    // U-turn at X); with the funnel occupied it holds (its petal by its bearing): never two converging on X. B.3 item 3
                    // (Run163: the rejoin at 2.6 km went around 2 s later): past the last stretch only while P2's arrival sphere is still
                    // ahead (not beyond A along F: a ship past A cannot reach P2 forward, the funnel's carrot turns it back to A and the
                    // first prediction fails), and also from the release window (behind the petals' front, the nose within 60 deg of F,
                    // within the horizon) when the predictor finds a candidate now: straight into the funnel, no petal lap
                    float rj_t = 0.0f, rj_v = -1.0f;
                    bool rejoin = false, predicted = false;
                    if (m.q_ok && m.release_ms && !occupied) {
                        float X[3];
                        lambda4(g, m, X);
                        const float dx[3] = {m.st.x[0] - X[0], m.st.x[1] - X[1], m.st.x[2] - X[2]};
                        if (past_last_stretch(g, m)) rejoin = sc::dot3(dx, g.F) < sc::kGateCrossFraction * g.r_gate;
                        else {
                            const bool window = release_window(g, m) && dist_x(g, m) <= m.params.v_max * kHorizonS; // B.6 item 3
                            if (window) {
                                predicted = true;
                                rj_v = funnel_feasible(g, m, st ? &st->predictions : nullptr, &rj_t);
                                rejoin = rj_v > 0.0f;
                            }
                        }
                    }
                    if (rejoin) {
                        m.sb = Sb::funnel;
                        m.release_ms = now ? now : 1;
                        // review F2: an occupant at once (occupant() needs a decision within 5 s; enter() cleared it), so a later rejoin
                        // and this frame's release see the funnel taken: one ship per frame enters the funnel
                        // re-review R3: dated kDecideMs back, so due() lets its first real decision run at once
                        m.decided_ms = now > kDecideMs ? now - kDecideMs : 1;
                        m.pred = 0;
                        m.feas_ms = predicted ? (now ? now : 1) : 0;
                        if (predicted) {
                            m.v_feas = rj_v;
                            m.x_pred_ms = now + std::uint32_t(1000.0f * rj_t);
                        }
                        float X[3];
                        lambda4(g, m, X);
                        const float xg[3] = {X[0] - g.G[0], X[1] - g.G[1], X[2] - g.G[2]};
                        const float vx = predicted ? rj_v : m.st.v > kYieldFloorMs ? m.st.v : kYieldFloorMs;
                        m.plan_x_ms = now + std::uint32_t(1000.0f * dist_x(g, m) / vx);
                        if (predicted && std::int32_t(m.x_pred_ms - m.plan_x_ms) > 0) m.plan_x_ms = m.x_pred_ms;
                        m.plan_launch_ms = m.plan_x_ms + std::uint32_t(1000.0f * (1.0f + leg5_s(m, sc::sqrt_ss(sc::dot3(xg, xg)), vx)));
                        if (!g.funnel_ship) g.funnel_ship = m.ship;
                    } else
                        m.sb = Sb::hold;
                }
                if (m.sb == Sb::funnel && !m.q_ok) m.sb = Sb::hold;
            } else if ((o == sc::kOrderNone || o == sc::kOrderRotate) && m.handover_ms) {
                if (m.sb != Sb::handover) { // a new hand-over: its own stamps (review F11)
                    m.sb = Sb::handover;
                    m.x_ms = m.handover_ms;
                    m.refused = 0;
                    m.o8_ms = 0;
                }
            } else if (o == sc::kOrderDock) {
                if (m.sb == Sb::handover && m.x_ms && st) st->x_wait_ms += now - m.x_ms > 3000u ? now - m.x_ms - 3000u : 0u;
                m.sb = Sb::dock;
                m.handover_ms = m.x_ms = 0; // B.3 item 5: the hand-over is over (Run163: a stale handover_ms re-entered the hand-over
                m.refused = 0;              // after the launch and counted ctl_x_refused)
            } else if (o == sc::kOrderWarpAway) {
                m.sb = Sb::launch;
                m.handover_ms = m.x_ms = 0;
                m.refused = 0;
                if (!m.o8_ms) m.o8_ms = m.st.ms;
                const std::uint32_t end = m.o8_ms + 900u;
                if (!g.funnel_end_ms || std::int32_t(end - g.funnel_end_ms) > 0) g.funnel_end_ms = end;
            }
            if (m.sb == Sb::handover && !m.refused && m.x_ms && now - m.x_ms > 5000u) { // waiting at X for the through slot
                m.refused = 1;
                if (st) ++st->x_refused;
            }
        }
        if ((m.sb == Sb::hold || m.sb == Sb::inbound) && m.petal < 0) {
            float r, u, z;
            sb_local(g, m.st.x, &r, &u, &z);
            float phi = sb_bearing(r, u);
            if (m.role == Role::escort) { // an escort: next to its leader's petal (the leader's column, D.3)
                const int li = find(g, m.leader);
                if (li >= 0 && g.m[li].petal >= 0) {
                    const int lp = g.m[li].petal % int(kPetalsPerRing);
                    phi = kPetalPhi0 + kPetalDphi * float(lp + (lp + 2 < int(kPetalsPerRing) ? 2 : -2));
                }
            }
            const int p = petal_pick(g, phi);
            if (p >= 0) {
                m.petal = std::int8_t(p);
                g.petal_ship[p] = m.ship;
            }
        }
        if (m.sb == Sb::hold || m.sb == Sb::inbound) { // B.2 item 2: inbound beyond the ring's extent + 2 km (+ 1 km back out), else hold
            const float dg[3] = {m.st.x[0] - g.G[0], m.st.x[1] - g.G[1], m.st.x[2] - g.G[2]};
            const float d2 = sc::dot3(dg, dg), ext = ring_extent(g, m);
            const float lim = ext + kInboundM + (m.sb == Sb::inbound ? 0.0f : kInboundHystM);
            if (d2 > lim * lim) {
                m.sb = Sb::inbound;
                m.ringed = 0;
            } else {
                m.sb = Sb::hold;
                if (d2 <= ext * ext) m.ringed = 1; // reached its petal ring (the head skip may apply from now)
            }
        }
        if (m.sb != before)
            sb_event(st, m, before, m.sb, m.sb == Sb::hold ? Why::hold : m.sb == Sb::inbound ? Why::inbound : m.sb == Sb::funnel ? Why::rejoin
                                          : m.sb == Sb::dock ? Why::dock : m.sb == Sb::launch ? Why::launch : Why::handover);
        if (m.sb != Sb::hold && m.sb != Sb::inbound && m.petal >= 0) {
            if (g.petal_ship[unsigned(m.petal)] == m.ship) g.petal_ship[unsigned(m.petal)] = 0;
            m.petal = -1;
        }
    }
    // the funnel: the last released ship and the time the next X may come (its launch + the gap, never before a blocked slot frees)
    int f = g.funnel_ship ? find(g, g.funnel_ship) : -1;
    if (f >= 0 && (!pair_role(g.m[f].role) || g.m[f].sb == Sb::hold || g.m[f].sb == Sb::inbound || g.m[f].sb == Sb::none)) f = -1;
    if (f < 0) g.funnel_ship = 0;
    auto t_after = [&](int k) -> std::uint32_t {
        std::uint32_t t = (k >= 0 ? eta_launch(g, g.m[k], now) : (g.funnel_end_ms ? g.funnel_end_ms : now)) + kFunnelGapMs;
        if (g.slot_block_ms && std::int32_t(g.slot_block_ms - t) > 0) t = g.slot_block_ms;
        return t;
    };
    burst_keys(g); // the entry bursts (review B1: the order is a strict total order over (group, distance, key, ship, sub))
    // the candidate: the first in sequence among the holders (committed on order 4 with Q). Review F1 (B.3): an escort that has not
    // committed is never a candidate and never blocks a holder: its reservation is a key (its place once its order 4 commits it), not a
    // queue slot (B.2's waiting escort with its backfill blocked every holder while the escort loitered, the more so once the
    // reservation was refreshed within 15 km)
    int c = -1;
    for (unsigned i = 0; i < g.n; ++i) {
        const Member& m = g.m[i];
        if (!live(g, m, now)) continue;
        if (!(m.role == Role::committed && m.sb == Sb::hold && m.st.order == sc::kOrderFlyBefore && m.q_ok)) continue;
        if (c < 0 || seq_before(g, m, g.m[c])) c = int(i);
    }
    // the funnel's order-4 part holds one ship until it is on its final straight (within kFinalLatM of Lambda_4 and kFinalAlongM of X)
    // or past P2: two S-curves never share the funnel (D.2.3)
    // review (D.2.6, Fable 1): any committed member waiting at X (the hand-over) or on order 5 / 8; B.2: any committed funnel member not yet
    // on its final straight (the last released one, or a ship re-committed past the last stretch)
    // B.6 item 2: t_busy, the predicted funnel-free moment (the latest over the busy occupants): an order-4 funnel member reaches its final
    // straight from its live along-X position at its planned speed (>= 10 m/s); one at the hand-over or on order 5 / 8 ends its order 8
    // (its predicted launch). A slot block bounds the target at one nominal funnel (50 m/s over the lead, R_h and rho) before it: the head
    // released then passes the timing gate and the funnel's shaping paces it to t_free (the coordinator's decision)
    const std::uint32_t t_slot = g.slot_block_ms ? g.slot_block_ms - std::uint32_t(std::int32_t(1000.0f * (kLeadM + kPetalRh + g.rho) / 50.0f)) : 0u;
    bool funnel_busy = false;
    std::uint32_t busy_by = 0, t_busy = 0;
    for (unsigned i = 0; i < g.n; ++i) { // review N3: only live members decided within 5 s (occupant())
        const Member& o = g.m[i];
        if (!occupant(g, o, now)) continue;
        bool busy = o.sb != Sb::funnel;
        std::uint32_t t_o = 0;
        if (busy) {
            t_o = eta_launch(g, o, now);
            if (g.slot_block_ms && std::int32_t(t_slot - t_o) > 0) t_o = t_slot;
        } else {
            float X[3];
            lambda4(g, o, X);
            const float d[3] = {o.st.x[0] - X[0], o.st.x[1] - X[1], o.st.x[2] - X[2]};
            const float s_along = sc::dot3(d, g.F), e2 = sc::dot3(d, d) - s_along * s_along;
            busy = !(s_along > -kFinalAlongM && e2 < kFinalLatM * kFinalLatM);
            if (busy) {
                float v = o.v_sb > 0.0f ? o.v_sb : o.v_feas > 0.0f ? o.v_feas : o.st.v;
                if (v < kYieldFloorMs) v = kYieldFloorMs;
                const float rest = -s_along - kFinalAlongM;
                t_o = now + (rest > 0.0f ? std::uint32_t(std::int32_t(1000.0f * rest / v)) : 0u); // (cvttss2si: <= 2^31 ms; no x87)
            }
        }
        if (!busy) continue;
        if (!funnel_busy) busy_by = o.ship;
        if (!funnel_busy || std::int32_t(t_o - t_busy) > 0) t_busy = t_o;
        funnel_busy = true;
    }
    for (unsigned i = 0; i < g.n; ++i) // the tie-break's stamps belong to the head only, and run only while the funnel is free
        if (int(i) != c || funnel_busy) g.m[i].head_ms = g.m[i].infeas_ms = 0;
    if (st && st->seq_on) { // review N7: only when the caller asks (the DLL: --debug)
        st->seq.cand = c >= 0 ? g.m[c].ship : 0;
        st->seq.key = c >= 0 ? g.m[c].key : 0;
        st->seq.role = c >= 0 ? std::uint8_t(g.m[c].role) : 0;
        st->seq.stage = c >= 0 ? std::uint8_t(g.m[c].sb) : 0;
        st->seq.busy_by = busy_by;
        st->seq.window = st->seq.timing = 2; // not evaluated (no committed head, or the funnel busy)
        st->seq.free_ms = std::int32_t(t_after(f) - now);
        st->seq.block_ms = g.slot_block_ms ? std::int32_t(g.slot_block_ms - now) : 0;
        st->seq.v_feas = c >= 0 ? g.m[c].v_feas : -1.0f;
        st->seq.dx = c >= 0 && g.m[c].role == Role::committed && g.m[c].q_ok ? dist_x(g, g.m[c]) : -1.0f;
        st->seq.ringed = c >= 0 ? g.m[c].ringed : 0;
        unsigned holders = 0, inbound = 0;
        for (unsigned i = 0; i < g.n; ++i) {
            const Member& o = g.m[i];
            if (o.role != Role::committed && o.role != Role::escort) continue;
            holders += o.sb == Sb::hold || o.sb == Sb::inbound ? 1u : 0u;
            inbound += o.sb == Sb::inbound ? 1u : 0u;
        }
        st->seq.holders = std::uint8_t(holders);
        st->seq.inbound = std::uint8_t(inbound);
    }
    std::uint32_t t_next = 0; // the next holder's target at its release window
    if (c >= 0 && g.m[c].role == Role::committed && !funnel_busy) {
        Member& m = g.m[c];
        const std::uint32_t t_free = t_after(f);
        const float dX = dist_x(g, m);
        // behind the ring-0 front turn's 60-deg point, heading within 60 deg of F (no U-turn in the funnel; B.6 item 3), within the
        // predictor's horizon (B.2: dX <= v_max * 256 s; an inbound ship is no candidate); the prediction decides the rest
        const bool window = release_window(g, m) && dX <= m.params.v_max * kHorizonS;
        const bool timing = std::int32_t(t_free - now) <= std::int32_t(dX / kPetalVmin * 1000.0f);
        if (st && st->seq_on) {
            st->seq.window = window ? 1 : 0;
            st->seq.timing = timing ? 1 : 0;
        }
        // the tie-break (review item 1): a head with the funnel free and its slot due that stays in its release window without a feasible
        // candidate for more than kHeadSkipMs, or whose window never opens within a slowest lap plus kHeadSkipMs, yields its place and
        // re-queues at the tail (ctl_seq_skipped); a lap in progress is not a deferral. B.2 item 1: the timers run only once the head has
        // reached its petal ring, and the skip applies only when another committed non-escort holder is feasible; the leader and its
        // reserved escorts move together (keys key + 1 + rank), so a leader never lands behind its own escorts
        if (timing && m.ringed) {
            if (!m.head_ms) m.head_ms = now ? now : 1;
        } else
            m.head_ms = 0;
        if (timing && window && m.ringed && m.feas_ms && m.v_feas <= 0.0f) {
            if (!m.infeas_ms) m.infeas_ms = now ? now : 1;
        } else if (!window || !m.ringed || m.v_feas > 0.0f)
            m.infeas_ms = 0;
        const std::uint32_t lap_ms = std::uint32_t(1000.0f * kLap / kPetalVmin) + kHeadSkipMs;
        const bool skip_due = (m.infeas_ms && now - m.infeas_ms > kHeadSkipMs) || (m.head_ms && now - m.head_ms > lap_ms);
        bool alt = false; // another committed non-escort holder that is feasible (at most one new prediction per frame)
        if (skip_due) {
            bool predicted = false;
            for (unsigned i = 0; i < g.n && !alt; ++i) {
                Member& o = g.m[i];
                if (int(i) == c || !live(g, o, now) || o.role != Role::committed || o.sb != Sb::hold || o.st.order != sc::kOrderFlyBefore || !o.q_ok)
                    continue;
                if ((o.key & ~15u) == (m.key & ~15u)) continue; // review F1: a sibling committed on its own order 4 is the head's convoy
                if (dist_x(g, o) > o.params.v_max * kHorizonS) continue;
                if (o.feas_ms && now - o.feas_ms < kFeasMs) {
                    alt = o.v_feas > 0.0f;
                    continue;
                }
                if (predicted) continue;
                predicted = true;
                o.feas_ms = now ? now : 1;
                o.v_feas = funnel_feasible(g, o, st ? &st->predictions : nullptr);
                alt = o.v_feas > 0.0f;
            }
        }
        if (skip_due && alt) {
            m.infeas_ms = 0;
            std::uint32_t tail = g.sb_tail_key;
            for (unsigned i = 0; i < g.n; ++i)
                if (g.m[i].key > tail) tail = g.m[i].key;
            const std::uint32_t old_key = m.key;
            m.key = ((tail >> 4) + 1u) << 4;
            for (unsigned i = 0; i < g.n; ++i) { // the convoy moves as one: the reserved escorts right behind their leader again (review F1:
                Member& e = g.m[i];                 // every reserved member of the leader's key class, a sibling already committed included)
                if (int(i) == c || !e.ship || !e.key_res) continue;
                const bool of_leader = (e.role == Role::escort && e.leader == m.ship) || (e.key & ~15u) == old_key;
                if (!of_leader) continue;
                const std::uint32_t rank = e.key > old_key && e.key - old_key - 1u < 15u ? e.key - old_key - 1u : 0u;
                e.key = m.key + 1u + rank;
            }
            g.sb_tail_key = m.key;
            m.key_res = 0;
            m.head_ms = 0;
            m.unburst = 1; // out of its entry burst (review B1: enter_ms is kept, so a skip creates no burst)
            ++g.sb_skips;
            sb_event(st, m, m.sb, m.sb, Why::skip);
            if (st) ++st->seq_skipped;
        } else if (window && timing && (!m.feas_ms || now - m.feas_ms >= kFeasMs)) {
            m.feas_ms = now ? now : 1;
            float t_pred = 0.0f;
            m.v_feas = funnel_feasible(g, m, st ? &st->predictions : nullptr, &t_pred);
            if (m.v_feas > 0.0f) { // the release
                m.x_pred_ms = now + std::uint32_t(1000.0f * t_pred);
                const Sb before = m.sb;
                m.sb = Sb::funnel;
                m.release_ms = now ? now : 1;
                m.pred = f >= 0 ? g.m[f].ship : 0;
                if (m.petal >= 0 && g.petal_ship[unsigned(m.petal)] == m.ship) g.petal_ship[unsigned(m.petal)] = 0;
                sb_event(st, m, before, m.sb, Why::release);
                m.petal = -1;
                g.funnel_ship = m.ship;
                float X[3];
                lambda4(g, m, X);
                const float xg[3] = {X[0] - g.G[0], X[1] - g.G[1], X[2] - g.G[2]};
                std::uint32_t t_x = now + std::uint32_t(1000.0f * dX / m.v_feas);
                if (std::int32_t(m.x_pred_ms - t_x) > 0) t_x = m.x_pred_ms; // the predicted path (the S-curve), not the chord
                m.plan_x_ms = std::int32_t(t_free - t_x) > 0 ? t_free : t_x;
                m.plan_launch_ms = m.plan_x_ms + std::uint32_t(1000.0f * (1.0f + leg5_s(m, sc::sqrt_ss(sc::dot3(xg, xg)), kPetalVmax)));
                ++g.sb_releases;
                if (st) ++st->releases;
                f = c;
            } else if (st)
                ++st->deferred;
        }
    }
    // B.6 item 2: the head's lap target is the predicted funnel-free moment (re-evaluated every frame; the funnel free: now, never before
    // one nominal funnel before a slot block)
    if (c >= 0 && g.m[c].role == Role::committed && g.m[c].sb == Sb::hold) {
        t_next = funnel_busy ? t_busy : g.slot_block_ms && std::int32_t(t_slot - now) > 0 ? t_slot : now; // review 2, the funnel's offset
        if (!t_next) t_next = 1;
    }
    // speeds: the funnel's shaping and feasibility (the go-around), the holders' laps, the grid sphere guard
    for (unsigned i = 0; i < g.n; ++i) {
        Member& m = g.m[i];
        m.t_rel_ms = 0; // B.8 (diagnostics): set below for a holder with a lap target
        if (!live(g, m, now) || !(m.role == Role::committed || m.role == Role::escort)) continue;
        if (m.sb == Sb::funnel && m.role == Role::committed && m.st.order == sc::kOrderFlyBefore) {
            const int pk = m.pred ? find(g, m.pred) : -1;
            const int pp = pk >= 0 && pair_role(g.m[pk].role) && g.m[pk].sb != Sb::hold && g.m[pk].sb != Sb::inbound && g.m[pk].sb != Sb::none ? pk : -1;
            const std::uint32_t t_target = t_after(pp);
            const float dX = dist_x(g, m);
            const float dt = float(std::int32_t(t_target - now)) * 0.001f;
            const float vcap = m.params.v_max > kYieldFloorMs ? m.params.v_max : kYieldFloorMs;
            float v = dt > 0.05f ? dX / dt : vcap;
            if (v > vcap) v = vcap;
            if (v < kYieldFloorMs) v = kYieldFloorMs;
            if (!m.feas_ms || now - m.feas_ms >= kFeasMs) {
                m.feas_ms = now ? now : 1;
                float t_pred = 0.0f;
                m.v_feas = funnel_feasible(g, m, st ? &st->predictions : nullptr, &t_pred);
                if (m.v_feas > 0.0f) m.x_pred_ms = now + std::uint32_t(1000.0f * t_pred);
            }
            if ((m.v_feas <= 0.0f || dt - dX / kYieldFloorMs > 10.0f) && past_last_stretch(g, m)) { // B.2 item 4: never re-held there
                m.v_sb = v;
                continue;
            }
            if (m.v_feas <= 0.0f || dt - dX / kYieldFloorMs > 10.0f) { // no feasible candidate, or the slot frees too late: go around
                const Sb before = m.sb;
                m.sb = Sb::hold;
                ++m.go_arounds;
                ++g.sb_go_arounds;
                if (st) ++st->go_arounds;
                if (st && dX < 1000.0f) ++st->go_arounds_near;
                sb_event(st, m, before, m.sb, Why::go_around);
                if (g.funnel_ship == m.ship) g.funnel_ship = 0;
                m.v_sb = kPetalVnom;
                continue;
            }
            m.v_sb = v < m.v_feas ? v : m.v_feas;
            if (g.slot_block_ms && std::int32_t(g.slot_block_ms - m.plan_x_ms) > 0) { // a direction change after the release: the slot
                const std::uint32_t shift = g.slot_block_ms - m.plan_x_ms;               // frees later (D.7 item 4: + 60 s)
                m.plan_x_ms += shift;
                m.plan_launch_ms += shift;
            }
            continue;
        }
        if (m.sb == Sb::inbound) { // B.2 item 2: toward the petal's entry at the plan speed: v_max, braking to the petals' top speed at the
                                   // boundary (site B caps it at the cornering and large caps)
            const float dg[3] = {m.st.x[0] - g.G[0], m.st.x[1] - g.G[1], m.st.x[2] - g.G[2]};
            const float d = sc::sqrt_ss(sc::dot3(dg, dg)), lim = ring_extent(g, m) + kInboundM;
            const float a = m.params.a_lin > 0.1f ? m.params.a_lin : 0.1f, vmax = m.params.v_max > kPetalVmax ? m.params.v_max : kPetalVmax;
            const float v = sc::sqrt_ss(kPetalVmax * kPetalVmax + 2.0f * a * (d > lim ? d - lim : 0.0f));
            m.v_sb = v < vmax ? v : vmax;
            continue;
        }
        if (m.sb != Sb::hold) continue;
        std::uint32_t t_rel = 0;
        if (int(i) == c && m.role == Role::committed) t_rel = t_next;
        else if (m.role == Role::escort && m.key_res) // the escort: at its stage before its order 4 can come (D.12; B.8: kStageLeadMs early)
            t_rel = escort_target(g, m, now);
        m.t_rel_ms = t_rel;
        // B.6 (coordinator's amendment): an escort whose leader launched, once inside the script's 10 km and with its target past, laps
        // at 25 m/s over the whole lap (the calmest holding; the script's order 4 comes on its own schedule); a committed head keeps the law
        m.v_sb = hold_speed(g, m, now, t_rel, st);
    }
}
// The controller's leaf point and site B's speed for a member at x flying at v on `order` (site A; the DLL's decide() and the
// fixture's steer_call): a holder on order 4 or an escort on 0x14 / 0x15 in its petal (the racetrack's carrot, the lap speed), the
// funnel on order 4 (Lambda_4's carrot, the shaped speed), the order-5 leg (Lambda_5's carrot, the aligned ramp written both ways:
// D.12, the creep on any off-axis Q). False: the module's other rules.
struct SbSteer {
    float P[3] = {0, 0, 0};
    float v = -1.0f;
    Sb sb = Sb::none;
};
inline bool sb_steer(const Gate& g, const Member& m, const float x[3], float v_now, std::uint16_t order, std::uint32_t target,
                     SbSteer* out) noexcept {
    out->sb = m.sb;
    const float W = m.params.W, rg = g.r_gate;
    if (!(rg > 0.0f)) return false;
    // the live target first (review F2): orders 4 / 5 to this gate; 0x14 to the member's leader; 0x15 to its leader, or to this gate
    // once the leader launched (the reservation)
    if (order == sc::kOrderFlyBefore || order == sc::kOrderDock) {
        if (target != g.gate) return false;
    } else if (order == sc::kOrderEscort) {
        if (target != m.leader) return false;
    } else if (order == sc::kOrderFollow) {
        if (target != m.leader && !(m.res_until && target == g.gate)) return false;
    }
    const bool o4 = m.role == Role::committed && order == sc::kOrderFlyBefore;
    if (o4 || (m.role == Role::escort && (order == sc::kOrderEscort || order == sc::kOrderFollow))) {
        if (m.sb == Sb::inbound && m.petal >= 0) { // B.2 item 2: toward the petal's entry, then along its inner straight, at the plan speed
            PetalGeom pg;
            petal_geom(g, unsigned(m.petal), m.zx, &pg);
            const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
            float ta, tb;
            inbound_carrot(pg, rg, W, sc::dot3(d, g.F), sc::dot3(d, pg.e), sc::dot3(d, pg.n), v_now, &ta, &tb);
            for (unsigned i = 0; i < 3; ++i) out->P[i] = g.G[i] + ta * g.F[i] + tb * pg.e[i];
            out->v = m.v_sb > 0.0f ? m.v_sb : kPetalVmax;
            return sc::finite_f(out->P[0]) && sc::finite_f(out->P[1]) && sc::finite_f(out->P[2]);
        }
        if (m.sb == Sb::hold && m.petal >= 0) {
            PetalGeom pg;
            petal_geom(g, unsigned(m.petal), m.zx, &pg);
            const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
            float ta, tb;
            if (m.role == Role::escort && m.res_until && (!m.esc_in10 || !petal_near(pg)) && order == sc::kOrderFollow &&
                sc::dot3(d, d) > kEscortSwitchM * kEscortSwitchM * 0.9f) { // B.6: until its first pass inside 10 km (a far petal: always)
                // the leader launched and the escort is beyond the script's 10 km: onto its inner straight, heading +F, toward the
                // release window (the pursuit of the straight b = rho; D.12: order 4 then comes inside 10 km with the nose along F)
                ta = sc::dot3(d, g.F) + pursuit_look(v_now, W, rg);
                if (ta < pg.a0) ta = pg.a0;
                if (ta > pg.a1) ta = pg.a1;
                tb = pg.rho;
                for (unsigned i = 0; i < 3; ++i) out->P[i] = g.G[i] + ta * g.F[i] + tb * pg.e[i];
                out->v = kPetalVmax;
                return sc::finite_f(out->P[0]) && sc::finite_f(out->P[1]) && sc::finite_f(out->P[2]);
            }
            if (!m.ringed) // B.2: a holder not yet at its ring keeps the inbound carrot (behind the ring, along its own petal's plane: no
                inbound_carrot(pg, rg, W, sc::dot3(d, g.F), sc::dot3(d, pg.e), sc::dot3(d, pg.n), v_now, &ta, &tb); // crossing of the other petals)
            else
                petal_carrot(pg, rg, W, sc::dot3(d, g.F), sc::dot3(d, pg.e), v_now, &ta, &tb);
            for (unsigned i = 0; i < 3; ++i) out->P[i] = g.G[i] + ta * g.F[i] + tb * pg.e[i];
            out->v = m.v_sb > 0.0f ? m.v_sb : kPetalVnom;
            return sc::finite_f(out->P[0]) && sc::finite_f(out->P[1]) && sc::finite_f(out->P[2]);
        }
        if (o4 && m.sb == Sb::funnel && m.q_ok) {
            float X[3];
            lambda4(g, m, X);
            line_carrot(X, g.F, sc::kGateCrossFraction * rg, rg, W, x, v_now, out->P);
            out->v = m.v_sb > 0.0f ? m.v_sb : kYieldFloorMs;
            return sc::finite_f(out->P[0]) && sc::finite_f(out->P[1]) && sc::finite_f(out->P[2]);
        }
        return false;
    }
    if (m.role == Role::committed && order == sc::kOrderDock && m.q_ok && (m.sb == Sb::dock || m.sb == Sb::handover || m.sb == Sb::funnel)) {
        float X[3];
        lambda5(g, m, X);
        line_carrot(X, g.F, 2.0f * rg, rg, W, x, v_now, out->P);
        const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
        out->v = dock_ramp(sc::sqrt_ss(sc::dot3(d, d)), rg, m.params.v_max);
        return sc::finite_f(out->P[0]) && sc::finite_f(out->P[1]) && sc::finite_f(out->P[2]) && out->v > 0.0f;
    }
    return false;
}
// P2 (the order-4 arrival at the grid sphere): the funnel's crossing is the kept hand-over's precondition (D.6: the funnel replaces
// the join mode of P2's verdict); the stage moves to the hand-over at the next frame from the live order.
__attribute__((always_inline)) inline bool sb_funnel(const Member& m) noexcept { return m.role == Role::committed && m.sb == Sb::funnel; }
// Whether stage B owns a committed member's gate legs (review F8): Q known and either a petal held or a released stage. Otherwise the
// member keeps the stage-A / amendment-17 join (join on, P2 kept when aligned), never "join off + vanilla stop".
__attribute__((always_inline)) inline bool sb_managed(const Member& m) noexcept {
    if (m.role != Role::committed || !m.q_ok) return false;
    if (m.sb == Sb::hold || m.sb == Sb::inbound) return m.petal >= 0;
    return m.sb == Sb::funnel || m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch;
}
// ---- D.5 (with D.12): a small ship's order-5 pass point while a capital is in the funnel -------------------------------------------
// The funnel is active (D.5: a member released and not yet launched: on its final, the hand-over, order 5 or 8); *side = the side of
// that capital's Q along R (+1 when on the axis: the bias of q_point).
inline bool funnel_active(const Gate& g, float* side) noexcept {
    for (unsigned i = 0; i < g.n; ++i) {
        const Member& m = g.m[i];
        if (m.role != Role::committed || !(m.sb == Sb::funnel || m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch)) continue;
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
    const std::uint32_t eta = now + std::uint32_t(1000.0f * d_order / (v > 1.0f ? v : 1.0f));
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
} // namespace x3m::ship_avoidance::ctl
