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
// Under ship_steering_gates_lane the lane (lane_frame, lane_site_b, lane_steer below) flies the members on top of this certificate; with
// the key off the certificate is all there is. Stage B (the petals, the sequencer, the release window, the lap and hold laws) was deleted
// on 2026-10-15 (docs/architecture/ship-steering-holistic-review.md, rank 2).
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
// lane fix round 2 item 3d: under the lane key a hand-over the board has flagged refused (Member::refused) stays a member this long after
// P2 (the KC's through-slot refusal, unknown to the board: Run173 116 s; the cast p2_miss_near's 112-122 s crossed kHandoverMaxMs, the
// leader left the board before its grant and its escort's queue hold ended with it, the escort then flying the engine's 0x14 at the
// stopped leader); key off, or not refused: kHandoverMaxMs
constexpr std::uint32_t kLaneRefusedMaxMs = 300000;
constexpr std::uint32_t kLaneExitMs = 60000;      // lane fix round 2 item 2: a handed escort's exit from the lane lasts at most this (+ the step)
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
// The pass point Q and the final lines (D.2.2), the through-slot model, the escort's reservation and the order-0 gaps (stage B's petals,
// sequencer, release window and lap law were deleted on 2026-10-15; the lane keeps these)
constexpr float kChordSlackM = 150.0f; // the final line crosses the grid sphere as a chord: |Lambda - A_lat| <= 0.75 r - 150 m (D.2.2)
constexpr float kTrackM = 100.0f, kFanMarginM = 50.0f, kSmallMaxM = 140.0f; // the trigger's tracking allowance; the upper fan's margin
constexpr float kFanLowR = 0.38890873f; // the upper small-arrival fan's lowest U (0.55 r cos 45 deg)
constexpr float kSin3 = 0.05233596f;  // the hull's cross-section with the nose 3 deg off F: w + (L - w) sin 3 deg
constexpr std::uint32_t kFunnelGapMs = 2000;    // the funnel's gap after a launch (the KC's hand-over frames)
constexpr std::uint32_t kSlotArrivalMs = 61000;  // a capital launch at the gate blocks our side this long (t_a + 60 + 1 s)
constexpr std::uint32_t kReserveMs = 300000;     // an escort's reserved number holds 300 s after its leader's launch, refreshed while it is
                                                 // on 0x15 to the gate or within kReserveKeepM of it (B.3 item 4; Run163: order 4 at +53 s
                                                 // and +81 s after the leader's launch, outbound legs at +223 / +274 s from 33 km)
constexpr float kReserveKeepM = 15000.0f;        // B.3 item 4: within this distance of the gate any order keeps the reservation alive
constexpr std::uint32_t kGapMs = 5000;       // the script's order-0 re-issue gap (escort legs: 2-3 s waits; committed: <= 5 s)
constexpr std::uint32_t kGapEscortMs = 3000; // the escort template's wait (2-3 s): an escort's gap ends by then
// The lane's stage of a member (lane_frame): inbound (before its final), funnel (on its final), the hand-over after P2, order 5 (dock),
// order 8 (launch); none under the key off
enum class Sb : std::uint8_t { none = 0, funnel, handover, dock, launch, inbound, count };
inline const char* sb_name(Sb s) noexcept {
    static const char* const n[unsigned(Sb::count)] = {"none", "funnel", "handover", "dock", "launch", "inbound"};
    return unsigned(s) < unsigned(Sb::count) ? n[unsigned(s)] : "?";
}

// Lane stage 1 (convoy-gate-transit.md section 6.3, ship_steering_gates_lane): the speed law a lane member flies this frame (the member
// row's law=): join (toward Lambda at the lane speed), lane (on Lambda at the lane speed), merge (the yielder opening g_req behind its
// predecessor), follow (the follow law below the lane speed), stop (written 0: the brief stop, or behind a stopped predecessor), pace (the
// head toward the slot's free time), gate (an escort without order 4 kept short of d_end), hold (site B on order 0 / 0x1f).
enum class Law : std::uint8_t { none = 0, join, lane, merge, follow, stop, pace, gate, hold, count };
inline const char* law_name(Law l) noexcept {
    static const char* const n[unsigned(Law::count)] = {"none", "join", "lane", "merge", "follow", "stop", "pace", "gate", "hold"};
    return unsigned(l) < unsigned(Law::count) ? n[unsigned(l)] : "?";
}

