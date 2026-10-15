#pragma once
// The gate traffic controller (ship_gate_ctl_core.h, the umbrella): the board. Constants, the member and pair types, roles and Why,
// the board, gate and member structures, the membership bitmap, the capsule geometry, enter / leave / locate / acquire_gate, validity,
// the member fields from a steering decision (state_from, hull_from, commit_fill, escort_fill), the priority and the yield cap in force.
#include <cstdint>
#include "ship_steering_core.h"
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

// B.9 review (item 3): the yield cap in force at the state's read (set within the cap's age and not released; -1 none). In the order-0
// gap a decision whose rollout cleared on it keeps it in force (decide: re-stamped, never released there), so site B applies it when the
// script re-issues the steered order: the speed the gap's rollout resumes at is the speed site B will write.
// The age is taken at now, site B's clock (its test: now - cap_ms <= the cap's age); now 0: the state's read (the fixture's units).
__attribute__((always_inline)) inline float cap_in_force(const Gate& g, const Member& m, std::uint32_t now = 0) noexcept {
    const std::uint32_t t = now ? now : m.st.ms;
    return m.cap >= 0.0f && m.cap_ms && std::int32_t(t - m.cap_ms) <= std::int32_t(age_bound(g, kCapMaxAgeMs)) ? m.cap : -1.0f;
}
} // namespace x3m::ship_avoidance::ctl
