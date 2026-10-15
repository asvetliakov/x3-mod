#pragma once
// Internal to the ship steering module: the state and the inline helpers its translation units share (ship_steering_install.cpp,
// _decide.cpp, _clamp.cpp, _warn.cpp, _rows.cpp, ship_gate_ctl_glue.cpp, ship_dock_glue.cpp; the source layout table is in
// docs/architecture/ship-steering-and-avoidance.md). Split out of ship_steering.cpp on 2026-10-15 as pure moves: the state that was
// the file's anonymous namespace is the inline variables below (namespace x3m_ship_steering_rt, nominated by a using-directive as the
// anonymous namespace was); the inline helpers more than one unit calls stay inline here so that no call the single translation unit
// inlined becomes an out-of-line call. State and helpers one unit alone uses stay in that unit's anonymous namespace.
#include "ship_steering.h"
#include "ship_steering_core.h"
#include "ship_avoidance_core.h"
#include "ship_avoidance_walk_core.h"
#include "ship_gate_ctl_core.h"
#include "ship_gate_ctl_live_core.h"
#include "ship_steering_sites.h"
#include "config.h"
#include "cpu_state.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "capture.h"
#include "log_tiers.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>

// Four engine_patch claims (ship_steering_sites.h) with four stubs assembled
// into this DLL's .text (below) and four continuation slots in its .data: no
// stub byte is emitted into the arena and nothing writes into it (it is
// PAGE_EXECUTE_READ outside emission; 2026-10-11). The handlers run on the
// engine's main-loop thread inside the command pass under the light CPU
// envelope (MXCSR + LastError; the x87 stack is the leaf caller's and nothing
// here executes an x87 instruction: check_no_x87.py walks the stubs and both
// handlers). Hot path: no log call, no allocation, no lock; counters are
// relaxed stores from the one writing thread; the frame boundary formats.
static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace core = x3m::ship_steering::core;
namespace sites = x3m::ship_steering::sites;

extern "C" {
// Continuation slots (jmp [slot] in the stubs): the claim tails, written before the stub is reachable.
extern void* x3m_ship_steering_next_a0;
extern void* x3m_ship_steering_next_a1;
extern void* x3m_ship_steering_next_a2;
extern void* x3m_ship_steering_next_b;
void x3m_ship_steering_a0_stub();
void x3m_ship_steering_a1_stub();
void x3m_ship_steering_a2_stub();
void x3m_ship_steering_b_stub();
int __cdecl x3m_ship_steering_enter(sites::Frame* frame, int leaf);
void __cdecl x3m_ship_steering_clamp(std::uint32_t obj);
// Site E (step 2): the warn test's entry; the stub answers a resolved pair with EAX = 0, `ret 0x10`.
extern void* x3m_ship_avoid_next_e;
void x3m_ship_avoid_e_stub();
int __cdecl x3m_ship_avoid_suppress(std::uint32_t a, std::uint32_t b);
// Step 3 (gate approach): the three gate sites' continuation slots (the claim tails: pass-through), the resume
// addresses the live stubs jump to (absolute engine addresses, constants: the EXE is not relocatable), the live
// byte the stubs test first, the stubs and their handlers.
extern void* x3m_ship_gate_next_a;
extern void* x3m_ship_gate_next_b;
extern void* x3m_ship_gate_next_l;
extern void* x3m_ship_gate_resume_a;
extern void* x3m_ship_gate_resume_b;
extern void* x3m_ship_gate_resume_l;
extern volatile unsigned char x3m_ship_gates_live;
void x3m_ship_gate_p2_stub();
void x3m_ship_gate_p4_stub();
void x3m_ship_gate_p5_stub();
void __cdecl x3m_ship_gate_stop_a(std::uint32_t obj);
void __cdecl x3m_ship_gate_stop_b(std::uint32_t obj);
std::int32_t __cdecl x3m_ship_gate_launch(std::uint32_t obj, std::int32_t t_ms);
// The gate traffic controller, stage A: the launch window 0x00444606 (ship_steering_sites.h (n)); the stub continues into the claim
// tail whether or not the handler offset the exit point.
extern void* x3m_ship_gate_next_t;
extern volatile unsigned char x3m_ship_traffic_live;
void x3m_ship_gate_arrival_stub();
void __cdecl x3m_ship_gate_arrival(std::uint32_t obj, std::uint32_t gate);
}