enum class Role : std::uint8_t { none = 0, committed, escort, arrival, evicted, count };
inline const char* role_name(Role r) noexcept {
    static const char* const n[unsigned(Role::count)] = {"none", "committed", "escort", "arrival", "evicted"};
    return unsigned(r) < unsigned(Role::count) ? n[unsigned(r)] : "?";
}
enum class Plan : std::uint8_t { drift = 0, line, dock, brake, hold, warp_in, exit, launch, follow, line5, lane, count };
inline const char* plan_name(Plan p) noexcept {
    static const char* const n[unsigned(Plan::count)] = {"drift", "line", "dock", "brake", "hold", "warp_in", "exit", "launch", "follow",
                                                         "line5", "lane"};
    return unsigned(p) < unsigned(Plan::count) ? n[unsigned(p)] : "?";
}
enum class PairV : std::uint8_t { none = 0, clear, unresolved, count };
inline const char* pair_name(PairV v) noexcept {
    static const char* const n[unsigned(PairV::count)] = {"none", "clear", "unresolved"};
    return unsigned(v) < unsigned(PairV::count) ? n[unsigned(v)] : "?";
}
// B.9: why a pair's verdict is what it is (the last write of its matrix entry; the pair row's cause=): nocert (no sweep: the pair was
// reset, a member left or was not live at its decision), cert (clear on both plans), yield (clear after a yield), contact (the sweep found
// a conflict and no yield candidate cleared it), budget (a conflict whose yield was not tried: the decision's yields were spent), stale
// (the rollouts too far apart in time or too short an overlap), zero (a rollout without samples), noyield (B.9 review: a conflict whose
// designated yielder could not yield: no steered order (can_yield) or no candidate on its ladder). kPairWhyPlan is or-ed in when the
// member whose rollout wrote the entry (the deciding member; a yielder's own for the rows its yield wrote, with the decider's or-ed into
// their shared pair) changed its plan since its previous rollout (plan_changed; the row's trig=plan, else clock).
// Lane stage 1 (convoy-gate-transit.md section 6.3): merge (a lane yielder cleared on the 10 m/s floor), stop (on the brief stop).
enum class PairWhy : std::uint8_t { nocert = 0, cert, yield, contact, budget, stale, zero, noyield, merge, stop, count };
constexpr std::uint8_t kPairWhyPlan = 0x80;
inline const char* pair_why_name(std::uint8_t w) noexcept {
    static const char* const n[unsigned(PairWhy::count)] = {"nocert", "cert", "yield", "contact", "budget", "stale", "zero", "noyield",
                                                            "merge", "stop"};
    w &= std::uint8_t(~kPairWhyPlan);
    return w < unsigned(PairWhy::count) ? n[w] : "?";
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
    float roll_v = -1.0f;       // B.9: the plan speed, plan and stage the member's own rollout was made on (plan_changed)
    Plan roll_plan = Plan::drift;
    Sb roll_sb = Sb::none;
    float v_roll_max = 0;       // the largest speed over the rollout (the time-offset margin)
    float cap = -1.0f;          // the yield cap (m/s, < 0 none)
    std::uint32_t cap_ms = 0;
    std::uint32_t yield_ms = 0; // B.9 review: the decision that set the cap (try_yield); cap_ms is also refreshed while an order-0 gap keeps it
    // the lane stage (lane_frame; Sb::none under the key off: stage B's sequencer was deleted on 2026-10-15), the sequence key (seq << 4;
    // an escort: its leader's key + 1 + its rank), the reservation (an escort's key holds until res_until; 0 while its leader is present),
    // Q and the final lines (gate-frame R, U)
    Sb sb = Sb::none;
    std::uint8_t key_res = 0, q_ok = 0, refused = 0;
    std::uint8_t p2_kept = 0;   // lane fix round 1 item 3: P2 kept the speed at the hand-over (handover_ms); site B never latches one it did not keep
    std::uint32_t key = 0, res_until = 0, pred = 0; // pred: the ship ahead on the final (its launch frees the funnel)
    std::uint32_t res_o8_ms = 0; // B.3 item 4: the leader's order-8 stamp the reservation started from (res_until is refreshed)
    // B.3 item 2: the KC's last warn of this member (site B's warned frame: the obstacle list named its own gate) and a 0x12 episode
    // kept through (the funnel state stays; avoid_ms: the episode's first kept observation, keep_ms: its last)
    std::uint32_t warn_ms = 0, avoid_ms = 0, keep_ms = 0;
    std::uint8_t warn_gate = 0, warn_stage = 0;
    std::uint32_t o8_ms = 0, x_ms = 0;
    float zx = 0;               // X along F from G (m, < 0): the grid line's, or the class estimate before order 4
    float q[2] = {0, 0}, l4[2] = {0, 0}, a_lat[2] = {0, 0}; // Q, Lambda_4 and A (R, U), m
    float v_sb = -1.0f;         // the lane law's speed for this ship (m/s; < 0 none)
    // the order-0 gap (Run161): the last state read on the role's steered order (committed 4 / 5, escort 0x14 / 0x15) and its plan
    std::uint32_t steered_ms = 0;
    Plan last_plan = Plan::drift;
    // B.5 (ship_gate_ctl_live_core.h): proven live this frame (the frame boundary's live_prove), the last proof (game ms), the sector
    // of the snapshot that listed it, and the record class code site B read in the update (the member row's class; 0xffffffff unread)
    std::uint8_t proven = 0;
    std::uint32_t live_ms = 0, live_sector = 0, code = 0xffffffffu;
    // Lane stage 1 (convoy-gate-transit.md, ship_steering_gates_lane; lane_frame): lane-managed (its board's lane key), on Lambda (merged
    // laterally), re-departing (out along -F before its U-turn), the follow pair's speed law settled (the settling gap >= g_req - 50 m), the
    // law of this frame, its place in the lane order (0 the head; 0xff not in it) and the ship ahead (lane_pred); the merge's start, the
    // brief stop's start (0 none); along F from G (m, < 0 before the plane), the gap tail(pred) - nose(self) along F (m; 3.4e38 none),
    // the ETA to d_end the order ranks by (s), the lateral distance from Lambda (m)
    std::uint8_t ln = 0, lane_on = 0, redep = 0, merged = 0;
    // lane fix round 1 item 4: past the plane without launching (an 0x12 overshoot; until back on Lambda or out of the lane stages), the
    // re-departure's out distance beyond d_end (kLaneRedepM; an overshoot's: its settling distance + its turn radius), the lane law's stop
    // episode (its start; 0 none: lane_law's T_stop bound), the hand-over's refusal wait already counted (ctl_x_wait_s, item 3)
    std::uint8_t over = 0;
    float redep_out = 3000.0f, redep_side = 1000.0f, redep_r = 0.0f; // (+ the out-leg's offset below Lambda, an overshoot's turn radius)
    float lane_settle = 0.0f; // item 3: the blend's settling distance (lane_blend_settle), cached by lane_frame (0: not yet: computed)
    float d_end = 0.0f;       // lane fix round 2 item 4: its gate's d_end (lane_end), copied by lane_frame (0: kLaneEndM, dend)
    // lane fix round 2 item 2 (Run167/168 why=leader): an escort the script handed to another leader leaves the lane as a member: the
    // hand's game ms (0 none), its new target, and whether it is clear of Lambda's footprint (lane_frame; validity then ends the role)
    std::uint32_t exit_ms = 0, exit_tgt = 0;
    std::uint8_t exit_clear = 0;
    // lane fix round 2 item 5 (review 2, 3, 8, 9): seen in front of the plane since it entered (an evicted member keeps it 30 s: only a
    // member that crossed the plane is an overshoot, not one that committed from behind the gate); the overshoot's start (game ms)
    std::uint8_t front_seen = 0, over_new = 0;
    std::uint8_t over_in = 0; // lane fix round 2 item 3c: the overshoot has turned in (its re-departure's turn done; its law from its own s)
    std::uint32_t over_ms = 0, over_ret_ms = 0; // (over_ret_ms: its predicted return to its apex, set when it begins)
    std::uint32_t over_ahead_of = 0; // items 3a / 5: the overshoot this member was inside the return zone of when it began (it stays ahead)
    std::uint32_t lstop_ms = 0, lstop_last_ms = 0, x_acc_ms = 0; // (lstop_last_ms: the episode's last frame with the stop's condition)
    std::uint8_t merge_done = 1; // review item 6: the merge's completion, the raw gap >= g_req - 50 m (merged is the speed law's state)
    Law law = Law::none;
    std::uint8_t lane_rank = 0xff, lane_esc = 0; // lane_esc: entered as an escort (its final stays on l_c: lane_lat_at)
    std::uint8_t lane_ord_ok = 0;  // final review item 1: lane_ord_ms holds the joiner's sticky order moment (lane_frame's lane order)
    std::uint32_t lane_ord_ms = 0; // (game ms: now + its score when last taken)
    std::uint32_t lane_pred = 0, merge_ms = 0, stop_ms = 0;
    std::uint32_t lane_free_ms = 0, lane_gate_ms = 0; // the head's slot free moment; an escort's gate moment (game ms, 0 none)
    float lane_s = 0, lane_gap = 3.4e38f, lane_eta = 0, lane_lat = 0;
    // review item 2 (lane_defer_step): the game clock of the last deferral write and the deferred value site B wrote over f+0x210, the
    // order's re-plan stamp (engine ms; defer_w 0: no deferral in force)
    std::uint32_t defer_base = 0, defer_w = 0;
    // review item 9 (the lane row): site B's last lane write (m/s; < 0 none) and its kind (LaneWrite)
    float sb_wv = -1.0f;
    std::uint8_t sb_wk = 0;
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
    std::uint8_t why[kMembers][kMembers] = {}; // B.9: PairWhy of the last write (| kPairWhyPlan: its decision's member had a plan change)
    std::uint8_t why_plan = 0;                 // B.9: kPairWhyPlan during a decision whose member's plan changed (decide), else 0
    std::uint32_t vms[kMembers][kMembers] = {};
    std::uint8_t logged[kMembers][kMembers] = {}; // the last PairV a ship_gate_ctl_pair row carried
    std::uint32_t logged_ms[kMembers][kMembers] = {};
    // the plane occupancy the small-ship placement reads (D.4): the last capital launch here, the last departing capital trigger
    std::uint32_t cap_arrival_ms = 0, cap_depart_ms = 0;
    float cap_arrival_w = 0, cap_depart_w = 0;
    // the through-slot model of lane_frame (the end of the last launch, a capital launch here blocks our side until slot_block_ms, the
    // last arrival seen, the frame's previous game time)
    std::uint32_t funnel_end_ms = 0, slot_block_ms = 0, last_arrival_seen = 0, sb_last_ms = 0;
    std::uint32_t o0_ms[kMembers][kMembers] = {}; // review F5: the last order-0 pair row per pair (one per second; the DLL's --debug)
    std::uint32_t pass_ms[kMembers][kMembers] = {}; // Run161: the last E pass row per pair (one per second; the DLL's --debug)
    // B.3 item 1: the SETA slack of the board's freshness bounds (note_step): the larger of this and the previous frame's game step
    // (clamped to kStepClampMs)
    std::uint32_t step_ms = 0;
    // Lane stage 1: the board's key at the gate's acquisition (Board::lane), Lambda's common lateral (R, U; m), the lane order (ships,
    // front first; lane_n entries), the merges and brief stops started at this gate
    std::uint8_t lane = 0, lane_c_ok = 0;
    float lane_c[2] = {0, 0};
    std::uint32_t lane_ord[kMembers] = {};
    unsigned lane_n = 0, lane_merges = 0, lane_stops = 0;
};
struct Board {
    std::uint8_t lane = 0; // ship_steering_gates_lane (convoy-gate-transit.md stage 1): every gate acquired runs lane_frame (0: none)
    Gate g[kGates];
    unsigned full_gates = 0, full_members = 0;
    // lane fix round 1 item 4 (Run173: E1 re-committed 15 times in 1.7 s, why=target): the last kRecentLeaves leaves on Why::target
    // (ship, game ms) for the re-commit hysteresis (recommit_ok)
    static constexpr unsigned kRecentLeaves = 8;
    std::uint32_t tl_ship[kRecentLeaves] = {}, tl_ms[kRecentLeaves] = {};
    unsigned tl_next = 0;
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
__attribute__((always_inline)) inline float capsule_gap(const float ca[3], const float fa[3], float La, float wa, const float cb[3], const float fb[3], float Lb, float wb) noexcept {
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
        g.why[i][j] = g.why[j][i] = std::uint8_t(PairWhy::nocert);
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
        g->lane = b.lane;
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
        m.enter_ms = now ? now : 1;
        // (round 2 review item 2: a hand to another leader belongs to the escort membership it was stamped in: a ship re-entering in
        // another role, after an eviction within 30 s or as a committed member, flies no Plan::drift and keeps no frozen lane order)
        m.exit_ms = m.exit_tgt = 0;
        m.exit_clear = 0;
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
    m.cap_ms = m.yield_ms = 0; // B.9 review (item 3): a re-entering member resumes no old cap or plan speed
    m.v_plan = -1.0f;
    m.v_plan_ms = 0;
    m.decided_ms = 0;
    if (evict) {
        m.role = Role::evicted;
        m.evict_ms = now ? now : 1;
        m.avoid_ms = m.keep_ms = 0; // review F9
        m.warn_gate = 0;
        m.exit_ms = m.exit_tgt = 0; // round 2 review item 2: an evicted escort is no longer leaving the lane as a member
        m.exit_clear = 0;
    } else {
        m = Member{}; // the slot is free: no pointer left behind for locate() or the bitmap
    }
}
// Lane fix round 1 item 4 (Run173: E1 re-committed 15 times in 1.7 s, every leave why=target): a ship that left a board on Why::target
// re-commits at most once per kRecommitMs + the game step (the larger of the board's last two, clamped). The caller notes the leave
// (note_target_leave) and asks before a commit that is not a committed member's refresh (recommit_ok).
constexpr std::uint32_t kRecommitMs = 2000;
inline void note_target_leave(Board& b, std::uint32_t ship, std::uint32_t now) noexcept {
    if (!ship) return;
    unsigned k = 0;
    while (k < Board::kRecentLeaves && b.tl_ship[k] != ship) ++k;
    if (k == Board::kRecentLeaves) {
        k = b.tl_next;
        b.tl_next = (b.tl_next + 1) % Board::kRecentLeaves;
    }
    b.tl_ship[k] = ship;
    b.tl_ms[k] = now ? now : 1;
}
inline bool recommit_ok(const Board& b, const Gate& g, std::uint32_t ship, std::uint32_t now) noexcept {
    const int i = find(g, ship);
    if (i >= 0 && g.m[i].role == Role::committed) return true; // (a committed member's refresh)
    std::uint32_t step = b.step_cur_ms > b.step_prev_ms ? b.step_cur_ms : b.step_prev_ms;
    if (step > kStepClampMs) step = kStepClampMs;
    for (unsigned k = 0; k < Board::kRecentLeaves; ++k)
        if (b.tl_ship[k] == ship && b.tl_ms[k] && std::int32_t(now - b.tl_ms[k]) >= 0 && now - b.tl_ms[k] < kRecommitMs + step) return false;
    return true;
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
    if (in.order == sc::kOrderFlyBefore) { // a new order-4 decision: any earlier P2 belongs to a previous approach
        m.handover_ms = 0;
        m.p2_kept = 0;
    }
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
        // (lane fix round 2 item 3d; round 2 review item 5: the refused bound is game time + the step)
        const std::uint32_t hand_max = g.lane && m.refused ? kLaneRefusedMaxMs + g.step_ms : kHandoverMaxMs;
        // the script's order-0 re-issue gap (<= kGapMs after the last order-4 / 5 read; order 0 zeroes f+0x224) and the P2 hand-over on
        // order 0 / 0x1f with the target zeroed keep the member (Run161: eviction only on 0x12)
        const std::uint32_t gap = age_bound(g, kGapMs);
        if (order == sc::kOrderNone && (target == g.gate || !target) && m.steered_ms && now - m.steered_ms <= gap) return Why::count;
        // B.3 item 2: the KC's order 0 after a 0x12 episode the member was kept through (keep_through_avoid) keeps it like the gap
        if (order == sc::kOrderNone && (target == g.gate || !target) && m.keep_ms && now - m.keep_ms <= gap) return Why::count;
        if (!target && (order == sc::kOrderNone || order == sc::kOrderRotate) && m.handover_ms && now - m.handover_ms <= hand_max)
            return Why::count;
        if (target != g.gate) return Why::target;
        if (order == sc::kOrderFlyBefore || order == sc::kOrderDock) // (order 5 right after the hand-over: its first leaf call commits)
            return now - m.commit_ms > age_bound(g, kCommitFreshMs) && !(order == sc::kOrderDock && m.handover_ms && now - m.handover_ms <= hand_max)
                       ? Why::expire : Why::count;
        if (order == sc::kOrderNone || order == sc::kOrderRotate) return !m.handover_ms || now - m.handover_ms > hand_max ? Why::order : Why::count;
        return order == sc::kOrderWarpAway ? Why::count : Why::order;
    }
    if (m.role == Role::escort) {
        // D.3, D.12: the leader launched (res_until set at its order 8); the escort keeps its reserved number until the reservation lapses.
        // B.3 item 4 (Run163: the script's order 4 at +53 s and +81 s, B.2's 60 s lapsed at +60 s): on any order but a dock or launch leg to
        // another target (it docks elsewhere or leaves the sector); lane_frame refreshes res_until (kReserveMs) while the escort is on 0x15
        // to the gate or within kReserveKeepM of it; its own order 4 commits it
        if (m.key_res && m.res_until && std::int32_t(m.res_until - now) >= 0 &&
            !((order == sc::kOrderDock || order == sc::kOrderWarpAway) && target != g.gate))
            return Why::count;
        const int li = find(g, m.leader);
        if (li < 0 || g.m[li].role != Role::committed || !live(g, g.m[li], now)) return Why::leader;
        if (formation_current) return Why::count; // Run161: the formation memory current => a member, whatever the live order
        // lane fix round 2 item 2 (Run167/168 why=leader): a lane escort the script hands to another leader stays a member while it
        // leaves the lane (lane_frame: its laws off, a predecessor for the ships behind it; plan_of: drift) until it is clear of Lambda's
        // footprint, at most kLaneExitMs + the step after the hand, or its target changes again
        if ((order == sc::kOrderEscort || order == sc::kOrderFollow) && target != m.leader && target && g.lane && m.ln &&
            (m.sb == Sb::inbound || m.sb == Sb::funnel)) {
            if (!m.exit_ms) return Why::count; // (lane_frame stamps the hand from this state read)
            return m.exit_tgt != target || m.exit_clear || std::int32_t(now - m.exit_ms) > std::int32_t(kLaneExitMs + g.step_ms) ||
                           std::int32_t(now - m.exit_ms) < 0
                       ? Why::leader
                       : Why::count;
        }
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

// ---- the pass point and the final lines (D.2.2; the lane's) ----------------------------------------------------------------------
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
// The plant step of sc::plant_step toward an explicit target (tx, ty) in the plane (the lane rollout's carrot); the same arithmetic. k_in (B.9):
// the in-plane share of the step's travel (1 in the plane; an entering ship flying the 3-D line to an in-plane carrot moves d_in / d0 of it
// in the plane and the rest out of it).
__attribute__((always_inline)) inline void plant_step_to(const sc::JoinParams& p, float v_des, float dt, float k_lag, float tx, float ty, float& x,
                                                         float& y, float& psi, float& upsi, float& w, float& v, float k_in = 1.0f) noexcept {
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
    x += k_in * v * cu * dt;
    y += k_in * v * su * dt;
}
// The order-5 handler's aligned speed toward the gate origin (section 10.5): vmax at r_gate, vmax / 2 at r_gate / 5, linear between.
__attribute__((always_inline)) inline float dock_ramp(float d, float r_gate, float v_max) noexcept {
    if (d >= r_gate) return v_max;
    if (d <= 0.2f * r_gate) return 0.5f * v_max;
    return 0.5f * v_max + 0.5f * v_max * (d - 0.2f * r_gate) / (0.8f * r_gate);
}
// ---- priority (D.1 "When the test fails") ----------------------------------------------------------------------------------------
// 1 arrival; 2 the hand-over, order 5 or order 8 (the slot holder); 3 the approach on order 4; 5 escort.
inline unsigned rank(const Member& m) noexcept {
    if (m.role == Role::arrival) return 1;
    if (m.role == Role::escort) return 5;
    const std::uint16_t o = m.st.order;
    if (o == sc::kOrderNone || o == sc::kOrderRotate || o == sc::kOrderDock || o == sc::kOrderWarpAway) return 2;
    return 3;
}
// Whether a yields to b: the lower rank yields; within a rank the higher sequence number.
inline bool yields_to(const Member& a, const Member& b) noexcept {
    // lane stage 1 (section 6.2): two members in the lane order yield by it, whatever their roles (the ship ahead never yields)
    if (a.ln && b.ln && a.lane_rank != 0xff && b.lane_rank != 0xff && a.lane_rank != b.lane_rank) return a.lane_rank > b.lane_rank;
    // ... and a lane member behind a committed member past d_end (the hand-over, order 5, order 8: ahead on the same final, the head's
    // predecessor of lane_frame) yields to it: the ship at the gate cannot yield and is never capped
    if (a.ln && b.ln && (a.lane_rank != 0xff) != (b.lane_rank != 0xff)) {
        const Member& o = a.lane_rank != 0xff ? b : a;
        if (o.role == Role::committed && (o.sb == Sb::handover || o.sb == Sb::dock || o.sb == Sb::launch)) return a.lane_rank != 0xff;
    }
    const unsigned ra = rank(a), rb = rank(b);
    if (ra != rb) return ra > rb;
    if (a.seq != b.seq) return a.seq > b.seq;
    return a.ship > b.ship;
}
// The yield needs a steered order whose speed site B writes: a committed approach on order 4 (the line plan) or an escort on
// 0x14 / 0x15.
// B.9 review: site B tests it on the live order of its call (can_yield_on), not the last observed state's order: the first steered
// write after an order-0 gap applies the cap.
inline bool can_yield_on(const Member& m, std::uint16_t order) noexcept {
    if (m.role == Role::committed) return order == sc::kOrderFlyBefore && m.line_ok;
    if (m.role == Role::escort) return order == sc::kOrderEscort || order == sc::kOrderFollow;
    return false;
}
inline bool can_yield(const Member& m) noexcept { return can_yield_on(m, m.st.order); }

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
    if (m.last_plan == Plan::lane) return m.ln != 0;
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
    if (m.ln) { // lane stage 1: order 4 toward Lambda or on it, the hand-over at the held speed (P2 keeps it, site B holds it on 0 / 0x1f),
                // an escort's 0x14 / 0x15 (the module's lane leaf); the order-0 gap below resumes the lane
        if (m.exit_ms && m.role == Role::escort) return Plan::drift; // lane fix round 2 item 2: handed to another leader, it flies the
                                                                     // engine's leg to it (round 2 review item 2: an escort's hand only)
        if (m.role == Role::committed && o == sc::kOrderFlyBefore && (m.sb == Sb::inbound || m.sb == Sb::funnel)) return Plan::lane;
        if (m.role == Role::committed && (o == sc::kOrderNone || o == sc::kOrderRotate) && m.handover_ms) return Plan::lane;
        if (m.role == Role::committed && o == sc::kOrderNone && m.sb == Sb::dock) return Plan::launch; // P4's frame: order 8 next
        if (m.role == Role::escort && (o == sc::kOrderEscort || o == sc::kOrderFollow)) return Plan::lane;
        // the lost-leader wait (section 6.5): an escort on order 0 after its leader's order 5 / 8 (the reservation) is held at its
        // lane law's speed by site B (lane_site_b) for as long as the script's lambda lasts, not only for kGapMs: the lane rollout, which
        // brakes for the whole horizon when the nose is beyond site B's 8 deg (lane_hold_o0)
        if (m.role == Role::escort && o == sc::kOrderNone && m.res_until && (m.sb == Sb::inbound || m.sb == Sb::funnel)) return Plan::lane;
    }
    if (m.role == Role::committed) {
        if (o == sc::kOrderFlyBefore) return m.line_ok ? Plan::line : Plan::drift; // a lane funnel flies Lambda_4 (line_of)
        if (o == sc::kOrderDock) return m.sb == Sb::dock && m.q_ok ? Plan::line5 : Plan::dock;
        if (o == sc::kOrderRotate) return Plan::hold;
    }
    // order 0 (Run161): the P2 hand-over brakes (order 0 sets the desired speed 0; 0x1f holds); a committed member or an escort in the
    // script's re-issue gap (<= kGapMs after its last steered read) rolls out on its last steered plan at its current speed (an escort:
    // following its leader; a committed ship: its line); anything else brakes along the nose
    if (o == sc::kOrderNone) {
        if (m.role == Role::committed && m.handover_ms) return Plan::brake;
        if ((m.role == Role::committed || m.role == Role::escort) && gap_plan(m)) return m.last_plan;
        return Plan::brake;
    }
    if (m.role == Role::escort && (o == sc::kOrderEscort || o == sc::kOrderFollow)) return Plan::follow;
    return Plan::drift;
}
// B.9 review (item 3): the yield cap in force at the state's read (set within the cap's age and not released; -1 none). In the order-0
// gap a decision whose rollout cleared on it keeps it in force (decide: re-stamped, never released there), so site B applies it when the
// script re-issues the steered order: the speed the gap's rollout resumes at is the speed site B will write.
// The age is taken at now, site B's clock (its test: now - cap_ms <= the cap's age); now 0: the state's read (the fixture's units).
__attribute__((always_inline)) inline float cap_in_force(const Gate& g, const Member& m, std::uint32_t now = 0) noexcept {
    const std::uint32_t t = now ? now : m.st.ms;
    return m.cap >= 0.0f && m.cap_ms && std::int32_t(t - m.cap_ms) <= std::int32_t(age_bound(g, kCapMaxAgeMs)) ? m.cap : -1.0f;
}
// The plan's speed: the module's own speed before a yield cap when known and fresh, else the desired speed site B left.
inline float plan_speed(const Gate& g, const Member& m, std::uint32_t now = 0) noexcept {
    const std::uint32_t cap_age = age_bound(g, kCapMaxAgeMs); // B.3 item 1
    if (m.st.order == sc::kOrderNone) { // the order-0 gap: the last plan's speed (the rollout brakes for the gap's rest first)
        // B.9: the resume speed is the one site B writes once the script re-issues the order: the module's plan speed, under the yield
        // cap when it is still in force (cap_in_force; the first decision on the steered order after the gap re-yields or releases it)
        if (m.v_plan > 0.0f && m.v_plan_ms && std::int32_t(m.st.ms - m.v_plan_ms) <= std::int32_t(kGapMs + cap_age)) {
            const float c = cap_in_force(g, m, now);
            return c >= 0.0f && c < m.v_plan ? c : m.v_plan;
        }
        return m.st.v > 0.0f ? m.st.v : 0.0f;
    }
    // B.9 review: the age is signed: site A records v_plan at each decision, after site B's last state read (st.ms, every 100 ms), so
    // a fresh v_plan is often newer than the state; unsigned, that wrapped and fell back to the capped v_des (the yielder's ladder then
    // started from its own cap)
    if (m.v_plan > 0.0f && m.v_plan_ms && std::int32_t(m.st.ms - m.v_plan_ms) <= std::int32_t(cap_age)) return m.v_plan;
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
// The order-4 line a member's rollout flies: the grid line (X, h), or in the lane's funnel Lambda_4 (through the point at X's depth,
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
// ---- Lane stage 1 (convoy-gate-transit.md sections 6.1-6.5; ship_steering_gates_lane): geometry and the speed laws ------------------
// One departure lane Lambda per gate: G + l_c (R, U) + s F for s in [-kLaneFarM, -kLaneEndM], l_c the middle of the members' finals; over
// the kLaneBlendM before d_end each final moves to its own pass point Q (q_point's window on the lower half; Lambda_5 continues it to
// the plane). Members join Lambda like cars at a lane merge: the lane order ranks by the ETA to d_end (a tie band, the higher sequence
// key yields, a leader ahead of its own escorts), the later ship yields (merge law), then follows (follow law); the ship ahead is never
// slowed for the pair. Each capital's order 4 ends at its lane point L_i (site A's f+0x240 rewrite, lane_order_point), d_end before the
// plane. The design values below are measured in the fixture (scenario C; verification/results/ship-steering/convoy-gate-transit/).
// The X3M_LANE_* macros exist for the fixture's sensitivity builds only (scenario_c.py); production builds take the defaults.
#ifndef X3M_LANE_GAP_M
#define X3M_LANE_GAP_M 500.0f // design 400 m; measured (scenario C): at 400 m the sweep's own margin at its 30 s horizon (50 m + 10 m/s x 30 s
#endif                        // + the rollouts' offset terms, 360-410 m) leaves a follower at g_req unresolved: site E passes the pair
#ifndef X3M_LANE_TAU_MERGE_S
#define X3M_LANE_TAU_MERGE_S 4.0f
#endif
#ifndef X3M_LANE_TAU_FOLLOW_S
#define X3M_LANE_TAU_FOLLOW_S 10.0f
#endif
#ifndef X3M_LANE_STOP_MS
#define X3M_LANE_STOP_MS 20000u
#endif
#ifndef X3M_LANE_LAMBDA_MAX_MS
#define X3M_LANE_LAMBDA_MAX_MS 17000u
#endif
#ifndef X3M_LANE_CRAWL_M
#define X3M_LANE_CRAWL_M 200.0f
#endif
#ifndef X3M_LANE_END_M
#define X3M_LANE_END_M 2300.0f
#endif
#ifndef X3M_LANE_ESC_JOIN_K
#define X3M_LANE_ESC_JOIN_K 1.0f // an escort's join completes this x lambda_max at the lane speed earlier (lane_join_end)
#endif
constexpr float kLaneGapM = X3M_LANE_GAP_M;                // g_req: tail(P) - nose(Y) along F once merged
constexpr float kLaneTauMergeS = X3M_LANE_TAU_MERGE_S;     // tau_m: v_Y = v_P - (g_req - gap) / tau_m during the merge (floor 10 m/s);
                                                           // 4 s (design 8 s): the level M1's merge at its bang-bang limit (scenario C)
constexpr float kLaneTauFollowS = X3M_LANE_TAU_FOLLOW_S;   // tau_f: v_Y = min(v_lane, v_P + (gap - g_req) / tau_f) after it (floor 10 m/s)
constexpr std::uint32_t kLaneStopMs = X3M_LANE_STOP_MS;    // T_stop: a brief stop lasts at most this + the game step (Gate::step_ms)
constexpr std::uint32_t kLaneLambdaMaxMs = X3M_LANE_LAMBDA_MAX_MS; // lambda_max: the script's order 4 comes at most this (+ the frame
                                                                    // step) after the leader's launch (measured +4.1..+16.9 s, section 10.12):
                                                                    // the horizon of the lost-leader wait's order-0 hold; the escort gate's
                                                                    // braking reserve (lane_reserve) holds for any latency
constexpr float kLaneCrawlM = X3M_LANE_CRAWL_M; // the reserve's last stretch before d_end at the 10 m/s floor (then the stop 50 m short)
constexpr float kLaneEndM = X3M_LANE_END_M;                // d_end: order 4 ends this far before the plane (the short final, section 6.4)
// The installed gate rows (ship-navigation-ai.md section 10.9): the ordinary rows 0..3 and 5..14, row 4 (Xenon, a blended disc only) and
// rows 15..20 (inactive hubs, an empty body stub) carry no collision mesh; the Terran rows 21..24 carry the ring.
__attribute__((always_inline)) inline bool gate_no_mesh(std::uint16_t type) noexcept { return type <= 20; }
__attribute__((always_inline)) inline bool gate_terran(std::uint16_t type) noexcept { return type >= 21 && type <= 24; }
// Lane fix round 2 item 4 (review 1, 4, 5, 10): the gate's solid for the lane's clearance tests, a ring about the axis in the gate frame:
// radial (from the axis) in [h, r_out] over z (along F from G) in [z0, z1], in r_gate (gate_hole.txt, section 10.9: the installed Terran
// ring h 0.663, outer 1.026, body z -0.498..+0.179; the ordinary rows none (no collision body: a ship can never touch them); an unknown
// row the most conservative of the measured rings: the stock Argon hole 0.605, the outer 1.026, z -0.651..+0.655).
struct GateSolid {
    bool solid = false;
    float h = 0, r_out = 0, z0 = 0, z1 = 0; // m
};
inline GateSolid gate_solid(std::uint16_t type, float r_gate) noexcept {
    GateSolid s;
    if (gate_no_mesh(type) || !(r_gate > 0.0f)) return s;
    s.solid = true;
    const bool t = gate_terran(type);
    s.h = (t ? 0.663f : 0.605f) * r_gate;
    s.r_out = 1.026f * r_gate;
    s.z0 = (t ? -0.498f : -0.651f) * r_gate;
    s.z1 = (t ? 0.179f : 0.655f) * r_gate;
    return s;
}
// d_end of a gate: kLaneEndM, or on a gate with a solid at least its front + the longest hull (kLaneHullMaxM) + the stop's run-out of a
// hand-over at the lane speed (kLaneRunMaxM: 100 m/s at the slowest capital's 5 m/s^2, 1,000 m, and 1 s) + the sweep margin + the
// tracking allowance, so a member on its final at d_end (the hand-over until order 5, which the pair filter exempts from contact with
// its gate) is clear of the ring with its nose: the installed Terran ring (r 3207.5 m) 1597 + 2550 = 4147 m; every gate without a solid
// kLaneEndM. (Round 2 review item 10: kLaneHullMaxM is the design hull, not the installed roster's longest: 40 of the 131 installed
// capitals exceed 1,300 m in radius or hull half-extent, M2 to 2,998 m, M1 to 4,583 m, TL 1,837 m, M7 1,431 m (ships_by_class.json,
// convoy-gate-transit/round2_review/long_hulls.txt). For those the premise above does not hold; the own-gate row tests each member's own
// hull (L, w) against the solid on its path and over the hand-over's run-out, so such a hull is guarded there, never suppressed into the
// ring: the engine's warn and its 0x12, an eviction from the lane.)
constexpr float kLaneHullMaxM = 1300.0f; // the design hull half-length (installed medians: M1 / TL 1,135 m, M2 1,337 m)
constexpr float kLaneRunMaxM = 1100.0f;
inline float lane_end(float r_gate, std::uint16_t type) noexcept {
    const GateSolid s = gate_solid(type, r_gate);
    const float e = s.solid ? -s.z0 + kLaneHullMaxM + kLaneRunMaxM + 50.0f + 100.0f : 0.0f;
    return e > kLaneEndM ? e : kLaneEndM;
}
// A member's d_end (Member::d_end, lane_frame's copy of its gate's lane_end; 0 before: kLaneEndM)
__attribute__((always_inline)) inline float dend(const Member& m) noexcept { return m.d_end > 0.0f ? m.d_end : kLaneEndM; }
constexpr float kLaneFarM = 15000.0f;              // Lambda's far end
constexpr float kLaneBlendM = 2000.0f;             // the finals' lateral change from l_c to Q (<= 500 m over >= 2 km)
constexpr float kLaneOnM = 100.0f, kLaneOffM = 1500.0f; // on Lambda (its merge point M_i passed) within kLaneOnM laterally with the nose
constexpr float kLaneOnCos = 0.99862953f;               // within 3 deg of F; off beyond kLaneOffM or with the nose away (hysteresis)
constexpr float kLaneShiftMs = 5.0f; // l_c follows the members' middle at most this fast (m/s of game time): a member leaving or joining
                                     // moves the line under the others slowly (a 165 m shift over 33 s, about 0.3 deg)
constexpr float kLaneSideM = 300.0f; // a yielder level with its predecessor closes laterally to w_P + w_Y + this, never nearer, until gap >= g_req - 100 m
constexpr float kLaneHoldCos = 0.99026807f;        // cos 8 deg: site B holds the lane speed on order 0 / 0x1f with the nose this close to F
constexpr float kLaneJoinHoldCos = 0.70710678f;    // cos 45 deg (the steepest join): a joining member's order-0 hold (lane_hold_o0)
constexpr float kLaneJoinEndM = 1500.0f; // ... and completes this far before d_end (steeper when needed, at most 45 deg)
constexpr std::uint32_t kLaneStopEndMs = 2000; // lane fix round 1 item 4: a law stop episode ends this long (+ the step) after its condition
constexpr float kLaneOverApexR = 3.0f;          // ... an overshoot's U-turn reaches at most this x its turn radius beyond its turn point
constexpr float kLaneRedepM = 3000.0f;             // a re-departing ship flies out to d_end + this before it turns in (section 6.1), ...
constexpr float kLaneRedepSideM = 1000.0f;         // ... this far below Lambda (-U, the departure side): never head-on along the trail,
constexpr float kLaneTurnM = 1000.0f;              // ... then turns up and around onto Lambda: the carrot this far ahead (+F) and above (+U)
                                                   // of it (a capital's turn diameter ~1.1-1.4 km ends near Lambda, ~1.5 km below the arrivals)
constexpr float kLaneTieS = 2.0f;                  // the merge rank's tie band (section 6.2)
constexpr float kLaneKeepS = 5.0f;                 // the lane order's hysteresis: a pair keeps its order unless the ETAs differ by more
constexpr float kLaneGridTolM = 1.0f;              // the order-4 point guard: f+0x240 within this of a KC grid point (section 6.4)
constexpr std::uint32_t kLanePollMs = 11000;       // the KC's longest poll after a refused through slot (section 6.4's brake bound)
constexpr float kLaneTrigMarginM = 300.0f;         // ... which keeps the refused ship this far outside its trigger sphere
constexpr float kLaneStopV = 0.5f;                 // a predecessor slower than this is stopped (its follower stops at spacing)
constexpr std::uint32_t kLaneRefuseMs = 5000;      // a member at the hand-over this long without order 5 counts as refused (+ the step)
// Review item 6: a merging yielder is beside the ship ahead while the raw gap (tail to nose along F) is short of this: its speed law does
// not settle (nor its lateral leave the clearance band) before it is behind
constexpr float kLaneBesideM = 0.0f;
// The lane speed: the large cap (100 m/s) under the hull's v_max (site B's gate speed without the cornering term).
__attribute__((always_inline)) inline float lane_vmax(const Member& m) noexcept { return sc::gate_speed(m.params.v_max, m.large != 0, 0.0f); }
// A hull's final Q (section 6.1): q_point's window nearest straight below the axis (A_lat far down -U), the lower half of the disc.
__attribute__((always_inline)) inline bool lane_q(float r_s, float w, float L, float v_max, float r_gate, bool terran, float q[2]) noexcept {
    const float a_lat[2] = {0.0f, -4.0f * r_gate};
    float l4[2];
    return q_point(r_s, w, L, v_max, r_gate, a_lat, q, l4, terran);
}
// Lambda's lateral (R, U) for member m at s along F: l_c beyond the blend, m's Q from d_end + its settling distance on, linear between. A member that entered as
// an escort stays on l_c to d_end (no blend; order 5's Lambda_5 takes it to Q): the script's order 4 reaches it near d_end, inside the
// blend (a late blend would turn it off F at P2), and order 4's first tick tests the KC grid point
// before site A's rewrite takes effect (ship-navigation-ai.md section 10.12 item 2), so the escort must not sit in a grid sphere then:
// l_c lies at least 0.75 r_gate + kLaneGridClearM below the axis (outside the spheres of the grid's first row, slots 0-4), a final Q
// of an M7 (|Q| <= 667 m) lies inside slot 0's at the grid depth.
constexpr float kLaneGridClearM = 100.0f;
// Lane fix round 1 item 3 (Run173: the leader's P2 with the nose 22 deg off F): the blend ends a settling distance before d_end, the
// hull's pursuit look-ahead at the lane speed (pursuit_look: 2 v / W, a turn diameter), so the nose is back along F at P2.
__attribute__((always_inline)) inline float lane_blend_settle(const Gate& g, const Member& m) noexcept {
    return m.lane_settle > 0.0f ? m.lane_settle : pursuit_look(lane_vmax(m), m.params.W, g.r_gate);
}
__attribute__((always_inline)) inline void lane_lat_at(const Gate& g, const Member& m, float s, float lat[2]) noexcept {
    float k = (s + dend(m) + lane_blend_settle(g, m) + kLaneBlendM) / kLaneBlendM;
    k = m.role == Role::escort || m.lane_esc ? 0.0f : k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k;
    lat[0] = g.lane_c[0] + k * (m.q[0] - g.lane_c[0]);
    lat[1] = g.lane_c[1] + k * (m.q[1] - g.lane_c[1]);
}
__attribute__((always_inline)) inline void lane_point_at(const Gate& g, const Member& m, float s, float P[3]) noexcept {
    float lat[2];
    lane_lat_at(g, m, s, lat);
    for (unsigned i = 0; i < 3; ++i) P[i] = g.G[i] + lat[0] * g.R[i] + lat[1] * g.U[i] + s * g.F[i];
}
// L_i (section 6.4): order 4's own test (dist <= 0.75 r_gate, 0x00454b1a) fires at d_end when its point lies d_end - 0.75 r_gate before
// the plane on the hull's final (the note's "d_end + 0.75 r_gate" read as the arrival depth; Implementation section).
__attribute__((always_inline)) inline float lane_point_depth(float r_gate, std::uint16_t type = 0) noexcept {
    return lane_end(r_gate, type) - sc::kGateCrossFraction * r_gate;
}
inline void lane_order_point(const Gate& g, const Member& m, float L[3]) noexcept {
    const float z = -lane_point_depth(g.r_gate, g.type);
    const float lr = m.lane_esc ? g.lane_c[0] : m.q[0], lu = m.lane_esc ? g.lane_c[1] : m.q[1]; // (an escort's final: l_c, lane_lat_at)
    for (unsigned i = 0; i < 3; ++i) L[i] = g.G[i] + lr * g.R[i] + lu * g.U[i] + z * g.F[i];
}
// The KC grid point guard (section 6.4): p (world, m) within kLaneGridTolM of G - 2 (r_gate + r_s) F + 666 m (a R + b U), a in -2..2,
// b in -3..0 (the 20 slots of GetWarpInPos, ship-navigation-ai.md section 10.2). Returns the slot (0..19) or -1.
inline int kc_grid_slot(const float G[3], const float R[3], const float U[3], const float F[3], float r_gate, float r_s, const float p[3]) noexcept {
    if (!(r_gate > 0.0f) || !(r_s > 0.0f) || !sc::finite_f(p[0]) || !sc::finite_f(p[1]) || !sc::finite_f(p[2])) return -1;
    const float d[3] = {p[0] - G[0], p[1] - G[1], p[2] - G[2]};
    const float x = sc::dot3(d, R), y = sc::dot3(d, U), z = sc::dot3(d, F);
    if (sc::fabs_f(z + 2.0f * (r_gate + r_s)) > kLaneGridTolM) return -1;
    const float pitch = sc::kGateGridPitchM;
    const float af = x / pitch, bf = y / pitch;
    if (!(af > -2.5f && af < 2.5f && bf > -3.5f && bf < 0.5f)) return -1;
    const int a = int(af + (af >= 0.0f ? 0.5f : -0.5f)), b = int(bf + (bf >= 0.0f ? 0.5f : -0.5f));
    if (sc::fabs_f(x - float(a) * pitch) > kLaneGridTolM || sc::fabs_f(y - float(b) * pitch) > kLaneGridTolM) return -1;
    static const int col[5] = {4, 2, 0, 1, 3}; // a = -2, -1, 0, 1, 2 (the KC's column order 0, 1, -1, 2, -2)
    return -b * 5 + col[a + 2];
}
// Whether p (world, m) is a lane point L_i of a hull with the final q (R, U; m) at this gate: its depth within the flight validator's
// 10 % of L_i's depth (sc::kGateGridDepthTol; a KC grid point lies at 2 (r_gate + r_s), never there) and its lateral within its 20 m
// (sc::kGateGridTolM) of q or, when lc is given, of Lambda's l_c (an escort's final; l_c moves at most kLaneShiftMs, so a written point
// read back seconds later is not compared to the live l_c within 1 m: review item 4).
inline bool lane_point_match(const float G[3], const float R[3], const float U[3], const float F[3], float r_gate, const float q[2], const float* lc,
                             const float p[3], std::uint16_t type = 0) noexcept {
    if (!(r_gate > 0.0f) || !sc::finite_f(p[0]) || !sc::finite_f(p[1]) || !sc::finite_f(p[2])) return false;
    const float d[3] = {p[0] - G[0], p[1] - G[1], p[2] - G[2]};
    const float x = sc::dot3(d, R), y = sc::dot3(d, U), z = sc::dot3(d, F), depth = lane_point_depth(r_gate, type);
    if (!(depth > 0.0f) || sc::fabs_f(-z - depth) > sc::kGateGridDepthTol * depth) return false;
    const float tol2 = sc::kGateGridTolM * sc::kGateGridTolM;
    const float dq = (x - q[0]) * (x - q[0]) + (y - q[1]) * (y - q[1]);
    if (dq <= tol2) return true;
    return lc && (x - lc[0]) * (x - lc[0]) + (y - lc[1]) * (y - lc[1]) <= tol2;
}
// Whether p is member m's lane point L_i (a written point read back: the field still holds ours after a slot reset or a load).
inline bool lane_point_ours(const Gate& g, const Member& m, const float p[3]) noexcept {
    return m.q_ok && lane_point_match(g.G, g.R, g.U, g.F, g.r_gate, m.q, g.lane_c_ok ? g.lane_c : nullptr, p, g.type);
}
// Review item 4: whether f+0x240 (p) is this hull's own lane point when the ship has no lane member (a load mid-approach, a GateState
// slot reset after a > 5 s order-0 gap): its final Q from its hull alone (lane_q, the lower half), compared as lane_point_match; lc the
// gate board's l_c when it has one (an escort's point). The caller then gives the order's geometry the KC's slot-0 grid point.
inline bool lane_point_own(const float G[3], const float R[3], const float U[3], const float F[3], float r_gate, float r_s, float w, float L,
                           float v_max, bool terran, const float* lc, const float p[3], std::uint16_t type = 0) noexcept {
    float q[2];
    if (!lane_q(r_s, w, L, v_max, r_gate, terran, q)) return false;
    return lane_point_match(G, R, U, F, r_gate, q, lc, p, type);
}
// Final review item 4: whether p could be an escort's L_i written on some l_c, for a ship with no lane member and no board l_c (a load:
// the board is rebuilt from the members' commits, so lane_point_own has no lc). l_c is the middle of the members' finals, all inside
// the gate's disc in its lower half, and lies at least 0.75 r_gate + kLaneGridClearM below the axis (lane_frame's clamp): p at L_i's
// depth (as lane_point_match: within 10 %, never a KC grid point's 2 (r_gate + r_s)) with its lateral inside the disc and below that
// clearance (+ the 20 m tolerance).
inline bool lane_point_lc_env(const float G[3], const float R[3], const float U[3], const float F[3], float r_gate, const float p[3],
                              std::uint16_t type = 0) noexcept {
    if (!(r_gate > 0.0f) || !sc::finite_f(p[0]) || !sc::finite_f(p[1]) || !sc::finite_f(p[2])) return false;
    const float d[3] = {p[0] - G[0], p[1] - G[1], p[2] - G[2]};
    const float x = sc::dot3(d, R), y = sc::dot3(d, U), z = sc::dot3(d, F), depth = lane_point_depth(r_gate, type);
    if (!(depth > 0.0f) || sc::fabs_f(-z - depth) > sc::kGateGridDepthTol * depth) return false;
    const GateSolid gs = gate_solid(type, r_gate); // (lane fix round 2 item 4: on a gate with a solid l_c lies inside its hole)
    if (gs.solid) return y <= 0.0f && x * x + y * y <= gs.h * gs.h;
    return y <= -(sc::kGateCrossFraction * r_gate + kLaneGridClearM) + sc::kGateGridTolM && x * x + y * y <= r_gate * r_gate;
}
// One member's law at one moment (lane_frame on the live state, the rollout on its samples): its along position s, the lane speed, the
// predecessor's gap and along speed, the head's pacing (seconds until the through slot is free) and an escort's gate (seconds until its
// leader's launch + lambda_max). Returns the speed (0: a stop) and the law; *merged (the law's state) becomes true once the settling gap is g_req - 50 m.
struct LaneCtx {
    float s = 0, v_lane = 0, a = 1.0f, v_self = 0, y = 0; // y: the lateral still to join (m; 0 on Lambda)
    float loss = 0; // the along-track distance the pair's joins will still cost the gap: the member's own less its predecessor's (lane_join_loss)
    bool pred = false, merged = true;
    float gap = 3.4e38f, v_pred = 0;
    bool head = false, gate = false;
    float t_free_s = 0, t_gate_s = 0; // t_gate_s: until the escort's latest expected order 4 (its leader's launch + lambda_max + the step)
    float dt = 0;                     // the frame's game step (s): the reserve's one step of reaction
    // lane fix round 1 item 4 (Run173: E1 written 0 for 32.6 s behind a predecessor flying away from the gate after its 0x12): the ship
    // ahead is physically stopped (a block: its speed under kLaneStopV), else the law's stop lasts at most T_stop + the step (stop_left_s:
    // what is left of it) and then releases to the floor
    bool block = false;
    float stop_left_s = 3.4e38f;
    float d_end = kLaneEndM; // lane fix round 2 item 4: the member's d_end (dend)
    // lane fix round 2: the ship ahead flies back toward the follower along F (its along speed below -kLaneStopV): never a release into it
    // (item 5); it is inside its final (past d_end - its settling distance, or in the hand-over): the follow gap covers the follower's own
    // stop, g_req + v^2 / 2a, so a P2 that drops its speed at d_end leaves a resolvable pair (item 3b)
    bool closing = false, pred_final = false;
    // lane fix round 2 item 3d: the ship ahead has its grant (order 5 or 8 seen): it leaves along F, so its standstill is no stop at
    // spacing (the queue hold behind a refused leader ends within the step of its grant, not once its speed passes kLaneStopV)
    bool granted = false;
};
// The escort gate's braking reserve (section 6.5, review item 1): the highest speed at dist (m along F to d_end) from which a ship at a
// (m/s^2) still stops kLaneStopShortM short of d_end: an escort without its order 4 never passes d_end whatever the script's latency.
// Before the latest expected order (t_left s, lambda_max after its leader's launch) the stop is preceded by a crawl at the 10 m/s floor
// as long as the time left allows (at most kLaneCrawlM): a late order finds it crawling, not stopped; lambda_max is only where the crawl
// runs out and the stop binds.
constexpr float kLaneStopShortM = 50.0f;
constexpr std::uint32_t kLaneCrawlSlackMs = 2000; // the crawl's budget runs this long past the latest expected order (the last metres
                                                  // before the stop are slower than 1 m/s)
// (each curve keeps one frame step dt of travel before the braking: v dt + (v^2 - v_f^2) / 2a <= D, so a SETA step cannot carry the ship
// past the stop)
__attribute__((always_inline)) inline float lane_reserve(float dist, float a, float v_self, float t_left, float dt) noexcept {
    const float am = a > 0.1f ? a : 0.1f, rest = dist - kLaneStopShortM, ad = am * (dt > 0.0f ? dt : 0.0f);
    if (!(rest > 0.0f)) return 0.0f;
    const float vb = sc::sqrt_ss(ad * ad + 2.0f * am * rest) - ad; // the stop
    const float t_br = v_self > kYieldFloorMs ? (v_self - kYieldFloorMs) / am : 0.0f;
    float crawl = kYieldFloorMs * (t_left - t_br);
    if (!(crawl > 0.0f)) return vb;
    if (crawl > kLaneCrawlM) crawl = kLaneCrawlM;
    const float vc = rest > crawl ? sc::sqrt_ss(ad * ad + 2.0f * am * (rest - crawl) + kYieldFloorMs * kYieldFloorMs) - ad : kYieldFloorMs;
    return vc < vb ? vc : vb;
}
inline float lane_law(const LaneCtx& c, Law* law, bool* merged, bool* stop_cond = nullptr) noexcept {
    float v = c.v_lane;
    if (stop_cond) *stop_cond = false;
    *law = Law::lane;
    *merged = c.merged;
    if (c.pred) {
        // the gap the pair settles at once the speeds match (the speed difference's distance at a: a yielder still slower than the ship
        // ahead keeps losing ground while it re-accelerates; the large cap is both ships' top speed, so a gap opened too wide never closes)
        // ... and the along-track distance the joins onto Lambda will still cost at the shared cap (its own less the predecessor's)
        const float am = c.a > 0.1f ? c.a : 0.1f, dv = c.v_pred - c.v_self;
        const float gap_v = c.gap + dv * (dv > 0.0f ? dv : -dv) / (2.0f * am), gap = gap_v + c.loss;
        // the speed law moves from merge to follow (and the yielder's lateral is released onto Lambda) once the settling gap is
        // g_req - 50 m and the yielder is no longer beside the ship ahead (the raw gap >= kLaneBesideM: review item 6; HEAD settled at a raw
        // gap of -85 m on the speed term of a yielder at the floor). While merging, the joins' loss is credited only up to what keeps the
        // settled raw gap >= kLaneBesideM (the yielder holds its lateral until then). *merged is the law's state; the merge's completion
        // for the row and the bounds is the raw gap's g_req - 50 m (lane_frame)
        const float credit_max = kLaneGapM - 50.0f - kLaneBesideM;
        const float gap_m = gap_v + (c.loss < credit_max ? c.loss : credit_max);
        if (!*merged && gap_m >= kLaneGapM - 50.0f && c.gap >= kLaneBesideM) *merged = true;
        if (!*merged) { // the merge law: open the gap at tau_m, the yielder's floor 10 m/s
            float vm = c.v_pred - (kLaneGapM - gap_m) / kLaneTauMergeS;
            if (vm < kYieldFloorMs) vm = kYieldFloorMs;
            if (vm < v) v = vm;
            *law = Law::merge;
        } else { // the follow law (lane fix round 2 item 3b: behind a predecessor inside its final the gap also covers the own stop)
            const float g_need = c.pred_final ? kLaneGapM + c.v_self * (c.v_self > 0.0f ? c.v_self : 0.0f) / (2.0f * am) : kLaneGapM;
            float vf = c.v_pred + (gap - g_need) / kLaneTauFollowS;
            if (vf < kYieldFloorMs) vf = kYieldFloorMs;
            if (vf < v - 0.5f) {
                v = vf;
                *law = Law::follow;
            }
        }
        if (((c.v_pred < kLaneStopV && !c.granted) || c.closing) && gap < kLaneGapM) { // the ship ahead stopped (a block) or flying back: stop at spacing
            if (stop_cond) *stop_cond = true;                              // behind it (its braking counted)
            if (c.block || c.closing || c.stop_left_s > 0.0f) {
                v = 0.0f;
                *law = Law::stop;
            } else if (v > kYieldFloorMs) { // (lane fix round 1 item 4: not a block, past T_stop + the step: the floor)
                v = kYieldFloorMs;
                if (*law == Law::lane) *law = *merged ? Law::follow : Law::merge;
            }
        }
    }
    const float dist = -c.d_end - c.s; // to d_end along F
    // the path still to fly to d_end (the pacing laws): a joining ship flies the straight join, not the along-track distance (lane_eta's)
    const float path = c.y > kLaneOnM && dist > 0.0f ? sc::sqrt_ss(dist * dist + c.y * c.y) : dist;
    const float a = c.a > 0.1f ? c.a : 0.1f;
    const float vb = dist > 50.0f ? sc::sqrt_ss(2.0f * a * (dist - 50.0f)) : 0.0f; // the braking curve to a stop at d_end
    if (c.head && c.t_free_s > 0.0f && v > 0.0f) { // head pacing: at d_end no earlier than the slot's free time, then the stop there
        float vp = dist > 0.0f ? path / c.t_free_s : 0.0f;
        if (vp < kYieldFloorMs) vp = kYieldFloorMs;
        if (vb < vp) vp = vb;
        if (vp < v) {
            v = vp;
            *law = v > 0.0f ? Law::pace : Law::stop;
        }
    }
    // the escort gate (section 6.5, review item 1): an escort without its order 4 flies the braking reserve to d_end (lane_reserve),
    // whatever its leader's launch and the script's latency; no pacing on a predicted moment
    if (c.gate && v > 0.0f) {
        const float vg = lane_reserve(dist, a, c.v_self, c.t_gate_s, c.dt);
        if (vg < v) {
            v = vg;
            *law = v > 0.0f ? Law::gate : Law::stop;
        }
    }
    return v;
}
// The order-0 / 0x1f hold of a member in the hand-over refused at the through slot (section 6.4): the speed stands while the longest poll
// keeps the ship outside its trigger sphere + kLaneTrigMarginM, else the brake at a to a stop there.
__attribute__((always_inline)) inline float lane_refused_hold(const Gate& g, const Member& m, const float x[3], float v) noexcept {
    const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
    const float room = sc::sqrt_ss(sc::dot3(d, d)) - (m.r + v * 0.1f) - kLaneTrigMarginM;
    if (room > v * float(kLanePollMs) * 0.001f) return v;
    const float a = m.params.a_lin > 0.1f ? m.params.a_lin : 0.1f, vb = room > 0.0f ? sc::sqrt_ss(2.0f * a * room) : 0.0f;
    return vb < v ? vb : v;
}
// Where member m's join onto Lambda completes (m before the plane): kLaneJoinEndM before d_end; an escort's (it entered as one) a further
// lambda_max at the lane speed, so that its lost-leader wait (order 0: no leaf, the nose held, lane_hold_o0) begins on Lambda and not in
// the join's last turn (scenario C crossing: an escort joining at 35 deg reached P2 75-100 m off Lambda with the nose beyond 8.1 deg)
__attribute__((always_inline)) inline float lane_join_end(const Member& m) noexcept {
    return dend(m) + kLaneJoinEndM + (m.role == Role::escort || m.lane_esc ? X3M_LANE_ESC_JOIN_K * lane_vmax(m) * float(kLaneLambdaMaxMs) * 0.001f : 0.0f);
}
// Lane fix round 1 item 1 (Run173: the old carrot, kLaneJoinK x the lateral ahead on Lambda, was a fixed point at s = -3.8 km for a ship
// 15 km off Lambda 45 km out: the ships flew ~20 deg off the axis to 3 km out and turned onto Lambda there). The join is a tangent onto
// Lambda: from the ship at (s, y) the reference path is the parabola y_ref(s') = y ((s_m - s') / (s_m - s))^2, tangent to Lambda at its
// merge point s_m (its vertex: the parabola through any later point of itself with the same s_m is the same curve, so the stateless rule
// re-planned every frame flies one path). Its steepest angle is at its start, atan(2 y / D) with D = s_m - s, and its vertex curvature
// 2 y / D^2. The span a join needs: D >= 2 y / tan 30 deg and D >= sqrt(2 y R) (the vertex within the hull's turn radius R = v / W). The
// merge point: Lambda's far end (15 km) when that span fits, else kLaneMergeM (8 km), else the join's end (lane_join_end) with the room
// it leaves (steeper, the start at most 45 deg: D >= 2 y), past it the room to d_end, else 2 y (the residual of an order-0 coast). A ship
// following its parabola keeps the span (both D_need and D shrink as the parabola's D^2), so the merge point changes only toward the
// earlier candidate (a ship far out and wide starts on the join's end and moves to 8 or 15 km once that fits: a step of a few degrees).
// R = 0: no turn term (lane_join_loss).
constexpr float kLaneJoinTan = 0.57735027f; // tan 30 deg: the join's steepest angle where the room allows
constexpr float kLaneMergeM = 8000.0f;      // the merge point when Lambda's far end does not fit
// A member's turn radius at its lane speed (m; 0 without a turn rate): the R of lane_join_span for lane_join_loss.
__attribute__((always_inline)) inline float lane_turn_r(const Member& m) noexcept {
    const float v = lane_vmax(m) > kYieldFloorMs ? lane_vmax(m) : kYieldFloorMs;
    return m.params.W > 1e-4f ? v / m.params.W : 0.0f;
}
__attribute__((always_inline)) inline float lane_join_span(float s, float y, float R, float join_end, float d_end = kLaneEndM) noexcept {
    const float yy = y > 0.0f ? y : 0.0f;
    float need = 2.0f * yy / kLaneJoinTan;
    if (2.0f * yy * R > need * need) need = sc::sqrt_ss(2.0f * yy * R); // (the turn term only where it binds: small laterals)
    if (-kLaneFarM - s >= need) return -kLaneFarM - s;
    if (-kLaneMergeM - s >= need) return -kLaneMergeM - s;
    const float room = -join_end - s, room_end = -d_end - s;
    return room >= 2.0f * yy ? room : room_end >= 2.0f * yy ? room_end : 2.0f * yy;
}
// The along-track distance a member at s with the lateral y still to join loses against a ship on Lambda at the same speed: the
// parabola's arc length less its span, D f(m) with m = 2 y / D its start slope, f(m) = (m sqrt(1 + m^2) + asinh m) / m^2 - 1 =
// m^2 / 6 - m^4 / 40 + m^6 / 112 - ...: y (m / 3 - m^3 / 20 + m^5 / 56) (within 2 % for m <= 1). (Before round 1: y tan(theta / 2) for
// the straight join; scenario C crossing: an escort joining at 37 deg behind its leader at 34 deg gained 450 m of gap over 130 s.)
// (lane fix round 2 item 6: R, the hull's turn radius at the lane speed (lane_turn_r), as lane_carrot's span: with R = 0 the span of a
// small lateral, where the turn term binds, was the 30 deg one and the loss credited to the pair larger than the join flown)
__attribute__((always_inline)) inline float lane_join_loss(float s, float y, float join_end, float d_end = kLaneEndM, float R = 0.0f) noexcept {
    if (!(y > kLaneOnM)) return 0.0f;
    const float D = lane_join_span(s, y, R, join_end, d_end);
    float m = D > 1.0f ? 2.0f * y / D : 1.0f;
    if (m > 1.0f) m = 1.0f;
    const float m2 = m * m;
    return y * m * (1.0f / 3.0f - m2 * (1.0f / 20.0f) + m2 * m2 * (1.0f / 56.0f));
}
// Site B's order-0 hold (section 6.5) for a lane member at x with the nose f (site B on the live state, the rollout on the member's
// state): the speed it writes (< 0: none, order 0 brakes) and for how long the straight coast below applies (t_s). The nose within 8 deg
// of F: the lane law's speed v_sb. The nose within 45 deg of F (a join line, or a nose not yet settled on Lambda): on order 0 the engine
// consumes the last leaf's turn remainder and then flies straight (ship-navigation-ai.md section 10.11 Q4), so the hold writes the highest
// speed up to v_sb whose straight run over the hold's remaining horizon never takes the ship further from Lambda than it is or
// kLaneDriftM (it may cross Lambda: the 8 deg rule drifts sin 8 deg x 100 m/s x lambda_max = 237 m too) and, while it is level with its
// predecessor, never nearer than the clearance band w_P + w_Y + kLaneSideM. The horizon: the lost-leader wait to its leader's launch +
// lambda_max + the step (an escort with the reservation), else the script's re-issue gap (kGapMs after the last steered read,
// SETA-aware); past it only the 8 deg rule holds. The rollout coasts straight at this speed for t_s (review item 5).
#ifndef X3M_LANE_DRIFT_M
#define X3M_LANE_DRIFT_M 300.0f
#endif
constexpr float kLaneDriftM = X3M_LANE_DRIFT_M;
struct LaneHold {
    float v = -1.0f, t_s = 0.0f;
};
inline LaneHold lane_hold_o0(const Gate& g, const Member& m, const float x[3], const float f[3], std::uint32_t now) noexcept {
    LaneHold h;
    const float fF = sc::dot3(f, g.F), v = m.v_sb;
    if (!(v >= 0.0f) || !(fF >= kLaneJoinHoldCos)) return h;
    std::int32_t t_ms = m.steered_ms ? std::int32_t(age_bound(g, kGapMs)) - std::int32_t(now - m.steered_ms) : 0;
    if (m.role == Role::escort && m.res_until && m.res_o8_ms && std::int32_t(now - m.res_o8_ms) >= 0) { // the leader's order 8 began: the wait
        const std::int32_t tl = std::int32_t(m.res_o8_ms + 900u + kLaneLambdaMaxMs + g.step_ms - now);
        if (tl > t_ms) t_ms = tl;
    }
    if (fF >= kLaneHoldCos) { // (the rollout coasts straight over the horizon, a re-issue gap at least)
        h.v = v;
        h.t_s = float(t_ms > 0 ? t_ms : std::int32_t(age_bound(g, kGapMs))) * 0.001f;
        return h;
    }
    if (t_ms <= 0) return h;
    const float t = float(t_ms) * 0.001f;
    const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
    const float s = sc::dot3(d, g.F);
    float lat[2];
    lane_lat_at(g, m, s, lat);
    const float e[2] = {sc::dot3(d, g.R) - lat[0], sc::dot3(d, g.U) - lat[1]}; // the offset from Lambda
    const float n[2] = {sc::dot3(f, g.R), sc::dot3(f, g.U)};                     // the nose's lateral part
    const float ee = e[0] * e[0] + e[1] * e[1], en = e[0] * n[0] + e[1] * n[1], nn = n[0] * n[0] + n[1] * n[1];
    float vh = v;
    if (nn > 1e-8f) {
        // |e + n vh t|^2 <= Y^2 (Y = max(|e|, kLaneDriftM)): vh <= the larger root of nn t^2 vh^2 + 2 en t vh + ee - Y^2 = 0
        const float Y2 = ee > kLaneDriftM * kLaneDriftM ? ee : kLaneDriftM * kLaneDriftM;
        const float A = nn * t * t, B = en * t, disc = B * B - A * (ee - Y2);
        const float v_far = disc >= 0.0f ? (-B + sc::sqrt_ss(disc)) / A : 0.0f;
        if (v_far < vh) vh = v_far;
        // level with its predecessor: the run's closest approach to Lambda stays outside the clearance band (the smaller root)
        if (!m.merged && m.lane_gap < kLaneGapM - 100.0f && m.lane_pred && en < 0.0f && ee > kLaneOnM * kLaneOnM) { // (beside it, off Lambda)
            const int pi = find(g, m.lane_pred);
            const float y_min = pi >= 0 ? g.m[pi].w + m.w + kLaneSideM : kLaneOnM;
            const float perp2 = ee - en * en / nn; // the closest approach of the whole line
            if (perp2 < y_min * y_min) {
                if (ee <= y_min * y_min) vh = 0.0f;
                else {
                    const float disc2 = B * B - A * (ee - y_min * y_min);
                    const float v_near = disc2 >= 0.0f ? (-B - sc::sqrt_ss(disc2)) / A : vh;
                    if (v_near < vh) vh = v_near;
                }
            }
        }
    }
    if (!(vh > 0.0f)) return h;
    h.v = vh;
    h.t_s = t;
    return h;
}
// The lane carrot (section 6.1; lane_steer, the rollout) in the plane of F and the lateral: s along F from G, y the lateral distance from
// Lambda at s (>= 0), hold_lat (a merging yielder still level with its predecessor: its lateral closes to y_clear = w_P + w_Y + kLaneSideM
// at most, the ship ahead's lane footprint never entered; the join onto Lambda once gap >= g_req - 100 m), redep (out along -F to the turn
// point on Lambda: the arrival came out on +U, Lambda lies on -U, so the turn is on the departure side). Returns the carrot (sc, yc).
__attribute__((always_inline)) inline void lane_carrot(float s, float y, float v, float W, float r_gate, bool hold_lat, bool redep, float* sc_out,
                                                       float* yc, float y_clear = 0.0f, float join_end = kLaneEndM + kLaneJoinEndM,
                                                       float out_m = kLaneRedepM, float out_r = 0.0f, float d_end = kLaneEndM) noexcept {
    const float look = pursuit_look(v, W, r_gate);
    if (redep && s > -(d_end + out_m)) { // out to the turn point (out_m: Member::redep_out), kLaneRedepSideM below Lambda (y_clear:
        // its lateral in the caller's plane); the carrot kLaneBlendM beyond it, an overshoot's (out_r: its turn radius) one radius beyond
        *sc_out = -(d_end + out_m) - (out_r > 0.0f ? out_r : kLaneBlendM);
        *yc = y_clear;
        return;
    }
    if (redep) { // past it with the nose still away: the turn on the departure side (y_clear: the caller's lateral of the -U offset)
        *sc_out = s + kLaneTurnM;
        *yc = y + y_clear;
        return;
    }
    // the join (lane fix round 1 item 1): the pursuit of the tangent parabola onto Lambda (lane_join_span), the carrot a look-ahead ahead
    // on it (on Lambda past its merge point); a residual past the join's end (an order-0 coast's drift) ends tangent at Lambda's d_end
    // point, so the nose is along F at P2
    const float R = W > 1e-4f ? (v > kYieldFloorMs ? v : kYieldFloorMs) / W : 0.0f;
    const float D = lane_join_span(s, y, R, join_end, d_end), s_m = s + D;
    float along = s + look, yr = 0.0f;
    if (along < s_m && D > 1.0f) {
        const float u = (s_m - along) / D;
        yr = y * u * u;
    }
    if (along > 0.0f) along = 0.0f; // the plane at Q (order 4 ends at d_end on its own)
    *sc_out = along;
    *yc = hold_lat ? (yr > (y > y_clear ? y_clear : y) ? yr : (y > y_clear ? y_clear : y)) : yr; // holding: never nearer than the band
}
// moves follow its kinematics (section 10.10): 0x21 the warp-in path to f+0x240, then 0x1e; 0x1e axis-parallel out at 120 / 240 m/s
// until |dz| > r (r / 4 for small hulls), then straight on; order 8 along +F for 0.9 s, then gone; order 0 braking along the nose at
// a; 0x1f in place; order 5 at its speed toward f+0x240 until the order's arrival radius (|v| / 10 + r), then gone.
inline unsigned rollout(const Member& m, const Gate& g, float v_cmd, Sample* out, float* vmax, std::uint32_t now = 0) noexcept {
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
        line_of(m, g, LX, Lh); // the grid line, or (in the lane's funnel) Lambda_4
        if (!line_plane(m, LX, Lh, &st, nn)) break;
        const float v_des = v_cmd >= 0.0f ? v_cmd : plan_speed(g, m, now);
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
    case Plan::lane: { // lane stage 1 (section 6.3, "the certificate's view"): the lane carrot (lane_carrot) in the plane of F and the
                       // ship's lateral from Lambda, at the lane laws on the rolled-out state, coupled to the predecessor's own rollout
                       // (front ship first: its samples at the same game time), the head's pacing and the escort gate from lane_frame's
                       // moments, the cornering cap of site B's write; past d_end the hand-over holds the speed (P2, the site-B hold; the
                       // refusal's brake) to the order-5 trigger, then order 8 for 0.9 s and the end point held. A candidate v_cmd >= 0
                       // caps the law (the merge floor); v_cmd == 0 is the brief stop for its rest (kLaneStopMs + the step from stop_ms)
        if (!(m.params.W > 1e-4f) || !(m.params.alpha > 1e-6f)) break;
        const float d0v[3] = {m.st.x[0] - g.G[0], m.st.x[1] - g.G[1], m.st.x[2] - g.G[2]};
        float lat0[2], LX[3], nn[3];
        lane_lat_at(g, m, sc::dot3(d0v, g.F), lat0);
        { // review item 5: a member near its Lambda with a blend still ahead takes the plane through the blend's end (its Q at d_end),
          // which then contains the whole blend l_c -> Q (lane_lat_at's points lie on that segment)
            float latE[2];
            lane_lat_at(g, m, -dend(m), latE);
            const float bR = latE[0] - lat0[0], bU = latE[1] - lat0[1], oR = sc::dot3(d0v, g.R) - lat0[0], oU = sc::dot3(d0v, g.U) - lat0[1];
            if (bR * bR + bU * bU > 1.0f && oR * oR + oU * oU < bR * bR + bU * bU) {
                lat0[0] = latE[0];
                lat0[1] = latE[1];
            }
        }
        for (unsigned i = 0; i < 3; ++i) LX[i] = g.G[i] + lat0[0] * g.R[i] + lat0[1] * g.U[i] - dend(m) * g.F[i];
        sc::JoinPlane st;
        if (!line_plane(m, LX, g.F, &st, nn)) break;
        const float k_lag = sc::one_minus_exp_neg(kStepS / sc::kSlipTau);
        float x = st.s, y = st.y, psi = st.psi, upsi = st.upsi, w = st.w, v = st.v;
        float ax[3] = {g.F[1] * nn[2] - g.F[2] * nn[1], g.F[2] * nn[0] - g.F[0] * nn[2], g.F[0] * nn[1] - g.F[1] * nn[0]};
        {
            const float an = sc::sqrt_ss(sc::dot3(ax, ax));
            for (float& q : ax) q = an > 1e-6f ? q / an : 0.0f;
        }
        const float psi0 = psi;
        // review item 5: Lambda's lateral moves from l_c to the member's Q over the blend (lane_lat_at, as lane_steer flies it live): in
        // the plane, Lambda lies at yL(s) = (lat(s) - lat0) . n (the out-of-plane part of the blend is not rolled out)
        const float nR = sc::dot3(nn, g.R), nU = sc::dot3(nn, g.U);
        auto y_lane = [&](float s_abs) {
            float l[2];
            lane_lat_at(g, m, s_abs, l);
            return (l[0] - lat0[0]) * nR + (l[1] - lat0[1]) * nU;
        };
        // the order-0 gap: desired 0 for its rest unless site B holds the lane speed (the nose within 8 deg of F)
        // (the lost-leader wait outside the re-issue gap: order 0 brakes until the script's order 4, beyond this horizon at worst)
        const bool o0 = m.st.order == sc::kOrderNone && !(m.role == Role::committed && m.handover_ms);
        const LaneHold hold = o0 ? lane_hold_o0(g, m, m.st.x, f0, m.st.ms) : LaneHold{};
        const bool o0_held = !o0 || hold.v >= 0.0f;
        const float t_brake = o0_held ? 0.0f
                              : m.st.order == sc::kOrderNone && m.role == Role::escort && !in_gap(m) ? float(kSamples) * kSampleS
                                                                                                    : gap_brake_s(m);
        const int pi = m.lane_pred ? find(g, m.lane_pred) : -1;
        // (signed: the predecessor's state may be a few ms newer than this member's; live() on m.st.ms would wrap)
        const Member* P = pi >= 0 && pair_role(g.m[pi].role) && g.m[pi].st.ms && g.m[pi].n > 1 &&
                                  std::int32_t(m.st.ms - g.m[pi].st.ms) <= std::int32_t(age_bound(g, kStateMaxAgeMs)) ? &g.m[pi] : nullptr;
        std::int32_t koff = 0;
        if (P) {
            const std::int32_t dms = std::int32_t(m.st.ms - P->roll_ms);
            koff = dms >= 0 ? (dms + std::int32_t(kSampleMs / 2)) / std::int32_t(kSampleMs) : -((-dms + std::int32_t(kSampleMs / 2)) / std::int32_t(kSampleMs));
        }
        auto p_along = [&](std::int32_t j) {
            const unsigned jj = j < 0 ? 0u : unsigned(j) < P->n ? unsigned(j) : P->n - 1u;
            const float d[3] = {P->s[jj].c[0] - g.G[0], P->s[jj].c[1] - g.G[1], P->s[jj].c[2] - g.G[2]};
            return sc::dot3(d, g.F);
        };
        LaneCtx cx;
        cx.v_lane = lane_vmax(m);
        cx.a = m.params.a_lin;
        cx.merged = m.merged != 0;
        cx.pred = P != nullptr; // (lane fix round 2 item 3d: cx.granted stays false: a granted predecessor's rollout shows its order-5 run, and
                                // at a SETA step's trigger order 8's lane rollout holds its point (v0 0): the follower's rollout stops there)
        cx.head = m.lane_free_ms != 0;
        cx.gate = m.lane_gate_ms != 0;
        cx.dt = float(g.step_ms > kStepClampMs ? kStepClampMs : g.step_ms) * 0.001f;
        // (lane fix round 2 item 3c: an overshoot that has not turned in flies its re-departure in the rollout, the out-leg to its turn point
        // and the U-turn, as lane_steer flies it, never the hand-over's straight run past d_end; its law runs from its U-turn's apex, where
        // it rejoins, as lane_frame's)
        bool redep = m.redep != 0 || (m.over && !m.over_in), past = false, merged = cx.merged;
        const float s_ov_ap = -(dend(m) + m.redep_out + kLaneOverApexR * m.redep_r);
        const bool escort = m.role == Role::escort;
        const float join_end = lane_join_end(m);
        const float stop_rest = v_cmd == 0.0f ? float(std::int32_t(m.stop_ms ? m.stop_ms + kLaneStopMs + g.step_ms - m.st.ms : kLaneStopMs + g.step_ms)) * 0.001f : 0.0f;
        // (lane fix round 1 item 4: the law's stop episode, lane_frame's lstop_ms: what is left of T_stop + the step at the state)
        const float stop_left0 = float(std::int32_t((m.lstop_ms ? m.lstop_ms : m.st.ms) + kLaneStopMs + g.step_ms - m.st.ms)) * 0.001f;
        const float omega = m.params.W < sc::kSlipCornerRate ? m.params.W : sc::kSlipCornerRate;
        const float top = lane_vmax(m);
        float dir[3] = {g.F[0], g.F[1], g.F[2]}, cpast[3] = {0, 0, 0}, ctrig[3] = {0, 0, 0}, t_launch = -1.0f;
        for (unsigned i = 0; i < 3; ++i) c[i] = LX[i] + x * g.F[i] + y * nn[i];
        // lane fix round 2 item 3d: a member already in its hand-over (past d_end after P2) rolls out the hand-over from its state (not one
        // plant step of its lane law first: a refused leader stopped at d_end crept 2.5 m/s in its rollout and its follower's floor ran into
        // it); the speed site B writes there (lane_site_b): P2 kept, the held speed (the refusal's brake bound); P2 not kept, the lane speed
        // under the refusal's brake bound, refused the brake to a stop at d_end (0 past it), reached at a_lin
        const bool hand = !escort && m.role == Role::committed && m.handover_ms && m.sb == Sb::handover;
        if (hand && x >= 0.0f) {
            past = true;
            float su, cu;
            sc::sincos_f(upsi, &su, &cu);
            for (unsigned i = 0; i < 3; ++i) {
                dir[i] = cu * g.F[i] + su * nn[i];
                cpast[i] = c[i];
            }
        }
        const float a_hand = m.params.a_lin > 0.1f ? m.params.a_lin : 0.1f;
        for (unsigned k = 0; k < kSamples; ++k) {
            const float t = float(k) * kSampleS;
            if (t_launch >= 0.0f) { // order 8, then gone: straight on along +F at its speed (as line5 under the lane)
                float p[3];
                for (unsigned i = 0; i < 3; ++i) p[i] = ctrig[i] + v * (t - t_launch) * g.F[i];
                put(out[n++], p, f0);
                continue;
            }
            for (unsigned step = 0; k && step < 2; ++step) {
                const float ts = float(k - 1u) * kSampleS + float(step) * kStepS; // the step's start (s from the state)
                if (past) { // the hand-over: the held speed along the last velocity (the refusal's brake), to the trigger
                    if (hand && !m.p2_kept) { // (item 3d: P2 not kept: site B's unkept write, approached at a_lin)
                        float vt;
                        if (m.refused) {
                            const float dd[3] = {cpast[0] - g.G[0], cpast[1] - g.G[1], cpast[2] - g.G[2]};
                            const float dist = -dend(m) - sc::dot3(dd, g.F);
                            vt = dist > kLaneStopShortM ? sc::sqrt_ss(2.0f * a_hand * (dist - kLaneStopShortM)) : 0.0f;
                        } else
                            vt = lane_refused_hold(g, m, cpast, m.v_sb >= 0.0f ? m.v_sb : lane_vmax(m));
                        const float dv = a_hand * kStepS;
                        v = v > vt + dv ? v - dv : v < vt - dv ? v + dv : vt;
                    } else if (m.refused)
                        v = lane_refused_hold(g, m, cpast, v);
                    for (unsigned i = 0; i < 3; ++i) cpast[i] += dir[i] * v * kStepS;
                    continue;
                }
                cx.s = x - dend(m);
                cx.d_end = dend(m);
                const float s_act = cx.s;
                const float yl = y_lane(cx.s), y_rel = y - yl;
                cx.y = y_rel > 0.0f ? y_rel : -y_rel;
                cx.loss = lane_join_loss(x - dend(m), cx.y, join_end, dend(m), lane_turn_r(m));
                {
                    float su_, cu_;
                    sc::sincos_f(upsi, &su_, &cu_);
                    cx.v_self = v * cu_;
                }
                if (redep && cx.s < -(dend(m) + m.redep_out)) {
                    float sp_, cp_;
                    sc::sincos_f(psi, &sp_, &cp_);
                    if (cp_ >= 0.0f) redep = false;
                }
                if (m.over && redep && cx.s > s_ov_ap) cx.s = s_ov_ap; // (item 3c: the law's s; the carrot below flies from s_act)
                if (P) {
                    const std::int32_t j = koff + std::int32_t(k) - 1;
                    const float sp = p_along(j), sp1 = p_along(j + 1);
                    if (P->ln && P->lane_rank != 0xff) { // the predecessor's own join still to come (from its sample's lateral)
                        const unsigned jj = j < 0 ? 0u : unsigned(j) < P->n ? unsigned(j) : P->n - 1u;
                        const float dp[3] = {P->s[jj].c[0] - g.G[0], P->s[jj].c[1] - g.G[1], P->s[jj].c[2] - g.G[2]};
                        float lp[2];
                        lane_lat_at(g, *P, sp, lp);
                        const float pr = sc::dot3(dp, g.R) - lp[0], pu = sc::dot3(dp, g.U) - lp[1];
                        cx.loss -= lane_join_loss(sp, sc::sqrt_ss(pr * pr + pu * pu), lane_join_end(*P), dend(*P), lane_turn_r(*P));
                    }
                    cx.v_pred = (sp1 - sp) / kSampleS;
                    cx.block = (cx.v_pred < kLaneStopV && cx.v_pred > -kLaneStopV) || (P->over && P->redep);
                    if (cx.v_pred < 0.0f) cx.v_pred = 0.0f;
                    // (lane fix round 1 item 4: an overshoot ahead turns back no farther out than its U-turn's apex: no nearer than that)
                    const float s_rj = -(dend(*P) + P->redep_out + kLaneOverApexR * P->redep_r), spp = P->over && sp > s_rj ? s_rj : sp;
                    cx.gap = (spp + (P->over && sp > s_rj ? 0.0f : (sp1 - sp) * float(step) * 0.5f) - P->L) - (cx.s + m.L);
                }
                cx.merged = merged;
                cx.t_free_s = cx.head ? float(std::int32_t(m.lane_free_ms - m.st.ms)) * 0.001f - ts : 0.0f;
                cx.t_gate_s = cx.gate ? float(std::int32_t(m.lane_gate_ms - m.st.ms)) * 0.001f - ts : 0.0f;
                cx.stop_left_s = stop_left0 - ts;
                Law law;
                float vd = lane_law(cx, &law, &merged);
                if (v_cmd > 0.0f && v_cmd < vd) vd = v_cmd;
                if (v_cmd == 0.0f && ts < stop_rest) vd = 0.0f;
                const bool hold_lat = !merged && cx.gap < kLaneGapM - 100.0f && !(cx.y <= kLaneOnM);
                float sc_c, yc;
                lane_carrot(s_act, cx.y, v, m.params.W, m.params.r_gate, hold_lat, redep, &sc_c, &yc,
                            redep ? (s_act > -(dend(m) + m.redep_out) ? -m.redep_side : kLaneTurnM) * sc::dot3(g.U, nn)
                                  : P ? P->w + m.w + kLaneSideM : 0.0f,
                            join_end, m.redep_out, m.redep_r, dend(m));
                float ta = sc_c + dend(m), tb = y_lane(sc_c) + (y_rel < 0.0f ? -yc : yc);
                const bool coast = o0 && ts < hold.t_s; // review item 5: the order-0 hold's straight coast (no leaf on order 0)
                if (coast) {
                    float sp_, cp_;
                    sc::sincos_f(psi, &sp_, &cp_);
                    ta = x + 10000.0f * cp_;
                    tb = y + 10000.0f * sp_;
                }
                const float d0 = sc::sqrt_ss((ta - x) * (ta - x) + (tb - y) * (tb - y));
                if (d0 > 1.0f) { // the cornering cap on the nose toward the carrot (core::steer's rule)
                    float sp, cp;
                    sc::sincos_f(psi, &sp, &cp);
                    const float cs = (cp * (ta - x) + sp * (tb - y)) / d0;
                    float vc = 3.4e38f;
                    if (cs < 0.0f) vc = omega * d0 * 0.5f;
                    else {
                        const float sn2 = 1.0f - cs * cs, sn = sn2 > 0.0f ? sc::sqrt_ss(sn2) : 0.0f;
                        if (sn > sc::kCornerSinMin) vc = omega * d0 / (2.0f * sn);
                    }
                    if (vc < sc::kFloorMs) vc = sc::kFloorMs;
                    if (vc < vd) vd = vc;
                }
                if (vd > top) vd = top;
                if (ts < hold.t_s && vd > hold.v) vd = hold.v; // the order-0 hold's straight-run cap (lane_hold_o0)
                if (ts < t_brake) vd = 0.0f;
                plant_step_to(m.params, vd, kStepS, k_lag, ta, tb, x, y, psi, upsi, w, v);
                if (!escort && !redep && x >= 0.0f) { // order 4 ends at d_end (L_i's 0.75 r_gate sphere): the hand-over from here
                    past = true;
                    float su, cu;
                    sc::sincos_f(upsi, &su, &cu);
                    for (unsigned i = 0; i < 3; ++i) {
                        dir[i] = cu * g.F[i] + su * nn[i];
                        cpast[i] = LX[i] + x * g.F[i] + y * nn[i];
                    }
                }
            }
            if (past) {
                for (unsigned i = 0; i < 3; ++i) c[i] = cpast[i];
                const float dd[3] = {c[0] - g.G[0], c[1] - g.G[1], c[2] - g.G[2]};
                if (sc::dot3(dd, dd) <= (v / 10.0f + m.r) * (v / 10.0f + m.r)) { // the order-5 trigger (tf 1: the latest), order 8 next
                    t_launch = t;
                    for (unsigned i = 0; i < 3; ++i) ctrig[i] = c[i];
                }
            } else
                for (unsigned i = 0; i < 3; ++i) c[i] = LX[i] + x * g.F[i] + y * nn[i];
            float sp, cp; // the live nose turned about F x n by the plant's in-plane turn (as the line plan)
            sc::sincos_f(psi - psi0, &sp, &cp);
            const float axf[3] = {ax[1] * f0[2] - ax[2] * f0[1], ax[2] * f0[0] - ax[0] * f0[2], ax[0] * f0[1] - ax[1] * f0[0]};
            const float af = sc::dot3(ax, f0);
            const float f[3] = {f0[0] * cp + axf[0] * sp + ax[0] * af * (1.0f - cp), f0[1] * cp + axf[1] * sp + ax[1] * af * (1.0f - cp),
                                f0[2] * cp + axf[2] * sp + ax[2] * af * (1.0f - cp)};
            put(out[n++], c, f);
            if (v > *vmax) *vmax = v;
        }
        break;
    }
    case Plan::line5: { // order 5 (the lane): the pursuit of Lambda_5 (through Q along F) at the order's aligned ramp (site B's write)
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
                // complete, review F13). Lane stage 1: straight on along +F at its speed (a ship behind on the same final: the launch only
                // moves it away faster, and order 8's 3 km/s would widen every sweep margin of the pair to a conflict)
                const float tl = g.lane ? t - t_launch : t - t_launch > 0.9f ? 0.9f : t - t_launch;
                const float tm = tl * 1000.0f, z = v0 * tl + (g.lane ? 0.0f : kWarpInK * tm * tm);
                float p[3];
                for (unsigned i = 0; i < 3; ++i) p[i] = c[i] + z * g.F[i];
                put(out[n++], p, f0);
                if (!g.lane && 2697.0f / 0.9f + v0 > *vmax) *vmax = 2697.0f / 0.9f + v0;
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
        // (signed, as the lane rollout's predecessor: the leader's state may be a few ms newer than this member's; live() on m.st.ms
        // would wrap and drop the leader)
        const Member* L = li >= 0 && g.m[li].ship && pair_role(g.m[li].role) && g.m[li].st.ms && g.m[li].n &&
                                  std::int32_t(m.st.ms - g.m[li].st.ms) <= std::int32_t(age_bound(g, kStateMaxAgeMs))
                              ? &g.m[li]
                              : nullptr;
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
        for (unsigned k = 0; k < kSamples; ++k) { // then gone: the rollout holds the end point (review F13, as line5); the lane: straight on
            const float t = g.lane ? float(k) * kSampleS : float(k) * kSampleS > 0.9f ? 0.9f : float(k) * kSampleS;
            const float tm = t * 1000.0f, z = v0 * t + (g.lane ? 0.0f : kWarpInK * tm * tm);
            for (unsigned i = 0; i < 3; ++i) c[i] = m.st.x[i] + z * g.F[i];
            put(out[n++], c, f0);
        }
        if (!g.lane && 2697.0f / 0.9f + v0 > *vmax) *vmax = 2697.0f / 0.9f + v0;
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
    // a rollout that ends because its ship leaves the sector (order 8's 0.9 s, then gone) is complete: one paired sample suffices
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
// B.9 (diagnostic, off the decision path): the sweep's minimum gap over the paired samples of two stored rollouts, every sample narrow:
// min over i of (the capsules' surface gap - the sweep's margin at i: kMarginM + the residual offset x B's largest speed + the drift
// k (t + |t0_A - t0_B|) + |du|_1 dt / 2), its sample, and the first sample below 0 (255 none). ok = false when the rollouts are too far
// apart in time (max_dms) or share fewer paired samples than the sweep needs (min_samples, never below kMinOverlapSamples; one with ends:
// overlap_min; B.9 review: gap_m is na where the verdict is stale). The pair row (on a verdict change) and the fixture call it (pair_scan);
// ~61 capsule tests.
struct GapScan {
    bool ok = false;
    unsigned i_min = 255, t_c = 255;
    float min_gap = 1e30f;
};
inline GapScan gap_scan(const Sample* sa, unsigned na, std::uint32_t a_ms, float La, float wa, const Sample* sb, unsigned nb, std::uint32_t b_ms,
                        float b_vmax, float Lb, float wb, std::uint32_t max_dms, unsigned min_samples, bool ends, float drift = kDriftMs) noexcept {
    GapScan out;
    const std::int32_t dms = std::int32_t(a_ms - b_ms), lim = std::int32_t(max_dms);
    if (dms > lim || dms < -lim) return out;
    const std::int32_t koff = dms >= 0 ? (dms + std::int32_t(kSampleMs / 2)) / std::int32_t(kSampleMs)
                                       : -((-dms + std::int32_t(kSampleMs / 2)) / std::int32_t(kSampleMs));
    const float resid = sc::fabs_f(float(dms - koff * std::int32_t(kSampleMs))) * 0.001f;
    const std::int32_t i0 = koff < 0 ? -koff : 0;
    std::int32_t i1 = std::int32_t(na);
    if (std::int32_t(nb) - koff < i1) i1 = std::int32_t(nb) - koff;
    if (i1 - i0 < (ends ? 1 : std::int32_t(min_samples < kMinOverlapSamples ? kMinOverlapSamples : min_samples))) return out;
    out.ok = true;
    const Sample* B = sb + koff;
    float base = kMarginM + resid * b_vmax + drift * (kSampleS * float(i0) + float(dms >= 0 ? dms : -dms) * 0.001f);
    for (std::int32_t i = i0; i < i1; ++i, base += drift * kSampleS) {
        const std::int32_t nx = i + 1 < i1 ? i + 1 : i > i0 ? i - 1 : i;
        const float du1 = sc::fabs_f((sa[nx].c[0] - sa[i].c[0]) - (B[nx].c[0] - B[i].c[0])) + sc::fabs_f((sa[nx].c[1] - sa[i].c[1]) - (B[nx].c[1] - B[i].c[1])) +
                          sc::fabs_f((sa[nx].c[2] - sa[i].c[2]) - (B[nx].c[2] - B[i].c[2]));
        const float gap = capsule_gap(sa[i].c, sa[i].f, La, wa, B[i].c, B[i].f, Lb, wb) - base - 0.5f * du1;
        if (gap < out.min_gap) {
            out.min_gap = gap;
            out.i_min = unsigned(i);
        }
        if (gap < 0.0f && out.t_c == 255) out.t_c = unsigned(i);
    }
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
    unsigned replans = 0; // B.9: decisions whose member's plan changed since its previous rollout (plan_changed)
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
// The paired samples a sweep of A's rollout (na samples) against B's needs (sweep_all; pair_scan uses the same): a rollout that ends because
// order 8 is live on the leaving ship is complete with one paired sample (*ends); one that ends at the order-5 trigger (line5) keeps the
// 4-sample minimum but not the warn reach (review F13); else the warn reach.
inline unsigned overlap_min(const Member& A, unsigned na, const Member& B, bool* ends) noexcept {
    const bool launching = (B.n < kSamples && plan_of(B) == Plan::launch) || (na < kSamples && plan_of(A) == Plan::launch);
    const bool triggering = (B.n < kSamples && plan_of(B) == Plan::line5) || (na < kSamples && plan_of(A) == Plan::line5);
    *ends = launching;
    return triggering && !launching ? kMinOverlapSamples : warn_reach_samples(A, B);
}
// Every pair of member i with the other live members, swept against the rollout s (n samples, t0 = i's state). write: the matrix row
// of i takes the verdicts (clear, or none for a conflict) and *conflicts the bit of each conflicting partner; !write (a yield
// candidate): stops at the first conflict. Returns whether every pair cleared.
// Lane stage 1 (skip_behind): a lane yielder's candidate skips the members behind it in the lane order: their rollouts follow its old
// plan (front ship first) and are re-made on its new one at their next decision (decide_order puts the lane order first); their live
// follow law reacts in the same frame (lane_frame). Their pair verdicts keep their stamps (they age out under kCertMaxAgeMs).
inline bool sweep_all(Gate& g, unsigned i, const Sample* s, unsigned n, float vmax, std::uint32_t now, bool write, std::uint32_t* conflicts,
                      DecideStats* st, PairWhy clear_why = PairWhy::cert, bool skip_behind = false) noexcept {
    const Member& A = g.m[i];
    bool all = true;
    for (unsigned j = 0; j < g.n; ++j) {
        if (j == i || !live(g, g.m[j], now)) continue;
        const Member& B = g.m[j];
        if (skip_behind && B.ln && B.lane_rank != 0xff && A.lane_rank != 0xff && B.lane_rank > A.lane_rank) continue;
        ++st->sweeps;
        // fail closed: no samples on either side, a rollout more than 1 s from the other, too short an overlap -> no verdict (none:
        // the engine's warn and the sphere avoidance), not a conflict to resolve
        // the overlap's minimum (overlap_min: one sample when a rollout ends at a launch, review F13)
        bool launching;
        const unsigned min_s = overlap_min(A, n, B, &launching);
        // (a member in the order-0 gap brakes in its rollout for the gap's expected rest: gap_brake_s; the drift k stays kDriftMs)
        const float drift = kDriftMs;
        // B.3: the rollouts' time offset bound is SETA-aware; the nominal bound keeps the constant sweep (sweep_impl)
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
            const PairWhy why = w.clear ? clear_why : !(B.n && n) ? PairWhy::zero : w.stale ? PairWhy::stale : PairWhy::contact; // B.9
            g.why[i][j] = g.why[j][i] = std::uint8_t(std::uint8_t(why) | g.why_plan);
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
// B.9 review (item 2): a member in the script's order-0 re-issue gap on a steered plan (in_gap) yields too, deferred: nothing is written
// on order 0 (site B applies a cap only on the steered order, can_yield), the cap holds the gap's resume, which site B writes once the
// script re-issues the order; the candidate's rollout brakes for the gap's rest, then flies the candidate (and decide keeps a cap the
// gap's rollout cleared on in force through the gap). A committed member needs its validated line (can_yield's).
__attribute__((always_inline)) inline bool gap_yield(const Member& m) noexcept {
    return in_gap(m) && (m.role == Role::escort || (m.role == Role::committed && m.line_ok));
}
// Y's yield by speed: Y's current plan at 0.6 v, then 25 m/s, then the 10 m/s floor (each below the plan's speed); a new rollout
// and sweeps against every member each; the first that clears every pair is taken (Y's rollout, Y's cap, Y's row of the matrix; the
// row's entries carry plan_flag, Y's own plan change). *swept (B.9 review): whether a candidate was rolled out at all (false: Y cannot
// yield, no steered order or an empty ladder).
inline bool try_yield(Gate& g, unsigned y, std::uint32_t now, DecideStats* st, std::uint8_t plan_flag, bool* swept) noexcept {
    Member& Y = g.m[y];
    *swept = false;
    if (!can_yield(Y) && !gap_yield(Y)) return false;
    const float vp = plan_speed(g, Y, now), v = vp > 0.0f ? vp : Y.st.v;
    // lane stage 1 (section 6.3): the yielder's ladder is the merge floor (10 m/s), then the brief stop (0) while its stop lasts at most
    // kLaneStopMs + the game step from its start; a member past that hands the pair back (cause=contact)
    const bool lane = plan_of(Y) == Plan::lane;
    const bool stop_ok = lane && (!Y.stop_ms || std::int32_t(now - Y.stop_ms) <= std::int32_t(kLaneStopMs + g.step_ms));
    const float cand[3] = {lane ? kYieldFloorMs : kYieldFrac * v, lane ? 0.0f : kYieldMidMs, lane ? -1.0f : kYieldFloorMs};
    const float floor = lane ? 0.0f : kYieldFloorMs;
    Sample tmp[kSamples];
    for (float c : cand) {
        if (!(c >= floor) || !(c < v || (lane && c == 0.0f))) continue;
        if (lane && c == 0.0f && !stop_ok) continue;
        ++st->candidates;
        ++st->rollouts;
        *swept = true;
        float vmax = 0.0f;
        const unsigned n = rollout(Y, g, c, tmp, &vmax, now);
        if (!n || !sweep_all(g, y, tmp, n, vmax, now, false, nullptr, st, PairWhy::cert, lane)) continue;
        for (unsigned k = 0; k < n; ++k) Y.s[k] = tmp[k];
        Y.n = std::uint8_t(n);
        Y.roll_ms = Y.st.ms;
        Y.v_roll_max = vmax;
        Y.cap = c;
        Y.cap_ms = Y.yield_ms = now ? now : 1;
        if (lane && c == 0.0f && !Y.stop_ms) {
            Y.stop_ms = now ? now : 1;
            ++g.lane_stops;
        }
        const std::uint8_t decider_flag = g.why_plan;
        g.why_plan = plan_flag;
        sweep_all(g, y, Y.s, Y.n, Y.v_roll_max, now, true, nullptr, st, !lane ? PairWhy::yield : c > 0.0f ? PairWhy::merge : PairWhy::stop, lane);
        g.why_plan = decider_flag;
        ++st->yields;
        return true;
    }
    return false;
}
// B.9 (diagnostic): whether a member's plan changed since its own rollout: another plan or stage, or a plan speed more than kPlanDeltaMs
// from the rollout's (a law step, the order-0 gap's resume). The
// verdicts its next decision writes carry kPairWhyPlan (the pair row's trig=plan). It schedules no decision (gate-traffic-controller.md
// B.9: the plan speed's fallbacks flip it every ~100 ms). The inbound law's own decrease (a_lin x 250 ms <= 2.5 m/s) stays under it.
constexpr float kPlanDeltaMs = 5.0f;
inline bool plan_changed(const Gate& g, const Member& m, std::uint32_t now = 0) noexcept {
    if (!m.n || !m.decided_ms) return false;
    if (plan_of(m) != m.roll_plan || m.sb != m.roll_sb) return true;
    return sc::fabs_f(plan_speed(g, m, now) - m.roll_v) > kPlanDeltaMs;
}
// Member i's decision (D.1): its rollout on its plan's speed (a yield cap lapses when that clears every pair), the sweeps against every
// live member, the yield of the lower-priority ship of each conflict pair (at most kMaxYieldsPerDecision resolutions; i itself at most
// once), and the hand-back of what stays in conflict (unresolved: site E passes, the sphere avoidance applies). Verdict changes of
// i's row go to ev (at most kMaxEvents).
inline void decide(Gate& g, unsigned i, std::uint32_t now, DecideStats* st, PairEvent* ev, unsigned* n_ev) noexcept {
    Member& A = g.m[i];
    const bool replan = plan_changed(g, A, now); // B.9: before this decision's stamps
    A.decided_ms = now ? now : 1;
    note_state(A); // Run161: the order-0 gap's steered stamp and plan
    std::uint8_t before[kMembers];
    for (unsigned j = 0; j < kMembers; ++j) before[j] = g.v[i][j];
    if (!live(g, A, now)) {
        clear_pairs(g, i);
        A.n = 0;
        return;
    }
    if (replan) ++st->replans;
    g.why_plan = replan ? kPairWhyPlan : 0;
    ++st->rollouts;
    float vmax = 0.0f;
    A.n = std::uint8_t(rollout(A, g, -1.0f, A.s, &vmax, now));
    A.roll_ms = A.st.ms;
    A.v_roll_max = vmax;
    A.roll_v = plan_speed(g, A, now); // B.9: the plan the rollout flew (plan_changed)
    A.roll_plan = plan_of(A);
    A.roll_sb = A.sb;
    std::uint32_t conflicts = 0;
    sweep_all(g, i, A.s, A.n, vmax, now, true, &conflicts, st); // A.n == 0 (a refused plane, a NaN state): every pair none
    if (!conflicts && A.cap >= 0.0f) {
        // B.9 review (item 3): the gap's rollout resumed at the cap and cleared: kept in force for site B's first write after the gap (no
        // release on order 0). Lane stage 1: the lane rollout flies its law, not the cap (v_cmd < 0), so a clear one releases it
        if (in_gap(A) && cap_in_force(g, A, now) >= 0.0f && A.roll_plan != Plan::lane)
            A.cap_ms = now ? now : 1;
        else {
            A.cap = -1.0f;
            A.stop_ms = 0; // lane stage 1: the brief stop is over
            ++st->released;
        }
    }
    unsigned tries = 0;
    bool self_tried = false;
    for (unsigned j = 0; j < g.n && conflicts; ++j) {
        if (!((conflicts >> j) & 1u)) continue;
        ++st->conflicts;
        if (g.v[i][j] == std::uint8_t(PairV::clear)) continue; // a yield earlier in this decision already cleared it
        const bool a_yields = yields_to(A, g.m[j]);
        bool ok = false, swept = false;
        const bool tried = tries < kMaxYieldsPerDecision && (!a_yields || !self_tried);
        if (tried) {
            ++tries;
            if (a_yields) self_tried = true;
            // B.9 review (item 4): a partner's yield writes its row with its own plan flag; the shared pair keeps the decider's too
            const std::uint8_t flag = a_yields ? g.why_plan : plan_changed(g, g.m[j], now) ? kPairWhyPlan : std::uint8_t(0);
            ok = try_yield(g, a_yields ? i : j, now, st, flag, &swept);
            if (ok && !a_yields) {
                g.why[i][j] |= g.why_plan;
                g.why[j][i] |= g.why_plan;
            }
        }
        if (!ok || g.v[i][j] != std::uint8_t(PairV::clear)) {
            g.v[i][j] = g.v[j][i] = std::uint8_t(PairV::unresolved);
            g.vms[i][j] = g.vms[j][i] = now ? now : 1;
            const PairWhy why = !tried ? PairWhy::budget : swept ? PairWhy::contact : PairWhy::noyield; // B.9 (review item 4: noyield)
            g.why[i][j] = g.why[j][i] = std::uint8_t(std::uint8_t(why) | g.why_plan);
            ++st->unresolved;
        }
    }
    g.why_plan = 0;
    if (!ev || !n_ev) return;
    for (unsigned j = 0; j < g.n; ++j) {
        if (j == i || g.v[i][j] == before[j] || *n_ev >= kMaxEvents) continue;
        PairEvent& e = ev[(*n_ev)++];
        e.a = A.ship;
        e.b = g.m[j].ship;
        e.verdict = g.v[i][j];
        e.t_c = g.tc[i][j];
        e.yielder = A.cap >= 0.0f && A.yield_ms == now ? 1 : g.m[j].cap >= 0.0f && g.m[j].yield_ms == now ? 2 : 0;
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
    std::uint8_t listed[total] = {}; // review item 8: the lane phase's entries (a flag per slot, not a scan of the list per member)
    for (unsigned gi = 0; gi < kGates; ++gi) { // lane stage 1: the lane order, front ship first (a follower's rollout reads its
        const Gate& g = b.g[gi];                // predecessor's of the same frame)
        if (!g.gate || !g.lane) continue;
        // the committed members past d_end first (the hand-over, order 5, order 8: the head's predecessor, lane_frame's gp), front first
        const unsigned n0 = n;
        for (unsigned i = 0; i < g.n; ++i) {
            const Member& o = g.m[i];
            if (!(o.ship && o.role == Role::committed && (o.sb == Sb::handover || o.sb == Sb::dock || o.sb == Sb::launch)) || !due(g, o, now)) continue;
            unsigned j = n++;
            for (; j > n0 && g.m[idx[j - 1] % kMembers].lane_s < o.lane_s; --j) idx[j] = idx[j - 1];
            idx[j] = std::uint16_t(gi * kMembers + i);
            listed[gi * kMembers + i] = 1;
        }
        for (unsigned q = 0; q < g.lane_n; ++q) {
            const int i = find(g, g.lane_ord[q]);
            if (i >= 0 && due(g, g.m[i], now) && !listed[gi * kMembers + unsigned(i)]) {
                idx[n++] = std::uint16_t(gi * kMembers + unsigned(i));
                listed[gi * kMembers + unsigned(i)] = 1;
            }
        }
    }
    for (unsigned k = 0; k < total; ++k) {
        const Gate& g = b.g[k / kMembers];
        const unsigned i = k % kMembers;
        if (g.gate && i < g.n && sb_priority(g.m[i]) && due(g, g.m[i], now) && !(g.lane && listed[k]))
            idx[n++] = std::uint16_t(k);
    }
    *n_prio = n;
    for (unsigned q = 0; q < total; ++q) {
        const unsigned k = (cursor + q) % total;
        const Gate& g = b.g[k / kMembers];
        const unsigned i = k % kMembers;
        if (g.gate && i < g.n && !sb_priority(g.m[i]) && due(g, g.m[i], now) && !(g.lane && g.m[i].lane_rank != 0xff && listed[k]))
            idx[n++] = std::uint16_t(k);
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
// B.9: gap_scan over the two members' stored rollouts with the sweep's own bounds (overlap_min, the SETA-aware time offset); pair_gap is
// the pair row's gap_m (m; 1e30 when either has none, they are too far apart or overlap too little: a stale verdict).
inline GapScan pair_scan(const Gate& g, unsigned i, unsigned j) noexcept {
    const Member& a = g.m[i];
    const Member& b = g.m[j];
    if (!a.n || !b.n) return GapScan{};
    bool ends;
    const unsigned min_s = overlap_min(a, a.n, b, &ends);
    return gap_scan(a.s, a.n, a.roll_ms, a.L, a.w, b.s, b.n, b.roll_ms, b.v_roll_max, b.L, b.w, age_bound(g, kRolloutMaxAgeMs), min_s, ends);
}
inline float pair_gap(const Gate& g, unsigned i, unsigned j) noexcept {
    const GapScan sc = pair_scan(g, i, j);
    return sc.ok ? sc.min_gap : 1e30f;
}
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
// ---- Lane fix round 2 item 1: a lane member against a non-member obstacle (site E's obstacle row) --------------------------------
// Run173 and the round-1 review: the engine's own warn (ship-navigation-ai.md section 3.1: the close-range rule d <= 1.2 (r_A + r_B) for
// a station or a B at least 0.75 r_A, the box test for stations and capital-type ships, the cone for the rest) fires on a lane member
// beside a station whose bounding sphere is far wider than its parts, on asteroids the join passes, on a mid-size ship (an M7 beside an
// M2) whose crossing our avoidance has already resolved, and on the order-0 frames of the script's gaps where A has no fresh decision.
// The lane row suppresses that warn when the member's path as site A will fly it (lane_path: the live position and velocity, the
// avoidance's fresh detour when there is one, the board's rollout) clears the obstacle's bound by the sweep margin (kMarginM + the
// drift k (t + the rollout's age); a moving ship's extrapolation charged k t again) over the rollout's horizon, which reaches at least
// the ship's stop (1 s + v / a). Fail closed: no lane member, the player's ship or manual control, an
// order the lane does not fly (0x12, 5, 8, ...), a stale or future state or rollout, an unreadable bound: no (the existing rows decide);
// a path that does not clear: roll (the existing rows decide: the engine's warn as before).
enum class ObstRow : std::uint8_t { no = 0, lane, roll, count };
inline const char* obst_row_name(ObstRow w) noexcept {
    static const char* const n[unsigned(ObstRow::count)] = {"no", "lane", "roll"};
    return unsigned(w) < unsigned(ObstRow::count) ? n[unsigned(w)] : "?";
}
// The obstacle's bound: its centre and velocity at the read (m, m/s), the bounding sphere r; a ship's hull capsule (w > 0: half-width
// w, half-length L >= w along f); a station's part spheres (world, m; np 0: the sphere r).
struct ObstBound {
    float c[3] = {0, 0, 0}, v[3] = {0, 0, 0};
    float r = 0;
    float w = 0, L = 0, f[3] = {0, 0, 1};
    const float (*pc)[3] = nullptr;
    const float* pr = nullptr;
    unsigned np = 0;
};
// A ship obstacle's hull capsule from its type widths (w: the lateral half-width, l: the census half-length) about its nose f, against
// its live bounding radius r: w within (0, r] and f a unit row, else false (the bound stays the sphere r). Round 2 review item 3: L = l
// when it is longer than w (a census half-length beyond r kept, as hull_from keeps it for a member), else r; never w, which made a
// stopped ship whose census half-length exceeds its radius a sphere of radius w < r.
inline bool obst_hull(ObstBound& o, float w, float l) noexcept {
    if (!(w > 0.0f && w <= o.r && sc::finite_f(w)) || !unit_row(o.f)) return false;
    o.w = w;
    o.L = sc::finite_f(l) && l > w ? l : o.r;
    return true;
}
// A lane member's path from now (seconds t from now at each vertex): the live position, the live velocity up to the rollout's first
// sample at or after now, the rollout's samples (their times); with a fresh avoidance detour P_d the turn site A's leaf flies onto it
// instead (lane_turn: the live velocity's heading turned toward P_d every half second at the hull's rate W, reached at its angular
// acceleration alpha and braked on sqrt(2 alpha angle), after one frame step and a think tick straight), P_d, then the rollout from
// its first sample P_d's lateral offset / tan 30 deg beyond P_d along F (the join's steepest angle; the times shifted by the detour's
// extra length). age_s: the rollout's age at now (the drift
// charges from its t0).
constexpr unsigned kPathMax = 2 * kSamples + 4;
struct LanePath {
    float p[kPathMax][3];
    float t[kPathMax];
    unsigned n = 0;
    float age_s = 0, v_end = 0;
};
// The rollout's freshness, two-sided (lane fix round 2, review 7: a stamp from before a backward clock jump is stale)
__attribute__((always_inline)) inline bool roll_fresh(const Gate& g, const Member& m, std::uint32_t now) noexcept {
    const std::int32_t age = std::int32_t(now - m.roll_ms);
    return m.n && m.roll_ms && age >= 0 && age <= std::int32_t(kStateMaxAgeMs + g.step_ms + kStateMs);
}
// The turn onto the detour point P_d as site A's leaf flies it (lane_path): straight for one frame step and a think tick, then every
// kSampleS the heading turns toward P_d by w dt, w ramping at alpha to W and braked to stop on the target heading (sqrt(2 alpha
// angle)); the speed stays; until P_d is within a step, its closest approach (a turn wider than P_d's offset: the leaf's next detour
// point lies beyond it) or kSamples steps. Writes P.p / P.t from vertex 1 on (vertex 0 is x); returns the
// vertex count (0: no turn rate, fail closed).
inline unsigned lane_turn(const Gate& g, const Member& m, const float x[3], const float u[3], float v, const float* pd, LanePath& P) noexcept {
    const float W = m.params.W, al = m.params.alpha > 1e-4f ? m.params.alpha : W;
    if (!(W > 1e-4f)) return 0;
    float h[3] = {g.F[0], g.F[1], g.F[2]};
    if (v > 0.5f)
        for (unsigned i = 0; i < 3; ++i) h[i] = u[i] / v;
    const float vs = v > 1.0f ? v : 1.0f;
    float pos[3], t = float(g.step_ms) * 0.001f + 0.1f, w = 0.0f;
    for (unsigned i = 0; i < 3; ++i) pos[i] = x[i] + h[i] * v * t;
    unsigned n = 1;
    for (unsigned i = 0; i < 3; ++i) P.p[n][i] = pos[i];
    P.t[n++] = t;
    float dl_prev = 3.4e38f;
    for (unsigned step = 0; step < kSamples && n + 1 < kPathMax; ++step) {
        const float d[3] = {pd[0] - pos[0], pd[1] - pos[1], pd[2] - pos[2]};
        const float dl = sc::sqrt_ss(sc::dot3(d, d));
        if (!sc::finite_f(dl)) return 0;
        if (dl <= vs * kSampleS || dl > dl_prev) break; // (at P_d, or past its closest approach: the leaf's next point lies beyond it)
        dl_prev = dl;
        const float inv = 1.0f / dl, dh[3] = {d[0] * inv, d[1] * inv, d[2] * inv};
        const float c = sc::dot3(h, dh);
        const float ang = sc::acos_deg(c) * (sc::kPi / 180.0f);
        if (ang < 0.002f) break; // aligned: straight on to P_d
        const float wb = sc::sqrt_ss(2.0f * al * ang);
        w += al * kSampleS;
        if (w > W) w = W;
        if (w > wb) w = wb;
        float dA = w * kSampleS;
        if (dA > ang) dA = ang;
        float e[3] = {dh[0] - c * h[0], dh[1] - c * h[1], dh[2] - c * h[2]};
        const float el = sc::sqrt_ss(sc::dot3(e, e));
        if (el > 1e-6f) {
            const float a2 = dA * dA, cs = 1.0f - a2 * (0.5f - a2 * (1.0f / 24.0f)), sn = dA * (1.0f - a2 * (1.0f / 6.0f));
            for (unsigned i = 0; i < 3; ++i) h[i] = h[i] * cs + e[i] / el * sn;
            const float hl = sc::sqrt_ss(sc::dot3(h, h));
            for (float& q : h) q /= hl;
        }
        for (unsigned i = 0; i < 3; ++i) pos[i] += h[i] * v * kSampleS;
        t += kSampleS;
        for (unsigned i = 0; i < 3; ++i) P.p[n][i] = pos[i];
        P.t[n++] = t;
    }
    const float d[3] = {pd[0] - pos[0], pd[1] - pos[1], pd[2] - pos[2]};
    for (unsigned i = 0; i < 3; ++i) P.p[n][i] = pd[i];
    P.t[n] = t + sc::sqrt_ss(sc::dot3(d, d)) / vs;
    return n + 1;
}
inline bool lane_path(const Gate& g, const Member& m, const float x[3], const float u[3], const float* detour, std::uint32_t now, LanePath& P) noexcept {
    P.n = 0;
    if (!roll_fresh(g, m, now)) return false;
    const std::int32_t age = std::int32_t(now - m.roll_ms);
    const unsigned k0 = unsigned((age + std::int32_t(kSampleMs) - 1) / std::int32_t(kSampleMs));
    if (k0 >= m.n) return false;
    P.age_s = float(age) * 0.001f;
    const float v = sc::sqrt_ss(sc::dot3(u, u));
    if (!sc::finite_f(v)) return false;
    const float vt = v > 1.0f ? v : 1.0f; // (the times of the path's own legs)
    for (unsigned i = 0; i < 3; ++i) P.p[0][i] = x[i];
    P.t[0] = 0.0f;
    unsigned n = 1, k = k0;
    float shift = 0.0f;
    if (detour) {
        n = lane_turn(g, m, x, u, v, detour, P);
        if (!n) return false;
        // the rejoin: back onto the rollout at the join's steepest angle (30 deg, lane_join_span: its first sample the lateral offset of
        // P_d / tan 30 deg beyond P_d along F; the leg to it is tested like the rest)
        const float gF = sc::dot3(g.G, g.F), sd = sc::dot3(detour, g.F) - gF;
        while (k < m.n && sc::dot3(m.s[k].c, g.F) - gF < sd) ++k;
        if (k < m.n) {
            const float dq[3] = {m.s[k].c[0] - detour[0], m.s[k].c[1] - detour[1], m.s[k].c[2] - detour[2]};
            const float sj = sd + sc::sqrt_ss(sc::dot3(dq, dq)) * (1.0f / kLaneJoinTan);
            while (k < m.n && sc::dot3(m.s[k].c, g.F) - gF < sj) ++k;
        }
        if (k < m.n) {
            const float dj[3] = {m.s[k].c[0] - detour[0], m.s[k].c[1] - detour[1], m.s[k].c[2] - detour[2]};
            const float tk = float(std::int32_t(m.roll_ms + k * kSampleMs - now)) * 0.001f;
            const float tj = P.t[n - 1] + sc::sqrt_ss(sc::dot3(dj, dj)) / vt;
            shift = tj > tk ? tj - tk : 0.0f;
        }
    } else {
        const float tk = float(std::int32_t(m.roll_ms + k0 * kSampleMs - now)) * 0.001f;
        if (tk > 0.01f) {
            for (unsigned i = 0; i < 3; ++i) P.p[1][i] = x[i] + u[i] * tk;
            P.t[1] = tk;
            n = 2;
        }
    }
    for (; k < m.n && n < kPathMax; ++k, ++n) {
        for (unsigned i = 0; i < 3; ++i) P.p[n][i] = m.s[k].c[i];
        P.t[n] = float(std::int32_t(m.roll_ms + k * kSampleMs - now)) * 0.001f + shift;
    }
    P.n = n;
    if (n >= 2 && P.t[n - 1] > P.t[n - 2]) {
        const float e[3] = {P.p[n - 1][0] - P.p[n - 2][0], P.p[n - 1][1] - P.p[n - 2][1], P.p[n - 1][2] - P.p[n - 2][2]};
        P.v_end = sc::sqrt_ss(sc::dot3(e, e)) / (P.t[n - 1] - P.t[n - 2]);
    } else
        P.v_end = v;
    return true;
}
// Round 2 review item 7: whether lane_path would build a path, without building it (the own-gate row on a gate with no solid needs only
// that): its fail-closed tests (the rollout two-sided fresh, a sample at or after now, a finite speed; with a detour lane_turn's: a turn
// rate, and every position of its turn finite: they stay within v (kSamples + 2) kSampleS of x, so a finite bound on the distance to
// P_d from there and on that reach is a finite distance at every step).
inline bool lane_path_exists(const Gate& g, const Member& m, const float x[3], const float u[3], const float* detour, std::uint32_t now) noexcept {
    if (!roll_fresh(g, m, now)) return false;
    const std::int32_t age = std::int32_t(now - m.roll_ms);
    if (unsigned((age + std::int32_t(kSampleMs) - 1) / std::int32_t(kSampleMs)) >= m.n) return false;
    const float v = sc::sqrt_ss(sc::dot3(u, u));
    if (!sc::finite_f(v)) return false;
    if (!detour) return true;
    if (!(m.params.W > 1e-4f)) return false;
    const float d[3] = {detour[0] - x[0], detour[1] - x[1], detour[2] - x[2]};
    const float reach = v * (float(kSamples + 2u) * kSampleS + float(g.step_ms) * 0.001f + 0.1f);
    return sc::finite_f(sc::sqrt_ss(sc::dot3(d, d)) + reach);
}
// The gap (m) from A's hull swept along the path segment i (vertices i, i + 1; times ta..tb from now) to the obstacle's bound, against
// the margin at tb: >= 0 clear. A's hull: its capsule along the leg (the leg extended by L - w at both ends, radius w; a single vertex:
// the sphere L); ext adds to its radius (the horizon's stopping distance). A moving obstacle sweeps its centre over [ta - dl, tb + dl]
// (dl: 0.5 s + 10 % of tb, the path's timing slack) as a sphere of its half-length. (Always inlined: an out-of-line float return would
// travel through st(0) on i386, the x87 audit.)
__attribute__((always_inline)) inline float obst_gap(const LanePath& P, unsigned i, const ObstBound& o, float LA, float wA, float ext) noexcept {
    const bool single = i + 1 >= P.n;
    const float* p0 = P.p[i];
    const float* p1 = P.p[single ? i : i + 1];
    const float tb = P.t[single ? i : i + 1];
    float a[3], b[3], rad = LA + ext;
    {
        const float d[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
        const float dl = sc::sqrt_ss(sc::dot3(d, d));
        const float h = LA > wA ? LA - wA : 0.0f;
        if (!single && dl > 1.0f) {
            for (unsigned k = 0; k < 3; ++k) {
                a[k] = p0[k] - d[k] / dl * h;
                b[k] = p1[k] + d[k] / dl * h;
            }
            rad = wA + ext;
        } else
            for (unsigned k = 0; k < 3; ++k) a[k] = b[k] = p0[k];
    }
    float margin = kMarginM + kDriftMs * (tb + P.age_s);
    if (sc::dot3(o.v, o.v) > 0.25f) { // a moving ship: its centre's sweep, its extent the half-length
        margin += kDriftMs * tb;
        const float ta = P.t[i], dl = 0.5f + 0.1f * tb;
        float b0[3], b1[3];
        for (unsigned k = 0; k < 3; ++k) {
            b0[k] = o.c[k] + o.v[k] * (ta - dl);
            b1[k] = o.c[k] + o.v[k] * (tb + dl);
        }
        const float ext_b = o.w > 0.0f && o.L > o.w ? o.L : o.r;
        return sc::sqrt_ss(seg_seg_d2(a, b, b0, b1)) - rad - ext_b - margin;
    }
    if (o.w > 0.0f) { // a stopped ship: its hull capsule
        const float h = o.L > o.w ? o.L - o.w : 0.0f;
        const float b0[3] = {o.c[0] - h * o.f[0], o.c[1] - h * o.f[1], o.c[2] - h * o.f[2]};
        const float b1[3] = {o.c[0] + h * o.f[0], o.c[1] + h * o.f[1], o.c[2] + h * o.f[2]};
        return sc::sqrt_ss(seg_seg_d2(a, b, b0, b1)) - rad - o.w - margin;
    }
    const float root = sc::sqrt_ss(seg_seg_d2(a, b, o.c, o.c)) - rad - o.r - margin;
    if (root >= 0.0f || !o.np) return root; // (a station's parts only where the root sphere is reached)
    float best = 3.4e38f;
    for (unsigned k = 0; k < o.np; ++k) {
        const float gp = sc::sqrt_ss(seg_seg_d2(a, b, o.pc[k], o.pc[k])) - rad - o.pr[k] - margin;
        if (gp < best) best = gp;
    }
    return best;
}
// The path clears the bound on every leg over a horizon of at least one second and the stop at a_lin (the engine's own warn looks no
// further than its braking distance: when a conflict enters the horizon later, the row passes and the engine's warn still has its room).
// A path that ends earlier (a detour point beyond the rollout's end) runs on straight along its last leg to that horizon. P is extended
// in place (the caller's copy).
inline bool lane_obst_clear(LanePath& P, const Member& m, const ObstBound& o) noexcept {
    if (P.n < 2) return false;
    const float LA = m.L > m.w ? m.L : m.w, wA = m.w > 0.0f ? m.w : LA;
    const float a = m.params.a_lin > 0.1f ? m.params.a_lin : 0.1f, t_req = 1.0f + P.v_end / a;
    const unsigned l = P.n - 1;
    if (P.t[l] < t_req) {
        const float d[3] = {P.p[l][0] - P.p[l - 1][0], P.p[l][1] - P.p[l - 1][1], P.p[l][2] - P.p[l - 1][2]};
        const float dl = sc::sqrt_ss(sc::dot3(d, d));
        if (!(dl > 1.0f) || P.n >= kPathMax) return false;
        const float run = P.v_end * (t_req - P.t[l]);
        for (unsigned i = 0; i < 3; ++i) P.p[P.n][i] = P.p[l][i] + d[i] / dl * run;
        P.t[P.n] = t_req;
        ++P.n;
    }
    for (unsigned i = 0; i + 1 < P.n; ++i)
        if (!(obst_gap(P, i, o, LA, wA, 0.0f) >= 0.0f)) return false;
    return true;
}
inline ObstRow lane_obst_why(const Gate& g, const Member& m, std::uint16_t order, std::uint32_t target, const float x[3], const float u[3],
                             const ObstBound& o, const float* detour, bool manual, std::uint32_t now) noexcept {
    if (!g.lane || !m.ln || manual || !(m.role == Role::committed || m.role == Role::escort)) return ObstRow::no;
    const bool order_ok = (order == sc::kOrderFlyBefore && target == g.gate) || order == sc::kOrderNone || order == sc::kOrderRotate ||
                          (m.role == Role::escort && (order == sc::kOrderEscort || order == sc::kOrderFollow) && target == m.leader);
    if (!order_ok) return ObstRow::no;
    const std::int32_t st_age = std::int32_t(now - m.st.ms);
    if (!m.st.ms || st_age < 0 || st_age > std::int32_t(kStateMaxAgeMs + g.step_ms + kStateMs)) return ObstRow::no;
    for (unsigned i = 0; i < 3; ++i)
        if (!sc::finite_f(x[i]) || !sc::finite_f(u[i]) || !sc::finite_f(o.c[i]) || !sc::finite_f(o.v[i])) return ObstRow::no;
    if (!(o.r > 0.0f) || !sc::finite_f(o.r)) return ObstRow::no;
    {
        const float dx[3] = {x[0] - m.st.x[0], x[1] - m.st.x[1], x[2] - m.st.x[2]};
        const float reach = ((m.params.v_max > 0.0f ? m.params.v_max : 0.0f) + 50.0f) * float(st_age) * 0.001f + 500.0f;
        if (!(sc::dot3(dx, dx) <= reach * reach)) return ObstRow::no;
    }
    static_assert(sizeof(LanePath) < 4096, "the lane path stays a small stack object");
    LanePath P;
    if (!lane_path(g, m, x, u, detour, now, P)) return ObstRow::roll;
    return lane_obst_clear(P, m, o) ? ObstRow::lane : ObstRow::roll;
}
// Site E's obstacle row as the DLL calls it (the fixture's engine model calls the same): the membership bitmap, the board of A, the
// object id; then lane_obst_why on the live position and velocity read (x_ok: the reads succeeded).
inline bool e_obst_row(Board& b, const Index& ix, std::uint32_t a, std::uint32_t id_a, std::uint16_t order, std::uint32_t target, const float x[3],
                       const float u[3], bool x_ok, const ObstBound& o, const float* detour, bool manual, std::uint32_t now,
                       ObstRow* why = nullptr) noexcept {
    if (why) *why = ObstRow::no;
    if (!b.lane || !a || !ix.maybe(a) || !x_ok) return false;
    int i = -1;
    const Gate* g = locate(b, a, &i);
    if (!g || i < 0 || g->m[i].id != id_a) return false;
    const ObstRow w = lane_obst_why(*g, g->m[i], order, target, x, u, o, detour, manual, now);
    if (why) *why = w;
    return w == ObstRow::lane;
}
// B.2 item 3 (Run162: 3 of 4 funnel releases evicted by the engine's 0x12 against their own gate): site E's gate row for a lane member.
// A (committed, Q known) in funnel / handover / dock / launch on B's board, B = its own gate as the live target, or the target zeroed on
// order 0 / 0x1f (the script's gap, the P2 hand-over), whatever the order; under the path guard dist(G, segment[x, P]) >= r_gate + r_A +
// 0.25 r_gate with P = Lambda_4's point at X's depth (the funnel's point). True: suppress_gate.
constexpr float kSbGateGuardR = 0.25f; // ship_avoidance_core.h lane::kCapitalGuardR (the capital rows' path guard)
// Review N6: fails closed unless the member is live, its radius is positive and the position read is finite and within reach of the
// member's last state read ((v_max + 50 m/s) per second of its age + 500 m). The hull radius is the member's own (validated at entry).
// B.3 item 2: also a member inbound within kSbGateNearR r_gate of its gate (a ship re-committed near the gate after an
// eviction), on the same orders and under the same path guard. B.3 item 6: the reason of a pass (the DLL's E row: gate_row=).
constexpr float kSbGateNearR = 6.0f;
// Lane fix round 1 item 2: lane (suppressed by the lane row below), roll (the lane row passed: the member's rollout is stale); lane fix
// round 2 item 4: guard also when the lane row's path or the hand-over reaches the gate's solid
enum class GateRow : std::uint8_t { suppress = 0, not_member, stale, unreadable, reach, stage, order, guard, lane, roll, count };
inline const char* gate_row_name(GateRow w) noexcept {
    static const char* const n[unsigned(GateRow::count)] = {"suppress", "not_member", "stale", "unreadable", "reach", "stage", "order", "guard",
                                                            "lane", "roll"};
    return unsigned(w) < unsigned(GateRow::count) ? n[unsigned(w)] : "?";
}
__attribute__((always_inline)) inline bool gate_row_suppresses(GateRow w) noexcept { return w == GateRow::suppress || w == GateRow::lane; }
// Lane fix round 1 item 2 (Run173: the engine's own 0x12 against the gate fired at 6.85-6.98 km on joiners) with lane fix round 2 item 4
// (the review of round 1: the sphere-and-slab bound assumed the solid inside the bounding sphere (the Terran ring's outer radius is
// 1.026 r), never discriminated before d_end (every point there cleared the slab), returned guard on the Terran ring (its d_end point
// inside the sphere), and the hand-over branch certified a hull 200 m off its final with the board's 1.1 s old nose and no clearance
// test (on the Terran ring up to ~470 m beyond the hole)): the own-gate row of a lane member (Board::lane, lane_frame's ln) is one
// geometric test against the gate's solid (gate_solid: the ring by the gate's TGates row; the installed ordinary gates have none), on
// the ship's live position, velocity and nose (E's reads), its hull capsule, the sweep margin (kMarginM + k (t + the rollout's age)):
// * Before d_end (inbound / funnel, order 4 or the script's order-0 gap, at any distance): its path as site A flies it (lane_path: the
//   live position and velocity, the avoidance's fresh detour turn, the board's rollout over its horizon, fresh two-sided within the
//   row's state bound, else roll) clears the solid on every leg (else guard). On a gate with a solid d_end lies the ring's front + the
//   longest hull + a stop's run-out before the plane (lane_end), so a ship before d_end cannot reach the ring within a stop whatever
//   its heading; the path's legs past d_end (the hand-over, order 5, which the engine's pair filter exempts from contact with its own
//   gate, section 10.9 Q2, and the launch from the trigger sphere about the final Q, inside the hole by q_point's window: on the Terran
//   ring l + w <= h r - 50 m) are where the test discriminates.
// * The hand-over (order 0 / 0x1f after P2, kept or not; a lane frame past d_end before P2's tick): within kLaneHandM of its final line
//   with the live direction of travel (the velocity, or the nose below 1 m/s) within 8 deg of F, and its hull now and over its stop's
//   run-out along that direction (v^2 / 2a + 1 s) clear of the solid by the margin: lane; else the path guard below (no solid: the window
//   alone).
// On the installed ordinary gates (no solid) the row reduces to the path's freshness and the hand-over's window: those gates have no
// collision body, a ship can never touch them (section 10.9), so the engine's warn against them guards nothing physical.
constexpr float kLaneHandM = 200.0f;
constexpr float kSolidStepM = 50.0f; // the solid test's sample spacing along a leg (its gap less half of it)
constexpr unsigned kSolidMaxN = 256;  // round 2 review item 6: at most this many samples per leg (a longer leg: a wider spacing, charged)
constexpr unsigned kSolidMaxLines = 32; // round 2 review item 8: at most this many swept lines across a hull (solid_gap_sweep)
// The solid's distance from a point (the 2-D distance in (radial, axial) to the ring's rectangle [h, r_out] x [z0, z1]; 0 inside).
// (Always inlined: an out-of-line float return would travel through st(0) on i386, the x87 audit.)
__attribute__((always_inline)) inline float solid_dist(const Gate& g, const GateSolid& gs, const float p[3]) noexcept {
    const float d[3] = {p[0] - g.G[0], p[1] - g.G[1], p[2] - g.G[2]};
    const float z = sc::dot3(d, g.F), a = sc::dot3(d, g.R), b = sc::dot3(d, g.U), rho = sc::sqrt_ss(a * a + b * b);
    float dr = gs.h - rho, dz = gs.z0 - z;
    if (rho - gs.r_out > dr) dr = rho - gs.r_out;
    if (z - gs.z1 > dz) dz = z - gs.z1;
    if (dr < 0.0f) dr = 0.0f;
    if (dz < 0.0f) dz = 0.0f;
    return sc::sqrt_ss(dr * dr + dz * dz);
}
// A hull capsule swept along the segment ab (extended by L - w at both ends, radius w) against the solid: the least distance over samples
// kSolidStepM apart, less half the spacing and w (< 0: it reaches the solid). A segment entirely before the ring's front by more than its
// reach is not sampled. (Round 2 review item 6: a non-finite length fails closed, -1; at most kSolidMaxN + 1 samples, the wider spacing
// charged by the same half-spacing term.)
__attribute__((always_inline)) inline float solid_gap_seg(const Gate& g, const GateSolid& gs, const float a[3], const float b[3], float LA,
                                                          float wA) noexcept {
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float dl = sc::sqrt_ss(sc::dot3(d, d)), h = LA > wA ? LA - wA : 0.0f;
    float u[3] = {0.0f, 0.0f, 0.0f};
    if (dl > 1e-3f)
        for (unsigned i = 0; i < 3; ++i) u[i] = d[i] / dl;
    float p0[3], p1[3];
    for (unsigned i = 0; i < 3; ++i) {
        p0[i] = a[i] - u[i] * h;
        p1[i] = b[i] + u[i] * h;
    }
    const float gF = sc::dot3(g.G, g.F), z0 = sc::dot3(p0, g.F) - gF, z1 = sc::dot3(p1, g.F) - gF;
    if ((z0 > z1 ? z0 : z1) + wA < gs.z0 - 1000.0f) return 1000.0f; // (far before the ring's front)
    const float len = dl + 2.0f * h;
    if (!sc::finite_f(len)) return -1.0f;
    const unsigned n = len < kSolidStepM * float(kSolidMaxN - 1u) ? unsigned(len / kSolidStepM) + 1u : kSolidMaxN;
    float best = 3.4e38f;
    for (unsigned k = 0; k <= n; ++k) {
        const float t = float(k) / float(n);
        const float q[3] = {p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t, p0[2] + (p1[2] - p0[2]) * t};
        const float e = solid_dist(g, gs, q);
        if (e < best) best = e;
    }
    return best - 0.5f * len / float(n) - wA;
}
// Round 2 review item 8: the hand-over's hull, a capsule along the live nose f (half-length LA, radius wA), swept along the direction of
// travel dir (unit) over run: the parallelogram x + a f + b dir (|a| <= h = LA - wA, 0 <= b <= run) grown by wA. Covered by n lines along
// dir through the hull points a_k (the centres of n equal bins of width s = 2 h / n), each extended by (s / 2) |f.dir| at both ends: every
// point of the parallelogram lies within (s / 2) sin(theta) of one (theta: f to dir), which is charged; n = 2 h sin(theta) / kSolidStepM
// + 1 (at most kSolidMaxLines) keeps the charge under half the sample spacing. The nose along dir: one line, the capsule along dir as
// before. An unusable nose (|f| outside [0.5, 2] or not finite): the sphere LA swept along dir.
__attribute__((always_inline)) inline float solid_gap_sweep(const Gate& g, const GateSolid& gs, const float x[3], const float f[3],
                                                            const float dir[3], float run, float LA, float wA) noexcept {
    const float fl = sc::sqrt_ss(sc::dot3(f, f));
    if (!(fl >= 0.5f && fl <= 2.0f)) {
        const float e[3] = {x[0] + dir[0] * run, x[1] + dir[1] * run, x[2] + dir[2] * run};
        return solid_gap_seg(g, gs, x, e, LA, LA);
    }
    const float fu[3] = {f[0] / fl, f[1] / fl, f[2] / fl}, h = LA > wA ? LA - wA : 0.0f;
    const float c = sc::dot3(fu, dir), c_abs = c < 0.0f ? -c : c, s2 = 1.0f - c * c, sn = s2 > 0.0f ? sc::sqrt_ss(s2) : 0.0f;
    const float nf = 2.0f * h * sn / kSolidStepM;
    const unsigned n = nf < float(kSolidMaxLines - 1u) ? unsigned(nf) + 1u : kSolidMaxLines;
    const float sp = 2.0f * h / float(n), ext = 0.5f * sp * c_abs;
    float best = 3.4e38f;
    for (unsigned k = 0; k < n; ++k) {
        const float ak = -h + (float(k) + 0.5f) * sp;
        float p0[3], p1[3];
        for (unsigned i = 0; i < 3; ++i) {
            p0[i] = x[i] + fu[i] * ak - dir[i] * ext;
            p1[i] = x[i] + fu[i] * ak + dir[i] * (run + ext);
        }
        const float e = solid_gap_seg(g, gs, p0, p1, wA, wA);
        if (!(e >= best)) best = e; // (a NaN propagates: fail closed)
    }
    return best - 0.5f * sp * sn;
}
inline GateRow lane_gate_why(const Gate& g, const Member& m, const float x[3], const float u[3], const float f[3], const float* detour,
                             std::uint32_t now) noexcept {
    const GateSolid gs = gate_solid(g.type, g.r_gate);
    const float dE = dend(m), LA = m.L > m.w ? m.L : m.w, wA = m.w > 0.0f ? m.w : LA;
    const float dx[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]}, sx = sc::dot3(dx, g.F);
    const bool lane_stage = m.sb == Sb::inbound || m.sb == Sb::funnel;
    const float v = sc::sqrt_ss(sc::dot3(u, u));
    float dir[3];
    for (unsigned i = 0; i < 3; ++i) dir[i] = v >= 1.0f ? u[i] / v : f[i];
    if ((m.sb == Sb::handover || (lane_stage && sx > -dE)) && sx < 0.0f && sc::dot3(dir, g.F) >= kLaneHoldCos) {
        // on its final at or past d_end (the hand-over; an order-4 or order-0 frame before P2's tick)
        float lat[2];
        lane_lat_at(g, m, -dE, lat); // (its final: Q, or l_c for a member that entered as an escort)
        const float er = sc::dot3(dx, g.R) - lat[0], eu = sc::dot3(dx, g.U) - lat[1];
        if (er * er + eu * eu <= kLaneHandM * kLaneHandM) {
            if (!gs.solid) return GateRow::lane;
            // (round 2 review item 8: the hull along its live nose, swept along the direction of travel; the window above is the
            // velocity's, up to 8 deg off F, while the hull lies along the nose)
            const float a = m.params.a_lin > 0.1f ? m.params.a_lin : 0.1f, run = v * v / (2.0f * a) + v;
            return solid_gap_sweep(g, gs, x, f, dir, run, LA, wA) - kMarginM >= 0.0f ? GateRow::lane : GateRow::guard;
        }
    }
    if (!lane_stage) return GateRow::count; // (the path guard)
    if (!gs.solid) return lane_path_exists(g, m, x, u, detour, now) ? GateRow::lane : GateRow::roll; // (round 2 review item 7)
    LanePath P;
    if (!lane_path(g, m, x, u, detour, now, P)) return GateRow::roll;
    for (unsigned i = 0; i + 1 < P.n; ++i) {
        const float margin = kMarginM + kDriftMs * (P.t[i + 1] + P.age_s);
        if (!(solid_gap_seg(g, gs, P.p[i], P.p[i + 1], LA, wA) - margin >= 0.0f)) return GateRow::guard;
    }
    return GateRow::lane;
}
// (u, f, detour: the live velocity and nose of A and its avoidance's fresh detour point (lane fix round 2 item 4; null: the member's
// state's, the fixture's units))
inline GateRow sb_gate_why(const Gate& g, const Member& m, std::uint32_t gate, std::uint16_t order, std::uint32_t target, const float x[3],
                           std::uint32_t now, const float* u = nullptr, const float* f = nullptr, const float* detour = nullptr) noexcept {
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
    const bool lane = g.lane && m.ln;
    if (!(m.sb == Sb::funnel || m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch)) {
        if (m.sb != Sb::inbound) return GateRow::stage;
        const float dg[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]}, near = kSbGateNearR * g.r_gate;
        if (!lane && !(sc::dot3(dg, dg) <= near * near)) return GateRow::stage;
    }
    // review F3: never on the engine's own avoid order (a member kept through a 0x12 keeps its board state, not the suppression)
    if (order == sc::kOrderAvoid || !(target == gate || (!target && (order == sc::kOrderNone || order == sc::kOrderRotate)))) return GateRow::order;
    if (lane) { // lane fix round 1 item 2 (lane_gate_why; count: the path guard below)
        const GateRow w = lane_gate_why(g, m, x, u ? u : m.st.u, f ? f : m.st.rows[2], detour, now);
        if (w != GateRow::count) return w;
    }
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
                        std::uint32_t now, const float* u = nullptr, const float* f = nullptr, const float* detour = nullptr) noexcept {
    return gate_row_suppresses(sb_gate_why(g, m, gate, order, target, x, now, u, f, detour));
}
// Review N8: site E's own-gate row as the DLL's ctl_e_fields reads it (the fixture's own-gate model calls the same): the membership
// bitmap, the board of A, B its gate, the object id; then sb_gate_row on the live position read (x_ok: the read succeeded).
inline bool e_gate_row(Board& b, const Index& ix, std::uint32_t a, std::uint32_t id_a, std::uint32_t gate, std::uint16_t order,
                       std::uint32_t target, const float x[3], bool x_ok, std::uint32_t now, GateRow* why = nullptr, const float* u = nullptr,
                       const float* f = nullptr, const float* detour = nullptr) noexcept {
    if (why) *why = GateRow::not_member;
    if (!a || !ix.maybe(a)) return false;
    int i = -1;
    const Gate* g = locate(b, a, &i);
    if (!g || i < 0 || g->gate != gate || g->m[i].id != id_a) return false;
    if (!x_ok) {
        if (why) *why = GateRow::unreadable;
        return false;
    }
    const GateRow w = sb_gate_why(*g, g->m[i], gate, order, target, x, now, u, f, detour);
    if (why) *why = w;
    return gate_row_suppresses(w);
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

// ---- the predicted launch of a released member (lane_frame's escort gate and pace law; stage B's sequencer was deleted on
// 2026-10-15) --------------------------------------------------------------------------------------------------------------
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
        return t_x + std::uint32_t(1000.0f * (1.0f + leg5_s(m, sc::sqrt_ss(sc::dot3(xg, xg)), vx)));
    }
    return now;
}
// ---- Lane stage 1: the frame (convoy-gate-transit.md sections 6.1-6.6; stage B's sb_frame until 2026-10-15) -------
// Kept from stage B: the keys and the escorts' reservation (tie-breakers and the leader-first rule), the stages from the live orders
// (inbound = joining Lambda, funnel = on Lambda; hand-over, order 5, order 8), the refusal at the hand-over, the slot block from a capital
// arrival. Deleted: petals, rings, the release, the lap law, the funnel's one-ship rule, the head skip, the escort stage (section 6.9).
struct LaneEvent {
    std::uint32_t ship = 0, pred = 0;
    std::uint8_t finish = 0; // 0 the merge's start, 1 its completion (the ship_gate_ctl_merge row)
    Law law = Law::none;
    float gap = 0;
    std::uint32_t ms = 0; // the completion's duration since the start (game ms)
};
constexpr unsigned kLaneEvents = 16;
struct LaneStats {
    unsigned merges = 0, merges_done = 0, x_refused = 0, clock_resets = 0, members = 0;
    std::uint32_t x_wait_ms = 0;
    unsigned n_ev = 0, ev_dropped = 0;
    LaneEvent ev[kLaneEvents];
};
__attribute__((always_inline)) inline void lane_event(LaneStats* st, const Member& m, bool finish, float gap, std::uint32_t ms) noexcept {
    if (!st) return;
    if (st->n_ev >= kLaneEvents) {
        ++st->ev_dropped;
        return;
    }
    LaneEvent& e = st->ev[st->n_ev++];
    e.ship = m.ship;
    e.pred = m.lane_pred;
    e.finish = finish ? 1 : 0;
    e.law = m.law;
    e.gap = gap;
    e.ms = ms;
}
// Whether e is an escort of the leader L (its live 0x14 / 0x15 leader, or a reserved key of L's key class: an escort committed on its own
// order 4 keeps its place behind its leader through the key).
__attribute__((always_inline)) inline bool lane_escort_of(const Member& e, const Member& L) noexcept {
    if (&e == &L || !e.ship || !L.ship) return false;
    if (e.role == Role::escort && e.leader == L.ship) return true;
    return e.key_res && L.role == Role::committed && !L.key_res && L.key && (e.key & ~15u) == L.key && e.key != L.key;
}
// The ETA to d_end the lane order ranks by (s, at the lane speed): along Lambda when on it; the straight join plus the rest when joining;
// out to the turn point and back (with a half turn at the hull's rate) when re-departing.
inline float lane_eta(const Member& m) noexcept {
    const float v = lane_vmax(m) > kYieldFloorMs ? lane_vmax(m) : kYieldFloorMs, s_end = -dend(m), s = m.lane_s;
    float path;
    if (m.redep) {
        const float s_out = -(dend(m) + m.redep_out), W = m.params.W > 1e-3f ? m.params.W : 1e-3f;
        path = (s > s_out ? s - s_out : 0.0f) + (s_end - s_out) + sc::kPi * v / W;
    } else if (m.lane_on)
        path = s_end > s ? s_end - s : 0.0f;
    else {
        const float rest = s_end - s;
        path = rest > 0.0f ? sc::sqrt_ss(m.lane_lat * m.lane_lat + rest * rest) : m.lane_lat - rest;
    }
    return path / v;
}
// The lane order's score (review item 3): the ETA to d_end at the common speed kGateLargeCapMs (the large cap), from lane_eta's path; on
// Lambda the along-F distance (negative past d_end), so that the order on Lambda is the position order.
inline float lane_score(const Member& m) noexcept {
    const float v = sc::kGateLargeCapMs, s_end = -dend(m), s = m.lane_s;
    if (m.redep) {
        const float s_out = -(dend(m) + m.redep_out), W = m.params.W > 1e-3f ? m.params.W : 1e-3f;
        return ((s > s_out ? s - s_out : 0.0f) + (s_end - s_out) + sc::kPi * v / W) / v;
    }
    if (m.lane_on) return (s_end - s) / v;
    const float rest = s_end - s;
    return (rest > 0.0f ? sc::sqrt_ss(m.lane_lat * m.lane_lat + rest * rest) : m.lane_lat - rest) / v;
}
// A lane member's predicted end of order 8 (game ms): to d_end at the lane speed, the hand-over (1 s), the order-5 leg from d_end and
// order 8; the prediction (eta_launch) from the hand-over on.
inline std::uint32_t lane_launch_eta(const Gate& g, const Member& m, std::uint32_t now) noexcept {
    if (m.sb == Sb::inbound || m.sb == Sb::funnel) {
        const float v = lane_vmax(m) > kYieldFloorMs ? lane_vmax(m) : kYieldFloorMs;
        return now + std::uint32_t(std::int32_t(1000.0f * (m.lane_eta + 1.0f + leg5_s(m, dend(m), v))));
    }
    return eta_launch(g, m, now);
}
// Lane fix round 1 item 3: a hand-over's wait beyond 3 s (ctl_x_wait_s), accrued up to now (x_acc_ms: what is counted already).
__attribute__((always_inline)) inline void lane_x_wait(Member& m, std::uint32_t now, LaneStats* st) noexcept {
    const std::uint32_t w = now - m.x_ms > 3000u ? now - m.x_ms - 3000u : 0u;
    if (w > m.x_acc_ms) {
        if (st) st->x_wait_ms += w - m.x_acc_ms;
        m.x_acc_ms = w;
    }
}
inline void lane_frame(Gate& g, std::uint32_t now, LaneStats* st) noexcept {
    if (!g.gate || !(g.r_gate > 0.0f)) return;
    if (g.sb_last_ms && std::int32_t(now - g.sb_last_ms) < 0) { // a backward game-clock jump (a load): stamps from the future are void
        g.slot_block_ms = g.funnel_end_ms = g.last_arrival_seen = 0;
        g.lane_n = 0;
        for (unsigned i = 0; i < g.n; ++i) {
            Member& m = g.m[i];
            m.x_ms = m.o8_ms = 0;
            m.refused = 0;
            m.cap = -1.0f;
            m.cap_ms = m.yield_ms = m.v_plan_ms = 0;
            m.merge_ms = m.stop_ms = m.lane_free_ms = m.lane_gate_ms = 0;
            m.merged = 0;
            m.lane_pred = 0;
            m.lstop_ms = m.lstop_last_ms = m.x_acc_ms = 0; // (lane fix round 1)
            m.defer_w = 0;      // final review item 2 (ship-navigation-ai.md section 10.13 item 4): no deferral survives a load
            m.lane_ord_ok = 0; // final review item 1: the sticky order moments are of the old clock
            m.roll_ms = 0;     // lane fix round 2 item 4 (review 7): a rollout of the old clock is no certificate
            m.n = 0;
            if (m.exit_ms) m.exit_ms = now ? now : 1; // (item 2: the exit's bound restarts on the new clock)
            // (round 2 review item 1: an overshoot's predicted return is of the old clock: now - over_ret_ms would stay negative and
            // its block hold for the rest of the out-leg; the return is re-taken from here by the lane order's over_new pass)
            m.over_ret_ms = 0;
            m.over_ms = m.over ? (now ? now : 1) : 0;
            m.over_new = m.over;
        }
        if (st) ++st->clock_resets;
    }
    g.sb_last_ms = now ? now : 1;
    if (g.cap_arrival_ms && g.cap_arrival_ms != g.last_arrival_seen) { // a capital launch here: our side refused 60 s (section 10.2)
        g.last_arrival_seen = g.cap_arrival_ms;
        g.slot_block_ms = g.cap_arrival_ms + kSlotArrivalMs;
    }
    // the finals (Q, lane_q) and Lambda's common lateral l_c: the middle of the members' finals
    const bool terran = gate_terran(g.type);
    float lo[2] = {3.4e38f, 3.4e38f}, hi[2] = {-3.4e38f, -3.4e38f};
    for (unsigned i = 0; i < g.n; ++i) {
        Member& m = g.m[i];
        if (!m.ship || !(m.role == Role::committed || m.role == Role::escort)) continue;
        m.ln = 1;
        m.d_end = lane_end(g.r_gate, g.type); // (lane fix round 2 item 4: the gate's d_end)
        // lane fix round 2 item 2: an escort whose live 0x14 / 0x15 names another leader is leaving the lane (validity keeps it until clear)
        if (m.role == Role::escort && (m.st.order == sc::kOrderEscort || m.st.order == sc::kOrderFollow) && m.st.target && m.st.target != m.leader) {
            if (!m.exit_ms || m.exit_tgt != m.st.target) {
                m.exit_ms = now ? now : 1;
                m.exit_tgt = m.st.target;
                m.exit_clear = 0;
            }
        } else if (m.exit_ms && (m.role != Role::escort || m.st.target == m.leader))
            m.exit_ms = m.exit_tgt = m.exit_clear = 0; // (handed back; round 2 review item 2: or no longer an escort)
        if (m.role == Role::escort) m.lane_esc = 1;
        m.q_ok = lane_q(m.r, m.w, m.L, m.params.v_max, g.r_gate, terran, m.q) ? 1 : 0;
        m.lane_settle = pursuit_look(lane_vmax(m), m.params.W, g.r_gate); // (lane fix round 1: lane_blend_settle's cache)
        if (!m.q_ok) continue;
        m.l4[0] = m.a_lat[0] = m.q[0];
        m.l4[1] = m.a_lat[1] = m.q[1];
        m.zx = -dend(m); // lambda4 (eta_launch, site E's guard line): the lane point at d_end
        for (unsigned c = 0; c < 2; ++c) {
            if (m.q[c] < lo[c]) lo[c] = m.q[c];
            if (m.q[c] > hi[c]) hi[c] = m.q[c];
        }
    }
    if (lo[0] <= hi[0]) { // l_c: the middle of the members' finals, set at once on an empty lane, else rate-limited (kLaneShiftMs)
        float tc[2] = {0.5f * (lo[0] + hi[0]), 0.5f * (lo[1] + hi[1])};
        // (lane fix round 2 item 4: on a gate with a solid l_c stays the middle of the finals, inside the ring's hole where every
        // final's window lies: an escort holding on l_c at d_end, then order 5, then order 8 from its trigger, passes the hole; its
        // order 4's first tick is deferred by site B (lane_defer_step), the grid spheres' first row lies at 2 (r_gate + r_s), far
        // beyond d_end)
        const float clear = -(sc::kGateCrossFraction * g.r_gate + kLaneGridClearM);
        if (tc[1] > clear && !gate_solid(g.type, g.r_gate).solid) tc[1] = clear;
        if (!g.lane_c_ok) {
            g.lane_c[0] = tc[0];
            g.lane_c[1] = tc[1];
            g.lane_c_ok = 1;
        } else {
            const float lim = kLaneShiftMs * float(g.step_ms > kStepClampMs ? kStepClampMs : g.step_ms) * 0.001f;
            for (unsigned c = 0; c < 2; ++c) {
                const float d = tc[c] - g.lane_c[c];
                g.lane_c[c] += d > lim ? lim : d < -lim ? -lim : d;
            }
        }
    } else
        g.lane_c_ok = 0;
    // keys, reservations, stages, the position on Lambda
    for (unsigned i = 0; i < g.n; ++i) {
        Member& m = g.m[i];
        if (!m.ship || !(m.role == Role::committed || m.role == Role::escort)) {
            if (m.ship) m.sb = Sb::none;
            m.lane_rank = 0xff;
            m.law = Law::none;
            continue;
        }
        if (!m.st.ms) continue;
        if (!m.key) m.key = m.seq << 4;
        const Sb before = m.sb;
        if (m.role == Role::escort) {
            const int li = find(g, m.leader);
            if (!m.key_res && li >= 0 && g.m[li].role == Role::committed && g.m[li].key) { // the reservation, once (D.3): a tie-breaker
                unsigned rank = 0;
                for (unsigned j = 0; j < g.n; ++j)
                    if (j != i && g.m[j].role == Role::escort && g.m[j].leader == m.leader && g.m[j].seq < m.seq) ++rank;
                m.key = g.m[li].key + 1u + (rank < 14u ? rank : 14u);
                m.key_res = 1;
            }
            // the leader on order 5 or 8: its launch (predicted on order 5, the order-8 stamp once seen; a SETA step can hide the 0.9 s of
            // order 8 from every frame); the escort stays a member through the lost-leader wait until its own order 4 commits it
            if (li >= 0 && g.m[li].role == Role::committed && (g.m[li].sb == Sb::dock || (g.m[li].sb == Sb::launch && g.m[li].o8_ms))) {
                m.res_o8_ms = g.m[li].sb == Sb::launch ? g.m[li].o8_ms : eta_launch(g, g.m[li], now) - 900u;
                if (!m.res_o8_ms) m.res_o8_ms = 1;
                if (!m.res_until) m.res_until = m.res_o8_ms + kReserveMs;
            }
            if (m.res_until && m.key_res) { // B.3 item 4: refreshed while on 0x15 to the gate or within kReserveKeepM of it
                const float d[3] = {m.st.x[0] - g.G[0], m.st.x[1] - g.G[1], m.st.x[2] - g.G[2]};
                if ((m.st.order == sc::kOrderFollow && m.st.target == g.gate) || sc::dot3(d, d) <= kReserveKeepM * kReserveKeepM) {
                    const std::uint32_t until = now + kReserveMs;
                    if (std::int32_t(until - m.res_until) > 0) m.res_until = until;
                }
            }
            if (m.sb != Sb::funnel) m.sb = Sb::inbound;
        } else {
            const std::uint16_t o = m.st.order;
            if (o == sc::kOrderFlyBefore) {
                if (m.sb != Sb::inbound && m.sb != Sb::funnel) {
                    m.x_ms = m.o8_ms = 0;
                    m.refused = 0;
                    m.sb = Sb::inbound;
                }
            } else if ((o == sc::kOrderNone || o == sc::kOrderRotate) && m.handover_ms) {
                if (m.sb != Sb::handover) {
                    m.sb = Sb::handover;
                    m.x_ms = m.handover_ms;
                    m.refused = 0;
                    m.o8_ms = 0;
                    m.x_acc_ms = 0;
                }
            } else if (o == sc::kOrderDock) {
                if (m.sb == Sb::handover && m.x_ms) lane_x_wait(m, now, st); // (the rest of the wait)
                m.sb = Sb::dock;
                m.handover_ms = m.x_ms = 0;
                m.refused = 0;
            } else if (o == sc::kOrderWarpAway) {
                m.sb = Sb::launch;
                m.handover_ms = m.x_ms = 0;
                m.refused = 0;
                if (!m.o8_ms) m.o8_ms = m.st.ms;
                const std::uint32_t end = m.o8_ms + 900u;
                if (!g.funnel_end_ms || std::int32_t(end - g.funnel_end_ms) > 0) g.funnel_end_ms = end;
            }
            // waiting at d_end for the through slot (review item 8: a signed game-time bound plus the frame step)
            if (m.sb == Sb::handover && !m.refused && m.x_ms && std::int32_t(now - m.x_ms) > std::int32_t(age_bound(g, kLaneRefuseMs))) {
                m.refused = 1;
                if (st) ++st->x_refused;
            }
            // lane fix round 1 item 3 (Run173: a 116 s refusal left ctl_x_wait_s at 0, the wait was counted only on an observed order 5):
            // the wait beyond 3 s accrues every frame from the refusal
            if (m.sb == Sb::handover && m.refused && m.x_ms) lane_x_wait(m, now, st);
        }
        const float d[3] = {m.st.x[0] - g.G[0], m.st.x[1] - g.G[1], m.st.x[2] - g.G[2]};
        m.lane_s = sc::dot3(d, g.F);
        float lat[2];
        lane_lat_at(g, m, m.lane_s, lat);
        const float er = sc::dot3(d, g.R) - lat[0], eu = sc::dot3(d, g.U) - lat[1];
        m.lane_lat = sc::sqrt_ss(er * er + eu * eu);
        const float fF = sc::dot3(m.st.rows[2], g.F);
        if (m.lane_on) {
            if (m.lane_lat > kLaneOffM || fF < 0.0f) m.lane_on = 0;
        } else if (m.lane_lat <= kLaneOnM && fF >= kLaneOnCos)
            m.lane_on = 1;
        // lane fix round 1 item 4 (Run173: E1 past the plane at s = +703 m after an 0x12 flew out 6 km, the re-departure's leg, to turn):
        // a member past the plane without launching is an overshoot until it is back on Lambda; its turn point lies its settling distance
        // + its turn radius R = v / W beyond d_end (the U-turn ends near it heading +F, then the settling run to P2: without the radius the
        // nose came to P2 12-14 deg off F in scenario C), not a re-departure's kLaneRedepM; the U-turn's own extent beyond it is
        // kLaneOverApexR R at most (scenario C overshoot: 1.53-1.69 km for an M1 at 100 m/s, R = 637 m: the pursuit's turn and the slip)
        // (lane fix round 2 item 5: only a member seen in front of the plane since it entered: one that committed from behind the gate is a
        // re-departure, not an overshoot ranked as the head)
        if (m.lane_s < 0.0f) m.front_seen = 1;
        if (m.sb == Sb::inbound || m.sb == Sb::funnel) {
            if (m.lane_s > 0.0f && m.front_seen) {
                if (!m.over) {
                    m.over_ms = now ? now : 1;
                    m.over_new = 1;
                    m.over_in = 0;
                }
                m.over = 1;
            } else if (m.lane_on || !m.front_seen)
                m.over = 0;
        } else
            m.over = 0;
        // (its out-leg runs 2.5 R below Lambda, the U-turn's measured lateral extent, so the U-turn ends on Lambda heading +F: with 2 R it
        // ended about R / 2 above it and P2 came 8.5 deg off F in 2 of 9 runs)
        m.redep_r = m.over && m.params.W > 1e-3f ? lane_vmax(m) / m.params.W : 0.0f;
        m.redep_out = m.over ? lane_blend_settle(g, m) + m.redep_r : kLaneRedepM;
        m.redep_side = m.over ? 2.5f * m.redep_r : kLaneRedepSideM;
        if (m.redep && m.lane_s < -(dend(m) + m.redep_out) && fF >= 0.0f) { // turned in: the join from here
            m.redep = 0;
            if (m.over) m.over_in = 1; // (item 3c: an overshoot's law runs from its own s from here)
        }
        if (!m.lane_on && fF < 0.0f && m.lane_s > -(dend(m) + m.redep_out)) m.redep = 1; // nose away inside the turn point: out first
        if (m.lane_on) m.redep = 0;
        if (m.sb == Sb::inbound || m.sb == Sb::funnel) m.sb = m.lane_on ? Sb::funnel : Sb::inbound;
        m.lane_eta = lane_eta(m);
        (void)before;
    }
    // the lane order (section 6.2): members joining or on Lambda, front first, by one score each: the ETA to d_end at the common speed
    // (lane_score; review item 3: on Lambda that is the position, so a slow hull ahead is never ranked behind a fast one closing from
    // behind, and a joiner compares against the on-lane ship's position on the same scale). A strict order, rebuilt every frame (final
    // review item 1: HEAD's adjacent swaps from the previous order let a joiner inside the hysteresis of both neighbours hold two ships on
    // Lambda against their positions): (1) the members on Lambda by position (the exact score; no hysteresis ever reorders two of them);
    // (2) the joiners by a sticky score (lane_ord_ms: the absolute moment of the score, re-taken only when the live one drifts more than
    // kLaneKeepS / 2 from it, so two joiners keep their order unless their ETAs differ by up to kLaneKeepS), within a tie cluster (members
    // within kLaneTieS of the cluster's first) the lower key first; (3) the two chains merged, a joiner before an on-lane ship when its
    // score is earlier beyond the tie band or, within it, its key is lower; (4) a leader always before its own escorts. Every step keeps a
    // total order (ties: the key, then the ship). Cost: insertion sorts and the leader move, O(n^2) comparisons worst case for n members
    // (n <= kMembers = 24), O(n) on an already ordered lane.
    unsigned idx[kMembers], n = 0, on_ix[kMembers], jn_ix[kMembers], n_on = 0, n_jn = 0;
    std::uint8_t onf[kMembers];
    float score[kMembers];
    auto before = [&](unsigned a, unsigned b) -> bool { // the strict total order on (score, key, ship)
        if (score[a] != score[b]) return score[a] < score[b];
        if (g.m[a].key != g.m[b].key) return g.m[a].key < g.m[b].key;
        return g.m[a].ship < g.m[b].ship;
    };
    auto insert = [&](unsigned* arr, unsigned& cnt, unsigned i) { // insertion into a chain sorted by `before`
        unsigned j = cnt;
        while (j > 0 && before(i, arr[j - 1])) {
            arr[j] = arr[j - 1];
            --j;
        }
        arr[j] = i;
        ++cnt;
    };
    constexpr std::int32_t kKeepHalfMs = std::int32_t(kLaneKeepS * 500.0f);
    for (unsigned i = 0; i < g.n; ++i) {
        Member& m = g.m[i];
        onf[i] = m.ship && (m.role == Role::committed || m.role == Role::escort) && live(g, m, now) && m.q_ok &&
                 (m.sb == Sb::inbound || m.sb == Sb::funnel);
        if (!onf[i]) {
            m.lane_ord_ok = 0;
            m.lane_rank = 0xff;
            m.lane_pred = 0;
            m.lane_free_ms = m.lane_gate_ms = 0;
            if (m.ship && (m.role == Role::committed || m.role == Role::escort)) {
                m.law = m.sb == Sb::handover ? Law::hold : Law::none;
                m.v_sb = lane_vmax(m);
            }
            continue;
        }
        const float sc_live = lane_score(m);
        const float ms_f = 1000.0f * (sc_live > -2.0e6f ? (sc_live < 2.0e6f ? sc_live : 2.0e6f) : -2.0e6f); // (an int32 of game ms)
        const std::uint32_t t = now + std::uint32_t(std::int32_t(ms_f));
        if (m.exit_ms && m.role == Role::escort) { // lane fix round 2 item 2: a leaving escort keeps its place in the order (its moment
                                                   // frozen at the hand), so the ship behind it keeps following it until it is clear
            if (!m.lane_ord_ok) {
                m.lane_ord_ms = t;
                m.lane_ord_ok = 1;
            }
            score[i] = 0.001f * float(std::int32_t(m.lane_ord_ms - now));
            insert(jn_ix, n_jn, i);
            continue;
        }
        if (m.lane_on || !m.lane_ord_ok) {
            m.lane_ord_ms = t;
            m.lane_ord_ok = 1;
        } else {
            const std::int32_t drift = std::int32_t(t - m.lane_ord_ms);
            if (drift > kKeepHalfMs || drift < -kKeepHalfMs) m.lane_ord_ms = t;
        }
        score[i] = m.lane_on ? sc_live : 0.001f * float(std::int32_t(m.lane_ord_ms - now));
        if (m.lane_on) insert(on_ix, n_on, i);
        else insert(jn_ix, n_jn, i);
    }
    for (unsigned c = 0; c < n_jn;) { // the joiners' tie clusters: within kLaneTieS of the cluster's first, the lower key first
        unsigned e = c + 1;
        while (e < n_jn && score[jn_ix[e]] - score[jn_ix[c]] <= kLaneTieS) ++e;
        for (unsigned a = c + 1; a < e; ++a) { // insertion by (key, ship) inside [c, e)
            const unsigned x = jn_ix[a];
            unsigned j = a;
            while (j > c && (g.m[x].key != g.m[jn_ix[j - 1]].key ? g.m[x].key < g.m[jn_ix[j - 1]].key : g.m[x].ship < g.m[jn_ix[j - 1]].ship)) {
                jn_ix[j] = jn_ix[j - 1];
                --j;
            }
            jn_ix[j] = x;
        }
        c = e;
    }
    for (unsigned a = 0, b = 0; a < n_on || b < n_jn;) { // the merge: the on-lane chain keeps its position order whatever the joiners
        bool take_j = a >= n_on;
        if (!take_j && b < n_jn) {
            const Member &J = g.m[jn_ix[b]], &O = g.m[on_ix[a]];
            const float ds = score[jn_ix[b]] - score[on_ix[a]];
            take_j = sc::fabs_f(ds) <= kLaneTieS ? (J.key != O.key ? J.key < O.key : J.ship < O.ship) : ds < 0.0f;
        }
        idx[n++] = take_j ? jn_ix[b++] : on_ix[a++];
    }
    // lane fix round 1 item 4 (Run173: E1 past the plane, the closest to the gate, was ranked behind E2 4 km out as a re-departing joiner):
    // an overshoot never ranks behind a member farther from the gate (it rejoins as the head when it is the closest); O(n) per overshoot
    // (lane fix round 2 items 3a / 5: a member inside the overshoot's return zone when the overshoot began, nearer the gate than its U-turn's
    // apex less g_req, both half-lengths and that member's own stop, stays ahead of it: the overshoot rejoins behind it and follows it (its
    // law from the apex); the members farther out rank behind the overshoot and stop at spacing behind its apex. The zone is decided once:
    // a member's own stop toward the apex would otherwise flip it in and out)
    unsigned ov[kMembers], n_ov = 0;
    for (unsigned k = 0; k < n; ++k)
        if (g.m[idx[k]].over) ov[n_ov++] = idx[k];
    for (unsigned t = 0; t < n_ov; ++t) {
        const unsigned x = ov[t];
        Member& o = g.m[x];
        const float d[3] = {o.st.x[0] - g.G[0], o.st.x[1] - g.G[1], o.st.x[2] - g.G[2]}, dO = sc::dot3(d, d);
        const float s_ap = -(dend(o) + o.redep_out + kLaneOverApexR * o.redep_r);
        if (o.over_new) { // its return: the out-leg to its turn point, a U-turn of pi R and the rejoin to its apex at the lane speed
            const float vl = lane_vmax(o) > kYieldFloorMs ? lane_vmax(o) : kYieldFloorMs;
            const float t_ret = (o.lane_s + dend(o) + o.redep_out + (3.14159265f + kLaneOverApexR) * o.redep_r) / vl;
            o.over_ret_ms = now + std::uint32_t(1000.0f * (t_ret > 0.0f ? t_ret : 0.0f));
        }
        if (o.over_new)
            for (unsigned q2 = 0; q2 < n; ++q2) {
                Member& a = g.m[idx[q2]];
                if (&a == &o) continue;
                const float va = sc::dot3(a.st.u, g.F), aa = a.params.a_lin > 0.1f ? a.params.a_lin : 0.1f;
                if (a.lane_s > s_ap - (kLaneGapM + o.L + a.L + (va > 0.0f ? va * va / (2.0f * aa) : 0.0f))) a.over_ahead_of = o.ship;
                else if (a.over_ahead_of == o.ship) a.over_ahead_of = 0;
            }
        unsigned k = 0;
        while (idx[k] != x) ++k;
        for (unsigned j = k; j + 1 < n; ++j) idx[j] = idx[j + 1]; // (out, then in at its place)
        unsigned q = 0;
        for (; q + 1 < n; ++q) {
            const Member& a = g.m[idx[q]];
            const float e[3] = {a.st.x[0] - g.G[0], a.st.x[1] - g.G[1], a.st.x[2] - g.G[2]};
            if (sc::dot3(e, e) > dO && a.over_ahead_of != o.ship) break;
            if (q >= k && a.over_ahead_of != o.ship) break; // (no further back than its own place but behind its zone's members)
        }
        for (unsigned j = n - 1; j > q; --j) idx[j] = idx[j - 1];
        idx[q] = x;
    }
    for (unsigned k = 0; k < n; ++k) g.m[idx[k]].over_new = 0;
    for (unsigned k = 0; k < n;) { // the leader-first rule: an escort ahead of its leader moves right behind it
        unsigned j = n;
        for (unsigned q = k + 1; q < n && j == n; ++q)
            if (lane_escort_of(g.m[idx[k]], g.m[idx[q]])) j = q;
        if (j == n) {
            ++k;
            continue;
        }
        const unsigned e = idx[k];
        for (unsigned q = k; q < j; ++q) idx[q] = idx[q + 1];
        idx[j] = e;
    }
    // the slot's free time (the head's pacing, section 6.6): the through slot's holder (a committed member at the hand-over, on order 5,
    // or on order 8 before its end) releases at its predicted launch + the hand-over gap; a capital arrival's block
    std::uint32_t t_free = 0;
    for (unsigned i = 0; i < g.n; ++i) {
        const Member& o = g.m[i];
        if (!o.ship || o.role != Role::committed || !live(g, o, now)) continue;
        if (!(o.sb == Sb::handover || o.sb == Sb::dock || (o.sb == Sb::launch && std::int32_t(now - (o.o8_ms + 900u)) < 0))) continue;
        const std::uint32_t t = eta_launch(g, o, now) + kFunnelGapMs;
        if (!t_free || std::int32_t(t - t_free) > 0) t_free = t;
    }
    if (g.slot_block_ms && std::int32_t(g.slot_block_ms - now) > 0 && (!t_free || std::int32_t(g.slot_block_ms - t_free) > 0)) t_free = g.slot_block_ms;
    if (t_free && std::int32_t(t_free - now) <= 0) t_free = 0;
    // the head's predecessor: the rearmost committed member past d_end (the hand-over, order 5, order 8 before its end): it is ahead on
    // the same final, so the head follows it as any follower (it is no lane rank: the stage-A rank rule keeps it ahead)
    int gp = -1;
    for (unsigned i = 0; i < g.n; ++i) {
        const Member& o = g.m[i];
        if (!o.ship || o.role != Role::committed || !live(g, o, now)) continue;
        if (!(o.sb == Sb::handover || o.sb == Sb::dock || (o.sb == Sb::launch && std::int32_t(now - (o.o8_ms + 900u)) < 0))) continue;
        const float d[3] = {o.st.x[0] - g.G[0], o.st.x[1] - g.G[1], o.st.x[2] - g.G[2]};
        g.m[i].lane_s = sc::dot3(d, g.F);
        if (gp < 0 || g.m[i].lane_s < g.m[gp].lane_s) gp = int(i);
    }
    // the laws, front first (the follower reads its predecessor's speed of this frame)
    g.lane_n = n;
    for (unsigned k = 0; k < n; ++k) {
        Member& m = g.m[idx[k]];
        g.lane_ord[k] = m.ship;
        m.lane_rank = std::uint8_t(k);
        const Member* P = k ? &g.m[idx[k - 1]] : gp >= 0 ? &g.m[gp] : nullptr;
        // lane fix round 2 item 2: leaving (handed to another leader): its laws as before (lane_steer: their speed on the engine's leg to
        // the new leader); clear once its hull is a lane footprint (kLaneOffM) off Lambda
        if (m.exit_ms && m.role == Role::escort) m.exit_clear = m.lane_lat >= kLaneOffM + m.w ? 1 : 0;
        LaneCtx cx;
        // (lane fix round 2 items 3a / 5: an overshoot behind a member inside its return zone runs its law from its U-turn's apex, where it
        // rejoins)
        // (lane fix round 2 item 3c: the head too, until it has turned in: its pacing and its follow law measured from where it rejoins, not
        // from its position past d_end, where the pacing's stop at d_end held it 19 s in its turn back)
        const float s_self_ap = -(dend(m) + m.redep_out + kLaneOverApexR * m.redep_r);
        const float s_self = m.over && !m.over_in && m.lane_s > s_self_ap ? s_self_ap : m.lane_s;
        cx.s = s_self;
        cx.v_lane = lane_vmax(m);
        cx.a = m.params.a_lin;
        cx.v_self = sc::dot3(m.st.u, g.F);
        cx.y = m.lane_on ? 0.0f : m.lane_lat;
        float gap = 3.4e38f;
        cx.d_end = dend(m);
        cx.loss = lane_join_loss(m.lane_s, cx.y, lane_join_end(m), dend(m), lane_turn_r(m));
        if (P && k) cx.loss -= lane_join_loss(P->lane_s, P->lane_on ? 0.0f : P->lane_lat, lane_join_end(*P), dend(*P), lane_turn_r(*P));
        if (P) {
            // (lane fix round 1 item 4: an overshoot ahead turns back no farther out than its U-turn's apex: no nearer than that along F;
            // until it has turned in it is a block, its follower stops at spacing behind the apex)
            const float s_rj = -(dend(*P) + P->redep_out + kLaneOverApexR * P->redep_r), sp = k && P->over && !P->over_in && P->lane_s > s_rj ? s_rj : P->lane_s;
            gap = (sp - P->L) - (s_self + m.L);
            float vp = sc::dot3(P->st.u, g.F);
            cx.closing = vp < -kLaneStopV && !(k && P->over); // (lane fix round 2 item 5: flying back toward it; an overshoot's out-leg is
                                                               // its block, bounded below)
            if (P->v_sb >= 0.0f && P->v_sb < vp) vp = P->v_sb;
            cx.pred = true;
            cx.gap = gap;
            cx.v_pred = vp > 0.0f ? vp : 0.0f;
            // (a block: stopped, or an overshoot turning back; lane fix round 2 item 5: the overshoot's block lasts its predicted return,
            // the out-leg to its turn point, a U-turn of pi R and the rejoin to its apex at the lane speed, + kLaneStopMs + the step; past
            // it the pair is the certificate's and the law's stop is bounded by T_stop as for any non-block)
            const bool over_block = k && P->over && P->redep && P->over_ret_ms &&
                                    std::int32_t(now - P->over_ret_ms) <= std::int32_t(kLaneStopMs + g.step_ms);
            // (lane fix round 2 item 3d: a predecessor stopped at d_end on a slot refusal is the queue hold's block, which lasts the refusal;
            // its grant, order 5 or 8 seen, releases it at once, not once its speed passes kLaneStopV: the follower's stop ends within the step)
            cx.granted = P->sb == Sb::dock || P->sb == Sb::launch;
            cx.block = (sc::dot3(P->st.u, P->st.u) < kLaneStopV * kLaneStopV && !cx.granted) || over_block;
            cx.pred_final = P->sb == Sb::handover || P->lane_s > -(dend(*P) + lane_blend_settle(g, *P));
        }
        if (m.lane_pred != (P ? P->ship : 0u)) { // a new pair: a merge when the gap is short
            m.lane_pred = P ? P->ship : 0u;
            m.merged = !P || gap >= kLaneGapM - 50.0f ? 1 : 0;
            m.merge_done = m.merged;
            m.merge_ms = 0;
            if (!m.merged) {
                m.merge_ms = now ? now : 1;
                ++g.lane_merges;
                if (st) ++st->merges;
                m.law = Law::merge;
                lane_event(st, m, false, gap, 0);
            }
        }
        // a merge behind a ship that is itself still merging starts when that one settles (its speed is the floor until then; the
        // follower's 40 s bound of section 6.11 item 2 runs from there): the start is re-stamped meanwhile
        if (!m.merged && m.merge_ms && P && P->lane_rank != 0xff && !P->merged && P->merge_ms) m.merge_ms = now ? now : 1;
        if (!P) m.merge_done = 1;
        cx.merged = m.merged != 0;
        m.lane_free_ms = k == 0 ? t_free : 0u;
        cx.head = m.lane_free_ms != 0;
        cx.t_free_s = cx.head ? float(std::int32_t(m.lane_free_ms - now)) * 0.001f : 0.0f;
        m.lane_gate_ms = 0;
        if (m.role == Role::escort) { // the escort gate (section 6.5): the braking reserve; lane_gate_ms (the row) its leader's launch +
                                      // lambda_max + the step, the latest moment the script's order 4 is expected
            std::uint32_t t_l = now;
            const int li = find(g, m.leader);
            if (m.res_until) t_l = m.res_o8_ms + 900u;
            else if (li >= 0 && g.m[li].role == Role::committed) t_l = lane_launch_eta(g, g.m[li], now);
            m.lane_gate_ms = t_l + kLaneLambdaMaxMs + g.step_ms + kLaneCrawlSlackMs;
            if (!m.lane_gate_ms) m.lane_gate_ms = 1;
            cx.gate = true;
            cx.t_gate_s = float(std::int32_t(m.lane_gate_ms - now)) * 0.001f;
            cx.dt = float(g.step_ms > kStepClampMs ? kStepClampMs : g.step_ms) * 0.001f;
        }
        // lane fix round 1 item 4: the law's stop lasts at most T_stop + the step unless the ship ahead is physically stopped (a block); each
        // stop episode counts in lane_stops (ctl_merge_stop); an episode ends once its condition has been off for kLaneStopEndMs + the step
        // (the follow law flickers across the stop's threshold while it brakes)
        cx.stop_left_s = float(std::int32_t((m.lstop_ms ? m.lstop_ms : now) + kLaneStopMs + g.step_ms - now)) * 0.001f;
        Law law;
        bool merged, stop_cond;
        m.v_sb = lane_law(cx, &law, &merged, &stop_cond);
        if (stop_cond) {
            if (!m.lstop_ms) {
                m.lstop_ms = now ? now : 1;
                ++g.lane_stops;
            }
            m.lstop_last_ms = now ? now : 1;
        } else if (m.lstop_ms && std::int32_t(now - m.lstop_last_ms) > std::int32_t(kLaneStopEndMs + g.step_ms))
            m.lstop_ms = m.lstop_last_ms = 0;
        if (law == Law::lane && !m.lane_on) law = Law::join;
        m.law = law;
        m.lane_gap = gap;
        if (merged && !m.merged) m.merged = 1;
        if (!m.merge_done && m.merged && gap >= kLaneGapM - 50.0f) { // review item 6: the merge completes on the raw gap (the finish
                                                                       // row; the follow law settles a few m short of g_req at the cap)
            m.merge_done = 1;
            if (st) ++st->merges_done;
            lane_event(st, m, true, gap, m.merge_ms ? now - m.merge_ms : 0u);
        }
    }
    if (st) st->members = n;
}
// Site B's lane write on a member's frame (section 6.3-6.5; the DLL's clamp hook, the fixture's site B): a stop in force (the law's
// stop, or the brief stop's cap 0) writes 0 on the steered orders and order 0; on order 0 / 0x1f with the live nose f within 8 deg of F
// the lane speed stands (the script's re-issue gaps, the lost-leader wait, the hand-over; the refusal's brake bound). False: no write.
// (*kind, optional: the write's LaneWrite for the lane row)
enum class LaneWrite : std::uint8_t { none = 0, stop, kept, hold, unkept, count };
inline const char* lane_write_name(LaneWrite w) noexcept {
    static const char* const n[unsigned(LaneWrite::count)] = {"none", "stop", "kept", "hold", "unkept"};
    return unsigned(w) < unsigned(LaneWrite::count) ? n[unsigned(w)] : "?";
}
inline bool lane_site_b(const Gate& g, const Member& m, std::uint16_t order, const float x[3], const float f[3], float v_now, std::uint32_t now,
                        float* v, LaneWrite* kind = nullptr) noexcept {
    if (kind) *kind = LaneWrite::none;
    if (!m.ln || !(m.role == Role::committed || m.role == Role::escort)) return false;
    const bool steered = (m.role == Role::committed && order == sc::kOrderFlyBefore) ||
                         (m.role == Role::escort && (order == sc::kOrderEscort || order == sc::kOrderFollow));
    const bool lane = m.sb == Sb::inbound || m.sb == Sb::funnel;
    const float cap = cap_in_force(g, m, now);
    if (lane && (steered || order == sc::kOrderNone) && (m.law == Law::stop || (cap == 0.0f && m.stop_ms))) {
        *v = 0.0f;
        if (kind) *kind = LaneWrite::stop;
        return true;
    }
    if (order != sc::kOrderNone && order != sc::kOrderRotate) return false;
    if (m.role == Role::committed && m.handover_ms && (m.sb == Sb::handover || m.sb == Sb::funnel)) { // the hand-over after P2
        if (!(sc::dot3(f, g.F) >= kLaneHoldCos)) return false;
        if (m.p2_kept) { // P2 kept the speed: it stands (the refusal's brake bound)
            *v = m.refused ? lane_refused_hold(g, m, x, v_now) : v_now;
            if (kind) *kind = LaneWrite::kept;
            return true;
        }
        // lane fix round 1 item 3 (Run173: P2 did not keep the leader's speed, the nose 22 deg off F; 0x1f and order 0 left it at 12.2 m/s
        // and this branch held that for the whole 116 s refusal, a crawl): never the live speed of a hand-over P2 did not keep: the lane
        // speed under the refusal's brake bound, refused the brake to a stop at d_end
        if (m.refused) {
            const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
            const float dist = -dend(m) - sc::dot3(d, g.F), a = m.params.a_lin > 0.1f ? m.params.a_lin : 0.1f;
            *v = dist > kLaneStopShortM ? sc::sqrt_ss(2.0f * a * (dist - kLaneStopShortM)) : 0.0f;
        } else
            *v = lane_refused_hold(g, m, x, m.v_sb >= 0.0f ? m.v_sb : lane_vmax(m));
        if (kind) *kind = LaneWrite::unkept;
        return true;
    }
    if (!lane || !(m.v_sb >= 0.0f)) return false;
    if (m.role == Role::escort ? !(m.res_until || (m.steered_ms && now - m.steered_ms <= age_bound(g, kGapMs)))
                               : !(m.steered_ms && now - m.steered_ms <= age_bound(g, kGapMs)))
        return false;
    const LaneHold h = lane_hold_o0(g, m, x, f, now);
    if (h.v < 0.0f) return false;
    *v = h.v;
    if (kind) *kind = LaneWrite::hold;
    return true;
}
// Review item 2: the KC grid's 20 spheres (0.75 r_gate = 829 m around points 666 m apart, rows at 0 / -666 / -1332 / -2000 m U) cover
// U in [-2827, +829] m and R in [-2161, +2161] m at the grid depth 2 (r_gate + r_s): no lane lateral inside the disc clears them (the
// row gap is 666 m against a 1658 m sphere diameter), so an escort whose order 4 is issued at the grid depth may be inside its slot's
// sphere, and order 4's first tick (which tests the KC's point, before site A's rewrite: ship-navigation-ai.md section 10.12 item 2)
// would end the order there. The tick deferral: on an order-0 frame of a lane escort in its lost-leader wait (the reservation set: the
// next order is the script's order 4), site B adds this to f+0x210, the order's re-plan stamp that order 0's tail has just set to the
// engine's now (section 10.11 Q4; SA_FlyObjectBeforeContainerPos does not write it, section 10.12 item 2). The order-4 handler's first
// frame then skips its tick (now - f+0x210 < 0: the jump to site B) and site B rewrites f+0x240 to L_i (lane_site_b_point) before the
// first tick tests it. The deferral covers the next frame's step up to kLaneDeferMs plus the recent step (a SETA frame of 1.5-2 s in
// scenario C); a longer step lets the tick run on the issue frame (the old behaviour). The first tick of the order comes that much later
// (the ship coasts at site B's last write meanwhile). 0: no deferral.
constexpr std::uint32_t kLaneDeferMs = 2500;
inline std::uint32_t lane_defer_ms(const Gate& g, const Member& m, std::uint16_t order) noexcept {
    if (order != sc::kOrderNone || !m.ln || !m.q_ok || m.role != Role::escort || !m.res_until || !(m.sb == Sb::inbound || m.sb == Sb::funnel))
        return 0;
    return kLaneDeferMs + (g.step_ms < kStepClampMs ? g.step_ms : kStepClampMs);
}
// Site B's order-4 point rewrite (review item 2; site A's rule at the frame of a deferred tick; lane_defer_step): a lane member (an escort just given its
// order 4, or a committed member) on order 4 to this gate whose f+0x240 (p, world m) is one of the KC's 20 grid points for its radius
// gets L_i in *L. False: leave the field.
inline bool lane_site_b_point(const Gate& g, const Member& m, std::uint16_t order, std::uint32_t target, const float p[3], float L[3]) noexcept {
    if (order != sc::kOrderFlyBefore || target != g.gate || !m.ln || !m.q_ok || !(m.role == Role::escort || m.role == Role::committed)) return false;
    if (kc_grid_slot(g.G, g.R, g.U, g.F, g.r_gate, m.r, p) < 0) return false;
    lane_order_point(g, m, L);
    return true;
}
// Site B's tick deferral on one frame of member m (review item 2), given the live order, its target, f+0x210 as read (f210), the game
// clock now (the engine's [[0x606f34]+0x718], the clock order 0's tail, the handlers and site B read) and f+0x240 (p, world m): on
// order 0 with lane_defer_ms, f+0x210 := now + the deferral (write210), on every such frame; on the first frame of the next order while
// f+0x210 still holds that value, the order-4 point rewrite when it is a grid point (rewrite, L) and f+0x210 := now (the next frame's
// handler ticks on the rewritten point: one frame of delay, whatever the step). Final review item 2 (ship-navigation-ai.md section 10.13
// items 3 and 6): the base and the restore are the clock, not the stamp read: on an order-0 frame whose tail did not run (another order
// ended into order 0 for site B) f210 still holds the previous deferral, and adding to it compounded; a restore of the stored base put
// back a stamp up to a deferral in the future. A lost restore (the next order's first frame never reaches site B's lane write) leaves
// f+0x210 at most kLaneDeferMs + the step after the last order-0 frame: the order's first tick comes at most that late; a later frame
// that finds f+0x210 changed drops the deferral without a write. The DLL applies the writes; the fixture models f+0x210 with the plant's
// re-plan time.
struct LaneDefer {
    bool write210 = false, rewrite = false;
    std::uint32_t v210 = 0;
    float L[3] = {0, 0, 0};
};
inline LaneDefer lane_defer_step(const Gate& g, Member& m, std::uint16_t order, std::uint32_t target, std::uint32_t f210, std::uint32_t now,
                                 const float p[3]) noexcept {
    LaneDefer out;
    if (order == sc::kOrderNone) {
        const std::uint32_t d = lane_defer_ms(g, m, order);
        if (!d) {
            m.defer_w = 0;
            return out;
        }
        m.defer_base = now;
        m.defer_w = now + d ? now + d : 1u;
        out.write210 = true;
        out.v210 = m.defer_w;
        return out;
    }
    if (!m.defer_w) return out;
    if (f210 == m.defer_w) {
        out.rewrite = lane_site_b_point(g, m, order, target, p, out.L);
        out.write210 = true;
        out.v210 = now;
    }
    m.defer_w = 0;
    return out;
}
// The lane's leaf point and site B's speed for a member at x flying at v on `order` (site A; the DLL's decide() and the fixture's
// steer_call; lane_steer). False: the module's other rules (always with the key off).
struct SbSteer {
    float P[3] = {0, 0, 0};
    float v = -1.0f;
    Sb sb = Sb::none;
    bool keep_point = false; // lane fix round 2 item 2: the speed only (the order's own point stands: a handed escort's leg to its new leader)
};
// Lane stage 1 (section 6.1): the leaf point and site B's speed of a lane member (sb_steer's branch under the key): a committed member on
// order 4 or an escort on 0x14 / 0x15 gets the lane carrot (lane_carrot: the join toward Lambda, the merging yielder's own lateral while
// it is level with its predecessor, the re-departure's turn point) and its law's speed (0: the stop, written by lane_site_b); a committed
// member on order 5 Lambda_5's carrot through Q at the aligned ramp.
inline bool lane_steer(const Gate& g, const Member& m, const float x[3], float v_now, std::uint16_t order, std::uint32_t target,
                       SbSteer* out) noexcept {
    out->sb = m.sb;
    const float rg = g.r_gate;
    if (!(rg > 0.0f) || !m.q_ok) return false;
    // lane fix round 2 item 2: an escort handed to another leader flies the engine's leg to it (the order's own point) at its lane law's
    // speed while it leaves the lane (its follow law behind the ship ahead: no closing on the convoy it leaves; its rollout drifts at it)
    if (m.exit_ms && m.role == Role::escort && (order == sc::kOrderEscort || order == sc::kOrderFollow) && target == m.exit_tgt &&
        (m.sb == Sb::inbound || m.sb == Sb::funnel)) {
        out->keep_point = true;
        out->v = m.v_sb;
        return m.v_sb >= 0.0f;
    }
    if (order == sc::kOrderFlyBefore || order == sc::kOrderDock) {
        if (target != g.gate) return false;
    } else if (order == sc::kOrderEscort) {
        if (target != m.leader) return false;
    } else if (order == sc::kOrderFollow) {
        if (target != m.leader && !(m.res_until && target == g.gate)) return false;
    }
    const bool o4 = m.role == Role::committed && order == sc::kOrderFlyBefore;
    if ((o4 || (m.role == Role::escort && (order == sc::kOrderEscort || order == sc::kOrderFollow))) && (m.sb == Sb::inbound || m.sb == Sb::funnel)) {
        const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
        const float s = sc::dot3(d, g.F);
        float lat[2];
        lane_lat_at(g, m, s, lat);
        const float er = sc::dot3(d, g.R) - lat[0], eu = sc::dot3(d, g.U) - lat[1];
        const float y = sc::sqrt_ss(er * er + eu * eu);
        const bool hold_lat = !m.merged && m.lane_gap < kLaneGapM - 100.0f && y > kLaneOnM;
        const int pi = hold_lat && m.lane_pred ? find(g, m.lane_pred) : -1;
        float sc_c, yc;
        lane_carrot(s, y, v_now, m.params.W, rg, hold_lat, m.redep != 0, &sc_c, &yc, pi >= 0 ? g.m[pi].w + m.w + kLaneSideM : 0.0f,
                    lane_join_end(m), m.redep_out, m.redep_r, dend(m));
        float latc[2];
        lane_lat_at(g, m, sc_c, latc);
        const float k = y > 1.0f ? yc / y : 0.0f;
        if (m.redep && s <= -(dend(m) + m.redep_out)) // the turn: ahead (+F) and above (+U) of the ship, onto Lambda
            for (unsigned i = 0; i < 3; ++i) out->P[i] = x[i] + kLaneTurnM * (g.F[i] + g.U[i]);
        else {
            const float side = m.redep ? m.redep_side : 0.0f; // out: the turn point below Lambda
            for (unsigned i = 0; i < 3; ++i)
                out->P[i] = g.G[i] + (latc[0] + (m.redep ? 0.0f : k * er)) * g.R[i] + (latc[1] - side + (m.redep ? 0.0f : k * eu)) * g.U[i] + sc_c * g.F[i];
        }
        out->v = m.law == Law::stop ? 0.0f : m.v_sb >= 0.0f ? m.v_sb : lane_vmax(m);
        return sc::finite_f(out->P[0]) && sc::finite_f(out->P[1]) && sc::finite_f(out->P[2]);
    }
    if (m.role == Role::committed && order == sc::kOrderDock && (m.sb == Sb::dock || m.sb == Sb::handover || m.sb == Sb::funnel)) {
        float X[3];
        lambda5(g, m, X);
        line_carrot(X, g.F, 2.0f * rg, rg, m.params.W, x, v_now, out->P);
        const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
        out->v = dock_ramp(sc::sqrt_ss(sc::dot3(d, d)), rg, m.params.v_max);
        return sc::finite_f(out->P[0]) && sc::finite_f(out->P[1]) && sc::finite_f(out->P[2]) && out->v > 0.0f;
    }
    return false;
}
inline bool sb_steer(const Gate& g, const Member& m, const float x[3], float v_now, std::uint16_t order, std::uint32_t target,
                     SbSteer* out) noexcept {
    return g.lane ? lane_steer(g, m, x, v_now, order, target, out) : false; // the key off: the stage-A certificate only (no steering)
}
// P2 (the order-4 arrival at the grid sphere): the funnel's crossing is the kept hand-over's precondition (D.6: the funnel replaces
// the join mode of P2's verdict); the stage moves to the hand-over at the next frame from the live order.
// Lane stage 1: a lane member on its final (on Lambda, or still converging onto it: the keep verdict's nose and velocity tolerances
// decide) is the funnel's equivalent.
__attribute__((always_inline)) inline bool sb_funnel(const Member& m) noexcept {
    return m.role == Role::committed && (m.sb == Sb::funnel || (m.ln && m.sb == Sb::inbound));
}
// Lane stage 1 (section 6.4): P2's coast bound (stop before the inflated trigger on a refusal) is replaced for a lane member by site B's
// hold with the refusal's brake bound (lane_refused_hold): its speed bound is open, the nose and velocity tolerances stay.
__attribute__((always_inline)) inline float lane_p2_bound(const Member& m, float bound) noexcept {
    return m.role == Role::committed && m.ln && (m.sb == Sb::funnel || m.sb == Sb::inbound) ? 3.4e38f : bound;
}
// Whether the lane owns a committed member's gate legs (review F8): Q known and a lane stage. Otherwise the member keeps the stage-A /
// amendment-15 join (join on, P2 kept when aligned), never "join off + vanilla stop".
__attribute__((always_inline)) inline bool sb_managed(const Member& m) noexcept {
    if (m.role != Role::committed || !m.q_ok) return false;
    if (m.ln) return m.sb == Sb::inbound || m.sb == Sb::funnel || m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch; // the lane
    return m.sb == Sb::funnel || m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch;
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