namespace x3m_ship_steering_rt {

// ---- module state -------------------------------------------------------
inline std::atomic<bool> installed_{false};
inline bool initialized_ = false, timing_ = false, debug_ = false, perf_ = false;
inline std::uint32_t class_mask_ = 0;
inline std::uint64_t qpc_frequency_ = 0;
inline core::ShipTable table_; // 64 KB of .bss, the per-ship steering state
namespace av = x3m::ship_avoidance::core;
inline bool avoidance_on_ = false;
inline std::atomic<bool> warn_live_{false}; // site E chained
inline const char* warn_state_ = "not_claimed";
inline av::Snapshot snaps_[sites::kSnapshotSlots];     // per-sector per-frame obstacle snapshots (fixed capacity)
namespace wk = x3m::ship_avoidance::walk;
namespace ln = x3m::ship_avoidance::lane;
inline wk::PartCache part_cache_;                       // station part models, keyed on object, root radius and position/basis
inline av::ShipAvoid avoid_[core::kShipCapacity];      // parallel to table_.entries (same slot index)
inline std::uint32_t table_id_[core::kShipCapacity];   // B.4: per table slot, [obj+0x08] at the last stamp (a recycled pointer is a new ship)
// --debug witnesses: detour events and one census per sector (first snapshot), drained at the frame boundary.
struct DetourEvent {
    std::uint32_t obj, obstacle, kind, side, vertical, order, obstacle_leader, obstacle_order;
    float t_cpa, miss, C, L, P[3], x[3];
};
inline DetourEvent detours_[16];
inline std::atomic<std::uint32_t> detour_write_{0};
inline std::uint32_t detour_read_ = 0;
using Census = wk::Census;
inline Census census_[4];
inline std::atomic<std::uint32_t> census_write_{0};
inline std::uint32_t census_read_ = 0, census_logged_sectors_[8] = {};
// Step 3: the gate approach (X3M_SHIP_STEERING_GATES, default on with the steering): the three sites as a group.
inline bool gates_on_ = false;
inline std::atomic<bool> gates_live_{false};
inline const char* gates_state_ = "not_claimed";
// Amendment 13 (capital-gate-transit.md): X3M_SHIP_STEERING_GATES_CAPITAL (default on, needs the gate group live): the capital
// lead-in toward its validated grid point, the speed write in both directions inside the window, and P2/P4/P5 keeping the speed
// of an aligned capital. Off = the amendment-10 capital path (vanilla steps). The pair policy and site E's small-pair branch
// are part of X3M_SHIP_AVOIDANCE and do not depend on this key.
inline bool gates_capital_on_ = false;
inline const char* gates_capital_state_ = "off";
struct GateState { // per table slot: the approach's last call, the crossing (set at P2: the engine's arrival test runs before
                   // the frame's leaf call, so a per-call distance test never sees it: Run152), the spread offset, the
                   // speed kept at the pass point, and the order 4 -> 5 transition diagnostic
    std::uint32_t obj, cross_stamp, v0_stamp, launch_stamp;
    std::int32_t v0_units;
    float cross_v, cross_err_deg, off[2]; // the spread offset in the gate's x-y plane, m (rows 0 and 1)
    float last_dA_r, last_err_deg, r_gate, A_local[3]; // the last order-4 call: dist(A)/r, heading error to h; A in the gate frame / r
    float G[3], rows[3][3];   // the gate frame (m, unit rows) for the diagnostic rows
    float carrot_local[3];    // the last lead-in carrot in the gate frame / r
    float p2_pos[3], p2_fwd[3], p2_dA_r; // at P2: position in the gate frame / r, the forward vector in gate rows, dist(A) / r
    std::uint32_t p2_stamp;   // game ms at P2
    std::uint32_t carrot_ms;  // game ms of the last lead-in call that wrote carrot_local (0 = none; amendment 11)
    std::uint32_t gate;       // the target the approach is to: another target or leaving its 2 r sphere starts a new approach
    std::uint8_t crossed, launched, capital, spread_k;
    std::uint8_t lead_counted; // the approach's verdict was counted at its first P2
    std::uint8_t validated;   // per approach, recomputed on every order-4 call: 0 none, 1 A matched the pinned formula (the
                              // lead-in; P2/P4 keep the speed), 2 fallback (the vanilla point and the vanilla stops)
    std::uint8_t pending;     // an order-4 call was seen; the next order-5 call logs the transition
    std::uint8_t dock_blocked; // order 5: the chord arc would sweep the ring; the engine's value stands
    std::uint8_t kept;        // a capital: P2 kept the speed (validated approach, crossing within the 0x1f tolerance; amendment 13)
    // Amendment 15 (capital-gate-transit.md section 15.3): the capital join (mode, the predictor's speed and start state), the
    // hand-over after a kept P2 (site E's gate row until order 5), the grid slot and the P2 coast bound's inputs
    std::uint8_t handover;    // P2 kept the speed: the gate row covers orders 0 / 0x1f until order 5 (keyed on the gate state, audit G3)
    std::uint8_t grid_slot;   // the KC grid slot 0..19 of the validated point (255 = none)
    std::uint8_t join_registered; // in join_reg_ (the frame-end predictor's registry)
    std::uint8_t join_logged_mode; // the mode of the last ship_gate_capital_join row
    core::JoinState join;
    float xg_m, r_ship, a_lin; // |X - G|, the ship's radius and acceleration at the last decision (P2's coast bound)
    std::uint32_t join_row_ms; // the last 1 Hz join row (game ms)
    std::uint32_t lead_ms;     // audit G1: a non-capital's last order-4 call with the lead-in inside its window (game ms; 0 = none)
    float join_e, join_carrot; // the last decision's lateral offset and carrot distance (m), for the row
    std::uint32_t join_code;   // the record code at the last decision (the frame-end row never dereferences the ship)
    // Amendment 16 (section 16): the native id [obj+0x08] at the approach's first decision (E's gate row and the lane entry key on
    // it), P2 seen kept or not (the extended hand-over; cleared like `handover`), the hull half-width cached per ship with the
    // radius bits, and the follow cap of the last decision
    std::uint32_t id;
    std::uint8_t p2_seen, w_src, w_tries, pad16; // w_tries: the width reads retried after a failure (review F4)
    std::int32_t w_gen;        // the radius bits [obj+0xa4] the width was read with
    std::uint32_t w_root;      // and the root node [obj+0x70] (a regenerated model re-walks)
    float w;                   // the lateral half-width, m
    // review F4 and amendment 16b: the last width read (a failure is retried every 5 s, at most 3 times), the along-axis half-extent
    // max |o_z| + e_z of that read (the census's l_m)
    std::uint32_t w_read_ms;
    float w_len;
    // stage B (D.5): the small ship's last f+0x240 rewrite (units), so a field still holding it is left alone and a KC re-issue
    // (a new theta) is rewritten again
    x3m::ship_avoidance::ctl::SmallBWrite b_w;
    // lane stage 1 (convoy-gate-transit.md section 6.4): a capital's last f+0x240 rewrite (units) and the KC grid point it replaced (m):
    // a field still holding the write is ours, and the order's grid geometry is the replaced point's
    x3m::ship_avoidance::ctl::SmallBWrite lane_w;
    float lane_grid[3];
    // review item 4: the last order-4 field a committed lane member held that was neither a KC grid point nor ours (units): a grid miss
    // is counted and rowed once per distinct field value
    x3m::ship_avoidance::ctl::SmallBWrite lane_miss;
    // final review item 4: the last order-4 field of this ship with no lane member found not to be its lane point (gc::lane_adopt_memo)
    x3m::ship_avoidance::ctl::AdoptMemo lane_noadopt;
    // convoy-gate-transit.md section 12.2 (round B): a small ship's bounded hold on this approach and this decision's speed cap from it
    // (m/s; < 0 none), applied with the existing small-ship clamp (site B's gate / order-5 write)
    x3m::ship_avoidance::ctl::SmallHold small_hold;
    float small_cap;
    std::int8_t small_side; // section 12.2: the side lead-in's side kept for this approach (0 none, +-1 along R)
};
struct GateTransition { // --debug: one row per order 4 -> 0 -> 5 transition at a gate (<= 8 per boundary)
    std::uint32_t obj, code, ms_since_p2;
    float dA_r, err_B_deg, v_ms, desired_ms, B_local[3], A_local[3];
    float p2_pos[3], p2_fwd[3], p2_carrot[3], o5_pos[3], o5_fwd[3], o5_dA_r, o5_dB_r; // amendment 10: the crossing and the order-5 start
    std::int32_t carrot_age_ms; // order-5 start minus the last lead-in call (-1: no lead-in call on this slot; amendment 11)
    std::uint8_t validated, crossed, capital, dock_blocked;
};
inline GateTransition gate_trans_[8];
inline std::atomic<std::uint32_t> gate_trans_write_{0};
inline std::uint32_t gate_trans_read_ = 0;
inline GateState gate_[core::kShipCapacity];
struct GateEvent { // --debug: one row per launch (the crossing data and the time from the crossing to the warp), or per
                   // capital crossing (the alignment at the grid point)
    std::uint32_t obj, code, cross_to_launch_ms;
    std::int32_t v0_units;
    float cross_v, cross_err_deg, off[2];
    std::uint8_t capital, spread_k;
};
inline GateEvent gate_events_[8];
inline std::atomic<std::uint32_t> gate_event_write_{0};
inline std::uint32_t gate_event_read_ = 0;
// Amendment 15: the formation membership memory (section 15.2; behind ship_avoidance), filled by the snapshot walk.
inline av::Members members_;
inline av::MemberStats member_stats_;     // totals since install (engine thread; read at the frame boundary)
// Amendment 15: the frame-end predictor's registry of capitals in join mode (core::JoinRegistry: table slot indices, at most 8,
// swept before each evaluation) and its cost.
inline core::JoinRegistry join_reg_;
// --debug rows (amendment 15, section 15.4), written to fixed rings in the handlers and formatted at the frame boundary.
struct JoinRow { // ship_gate_capital_join
    std::uint32_t obj, code, ms_in_window;
    std::uint8_t slot, mode, reason, crossing, kept, why_not, latched, pad;
    float s, e, nose, vel, rate_dps, v, v_cmd, carrot, pred_nose, pred_e;
    float cross_nose, cross_vel, cross_v;
};
inline JoinRow join_rows_[8];
inline std::atomic<std::uint32_t> join_row_write_{0};
inline std::uint32_t join_row_read_ = 0;
struct EVerdictRow { // ship_avoid_e_verdict (trace slot 0 as A)
    std::uint32_t a, b, b_class, b_code, b_leader, b_flags, member_leader;
    std::int32_t member_age_ms, a_age_ms, b_age_ms;
    std::uint16_t a_order, b_order;
    float d, r_b;
    std::uint8_t verdict;
    std::uint8_t gate_row;    // B.3 item 6: gc::GateRow of the own-gate row for a member A (0xff: not evaluated)
    std::int32_t gate_age_ms; // ... A's member state age then (-1: none)
    std::uint32_t step_ms;    // ... the frame's game-time step ([root+0x714], unclamped)
    std::uint8_t obst_row;    // lane fix round 2 item 1: gc::ObstRow of the lane obstacle row (0xff: not evaluated)
};
// amendment 17 (section 17 item 5) with review F4: E rows for every capital A as well: at most kERows rows per frame, one per pair
// per game second through a kELogged pair table; a new pair finding the table full (kELogged pairs logged within the last second) or
// the frame's rows written is dropped and counted (gate_rows_dropped, rows_dropped e_table / e_ring); 64 last verdicts for the
// warn_pre rows. The gate traffic controller (D.7): 32 rows per frame and a 128-pair table (Run160b: the ring sat at its cap of 8 in
// 401 of 2,829 crowd frames); member pairs write no per-call row (the ship_gate_ctl_pair row on a verdict change instead).
// (the arrival round item 6: Run174 dropped 34 E rows on the ring's cap of 32 rows per frame: 64)
constexpr unsigned kERows = 64, kELogged = 128, kELast = 64;
inline EVerdictRow e_rows_[kERows];
inline std::atomic<std::uint32_t> e_row_write_{0};
inline std::uint32_t e_row_read_ = 0;
struct WarnPreRow { // ship_avoid_engine_warn_pre
    std::uint32_t obj, order, tgt, count;
    std::uint32_t other[5], cls[5], code[5], o_order[5], leader[5], member[5];
    float d[5], r[5];
    std::uint8_t verdict[5];
    std::uint8_t gate_row; // lane fix round 1 item 5: the own-gate row's reason of the first gate entry's last verdict (0xff none)
    std::uint8_t obst_row; // lane fix round 2 item 1: the lane obstacle row's reason of the first other entry's last verdict (0xff none)
};
inline WarnPreRow warn_pre_[8];
inline std::atomic<std::uint32_t> warn_pre_write_{0};
inline std::uint32_t warn_pre_read_ = 0;
struct RingReadRow { // review of the arrival round item 1 (--debug): ship_gate_ring_read, the gate's collision-tree read, once per gate
    std::uint32_t t_ms, gate, id, scene;
    std::uint16_t type, nodes;
    std::uint8_t state, why, seen, board;
};
inline RingReadRow ring_read_rows_[8];
inline std::atomic<std::uint32_t> ring_read_write_{0};
inline std::uint32_t ring_read_read_ = 0;
using EV = av::EVerdict; // site E's verdicts (ship_avoidance_core.h site_e_verdict), the --debug rows
struct WidthCensus { // ship_capital_census: one row per capital type (the record index [obj+0x4a])
    std::uint32_t type, code;
    float r, w, l;           // l: max over the parts of |o_z| + e_z (amendment 16b: h = sqrt(r^2 + w^2) must cover it)
    std::uint8_t w_src, retries;
    // amendment 17 (section 17 item 5): the widest part (body id, lateral offset bound and half-extents in m, its widest LOD record)
    std::int32_t part_id, part_lod;
    float o_x, o_y, e_x, e_y;
};
inline WidthCensus width_census_[8];
inline std::atomic<std::uint32_t> width_census_write_{0};
inline std::uint32_t width_census_read_ = 0;


// Counters: written by the engine thread with plain relaxed stores (one
// writer), read at the frame boundary.
struct Counter {
    std::atomic<std::uint32_t> v{0};
    void add(std::uint32_t n = 1) noexcept { v.store(v.load(std::memory_order_relaxed) + n, std::memory_order_relaxed); }
    std::uint32_t get() const noexcept { return v.load(std::memory_order_relaxed); }
};
inline Counter calls_, handled_, reasons_[core::kReasonCount], leaf_calls_[3], clamp_calls_, clamp_applied_, clamp_stale_;
inline Counter handled_by_order_[0x40], redirected_;
inline Counter av_calls_, av_conflicts_, av_detours_, av_unresolved_, av_overflow_, av_snapshots_, av_walk_objects_, av_emergency_;
inline Counter av_unresolved_why_[unsigned(av::Unresolved::count)], av_unresolved_episodes_, av_conflict_episodes_, part_dirty_, part_scaled_;
inline Counter dock_parts_evicted_; // amendment 18 (review 6): a valid part model found every cache slot pinned by the current snapshot
inline Counter e_calls_, e_suppressed_, part_built_, part_fallback_, part_invalid_;
inline Counter e_suppressed_small_, e_small_pass_, e_small_pass_reason_[unsigned(av::SmallPair::count)]; // site E's small-ship row (amendment 15)
inline Counter e_suppressed_member_, e_suppressed_gate_, e_suppressed_gate_small_, avoid_excluded_member_; // amendment 15, audit G1
inline Counter gate_capital_kept_, gate_capital_lead_validated_, gate_capital_lead_fallback_;
inline Counter gate_capital_join_, gate_capital_slowed_, gate_capital_fallback_pred_, gate_capital_fallback_past_x_;
inline Counter gate_capital_not_kept_[unsigned(core::JoinKeep::count)];
inline Counter gate_capital_detour_write_; // amendment 16 fix: decisions of a join on a detour that kept the join's speed write
inline Counter gate_lead_, gate_cross_, gate_p2_, gate_stops_skipped_, gate_p4_, gate_v0_kept_, gate_p5_, gate_launch_continued_, gate_speed_writes_;
inline Counter gate_spread_offsets_, gate_capital_aligned_;
inline Counter gate_lead_validated_, gate_lead_fallback_, gate_lead_window_rejected_, gate_dock_writes_, gate_dock_blocked_;
inline Counter gate_p2_no_gate_target_, gate_p2_capital_, gate_p2_no_state_, gate_p4_no_gate_target_, gate_p4_capital_, gate_p4_no_state_;
inline Counter gate_dock_invalid_, gate_rows_dropped_, avoid_excluded_followers_, avoid_skipped_small_;
// The gate traffic controller's ring changes (gate-traffic-controller.md D.7): gate_rows_dropped stays the sum; each source counts
// its own drops (the summary's rows_dropped field, in this order).
enum class Drop : unsigned { gate_event = 0, join, gate_trans, warn_pre, engine_warn, warn_watch, e_table, e_ring, ctl, ctl_pair, launch, count };
constexpr const char* kDropNames[unsigned(Drop::count)] = {"gate_event", "join", "gate_trans", "warn_pre", "engine_warn", "warn_watch",
                                                           "e_table", "e_ring", "ctl", "ctl_pair", "launch"};
inline Counter rows_dropped_[unsigned(Drop::count)];
inline void row_dropped(Drop d) noexcept {
    gate_rows_dropped_.add();
    rows_dropped_[unsigned(d)].add();
}
// Step 4 item 7: ship_trace (--debug tier, X3M_SHIP_TRACE=1, X3M_SHIP_TRACE_MAX ships): the module follows the first N
// non-capital class-7 autopilot ships that enter a gate approach (order 4 with a class-0x12 target) or a dock approach
// (order 4 with a class-5/6 target, or order 5) and writes one row per frame for each from a ring drained at the frame
// boundary: the engine's state the fixture's model needs as input (position, basis rows, rates, speeds, the point the
// engine gave the leaf, the order, dt) and what the module handed back (the point, the written remainders); the next
// row of the same ship is the oracle. A slot is freed when the ship launches (P5), docks, or has not been seen for 5 s.
constexpr unsigned kTraceMax = 16, kTraceRing = 128;
constexpr std::uint32_t kTraceIdleMs = 5000;
inline bool trace_on_ = false;
inline unsigned trace_max_ = 8; // amendment 17 (section 17 item 5): the default; kTraceMax slots
struct TraceSlot {
    std::uint32_t obj, last_seen_ms;
    std::int32_t W[3], rot_raw, rot_pct, vmax, accel; // the dynamics the leaf call read (the last known)
    std::int32_t point[3], given[3], e[3];           // this frame's leaf call: ours, the engine's, the written remainders
    std::uint32_t obstacle;                          // the current detour's obstacle at the last leaf call (0 = none)
    float omega_cmd[3];                              // the commanded rate per axis at the last leaf call, deg/s
    std::uint8_t leaf_this_frame, emergency;         // emergency: the avoidance braking ramp was active at the last leaf call
    std::uint16_t last_order;                        // the order at the previous row (the first order-0x12 row logs the engine's list)
    std::uint8_t escort;                             // amendment 15: claimed as an escort of slot 0's capital (kept on 0x14/0x15/0x12/0)
    std::uint8_t member;                             // B.4: claimed as a board member (released when it leaves the board)
    std::uint8_t warn_pre_last;                      // the previous row saw the KC's warn bits (the warn_pre row is edge-triggered)
};
inline TraceSlot trace_slots_[kTraceMax];
// Amendment 17 (section 17 item 5) with review F3: the warn edge memory of untraced capital subjects. A slot holds a capital while
// its warn bits are set or it is on 0x12; it is freed when the ship is seen without either, when a trace slot claims the ship, or
// (docked, dead, no longer reaching site B) kCapWarnExpiryMs after its last hit, by the frame-boundary sweep or in place by the next
// capital needing a slot. A capital that finds none is not logged and counts gate_rows_dropped.
struct CapWarnSlot {
    std::uint32_t obj, seen_ms;
    std::uint8_t warn_last, avoid_last, pad[2];
};
constexpr unsigned kCapWarnSlots = 32; // the gate traffic controller (D.7): 16 -> 32
constexpr std::uint32_t kCapWarnExpiryMs = 20000;
inline CapWarnSlot cap_warn_[kCapWarnSlots];
inline unsigned cap_warn_used_ = 0; // slots holding a capital: with none, a ship neither warned nor on 0x12 costs two reads
inline void cap_warn_free(unsigned k) noexcept {
    if (!cap_warn_[k].obj) return;
    cap_warn_[k] = CapWarnSlot{};
    --cap_warn_used_;
}
inline void cap_warn_release(std::uint32_t obj) noexcept {
    if (!cap_warn_used_) return;
    for (unsigned k = 0; k < kCapWarnSlots; ++k)
        if (cap_warn_[k].obj == obj) cap_warn_free(k);
}
// Amendment 13 (--debug, ship_trace): at a followed ship's first order-0x12 row, the head of the engine's own obstacle list
// (f+0x1e0 count, f+0x1e4[0], f+0x1f8[0]: the object the warn named and its centre distance), so a remaining avoid leg names its pair.
struct EngineWarn {
    std::uint32_t obj, other, count, cls, code;
    float r, d;
};
inline EngineWarn engine_warns_[8];
inline std::atomic<std::uint32_t> engine_warn_write_{0};
inline std::uint32_t engine_warn_read_ = 0;
struct TraceRow {
    std::uint32_t obj, order, tgt, dt_ms, t_ms, obstacle;
    std::uint16_t code, tgt_class;
    std::int32_t pos[3], rate[3], v, des, des_engine, point[3], given[3], e[3], W[3], rot_raw, rot_pct, vmax, accel;
    std::int32_t slip[3]; // amendment 14: obj+0x60..+0x68 at site B (units/s), the integrator's slip the law reads
    float fwd[3], up[3], omega_cmd[3];
    std::uint8_t has_e, emergency;
    float cap;             // Run161 item 4: the traffic controller's yield cap on this ship (m/s, < 0 none) and its age (ms, -1 none)
    std::int32_t cap_age_ms;
};
inline TraceRow trace_ring_[kTraceRing];
// review F7: every ring and table indexed with a mask is a power of two
constexpr bool pow2(unsigned n) noexcept { return n && !(n & (n - 1)); }
static_assert(pow2(kTraceRing) && pow2(kERows) && pow2(kELogged) && pow2(kELast), "masked rings and tables are powers of two");
static_assert(pow2(sizeof join_rows_ / sizeof join_rows_[0]) && pow2(sizeof warn_pre_ / sizeof warn_pre_[0]) &&
                  pow2(sizeof width_census_ / sizeof width_census_[0]) && pow2(sizeof engine_warns_ / sizeof engine_warns_[0]),
              "masked rings and tables are powers of two");
inline std::atomic<std::uint32_t> trace_write_{0};
inline std::uint32_t trace_read_ = 0;
inline Counter trace_rows_, trace_dropped_;
inline TraceSlot* trace_slot_of(std::uint32_t obj) noexcept {
    for (unsigned i = 0; i < trace_max_ && i < kTraceMax; ++i)
        if (trace_slots_[i].obj == obj) return &trace_slots_[i];
    return nullptr;
}
// Slot 0 is reserved for a capital on an order-4 gate approach (amendment 11: the Run155 Boreas circled for minutes
// with no per-frame rows); the other trace_max_ - 1 slots follow non-capital gate/dock approaches.
// Amendment 15 (section 15.4): an escort of slot 0's capital (any class, on 0x14/0x15 to it) takes one of the other slots, so the
// escort side of the cascade gets its warn_pre rows too.
inline bool traffic_on_ = false; // the traffic controller live (set at install; defined here for trace_claim, section below)
inline bool lane_on_ = false;    // lane stage 1 (ship_steering_gates_lane, convoy-gate-transit.md): the departure lane (off: the stage-A certificate only)
inline const char* lane_state_ = "off";
inline TraceSlot* trace_claim(std::uint32_t obj, std::uint32_t now, bool capital, bool escort = false, bool member = false) noexcept {
    // Run161 item 4 (under ship_steering_gates_traffic): a capital (an escort too) takes any free slot, slot 0 first; a non-capital
    // slots 1..; with the key off amendment 17's rule (slot 0 for one non-escort capital). B.4 item 2: under the key the last two
    // slots are a board member's (gc::trace_slot_range), searched first by a member
    const unsigned max = trace_max_ < kTraceMax ? trace_max_ : kTraceMax;
    const x3m::ship_avoidance::ctl::TraceRange r = x3m::ship_avoidance::ctl::trace_slot_range(max, traffic_on_, capital, escort, member);
    for (unsigned k = 0, n = (r.res_end - r.res_first) + (r.end > r.first ? r.end - r.first : 0u); k < n; ++k) {
        const unsigned i = k < r.res_end - r.res_first ? r.res_first + k : r.first + (k - (r.res_end - r.res_first));
        if (!trace_slots_[i].obj) {
            trace_slots_[i] = TraceSlot{};
            trace_slots_[i].obj = obj;
            trace_slots_[i].last_seen_ms = now;
            trace_slots_[i].escort = escort ? 1 : 0;
            trace_slots_[i].member = member ? 1 : 0;
            cap_warn_release(obj); // review F3: the trace slot takes over its warn rows
            return &trace_slots_[i];
        }
    }
    return nullptr;
}
inline std::atomic<std::uint32_t> qpc_hist_[32];
inline std::atomic<std::uint32_t> qpc_max_{0};
// The sampled decision (--debug): a seqlock written by the handler when requested.
struct Sample {
    std::uint32_t obj, order, code, leaf, flags;
    float theta_deg, n_y, n_p, omega_dps, d, v, v_cmd, v_corner, d_arr, v_ref;
    float lag_deg, aim_cos, v_slip, slip[3]; // amendment 14: the velocity's lag and the lead (cosines), the closing-speed correction, obj+0x60 (m/s)
    std::int32_t e[3], w[3], rot_accel_raw, rot_pct;
    float P[3], x[3], W_dps[3], k, a_dps[3], omega_axis_dps[3];
    bool held;
};
inline Sample sample_;
inline std::atomic<std::uint32_t> sample_seq_{0};
inline std::atomic<bool> sample_request_{false};
// ship_steering_stuck witnesses (always tier, first 8 per session).
struct Stuck {
    std::uint32_t obj, order;
    float d, best_d;
};
inline Stuck stuck_[8];
inline std::atomic<std::uint32_t> stuck_count_{0};

// ---- engine reads -------------------------------------------------------
inline std::uint32_t rd32(std::uintptr_t at) noexcept {
    return *reinterpret_cast<const volatile std::uint32_t*>(at);
}
inline std::int32_t rds32(std::uintptr_t at) noexcept {
    return std::int32_t(rd32(at));
}
inline std::uint16_t rd16(std::uintptr_t at) noexcept {
    return *reinterpret_cast<const volatile std::uint16_t*>(at);
}
inline void wr32(std::uintptr_t at, std::int32_t v) noexcept {
    *reinterpret_cast<volatile std::int32_t*>(at) = v;
}
inline std::uint32_t game_time_ms() noexcept {
    const std::uint32_t clock = rd32(sites::kGameClock);
    return clock ? rd32(clock + sites::kGameTimeOffset) : 0;
}
inline std::uint32_t frame_step_ms() noexcept { // B.3: the real step clamped to 5 s (it returned 0 above 1 s: av::frame_step_clamp)
    const std::uint32_t clock = rd32(sites::kGameClock);
    return av::frame_step_clamp(clock ? rd32(clock + sites::kGameTimeOffset - 4) : 0); // [root+0x714], the frame's game-time step
}
// The engine getters (conventions pinned in ship_steering_sites.h).
inline std::int32_t call_eax(std::uintptr_t fn, std::uint32_t obj) noexcept {
    std::uint32_t v = obj;
    asm volatile("call *%1" : "+a"(v) : "r"(fn) : "ecx", "edx", "memory", "cc");
    return std::int32_t(v);
}
inline std::int32_t call_ecx(std::uintptr_t fn, std::uint32_t obj) noexcept {
    std::uint32_t v = obj;
    std::int32_t r;
    asm volatile("call *%2" : "=a"(r), "+c"(v) : "r"(fn) : "edx", "memory", "cc");
    return r;
}
inline std::uint64_t qpc() noexcept {
    LARGE_INTEGER v{};
    return QueryPerformanceCounter(&v) ? std::uint64_t(v.QuadPart) : 0;
}
// The light envelope plus the default MXCSR control bits for the SSE
// arithmetic (the game runs x87 code; an unusual MXCSR would only come from
// another injected module). LightCallBoundary restores the original bits.
struct Envelope {
    x3m::LightCallBoundary boundary;
    Envelope() noexcept {
        std::uint32_t mx = 0;
        asm volatile("stmxcsr %0" : "=m"(mx));
        if ((mx & 0xffc0u) != 0x1f80u) {
            const std::uint32_t d = 0x1f80u;
            asm volatile("ldmxcsr %0" ::"m"(d));
        }
    }
};
inline float sc_max(float a, float b) noexcept {
    return a > b ? a : b;
}
inline float fixed16(std::int32_t v) noexcept {
    return float(v) * (1.0f / 65536.0f);
}
// The plant of a ship from its engine getters and its type record (record = its row of the ship type table, code = its class code;
// rot_pct = record+0xd0 > 0 and fl = its flight object, else k = 0): the steering decision's and the launch hook's (review of rounds A-C
// item 1: an arrival member's JoinParams from the same read as a commit's).
inline void read_dynamics(std::uint32_t obj, std::uint32_t fl, std::int32_t rot_pct, std::uint32_t code, core::Dynamics& dyn) noexcept {
    dyn.W[0] = float(call_eax(sites::kGetYawCap, obj)) * core::kRadPerBinary;
    dyn.W[1] = float(call_eax(sites::kGetPitchCap, obj)) * core::kRadPerBinary;
    dyn.W[2] = float(call_eax(sites::kGetRollCap, obj)) * core::kRadPerBinary;
    for (float& w : dyn.W)
        if (w < 0.0f) w = 0.0f;
    dyn.k = fl && rot_pct > 0 ? fixed16(rds32(fl + sites::kFlightRotAccel)) * float(rot_pct) * 0.01f : 0.0f;
    dyn.v_max = float(call_eax(sites::kGetMaxSpeed, obj)) / core::kUnitsPerMetre;
    dyn.a_lin = float(call_ecx(sites::kGetAccel, obj)) / core::kUnitsPerMetre;
    if (dyn.a_lin < 0.0f) dyn.a_lin = 0.0f;
    dyn.r_own = float(rds32(obj + sites::kObjRadius)) / core::kUnitsPerMetre;
    dyn.large = code < core::kClassCount && ((core::kLargeMask >> code) & 1u);
}
// ---- step 2: the sector snapshot and the station part model ------------------
// The walk itself is the portable ship_avoidance_walk_core.h (the fixture runs it over a synthetic sector); here it
// reads the engine's memory directly. Census rows (--debug): at most one per sector per session.
struct RawMem {
    std::uint32_t u32(std::uint32_t at) const noexcept { return rd32(at); }
    std::int32_t s32(std::uint32_t at) const noexcept { return rds32(at); }
    std::uint16_t u16(std::uint32_t at) const noexcept { return rd16(at); }
};
void gate_event(const GateState& g, std::uint32_t obj, std::int32_t v0, std::uint32_t cross_to_launch) noexcept;
// ship_gate_capital_join (--debug, traced capitals only): at 1 Hz, on every mode change and at the crossing.
inline void join_row(const GateState& g, std::uint32_t obj, std::uint32_t now, bool crossing, float cross_nose, float cross_vel, float cross_v,
                     std::uint8_t kept, std::uint8_t why_not) noexcept {
    const std::uint32_t w = join_row_write_.load(std::memory_order_relaxed);
    if (w - join_row_read_ >= 8) {
        row_dropped(Drop::join);
        return;
    }
    JoinRow& r = join_rows_[w & 7];
    r = JoinRow{};
    r.obj = obj;
    r.code = g.join_code;
    r.slot = g.grid_slot;
    r.mode = std::uint8_t(g.join.mode);
    r.reason = std::uint8_t(g.join.reason);
    r.latched = g.join.mode == core::JoinMode::fallback && g.join_logged_mode == std::uint8_t(core::JoinMode::fallback) ? 1 : 0;
    r.ms_in_window = g.join.window_ms ? now - g.join.window_ms : 0;
    r.s = g.join.plane.s;
    r.e = g.join_e;
    r.nose = core::fabs_f(g.join.plane.psi) * (180.0f / core::kPi);
    r.vel = core::fabs_f(g.join.plane.upsi) * (180.0f / core::kPi);
    r.rate_dps = g.join.plane.w * (180.0f / core::kPi);
    r.v = g.join.plane.v;
    r.v_cmd = g.join.v_cmd;
    r.carrot = g.join_carrot;
    r.pred_nose = g.join.pred_nose;
    r.pred_e = g.join.pred_y;
    r.crossing = crossing ? 1 : 0;
    r.kept = kept;
    r.why_not = why_not;
    r.cross_nose = cross_nose;
    r.cross_vel = cross_vel;
    r.cross_v = cross_v;
    join_row_write_.store(w + 1, std::memory_order_release);
}
void ctl_escort(std::uint32_t obj, std::uint32_t leader, const core::Inputs& in, const core::Dynamics& dyn, std::uint32_t now) noexcept;
void push_warn_pre(std::uint32_t obj, std::uint32_t fl, std::uint16_t order_now, std::uint32_t now) noexcept;
void push_engine_warn(std::uint32_t obj, std::uint32_t fl) noexcept;
// A class-7 ship with a capital record code (0x0043fd50's table: [type table + type * stride + class code]); unreadable = not capital.
inline bool capital_obj(std::uint32_t obj) noexcept {
    if (!obj || rd16(obj + sites::kObjClass) != sites::kClassShip) return false;
    const std::uint32_t tt = rd32(sites::kShipTypeTableSlot);
    const std::int32_t type = std::int16_t(rd16(obj + sites::kObjType));
    if (!tt || type < 0) return false;
    const std::uint32_t code = rd32(tt + std::uint32_t(type) * sites::kRecordStride + sites::kRecordClassCode);
    return code < core::kClassCount && ((core::kCapitalMask >> code) & 1u);
}
void ctl_frame() noexcept;
void dock_rows(unsigned long long frame) noexcept;
void ctl_rows(unsigned long long frame) noexcept;
} // namespace x3m_ship_steering_rt
using namespace x3m_ship_steering_rt;
