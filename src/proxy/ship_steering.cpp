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
namespace engine_patch = x3m::engine_patch;

extern "C" {
// Continuation slots (jmp [slot] in the stubs): the claim tails, written before the stub is reachable.
void* x3m_ship_steering_next_a0 = nullptr;
void* x3m_ship_steering_next_a1 = nullptr;
void* x3m_ship_steering_next_a2 = nullptr;
void* x3m_ship_steering_next_b = nullptr;
void x3m_ship_steering_a0_stub();
void x3m_ship_steering_a1_stub();
void x3m_ship_steering_a2_stub();
void x3m_ship_steering_b_stub();
int __cdecl x3m_ship_steering_enter(sites::Frame* frame, int leaf);
void __cdecl x3m_ship_steering_clamp(std::uint32_t obj);
// Site E (step 2): the warn test's entry; the stub answers a resolved pair with EAX = 0, `ret 0x10`.
void* x3m_ship_avoid_next_e = nullptr;
void x3m_ship_avoid_e_stub();
int __cdecl x3m_ship_avoid_suppress(std::uint32_t a, std::uint32_t b);
// Step 3 (gate approach): the three gate sites' continuation slots (the claim tails: pass-through), the resume
// addresses the live stubs jump to (absolute engine addresses, constants: the EXE is not relocatable), the live
// byte the stubs test first, the stubs and their handlers.
void* x3m_ship_gate_next_a = nullptr;
void* x3m_ship_gate_next_b = nullptr;
void* x3m_ship_gate_next_l = nullptr;
void* x3m_ship_gate_next_w = nullptr;
void* x3m_ship_gate_resume_a = reinterpret_cast<void*>(sites::kGateStopAResume);
void* x3m_ship_gate_resume_b = reinterpret_cast<void*>(sites::kGateStopBResume);
void* x3m_ship_gate_resume_l = reinterpret_cast<void*>(sites::kGateLaunchResume);
volatile unsigned char x3m_ship_gates_live = 0;
void x3m_ship_gate_p2_stub();
void x3m_ship_gate_p4_stub();
void x3m_ship_gate_p5_stub();
void x3m_ship_gate_warp_stub();
void __cdecl x3m_ship_gate_warp_in(std::uint32_t obj, std::uint32_t target);
void __cdecl x3m_ship_gate_stop_a(std::uint32_t obj);
void __cdecl x3m_ship_gate_stop_b(std::uint32_t obj);
std::int32_t __cdecl x3m_ship_gate_launch(std::uint32_t obj, std::int32_t t_ms);
// The gate traffic controller, stage A: the launch window 0x00444606 (ship_steering_sites.h (n)); the stub continues into the claim
// tail whether or not the handler offset the exit point.
void* x3m_ship_gate_next_t = nullptr;
volatile unsigned char x3m_ship_traffic_live = 0;
void x3m_ship_gate_arrival_stub();
void __cdecl x3m_ship_gate_arrival(std::uint32_t obj, std::uint32_t gate);
}

// A1-A3 (X3M_SS_LEAF): save EAX/ECX/EDX and XMM0-7, call the handler with the
// Frame* and the leaf index, restore everything (EDX from the frame: the
// step-2 seam may redirect it), then `ret 4` when handled or continue into
// the claim tail. `lea esp` and the pops keep the handler's `test eax,eax`
// flags for the `jz`. B: the asm class-7 + autopilot filter first (three
// instructions for every bullet and asteroid), then the same envelope around
// the clamp; the flags it writes are dead (the tail's replayed `cmp` rewrites
// them before the `jne`). Both envelopes execute `cld`: DF leaves cleared,
// the state every compiled caller assumes between string operations (the
// engine never calls a leaf with DF set), and the handlers rely on it.
// E (step 2): the same envelope around the suppression
// lookup with the stdcall arguments A = [esp+4], B = [esp+8] re-pushed from
// the saved frame (0x8c bytes above); a hit returns EAX = 0 through `ret 0x10`
// with ECX/EDX and EFLAGS dead at every caller (ship_steering_sites.h (e)),
// a miss continues into the claim tail (the original warn test).
asm(R"(
    .intel_syntax noprefix
    .text
    .macro X3M_SS_SAVE
    push eax
    push ecx
    push edx
    cld
    sub esp, 0x80
    movups [esp], xmm0
    movups [esp+0x10], xmm1
    movups [esp+0x20], xmm2
    movups [esp+0x30], xmm3
    movups [esp+0x40], xmm4
    movups [esp+0x50], xmm5
    movups [esp+0x60], xmm6
    movups [esp+0x70], xmm7
    .endm
    .macro X3M_SS_RESTORE
    movups xmm0, [esp]
    movups xmm1, [esp+0x10]
    movups xmm2, [esp+0x20]
    movups xmm3, [esp+0x30]
    movups xmm4, [esp+0x40]
    movups xmm5, [esp+0x50]
    movups xmm6, [esp+0x60]
    movups xmm7, [esp+0x70]
    lea esp, [esp+0x80]
    pop edx
    pop ecx
    pop eax
    .endm
    .macro X3M_SS_LEAF name, k, slot
    .p2align 4
    .globl \name
\name:
    X3M_SS_SAVE
    mov eax, esp
    push \k
    push eax
    call _x3m_ship_steering_enter
    add esp, 8
    test eax, eax
    X3M_SS_RESTORE
    jz 1f
    ret 4
1:  jmp dword ptr [\slot]
    .endm
    X3M_SS_LEAF _x3m_ship_steering_a0_stub, 0, _x3m_ship_steering_next_a0
    X3M_SS_LEAF _x3m_ship_steering_a1_stub, 1, _x3m_ship_steering_next_a1
    X3M_SS_LEAF _x3m_ship_steering_a2_stub, 2, _x3m_ship_steering_next_a2
    .p2align 4
    .globl _x3m_ship_steering_b_stub
_x3m_ship_steering_b_stub:
    cmp word ptr [ebx+0x48], 7
    jne 1f
    test dword ptr [ebx+0x40], 0x80000
    jz 1f
    X3M_SS_SAVE
    push ebx
    call _x3m_ship_steering_clamp
    add esp, 4
    X3M_SS_RESTORE
1:  jmp dword ptr [_x3m_ship_steering_next_b]
    .p2align 4
    .globl _x3m_ship_avoid_e_stub
_x3m_ship_avoid_e_stub:
    X3M_SS_SAVE
    push dword ptr [esp+0x94]
    push dword ptr [esp+0x94]
    call _x3m_ship_avoid_suppress
    add esp, 8
    test eax, eax
    X3M_SS_RESTORE
    jnz 1f
    jmp dword ptr [_x3m_ship_avoid_next_e]
1:  xor eax, eax
    ret 0x10
    .p2align 4
    .globl _x3m_ship_gate_p2_stub
_x3m_ship_gate_p2_stub:
    cmp byte ptr [_x3m_ship_gates_live], 0
    je 1f
    X3M_SS_SAVE
    push ebx
    call _x3m_ship_gate_stop_a
    add esp, 4
    X3M_SS_RESTORE
    jmp dword ptr [_x3m_ship_gate_resume_a]
1:  jmp dword ptr [_x3m_ship_gate_next_a]
    .p2align 4
    .globl _x3m_ship_gate_p4_stub
_x3m_ship_gate_p4_stub:
    cmp byte ptr [_x3m_ship_gates_live], 0
    je 1f
    X3M_SS_SAVE
    push ebx
    call _x3m_ship_gate_stop_b
    add esp, 4
    X3M_SS_RESTORE
    jmp dword ptr [_x3m_ship_gate_resume_b]
1:  jmp dword ptr [_x3m_ship_gate_next_b]
    .p2align 4
    .globl _x3m_ship_gate_p5_stub
_x3m_ship_gate_p5_stub:
    cmp byte ptr [_x3m_ship_gates_live], 0
    je 1f
    X3M_SS_SAVE
    push dword ptr [esp+0x88]
    push ebx
    call _x3m_ship_gate_launch
    add esp, 8
    mov [esp+0x88], eax
    X3M_SS_RESTORE
    jmp dword ptr [_x3m_ship_gate_resume_l]
1:  jmp dword ptr [_x3m_ship_gate_next_l]
    .p2align 4
    .globl _x3m_ship_gate_warp_stub
_x3m_ship_gate_warp_stub:
    cmp byte ptr [_x3m_ship_gates_live], 0
    je 1f
    X3M_SS_SAVE
    push dword ptr [esp+0x88]
    push ebx
    call _x3m_ship_gate_warp_in
    add esp, 8
    X3M_SS_RESTORE
1:  jmp dword ptr [_x3m_ship_gate_next_w]
    .p2align 4
    .globl _x3m_ship_gate_arrival_stub
_x3m_ship_gate_arrival_stub:
    cmp byte ptr [_x3m_ship_traffic_live], 0
    je 1f
    X3M_SS_SAVE
    push edi
    push ebx
    call _x3m_ship_gate_arrival
    add esp, 8
    X3M_SS_RESTORE
1:  jmp dword ptr [_x3m_ship_gate_next_t]
    .att_syntax
)");

namespace {
// ---- module state -------------------------------------------------------
std::atomic<bool> installed_{false};
bool initialized_ = false, timing_ = false, debug_ = false, perf_ = false;
const char* state_ = "not_initialized";
engine_patch::Site patches_[sites::Count];
std::uint32_t class_mask_ = 0;
std::uint64_t qpc_frequency_ = 0;
core::ShipTable table_; // 64 KB of .bss, the per-ship steering state
namespace av = x3m::ship_avoidance::core;
bool avoidance_on_ = false;
std::atomic<bool> warn_live_{false}; // site E chained
engine_patch::Site warn_site_{};
const char* warn_state_ = "not_claimed";
av::Snapshot snaps_[sites::kSnapshotSlots];     // per-sector per-frame obstacle snapshots (fixed capacity)
namespace wk = x3m::ship_avoidance::walk;
namespace ln = x3m::ship_avoidance::lane;
wk::PartCache part_cache_;                       // station part models, keyed on object, root radius and position/basis
av::ShipAvoid avoid_[core::kShipCapacity];      // parallel to table_.entries (same slot index)
std::uint32_t table_id_[core::kShipCapacity];   // B.4: per table slot, [obj+0x08] at the last stamp (a recycled pointer is a new ship)
// --debug witnesses: detour events and one census per sector (first snapshot), drained at the frame boundary.
struct DetourEvent {
    std::uint32_t obj, obstacle, kind, side, vertical, order, obstacle_leader, obstacle_order;
    float t_cpa, miss, C, L, P[3], x[3];
};
DetourEvent detours_[16];
std::atomic<std::uint32_t> detour_write_{0};
std::uint32_t detour_read_ = 0;
using Census = wk::Census;
Census census_[4];
std::atomic<std::uint32_t> census_write_{0};
std::uint32_t census_read_ = 0, census_logged_sectors_[8] = {};
unsigned census_logged_count_ = 0;
std::int32_t target_ring_[4][3];  // the step-2 seam: a redirected EDX points here, never at the caller's buffer
unsigned target_depth_ = 0;
// Step 3: the gate approach (X3M_SHIP_STEERING_GATES, default on with the steering): the three sites as a group.
bool gates_on_ = false;
std::atomic<bool> gates_live_{false};
engine_patch::Site gate_patches_[sites::GateCount];
const char* gates_state_ = "not_claimed";
// Amendment 13 (capital-gate-transit.md): X3M_SHIP_STEERING_GATES_CAPITAL (default on, needs the gate group live): the capital
// lead-in toward its validated grid point, the speed write in both directions inside the window, and P2/P4/P5 keeping the speed
// of an aligned capital. Off = the amendment-10 capital path (vanilla steps). The pair policy and site E's small-pair branch
// are part of X3M_SHIP_AVOIDANCE and do not depend on this key.
bool gates_capital_on_ = false;
const char* gates_capital_state_ = "off";
// Amendment 16b (capital-gate-transit.md section 16.10): X3M_SHIP_STEERING_GATES_CAPITAL_SEQ (default on with the capital key):
// the sequenced verdict (s) and the sequencing cap. Off = amendment 16 as built (with the review fixes).
bool seq_on_ = false;
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
    float lane_cap;            // the lane pass's cap at the last decision (the (b) follow cap; m/s, < 0 none)
    // review F4 and amendment 16b: the last width read (a failure is retried every 5 s, at most 3 times), the along-axis half-extent
    // max |o_z| + e_z of that read (the census's l_m)
    std::uint32_t w_read_ms;
    float w_len;
    // stage B (D.5): the small ship's last f+0x240 rewrite (units), so a field still holding it is left alone and a KC re-issue
    // (a new theta) is rewritten again
    x3m::ship_avoidance::ctl::SmallBWrite b_w;
};
struct GateTransition { // --debug: one row per order 4 -> 0 -> 5 transition at a gate (<= 8 per boundary)
    std::uint32_t obj, code, ms_since_p2;
    float dA_r, err_B_deg, v_ms, desired_ms, B_local[3], A_local[3];
    float p2_pos[3], p2_fwd[3], p2_carrot[3], o5_pos[3], o5_fwd[3], o5_dA_r, o5_dB_r; // amendment 10: the crossing and the order-5 start
    std::int32_t carrot_age_ms; // order-5 start minus the last lead-in call (-1: no lead-in call on this slot; amendment 11)
    std::uint8_t validated, crossed, capital, dock_blocked;
};
GateTransition gate_trans_[8];
std::atomic<std::uint32_t> gate_trans_write_{0};
std::uint32_t gate_trans_read_ = 0;
GateState gate_[core::kShipCapacity];
struct GateEvent { // --debug: one row per launch (the crossing data and the time from the crossing to the warp), or per
                   // capital crossing (the alignment at the grid point)
    std::uint32_t obj, code, cross_to_launch_ms;
    std::int32_t v0_units;
    float cross_v, cross_err_deg, off[2];
    std::uint8_t capital, spread_k;
};
GateEvent gate_events_[8];
std::atomic<std::uint32_t> gate_event_write_{0};
std::uint32_t gate_event_read_ = 0;
// Amendment 15: the formation membership memory (section 15.2; behind ship_avoidance), filled by the snapshot walk.
av::Members members_;
av::MemberStats member_stats_;     // totals since install (engine thread; read at the frame boundary)
// Amendment 15: the frame-end predictor's registry of capitals in join mode (core::JoinRegistry: table slot indices, at most 8,
// swept before each evaluation) and its cost.
core::JoinRegistry join_reg_;
std::uint32_t join_eval_max_ticks_ = 0, join_evals_ = 0, join_predictions_ = 0;
// --debug rows (amendment 15, section 15.4), written to fixed rings in the handlers and formatted at the frame boundary.
struct JoinRow { // ship_gate_capital_join
    std::uint32_t obj, code, ms_in_window;
    std::uint8_t slot, mode, reason, crossing, kept, why_not, latched, pad;
    float s, e, nose, vel, rate_dps, v, v_cmd, carrot, pred_nose, pred_e;
    float cross_nose, cross_vel, cross_v;
};
JoinRow join_rows_[8];
std::atomic<std::uint32_t> join_row_write_{0};
std::uint32_t join_row_read_ = 0;
struct EVerdictRow { // ship_avoid_e_verdict (trace slot 0 as A)
    std::uint32_t a, b, b_class, b_code, b_leader, b_flags, member_leader;
    std::int32_t member_age_ms, a_age_ms, b_age_ms;
    std::uint16_t a_order, b_order;
    float d, r_b;
    std::uint8_t verdict;
    std::uint8_t gate_row;    // B.3 item 6: gc::GateRow of the own-gate row for a member A (0xff: not evaluated)
    std::int32_t gate_age_ms; // ... A's member state age then (-1: none)
    std::uint32_t step_ms;    // ... the frame's game-time step ([root+0x714], unclamped)
};
// amendment 17 (section 17 item 5) with review F4: E rows for every capital A as well: at most kERows rows per frame, one per pair
// per game second through a kELogged pair table; a new pair finding the table full (kELogged pairs logged within the last second) or
// the frame's rows written is dropped and counted (gate_rows_dropped, rows_dropped e_table / e_ring); 64 last verdicts for the
// warn_pre rows. The gate traffic controller (D.7): 32 rows per frame and a 128-pair table (Run160b: the ring sat at its cap of 8 in
// 401 of 2,829 crowd frames); member pairs write no per-call row (the ship_gate_ctl_pair row on a verdict change instead).
constexpr unsigned kERows = 32, kELogged = 128, kELast = 64;
EVerdictRow e_rows_[kERows];
std::uint8_t e_gate_why_ = 0xff;  // B.3 item 6: the own-gate row's reason of the current e_verdict call (ctl_e_fields), for e_record
std::int32_t e_gate_age_ = -1;
std::atomic<std::uint32_t> e_row_write_{0};
std::uint32_t e_row_read_ = 0;
struct EVerdictSeen { // the per-pair dedup (one row per pair per second of game time) and the last verdict per traced pair
    std::uint32_t a, b, ms;
    std::uint8_t verdict;
};
EVerdictSeen e_logged_[kELogged];
EVerdictSeen e_last_[kELast];
unsigned e_last_next_ = 0;
struct WarnPreRow { // ship_avoid_engine_warn_pre
    std::uint32_t obj, order, tgt, count;
    std::uint32_t other[5], cls[5], code[5], o_order[5], leader[5], member[5];
    float d[5], r[5];
    std::uint8_t verdict[5];
};
WarnPreRow warn_pre_[8];
std::atomic<std::uint32_t> warn_pre_write_{0};
std::uint32_t warn_pre_read_ = 0;
using EV = av::EVerdict; // site E's verdicts (ship_avoidance_core.h site_e_verdict), the --debug rows
// Amendment 16 (capital-gate-transit.md section 16): the lane board (8 gates x 20 capitals, filled at each capital decision on a
// validated approach and at P2; ship_capital_lane_core.h), behind ship_steering_gates_capital; amendment 16b behind
// ship_steering_gates_capital_seq.
ln::Board lanes_;
struct LaneRow { // ship_capital_lane (--debug, traced A): 1 Hz per pair and on every verdict or route change
    std::uint32_t a, b, trail;
    std::uint8_t verdict, foot_a, foot_b, w_src_a, w_src_b, route, watch, bind, held, pad16b[3];
    float gap, w_a, w_b, e_a, theta_a, ds, v_cap, D; // v_cap / D: the (b) follow cap when A trails (amendment 17: no (s) cap)
    float lat_gap, mot, h_t, h_l; // amendment 16b (section 16.10.6): the certificates
};
LaneRow lane_rows_[16];
std::atomic<std::uint32_t> lane_row_write_{0};
std::uint32_t lane_row_read_ = 0;
struct LaneSeen { // the last verdict and route per traced pair and its row time
    std::uint32_t a, b, ms;
    std::uint8_t verdict, route;
};
LaneSeen lane_seen_[32];
unsigned lane_seen_next_ = 0;
struct LatchRow { // ship_gate_capital_latch (--debug, every capital): at each pred_none / slow_join / unlatch transition
    std::uint32_t obj, code, obstacle;
    std::uint8_t event, before, after, slot, n_lanes, pad[3];
    core::JoinPlane plane;
    core::JoinParams params;
    float v_cmd;
    core::JoinPred cand[core::kJoinCandidateCount];
    std::uint32_t lane_other[6];
    std::uint8_t lane_verdict[6];
    float lane_gap[6];
};
LatchRow latch_rows_[8];
std::atomic<std::uint32_t> latch_row_write_{0};
std::uint32_t latch_row_read_ = 0;
struct WidthCensus { // ship_capital_census: one row per capital type (the record index [obj+0x4a])
    std::uint32_t type, code;
    float r, w, l;           // l: max over the parts of |o_z| + e_z (amendment 16b: h = sqrt(r^2 + w^2) must cover it)
    std::uint8_t w_src, retries;
    // amendment 17 (section 17 item 5): the widest part (body id, lateral offset bound and half-extents in m, its widest LOD record)
    std::int32_t part_id, part_lod;
    float o_x, o_y, e_x, e_y;
};
WidthCensus width_census_[8];
std::atomic<std::uint32_t> width_census_write_{0};
std::uint32_t width_census_read_ = 0;
std::int32_t width_census_types_[32];
unsigned width_census_count_ = 0;


// Counters: written by the engine thread with plain relaxed stores (one
// writer), read at the frame boundary.
struct Counter {
    std::atomic<std::uint32_t> v{0};
    void add(std::uint32_t n = 1) noexcept { v.store(v.load(std::memory_order_relaxed) + n, std::memory_order_relaxed); }
    std::uint32_t get() const noexcept { return v.load(std::memory_order_relaxed); }
};
Counter calls_, handled_, reasons_[core::kReasonCount], leaf_calls_[3], clamp_calls_, clamp_applied_, clamp_stale_;
Counter handled_by_order_[0x40], redirected_;
Counter av_calls_, av_conflicts_, av_detours_, av_unresolved_, av_overflow_, av_snapshots_, av_walk_objects_, av_emergency_;
Counter av_unresolved_why_[unsigned(av::Unresolved::count)], av_unresolved_episodes_, av_conflict_episodes_, part_dirty_, part_scaled_;
Counter dock_parts_evicted_; // amendment 18 (review 6): a valid part model found every cache slot pinned by the current snapshot
Counter e_calls_, e_suppressed_, part_built_, part_fallback_, part_invalid_;
Counter e_suppressed_small_, e_small_pass_, e_small_pass_reason_[unsigned(av::SmallPair::count)]; // site E's small-ship row (amendment 15)
Counter e_suppressed_member_, e_suppressed_gate_, e_suppressed_gate_small_, avoid_excluded_member_; // amendment 15, audit G1
Counter gate_capital_kept_, gate_capital_lead_validated_, gate_capital_lead_fallback_;
Counter gate_capital_join_, gate_capital_slowed_, gate_capital_fallback_pred_, gate_capital_fallback_past_x_;
Counter gate_capital_not_kept_[unsigned(core::JoinKeep::count)];
Counter lane_exempt_, lane_follow_, lane_cross_, e_suppressed_lane_, gate_capital_slow_join_, gate_capital_unlatch_, lane_capped_; // amendment 16
Counter gate_capital_detour_write_; // amendment 16 fix: decisions of a join / slow join on a detour that kept the join's speed write
// amendment 16b (section 16.10.6): watch pairs by route per decision (lat, along, left at (c)), decisions with the sequencing cap
// applied, ms at its floor, along-route pairs of a capped trailer that turned (c)
Counter lane_seq_lat_, lane_seq_along_, lane_seq_cross_;
Counter lane_apart_; // amendment 17: committed (c) pairs on different lane points dropped from the avoidance
Counter lane_cap_carried_; // decisions that carry a lane cap (lane_capped: those whose write it lowered)
Counter lane_hysteresis_hold_; // decisions whose own in-lane state is held by the hysteresis only (section 16.11)
Counter gate_lead_, gate_cross_, gate_p2_, gate_stops_skipped_, gate_p4_, gate_v0_kept_, gate_p5_, gate_launch_continued_, gate_speed_writes_;
Counter gate_spread_offsets_, gate_exit_offsets_, gate_capital_aligned_, gate_warp_calls_;
Counter gate_lead_validated_, gate_lead_fallback_, gate_lead_window_rejected_, gate_dock_writes_, gate_dock_blocked_;
Counter gate_p2_no_gate_target_, gate_p2_capital_, gate_p2_no_state_, gate_p4_no_gate_target_, gate_p4_capital_, gate_p4_no_state_;
Counter gate_dock_invalid_, gate_warp_skipped_, gate_rows_dropped_, avoid_excluded_followers_, avoid_skipped_small_;
// The gate traffic controller's ring changes (gate-traffic-controller.md D.7): gate_rows_dropped stays the sum; each source counts
// its own drops (the summary's rows_dropped field, in this order).
enum class Drop : unsigned { gate_event = 0, join, latch, lane, gate_trans, warn_pre, engine_warn, warn_watch, e_table, e_ring, ctl, ctl_pair,
                             launch, count };
constexpr const char* kDropNames[unsigned(Drop::count)] = {"gate_event", "join", "latch", "lane", "gate_trans", "warn_pre", "engine_warn",
                                                           "warn_watch", "e_table", "e_ring", "ctl", "ctl_pair", "launch"};
Counter rows_dropped_[unsigned(Drop::count)];
inline void row_dropped(Drop d) noexcept {
    gate_rows_dropped_.add();
    rows_dropped_[unsigned(d)].add();
}
// The summary's gates text (frame boundary): its worst case, every conversion at its widest, as summary_bound.py computes it from
// this file (the host test keeps the constant equal to the computed figure); the buffer must hold it. It held 4,096 B up to B.3,
// below the 4,123-character worst case: the tail truncated.
constexpr std::size_t kSummaryGatesWorstCase = 4239;
constexpr std::size_t kSummaryGatesBytes = 8192;
static_assert(kSummaryGatesWorstCase < kSummaryGatesBytes, "the summary's gates text fits its buffer (summary_bound.py)");
std::uint32_t rep_gate_[32] = {};
// Step 4 item 7: ship_trace (--debug tier, X3M_SHIP_TRACE=1, X3M_SHIP_TRACE_MAX ships): the module follows the first N
// non-capital class-7 autopilot ships that enter a gate approach (order 4 with a class-0x12 target) or a dock approach
// (order 4 with a class-5/6 target, or order 5) and writes one row per frame for each from a ring drained at the frame
// boundary: the engine's state the fixture's model needs as input (position, basis rows, rates, speeds, the point the
// engine gave the leaf, the order, dt) and what the module handed back (the point, the written remainders); the next
// row of the same ship is the oracle. A slot is freed when the ship launches (P5), docks, or has not been seen for 5 s.
constexpr unsigned kTraceMax = 16, kTraceRing = 128;
constexpr std::uint32_t kTraceIdleMs = 5000;
bool trace_on_ = false;
unsigned trace_max_ = 8; // amendment 17 (section 17 item 5): the default; kTraceMax slots
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
TraceSlot trace_slots_[kTraceMax];
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
CapWarnSlot cap_warn_[kCapWarnSlots];
unsigned cap_warn_used_ = 0; // slots holding a capital: with none, a ship neither warned nor on 0x12 costs two reads
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
EngineWarn engine_warns_[8];
std::atomic<std::uint32_t> engine_warn_write_{0};
std::uint32_t engine_warn_read_ = 0;
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
TraceRow trace_ring_[kTraceRing];
// review F7: every ring and table indexed with a mask is a power of two
constexpr bool pow2(unsigned n) noexcept { return n && !(n & (n - 1)); }
static_assert(pow2(kTraceRing) && pow2(kERows) && pow2(kELogged) && pow2(kELast), "masked rings and tables are powers of two");
static_assert(pow2(sizeof join_rows_ / sizeof join_rows_[0]) && pow2(sizeof warn_pre_ / sizeof warn_pre_[0]) &&
                  pow2(sizeof lane_rows_ / sizeof lane_rows_[0]) && pow2(sizeof latch_rows_ / sizeof latch_rows_[0]) &&
                  pow2(sizeof width_census_ / sizeof width_census_[0]) && pow2(sizeof engine_warns_ / sizeof engine_warns_[0]) &&
                  pow2(sizeof lane_seen_ / sizeof lane_seen_[0]),
              "masked rings and tables are powers of two");
std::atomic<std::uint32_t> trace_write_{0};
std::uint32_t trace_read_ = 0;
Counter trace_rows_, trace_dropped_;
std::uint32_t rep_trace_[2] = {};
inline TraceSlot* trace_slot_of(std::uint32_t obj) noexcept {
    for (unsigned i = 0; i < trace_max_ && i < kTraceMax; ++i)
        if (trace_slots_[i].obj == obj) return &trace_slots_[i];
    return nullptr;
}
// Slot 0 is reserved for a capital on an order-4 gate approach (amendment 11: the Run155 Boreas circled for minutes
// with no per-frame rows); the other trace_max_ - 1 slots follow non-capital gate/dock approaches.
// Amendment 15 (section 15.4): an escort of slot 0's capital (any class, on 0x14/0x15 to it) takes one of the other slots, so the
// escort side of the cascade gets its warn_pre rows too.
bool traffic_on_ = false; // the traffic controller live (set at install; defined here for trace_claim, section below)
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
inline void trace_release(std::uint32_t obj) noexcept {
    if (TraceSlot* s = trace_slot_of(obj)) s->obj = 0;
}
std::atomic<std::uint32_t> qpc_hist_[32];
std::atomic<std::uint32_t> qpc_max_{0};
// Reported totals (frame boundary only).
std::uint32_t rep_calls_ = 0, rep_handled_ = 0, rep_reasons_[core::kReasonCount] = {}, rep_clamp_calls_ = 0,
              rep_clamp_applied_ = 0, rep_clamp_stale_ = 0, rep_leaf_[3] = {}, rep_redirected_ = 0;
std::uint32_t rep_av_[13] = {}, rep_av_why_[unsigned(av::Unresolved::count)] = {}, rep_av_ep_[4] = {};
unsigned long long last_detour_tick_ = 0;
unsigned detour_rows_this_second_ = 0;
unsigned long long last_summary_tick_ = 0, last_sample_tick_ = 0;
std::uint32_t last_logged_seq_ = 0; // the sample row is logged once per written sample
// The sampled decision (--debug): a seqlock written by the handler when requested.
struct Sample {
    std::uint32_t obj, order, code, leaf, flags;
    float theta_deg, n_y, n_p, omega_dps, d, v, v_cmd, v_corner, d_arr, v_ref;
    float lag_deg, aim_cos, v_slip, slip[3]; // amendment 14: the velocity's lag and the lead (cosines), the closing-speed correction, obj+0x60 (m/s)
    std::int32_t e[3], w[3], rot_accel_raw, rot_pct;
    float P[3], x[3], W_dps[3], k, a_dps[3], omega_axis_dps[3];
    bool held;
};
Sample sample_;
std::atomic<std::uint32_t> sample_seq_{0};
std::atomic<bool> sample_request_{false};
// ship_steering_stuck witnesses (always tier, first 8 per session).
struct Stuck {
    std::uint32_t obj, order;
    float d, best_d;
};
Stuck stuck_[8];
std::atomic<std::uint32_t> stuck_count_{0};
unsigned stuck_reported_ = 0;

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
inline void record_time(std::uint64_t ticks) noexcept {
    const std::uint32_t t = ticks > 0xffffffffull ? 0xffffffffu : std::uint32_t(ticks);
    const unsigned bucket = t ? 31u - unsigned(__builtin_clz(t)) : 0u;
    qpc_hist_[bucket].store(qpc_hist_[bucket].load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    if (t > qpc_max_.load(std::memory_order_relaxed)) qpc_max_.store(t, std::memory_order_relaxed);
}
inline float sc_max(float a, float b) noexcept {
    return a > b ? a : b;
}
inline float fixed16(std::int32_t v) noexcept {
    return float(v) * (1.0f / 65536.0f);
}
// The step-2 seam: the avoidance layer replaces this identity with its detour
// point. A changed point is copied into the next slot of a 4-deep round-robin
// ring (DLL .data; target_depth_ is a running index, not a nesting depth: the
// leaves are not recursive, so one slot would do) and EDX is redirected to it, so
// the law and a pass-through leaf both see the adjusted point and the
// caller's buffer (often a stack local) and f+0x240 (order state) stay unwritten.
inline const std::int32_t* adjust_target(std::uint32_t obj, std::uint16_t order, const std::int32_t point[3]) noexcept {
    (void)obj;
    (void)order;
    return point;
}
inline void redirect_target(sites::Frame* f, const std::int32_t point[3]) noexcept {
    std::int32_t* slot = target_ring_[target_depth_++ & 3u];
    slot[0] = point[0];
    slot[1] = point[1];
    slot[2] = point[2];
    f->edx = std::uint32_t(reinterpret_cast<std::uintptr_t>(slot));
    redirected_.add();
}
// ---- the gate traffic controller, stage A (gate-traffic-controller.md D.1, D.4, D.7; ship_gate_ctl_core.h) -------------------------
// X3M_SHIP_STEERING_GATES_TRAFFIC (default on; needs ship_steering_gates_capital live): the per-gate member boards replace the lane
// board for capitals at a gate (no lane verdicts, no certificates, no (b) cap), site E owns member pairs (suppress_ctl / pass_ctl)
// and the arrival-gate row, the avoidance drops member pairs with a clear verdict, the launch window 0x00444606 places arrivals.
// Off: amendment 17 exactly (no claim, no board, no E row, no exclusion). Game thread only; no allocation, no lock.
namespace gc = x3m::ship_avoidance::ctl;
std::atomic<bool> traffic_live_{false};
engine_patch::Site traffic_patch_;
const char* traffic_state_ = "off";
gc::Board ctl_;
gc::Index ctl_ix_;      // member ships (every board, every role but none)
gc::Arrivals arrivals_; // every launch at a gate within 60 s (the arrival-gate row; capital arrivals for the small-ship row)
gc::Index arr_ix_;      // their ships (rebuilt once per second)
unsigned ctl_cursor_ = 0;
std::uint32_t ctl_decide_max_ticks_ = 0, ctl_frame_max_ticks_ = 0, ctl_arrival_max_ticks_ = 0;
unsigned ctl_frame_max_decisions_ = 0;
std::uint32_t arr_ix_rebuilt_ms_ = 0;
Counter ctl_decisions_, ctl_rollouts_, ctl_sweeps_, ctl_narrow_, ctl_conflicts_, ctl_yields_, ctl_unresolved_, ctl_released_;
Counter ctl_entered_, ctl_left_, ctl_evicted_, ctl_capped_, ctl_deferred_;
Counter e_suppressed_ctl_, e_pass_ctl_, e_suppressed_arrival_gate_, avoid_ctl_excluded_;
Counter arr_launches_, arr_offset_capital_, arr_small_side_, arr_small_unseparated_, arr_skipped_;
// stage B (B.1): releases, go-arounds, KC refusals at X (and their wait beyond 3 s), the decisions the controller flew, the funnel
// predictions, the guard's frames, the P2 remainder zeroing, the small ships' f+0x240 rewrites (D.5)
Counter ctl_releases_, ctl_go_around_, ctl_x_refused_, ctl_sb_steered_, ctl_sb_predictions_, ctl_guarded_, ctl_p2_zeroed_, small_b_rewrites_;
Counter e_pass_ctl_why_[unsigned(gc::EWhy::count)]; // Run161: why site E passed a member pair (not_member: a member against a capital
                                                    // that is not on its board)
Counter ctl_evict_ignored_gate_, ctl_budget_cut_; // B.3: 0x12 episodes kept through (item 2); frames cut at the 1 ms ceiling
std::uint64_t ctl_ceiling_ticks_ = 0;             // B.3: gc::kFrameCeilingUs in QPC ticks (0: no ceiling, no QPC)
Counter e_small_gap_; // B.4: suppress_small with A's decision older than the fresh window (the order-0 gap)
// B.5 (ship_gate_ctl_live_core.h): the per-frame live set, the controller's sector, the frame's proofs and leaves; counters: members left
// unproven (gone), launched members left (leg over or missing from the snapshot), sector resets and the entries they dropped, member-frames
// without a proof, frames whose set overflowed (nothing proven); the proofs' longest frame (QPC ticks, timing tier)
gc::LiveSet live_set_;
gc::LiveTrack live_track_;
gc::LiveOut live_out_;
Counter ctl_live_gone_, ctl_live_launch_, ctl_sector_resets_, ctl_sector_dropped_, ctl_unproven_, ctl_live_overflow_;
std::uint32_t ctl_live_max_ticks_ = 0;
Counter e_gate_small_gap_, small_b_both_, ctl_seq_skipped_, ctl_go_around_near_, ctl_clock_resets_;                         // Run161 item 5: the small-ship gate row on order 0 in the hand-over gap
constexpr std::uint32_t kSmallGapMs = 5000;        // ... while the order-4 decision or the lead-in is at most 5 s old
std::uint32_t ctl_x_wait_ms_ = 0, ctl_sb_max_ticks_ = 0;
// The widest-part read per capital type (the record index [obj+0x4a]) for the arrival placement: a launched capital has no gate
// state yet; w and l from the last successful LOD read of a ship of that type (w = r, l = r when the type was never read).
struct TypeWidth {
    std::int32_t type;
    float w, l;
};
TypeWidth type_widths_[32];
unsigned type_width_count_ = 0;
inline void type_width_put(std::int32_t type, float w, float l) noexcept {
    for (unsigned i = 0; i < type_width_count_; ++i)
        if (type_widths_[i].type == type) {
            type_widths_[i].w = w;
            type_widths_[i].l = l;
            return;
        }
    if (type_width_count_ < 32) type_widths_[type_width_count_++] = TypeWidth{type, w, l};
}
inline bool type_width_get(std::int32_t type, float* w, float* l) noexcept {
    for (unsigned i = 0; i < type_width_count_; ++i)
        if (type_widths_[i].type == type) {
            *w = type_widths_[i].w;
            *l = type_widths_[i].l;
            return true;
        }
    return false;
}
// --debug rows (D.7), drained at the frame boundary: ship_gate_ctl (member role and sequence transitions, ring 32),
// ship_gate_ctl_pair (a member pair's verdict change, at most once per pair per second, ring 64), ship_gate_launch (one per launch at
// 0x00444606, ring 8).
struct CtlRow {
    std::uint32_t gate, ship, id, seq, t_ms, key;
    std::uint16_t order;
    std::uint8_t from, to, why, rank, sb_from, sb_to;
    std::int8_t petal;
    std::uint8_t pad;
};
CtlRow ctl_rows_[32];
std::atomic<std::uint32_t> ctl_row_write_{0};
std::uint32_t ctl_row_read_ = 0;
struct CtlPairRow {
    std::uint32_t gate, a, b, t_ms, seq_a, seq_b;
    std::uint8_t verdict, before, t_c, yielder, rank_a, rank_b, kind, why; // kind: 0 a verdict change, 1 site E passed the pair (why: the
    float cap, d;                                                          // gc::EWhy), 2 a member's order-0 frame (the pair's E reason)
};
CtlPairRow ctl_pair_rows_[64];
std::atomic<std::uint32_t> ctl_pair_write_{0};
std::uint32_t ctl_pair_read_ = 0;
struct LaunchRow {
    std::uint32_t ship, gate, code, t_ms;
    std::int32_t slot;
    std::uint16_t type;
    std::uint8_t kind, capital; // kind: 0 vanilla, 1 capital offset, 2 small side spawn, 3 small unseparated
    float P[3], off[2], r_gate, r, w;
};
LaunchRow launch_rows_[8];
std::atomic<std::uint32_t> launch_row_write_{0};
std::uint32_t launch_row_read_ = 0;
// B.2 item 5: ship_gate_ctl_seq (the sequencer's state, one per gate per game second while a holder exists, ring 8; drops count as ctl)
struct SeqRow {
    std::uint32_t gate, t_ms, skips, releases;
    gc::SbSeq s;
};
SeqRow seq_rows_[8];
std::atomic<std::uint32_t> seq_row_write_{0};
std::uint32_t seq_row_read_ = 0;
// B.4 item 2: ship_gate_ctl_member (--debug): one row per board member (pair role) per game second, traced or not (ring 32; drops
// count as ctl, as the seq rows). The position and the nose are the member's last state read (site B), in the gate frame (R, U, F; m).
struct MemberRow {
    std::uint32_t gate, ship, t_ms, target, key, code;
    std::int32_t st_age_ms, dec_age_ms;
    std::uint16_t order;
    std::uint8_t role, stage, traced;
    std::int8_t petal;
    float p[3], nose_f, v;
    std::uint8_t live; // B.5: the member passed the liveness proof this frame (0: no engine field read; class=?)
};
MemberRow member_rows_[32];
std::atomic<std::uint32_t> member_row_write_{0};
std::uint32_t member_row_read_ = 0;
std::uint32_t member_row_ship_[gc::kGates][gc::kMembers] = {}, member_row_ms_[gc::kGates][gc::kMembers] = {}; // the last row per slot
static_assert(pow2(sizeof ctl_rows_ / sizeof ctl_rows_[0]) && pow2(sizeof ctl_pair_rows_ / sizeof ctl_pair_rows_[0]) &&
                  pow2(sizeof launch_rows_ / sizeof launch_rows_[0]) && pow2(sizeof seq_rows_ / sizeof seq_rows_[0]) &&
                  pow2(sizeof member_rows_ / sizeof member_rows_[0]),
              "masked rings are powers of two");
inline void ctl_row(const gc::Gate& g, const gc::Member& m, gc::Role from, gc::Role to, gc::Why why, std::uint32_t now) noexcept {
    if (!debug_) return;
    const std::uint32_t w = ctl_row_write_.load(std::memory_order_relaxed);
    if (w - ctl_row_read_ >= 32) {
        row_dropped(Drop::ctl);
        return;
    }
    ctl_rows_[w & 31] = CtlRow{g.gate, m.ship, m.id, m.seq, now, m.key, m.st.order, std::uint8_t(from), std::uint8_t(to), std::uint8_t(why),
                               std::uint8_t(gc::rank(m)), std::uint8_t(m.sb), std::uint8_t(m.sb), m.petal, 0};
    ctl_row_write_.store(w + 1, std::memory_order_release);
}
// The ship's board and member index (two loads for a non-member: the bitmap).
inline gc::Gate* ctl_lookup(std::uint32_t obj, int* index) noexcept {
    *index = -1;
    if (!obj || !ctl_ix_.maybe(obj)) return nullptr;
    return gc::locate(ctl_, obj, index);
}
// B.4 item 2: the ship holds a pair role on a board (the reserved trace slots)
inline bool ctl_on_board(std::uint32_t obj) noexcept {
    int i = -1;
    const gc::Gate* g = ctl_lookup(obj, &i);
    return g && i >= 0 && gc::pair_role(g->m[i].role);
}
// A role change with its row and the bitmap kept current.
inline void ctl_leave(gc::Gate& g, unsigned i, bool evict, gc::Why why, std::uint32_t now) noexcept {
    const gc::Member before = g.m[i]; // the row carries the member as it was (a none leave clears the slot)
    if (before.role == gc::Role::none) return;
    gc::leave(g, i, evict, now);
    if (evict) ctl_evicted_.add();
    else ctl_left_.add();
    ctl_row(g, before, before.role, evict ? gc::Role::evicted : gc::Role::none, why, now);
    if (!evict) gc::rebuild(ctl_ix_, ctl_);
}
inline gc::Member* ctl_enter(gc::Gate& g, std::uint32_t obj, std::uint32_t id, gc::Role role, gc::Why why, std::uint32_t now) noexcept {
    gc::Role before = gc::Role::count;
    gc::Member* m = gc::enter(ctl_, g, obj, id, role, now, &before);
    if (!m) return nullptr;
    if (before != role) { // gc::enter also removed the ship from any other board: the bitmap is rebuilt on every role change
        ctl_entered_.add();
        ctl_row(g, *m, before == gc::Role::count ? gc::Role::none : before, role, why, now);
        gc::rebuild(ctl_ix_, ctl_);
    }
    return m;
}
// The gate's frame from its phys block (decide()'s reads); false when the rows are not an orthonormal frame or r is not positive.
inline bool gate_frame(std::uint32_t gate, float G[3], float R[3], float U[3], float F[3], float* r_gate) noexcept {
    const std::uint32_t gph = gate ? rd32(gate + sites::kObjPhys) : 0;
    if (!gph) return false;
    for (unsigned i = 0; i < 3; ++i) {
        G[i] = float(rds32(gph + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
        R[i] = fixed16(rds32(gph + sites::kPhysBasis + 4 * i));
        U[i] = fixed16(rds32(gph + sites::kPhysBasis + 0x10 + 4 * i));
        F[i] = fixed16(rds32(gph + sites::kPhysBasis + 0x20 + 4 * i));
    }
    *r_gate = float(rds32(gate + sites::kObjRadius)) / core::kUnitsPerMetre;
    const float rr = core::dot3(R, R), uu = core::dot3(U, U), ff = core::dot3(F, F);
    const float ru = core::fabs_f(core::dot3(R, U)), rf = core::fabs_f(core::dot3(R, F)), uf = core::fabs_f(core::dot3(U, F));
    return *r_gate > 0.0f && core::finite_f(*r_gate) && rr > 0.98f && rr < 1.02f && uu > 0.98f && uu < 1.02f && ff > 0.98f && ff < 1.02f &&
           ru < 0.02f && rf < 0.02f && uf < 0.02f;
}
// A member's live state (site B, every kStateMs at most; the launch hook; decide()).
inline void ctl_read_state(gc::Member& m, std::uint32_t obj, std::uint32_t fl, std::uint32_t ph, std::uint32_t now) noexcept {
    gc::State& st = m.st;
    st.ms = now ? now : 1;
    st.order = rd16(fl + sites::kFlightOrder);
    st.target = rd32(fl + sites::kFlightTarget);
    for (unsigned i = 0; i < 3; ++i) {
        st.x[i] = float(rds32(ph + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
        st.w[i] = float(rds32(obj + sites::kObjRates + 4 * i)) * core::kRadPerBinary;
        st.point[i] = float(rds32(fl + sites::kFlightPoint + 4 * i)) / core::kUnitsPerMetre;
    }
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = 0; c < 3; ++c) st.rows[r][c] = fixed16(rds32(fl + sites::kFlightBasis + 0x10 * r + 4 * c));
    st.f_ok = gc::unit_row(st.rows[2]) ? 1 : 0;
    st.v = float(rds32(obj + sites::kObjSpeed)) / core::kUnitsPerMetre;
    st.v_des = float(rds32(obj + sites::kObjDesiredSpeed)) / core::kUnitsPerMetre;
    float slip[3];
    for (unsigned i = 0; i < 3; ++i) slip[i] = float(rds32(obj + sites::kObjSlip + 4 * i)) / core::kUnitsPerMetre;
    const float vmax_guess = m.params.v_max > 0.0f ? m.params.v_max : 200.0f;
    if (st.order == core::kOrderDock || !core::slip_usable(slip, vmax_guess)) slip[0] = slip[1] = slip[2] = 0.0f;
    for (unsigned i = 0; i < 3; ++i) st.u[i] = st.v * st.rows[2][i] + slip[i];
    st.left_ms = std::int32_t(rd32(fl + sites::kFlightDeadline) - now);
    gc::note_state(m); // Run161: the order-0 gap's steered stamp and plan
    // B.5: the record class code for the member row, read here (the engine handed obj to this call), never at the frame boundary
    const std::uint32_t tt = rd32(sites::kShipTypeTableSlot);
    const std::int32_t type = std::int16_t(rd16(obj + sites::kObjType));
    m.code = tt && type >= 0 ? rd32(tt + std::uint32_t(type) * sites::kRecordStride + sites::kRecordClassCode) : 0xffffffffu;
}
// B.5: board entries exist (the live set enters a walk's ships only then; its sector always)
inline bool ctl_any_member() noexcept {
    for (const gc::Gate& g : ctl_.g)
        for (unsigned i = 0; i < g.n; ++i)
            if (g.m[i].ship && g.m[i].role != gc::Role::none) return true;
    return false;
}
// ---- step 2: the sector snapshot and the station part model ------------------
// The walk itself is the portable ship_avoidance_walk_core.h (the fixture runs it over a synthetic sector); here it
// reads the engine's memory directly. Census rows (--debug): at most one per sector per session.
struct RawMem {
    std::uint32_t u32(std::uint32_t at) const noexcept { return rd32(at); }
    std::int32_t s32(std::uint32_t at) const noexcept { return rds32(at); }
    std::uint16_t u16(std::uint32_t at) const noexcept { return rd16(at); }
};
const av::Snapshot* snapshot_for(std::uint32_t sector, std::uint32_t now) noexcept {
    unsigned slot = 0;
    for (unsigned i = 0; i < sites::kSnapshotSlots; ++i) {
        if (snaps_[i].sector == sector && snaps_[i].epoch == now && snaps_[i].count + snaps_[i].overflow) return &snaps_[i];
        if (snaps_[i].sector == sector) slot = i;
        else if (snaps_[slot].sector != sector && snaps_[i].epoch < snaps_[slot].epoch) slot = i;
    }
    av::Snapshot& s = snaps_[slot];
    bool want_census = debug_;
    for (unsigned i = 0; i < census_logged_count_ && want_census; ++i)
        if (census_logged_sectors_[i] == sector) want_census = false;
    Census* census = nullptr;
    Census local{};
    if (want_census && census_logged_count_ < 8) {
        census_logged_sectors_[census_logged_count_++] = sector;
        census = &local;
        census->sector = sector;
    }
    RawMem mem;
    wk::Stats stats;
    wk::fill_snapshot(mem, sector, now, s, part_cache_, stats, census, &members_, &member_stats_); // amendment 15: the membership pass
    // B.5: the walk is this frame's update handing the sector's objects over: its ships enter the live set (the frame boundary proves
    // board members from it without reading engine memory)
    if (traffic_live_.load(std::memory_order_relaxed)) live_set_.add_snapshot(s, ctl_any_member());
    av_snapshots_.add();
    av_walk_objects_.add(stats.visited);
    if (s.overflow) av_overflow_.add();
    part_built_.add(stats.part_built);
    part_fallback_.add(stats.part_fallback);
    if (stats.part_pinned_full) dock_parts_evicted_.add(stats.part_pinned_full);
    part_invalid_.add(stats.part_invalid);
    part_dirty_.add(stats.part_dirty);
    part_scaled_.add(stats.part_scaled);
    if (census) {
        const std::uint32_t w = census_write_.load(std::memory_order_relaxed);
        if (w - census_read_ < 4) {
            census_[w & 3] = local;
            census_write_.store(w + 1, std::memory_order_release);
        }
    }
    return &s;
}
// ---- Amendment 18: the docking approach (docking-approach.md; ship_avoidance_core.h "Amendment 18") ---------------------------------
// X3M_SHIP_STEERING_DOCK (default on; needs X3M_SHIP_AVOIDANCE): site E's own-container row (suppress_dock / pass_dock_*), the order's
// container kept as a part obstacle on order 4 (stations and carriers) and on 0x1d (stations), the carrier's hull parts through the
// station part walk, the ship_avoid_dock row (--debug) and the counters. Off: today's rules exactly (the target excluded on order 4, no
// row at E). No new hook site; reads only; game thread; no allocation.
bool dock_on_ = false;
const char* dock_state_ = "off";
Counter e_suppressed_dock_, e_pass_dock_[unsigned(av::DockE::count)], dock_part_obstacles_, dock_detours_, idle_steered_;
Counter dock_models_built_, dock_no_model_, dock_rows_dropped_;
std::uint32_t dock_e_max_ticks_ = 0;
std::uint32_t dock_row_ms_[core::kShipCapacity]; // per table slot: the last ship_avoid_dock row (1 Hz per ship)
float dock_w_[core::kShipCapacity][3];          // per table slot: the container's velocity of the last order-4 decision (0: none)
struct DockRow {
    std::uint32_t ship, cont, t_ms;
    std::uint16_t order, cls;
    std::uint8_t parts_ok, verdict, zone, kept, detour, pad[3];
    unsigned n_parts;
    float path_m, ray_m, margin_m, reach_m, d_c_m, r_c_m, v_ms;
};
DockRow dock_rows_[32];
std::atomic<std::uint32_t> dock_row_write_{0};
std::uint32_t dock_row_read_ = 0;
// The container's position (m), radius (m) and velocity (m/s: a carrier's p+0x100 + obj+0x60, saturated as in the walk; a station's 0).
// False when unreadable, deleted or attached.
constexpr float kDockOmegaCapRad = 0.5f; // a carrier's angular velocity used for its parts, at most (rad/s)
inline bool dock_container_state(std::uint32_t cont, std::uint16_t cls, float pos[3], float* r_m, float w[3], float* omega = nullptr) noexcept {
    const std::uint32_t ph = rd32(cont + sites::kObjPhys);
    if (!ph || (rd32(cont + sites::kObjFlags) & (sites::kFlagDeleted | sites::kFlagAttached))) return false;
    *r_m = float(rds32(cont + sites::kObjRadius)) / core::kUnitsPerMetre;
    if (!(*r_m > 0.0f)) return false;
    for (unsigned i = 0; i < 3; ++i) {
        pos[i] = float(rds32(ph + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
        w[i] = 0.0f;
        if (cls == sites::kClassShip) {
            const std::int64_t sum = std::int64_t(rds32(ph + sites::kPhysVelocity + 4 * i)) + std::int64_t(rds32(cont + sites::kObjSlip + 4 * i));
            const std::int32_t sat = sum > 2147483647 ? 2147483647 : sum < -2147483647 - 1 ? -2147483647 - 1 : std::int32_t(sum);
            w[i] = float(sat) / core::kUnitsPerMetre;
        }
    }
    if (omega) { // a carrier's turn: its rates and flight basis (0 for a station or without a flight block)
        omega[0] = omega[1] = omega[2] = 0.0f;
        const std::uint32_t fl = cls == sites::kClassShip ? rd32(cont + sites::kObjFlight) : 0;
        if (fl) {
            float rates[3], rows[3][3];
            for (unsigned i = 0; i < 3; ++i) rates[i] = float(rds32(cont + sites::kObjRates + 4 * i)) * core::kRadPerBinary;
            for (unsigned r = 0; r < 3; ++r)
                for (unsigned c = 0; c < 3; ++c) rows[r][c] = fixed16(rds32(fl + sites::kFlightBasis + 0x10 * r + 4 * c));
            core::ship_angular_velocity(rates, rows, omega);
            if (!core::finite_f(omega[0]) || !core::finite_f(omega[1]) || !core::finite_f(omega[2])) omega[0] = omega[1] = omega[2] = 0.0f;
            const float om2 = core::dot3(omega, omega); // review 4: at most 0.5 rad/s (a misread rate must not throw the parts around)
            if (om2 > kDockOmegaCapRad * kDockOmegaCapRad) {
                const float k = kDockOmegaCapRad / core::sqrt_ss(om2);
                for (unsigned i = 0; i < 3; ++i) omega[i] *= k;
            }
        }
    }
    return true;
}
// The container's current part model. A station: the station part walk, cached on (object, root radius, position and basis) and pinned
// for the current snapshot (PartCache). A carrier (review 5): walked into its own record at most every kDockRebuildMs and extrapolated
// in between by the velocity and angular velocity read at the walk; a failed walk (station or carrier) is not retried for kDockRetryMs.
// Null = no valid model (the fallback: today's rules for the pair).
constexpr std::uint32_t kDockRebuildMs = 250, kDockRetryMs = 5000;
struct DockModelRec {
    std::uint32_t cont, built_ms, fail_ms, generation;
    float C0[3], w[3], om[3];
    av::PartSet base, cur;
};
DockModelRec dock_models_[8];
inline DockModelRec& dock_rec(std::uint32_t cont, std::uint32_t now) noexcept {
    unsigned oldest = 0;
    std::uint32_t oldest_age = 0;
    for (unsigned i = 0; i < 8; ++i) {
        if (dock_models_[i].cont == cont) return dock_models_[i];
        const std::uint32_t last = dock_models_[i].built_ms > dock_models_[i].fail_ms ? dock_models_[i].built_ms : dock_models_[i].fail_ms;
        const std::uint32_t age = dock_models_[i].cont ? now - last : 0xffffffffu;
        if (age >= oldest_age) {
            oldest_age = age;
            oldest = i;
        }
    }
    DockModelRec& r = dock_models_[oldest];
    r.cont = cont;
    r.built_ms = r.fail_ms = 0;
    r.base.count = r.cur.count = 0;
    return r;
}
inline const av::PartSet* dock_model(std::uint32_t cont, std::uint16_t cls, const float pos[3], float r_m, const float w[3], const float om[3],
                                     std::uint32_t now) noexcept {
    const std::uint32_t ph = rd32(cont + sites::kObjPhys);
    if (!ph) return nullptr;
    DockModelRec& rec = dock_rec(cont, now);
    if (rec.fail_ms && now - rec.fail_ms < kDockRetryMs) return nullptr; // a failed walk waits (the width read's rule)
    std::uint32_t generation;
    std::memcpy(&generation, &r_m, 4);
    RawMem mem;
    if (cls != sites::kClassShip) { // a station: the shared cache
        wk::Stats st;
        const unsigned idx = wk::station_parts(mem, part_cache_, cont, ph, pos, r_m, nullptr, st);
        if (st.part_built) dock_models_built_.add(st.part_built);
        if (st.part_pinned_full) dock_parts_evicted_.add(st.part_pinned_full);
        if (idx >= av::kStationCapacity) {
            dock_no_model_.add();
            rec.fail_ms = now ? now : 1;
            return nullptr;
        }
        rec.fail_ms = 0;
        return &part_cache_.sets[idx];
    }
    if (rec.built_ms && rec.base.count && rec.generation == generation && now - rec.built_ms < kDockRebuildMs) {
        const float dt = float(std::int32_t(now - rec.built_ms)) * 0.001f;
        rec.cur = rec.base;
        for (unsigned p = 0; p < rec.base.count; ++p) {
            float vp[3];
            av::dock_part_velocity(rec.w, rec.om, rec.C0, rec.base.c[p], vp);
            for (unsigned k = 0; k < 3; ++k) rec.cur.c[p][k] = rec.base.c[p][k] + vp[k] * dt;
        }
        return &rec.cur;
    }
    wk::WalkOut seen;
    if (!wk::part_walk(mem, ph, pos, r_m, rec.base, nullptr, &seen)) {
        dock_no_model_.add();
        rec.fail_ms = now ? now : 1;
        rec.base.count = 0;
        return nullptr;
    }
    dock_models_built_.add();
    rec.fail_ms = 0;
    rec.built_ms = now ? now : 1;
    rec.generation = generation;
    rec.base.station = cont;
    rec.base.generation = generation;
    for (unsigned k = 0; k < 3; ++k) {
        rec.C0[k] = pos[k];
        rec.w[k] = w[k];
        rec.om[k] = om[k];
    }
    rec.cur = rec.base;
    return &rec.cur;
}
// A's reads for the row: position, unit forward (flight basis row 2), speed, slip (zeroed when not usable, as decide() does).
struct DockSelf {
    float x[3], f[3], u[3], v, r;
};
inline bool dock_self(std::uint32_t a, std::uint32_t fl, DockSelf& d) noexcept {
    const std::uint32_t pa = rd32(a + sites::kObjPhys);
    if (!pa) return false;
    d.v = float(rds32(a + sites::kObjSpeed)) / core::kUnitsPerMetre;
    d.r = float(rds32(a + sites::kObjRadius)) / core::kUnitsPerMetre;
    float slip[3];
    for (unsigned i = 0; i < 3; ++i) {
        d.x[i] = float(rds32(pa + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
        d.f[i] = fixed16(rds32(fl + sites::kFlightBasis + 0x20 + 4 * i));
        slip[i] = float(rds32(a + sites::kObjSlip + 4 * i)) / core::kUnitsPerMetre;
    }
    const float v_bound = d.v > 1.0f ? d.v : 1.0f; // E has no v_max at hand: a slip beyond 2 x the current speed is not the lag
    if (!core::slip_usable(slip, v_bound)) slip[0] = slip[1] = slip[2] = 0.0f;
    for (unsigned i = 0; i < 3; ++i) d.u[i] = d.v * d.f[i] + slip[i];
    return core::finite_f(d.v) && d.v >= 0.0f && core::finite_f(d.r);
}
inline std::uint8_t dock_zone(float d_c, float r_c, std::uint16_t cls) noexcept { // 0 out (> 2 r), 1 near, 2 in (<= r), 3 inner carrier
    if (cls == sites::kClassShip && d_c <= av::kDockCarrierInner * r_c) return 3;
    return d_c > 2.0f * r_c ? 0 : d_c > r_c ? 1 : 2;
}
// Site E's own-container row for (A on order 4, B = f+0x224 a container): the part model current, the clearances over the engine's
// reach, the verdict. none: a carrier within 0.8 r_C (today's rules). The final zone is an order-5 phase (section 11.5): on order 4 A
// is never in it (the row applies on order 4 only).
inline av::DockE dock_e(std::uint32_t a, std::uint32_t fl, std::uint32_t b, std::uint16_t cls_b, const core::Ship& s) noexcept {
    const std::uint64_t t0 = timing_ ? qpc() : 0;
    DockSelf me;
    float C[3], w[3], om[3], r_c = 0.0f;
    if (!dock_self(a, fl, me) || !dock_container_state(b, cls_b, C, &r_c, w, om)) return av::DockE::no_parts;
    const float dc[3] = {me.x[0] - C[0], me.x[1] - C[1], me.x[2] - C[2]};
    if (cls_b == sites::kClassShip && core::dot3(dc, dc) <= (av::kDockCarrierInner * r_c) * (av::kDockCarrierInner * r_c)) return av::DockE::none;
    const av::PartSet* set = dock_model(b, cls_b, C, r_c, w, om, game_time_ms());
    av::DockClear c;
    const float v_close = cls_b == sites::kClassShip ? av::dock_closing_speed(me.x, me.f, me.v, C, w) : -1.0f; // review 1
    if (set) c = av::dock_clearance(*set, w, me.x, me.f, me.u, me.v, s.a_lin, om, C, v_close);
    const av::DockE v = av::dock_verdict(set != nullptr, false, c, me.r);
    if (timing_) {
        const std::uint64_t dt = qpc() - t0;
        const std::uint32_t ticks = dt > 0xffffffffull ? 0xffffffffu : std::uint32_t(dt);
        if (ticks > dock_e_max_ticks_) dock_e_max_ticks_ = ticks;
    }
    return v;
}
// avoid_adjust's binding (items 2-4): the order's container kept as a part obstacle. A station: its snapshot model (resolve checks it);
// a carrier outside 0.8 r_C: its hull parts built here from the live pose. Counts 0x1d near its station and, under --debug, fills the
// 1 Hz row (pushed by the caller after the resolve with kept / detour).
inline bool dock_bind(av::Self& self, std::uint32_t obj, const core::Inputs& in, float a_lin, float r_own, unsigned slot, std::uint32_t now,
                      DockRow& r) noexcept {
    const std::uint32_t cont = self.target;
    if (!cont) return false;
    const std::uint16_t cls = rd16(cont + sites::kObjClass);
    if (!av::dock_container(in.order, cls, rd32(cont + sites::kObjFlags))) return false; // review 2: a ship only when dock-capable
    float C[3], w[3], om[3], r_c = 0.0f;
    if (!dock_container_state(cont, cls, C, &r_c, w, om)) return false;
    const float dc[3] = {in.x[0] - C[0], in.x[1] - C[1], in.x[2] - C[2]};
    const float d_c = core::sqrt_ss(core::dot3(dc, dc));
    if (in.order == core::kOrderIdle && d_c <= av::kDockIdleNear * r_c) idle_steered_.add();
    const bool inner = cls == sites::kClassShip && d_c <= av::kDockCarrierInner * r_c;
    const av::PartSet* set = nullptr;
    if (!inner) {
        self.dock_target = cont;
        self.dock_cap_P = in.order == core::kOrderFlyBefore;
        if (cls == sites::kClassShip) {
            set = dock_model(cont, cls, C, r_c, w, om, now);
            self.dock_set = set;
            for (unsigned i = 0; i < 3; ++i) self.dock_omega[i] = om[i];
            if (!set) self.dock_target = 0; // no hull model: today's rule (the target excluded on order 4)
        }
    }
    if (!debug_ || (dock_row_ms_[slot] && now - dock_row_ms_[slot] < 1000)) return false;
    dock_row_ms_[slot] = now ? now : 1;
    if (!set && !inner) set = dock_model(cont, cls, C, r_c, w, om, now); // a station's model for the row (the cache hit of this frame's walk)
    r = DockRow{};
    r.ship = obj;
    r.cont = cont;
    r.t_ms = now;
    r.order = in.order;
    r.cls = cls;
    r.zone = dock_zone(d_c, r_c, cls);
    r.d_c_m = d_c;
    r.r_c_m = r_c;
    r.v_ms = in.v;
    r.margin_m = av::dock_margin_m(r_own);
    r.parts_ok = set ? 1 : 0;
    r.n_parts = set ? set->count : 0;
    r.path_m = r.ray_m = -1.0f;
    const bool row_e = in.order == core::kOrderFlyBefore && !inner; // what E's row would return now (order 4 only)
    if (set) {
        const float u[3] = {in.v * in.row[2][0] + in.slip[0], in.v * in.row[2][1] + in.slip[1], in.v * in.row[2][2] + in.slip[2]};
        const float v_close = cls == sites::kClassShip ? av::dock_closing_speed(in.x, in.row[2], in.v, C, w) : -1.0f;
        const av::DockClear c = av::dock_clearance(*set, w, in.x, in.row[2], u, in.v, a_lin, om, C, v_close);
        r.path_m = c.path_m;
        r.ray_m = c.ray_m;
        r.reach_m = c.reach_m;
        r.verdict = std::uint8_t(row_e ? av::dock_verdict(true, false, c, r_own) : av::DockE::none);
    } else
        r.verdict = std::uint8_t(row_e ? av::DockE::no_parts : av::DockE::none);
    return true;
}
inline void dock_row_push(const DockRow& r) noexcept {
    const std::uint32_t wv = dock_row_write_.load(std::memory_order_relaxed);
    if (wv - dock_row_read_ >= 32) {
        dock_rows_dropped_.add();
        return;
    }
    dock_rows_[wv & 31] = r;
    dock_row_write_.store(wv + 1, std::memory_order_release);
}
// The avoidance adjustment of the order's point for one steering call (av::resolve over the sector snapshot).
void avoid_adjust(std::uint32_t obj, std::uint32_t fl, const core::Inputs& in, const core::Dynamics& dyn, std::uint32_t code,
                  unsigned slot, std::uint32_t now, float P[3], std::uint32_t* v_emergency_out, const ln::Pass* lane) noexcept {
    const std::uint32_t sector = rd32(obj + sites::kObjSector);
    av_calls_.add();
    if (!sector) return;
    const av::Snapshot* snap = snapshot_for(sector, now);
    av::Self self;
    for (unsigned c = 0; c < 3; ++c) {
        self.x[c] = in.x[c];
        self.f[c] = in.row[2][c];
        self.slip[c] = in.slip[c]; // amendment 14: the swept path along u = v f + slip (decide() read obj+0x60 once)
    }
    self.up[0] = 0.0f;
    self.up[1] = 1.0f;
    self.up[2] = 0.0f;
    self.v = in.v;
    self.r = dyn.r_own;
    self.a = dyn.a_lin;
    self.omega = core::fmin_f(dyn.W[0] > 0.0f ? dyn.W[0] : 1e30f, dyn.W[1] > 0.0f ? dyn.W[1] : 1e30f);
    if (self.omega >= 1e29f) self.omega = 0.0f;
    self.alpha = self.omega * dyn.k;
    self.ptr = obj;
    self.target = rd32(fl + sites::kFlightTarget);
    self.leader = (in.order == core::kOrderEscort || in.order == core::kOrderFollow) ? self.target : 0;
    self.leader_x[0] = self.leader_x[1] = self.leader_x[2] = 0.0f; // diagnostics only since amendment 11: not read here
    self.L_leader = 0.0f;
    if (self.leader) {
        const core::Ship* ls = table_.find(self.leader);
        if (ls) {
            const av::ShipAvoid& la = avoid_[unsigned(ls - table_.entries)];
            if (la.obj == self.leader && now - la.stamp_ms <= sites::kClampMaxAgeMs) self.L_leader = la.L; // a fresh decision only
        }
    }
    self.order = in.order;
    self.capital = code < core::kClassCount && ((core::kCapitalMask >> code) & 1u);
    self.exclude_target = in.order == core::kOrderFlyBefore || in.order == core::kOrderDock || in.order == core::kOrderEscort ||
                          in.order == core::kOrderFollow || in.order == core::kOrderAttack || in.order == core::kOrderAttackBig ||
                          in.order == core::kOrderCapture;
    // amendment 18 (docking-approach.md items 2-4): the order's container kept as a part obstacle (order 4, 0x1d at a station)
    DockRow dock_row;
    const bool dock_row_due = dock_on_ && dock_bind(self, obj, in, dyn.a_lin, dyn.r_own, slot, now, dock_row);
    // amendment 16 / 16b (section 16.1, 16.10): a capital committed to its gate gets the lane rule against the capitals committed to
    // the same gate, from this decision's pass (decide(): the same pass site B takes the caps from)
    av::lane_bind(self, lane);
    // the gate traffic controller (D.1): a capital member's partners with a clear verdict (at most kExcludeMaxAgeMs old) are not
    // obstacles; a handed-back (unresolved), unknown or stale pair keeps today's sphere avoidance
    std::uint32_t ctl_ptr[gc::kMembers], ctl_id[gc::kMembers];
    if (self.capital && traffic_live_.load(std::memory_order_relaxed)) {
        int mi = -1;
        const gc::Gate* cg = ctl_lookup(obj, &mi);
        if (cg && cg->m[mi].id == rd32(obj + sites::kObjId)) {
            for (unsigned j = 0; j < cg->n; ++j)
                // review F4: neither ship on the engine's own avoid order (A's live order, B's last read)
                if (int(j) != mi && in.order != core::kOrderAvoid && cg->m[j].st.order != core::kOrderAvoid &&
                    gc::pair_verdict(*cg, unsigned(mi), j, now, gc::kExcludeMaxAgeMs) == gc::PairV::clear) {
                    ctl_ptr[self.n_ctl] = cg->m[j].ship;
                    ctl_id[self.n_ctl] = cg->m[j].id;
                    ++self.n_ctl;
                }
            self.ctl_ptr = ctl_ptr;
            self.ctl_id = ctl_id;
        }
    }
    av::ShipAvoid& state = avoid_[slot];
    if (state.obj != obj) state = av::ShipAvoid{}; // the slot was another ship's: no stale sides or resolved set
    state.obj = obj;
    const bool was_unresolved = state.unresolved;
    const av::Result r = av::resolve(*snap, part_cache_.sets, part_cache_.count, self, P, state, now);
    if (r.conflicts) av_conflicts_.add(r.conflicts);
    if (r.unresolved) {
        av_unresolved_.add();
        av_unresolved_why_[r.reason < unsigned(av::Unresolved::count) ? r.reason : 0u].add();
    }
    // episodes: a conflict (or an unresolved call) counts once per run of consecutive calls, not per call
    const bool conflict_now = r.conflicts > 0;
    if (conflict_now && !state.in_conflict) av_conflict_episodes_.add();
    if (r.unresolved && !was_unresolved) av_unresolved_episodes_.add();
    state.in_conflict = conflict_now;
    if (r.emergency) av_emergency_.add();
    if (r.excluded_followers) avoid_excluded_followers_.add(r.excluded_followers);
    if (r.skipped_small) avoid_skipped_small_.add(r.skipped_small);
    if (r.excluded_members) avoid_excluded_member_.add(r.excluded_members);
    if (r.lane_exempt) lane_exempt_.add(r.lane_exempt);
    if (r.lane_follow) lane_follow_.add(r.lane_follow);
    if (r.lane_cross) lane_cross_.add(r.lane_cross);
    if (r.lane_apart) lane_apart_.add(r.lane_apart);
    if (r.ctl_excluded) avoid_ctl_excluded_.add(r.ctl_excluded);
    if (r.dock_kept) {
        dock_part_obstacles_.add();
        if (r.adjusted && r.obstacle == self.dock_target) dock_detours_.add();
    }
    if (dock_row_due) {
        dock_row.kept = std::uint8_t(r.dock_kept ? 1 : 0);
        dock_row.detour = std::uint8_t(r.adjusted && r.obstacle == self.target ? 1 : 0);
        dock_row_push(dock_row);
    }
    std::memcpy(v_emergency_out, &state.v_emergency, 4);
    if (!r.adjusted) return;
    av_detours_.add();
    P[0] = r.P[0];
    P[1] = r.P[1];
    P[2] = r.P[2];
    if (debug_) {
        const std::uint32_t w = detour_write_.load(std::memory_order_relaxed);
        if (w - detour_read_ < 16) {
            DetourEvent& e = detours_[w & 15];
            e.obj = obj;
            e.obstacle = r.obstacle;
            e.kind = r.obstacle_kind;
            e.side = std::uint32_t(std::int32_t(r.side));
            e.vertical = r.vertical ? 1 : 0;
            e.order = in.order;
            e.obstacle_leader = r.obstacle_leader;
            e.obstacle_order = r.obstacle_order;
            e.t_cpa = r.t_cpa;
            e.miss = r.miss;
            e.C = r.C;
            e.L = r.L;
            for (unsigned c = 0; c < 3; ++c) {
                e.P[c] = r.P[c];
                e.x[c] = in.x[c];
            }
            detour_write_.store(w + 1, std::memory_order_release);
        }
    }
}
// The --debug gate row: at a launch (the whole approach) or at a capital's crossing (the alignment at the grid point).
void gate_event(const GateState& g, std::uint32_t obj, std::int32_t v0, std::uint32_t cross_to_launch) noexcept {
    const std::uint32_t w = gate_event_write_.load(std::memory_order_relaxed);
    if (w - gate_event_read_ >= 8) {
        row_dropped(Drop::gate_event);
        return;
    }
    GateEvent& e = gate_events_[w & 7];
    e.obj = obj;
    e.code = 0;
    const std::int32_t type = std::int16_t(rd16(obj + sites::kObjType));
    const std::uint32_t table = rd32(sites::kShipTypeTableSlot);
    if (table && type >= 0) e.code = rd32(table + std::uint32_t(type) * sites::kRecordStride + sites::kRecordClassCode);
    e.cross_to_launch_ms = cross_to_launch;
    e.v0_units = v0;
    e.cross_v = g.crossed ? g.cross_v : -1.0f;
    e.cross_err_deg = g.crossed ? g.cross_err_deg : -1.0f;
    e.off[0] = g.off[0];
    e.off[1] = g.off[1];
    e.capital = g.capital;
    e.spread_k = g.spread_k;
    gate_event_write_.store(w + 1, std::memory_order_release);
}
// Amendment 15: the frame-end predictor's registry (a capital in join mode registers its table slot on every decision until it is
// in; refused while full, it keeps v_cmd 100 until a slot frees).
inline void join_register(unsigned slot) noexcept {
    GateState& g = gate_[slot];
    if (!g.join_registered) g.join_registered = join_reg_.add(slot) ? 1 : 0;
}
// The KC grid slot (0..19) of a validated capital point: a in {0, 1, -1, 2, -2} -> 0..4, b in {0, -1, -2, -3} -> rows.
inline std::uint8_t grid_slot_of(const float A_local_r[3], float r_gate) noexcept {
    const std::int32_t a = core::round_to_int(A_local_r[0] * r_gate / core::kGateGridPitchM), b = core::round_to_int(A_local_r[1] * r_gate / core::kGateGridPitchM);
    static const std::int8_t col[5] = {4, 2, 0, 1, 3}; // a = -2, -1, 0, 1, 2
    if (a < -2 || a > 2 || b < -3 || b > 0) return 255;
    return std::uint8_t(-b * 5 + col[a + 2]);
}
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
// ship_gate_capital_latch (--debug, every capital, section 16.2): at each pred_none / slow_join / unlatch transition of the
// frame-end predictor, its inputs, the four candidates, the ship's current detour obstacle and its lane verdicts against every
// committed neighbour (the stored board; the ship is never dereferenced here).
void latch_row(const GateState& g, unsigned slot, const core::JoinEval& ev) noexcept {
    const std::uint32_t w = latch_row_write_.load(std::memory_order_relaxed);
    if (w - latch_row_read_ >= 8) {
        row_dropped(Drop::latch);
        return;
    }
    LatchRow& r = latch_rows_[w & 7];
    r = LatchRow{};
    r.obj = g.obj;
    r.code = g.join_code;
    r.obstacle = slot < core::kShipCapacity && avoid_[slot].obj == g.obj ? avoid_[slot].obstacle : 0;
    r.event = ev.unlatched ? 2 : ev.slow_entered ? 1 : 0; // 0 pred_none (the fallback), 1 slow_join, 2 unlatch
    r.before = std::uint8_t(ev.before);
    r.after = std::uint8_t(ev.after);
    r.slot = g.grid_slot;
    r.plane = g.join.plane;
    r.params = g.join.params;
    r.v_cmd = g.join.v_cmd;
    for (unsigned i = 0; i < core::kJoinCandidateCount; ++i) r.cand[i] = ev.cand[i];
    if (const ln::Gate* lg = ln::gate_of(lanes_, g.gate)) {
        const ln::Entry* me = ln::find(*lg, g.obj);
        const std::uint32_t now = game_time_ms(), fresh = av::fresh_window_ms(av::kSteeredFreshMs, frame_step_ms(), av::kSteeredReplanMs);
        if (me) {
            for (unsigned i = 0; i < lg->n && r.n_lanes < 6; ++i) {
                const ln::Entry& o = lg->e[i];
                if (&o == me || !ln::committed_stored(o, now, fresh)) continue;
                ln::PairInfo info;
                r.lane_verdict[r.n_lanes] = std::uint8_t(ln::verdict_sym(*lg, *me, o, now, fresh, seq_on_, &info));
                r.lane_other[r.n_lanes] = o.ship;
                r.lane_gap[r.n_lanes] = info.gap;
                ++r.n_lanes;
            }
        }
    }
    latch_row_write_.store(w + 1, std::memory_order_release);
}
// ---- amendment 16: the lane board's per-decision work (capital-gate-transit.md section 16.1) -----------------------------
// The hull's lateral half-width (m) and its source (section 16.1 with the ratification addendum 2026-10-13 (5); the chain and
// guards of render-node-bounds.md section 6.1): the descendants of the root node [obj+0x70] with a body id > 0 and a collision
// tree, each model found read-only through the body cache hash, the per-axis max over all its LOD records times the node scale,
// placed by the node offsets (wk::hull_width). Any failed guard: w = r (`fail`). Plain reads on the game thread at the decision
// site (the cache hash has no lock), never an engine call; only w is cached (models may be freed).
__attribute__((always_inline)) inline float capital_half_width(std::uint32_t obj, float r_m, ln::WidthSrc* src, float* len_m,
                                                              wk::HullWidth* full) noexcept {
    RawMem mem;
    *full = wk::hull_width(mem, obj, r_m);
    *src = full->src;
    *len_m = full->half_len_m;
    return full->w_m;
}
// Cached per ship on its gate state with the radius bits [obj+0xa4] and the root node [obj+0x70]; a failed read (a body not in the
// cache yet, any guard) is retried every 5 s at most 3 times, then w = r until the radius generation or the root changes (review
// F4: ln::width_reread). One census row per capital type (--debug) once the read is final (lod, or failed after the retries).
inline void width_ensure(GateState& g, std::uint32_t obj, std::uint32_t code, float r_m, std::uint32_t now) noexcept {
    const std::int32_t gen = rds32(obj + sites::kObjRadius);
    const std::uint32_t root = rd32(obj + sites::kObjPhys);
    const bool cached = g.w > 0.0f && g.w_gen == gen && g.w_root == root;
    if (!ln::width_reread(cached, g.w_src, g.w_tries, g.w_read_ms, now)) return;
    g.w_tries = cached ? std::uint8_t(g.w_tries + 1) : 0;
    ln::WidthSrc src = ln::WidthSrc::unread;
    float len = 0.0f;
    wk::HullWidth hw;
    float w = capital_half_width(obj, r_m, &src, &len, &hw);
    if (!(w > 0.0f) || !core::finite_f(w) || !(w <= r_m)) {
        w = r_m;
        if (src == ln::WidthSrc::lod) src = ln::WidthSrc::failed;
    }
    if (src != ln::WidthSrc::lod) src = ln::WidthSrc::failed; // w = r: retried while due
    g.w = w;
    g.w_src = std::uint8_t(src);
    g.w_gen = gen;
    g.w_root = root;
    g.w_read_ms = now;
    g.w_len = src == ln::WidthSrc::lod ? len : 0.0f;
    if (traffic_on_ && src == ln::WidthSrc::lod) type_width_put(std::int16_t(rd16(obj + sites::kObjType)), w, len); // the arrival placement's w
    if (!debug_ ||(src != ln::WidthSrc::lod && g.w_tries < ln::kWidthRetries)) return;
    const std::int32_t type = std::int16_t(rd16(obj + sites::kObjType));
    for (unsigned i = 0; i < width_census_count_; ++i)
        if (width_census_types_[i] == type) return;
    if (width_census_count_ >= 32) return;
    const std::uint32_t wr = width_census_write_.load(std::memory_order_relaxed);
    if (wr - width_census_read_ >= 8) return; // the ring is full: logged at a later decision of the type
    width_census_types_[width_census_count_++] = type;
    const bool lod = src == ln::WidthSrc::lod;
    width_census_[wr & 7] = WidthCensus{std::uint32_t(type), code, r_m, w, g.w_len, std::uint8_t(src), g.w_tries, lod ? hw.widest_id : 0,
                                        lod ? hw.widest_lod : -1, lod ? hw.widest_o[0] : 0.0f, lod ? hw.widest_o[1] : 0.0f,
                                        lod ? hw.widest_e[0] : 0.0f, lod ? hw.widest_e[1] : 0.0f};
    width_census_write_.store(wr + 1, std::memory_order_release);
}
// ship_capital_lane (--debug, traced A): the decision's pass verdict against every committed neighbour, at 1 Hz per pair and on
// every verdict or route change; the fields from the stored board in A's direction (amendment 16b: section 16.10.6).
void lane_rows(const ln::Pass& p) noexcept {
    const ln::Gate& lg = *p.g;
    const ln::Entry& me = *p.self;
    const std::uint32_t now = p.now;
    ln::Side a;
    ln::side_of(lg, me, nullptr, nullptr, me.order, now, &a);
    for (unsigned i = 0; i < lg.n; ++i) {
        const ln::Entry& o = lg.e[i];
        const ln::Verdict v = p.v[i];
        if (&o == &me || v == ln::Verdict::none) continue;
        unsigned k = 0;
        while (k < 32 && !(lane_seen_[k].a == me.ship && lane_seen_[k].b == o.ship)) ++k;
        if (k < 32 && lane_seen_[k].verdict == std::uint8_t(v) && lane_seen_[k].route == std::uint8_t(p.route[i]) && now - lane_seen_[k].ms < 1000)
            continue;
        const std::uint32_t w = lane_row_write_.load(std::memory_order_relaxed);
        if (w - lane_row_read_ >= 16) {
            row_dropped(Drop::lane);
            return;
        }
        if (k == 32) {
            k = lane_seen_next_;
            lane_seen_next_ = (lane_seen_next_ + 1) & 31;
        }
        lane_seen_[k] = LaneSeen{me.ship, o.ship, now, std::uint8_t(v), std::uint8_t(p.route[i])};
        ln::Side b;
        ln::side_of(lg, o, nullptr, nullptr, o.order, now, &b);
        ln::PairInfo info;
        (void)ln::pair_verdict16b(a, b, p.fresh_ms, p.seq, &info);
        LaneRow& r = lane_rows_[w & 15];
        r = LaneRow{};
        r.a = me.ship;
        r.b = o.ship;
        r.verdict = std::uint8_t(v);
        r.route = std::uint8_t(p.route[i]);
        r.watch = info.watch ? 1 : 0;
        r.held = ln::lane_state(a) == 2 ? 1 : 0; // A in lane by the hysteresis only (section 16.11)
        r.foot_a = std::uint8_t(info.foot_a);
        r.foot_b = std::uint8_t(info.foot_b);
        r.w_src_a = me.w_src;
        r.w_src_b = o.w_src;
        r.gap = info.gap;
        r.w_a = me.w;
        r.w_b = o.w;
        const float dq[2] = {a.q[0] - me.lane[0], a.q[1] - me.lane[1]};
        r.e_a = core::sqrt_ss(dq[0] * dq[0] + dq[1] * dq[1]);
        r.theta_a = core::acos_deg(me.cos_t > 1.0f ? 1.0f : me.cos_t < -1.0f ? -1.0f : me.cos_t);
        r.ds = info.ds;
        r.v_cap = -1.0f;
        r.D = -1.0f;
        r.lat_gap = info.lat_gap;
        r.mot = info.mot_a + info.mot_b;
        r.trail = info.trail == 1 ? me.ship : info.trail == 2 ? o.ship : 0u;
        r.h_t = info.trail == 2 ? o.h : me.h;
        r.h_l = info.trail == 2 ? me.h : o.h;
        if (v == ln::Verdict::follow && info.ds > 0.0f) { // this ship trails a (b) pair: its cap against this leader (the only lane cap)
            r.D = ln::follow_reach(a.vF, b.vF, me.a_lin, me.r, o.r);
            r.v_cap = ln::follow_cap(b.vF, me.a_lin, info.ds, r.D, o.a_lin);
            r.bind = p.follow_cap >= 0.0f && p.follow_leader == o.ship ? 1 : 0;
        }
        lane_row_write_.store(w + 1, std::memory_order_release);
    }
}
// One capital decision on a validated approach (order 4 under the capital policy, or order 5): the board entry (refreshed after
// this decision's pass, for the next decision and the other ships) and the trace rows of the pass.
void lane_decision(GateState& g, std::uint32_t obj, const core::Inputs& in, const core::Dynamics& dyn, const float A[3], const float G[3],
                   const float row0[3], const float row1[3], const float row2[3], core::JoinMode mode, std::uint32_t now, const ln::Pass* pass) noexcept {
    width_ensure(g, obj, g.join_code, dyn.r_own, now);
    ln::Decision d;
    d.ship = obj;
    d.id = g.id;
    d.gate = g.gate;
    d.order = in.order;
    d.mode = std::uint8_t(mode);
    d.pred = (mode == core::JoinMode::join || mode == core::JoinMode::slow_join) && g.join.eval_ms && g.join.pred_reached ? 1 : 0;
    for (unsigned i = 0; i < 3; ++i) {
        d.G[i] = G[i];
        d.R[i] = row0[i];
        d.U[i] = row1[i];
        d.F[i] = row2[i];
        d.x[i] = in.x[i];
        d.f[i] = in.row[2][i];
        d.u[i] = in.v * in.row[2][i] + in.slip[i];
        d.A[i] = A[i];
    }
    d.r = dyn.r_own;
    d.w = g.w;
    d.w_src = g.w_src;
    d.a_lin = dyn.a_lin;
    d.e_max = g.join.pred_e_max;
    d.over = g.join.pred_over;
    d.pred_nose = g.join.pred_nose;
    ln::seq_rates(dyn, in.w, &d.omega, &d.alpha);
    d.l = g.w_src == std::uint8_t(ln::WidthSrc::lod) ? g.w_len : 0.0f;
    if (pass && pass->g && trace_on_ && debug_ && trace_slot_of(obj)) lane_rows(*pass); // the pass's state, before the refresh
    ln::refresh(lanes_, d, now);
}
// The gate traffic controller's commit refresh (replaces lane_decision under ship_steering_gates_traffic; the lane board's commit
// test: an order-4 / order-5 decision on the validated capital grid geometry of the target gate): the capital's member entry on that
// gate's board (role committed), its hull (w from the width read, w = r when unread; L = max(r, l)), the order-4 line (X, h) and the
// plant parameters for its rollout, and this decision's state (site B re-reads it every kStateMs).
gc::Member* ctl_commit(GateState& g, std::uint32_t obj, const core::Inputs& in, const core::Dynamics& dyn, const core::GateGeometry* geo,
                       const float G[3], const float R[3], const float U[3], const float F[3], float r_gate, std::uint32_t gate,
                       std::uint32_t now) noexcept {
    width_ensure(g, obj, g.join_code, dyn.r_own, now);
    gc::Gate* cg = gc::acquire_gate(ctl_, gate, now, G, R, U, F, r_gate, rd16(gate + sites::kObjType));
    if (!cg) return nullptr;
    gc::Member* m = ctl_enter(*cg, obj, g.id, gc::Role::committed, gc::Why::commit, now);
    if (!m) return nullptr;
    gc::commit_fill(*m, in, dyn, geo, g.w, g.w_src == std::uint8_t(ln::WidthSrc::lod) ? g.w_len : 0.0f, r_gate, gate, now);
    return m;
}
// An escort (D.1): a capital on 0x14 / 0x15 whose leader is a committed member; it joins the leader's board (role escort). On order 0
// it stays through the formation memory (the 3 s order-0 gaps): ctl_observe. Its hull from the last width read of its type (w = r
// when the type was never read).
void ctl_escort(std::uint32_t obj, std::uint32_t leader, const core::Inputs& in, const core::Dynamics& dyn, std::uint32_t now) noexcept {
    int li = -1;
    gc::Gate* cg = ctl_lookup(leader, &li);
    if (!cg || cg->m[li].role != gc::Role::committed || !gc::live(*cg, cg->m[li], now)) return;
    // B.5: the leader is the engine's f+0x224 of this call (live); its board entry must be the same object (a reused address keeps a
    // stale entry under another id until the frame boundary removes it)
    if (!gc::leader_entry_current(cg->m[li], rd32(leader + sites::kObjId))) return;
    int ei = -1;
    gc::Gate* own = ctl_lookup(obj, &ei);
    if (own && own != cg && ei >= 0) ctl_leave(*own, unsigned(ei), false, gc::Why::leader, now); // another gate's member: it follows
    gc::Member* m = ctl_enter(*cg, obj, rd32(obj + sites::kObjId), gc::Role::escort, gc::Why::escort, now);
    if (!m) return;
    float w = 0.0f, l = 0.0f;
    if (!type_width_get(std::int16_t(rd16(obj + sites::kObjType)), &w, &l)) w = l = 0.0f;
    gc::escort_fill(*m, leader, in, dyn, w, l, cg->r_gate, now);
}
inline bool capital_obj(std::uint32_t obj) noexcept; // (below)
// ---- the steering decision -------------------------------------------
core::Reason decide(sites::Frame* f, unsigned leaf) noexcept {
    const std::uint32_t obj = f->obj;
    if (!obj || rd16(obj + sites::kObjClass) != sites::kClassShip) return core::Reason::not_ship;
    const std::uint32_t flags = rd32(obj + sites::kObjFlags);
    const bool player = (flags & sites::kFlagPlayerObject) != 0; // stage B: the player's own ship is never on a board, sequenced or steered
    if (!(flags & sites::kFlagAutopilot)) return core::Reason::no_autopilot;
    if ((flags & (sites::kFlagDeleted | sites::kFlagAttached)) || rd32(obj + sites::kObjDocked)) return core::Reason::deleted;
    const std::uint32_t fl = rd32(obj + sites::kObjFlight);
    if (!fl) return core::Reason::no_flight;
    const std::uint32_t ph = rd32(obj + sites::kObjPhys);
    if (!ph) return core::Reason::no_phys;
    const std::uint16_t order = rd16(fl + sites::kFlightOrder);
    const core::OrderPolicy policy = core::order_policy(order);
    if (!policy.steered) return core::Reason::order_not_steered;
    const std::int32_t type = std::int16_t(rd16(obj + sites::kObjType));
    const std::uint32_t table = rd32(sites::kShipTypeTableSlot);
    if (!table || type < 0) return core::Reason::no_record;
    const std::uint32_t rec = table + std::uint32_t(type) * sites::kRecordStride;
    const std::uint32_t code = rd32(rec + sites::kRecordClassCode);
    if (code < core::kClassCount && ((class_mask_ >> code) & 1u)) return core::Reason::class_excluded;
    const std::int32_t rot_pct = rds32(rec + sites::kRecordRotAccel);
    if (rot_pct <= 0) return core::Reason::no_angular_accel;
    core::Dynamics dyn;
    dyn.W[0] = float(call_eax(sites::kGetYawCap, obj)) * core::kRadPerBinary;
    dyn.W[1] = float(call_eax(sites::kGetPitchCap, obj)) * core::kRadPerBinary;
    dyn.W[2] = float(call_eax(sites::kGetRollCap, obj)) * core::kRadPerBinary;
    for (float& w : dyn.W)
        if (w < 0.0f) w = 0.0f;
    dyn.k = fixed16(rds32(fl + sites::kFlightRotAccel)) * float(rot_pct) * 0.01f;
    dyn.v_max = float(call_eax(sites::kGetMaxSpeed, obj)) / core::kUnitsPerMetre;
    dyn.a_lin = float(call_ecx(sites::kGetAccel, obj)) / core::kUnitsPerMetre;
    if (dyn.a_lin < 0.0f) dyn.a_lin = 0.0f;
    dyn.r_own = float(rds32(obj + sites::kObjRadius)) / core::kUnitsPerMetre;
    dyn.large = code < core::kClassCount && ((core::kLargeMask >> code) & 1u);
    const bool capital = code < core::kClassCount && ((core::kCapitalMask >> code) & 1u); // 0x0043fd50 (class 7 checked above)
    core::Inputs in;
    for (unsigned i = 0; i < 3; ++i) in.x[i] = float(rds32(ph + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = 0; c < 3; ++c) in.row[r][c] = fixed16(rds32(fl + sites::kFlightBasis + 0x10 * r + 4 * c));
    in.v = float(rds32(obj + sites::kObjSpeed)) / core::kUnitsPerMetre;
    for (unsigned i = 0; i < 3; ++i) in.w[i] = float(rds32(obj + sites::kObjRates + 4 * i)) * core::kRadPerBinary;
    // Amendment 14: the integrator's slip velocity obj+0x60..+0x68 (units/s, ship-movement.md section 13.2), read once per call;
    // core::steer() checks it finite and treats it as 0 on order 5 (the integrator only decays it there); avoidance uses it too
    for (unsigned i = 0; i < 3; ++i) in.slip[i] = float(rds32(obj + sites::kObjSlip + 4 * i)) / core::kUnitsPerMetre;
    if (order == core::kOrderDock || !core::slip_usable(in.slip, dyn.v_max)) // beyond 2 v_max: not the lag (glide, contact, undock)
        in.slip[0] = in.slip[1] = in.slip[2] = 0.0f;
    // The target: the caller's buffer, read only. Step 2 adjusts it here and
    // redirects EDX to a ring slot (never the caller's buffer, never f+0x240).
    const std::uint32_t target = f->edx;
    if (!target) return core::Reason::not_finite;
    const std::int32_t point[3] = {rds32(target), rds32(target + 4), rds32(target + 8)};
    const std::int32_t* adjusted = policy.avoidance ? adjust_target(obj, order, point) : point; // never 0x12 or 5
    for (unsigned i = 0; i < 3; ++i) in.P[i] = float(adjusted[i]) / core::kUnitsPerMetre;
    in.order = order;
    const std::uint32_t now = game_time_ms();
    core::Ship* s = table_.find_or_insert(obj, now);
    if (!s) return core::Reason::table_full;
    const unsigned slot = unsigned(s - table_.entries);
    // The servo budget (core::servo_tick): the measured interval since this ship's previous call for an order that
    // calls the leaf every frame, 50 ms for a gated one (Run151: per-frame orders carried a 50 ms rate surplus).
    {
        const std::uint32_t clock = rd32(sites::kGameClock);
        std::uint32_t frame_ms = clock ? rd32(clock + sites::kGameTimeOffset - 4) : 0; // [root+0x714] = the frame dt
        if (frame_ms < 1 || frame_ms > 1000) frame_ms = 1;
        const std::uint32_t gap = s->last_call_ms && now >= s->last_call_ms ? now - s->last_call_ms : 50;
        in.tick_s = core::servo_tick(gap, frame_ms, s->per_frame_run);
    }
    std::uint32_t v_emergency_bits = 0;
    float order_point[3] = {in.P[0], in.P[1], in.P[2]};
    // Amendment 16b with review F2, F5, F6: one lane pass per capital decision on order 4 under the capital policy (order 5 has no
    // avoidance and no site-B cap), from the stored board (this ship's own entry fresh, on the board of its live target): the verdicts
    // the avoidance excludes on and the trailing caps site B applies below. The ship's entry is refreshed after it (lane_decision), for
    // the next decision and the others.
    ln::Pass lane_pass;
    const ln::Pass* lane = nullptr;
    const std::uint32_t lane_fresh = av::fresh_window_ms(av::kSteeredFreshMs, frame_step_ms(), av::kSteeredReplanMs);
    if (gates_capital_on_ && !traffic_on_ && capital && order == core::kOrderFlyBefore && // the controller replaces the lane pass
        ln::run_pass(lanes_, rd32(fl + sites::kFlightTarget), obj, rd32(obj + sites::kObjId), order, now, lane_fresh, seq_on_, &lane_pass))
        lane = &lane_pass;
    if (avoidance_on_ && policy.avoidance) {
        float P[3] = {in.P[0], in.P[1], in.P[2]};
        avoid_adjust(obj, fl, in, dyn, code, slot, now, P, &v_emergency_bits, lane);
        if (P[0] != in.P[0] || P[1] != in.P[1] || P[2] != in.P[2]) {
            std::int32_t detour[3];
            const float dd[3] = {order_point[0] - in.x[0], order_point[1] - in.x[1], order_point[2] - in.x[2]};
            in.d_order = core::sqrt_ss(core::dot3(dd, dd)); // the hold and the ramp keep measuring the order's point
            in.detour = true; // amendment 14: the detour point already carries the velocity (no lateral-velocity lead)
            for (unsigned i = 0; i < 3; ++i) {
                in.P[i] = P[i];
                detour[i] = core::round_to_int(P[i] * core::kUnitsPerMetre);
            }
            redirect_target(f, detour); // a pass-through leaf (if any) sees the adjusted point too
        }
    } else if (adjusted != point)
        redirect_target(f, adjusted);
    // Step 3, P1: a non-capital ship on order 4 to a gate is steered along the lead-in line (core::gate_lead_in) with
    // no arrival ramp and the module's approach speed; the engine's point A in f+0x240 is never written. The gate
    // frame is read from the target's phys block; a layout other than the pinned one keeps the vanilla point.
    bool gate_approach = false; // the non-capital policy: the spread lead-in, no arrival ramp, the module's speed
    bool gate_lead = false;     // a lead-in geometry exists (non-capital, or a capital toward its grid point: amendment 8)
    bool gate_dock = false;     // order 5 to a gate: the cornering speed replaces the 1 m/s creep (Run152 follow-up)
    GateState* gs = nullptr;
    const std::uint32_t gate_tgt = gates_live_.load(std::memory_order_relaxed) && (order == core::kOrderFlyBefore || order == core::kOrderDock)
                                       ? rd32(fl + sites::kFlightTarget) : 0;
    const std::uint32_t gph = gate_tgt && rd16(gate_tgt + sites::kObjClass) == sites::kClassGate ? rd32(gate_tgt + sites::kObjPhys) : 0;
    float G[3] = {0, 0, 0}, row0[3] = {0, 0, 0}, row1[3] = {0, 0, 0}, row2[3] = {0, 0, 0}, r_gate = 0.0f;
    if (gph) {
        for (unsigned i = 0; i < 3; ++i) {
            G[i] = float(rds32(gph + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
            row0[i] = fixed16(rds32(gph + sites::kPhysBasis + 4 * i));
            row1[i] = fixed16(rds32(gph + sites::kPhysBasis + 0x10 + 4 * i));
            row2[i] = fixed16(rds32(gph + sites::kPhysBasis + 0x20 + 4 * i));
        }
        r_gate = float(rds32(gate_tgt + sites::kObjRadius)) / core::kUnitsPerMetre;
        gs = &gate_[slot];
        const std::uint32_t obj_id = rd32(obj + sites::kObjId);
        // another ship in the slot, or another gate: a new approach; amendment 16: under the capital policy also another object
        // behind the same pointer (the lane entry and E's gate row key on the id)
        if (gs->obj != obj || gs->gate != gate_tgt || (capital && gates_capital_on_ && gs->id != obj_id)) {
            *gs = GateState{};
            gs->obj = obj;
            gs->gate = gate_tgt;
            gs->id = obj_id;
            gs->lane_cap = -1.0f;
        }
        gs->capital = capital ? 1 : 0;
        gs->r_gate = r_gate;
        for (unsigned i = 0; i < 3; ++i) {
            gs->G[i] = G[i];
            gs->rows[0][i] = row0[i];
            gs->rows[1][i] = row1[i];
            gs->rows[2][i] = row2[i];
        }
    }
    // Amendment 13 (capital-gate-transit.md; the amendment-10 vanilla path when ship_steering_gates_capital is off or A does
    // not validate): a capital on order 4 to a gate is steered along the lead-in line through its validated grid point with
    // direction F (the gate axis, order 0x1f's target) inside the capital window (core::gate_lead_in_capital), with the
    // module's speed written in both directions there (the engine's alignment creep, 1 m/s beyond 45.6 deg off A, would
    // stall the lateral join) and no arrival ramp: the 0.75 r_gate arrival sphere (0x00454b1a; measured 823-828 m in Run156)
    // is crossed at the large cap and P2 keeps the speed when the crossing is within the 0x1f tolerance. Outside the window
    // the engine's law stands (the clamp lowers only). Its alignment at the crossing feeds `gate_capital_aligned`.
    const bool capital_gate = capital && gph != 0;
    const bool capital_policy = capital_gate && gates_capital_on_;
    bool capital_window = false; // this call's point came from the capital lead-in (inside its window): the site-B write
    bool capital_fb_write = false; // amendment 15: the fallback's slow entry toward A this call (the site-B write, both ways)
    bool capital_detour = false;   // amendment 16 fix: join / slow_join on an avoidance detour: the join's speed write is kept
    float capital_fb_v = 0.0f;
    if (gph && order == core::kOrderFlyBefore) {
        core::GateGeometry g;
        float off[3] = {0.0f, 0.0f, 0.0f};
        unsigned k = 0;
        if (!capital) {
            if (core::gate_geometry(order_point, G, row0, row1, row2, r_gate, &g)) {
                gate_lead = gate_approach = true;
                // the per-ship spread: the lead-in line shifted on the ring in the gate's x-y plane; A (the engine's
                // arrival test) is not moved
                k = core::gate_spread_index(obj);
                core::gate_spread_offset(k, core::kGateSpreadR * g.r, row0, row1, off);
                for (unsigned i = 0; i < 3; ++i) g.X[i] += off[i];
            }
        } else if (core::gate_geometry_capital(order_point, G, row0, row1, row2, r_gate, dyn.r_own, &g)) {
            // the alignment to F the rotate step needs (read at P2); the lead-in itself only under the capital policy
            float c = core::dot3(in.row[2], g.h);
            if (c > 1.0f) c = 1.0f;
            if (c < -1.0f) c = -1.0f;
            gs->last_err_deg = core::acos_deg(c);
            if (capital_policy) gate_lead = gate_approach = true; // no spread (the grid is the spread), no arrival ramp
        } else
            gs->last_err_deg = -1.0f; // no validated geometry: no alignment figure, the vanilla point
        const float dA[3] = {order_point[0] - in.x[0], order_point[1] - in.x[1], order_point[2] - in.x[2]};
        const float d_A = core::sqrt_ss(core::dot3(dA, dA));
        const float a_rel[3] = {order_point[0] - G[0], order_point[1] - G[1], order_point[2] - G[2]};
        gs->A_local[0] = r_gate > 0.0f ? core::dot3(a_rel, row0) / r_gate : 0.0f;
        gs->A_local[1] = r_gate > 0.0f ? core::dot3(a_rel, row1) / r_gate : 0.0f;
        gs->A_local[2] = r_gate > 0.0f ? core::dot3(a_rel, row2) / r_gate : 0.0f;
        gs->last_dA_r = r_gate > 0.0f ? d_A / r_gate : 0.0f;
        gs->pending = 1;
        if (r_gate > 0.0f && d_A > 2.0f * r_gate && (gs->crossed || gs->validated)) { // left the gate's 2 r sphere: a new approach
            gs->crossed = 0;
            gs->validated = 0;
        }
        if (capital_policy && !gate_lead) {
            if (!traffic_on_) ln::drop(lanes_, gate_tgt, obj); // amendment 16: no validated grid point, no lane
            else { // the controller: not committed (the same test)
                int ci = -1;
                gc::Gate* cg = ctl_lookup(obj, &ci);
                if (cg && cg->gate == gate_tgt && cg->m[ci].role == gc::Role::committed) ctl_leave(*cg, unsigned(ci), false, gc::Why::target, now);
            }
        }
        gs->validated = gate_lead ? 1 : 2; // per approach, recomputed on every order-4 call (the container detour of section
                                           // 3.4 hands the leaf another point: that call is a fallback); counted at P2
        if (gate_lead) {
            gate_lead_.add();
            gs->spread_k = std::uint8_t(k);
            gs->off[0] = core::dot3(off, row0);
            gs->off[1] = core::dot3(off, row1);
            float c = core::dot3(in.row[2], g.h);
            if (c > 1.0f) c = 1.0f;
            if (c < -1.0f) c = -1.0f;
            gs->last_err_deg = core::acos_deg(c); // read at P2 (the crossing) for the row
            if (gs->crossed && d_A > 2.0f * g.r) gs->crossed = 0; // a new approach
            float lead[3], s_line = 0.0f;
            float omega = core::fmin_f(dyn.W[0] > 0.0f ? dyn.W[0] : 1e30f, dyn.W[1] > 0.0f ? dyn.W[1] : 1e30f);
            if (omega >= 1e29f) omega = 0.0f;
            bool cap_join = true; // a non-capital, or a capital in join mode: the lead-in as before
            // stage B (D.6): the controller's committed member is entered first; its order-4 leg has no join mode, slow join or aim-at-A
            // latch (the funnel's feasibility and the go-around replace them); a ship the board refuses keeps amendment 17's path
            bool sb_member = false;
            if (capital && traffic_on_ && !player) {
                const gc::Member* cm = ctl_commit(*gs, obj, in, dyn, &g, G, row0, row1, row2, r_gate, gate_tgt, now);
                sb_member = cm && gc::sb_managed(*cm); // review F8: else the stage-A / amendment-17 join
            }
            if (capital) {
                // Amendment 15 (section 15.3): the join mode of this leg (out beyond 6 r, join, or the latched fallback), the
                // decision's state in the plane of the lateral offset for the frame-end predictor, the P2 bound's inputs and the row
                const float dX[3] = {in.x[0] - g.X[0], in.x[1] - g.X[1], in.x[2] - g.X[2]};
                const float s_now = core::dot3(dX, g.h), e2 = core::dot3(dX, dX) - s_now * s_now;
                const float e_now = e2 > 0.0f ? core::sqrt_ss(e2) : 0.0f;
                const float XG[3] = {g.X[0] - G[0], g.X[1] - G[1], g.X[2] - G[2]};
                gs->xg_m = core::sqrt_ss(core::dot3(XG, XG));
                gs->r_ship = dyn.r_own;
                gs->a_lin = dyn.a_lin;
                gs->grid_slot = grid_slot_of(gs->A_local, r_gate);
                gs->join_e = e_now;
                gs->join_carrot = 0.0f;
                gs->join_code = code;
                if (sb_member) {
                    cap_join = false;
                    gs->join.mode = core::JoinMode::none;
                } else {
                const bool converged = gs->last_err_deg >= 0.0f && gs->last_err_deg <= core::kJoinConvergedNoseDeg && e_now <= core::kJoinConvergedLatM;
                bool changed = false;
                const core::JoinMode mode = core::join_on_decision(gs->join, order_point, s_now, r_gate, converged, s->order == core::kOrderFlyBefore, now, &changed);
                const float u[3] = {in.v * in.row[2][0] + in.slip[0], in.v * in.row[2][1] + in.slip[1], in.v * in.row[2][2] + in.slip[2]};
                if (core::join_plane(g, in.x, in.row, in.w, u, in.v, &gs->join.plane)) {
                    gs->join.plane_ms = now ? now : 1;
                    gs->join.params.W = omega;
                    gs->join.params.alpha = omega * dyn.k;
                    gs->join.params.v_max = dyn.v_max;
                    gs->join.params.a_lin = dyn.a_lin;
                    gs->join.params.r_gate = r_gate;
                }
                // also after a sweep dropped it (a stale gap, then the same leg); amendment 16: slow_join and the pred_none fallback too
                if (core::join_eligible16(gs->join)) join_register(slot);
                if (changed) {
                    if (mode == core::JoinMode::join) {
                        gate_capital_join_.add();
                    } else if (mode == core::JoinMode::fallback && gs->join.reason == core::JoinReason::past_x)
                        gate_capital_fallback_past_x_.add();
                }
                cap_join = mode == core::JoinMode::join;
                if (mode == core::JoinMode::fallback && !v_emergency_bits && in.d_order < 0.0f && d_A > 0.0f) {
                    // aim at A (the order's own point: no redirect) at the slow-entry speed, written both ways
                    float c = core::dot3(in.row[2], dA) / d_A;
                    if (c > 1.0f) c = 1.0f;
                    if (c < -1.0f) c = -1.0f;
                    capital_fb_v = core::join_fallback_speed(d_A, core::acos_deg(c) * (core::kPi / 180.0f), dyn.a_lin, r_gate);
                    capital_fb_write = true;
                }
                if (trace_on_ && trace_slot_of(obj) && (changed || now - gs->join_row_ms >= 1000)) {
                    join_row(*gs, obj, now, false, -1.0f, -1.0f, -1.0f, 0, 0);
                    gs->join_row_ms = now;
                    gs->join_logged_mode = std::uint8_t(mode);
                }
                cap_join = cap_join || mode == core::JoinMode::slow_join; // amendment 16: the slow join keeps the carrot pursuit
                // a detour hands the leaf another point (the lead-in is skipped below); the leg is still steered, so site B keeps the
                // join's speed both ways instead of the engine's 1 m/s alignment creep (Run158 crowd; fixture scenario M)
                capital_detour = cap_join && in.detour;
                // Amendment 16 (section 16.1): this decision's lane entry, the follow cap of a trailing (b) ship, the lane rows
                if (!traffic_on_) // the controller's member entry ran above (gate-traffic-controller.md D.6)
                    lane_decision(*gs, obj, in, dyn, order_point, G, row0, row1, row2, mode, now, lane);
                } // not a stage-B member
            }
            if (!v_emergency_bits && in.d_order < 0.0f && cap_join) {
                bool in_window;
                if (capital) // the capital window (6 r, blended from 5 r) and the turning-radius look-ahead (amendment 13)
                    in_window = core::gate_lead_in_capital(g, in.x, in.v, omega, lead, &s_line);
                else // the carrot by the nose's lag (amendment 14): alpha = the slower of yaw/pitch times k
                    in_window = core::gate_lead_in(g, in.x, in.v, core::slower_axis_alpha(dyn), lead, &s_line);
                if (in_window) {
                    if (!capital) gs->lead_ms = now ? now : 1; // audit G1: site E's gate row for the small ship's own target gate
                    else {
                        const float dc[3] = {lead[0] - in.x[0], lead[1] - in.x[1], lead[2] - in.x[2]};
                        gs->join_carrot = core::sqrt_ss(core::dot3(dc, dc));
                    }
                    std::int32_t units[3];
                    for (unsigned i = 0; i < 3; ++i) {
                        in.P[i] = lead[i];
                        units[i] = core::round_to_int(lead[i] * core::kUnitsPerMetre);
                    }
                    in.d_order = d_A;
                    redirect_target(f, units);
                    const float c_rel[3] = {lead[0] - G[0], lead[1] - G[1], lead[2] - G[2]};
                    for (unsigned i = 0; i < 3; ++i) gs->carrot_local[i] = r_gate > 0.0f ? core::dot3(c_rel, gs->rows[i]) / r_gate : 0.0f;
                    gs->carrot_ms = now;
                    capital_window = capital;
                } else
                    gate_lead_window_rejected_.add();
            }
        } else if (!capital)
            gs->last_err_deg = -1.0f;
    } else if (gph && order == core::kOrderDock && capital) {
        // amendment 16 (section 16.1): a capital's order-5 leg on its validated approach keeps its lane entry current (the leg's
        // footprint runs from its lateral position to the gate origin); the engine's speed law stands (no site-B write)
        if (capital_policy && gs->validated == 1) {
            if (traffic_on_) {
                if (!player) ctl_commit(*gs, obj, in, dyn, nullptr, G, row0, row1, row2, r_gate, gate_tgt, now);
            }
            else
                lane_decision(*gs, obj, in, dyn, order_point, G, row0, row1, row2, gs->join.mode, now, lane);
        }
    } else if (gph && order == core::kOrderDock && !capital) {
        // Stage B (D.5 with D.12): while a capital is in this gate's funnel, a small ship's pass point B (f+0x240, read by the order-5
        // handler only, every frame, uncached) is mirrored into the free half of the lower arc when it is the engine's arc point on the
        // capital's side; f+0x230 is not written (unread on a gate leg). This frame's misalignment test and every later trigger test
        // see the new B; the leaf steers at it from this call. No capital in the funnel: nothing is written.
        // review: only beyond 0.55 r_gate of B (the trigger may already fire), never on a detour frame, never the player's ship; the side
        // against every funnel member that reaches the plane before this ship (gc::small_b_decide); both sides taken: no rewrite
        if (traffic_on_ && !player && !in.detour) {
            const gc::Gate* cg = gc::gate_of(ctl_, gate_tgt);
            const std::int32_t cur[3] = {rds32(fl + sites::kFlightPoint), rds32(fl + sites::kFlightPoint + 4), rds32(fl + sites::kFlightPoint + 8)};
            if (cg && !gc::small_b_ours(gs->b_w, cur)) {
                float d[3], bl[3], o[3];
                for (unsigned i = 0; i < 3; ++i) d[i] = float(cur[i]) / core::kUnitsPerMetre - G[i];
                bl[0] = core::dot3(d, row0);
                bl[1] = core::dot3(d, row1);
                bl[2] = core::dot3(d, row2);
                const float dB[3] = {float(cur[0]) / core::kUnitsPerMetre - in.x[0], float(cur[1]) / core::kUnitsPerMetre - in.x[1],
                                     float(cur[2]) / core::kUnitsPerMetre - in.x[2]};
                const gc::SmallB v = gc::small_b_decide(*cg, now, bl, core::sqrt_ss(core::dot3(dB, dB)), in.v, dyn.r_own, o);
                if (v == gc::SmallB::both_taken) small_b_both_.add();
                if (v == gc::SmallB::mirrored) {
                    std::int32_t units[3];
                    bool ok = true;
                    for (unsigned i = 0; i < 3; ++i) {
                        const float w = G[i] + o[0] * row0[i] + o[1] * row1[i] + o[2] * row2[i];
                        ok = ok && core::finite_f(w) && w > -4.0e6f && w < 4.0e6f; // the int32 range guard (units = 500 x m)
                        units[i] = ok ? core::round_to_int(w * core::kUnitsPerMetre) : 0;
                    }
                    if (ok) {
                        for (unsigned i = 0; i < 3; ++i) {
                            wr32(fl + sites::kFlightPoint + 4 * i, units[i]);
                            gs->b_w.w[i] = units[i];
                            order_point[i] = float(units[i]) / core::kUnitsPerMetre;
                            in.P[i] = order_point[i];
                        }
                        gs->b_w.ok = 1;
                        redirect_target(f, units);
                        small_b_rewrites_.add();
                    }
                }
            }
        }
        // order 5 to a gate: the cornering speed toward B at site B (the chord rule) when B is the pinned pass point of a
        // gate whose frame validates (fail closed to the engine's creep), unless the arc would sweep the ring
        const bool valid = core::gate_pass_point_valid(order_point, G, row0, row1, row2, r_gate, rd16(gate_tgt + sites::kObjType));
        const bool clear = valid && core::gate_chord_clears_ring(in.x, in.row[2], order_point, G, row0, row1, row2, r_gate, dyn.r_own);
        gs->dock_blocked = clear ? 0 : 1;
        gate_dock = valid; // the override runs over the whole leg (amendment 9): the chord test against the ring's hole bound is a
                           // visual diagnostic only (`dock_blocked` in the transition row, `gate_dock_blocked`): the engine's collision
                           // pair filter exempts the ship-gate pair on this leg and the installed gates are collision-free bubbles
        if (!valid) gate_dock_invalid_.add();
        else if (!clear) gate_dock_blocked_.add();
        if (gs->pending && debug_) { // the transition diagnostic: one row per approach
            gs->pending = 0;
            const std::uint32_t w = gate_trans_write_.load(std::memory_order_relaxed);
            if (w - gate_trans_read_ >= 8) row_dropped(Drop::gate_trans);
            if (w - gate_trans_read_ < 8) {
                GateTransition& e = gate_trans_[w & 7];
                e.obj = obj;
                e.code = code;
                e.dA_r = gs->last_dA_r;
                const float dB[3] = {order_point[0] - in.x[0], order_point[1] - in.x[1], order_point[2] - in.x[2]};
                const float dn = core::sqrt_ss(core::dot3(dB, dB));
                float c = dn > 0.0f ? core::dot3(in.row[2], dB) / dn : 1.0f;
                if (c > 1.0f) c = 1.0f;
                if (c < -1.0f) c = -1.0f;
                e.err_B_deg = core::acos_deg(c);
                e.v_ms = in.v;
                e.desired_ms = float(rds32(obj + sites::kObjDesiredSpeed)) / core::kUnitsPerMetre;
                const float b_rel[3] = {order_point[0] - G[0], order_point[1] - G[1], order_point[2] - G[2]};
                e.B_local[0] = r_gate > 0.0f ? core::dot3(b_rel, row0) / r_gate : 0.0f;
                e.B_local[1] = r_gate > 0.0f ? core::dot3(b_rel, row1) / r_gate : 0.0f;
                e.B_local[2] = r_gate > 0.0f ? core::dot3(b_rel, row2) / r_gate : 0.0f;
                for (unsigned i = 0; i < 3; ++i) e.A_local[i] = gs->A_local[i];
                e.validated = gs->validated;
                e.crossed = gs->crossed;
                e.capital = gs->capital;
                e.dock_blocked = gs->dock_blocked;
                // amendment 10: the crossing (P2) and this order-5 start in the gate frame
                for (unsigned i = 0; i < 3; ++i) {
                    e.p2_pos[i] = gs->p2_pos[i];
                    e.p2_fwd[i] = gs->p2_fwd[i];
                    e.p2_carrot[i] = gs->carrot_local[i];
                    const float x_rel[3] = {in.x[0] - G[0], in.x[1] - G[1], in.x[2] - G[2]};
                    e.o5_pos[i] = r_gate > 0.0f ? core::dot3(x_rel, gs->rows[i]) / r_gate : 0.0f;
                    e.o5_fwd[i] = core::dot3(in.row[2], gs->rows[i]);
                }
                {
                    float A_w[3];
                    for (unsigned i = 0; i < 3; ++i) A_w[i] = G[i] + (gs->A_local[0] * row0[i] + gs->A_local[1] * row1[i] + gs->A_local[2] * row2[i]) * r_gate;
                    const float dA[3] = {A_w[0] - in.x[0], A_w[1] - in.x[1], A_w[2] - in.x[2]};
                    e.o5_dA_r = r_gate > 0.0f ? core::sqrt_ss(core::dot3(dA, dA)) / r_gate : 0.0f;
                    e.o5_dB_r = r_gate > 0.0f ? dn / r_gate : 0.0f;
                }
                e.ms_since_p2 = gs->p2_stamp ? now - gs->p2_stamp : 0;
                e.carrot_age_ms = gs->carrot_ms ? std::int32_t(now - gs->carrot_ms) : -1;
                gate_trans_write_.store(w + 1, std::memory_order_release);
            }
        } else if (gs->pending)
            gs->pending = 0;
    }
    // Arrival parameters (section 2.5).
    float dock_w_new[3] = {0.0f, 0.0f, 0.0f}; // amendment 18: order 4 to a moving container; written with the decision (review 7)
    in.arrival_ramp = policy.arrival_ramp && !gate_approach;
    if (in.arrival_ramp) {
        const std::uint32_t tgt = rd32(fl + sites::kFlightTarget);
        if (order == core::kOrderFlyBefore) {
            std::int32_t units;
            const std::uint16_t tclass = tgt ? rd16(tgt + sites::kObjClass) : 0;
            if (tgt && tclass == sites::kClassShip) {
                units = 111000;
                // amendment 18: a moving container (a carrier): the ramp is relative, its reference speed the container's velocity along
                // the nose, its distance (site B) to the live approach point (dock_w_)
                float C[3], w[3], r_c = 0.0f;
                if (dock_on_ && av::dock_container(order, tclass, rd32(tgt + sites::kObjFlags)) && dock_container_state(tgt, tclass, C, &r_c, w)) {
                    in.v_ref = core::container_ref_speed(w, in.row[2], dyn.v_max);
                    for (unsigned c = 0; c < 3; ++c) dock_w_new[c] = w[c];
                }
            }
            else if (tgt && tclass == sites::kClassGate)
                units = rds32(tgt + sites::kObjRadius) / 4 * 3;
            else {
                units = (dyn.large ? 1 : 3) * rds32(obj + sites::kObjRadius);
                if (units < 22200) units = 22200;
                if (units > 111000) units = 111000;
            }
            in.d_arr = float(units) / core::kUnitsPerMetre;
        } else {
            in.d_arr = float(rds32(fl + sites::kFlightDistance)) / core::kUnitsPerMetre;
            if (order == core::kOrderEscort) in.d_arr = sc_max(in.d_arr, dyn.r_own);
        }
        if (in.d_arr < 0.0f) in.d_arr = 0.0f;
        if ((order == core::kOrderEscort || order == core::kOrderFollow) && tgt && rd16(tgt + sites::kObjClass) == sites::kClassShip) {
            const std::uint32_t lf = rd32(tgt + sites::kObjFlight);
            if (lf) {
                float lead[3];
                for (unsigned c = 0; c < 3; ++c) lead[c] = fixed16(rds32(lf + sites::kFlightBasis + 0x20 + 4 * c));
                const float v_lead = float(rds32(tgt + sites::kObjSpeed)) / core::kUnitsPerMetre;
                const float along = core::dot3(lead, in.row[2]);
                in.v_ref = along > 0.0f ? v_lead * along : 0.0f;
                // Amendment 14 (slip-aware-steering.md section 2.5): velocity matching on the leader's actual velocity, its
                // nose velocity v_lead row 2 plus its slip obj+0x60 (a live, undeleted ship; the same reads the snapshot does)
                if (!(rd32(tgt + sites::kObjFlags) & sites::kFlagDeleted)) {
                    in.formation = true;
                    float ls[3];
                    for (unsigned c = 0; c < 3; ++c) ls[c] = float(rds32(tgt + sites::kObjSlip + 4 * c)) / core::kUnitsPerMetre;
                    // the leader's slip under the same bound (2 x the larger of the follower's v_max and the leader's speed)
                    const bool ok = core::slip_usable(ls, dyn.v_max > v_lead ? dyn.v_max : v_lead);
                    for (unsigned c = 0; c < 3; ++c) {
                        in.nose_lead[c] = v_lead * lead[c];
                        in.u_lead[c] = in.nose_lead[c] + (ok ? ls[c] : 0.0f);
                    }
                }
            }
        }
    }
    // Amendment 12: order 5 to a port (a target other than a gate, with a port index: the engine's port-roll match needs
    // its port data, which a gate never has) and the leg's hold from this ship's previous call.
    if (order == core::kOrderDock) {
        const std::uint32_t tg = rd32(fl + sites::kFlightTarget);
        in.port_leg = tg && rd16(tg + sites::kObjClass) != sites::kClassGate && rds32(fl + sites::kFlightPortIndex) >= 0;
        in.port_hold = in.port_leg && core::port_hold_active(*s, order, now);
    }
    // Stage B (gate-traffic-controller.md B.1): a member the controller flies (a holder or an escort in its petal, the funnel on order 4,
    // the capital order-5 leg on Lambda_5) gets the controller's leaf point (an avoidance detour keeps its point) and its speed at site B
    // in both directions (no arrival ramp, no formation law)
    bool sb_on = false;
    gc::SbSteer sbs;
    if (traffic_on_ && capital && !player &&
        (order == core::kOrderFlyBefore || order == core::kOrderDock || order == core::kOrderEscort || order == core::kOrderFollow)) {
        int ci = -1;
        if (const gc::Gate* cg = ctl_lookup(obj, &ci)) {
            const gc::Member& cm = cg->m[ci];
            if (gc::pair_role(cm.role) && cm.id == rd32(obj + sites::kObjId) && (cm.role != gc::Role::committed || gc::sb_managed(cm)))
                sb_on = gc::sb_steer(*cg, cm, in.x, in.v, order, rd32(fl + sites::kFlightTarget), &sbs);
        }
        if (sb_on) {
            if (!in.detour) {
                std::int32_t units[3];
                for (unsigned i = 0; i < 3; ++i) {
                    in.P[i] = sbs.P[i];
                    units[i] = core::round_to_int(sbs.P[i] * core::kUnitsPerMetre);
                }
                redirect_target(f, units);
            }
            const float dd[3] = {order_point[0] - in.x[0], order_point[1] - in.x[1], order_point[2] - in.x[2]};
            in.d_order = core::sqrt_ss(core::dot3(dd, dd));
            in.arrival_ramp = false;
            in.d_arr = 0.0f;
            in.formation = false;
            in.v_ref = sbs.v;
            gate_dock = false;
            ctl_sb_steered_.add();
        }
    }
    core::Command cmd;
    const core::Reason r = core::steer(in, dyn, &cmd);
    if (r != core::Reason::handled) return r;
    // The write: exactly the leaf's three outputs.
    wr32(obj + sites::kObjRemaining, cmd.e[0]);
    wr32(obj + sites::kObjRemaining + 4, cmd.e[1]);
    wr32(obj + sites::kObjRemaining + 8, cmd.e[2]);
    s->stamp_ms = now;
    table_id_[unsigned(s - table_.entries)] = rd32(obj + sites::kObjId); // B.4: the decision's object identity (site E's any-age row)
    s->order = order;
    // the gate speed policy: the non-capital approach (no ramp) and, inside the capital window, the capital lead-in (no ramp:
    // the engine's alignment rule, 1 m/s beyond 45.6 deg off A, would otherwise stall the lateral join; amendment 13); a
    // capital outside the window on a validated approach gets the lowering clamp only (the engine's law stands); any other
    // capital gate leg (orders 4 and 5: the amendment-10 path, order 5 always) gets no site-B write; the order-5 leg
    std::uint16_t speed_flags;
    if (sb_on)
        speed_flags = std::uint16_t(core::kShipFlagGate | core::kShipFlagClamp); // stage B: the controller's speed, both ways
    else if (capital_gate && !capital_window && !capital_fb_write && !capital_detour)
        speed_flags = std::uint16_t(capital_policy && gate_lead && cmd.v_cmd > 0.0f ? core::kShipFlagClamp : 0);
    else
        speed_flags = std::uint16_t((in.arrival_ramp && !cmd.formation_speed ? core::kShipFlagRamp : 0) | (cmd.v_cmd > 0.0f ? core::kShipFlagClamp : 0) |
                                    (gate_lead ? (core::kShipFlagGate | core::kShipFlagClamp) : 0) |
                                    (gate_dock ? (core::kShipFlagGateDock | core::kShipFlagClamp) : 0));
    s->flags = std::uint16_t(speed_flags | (cmd.port_aligned ? core::kShipFlagPortHold : 0)); // amendment 12: the next call's hold
    if (order == core::kOrderMovePos && s->order == order &&
        (s->P[0] != order_point[0] || s->P[1] != order_point[1] || s->P[2] != order_point[2]))
        s->best_d = -1.0f; // a new MovePos point: the stuck witness restarts (Run154: 8 false rows while d fell 34.5 -> 10.3 km)
    for (unsigned i = 0; i < 3; ++i) s->P[i] = order_point[i]; // site B measures the order's point, not a detour
    s->a_lin = dyn.a_lin;
    s->d_arr = gate_dock ? r_gate : in.d_arr; // order 5 to a gate: the engine's ramp runs over r_gate
    // the gate approach: site B's speed (never under an avoidance emergency: the emergency ramp is site B's value then);
    // order 5 to a gate: vmax for the ramp
    // amendment 16 fix: a capital's join on a detour keeps the join's write; site B lowers it to the emergency ramp (clamp_for)
    const bool join_write = capital_window || capital_detour;
    s->v_ref = (speed_flags & core::kShipFlagGate) && (!v_emergency_bits || capital_detour) ? core::gate_speed(dyn.v_max, dyn.large, cmd.v_corner)
               : gate_dock ? dyn.v_max : in.v_ref;
    for (unsigned c = 0; c < 3; ++c) dock_w_[slot][c] = dock_w_new[c]; // amendment 18: the velocity paired with this decision's v_ref
    // amendment 15: a capital in join mode at most the predictor's speed, in fallback the slow entry
    if ((speed_flags & core::kShipFlagGate) && (!v_emergency_bits || capital_detour) && gs) {
        if (join_write && gs->join.v_cmd < s->v_ref) s->v_ref = gs->join.v_cmd;
        if (capital_fb_write && capital_fb_v < s->v_ref) s->v_ref = capital_fb_v;
        if (capital_detour) gate_capital_detour_write_.add();
    }
    if (sb_on) // stage B: the controller's speed under the cornering cap and the large cap (review F3; an emergency lowers it at site B)
        s->v_ref = core::fmin_f(sbs.v, core::gate_speed(dyn.v_max, dyn.large, cmd.v_corner));
    if (order != core::kOrderFlyBefore && order != core::kOrderRotate && gate_[slot].obj == obj) {
        gate_[slot].handover = 0; // order 5 (or 0x12, any other handled order) ends the hand-over's gate row
        gate_[slot].p2_seen = 0;  // amendment 16: the extended hand-over too
        // and the join (the registry's sweep drops it); amendment 16: the slow join and the fallback as well
        if (gate_[slot].join.mode == core::JoinMode::join || gate_[slot].join.mode == core::JoinMode::slow_join ||
            gate_[slot].join.mode == core::JoinMode::fallback)
            gate_[slot].join.mode = core::JoinMode::none;
        if (order != core::kOrderDock && gates_capital_on_ && gate_[slot].gate) ln::mark(lanes_, gate_[slot].gate, obj, order); // not committed
    }
    // amendment 14: the formation law's |N| rides in the cornering slot (no ramp: site B returns it floored); the closing-speed
    // correction applies to the ramp/cornering schedule only (not on the gate legs, whose speed is the module's own)
    s->v_corner = cmd.formation_speed ? cmd.v_cmd : cmd.v_corner;
    s->v_slip = (speed_flags & (core::kShipFlagGate | core::kShipFlagGateDock)) || cmd.formation_speed ? 0.0f : cmd.v_slip;
    std::memcpy(&s->v_emergency, &v_emergency_bits, 4);
    // Review F2 / amendment 17: the pass's (b) follow cap on order 4 (the only lane cap: the 16b sequencing cap is gone) is in force
    // at site B on the module's writes (ln::apply_cap: lowering only); the same pass dropped the pair from the avoidance above.
    // lane_capped counts the decisions whose write the cap lowered (as in Run158), lane_cap_carried every decision that carries one.
    if (lane && order == core::kOrderFlyBefore) {
        const float cap = lane->cap();
        if (cap >= 0.0f) {
            lane_cap_carried_.add();
            if (ln::apply_cap(*s, cap)) lane_capped_.add();
        }
        if (lane->self_held) lane_hysteresis_hold_.add();
        if (lane->n_lat) lane_seq_lat_.add(lane->n_lat);
        if (lane->n_along) lane_seq_along_.add(lane->n_along);
        if (lane->n_watch_cross) lane_seq_cross_.add(lane->n_watch_cross);
        if (gs) gs->lane_cap = cap;
    } else if (gs && gs->obj == obj)
        gs->lane_cap = -1.0f;
    // The gate traffic controller (D.1, stage A): an escort enters on 0x14 / 0x15 behind a committed leader; a member records the
    // module's own speed of this decision before any yield cap (its rollout's plan speed), then the yield cap of its last controller
    // decision is in force at site B (ln::apply_cap: lowering only; the cap is >= 10 m/s by construction).
    if (traffic_on_ && capital && !player) {
        if (order == core::kOrderEscort || order == core::kOrderFollow) {
            const std::uint32_t leader = rd32(fl + sites::kFlightTarget);
            if (leader && ctl_ix_.maybe(leader)) ctl_escort(obj, leader, in, dyn, now);
        }
        int ci = -1;
        if (gc::Gate* cg = ctl_lookup(obj, &ci)) {
            gc::Member& m = cg->m[ci];
            if (gc::pair_role(m.role) && m.id == rd32(obj + sites::kObjId)) {
                m.v_plan = (s->flags & core::kShipFlagGate) ? s->v_ref : (s->flags & core::kShipFlagClamp) && s->v_corner > 0.0f ? s->v_corner : -1.0f;
                m.v_plan_ms = now ? now : 1;
                if (m.cap >= 0.0f && now - m.cap_ms <= gc::age_bound(*cg, gc::kCapMaxAgeMs) && gc::can_yield(m) && ln::apply_cap(*s, m.cap)) ctl_capped_.add();
            }
        }
    }
    s->last_call_ms = now;
    if (order == core::kOrderMovePos) {
        if (core::stuck_update(*s, cmd.d_order, now)) {
            const std::uint32_t n = stuck_count_.load(std::memory_order_relaxed);
            if (n < 8) {
                stuck_[n] = Stuck{obj, order, cmd.d, s->best_d};
                stuck_count_.store(n + 1, std::memory_order_release);
            }
        }
    } else
        s->best_d = -1.0f; // another order: the witness restarts with the next MovePos
    handled_by_order_[order & 0x3f].add();
    if (trace_on_) { // the followed ships: claim on entering a gate or dock approach, note this frame's leaf call
        TraceSlot* ts = trace_slot_of(obj);
        if (!ts) {
            const std::uint32_t tg = rd32(fl + sites::kFlightTarget);
            const std::uint16_t tc = tg ? rd16(tg + sites::kObjClass) : 0;
            // a capital only on order 4 to a gate (slot 0); a non-capital on order 4 to a gate or station, or on order 5
            const bool approach = capital ? order == core::kOrderFlyBefore && tc == sites::kClassGate
                                          : (order == core::kOrderFlyBefore && (tc == sites::kClassGate || tc == sites::kClassStationA || tc == sites::kClassStationB)) ||
                                                order == core::kOrderDock;
            const bool member = traffic_on_ && capital && ctl_on_board(obj); // B.4 item 2: a board member may take a reserved slot
            if (approach) ts = trace_claim(obj, now, capital, false, member);
            else if ((order == core::kOrderEscort || order == core::kOrderFollow) && tg) { // amendment 15: an escort of a traced capital
                const TraceSlot* lt = trace_slot_of(tg); // (Run161, under the traffic key: any traced non-escort capital; else slot 0's)
                if (traffic_on_ ? lt && !lt->escort && capital_obj(tg) : tg == trace_slots_[0].obj) ts = trace_claim(obj, now, capital, true, member);
            }
        }
        if (ts) {
            ts->last_seen_ms = now;
            ts->leaf_this_frame = 1;
            ts->emergency = v_emergency_bits ? 1 : 0;
            ts->obstacle = avoid_[slot].obj == obj && avoid_[slot].stamp_ms == now ? avoid_[slot].obstacle : 0; // this frame's resolve only
            for (unsigned i = 0; i < 3; ++i) {
                ts->W[i] = call_eax(i == 0 ? sites::kGetYawCap : i == 1 ? sites::kGetPitchCap : sites::kGetRollCap, obj);
                ts->given[i] = point[i];
                ts->point[i] = core::round_to_int(in.P[i] * core::kUnitsPerMetre);
                ts->e[i] = cmd.e[i];
                ts->omega_cmd[i] = cmd.omega_axis[i] * 180.0f / core::kPi;
            }
            ts->rot_raw = rds32(fl + sites::kFlightRotAccel);
            ts->rot_pct = rot_pct;
            ts->vmax = call_eax(sites::kGetMaxSpeed, obj);
            ts->accel = call_ecx(sites::kGetAccel, obj);
        }
    }
    if (sample_request_.load(std::memory_order_relaxed)) {
        sample_request_.store(false, std::memory_order_relaxed);
        const std::uint32_t seq = sample_seq_.load(std::memory_order_relaxed);
        sample_seq_.store(seq + 1, std::memory_order_release); // odd: being written
        Sample& S = sample_;
        S.obj = obj;
        S.order = order;
        S.code = code;
        S.leaf = leaf;
        S.flags = flags;
        S.theta_deg = cmd.theta * 180.0f / core::kPi;
        S.n_y = cmd.n[0];
        S.n_p = cmd.n[1];
        S.omega_dps = cmd.omega * 180.0f / core::kPi;
        S.d = cmd.d;
        S.v = in.v;
        S.v_cmd = cmd.v_cmd;
        S.v_corner = cmd.v_corner;
        S.d_arr = in.d_arr;
        S.v_ref = in.v_ref;
        S.lag_deg = core::lag_deg(cmd.u, in.row[2]); // the sampled call only (one per second)
        S.aim_cos = cmd.aim_cos;
        S.v_slip = cmd.v_slip;
        for (unsigned i = 0; i < 3; ++i) S.slip[i] = in.slip[i];
        for (unsigned i = 0; i < 3; ++i) {
            S.e[i] = cmd.e[i];
            S.w[i] = rds32(obj + sites::kObjRates + 4 * i);
            S.P[i] = in.P[i];
            S.x[i] = in.x[i];
            S.W_dps[i] = dyn.W[i] * 180.0f / core::kPi;
        }
        S.k = dyn.k;
        S.rot_accel_raw = rds32(fl + sites::kFlightRotAccel);
        S.rot_pct = rot_pct;
        for (unsigned i = 0; i < 3; ++i) {
            S.a_dps[i] = dyn.W[i] * dyn.k * 180.0f / core::kPi;
            S.omega_axis_dps[i] = cmd.omega_axis[i] * 180.0f / core::kPi;
        }
        S.held = cmd.held;
        sample_seq_.store(seq + 2, std::memory_order_release); // even: complete
    }
    return core::Reason::handled;
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the engine jumps into it.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&initialized_), &module) != FALSE &&
           module != nullptr;
}
void* stub_of(unsigned i) {
    switch (i) {
    case sites::LeafYawPitch: return reinterpret_cast<void*>(&x3m_ship_steering_a0_stub);
    case sites::LeafRollYaw: return reinterpret_cast<void*>(&x3m_ship_steering_a1_stub);
    case sites::LeafRollPitch: return reinterpret_cast<void*>(&x3m_ship_steering_a2_stub);
    default: return reinterpret_cast<void*>(&x3m_ship_steering_b_stub);
    }
}
void** slot_of(unsigned i) {
    switch (i) {
    case sites::LeafYawPitch: return &x3m_ship_steering_next_a0;
    case sites::LeafRollYaw: return &x3m_ship_steering_next_a1;
    case sites::LeafRollPitch: return &x3m_ship_steering_next_a2;
    default: return &x3m_ship_steering_next_b;
    }
}
// The install transaction (the stamp::install_group shape, with the
// continuation slots in this module's .data): refused outside the install
// window, every span preflighted before any claim, claims in table order,
// every patched site rolled back in reverse on the first failure. The module
// stays off unless all four sites are live.
bool install_all(const char** status, char (&detail)[64]) {
    detail[0] = 0;
    if (!engine_patch::install_window_open()) {
        *status = "late_claim";
        return false;
    }
    for (unsigned i = 0; i < sites::Count; ++i)
        if (!engine_patch::verify_bytes(sites::kSites[i].address, sites::kSites[i].expected, sites::kSites[i].length)) {
            *status = "bytes_mismatch";
            std::snprintf(detail, sizeof detail, "%s", sites::kSites[i].name);
            return false;
        }
    if (!pin_self()) {
        *status = "pin_failed";
        return false;
    }
    for (unsigned i = 0; i < sites::Count; ++i) {
        patches_[i] = engine_patch::Site{};
        if (engine_patch::claim(patches_[i], sites::kSites[i])) {
            void** slot = slot_of(i);
            *slot = *patches_[i].entry; // the tail: the engine's own bytes until the stub is chained
            std::atomic_thread_fence(std::memory_order_release);
            if (engine_patch::push_front(patches_[i], stub_of(i))) continue;
            *status = "stub_chain_failed";
        } else
            *status = patches_[i].status;
        std::snprintf(detail, sizeof detail, "%s", sites::kSites[i].name);
        bool restored = true;
        for (unsigned j = sites::Count; j-- > 0;)
            if (patches_[j].patched_in && !engine_patch::restore(patches_[j])) restored = false;
        // A site whose restore failed stays live with our stub chained: both handlers pass through while
        // installed_ is false, and shutdown() retries the restore (patched_in stays set).
        if (!restored) *status = "rollback_failed_live_sites_pass_through";
        return false;
    }
    *status = "ok";
    return true;
}
const char* write_name(const engine_patch::Site& s) {
    return s.patched_in ? (s.atomic_write ? "atomic" : "plain") : "none";
}
void** gate_slot_of(unsigned i) {
    switch (i) {
    case sites::GateStopA: return &x3m_ship_gate_next_a;
    case sites::GateStopB: return &x3m_ship_gate_next_b;
    case sites::GateLaunch: return &x3m_ship_gate_next_l;
    default: return &x3m_ship_gate_next_w;
    }
}
void* gate_stub_of(unsigned i) {
    switch (i) {
    case sites::GateStopA: return reinterpret_cast<void*>(&x3m_ship_gate_p2_stub);
    case sites::GateStopB: return reinterpret_cast<void*>(&x3m_ship_gate_p4_stub);
    case sites::GateLaunch: return reinterpret_cast<void*>(&x3m_ship_gate_p5_stub);
    default: return reinterpret_cast<void*>(&x3m_ship_gate_warp_stub);
    }
}
}

extern "C" int __cdecl x3m_ship_steering_enter(sites::Frame* frame, int leaf) {
    // A site that stayed chained after a failed rollback passes through (the original leaf runs).
    if (!installed_.load(std::memory_order_relaxed)) return 0;
    Envelope env;
    const std::uint64_t t0 = timing_ ? qpc() : 0;
    const core::Reason r = decide(frame, unsigned(leaf) < 3 ? unsigned(leaf) : 0u);
    calls_.add();
    leaf_calls_[unsigned(leaf) < 3 ? unsigned(leaf) : 0u].add();
    if (r == core::Reason::handled)
        handled_.add();
    else
        reasons_[unsigned(r) < core::kReasonCount ? unsigned(r) : 0u].add();
    if (timing_) record_time(qpc() - t0);
    return r == core::Reason::handled ? 1 : 0;
}
namespace {
// Amendment 15 (section 15.4): the frame after the KC's CollisionWarn ran ([obj+0x40] & 0x3000 both set, the order not yet 0x12;
// L0 of the next collision pass clears the bits): the engine's list with E's verdict per entry, one row per warn (edge-triggered by
// the caller: a traced ship's slot, or amendment 17's capital edge memory).
void push_warn_pre(std::uint32_t obj, std::uint32_t fl, std::uint16_t order_now, std::uint32_t now) noexcept {
    const std::uint32_t pw = warn_pre_write_.load(std::memory_order_relaxed);
    if (pw - warn_pre_read_ < 8) {
        WarnPreRow& p = warn_pre_[pw & 7];
        p = WarnPreRow{};
        p.obj = obj;
        p.order = order_now;
        p.tgt = rd32(fl + sites::kFlightTarget);
        p.count = rd32(fl + sites::kFlightObstacleCount);
        const unsigned n = p.count <= sites::kFlightObstacleMax ? p.count : 0;
        const std::uint32_t tt = rd32(sites::kShipTypeTableSlot);
        const std::uint32_t own_id = rd32(obj + sites::kObjId);
        for (unsigned i = 0; i < n; ++i) {
            const std::uint32_t o = rd32(fl + sites::kFlightObstacleList + 4 * i);
            p.other[i] = o;
            p.d[i] = float(rds32(fl + sites::kFlightObstacleDist + 4 * i)) / core::kUnitsPerMetre;
            p.code[i] = 0xffff;
            p.verdict[i] = 0;
            if (!o) continue;
            p.cls[i] = rd16(o + sites::kObjClass);
            p.r[i] = float(rds32(o + sites::kObjRadius)) / core::kUnitsPerMetre;
            if (p.cls[i] == sites::kClassShip) {
                const std::int32_t ot = std::int16_t(rd16(o + sites::kObjType));
                if (tt && ot >= 0) p.code[i] = rd32(tt + std::uint32_t(ot) * sites::kRecordStride + sites::kRecordClassCode);
                const std::uint32_t ofl = rd32(o + sites::kObjFlight);
                if (ofl) {
                    p.o_order[i] = rd16(ofl + sites::kFlightOrder);
                    p.leader[i] = rd32(ofl + sites::kFlightTarget);
                }
                // the remembered membership linking the pair (either direction), 0 = none
                if (members_.current(o, obj, rd32(o + sites::kObjId), own_id, now)) p.member[i] = obj;
                else if (members_.current(obj, o, own_id, rd32(o + sites::kObjId), now)) p.member[i] = o;
            }
            for (unsigned k = 0; k < kELast; ++k)
                if (e_last_[k].a == obj && e_last_[k].b == o && now - e_last_[k].ms <= 1000) p.verdict[i] = e_last_[k].verdict;
        }
        warn_pre_write_.store(pw + 1, std::memory_order_release);
    } else
        row_dropped(Drop::warn_pre);
}
// Amendment 13: the engine's list at a ship's first 0x12 frame (the ship_avoid_engine_warn row).
void push_engine_warn(std::uint32_t obj, std::uint32_t fl) noexcept {
    const std::uint32_t ew = engine_warn_write_.load(std::memory_order_relaxed);
    if (ew - engine_warn_read_ < 8) {
        EngineWarn& e = engine_warns_[ew & 7];
        e.obj = obj;
        e.count = rd32(fl + sites::kFlightObstacleCount);
        const bool listed = e.count >= 1 && e.count <= sites::kFlightObstacleMax;
        e.other = listed ? rd32(fl + sites::kFlightObstacleList) : 0;
        e.d = listed ? float(rds32(fl + sites::kFlightObstacleDist)) / core::kUnitsPerMetre : 0.0f;
        e.cls = e.other ? rd16(e.other + sites::kObjClass) : 0;
        e.r = e.other ? float(rds32(e.other + sites::kObjRadius)) / core::kUnitsPerMetre : 0.0f;
        e.code = 0xffff;
        if (e.cls == sites::kClassShip) {
            const std::int32_t ot = std::int16_t(rd16(e.other + sites::kObjType));
            const std::uint32_t tt = rd32(sites::kShipTypeTableSlot);
            if (tt && ot >= 0) e.code = rd32(tt + std::uint32_t(ot) * sites::kRecordStride + sites::kRecordClassCode);
        }
        engine_warn_write_.store(ew + 1, std::memory_order_release);
    } else
        row_dropped(Drop::engine_warn);
}
// A class-7 ship with a capital record code (0x0043fd50's table: [type table + type * stride + class code]); unreadable = not capital.
inline bool capital_obj(std::uint32_t obj) noexcept {
    if (!obj || rd16(obj + sites::kObjClass) != sites::kClassShip) return false;
    const std::uint32_t tt = rd32(sites::kShipTypeTableSlot);
    const std::int32_t type = std::int16_t(rd16(obj + sites::kObjType));
    if (!tt || type < 0) return false;
    const std::uint32_t code = rd32(tt + std::uint32_t(type) * sites::kRecordStride + sites::kRecordClassCode);
    return code < core::kClassCount && ((core::kCapitalMask >> code) & 1u);
}
// Amendment 17 (section 17 item 5): the warn_pre and warn rows for every capital subject that no trace slot follows (the edge
// memory cap_warn_, declared beside the trace slots). The type is read only on a warned or 0x12 frame of a ship not held. Rows share
// the traced ships' rings (8 warn_pre and 8 warn rows per frame).
inline void capital_warn_watch(std::uint32_t obj, std::uint32_t fl) noexcept {
    const std::uint16_t order_now = rd16(fl + sites::kFlightOrder);
    const bool avoid = order_now == core::kOrderAvoid;
    const bool warned = !avoid && (rd32(obj + sites::kObjFlags) & sites::kFlagWarnBits) == sites::kFlagWarnBits;
    if (!warned && !avoid && !cap_warn_used_) return;
    unsigned k = 0;
    while (k < kCapWarnSlots && cap_warn_[k].obj != obj) ++k;
    if (!warned && !avoid) {
        if (k < kCapWarnSlots) cap_warn_free(k); // the episode is over: the edge memory frees
        return;
    }
    const std::uint32_t now = game_time_ms();
    if (k == kCapWarnSlots) {
        if (!capital_obj(obj)) return;
        for (k = 0; k < kCapWarnSlots; ++k)
            if (!cap_warn_[k].obj || now - cap_warn_[k].seen_ms > kCapWarnExpiryMs) break;
        if (k == kCapWarnSlots) {
            row_dropped(Drop::warn_watch);
            return;
        }
        if (!cap_warn_[k].obj) ++cap_warn_used_; // a stale slot is reclaimed in place
        cap_warn_[k] = CapWarnSlot{};
        cap_warn_[k].obj = obj;
    }
    CapWarnSlot& c = cap_warn_[k];
    c.seen_ms = now;
    if (warned && !c.warn_last) push_warn_pre(obj, fl, order_now, now);
    if (avoid && !c.avoid_last) push_engine_warn(obj, fl);
    c.warn_last = warned ? 1 : 0;
    c.avoid_last = avoid ? 1 : 0;
}
// The ship_trace row of this frame for a followed ship, pushed at site B after the module's speed write (the row carries
// `des` as left by the write and `des_engine` as the engine set it), before the slew. Orders 0 (the KC's hand-over and
// SA_StopObject), 4, 5, 8, 0x12 (the engine's avoid order after a collision warn) and 0x1f (RotateToAngles, the capital's
// alignment) keep the slot; any other order ends the approach and frees it.
inline bool trace_frame(std::uint32_t obj, std::uint32_t fl, std::uint32_t ph, std::int32_t des_engine) noexcept {
    TraceSlot* ts = trace_slot_of(obj);
    if (!ts) return false; // not followed: the caller watches a capital's warns (amendment 17)
    const std::uint32_t now = game_time_ms();
    const std::uint16_t order_now = rd16(fl + sites::kFlightOrder);
    // The approach passes through order 0 (the arrival, and SA_StopObject after a collision warn), the engine's own avoid
    // order 0x12 (Run155: every trace gap before order 5 was a stop row followed by 70-900 frames on 0x12, the slot
    // released here and re-claimed only at order 5) and RotateToAngles 0x1f (the capital's alignment step).
    const bool keep = ts->escort ? order_now == core::kOrderNone || order_now == core::kOrderAvoid || order_now == core::kOrderEscort ||
                                       order_now == core::kOrderFollow || order_now == core::kOrderRotate
                                 : order_now == core::kOrderNone || order_now == core::kOrderFlyBefore || order_now == core::kOrderDock ||
                                       order_now == core::kOrderWarpAway || order_now == core::kOrderAvoid || order_now == core::kOrderRotate;
    if (!keep) {
        ts->obj = 0; // the approach (or the escort's formation) is over (another order)
        return true;
    }
    ts->last_seen_ms = now;
    {
        const bool warned = (rd32(obj + sites::kObjFlags) & sites::kFlagWarnBits) == sites::kFlagWarnBits && order_now != core::kOrderAvoid;
        if (warned && !ts->warn_pre_last) push_warn_pre(obj, fl, order_now, now);
        ts->warn_pre_last = warned ? 1 : 0;
    }
    if (order_now == core::kOrderAvoid && ts->last_order != core::kOrderAvoid) push_engine_warn(obj, fl); // amendment 13: the engine's list at the first 0x12 row
    ts->last_order = order_now;
    const std::uint32_t w = trace_write_.load(std::memory_order_relaxed);
    if (w - trace_read_ >= kTraceRing) {
        trace_dropped_.add();
        ts->leaf_this_frame = 0;
        return true;
    }
    TraceRow& r = trace_ring_[w & (kTraceRing - 1)];
    r.obj = obj;
    r.t_ms = now;
    const std::uint32_t clock = rd32(sites::kGameClock);
    r.dt_ms = clock ? rd32(clock + sites::kGameTimeOffset - 4) : 0;
    r.order = rd16(fl + sites::kFlightOrder);
    r.tgt = rd32(fl + sites::kFlightTarget);
    r.tgt_class = r.tgt ? rd16(r.tgt + sites::kObjClass) : 0;
    const std::int32_t type = std::int16_t(rd16(obj + sites::kObjType));
    const std::uint32_t table = rd32(sites::kShipTypeTableSlot);
    r.code = std::uint16_t(table && type >= 0 ? rd32(table + std::uint32_t(type) * sites::kRecordStride + sites::kRecordClassCode) : 0xffff);
    for (unsigned i = 0; i < 3; ++i) {
        r.pos[i] = rds32(ph + sites::kPhysPosition + 4 * i);
        r.rate[i] = rds32(obj + sites::kObjRates + 4 * i);
        r.fwd[i] = fixed16(rds32(fl + sites::kFlightBasis + 0x20 + 4 * i));
        r.up[i] = fixed16(rds32(fl + sites::kFlightBasis + 0x10 + 4 * i));
        r.W[i] = ts->W[i];
        r.slip[i] = rds32(obj + sites::kObjSlip + 4 * i);
    }
    r.v = rds32(obj + sites::kObjSpeed);
    r.des = rds32(obj + sites::kObjDesiredSpeed);
    r.des_engine = des_engine;
    r.rot_raw = ts->rot_raw;
    r.rot_pct = ts->rot_pct;
    r.vmax = ts->vmax;
    r.accel = ts->accel;
    r.has_e = ts->leaf_this_frame;
    r.emergency = ts->emergency;
    r.cap = -1.0f;
    r.cap_age_ms = -1;
    if (traffic_live_.load(std::memory_order_relaxed) && ctl_ix_.maybe(obj)) {
        int ci = -1;
        if (const gc::Gate* cg = gc::locate(ctl_, obj, &ci))
            if (cg->m[ci].cap >= 0.0f) {
                r.cap = cg->m[ci].cap;
                r.cap_age_ms = std::int32_t(now - cg->m[ci].cap_ms);
            }
    }
    r.obstacle = ts->obstacle;
    for (unsigned i = 0; i < 3; ++i) r.omega_cmd[i] = ts->omega_cmd[i];
    for (unsigned i = 0; i < 3; ++i) {
        if (ts->leaf_this_frame) {
            r.point[i] = ts->point[i];
            r.given[i] = ts->given[i];
            r.e[i] = ts->e[i];
        } else { // no leaf call this frame: the engine's own point (f+0x240) stands for both, e omitted
            r.point[i] = r.given[i] = rds32(fl + sites::kFlightPoint + 4 * i);
            r.e[i] = 0;
        }
    }
    ts->leaf_this_frame = 0;
    trace_write_.store(w + 1, std::memory_order_release);
    return true;
}
}
namespace {
// The gate traffic controller at site B (after the module's write and the trace row): a member's live state every kStateMs and its
// role's validity from the live order and target (D.1). 0x12 evicts (the sequence number kept 30 s). A committed member keeps its
// role to its gate on order 4 / 5 with a commit refresh within kCommitFreshMs, on 0 / 0x1f within the hand-over after P2, and on 8
// (the launch). An escort keeps it on 0x14 / 0x15 to its leader, or on order 0 while the formation memory holds the pair (the KC's
// 3 s gaps), behind a committed live leader on the same board. An arrival keeps it for 60 s after its launch while in front of the
// plane. Anything else leaves the board (the pair falls back to today's rules).
void ctl_order0_rows(std::uint32_t obj) noexcept; // Run161 item 4 (below)
// B.3 item 2: the frame after the KC's CollisionWarn ran on a member ([obj+0x40] & 0x3000 both set, the order not yet 0x12; the
// warn_pre row's moment): whether its obstacle list named the member's own gate, and its stage then (gc::note_warn).
inline void ctl_note_warn(std::uint32_t obj, std::uint32_t fl) noexcept {
    if ((rd32(obj + sites::kObjFlags) & sites::kFlagWarnBits) != sites::kFlagWarnBits || rd16(fl + sites::kFlightOrder) == core::kOrderAvoid) return;
    int i = -1;
    gc::Gate* g = gc::locate(ctl_, obj, &i);
    if (!g || i < 0 || g->m[i].role != gc::Role::committed || g->m[i].id != rd32(obj + sites::kObjId)) return;
    const std::uint32_t count = rd32(fl + sites::kFlightObstacleCount);
    const unsigned n = count <= sites::kFlightObstacleMax ? count : 0;
    // the list holds the own gate and nothing else (decision 2026-10-13: the warn's unsuppressed list only the own gate; whether the
    // collision pass lists an obstacle site E suppressed is not established, so any other entry fails closed: the 0x12 evicts)
    bool own = false, other = false;
    for (unsigned q = 0; q < n; ++q) {
        const std::uint32_t o = rd32(fl + sites::kFlightObstacleList + 4 * q);
        if (o == g->gate) own = true;
        else if (o) other = true;
    }
    gc::note_warn(*g, g->m[i], own && !other, game_time_ms());
}
inline void ctl_observe(std::uint32_t obj, std::uint32_t fl, std::uint32_t ph) noexcept {
    int i = -1;
    gc::Gate* g = gc::locate(ctl_, obj, &i);
    if (!g) return;
    gc::Member& m = g->m[i];
    if (!gc::pair_role(m.role)) return;
    const std::uint32_t now = game_time_ms();
    gc::note_step(ctl_, now, frame_step_ms()); // B.3 item 1: the SETA-aware bounds (once per game-time frame)
    if (m.st.ms && now - m.st.ms < gc::kStateMs) return;
    const unsigned k = unsigned(i);
    if (rd32(obj + sites::kObjId) != m.id) {
        ctl_leave(*g, k, false, gc::Why::identity, now);
        return;
    }
    const std::uint16_t order = rd16(fl + sites::kFlightOrder);
    const std::uint32_t target = rd32(fl + sites::kFlightTarget);
    if (order == core::kOrderAvoid) {
        bool first = false;
        if (gc::keep_through_avoid(*g, m, now, &first)) { // B.3 item 2: the KC's warn named the own gate in funnel / hand-over
            if (first) ctl_evict_ignored_gate_.add();
            ctl_read_state(m, obj, fl, ph, now);
            return;
        }
        ctl_leave(*g, k, true, gc::Why::evict, now);
        return;
    }
    gc::avoid_over(m, now);
    bool formation_current = false;
    if (m.role == gc::Role::escort && order == core::kOrderNone) {
        const int li = gc::find(*g, m.leader);
        formation_current = li >= 0 && members_.current(obj, m.leader, m.id, g->m[li].id, now) != nullptr;
    }
    const gc::Why why = gc::validity(*g, m, order, target, now, formation_current);
    if (why != gc::Why::count) {
        ctl_leave(*g, k, why == gc::Why::evict, why, now);
        return;
    }
    ctl_read_state(m, obj, fl, ph, now);
    if (m.role == gc::Role::arrival && m.st.order != core::kOrderWarpIn && !gc::in_front(*g, m.st.x)) // 0x21 starts behind the plane
        ctl_leave(*g, k, false, gc::Why::window, now);                                                   // past the plane
}
// P4 (the order-5 trigger) and P5 (every order-8 frame) of a member: a capital hull is at the gate's plane (D.4, the small side spawn).
inline void ctl_depart(std::uint32_t obj) noexcept {
    int i = -1;
    gc::Gate* g = ctl_lookup(obj, &i);
    if (!g || !gc::pair_role(g->m[i].role) || g->m[i].id != rd32(obj + sites::kObjId)) return;
    const std::uint32_t now = game_time_ms();
    g->cap_depart_ms = now ? now : 1;
    g->cap_depart_w = g->m[i].w;
}
// A member pair's E reason row (Run161 item 3, item 4: kind 1 a pass at E, kind 2 a member's order-0 frame; --debug).
inline void ctl_pass_row(const gc::Gate& g, unsigned i, unsigned j, std::uint32_t now, std::uint8_t kind, gc::EWhy why) noexcept {
    const std::uint32_t w = ctl_pair_write_.load(std::memory_order_relaxed);
    if (w - ctl_pair_read_ >= 64) {
        row_dropped(Drop::ctl_pair);
        return;
    }
    const gc::Member& m = g.m[i];
    const gc::Member& b = g.m[j];
    const float dx[3] = {m.st.x[0] - b.st.x[0], m.st.x[1] - b.st.x[1], m.st.x[2] - b.st.x[2]};
    ctl_pair_rows_[w & 63] = CtlPairRow{g.gate, m.ship, b.ship, now, m.seq, b.seq, g.v[i][j], 0, g.tc[i][j], 0, std::uint8_t(gc::rank(m)),
                                        std::uint8_t(gc::rank(b)), kind, std::uint8_t(why), m.cap, core::sqrt_ss(core::dot3(dx, dx))};
    ctl_pair_write_.store(w + 1, std::memory_order_release);
}
// Site E's controller fields (D.1, D.4): the arrival-gate row for B = A's arrival gate; for B = a ship, whether A and B are live
// members of one board (the controller owns the pair: certified or handed back), and whether A is a capital arrival (the small-ship
// row takes it as fresh).
inline void ctl_e_fields(std::uint32_t a, std::uint32_t fl, std::uint32_t b, std::uint16_t cls_b, std::uint32_t now, av::SiteE& in) noexcept {
    if (cls_b == sites::kClassGate) {
        if (ctl_ix_.maybe(a)) { // B.2 item 3: a stage-B member against its own gate (funnel / handover / dock / launch, the path guard);
                                // gc::e_gate_row: the bitmap, the board, the id, the position checked against the member's state (review N6, N8)
            const std::uint32_t pa = rd32(a + sites::kObjPhys);
            float x[3] = {0, 0, 0};
            if (pa)
                for (unsigned i = 0; i < 3; ++i) x[i] = float(rds32(pa + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
            gc::note_step(ctl_, now, frame_step_ms()); // B.3 item 1: E may run before site B in a frame
            gc::GateRow why = gc::GateRow::not_member;
            in.ctl_gate = gc::e_gate_row(ctl_, ctl_ix_, a, rd32(a + sites::kObjId), b, in.a_order, in.a_target, x, pa != 0, now, &why);
            if (trace_on_) { // the E row's fields (e_record runs only with the trace on)
                e_gate_why_ = std::uint8_t(why);
                int mi = -1;
                const gc::Gate* mg = gc::locate(ctl_, a, &mi);
                e_gate_age_ = mg && mi >= 0 && mg->m[mi].st.ms ? std::int32_t(now - mg->m[mi].st.ms) : -1;
            }
        }
        if (!arr_ix_.maybe(a)) return;
        const gc::Arrival* r = gc::find_arrival(arrivals_, a, now);
        if (!r || r->gate != b || r->id != rd32(a + sites::kObjId)) return;
        const std::uint32_t pa = rd32(a + sites::kObjPhys);
        float G[3], R[3], U[3], F[3], r_gate = 0.0f;
        if (!pa || !gate_frame(b, G, R, U, F, &r_gate)) return;
        float x[3], u[3], slip[3];
        const float v = float(rds32(a + sites::kObjSpeed)) / core::kUnitsPerMetre;
        for (unsigned i = 0; i < 3; ++i) {
            x[i] = float(rds32(pa + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
            slip[i] = float(rds32(a + sites::kObjSlip + 4 * i)) / core::kUnitsPerMetre;
        }
        for (unsigned i = 0; i < 3; ++i) u[i] = v * fixed16(rds32(fl + sites::kFlightBasis + 0x20 + 4 * i)) + slip[i];
        in.arrival_gate = gc::arrival_gate_row(*r, b, now, x, u, G, F, r_gate, rd16(b + sites::kObjType));
        return;
    }
    if (cls_b != sites::kClassShip) return;
    if (arr_ix_.maybe(a)) {
        const gc::Arrival* r = gc::find_arrival(arrivals_, a, now);
        in.a_arrival = r && r->capital && r->id == rd32(a + sites::kObjId);
    }
    if (!ctl_ix_.maybe(a) || !ctl_ix_.maybe(b)) {
        if (ctl_ix_.maybe(a) != ctl_ix_.maybe(b) && capital_obj(a) && capital_obj(b)) e_pass_ctl_why_[unsigned(gc::EWhy::not_member)].add();
        return;
    }
    // B.4 item 3 (Run164 f 24561: a 310 ms SETA step after 50 ms steps): E runs before site B in a frame, so the member pair's certificate
    // is judged against this frame's step too (B.3 item 1), as the gate branch above does
    gc::note_step(ctl_, now, frame_step_ms());
    int ia = -1;
    gc::Gate* g = gc::locate(ctl_, a, &ia);
    if (!g) return;
    const int ib = gc::find(*g, b);
    if (ib < 0 || !gc::live(*g, g->m[ia], now) || !gc::live(*g, g->m[ib], now) || g->m[ia].id != rd32(a + sites::kObjId) ||
        g->m[ib].id != rd32(b + sites::kObjId)) {
        e_pass_ctl_why_[unsigned(gc::EWhy::not_member)].add();
        return;
    }
    const std::uint32_t fl_b = rd32(b + sites::kObjFlight);
    if (!fl_b) return;
    in.ctl_pair = true;
    float fa[3], fb[3];
    for (unsigned i = 0; i < 3; ++i) {
        fa[i] = fixed16(rds32(fl + sites::kFlightBasis + 0x20 + 4 * i));
        fb[i] = fixed16(rds32(fl_b + sites::kFlightBasis + 0x20 + 4 * i));
    }
    const std::uint16_t b_order = rd16(fl_b + sites::kFlightOrder);
    in.ctl_certified = gc::e_certified(*g, unsigned(ia), unsigned(ib), now, gc::unit_row(fa), gc::unit_row(fb), in.a_order, b_order);
    if (!in.ctl_certified) { // Run161 item 3: the reason of every pass_ctl, counted; one row per pair per second (--debug)
        const gc::EWhy why = gc::e_reason(*g, unsigned(ia), unsigned(ib), now, gc::unit_row(fa), gc::unit_row(fb), in.a_order, b_order);
        e_pass_ctl_why_[unsigned(why) < unsigned(gc::EWhy::count) ? unsigned(why) : 0u].add();
        std::uint32_t& last = g->pass_ms[ia][ib];
        if (debug_ && (!last || now - last >= 1000)) {
            last = now ? now : 1;
            ctl_pass_row(*g, unsigned(ia), unsigned(ib), now, 1, why);
        }
    }
}
// Site B's write for one ship (the clamp from its last decision), split out so the trace row follows it.
inline void clamp_apply(std::uint32_t obj, std::uint32_t fl, std::uint32_t ph) noexcept {
    const core::Ship* s = table_.find(obj);
    if (!s) return;
    const std::uint16_t order = rd16(fl + sites::kFlightOrder);
    float x[3];
    for (unsigned i = 0; i < 3; ++i) x[i] = float(rds32(ph + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
    const float v = core::clamp_for(*s, x, order, game_time_ms(), sites::kClampMaxAgeMs,
                                    dock_on_ ? dock_w_[unsigned(s - table_.entries)] : nullptr); // amendment 18: the live point
    if (v <= 0.0f) {
        clamp_stale_.add();
        return;
    }
    if (s->flags & (core::kShipFlagGate | core::kShipFlagGateDock)) { // the gate approach (P1) and the order-5 leg: the module's
        const std::int32_t units = core::gate_speed_units(v, rds32(obj + sites::kObjDesiredSpeed)); // speed in both directions
        if (units > 0) {
            wr32(obj + sites::kObjDesiredSpeed, units);
            if (s->flags & core::kShipFlagGate) gate_speed_writes_.add();
            else gate_dock_writes_.add();
        }
        return;
    }
    // Lowering only (compared in float before any conversion: core::clamp_units), never below the engine's own floor; a capital's lane
    // cap on this path rides in v_corner (kShipFlagLower, review F2).
    const std::int32_t units = core::clamp_units(v, rds32(obj + sites::kObjDesiredSpeed));
    if (units > 0) {
        wr32(obj + sites::kObjDesiredSpeed, units);
        clamp_applied_.add();
    }
}
}
extern "C" void __cdecl x3m_ship_steering_clamp(std::uint32_t obj) {
    if (!installed_.load(std::memory_order_relaxed)) return;
    Envelope env;
    clamp_calls_.add();
    const std::uint32_t flags = rd32(obj + sites::kObjFlags);
    if ((flags & (sites::kFlagDeleted | sites::kFlagAttached)) || rd32(obj + sites::kObjDocked)) {
        if (trace_on_ && rd32(obj + sites::kObjDocked)) trace_release(obj); // docked: the approach is over
        return;
    }
    const std::uint32_t fl = rd32(obj + sites::kObjFlight), ph = rd32(obj + sites::kObjPhys);
    if (!fl || !ph) return;
    const std::int32_t des_engine = rds32(obj + sites::kObjDesiredSpeed);
    clamp_apply(obj, fl, ph);
    // after the write: the row carries des as the module left it; amendment 17: an untraced capital's warn rows
    if (trace_on_ && !trace_frame(obj, fl, ph, des_engine)) capital_warn_watch(obj, fl);
    // the gate traffic controller: a member's live state and role (two loads for any other ship: the bitmap)
    if (traffic_live_.load(std::memory_order_relaxed) && ctl_ix_.maybe(obj)) {
        live_set_.add(obj, rd32(obj + sites::kObjId), gc::kLiveNoSector); // B.5: handed over in this update, flags tested above
        ctl_note_warn(obj, fl);
        ctl_observe(obj, fl, ph);
        if (debug_ && rd16(fl + sites::kFlightOrder) == core::kOrderNone) ctl_order0_rows(obj);
    }
}
// Run161 item 4 (--debug): at every order-0 frame of a member, the pair's E reason against each live partner within 5 km (the engine's
// warn range of two capitals; kind 2 rows, the ctl_pair ring).
namespace {
void ctl_order0_rows(std::uint32_t obj) noexcept {
    int i = -1;
    gc::Gate* g = ctl_lookup(obj, &i);
    if (!g || !gc::pair_role(g->m[i].role)) return;
    const std::uint32_t now = game_time_ms();
    const gc::Member& m = g->m[i];
    for (unsigned j = 0; j < g->n; ++j) {
        const gc::Member& b = g->m[j];
        if (int(j) == i || !gc::live(*g, b, now)) continue;
        const float dx[3] = {m.st.x[0] - b.st.x[0], m.st.x[1] - b.st.x[1], m.st.x[2] - b.st.x[2]};
        if (core::dot3(dx, dx) > 5000.0f * 5000.0f) continue;
        std::uint32_t& last = g->o0_ms[i][j]; // review F5: at most one row per pair per second
        if (last && now - last < 1000) continue;
        last = now ? now : 1;
        ctl_pass_row(*g, unsigned(i), j, now, 2, gc::e_reason(*g, unsigned(i), j, now, m.st.f_ok != 0, b.st.f_ok != 0, core::kOrderNone, b.st.order));
    }
}
}
// Step 3: the gate sites' handlers. Each runs on the light envelope inside the order handlers of the command pass
// (no log, no allocation, no lock); the stubs pass through to the claim tails when the group is not live.
namespace {
// The shared rule (core::gate_target_noncapital over the engine's memory): a class-7 non-capital ship with a gate target.
inline bool gate_target_noncapital(std::uint32_t obj) noexcept {
    RawMem mem;
    return core::gate_target_noncapital(mem, obj, rd32(sites::kShipTypeTableSlot));
}
inline bool gate_target_capital(std::uint32_t obj) noexcept {
    RawMem mem;
    return core::gate_target_capital(mem, obj, rd32(sites::kShipTypeTableSlot));
}
inline GateState* gate_state_of(std::uint32_t obj) noexcept {
    const core::Ship* s = table_.find(obj);
    if (!s) return nullptr;
    GateState* g = &gate_[unsigned(s - table_.entries)];
    return g->obj == obj ? g : nullptr;
}
}
// P2 (0x00454b91): the order-4 arrival. [obj+0x14] = 0 always; [obj+0x10] = 0 unless a non-capital ship arrived at a
// gate's approach point (its speed carries into the KC's order 5), or a capital crossed its grid point's sphere on a
// validated approach within the 0x1f tolerance (amendment 13: RotateToAngles ends on its first frame, order 5 starts at speed).
extern "C" void __cdecl x3m_ship_gate_stop_a(std::uint32_t obj) {
    Envelope env;
    gate_p2_.add();
    wr32(obj + sites::kObjDesiredSpeed, 0);
    const bool live = installed_.load(std::memory_order_relaxed);
    GateState* g = live ? gate_state_of(obj) : nullptr;
    const std::uint32_t fl = rd32(obj + sites::kObjFlight), tgt = fl ? rd32(fl + sites::kFlightTarget) : 0;
    const bool gate_target = tgt && rd16(tgt + sites::kObjClass) == sites::kClassGate;
    // the crossing: the engine's arrival test fired for this ship (its own 0.75 r test runs before the frame's leaf call)
    if (g && g->validated && !g->lead_counted) { // per approach: the lead-in verdict is final here
        g->lead_counted = 1;
        if (!g->capital) {
            if (g->validated == 1) gate_lead_validated_.add();
            else gate_lead_fallback_.add();
        } else if (gates_capital_on_) {
            if (g->validated == 1) gate_capital_lead_validated_.add();
            else gate_capital_lead_fallback_.add();
        }
    }
    if (g && !g->crossed) { // the crossing: position, forward and dist(A) in the gate frame for the diagnostic row
        const std::uint32_t ph = rd32(obj + sites::kObjPhys), fl = rd32(obj + sites::kObjFlight);
        if (ph && fl && g->r_gate > 0.0f) {
            float x[3], fwd[3];
            for (unsigned i = 0; i < 3; ++i) {
                x[i] = float(rds32(ph + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre - g->G[i];
                fwd[i] = fixed16(rds32(fl + sites::kFlightBasis + 0x20 + 4 * i));
            }
            for (unsigned i = 0; i < 3; ++i) {
                g->p2_pos[i] = core::dot3(x, g->rows[i]) / g->r_gate;
                g->p2_fwd[i] = core::dot3(fwd, g->rows[i]);
            }
            const float a[3] = {g->A_local[0] - g->p2_pos[0], g->A_local[1] - g->p2_pos[1], g->A_local[2] - g->p2_pos[2]};
            g->p2_dA_r = core::sqrt_ss(core::dot3(a, a));
            g->p2_stamp = game_time_ms();
        }
    }
    if (g && g->validated == 1 && !g->capital && !g->crossed) {
        g->crossed = 1;
        g->launched = 0;
        g->cross_stamp = game_time_ms();
        g->cross_v = float(rds32(obj + sites::kObjSpeed)) / core::kUnitsPerMetre;
        g->cross_err_deg = g->last_err_deg;
        gate_cross_.add();
        gate_spread_offsets_.add();
    }
    // fail closed: only a ship the policy steered (a gate state with the validated geometry) with a gate target and a
    // non-capital record keeps its speed, or (amendment 13) a capital on a validated approach whose last decision's heading
    // error to F is within the 0x1f tolerance; the reasons for the vanilla stop are counted
    const bool noncap = gate_target_noncapital(obj);
    const bool cap = !noncap && gate_target_capital(obj);
    bool cap_keep = false;
    if (g && g->capital) { // the capital's alignment at its arrival (the grid point's 0.75 r sphere), per P2 call
        const bool aligned = g->last_err_deg >= 0.0f && g->last_err_deg <= core::kGateRotateTolDeg;
        const bool first = !g->crossed;
        if (first) {
            g->crossed = 1;
            g->launched = 0;
            g->cross_stamp = game_time_ms();
            g->cross_v = float(rds32(obj + sites::kObjSpeed)) / core::kUnitsPerMetre;
            g->cross_err_deg = g->last_err_deg;
            if (aligned) gate_capital_aligned_.add();
            if (g->validated == 1 && gates_capital_on_) gate_cross_.add();
            if (debug_) gate_event(*g, obj, 0, 0);
        }
        // Amendment 15 (section 15.3 item 4): keep [obj+0x10] only in join mode with the nose within 8.11 deg of F, the velocity
        // (nose speed plus the slip obj+0x60) within 10 deg and the speed under the coast bound; read live here
        float nose_deg = -1.0f, vel_deg = -1.0f, v_ms = 0.0f;
        {
            const std::uint32_t flp = rd32(obj + sites::kObjFlight);
            if (flp) {
                float fwd[3], u[3];
                v_ms = float(rds32(obj + sites::kObjSpeed)) / core::kUnitsPerMetre;
                for (unsigned i = 0; i < 3; ++i) {
                    fwd[i] = fixed16(rds32(flp + sites::kFlightBasis + 0x20 + 4 * i));
                    u[i] = v_ms * fwd[i] + float(rds32(obj + sites::kObjSlip + 4 * i)) / core::kUnitsPerMetre;
                }
                float c = core::dot3(fwd, g->rows[2]);
                nose_deg = core::acos_deg(c > 1.0f ? 1.0f : c < -1.0f ? -1.0f : c);
                const float un = core::sqrt_ss(core::dot3(u, u));
                if (un > 1e-3f) {
                    c = core::dot3(u, g->rows[2]) / un;
                    vel_deg = core::acos_deg(c > 1.0f ? 1.0f : c < -1.0f ? -1.0f : c);
                } else
                    vel_deg = nose_deg;
            }
        }
        // stage B (D.6): a member of the traffic controller crossing from its funnel takes the join mode's place in the verdict
        bool sb_funnel = false;
        if (live && traffic_on_ && cap && g->validated == 1) {
            int ci = -1;
            const gc::Gate* cg = ctl_lookup(obj, &ci);
            sb_funnel = cg && cg->gate == tgt && cg->m[ci].id == rd32(obj + sites::kObjId) && gc::sb_funnel(cg->m[ci]);
        }
        const bool join_mode = cap && gates_capital_on_ && g->validated == 1 && (g->join.mode == core::JoinMode::join || sb_funnel);
        const core::JoinKeep why = core::join_keep_verdict(join_mode, nose_deg, vel_deg, v_ms, core::join_coast_bound(g->xg_m, g->r_ship, v_ms, g->a_lin));
        cap_keep = live && why == core::JoinKeep::kept;
        if (cap_keep && sb_funnel) { // the P2 stub's contract (ship_steering_sites.h (h)): on a kept stage-B hand-over the three turn
            wr32(obj + sites::kObjRemaining, 0);      // remainders obj+0x30/+0x34/+0x38 are zeroed, so order 0 and a first-frame 0x1f
            wr32(obj + sites::kObjRemaining + 4, 0);  // hold the attitude the funnel delivered (ship-navigation-ai.md 10.11 Q4, D.12)
            wr32(obj + sites::kObjRemaining + 8, 0);
            ctl_p2_zeroed_.add();
        }
        g->kept = cap_keep ? 1 : 0;
        g->handover = cap_keep ? 1 : 0; // site E's gate row covers 0x1f / order 0 until order 5 (audit G3)
        // amendment 16 (sections 16.1, 16.3): the hand-over after P2 kept or not (E's gate row under the path guard; the lane entry
        // stays committed on 0x1f / order 0 with the state at the crossing and the speed P2 leaves)
        if (live && gates_capital_on_ && cap && g->validated == 1 && g->gate == tgt) {
            g->p2_seen = 1;
            const std::uint32_t php = rd32(obj + sites::kObjPhys), flp = rd32(obj + sites::kObjFlight);
            if (php && flp) {
                float x[3], fwd[3];
                for (unsigned i = 0; i < 3; ++i) {
                    x[i] = float(rds32(php + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
                    fwd[i] = fixed16(rds32(flp + sites::kFlightBasis + 0x20 + 4 * i));
                }
                const float vF = cap_keep ? v_ms * core::dot3(fwd, g->rows[2]) : 0.0f;
                if (!traffic_on_) ln::handover(lanes_, g->gate, obj, rd32(obj + sites::kObjId), game_time_ms(), x, fwd, vF);
            }
            if (traffic_on_) { // the controller: the committed member stays committed through order 0 / 0x1f (kHandoverMaxMs)
                int ci = -1;
                gc::Gate* cg = ctl_lookup(obj, &ci);
                if (cg && cg->gate == g->gate && cg->m[ci].role == gc::Role::committed && cg->m[ci].id == rd32(obj + sites::kObjId)) {
                    const std::uint32_t t = game_time_ms();
                    cg->m[ci].handover_ms = t ? t : 1;
                    ctl_row(*cg, cg->m[ci], gc::Role::committed, gc::Role::committed, gc::Why::handover, t);
                }
            }
        }
        if (first && gates_capital_on_ && cap && g->validated == 1) {
            if (cap_keep) gate_capital_kept_.add();
            else gate_capital_not_kept_[unsigned(why) < unsigned(core::JoinKeep::count) ? unsigned(why) : 0u].add();
            if (trace_on_ && trace_slot_of(obj))
                join_row(*g, obj, game_time_ms(), true, nose_deg, vel_deg, v_ms, cap_keep ? 1 : 0, std::uint8_t(why));
        }
    }
    if (live && core::gate_stop_keeps_speed(noncap, g != nullptr && g->validated == 1)) {
        gate_stops_skipped_.add();
        return;
    }
    if (cap_keep) {
        gate_stops_skipped_.add();
        return;
    }
    if (!gate_target) gate_p2_no_gate_target_.add();
    else if (!noncap) gate_p2_capital_.add();
    else gate_p2_no_state_.add();
    wr32(obj + sites::kObjSpeed, 0);
}
// P4 (0x00454700): the order-5 arrival. [obj+0x14] = 0 always; for a non-capital ship at a gate's pass point, or a capital
// on a validated approach under the capital policy (amendment 13), the speed is kept and recorded for the launch (P5), else
// zeroed as the engine does.
extern "C" void __cdecl x3m_ship_gate_stop_b(std::uint32_t obj) {
    Envelope env;
    gate_p4_.add();
    wr32(obj + sites::kObjDesiredSpeed, 0);
    if (traffic_live_.load(std::memory_order_relaxed) && installed_.load(std::memory_order_relaxed)) ctl_depart(obj); // D.4: at the plane
    GateState* g = installed_.load(std::memory_order_relaxed) ? gate_state_of(obj) : nullptr;
    const bool noncap = gate_target_noncapital(obj);
    const bool cap_keep = g != nullptr && g->capital && g->validated == 1 && gates_capital_on_ && !noncap && gate_target_capital(obj);
    if (core::gate_stop_keeps_speed(noncap, g != nullptr && g->validated == 1) || cap_keep) { // the same per-approach verdict as P2
        g->v0_units = rds32(obj + sites::kObjSpeed);
        g->v0_stamp = game_time_ms();
        g->launched = 0;
        gate_v0_kept_.add();
        return;
    }
    {
        RawMem mem;
        const std::uint32_t fl = rd32(obj + sites::kObjFlight), tgt = fl ? rd32(fl + sites::kFlightTarget) : 0;
        if (!tgt || rd16(tgt + sites::kObjClass) != sites::kClassGate) gate_p4_no_gate_target_.add();
        else if (!core::ship_noncapital(mem, obj, rd32(sites::kShipTypeTableSlot))) gate_p4_capital_.add();
        else gate_p4_no_state_.add();
    }
    wr32(obj + sites::kObjSpeed, 0);
}
// P5 (0x0045526f): the order-8 launch distance after t ms: the engine's t^2 law plus v0 t for a ship whose speed
// was kept at the pass point within the last 5 s (any other ship: the engine's own value).
extern "C" std::int32_t __cdecl x3m_ship_gate_launch(std::uint32_t obj, std::int32_t t_ms) {
    Envelope env;
    gate_p5_.add();
    std::int32_t v0 = 0;
    if (traffic_live_.load(std::memory_order_relaxed) && installed_.load(std::memory_order_relaxed)) ctl_depart(obj); // D.4: at the plane
    if (installed_.load(std::memory_order_relaxed)) {
        GateState* g = gate_state_of(obj);
        const std::uint32_t now = game_time_ms();
        if (g && g->v0_stamp && now - g->v0_stamp <= 5000) {
            v0 = g->v0_units > 0 ? g->v0_units : 0;
            if (!g->launched) {
                g->launched = 1;
                g->launch_stamp = now;
                if (v0 > 0) gate_launch_continued_.add();
                if (debug_) gate_event(*g, obj, v0, g->crossed ? now - g->cross_stamp : 0);
                if (trace_on_) trace_release(obj); // the approach is over
            }
        }
    }
    return core::gate_launch_units(v0, t_ms);
}
// Site W (0x00465bac, the SA_WarpObjectIn case): before the case copies the ship's position into f+0x240, offset it
// laterally in the gate's x-y plane by the ship's ring position (kGateExitSpreadR r_gate; never along the axis) for a
// non-capital ship warping in at a gate; every position of the warp-in (the case's write, the handler's per-frame
// writes and the final snap) is derived from that copy, so the whole animation carries the offset.
extern "C" void __cdecl x3m_ship_gate_warp_in(std::uint32_t obj, std::uint32_t target) {
    Envelope env;
    gate_warp_calls_.add();
    if (!installed_.load(std::memory_order_relaxed)) return;
    if (traffic_live_.load(std::memory_order_relaxed)) { // stage B (D.6): the old exit spread is deleted under the controller (the
        gate_warp_skipped_.add();                         // launch window 0x00444606 places arrivals); counted with the skips
        return;
    }
    RawMem mem;
    std::int32_t pos[3];
    // the shared rule (core::gate_warp_in_offset, unit-checked by the fixture): class-7 non-capital, class not excluded,
    // autopilot set (never the player's ship), not deleted/attached, not already on order 0x21, a class-0x12 target with a
    // valid frame, the int32 sum guarded
    if (!core::gate_warp_in_offset(mem, obj, target, rd32(sites::kShipTypeTableSlot), class_mask_, pos)) {
        gate_warp_skipped_.add();
        return;
    }
    const std::uint32_t ph = rd32(obj + sites::kObjPhys);
    for (unsigned i = 0; i < 3; ++i) wr32(ph + sites::kPhysPosition + 4 * i, pos[i]);
    gate_exit_offsets_.add();
}
// The gate traffic controller's launch window 0x00444606 (D.4; ship_steering_sites.h (n)): the exit point P (phys+0x30, already
// written by the launch 0x00444170) of a ship launched at a gate. A capital: P += l_a U with l_a = 0.6 r_gate (on a Terran ring
// min(0.6 r, 0.663 r - w - 50 m); an unknown gate row: none); it becomes an arrival member of the gate's board. A small ship keeps the
// engine's upper fan, except while a capital hull is at the plane (a capital launched here within 6 s, or a departing member on order 8
// or within 3 s of its order-5 trigger): then the side spawn (+-0.87 r, +0.2 r) by its exit slot's parity, or vanilla with
// arr_small_unseparated when that capital is wider than 0.68 r. Every launch is recorded for site E's arrival-gate row and logs one
// ship_gate_launch row (--debug). Never the player's object or a ship under manual control; the int32 write is guarded (a P that would
// leave the int32 range keeps the engine's value); nothing else is written. Runs inside the sector post pass, once per launch.
extern "C" void __cdecl x3m_ship_gate_arrival(std::uint32_t obj, std::uint32_t gate) {
    Envelope env;
    if (!installed_.load(std::memory_order_relaxed) || !traffic_live_.load(std::memory_order_relaxed)) return;
    const std::uint64_t t0 = timing_ ? qpc() : 0;
    arr_launches_.add();
    const std::uint32_t now = game_time_ms();
    std::uint32_t ph = 0;
    bool ok = obj && gate && rd16(obj + sites::kObjClass) == sites::kClassShip && rd16(gate + sites::kObjClass) == sites::kClassGate;
    if (ok) {
        const std::uint32_t flags = rd32(obj + sites::kObjFlags);
        ph = rd32(obj + sites::kObjPhys);
        ok = ph && !(flags & sites::kFlagPlayerObject) && !((flags & sites::kFlagPlayerControl) && !(flags & sites::kFlagAutopilot));
    }
    float G[3], R[3], U[3], F[3], r_gate = 0.0f;
    if (ok) ok = gate_frame(gate, G, R, U, F, &r_gate);
    if (!ok) {
        arr_skipped_.add();
        return;
    }
    const std::uint16_t type = rd16(gate + sites::kObjType);
    const std::int32_t stype = std::int16_t(rd16(obj + sites::kObjType));
    const std::uint32_t table = rd32(sites::kShipTypeTableSlot);
    const std::uint32_t code = table && stype >= 0 ? rd32(table + std::uint32_t(stype) * sites::kRecordStride + sites::kRecordClassCode) : 0xffffffffu;
    const bool capital = code < core::kClassCount && ((core::kCapitalMask >> code) & 1u);
    const float r = float(rds32(obj + sites::kObjRadius)) / core::kUnitsPerMetre;
    float w = r, l = r;
    if (capital) {
        float tw = 0.0f, tl = 0.0f;
        if (type_width_get(stype, &tw, &tl) && tw > 0.0f && tw <= r) {
            w = tw;
            l = tl > r ? tl : r;
        }
    }
    float P[3];
    for (unsigned i = 0; i < 3; ++i) P[i] = float(rds32(ph + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
    const float d[3] = {P[0] - G[0], P[1] - G[1], P[2] - G[2]};
    const float loc[3] = {core::dot3(d, R), core::dot3(d, U), core::dot3(d, F)};
    gc::Gate* g = gc::acquire_gate(ctl_, gate, now, G, R, U, F, r_gate, type);
    float off[2] = {0.0f, 0.0f};
    std::uint8_t kind = 0;
    std::int32_t slot = -1;
    if (capital) {
        const float la = gc::capital_offset(r_gate, w, type);
        if (la > 0.0f) {
            off[1] = la;
            kind = 1;
        }
        if (g) {
            g->cap_arrival_ms = now ? now : 1;
            g->cap_arrival_w = w;
        }
    } else if (g) {
        float wcap = 0.0f;
        if (gc::capital_at_plane(*g, now, &wcap)) {
            const std::uint32_t gf = rd32(gate + sites::kObjFlight);
            for (unsigned i = 0; gf && i < sites::kGateFlightSlotCount; ++i)
                if (rd32(gf + sites::kGateFlightSlots + 4 * i) == obj) {
                    slot = std::int32_t(i);
                    break;
                }
            float xl = 0.0f, yl = 0.0f;
            if (slot >= 0 && gc::small_side_spawn(r_gate, slot, wcap, &xl, &yl)) {
                off[0] = xl - loc[0];
                off[1] = yl - loc[1];
                kind = 2;
            } else {
                kind = 3;
                arr_small_unseparated_.add();
            }
        }
    }
    if (kind == 1 || kind == 2) {
        std::int32_t Nu[3];
        bool fits = true;
        for (unsigned i = 0; i < 3; ++i) {
            const float p = (P[i] + off[0] * R[i] + off[1] * U[i]) * core::kUnitsPerMetre;
            if (!core::finite_f(p) || !(core::fabs_f(p) < 2.0e9f)) fits = false;
            else Nu[i] = core::round_to_int(p);
        }
        if (fits) {
            for (unsigned i = 0; i < 3; ++i) {
                wr32(ph + sites::kPhysPosition + 4 * i, Nu[i]);
                P[i] = float(Nu[i]) / core::kUnitsPerMetre;
            }
            if (kind == 1) arr_offset_capital_.add();
            else arr_small_side_.add();
        } else {
            kind = 0;
            off[0] = off[1] = 0.0f;
            arr_skipped_.add();
        }
    }
    const std::uint32_t id = rd32(obj + sites::kObjId);
    gc::record_arrival(arrivals_, obj, id, gate, now, w, capital);
    arr_ix_.set(obj);
    if (capital && g) {
        gc::Member* m = ctl_enter(*g, obj, id, gc::Role::arrival, gc::Why::arrival, now);
        if (m) {
            m->launch_ms = now ? now : 1;
            m->r = r;
            m->w = w;
            m->L = l;
            m->large = 1;
            m->line_ok = 0;
            m->leader = 0;
            m->params = core::JoinParams{};
            m->params.r_gate = r_gate;
            m->cap = -1.0f;
            gc::State& st = m->st;
            st = gc::State{};
            st.ms = now ? now : 1;
            st.order = core::kOrderWarpIn;
            st.target = gate;
            for (unsigned i = 0; i < 3; ++i) { // facing out of the gate face (the launch's 180 deg yaw): rows (-R, U, -F)
                st.x[i] = st.point[i] = P[i];
                st.rows[0][i] = -R[i];
                st.rows[1][i] = U[i];
                st.rows[2][i] = -F[i];
            }
            st.f_ok = 1;
            st.left_ms = 900;
        }
    }
    if (debug_) {
        const std::uint32_t wv = launch_row_write_.load(std::memory_order_relaxed);
        if (wv - launch_row_read_ >= 8)
            row_dropped(Drop::launch);
        else {
            LaunchRow& row = launch_rows_[wv & 7];
            row = LaunchRow{obj, gate, code, now, slot, type, kind, std::uint8_t(capital ? 1 : 0), {P[0], P[1], P[2]}, {off[0], off[1]}, r_gate, r, w};
            launch_row_write_.store(wv + 1, std::memory_order_release);
        }
    }
    if (timing_) {
        const std::uint64_t dt = qpc() - t0;
        const std::uint32_t ticks = dt > 0xffffffffull ? 0xffffffffu : std::uint32_t(dt);
        if (ticks > ctl_arrival_max_ticks_) ctl_arrival_max_ticks_ = ticks;
    }
}
// Site E: 1 = suppress the engine's reactive warn for (A, B), 0 = the original warn test runs. Nothing is written; every read
// is the direct reader the snapshot walk uses, behind the class word, pointer and id checks below. Amendment 15
// (capital-gate-transit.md section 15.1) with the scenario audit's corrections (G1-G4):
//  * gate rows (ship_steering_gates): B = A's own target gate (class 0x12). A capital on order 4 in join mode on its validated
//    approach (decided on within the fresh window), or in the 0x1f / order-0 hand-over after P2 kept its speed, keyed on the gate
//    state until order 5 (G3) -> `e_suppressed_gate`; a non-capital on order 4 whose validated lead-in was active within the fresh
//    window (G1) -> `e_suppressed_gate_small`. Behind ship_steering_gates_capital for the capital row.
//  * the capital branch (A class 7, capital-coded, decided on within max(250 ms, 4 frame steps): G2): the small-ship row for any
//    B < r_A / 3 with a readable non-capital code whatever its order (A on 0x12 too: its 200 ms re-score runs the same test),
//    except no autopilot, deleted/attached/docked, a masked class, B on 5/0x1e/0x20 (G4) or B A's own leader; the member row for a
//    non-small B that is a current member of A's formation in the engine's transient with the hulls apart (A not on 0x12).
//  * the first branch: A not on 0x12 and B in A's resolved set within max(100 ms, 4 frame steps).
// Fail-safe: any unreadable field, A not in the table or not fresh: 0.
namespace {
__attribute__((always_inline)) inline float pair_distance_m(std::uint32_t a, std::uint32_t b) noexcept { // centre distance, m (-1: unreadable)
    const std::uint32_t pa = rd32(a + sites::kObjPhys), pb = rd32(b + sites::kObjPhys);
    if (!pa || !pb) return -1.0f;
    float d2 = 0.0f;
    for (unsigned i = 0; i < 3; ++i) {
        const float d = float(rds32(pa + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre - float(rds32(pb + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
        d2 += d * d;
    }
    return core::sqrt_ss(d2);
}
// --debug: the verdict for a traced or capital A (the warn_pre row's per-entry verdict) and, for trace slot 0 or any capital as A
// (amendment 17, section 17 item 5), the verdict row (one per pair per second of game time while the pair is in the dedup table).
void e_record(std::uint32_t a, std::uint32_t b, EV v, std::uint32_t now) noexcept {
    const bool slot0 = a == trace_slots_[0].obj;
    const bool capital = !slot0 && capital_obj(a);
    if (!capital && !trace_slot_of(a)) return;
    {
        unsigned k = 0;
        while (k < kELast && !(e_last_[k].a == a && e_last_[k].b == b)) ++k;
        if (k == kELast) {
            k = e_last_next_;
            e_last_next_ = (e_last_next_ + 1) & (kELast - 1);
        }
        e_last_[k] = EVerdictSeen{a, b, now, std::uint8_t(v)};
    }
    if (!slot0 && !capital) return;
    if (v == EV::suppress_ctl || v == EV::pass_ctl) return; // D.7: member pairs log ship_gate_ctl_pair on a verdict change instead
    unsigned k = 0, free_k = kELogged;
    for (; k < kELogged; ++k) {
        const EVerdictSeen& s = e_logged_[k];
        if (s.a == a && s.b == b) break;
        if (free_k == kELogged && (!s.a || now - s.ms >= 1000)) free_k = k;
    }
    if (k < kELogged && now - e_logged_[k].ms < 1000) return; // logged within the second
    if (k == kELogged) {
        if (free_k == kELogged) { // review F4: the table holds kELogged pairs logged within the second: drop, never evict
            row_dropped(Drop::e_table);
            return;
        }
        k = free_k;
    }
    const std::uint32_t w = e_row_write_.load(std::memory_order_relaxed);
    if (w - e_row_read_ >= kERows) {
        row_dropped(Drop::e_ring);
        return;
    }
    e_logged_[k] = EVerdictSeen{a, b, now, std::uint8_t(v)};
    EVerdictRow& r = e_rows_[w & (kERows - 1)];
    r = EVerdictRow{};
    r.a = a;
    r.b = b;
    r.verdict = std::uint8_t(v);
    r.gate_row = e_gate_why_;
    r.gate_age_ms = e_gate_age_;
    r.step_ms = frame_step_ms();
    const std::uint32_t fla = rd32(a + sites::kObjFlight);
    r.a_order = fla ? rd16(fla + sites::kFlightOrder) : 0;
    r.b_class = rd16(b + sites::kObjClass);
    r.b_code = 0xffff;
    r.b_flags = rd32(b + sites::kObjFlags) & (sites::kFlagAutopilot | sites::kFlagDeleted | sites::kFlagAttached);
    r.r_b = float(rds32(b + sites::kObjRadius)) / core::kUnitsPerMetre;
    r.d = pair_distance_m(a, b);
    r.member_age_ms = r.a_age_ms = r.b_age_ms = -1;
    if (const core::Ship* sa = table_.find(a)) r.a_age_ms = std::int32_t(now - sa->stamp_ms);
    if (r.b_class == sites::kClassShip) {
        const std::int32_t type = std::int16_t(rd16(b + sites::kObjType));
        const std::uint32_t tt = rd32(sites::kShipTypeTableSlot);
        if (tt && type >= 0) r.b_code = rd32(tt + std::uint32_t(type) * sites::kRecordStride + sites::kRecordClassCode);
        const std::uint32_t flb = rd32(b + sites::kObjFlight);
        if (flb) {
            r.b_order = rd16(flb + sites::kFlightOrder);
            r.b_leader = rd32(flb + sites::kFlightTarget);
        }
        if (const av::Member* m = members_.find(b)) {
            r.member_leader = m->leader;
            r.member_age_ms = std::int32_t(now - m->confirmed_ms);
        }
        if (const core::Ship* sb = table_.find(b)) r.b_age_ms = std::int32_t(now - sb->stamp_ms);
    }
    e_row_write_.store(w + 1, std::memory_order_release);
}
inline int e_verdict(std::uint32_t a, std::uint32_t b, std::uint32_t now, EV* out) noexcept {
    *out = EV::none;
    e_gate_why_ = 0xff;
    if (!a || !b || rd16(a + sites::kObjClass) != sites::kClassShip) return 0;
    const std::uint32_t fl = rd32(a + sites::kObjFlight);
    if (!fl) return 0;
    av::SiteE in;
    in.a_order = rd16(fl + sites::kFlightOrder);
    in.a_target = rd32(fl + sites::kFlightTarget);
    const core::Ship* s = table_.find(a);
    in.a_in_table = s != nullptr;
    const std::uint16_t cls_b = rd16(b + sites::kObjClass);
    if (traffic_live_.load(std::memory_order_relaxed)) { // the gate traffic controller's pairs need no steering decision of A
        ctl_e_fields(a, fl, b, cls_b, now, in);
        if (((in.arrival_gate || in.ctl_gate) && cls_b == sites::kClassGate) || (in.ctl_pair && cls_b == sites::kClassShip)) {
            in.b_gate = cls_b == sites::kClassGate;
            in.b_ship = cls_b == sites::kClassShip;
            const EV v = av::site_e_verdict(in);
            *out = v;
            if (v == EV::suppress_arrival_gate) e_suppressed_arrival_gate_.add();
            else if (v == EV::suppress_gate) e_suppressed_gate_.add(); // B.2 item 3
            else if (v == EV::suppress_ctl) e_suppressed_ctl_.add();
            else if (v == EV::pass_ctl) e_pass_ctl_.add();
            return av::e_suppresses(v) ? 1 : 0;
        }
    }
    if (!s && !in.a_arrival) {
        *out = EV::pass_not_fresh;
        return 0;
    }
    const unsigned slot = s ? unsigned(s - table_.entries) : 0u; // slot is read only with s (the gate rows, the resolved set)
    in.a_age_ms = s ? now - s->stamp_ms : 0xffffffffu;
    in.frame_ms = frame_step_ms();
    in.avoidance_on = avoidance_on_;
    // amendment 18: B is A's own container on order 4 (docking-approach.md item 1): the row's verdict (none for a carrier within 0.8 r_C)
    if (dock_on_ && s && in.a_order == core::kOrderFlyBefore && b == in.a_target && av::dock_container(in.a_order, cls_b, rd32(b + sites::kObjFlags)))
        in.dock = dock_e(a, fl, b, cls_b, *s);
    if (cls_b == sites::kClassGate && s) { // the gate rows: only A's own gate state is read
        const GateState& g = gate_[slot];
        in.b_gate = true;
        in.gates_live = gates_live_.load(std::memory_order_relaxed);
        in.capital_policy = gates_capital_on_;
        // Run161 item 5: under the traffic key a small ship's order 0 (f+0x224 zeroed) in the hand-over gap keeps its gate state
        in.small_gap_ms = traffic_on_ ? kSmallGapMs : 0u;
        in.gate_state = g.obj == a && g.gate == b && g.validated == 1 &&
                        (g.capital || in.a_target == b || (in.small_gap_ms && in.a_order == core::kOrderNone && !in.a_target));
        in.gate_capital = g.capital != 0;
        in.gate_join = g.join.mode == core::JoinMode::join && in.a_target == b;
        in.gate_handover = g.handover != 0;
        in.p2_age_ms = g.p2_stamp ? now - g.p2_stamp : 0xffffffffu;
        in.lead_age_ms = g.lead_ms ? now - g.lead_ms : 0xffffffffu;
        if (in.gate_state && in.gates_live) { // amendment 16 (sections 16.3 and 16.4): the extended rows and their path guards
            const std::uint32_t pa = rd32(a + sites::kObjPhys);
            float x[3], P[3];
            for (unsigned i = 0; i < 3; ++i) {
                x[i] = pa ? float(rds32(pa + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre : 0.0f;
                P[i] = g.G[i] + (g.A_local[0] * g.rows[0][i] + g.A_local[1] * g.rows[1][i] + g.A_local[2] * g.rows[2][i]) * g.r_gate;
            }
            const float r_a = float(rds32(a + sites::kObjRadius)) / core::kUnitsPerMetre;
            if (g.capital && gates_capital_on_) {
                if (g.id != rd32(a + sites::kObjId)) in.gate_state = false; // the state is A's own: object and id
                in.gate_slow_join = g.join.mode == core::JoinMode::slow_join && in.a_target == b;
                in.gate_fallback = g.join.mode == core::JoinMode::fallback && in.a_target == b;
                in.gate_window_ok = g.join.plane_ms && g.join.plane.s >= -core::kGateCapitalJoinR * g.r_gate;
                in.gate_p2_seen = g.p2_seen && g.p2_stamp;
                in.gate_path_ok = pa && ln::path_guard(g.G, x, P, g.r_gate, r_a, ln::kCapitalGuardR);
            } else if (!g.capital && (in.a_order == core::kOrderFlyBefore || (in.small_gap_ms && in.a_order == core::kOrderNone)) && pa)
                in.gate_small_guard = ln::path_guard(g.G, x, P, g.r_gate, r_a, ln::kSmallGuardR);
        }
    } else if (cls_b == sites::kClassShip && in.dock != av::DockE::none) {
        in.b_ship = true; // A's own carrier: the row decides (no lane, small-ship or member row reads)
    } else if (cls_b == sites::kClassShip) {
        in.b_ship = true;
        // amendment 16 / 16b (sections 16.1, 16.10): both capitals committed to A's gate (live order, f+0x224 and id) and the pair's
        // verdict from the stored board in both directions (review F6) carries a certificate (review F1: the footprint route of (a),
        // or (s) lateral / along F) -> suppress_lane; a corridor-only (a) pair keeps the engine's warn
        if (gates_capital_on_ && !traffic_on_) { // the controller deletes the lane certificates (D.6)
            const ln::Gate* lg = ln::gate_of(lanes_, in.a_target);
            const ln::Entry* ea = lg ? ln::find(*lg, a) : nullptr;
            const ln::Entry* eb = ea ? ln::find(*lg, b) : nullptr;
            const std::uint32_t fl_b = eb ? rd32(b + sites::kObjFlight) : 0;
            if (fl_b) {
                const std::uint32_t fresh = av::fresh_window_ms(av::kSteeredFreshMs, in.frame_ms, av::kSteeredReplanMs);
                const std::uint16_t b_order = rd16(fl_b + sites::kFlightOrder);
                if (ln::committed(*ea, now, fresh, in.a_order, in.a_target, lg->gate, rd32(a + sites::kObjId)) &&
                    ln::committed(*eb, now, fresh, b_order, rd32(fl_b + sites::kFlightTarget), lg->gate, rd32(b + sites::kObjId))) {
                    // amendment 17: a hand-over side's nose from its live flight basis (the forward row the trace reads), never P2's
                    // (review F5: read only for a side in the hand-over, the only one that uses it)
                    float fa[3] = {0, 0, 0}, fb[3] = {0, 0, 0};
                    const bool ha = ea->order == core::kOrderNone || ea->order == core::kOrderRotate;
                    const bool hb = eb->order == core::kOrderNone || eb->order == core::kOrderRotate;
                    for (unsigned i = 0; i < 3 && (ha || hb); ++i) {
                        if (ha) fa[i] = fixed16(rds32(fl + sites::kFlightBasis + 0x20 + 4 * i));
                        if (hb) fb[i] = fixed16(rds32(fl_b + sites::kFlightBasis + 0x20 + 4 * i));
                    }
                    ln::PairInfo info;
                    const ln::Verdict lv = ln::verdict_sym(*lg, *ea, *eb, now, fresh, seq_on_, &info, true, fa, fb);
                    in.lane_exempt = ln::certified(lv, info.route);
                }
            }
        }
        const std::uint32_t table = rd32(sites::kShipTypeTableSlot);
        const std::int32_t type_a = std::int16_t(rd16(a + sites::kObjType)), type_b = std::int16_t(rd16(b + sites::kObjType));
        in.codes_readable = table && type_a >= 0 && type_b >= 0;
        if (in.codes_readable) {
            const std::uint32_t code_a = rd32(table + std::uint32_t(type_a) * sites::kRecordStride + sites::kRecordClassCode);
            in.a_capital = code_a < core::kClassCount && ((core::kCapitalMask >> code_a) & 1u);
            if (in.a_capital) { // B's fields only for a capital A (the small and member rows)
                const std::uint32_t code_b = rd32(table + std::uint32_t(type_b) * sites::kRecordStride + sites::kRecordClassCode);
                in.b_readable = code_b < core::kClassCount; // unreadable: never a small or member pair (fail closed)
                in.b_capital_code = in.b_readable && ((core::kCapitalMask >> code_b) & 1u);
                in.b_class_masked = in.b_readable && ((class_mask_ >> code_b) & 1u) != 0;
                in.small_dock_legs = gates_capital_on_; // amendment 17 (section 17 item 4): the small-ship row on B's dock legs too
                in.small_any_age = gates_capital_on_;   // B.4: ... and before any freshness requirement (the order-0 gap)
                if (in.small_any_age && s) in.a_id_ok = table_id_[slot] == rd32(a + sites::kObjId); // B.4 review: the entry is A's
                in.r_a = float(rds32(a + sites::kObjRadius)) / core::kUnitsPerMetre;
                in.r_b = float(rds32(b + sites::kObjRadius)) / core::kUnitsPerMetre;
                in.b_flags = rd32(b + sites::kObjFlags);
                in.b_docked = rd32(b + sites::kObjDocked);
                const std::uint32_t fl_b = rd32(b + sites::kObjFlight);
                in.b_order = fl_b ? rd16(fl_b + sites::kFlightOrder) : std::uint16_t(0);
                in.b_target = fl_b ? rd32(fl_b + sites::kFlightTarget) : 0u;
                const std::uint32_t id_a = rd32(a + sites::kObjId), id_b = rd32(b + sites::kObjId);
                if (in.b_readable && av::small_pair(true, in.r_a, true, in.b_capital_code, in.r_b)) {
                    in.b_is_a_leader = (av::member_order(in.a_order) && in.a_target == b) || members_.current(a, b, id_a, id_b, now) != nullptr;
                } else if (in.b_readable && in.a_order != core::kOrderAvoid && avoidance_on_) {
                    in.b_member_current = members_.current(b, a, id_b, id_a, now) != nullptr;
                    if (in.b_member_current) in.d = pair_distance_m(a, b);
                }
            }
        }
    }
    in.b_resolved = s && in.a_order != core::kOrderAvoid && av::suppressed(avoid_[slot], a, b, now, av::fresh_window_ms(av::kResolvedAgeMs, in.frame_ms));
    const EV v = av::site_e_verdict(in);
    *out = v;
    switch (v) {
    case EV::suppress_small:
        e_suppressed_small_.add();
        if (s && in.a_age_ms > av::fresh_window_ms(av::kSteeredFreshMs, in.frame_ms, av::kSteeredReplanMs)) e_small_gap_.add(); // B.4
        return 1;
    case EV::suppress_member: e_suppressed_member_.add(); return 1;
    case EV::suppress_gate: e_suppressed_gate_.add(); return 1;
    case EV::suppress_gate_small:
        e_suppressed_gate_small_.add();
        if (in.a_order == core::kOrderNone) e_gate_small_gap_.add(); // Run161 item 5
        return 1;
    case EV::suppress_resolved: e_suppressed_.add(); return 1;
    case EV::suppress_lane: e_suppressed_lane_.add(); return 1;
    case EV::suppress_dock: e_suppressed_dock_.add(); return 1;
    case EV::pass_dock_parts: e_pass_dock_[unsigned(av::DockE::no_parts)].add(); return 0;
    case EV::pass_dock_path: e_pass_dock_[unsigned(av::DockE::path)].add(); return 0;
    case EV::pass_dock_ray: e_pass_dock_[unsigned(av::DockE::ray)].add(); return 0;
    case EV::pass_dock_zone: e_pass_dock_[unsigned(av::DockE::zone)].add(); return 0;
    case EV::pass_no_autopilot: case EV::pass_not_steerable: case EV::pass_class: case EV::pass_b_dock_leg: case EV::pass_own_leader: {
        e_small_pass_.add();
        const unsigned k = v == EV::pass_no_autopilot ? unsigned(av::SmallPair::no_autopilot) : v == EV::pass_not_steerable ? unsigned(av::SmallPair::not_steerable)
                           : v == EV::pass_class ? unsigned(av::SmallPair::class_masked) : v == EV::pass_b_dock_leg ? unsigned(av::SmallPair::b_dock_leg)
                                                                                     : unsigned(av::SmallPair::own_leader);
        e_small_pass_reason_[k].add();
        return 0;
    }
    default: return 0;
    }
}
}
extern "C" int __cdecl x3m_ship_avoid_suppress(std::uint32_t a, std::uint32_t b) {
    if (!installed_.load(std::memory_order_relaxed) || !warn_live_.load(std::memory_order_relaxed)) return 0;
    Envelope env;
    e_calls_.add();
    const std::uint32_t now = game_time_ms();
    EV v;
    const int r = e_verdict(a, b, now, &v);
    if (trace_on_ && a && b) e_record(a, b, v, now); // e_record dereferences both
    return r;
}

namespace {
// The gate traffic controller's frame work (D.1, D.8): member decisions from their stored states (never in a handler), round robin over
// every board, within the frame bound (gc::frame_room: 6 decisions, 14 rollouts); a member decides at most every kDecideMs and only on a state newer than its
// last rollout. A pair row (--debug) when a pair's verdict differs from the last one logged, at most once per pair per second.
// Stage B's sequencer per gate (gc::sb_frame: stages, petals, the release, the funnel's speed and feasibility, the go-around), its
// counters and its ship_gate_ctl rows (the stage transitions).
// B.4 item 2 (ship_trace / --debug only): every board member (pair role) holds a trace slot while on the board, the reserved ones
// first (trace_claim; the idle sweep never frees it: last_seen is the board's frame), and one ship_gate_ctl_member row per game second.
// B.5 (Run165 crash: the row read [ship+0x4a] of a launched member whose object was freed and reused): the row reads no engine
// memory; the class is site B's (Member.code), shown for a member proven this frame (live=1); an unproven one gets live=0, class=? and
// no trace slot.
void ctl_member_frame(const gc::Gate& g, unsigned gi, std::uint32_t now) noexcept {
    for (unsigned i = 0; i < g.n && i < gc::kMembers; ++i) {
        const gc::Member& m = g.m[i];
        if (!m.ship || !gc::pair_role(m.role)) continue;
        TraceSlot* ts = nullptr;
        if (trace_on_) {
            ts = trace_slot_of(m.ship);
            if (!ts && m.proven) ts = trace_claim(m.ship, now, true, m.role == gc::Role::escort, true); // B.5: a slot only for a proven member
            if (ts) {
                ts->last_seen_ms = now;
                ts->member = 1; // held as a member from now on: released when it leaves the board
            }
        }
        if (!debug_ || (member_row_ship_[gi][i] == m.ship && now - member_row_ms_[gi][i] < 1000u)) continue;
        member_row_ship_[gi][i] = m.ship;
        member_row_ms_[gi][i] = now;
        const std::uint32_t w = member_row_write_.load(std::memory_order_relaxed);
        if (w - member_row_read_ >= 32) {
            row_dropped(Drop::ctl); // as the seq rows
            continue;
        }
        MemberRow& r = member_rows_[w & 31];
        r.gate = g.gate;
        r.ship = m.ship;
        r.t_ms = now;
        r.target = m.st.target;
        r.key = m.key;
        r.code = gc::member_code(m); // B.5: site B's code of a member proven this frame (no engine read here); 0xffffffff otherwise
        r.live = m.proven;
        r.st_age_ms = m.st.ms ? std::int32_t(now - m.st.ms) : -1;
        r.dec_age_ms = m.decided_ms ? std::int32_t(now - m.decided_ms) : -1;
        r.order = m.st.order;
        r.role = std::uint8_t(m.role);
        r.stage = std::uint8_t(m.sb);
        r.traced = ts ? 1 : 0;
        r.petal = m.petal;
        const float d[3] = {m.st.x[0] - g.G[0], m.st.x[1] - g.G[1], m.st.x[2] - g.G[2]};
        r.p[0] = core::dot3(d, g.R);
        r.p[1] = core::dot3(d, g.U);
        r.p[2] = core::dot3(d, g.F);
        r.nose_f = core::dot3(m.st.rows[2], g.F);
        r.v = m.st.v;
        member_row_write_.store(w + 1, std::memory_order_release);
    }
}
void sb_frame_all(std::uint32_t now) noexcept {
    const std::uint64_t t0 = timing_ ? qpc() : 0;
    for (gc::Gate& g : ctl_.g) {
        if (!g.gate) continue;
        gc::SbStats st;
        st.seq_on = debug_; // review N7: the sequencer's row state only in the debug tier
        gc::sb_frame(g, now, &st);
        if (st.releases) ctl_releases_.add(st.releases);
        if (st.go_arounds) ctl_go_around_.add(st.go_arounds);
        if (st.x_refused) ctl_x_refused_.add(st.x_refused);
        if (st.predictions) ctl_sb_predictions_.add(st.predictions);
        if (st.guarded) ctl_guarded_.add(st.guarded);
        if (st.seq_skipped) ctl_seq_skipped_.add(st.seq_skipped);
        if (st.go_arounds_near) ctl_go_around_near_.add(st.go_arounds_near);
        if (st.clock_resets) ctl_clock_resets_.add(st.clock_resets);
        ctl_x_wait_ms_ += st.x_wait_ms;
        if (st.ev_dropped) for (unsigned k = 0; k < st.ev_dropped; ++k) row_dropped(Drop::ctl);
        if (!debug_) continue;
        if (st.seq.holders && (!g.seq_row_ms || now - g.seq_row_ms >= 1000u)) { // B.2 item 5: one row per gate per game second
            g.seq_row_ms = now ? now : 1;
            const std::uint32_t w = seq_row_write_.load(std::memory_order_relaxed);
            if (w - seq_row_read_ >= 8) row_dropped(Drop::ctl);
            else {
                seq_rows_[w & 7] = SeqRow{g.gate, now, g.sb_skips, g.sb_releases, st.seq};
                seq_row_write_.store(w + 1, std::memory_order_release);
            }
        }
        for (unsigned k = 0; k < st.n_ev; ++k) {
            const gc::SbEvent& e = st.ev[k];
            const int i = gc::find(g, e.ship);
            if (i < 0) continue;
            const std::uint32_t w = ctl_row_write_.load(std::memory_order_relaxed);
            if (w - ctl_row_read_ >= 32) {
                row_dropped(Drop::ctl);
                break;
            }
            const gc::Member& m = g.m[i];
            ctl_rows_[w & 31] = CtlRow{g.gate, m.ship, m.id, m.seq, now, e.key, m.st.order, std::uint8_t(m.role), std::uint8_t(m.role), e.why,
                                       std::uint8_t(gc::rank(m)), e.from, e.to, std::int8_t(e.petal == 0xff ? -1 : int(e.petal)), 0};
            ctl_row_write_.store(w + 1, std::memory_order_release);
        }
    }
    if (timing_) {
        const std::uint64_t dt = qpc() - t0;
        const std::uint32_t ticks = dt > 0xffffffffull ? 0xffffffffu : std::uint32_t(dt);
        if (ticks > ctl_sb_max_ticks_) ctl_sb_max_ticks_ = ticks;
    }
    if (trace_on_) // B.4 review: a slot held as a board member is released the frame its holder leaves the board (not by the idle sweep)
        for (unsigned i = 0; i < kTraceMax; ++i)
            if (trace_slots_[i].obj && trace_slots_[i].member && !ctl_on_board(trace_slots_[i].obj)) trace_slots_[i] = TraceSlot{};
    if (trace_on_ || debug_) // B.4 item 2 (outside the sequencer's timing)
        for (unsigned gi = 0; gi < gc::kGates; ++gi)
            if (ctl_.g[gi].gate) ctl_member_frame(ctl_.g[gi], gi, now);
}
// B.5 step 1 (before the sequencer): the sector check (a frame whose walks do not include the controller's sector resets every board:
// the player changed sector) and each member's proof against the live set the update filled (the walks, site B). The controller frame
// reads no engine memory: it dereferences only pointers the engine passed to the module in this frame's update, in that update
// (ship_gate_ctl_live_core.h).
void ctl_live_prove(std::uint32_t now) noexcept {
    if (!ctl_any_member()) { // no board entry: only the sector is tracked
        gc::live_track(live_track_, live_set_);
        return;
    }
    const std::uint64_t t0 = timing_ ? qpc() : 0;
    gc::live_prove(ctl_, live_track_, live_set_, now, &live_out_); // a lookup per member: no engine read at the frame boundary
    if (live_out_.reset) {
        for (const gc::Gate& g : ctl_.g)
            if (g.gate)
                for (unsigned i = 0; i < g.n; ++i)
                    if (g.m[i].ship && g.m[i].role != gc::Role::none) ctl_row(g, g.m[i], g.m[i].role, gc::Role::none, gc::Why::sector, now);
        ctl_sector_dropped_.add(gc::reset_boards(ctl_));
        ctl_sector_resets_.add();
        gc::rebuild(ctl_ix_, ctl_);
    }
    if (live_out_.unproven) ctl_unproven_.add(live_out_.unproven);
    if (live_out_.overflow) ctl_live_overflow_.add(); // the set overflowed: nothing proven this frame (fail closed)
    if (timing_) {
        const std::uint64_t dt = qpc() - t0;
        const std::uint32_t ticks = dt > 0xffffffffull ? 0xffffffffu : std::uint32_t(dt);
        if (ticks > ctl_live_max_ticks_) ctl_live_max_ticks_ = ticks;
    }
}
// B.5 step 2 (after the sequencer, before the decisions): launched members whose leg ended or that a complete snapshot misses, and
// members without a proof for kStateMaxAgeMs, leave the board (their sequence and funnel occupancy end; funnel_end_ms stays).
void ctl_live_leaves(std::uint32_t now) noexcept {
    gc::live_leaves(ctl_, live_track_, live_set_, now, &live_out_);
    for (unsigned k = 0; k < live_out_.n_leave; ++k) {
        const gc::LiveLeave& l = live_out_.leave[k];
        gc::Gate& g = ctl_.g[l.gate];
        if (!gc::pair_role(g.m[l.member].role)) continue;
        if (l.launched) ctl_live_launch_.add();
        else ctl_live_gone_.add();
        ctl_leave(g, l.member, false, l.why, now);
    }
}
void ctl_frame() noexcept {
    const std::uint32_t now = game_time_ms();
    const std::uint32_t step = frame_step_ms();
    gc::note_step(ctl_, now, step); // B.3 item 1
    ctl_live_prove(now);            // B.5
    sb_frame_all(now);
    ctl_live_leaves(now);
    const std::uint64_t t0 = timing_ || ctl_ceiling_ticks_ ? qpc() : 0;
    unsigned done = 0, rollouts = 0;
    // review F2: the funnel's occupants first (their decision stamps keep the occupancy), then the round robin (gc::decide_order)
    std::uint16_t order[gc::kGates * gc::kMembers];
    unsigned n_prio = 0;
    const unsigned n_order = gc::decide_order(ctl_, ctl_cursor_, now, order, &n_prio);
    // B.3: the budget follows game time (gc::frame_budget: 4 decisions per member per game second), under kFrameCeilingUs (1 ms) of measured time
    const gc::FrameBudget fb = gc::frame_budget(gc::members_live(ctl_), step);
    for (unsigned q = 0; q < n_order && gc::frame_room(done, rollouts, fb); ++q) {
        if (done && ctl_ceiling_ticks_ && qpc() - t0 > ctl_ceiling_ticks_) {
            ctl_budget_cut_.add();
            break;
        }
        const unsigned k = order[q];
        gc::Gate& g = ctl_.g[k / gc::kMembers];
        const unsigned i = k % gc::kMembers;
        gc::Member& m = g.m[i];
        const std::uint64_t td = timing_ ? qpc() : 0;
        gc::DecideStats st;
        gc::decide(g, i, now, &st, nullptr, nullptr);
        if (timing_) {
            const std::uint64_t dt = qpc() - td;
            const std::uint32_t ticks = dt > 0xffffffffull ? 0xffffffffu : std::uint32_t(dt);
            if (ticks > ctl_decide_max_ticks_) ctl_decide_max_ticks_ = ticks;
        }
        ctl_decisions_.add();
        rollouts += st.rollouts;
        ctl_rollouts_.add(st.rollouts);
        ctl_sweeps_.add(st.sweeps);
        ctl_narrow_.add(st.narrow);
        if (st.conflicts) ctl_conflicts_.add(st.conflicts);
        if (st.yields) ctl_yields_.add(st.yields);
        if (st.unresolved) ctl_unresolved_.add(st.unresolved);
        if (st.released) ctl_released_.add(st.released);
        if (q >= n_prio) ctl_cursor_ = k + 1;
        ++done;
        if (!debug_) continue;
        for (unsigned j = 0; j < g.n; ++j) {
            if (j == i || g.v[i][j] == g.logged[i][j] || (g.logged_ms[i][j] && now - g.logged_ms[i][j] < 1000)) continue;
            // the edges worth a row: into or out of a hand-back, and a certificate lost (clear -> none); a first certificate (none ->
            // clear) is recorded silently (a member's first decision would otherwise write a row per partner)
            const std::uint8_t unres = std::uint8_t(gc::PairV::unresolved), clear = std::uint8_t(gc::PairV::clear);
            if (g.v[i][j] == clear && g.logged[i][j] != unres) {
                g.logged[i][j] = g.logged[j][i] = clear;
                continue;
            }
            const std::uint32_t w = ctl_pair_write_.load(std::memory_order_relaxed);
            if (w - ctl_pair_read_ >= 64) {
                row_dropped(Drop::ctl_pair);
                break;
            }
            const gc::Member& b = g.m[j];
            const float dx[3] = {m.st.x[0] - b.st.x[0], m.st.x[1] - b.st.x[1], m.st.x[2] - b.st.x[2]};
            const std::uint8_t y = m.cap >= 0.0f && m.cap_ms == now ? 1 : b.cap >= 0.0f && b.cap_ms == now ? 2 : 0;
            ctl_pair_rows_[w & 63] = CtlPairRow{g.gate, m.ship, b.ship, now, m.seq, b.seq, g.v[i][j], g.logged[i][j], g.tc[i][j], y,
                                                std::uint8_t(gc::rank(m)), std::uint8_t(gc::rank(b)), 0, 0,
                                                y == 1 ? m.cap : y == 2 ? b.cap : -1.0f, core::sqrt_ss(core::dot3(dx, dx))};
            ctl_pair_write_.store(w + 1, std::memory_order_release);
            g.logged[i][j] = g.logged[j][i] = g.v[i][j];
            g.logged_ms[i][j] = g.logged_ms[j][i] = now ? now : 1;
        }
    }
    if (timing_ && done) {
        const std::uint64_t dt = qpc() - t0;
        const std::uint32_t ticks = dt > 0xffffffffull ? 0xffffffffu : std::uint32_t(dt);
        if (ticks > ctl_frame_max_ticks_) ctl_frame_max_ticks_ = ticks;
    }
    if (done > ctl_frame_max_decisions_) ctl_frame_max_decisions_ = done;
    live_set_.clear(); // B.5: the next update fills the next frame's set
    if (now - arr_ix_rebuilt_ms_ >= 1000) { // the arrival bitmap keeps only the records of the last 60 s
        arr_ix_rebuilt_ms_ = now;
        for (std::uint32_t& w : arr_ix_.bits) w = 0;
        for (const gc::Arrival& a : arrivals_.a)
            if (a.ship && now - a.launch_ms <= gc::kArrivalWindowMs) arr_ix_.set(a.ship);
    }
}
inline double ticks_us(std::uint32_t ticks) noexcept { // via int32 (cvtsi2sd): a 64-bit integer -> double conversion is x87 fild on i686
    return qpc_frequency_ ? double(std::int32_t(ticks > 0x7fffffffu ? 0x7fffffffu : ticks)) * 1e6 /
                                double(std::int32_t(qpc_frequency_ > 0x7fffffffull ? 0x7fffffffu : std::uint32_t(qpc_frequency_)))
                          : 0.0;
}
// Amendment 18: the ship_avoid_dock rows (--debug), drained at the frame boundary.
void dock_rows(unsigned long long frame) noexcept {
    static const char* const kZone[4] = {"out", "near", "in", "inner"};
    for (std::uint32_t w = dock_row_write_.load(std::memory_order_acquire); dock_row_read_ != w; ++dock_row_read_) {
        const DockRow& r = dock_rows_[dock_row_read_ & 31];
        x3m::log("ship_avoid_dock frame=%llu t_ms=%lu ship=0x%08lx cont=0x%08lx class=%u order=0x%x parts=%u n_parts=%u path_m=%.0f ray_m=%.0f "
                 "margin_m=%.0f reach_m=%.0f e=%s zone=%s d_c_m=%.0f r_c_m=%.0f v_ms=%.1f kept=%u detour=%u",
                 frame, static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.ship), static_cast<unsigned long>(r.cont), unsigned(r.cls),
                 unsigned(r.order), unsigned(r.parts_ok), r.n_parts, double(r.path_m), double(r.ray_m), double(r.margin_m), double(r.reach_m),
                 av::dock_e_name(av::DockE(r.verdict)), kZone[r.zone & 3], double(r.d_c_m), double(r.r_c_m), double(r.v_ms), unsigned(r.kept),
                 unsigned(r.detour));
    }
}
// The --debug rows of the controller (D.7), drained at the frame boundary.
void ctl_rows(unsigned long long frame) noexcept {
    static const char* const kLaunchKind[4] = {"vanilla", "capital_offset", "small_side", "small_unseparated"};
    static const char* const kYielder[3] = {"none", "a", "b"};
    for (std::uint32_t w = ctl_row_write_.load(std::memory_order_acquire); ctl_row_read_ != w; ++ctl_row_read_) {
        const CtlRow& r = ctl_rows_[ctl_row_read_ & 31];
        x3m::log("ship_gate_ctl frame=%llu t_ms=%lu gate=0x%08lx ship=0x%08lx id=%lu seq=%lu key=%lu order=0x%x from=%s to=%s why=%s rank=%u "
            "stage=%s>%s petal=%d", frame,
            static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.gate), static_cast<unsigned long>(r.ship),
            static_cast<unsigned long>(r.id), static_cast<unsigned long>(r.seq), static_cast<unsigned long>(r.key), unsigned(r.order),
            gc::role_name(gc::Role(r.from)), gc::role_name(gc::Role(r.to)), gc::why_name(gc::Why(r.why)), unsigned(r.rank),
            gc::sb_name(gc::Sb(r.sb_from)), gc::sb_name(gc::Sb(r.sb_to)), int(r.petal));
    }
    for (std::uint32_t w = ctl_pair_write_.load(std::memory_order_acquire); ctl_pair_read_ != w; ++ctl_pair_read_) {
        const CtlPairRow& r = ctl_pair_rows_[ctl_pair_read_ & 63];
        static const char* const kPairKind[3] = {"verdict", "e_pass", "order0"};
        x3m::log("ship_gate_ctl_pair frame=%llu t_ms=%lu gate=0x%08lx a=0x%08lx b=0x%08lx seq=%lu,%lu rank=%u,%u kind=%s verdict=%s before=%s "
            "why=%s t_c_s=%.1f yielder=%s cap_ms=%.1f d_m=%.0f",
            frame, static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.gate), static_cast<unsigned long>(r.a),
            static_cast<unsigned long>(r.b), static_cast<unsigned long>(r.seq_a), static_cast<unsigned long>(r.seq_b), unsigned(r.rank_a),
            unsigned(r.rank_b), kPairKind[r.kind < 3 ? r.kind : 0], gc::pair_name(gc::PairV(r.verdict)), gc::pair_name(gc::PairV(r.before)),
            r.kind ? gc::ewhy_name(gc::EWhy(r.why)) : "-", r.t_c == 255 ? -1.0 : double(r.t_c) * double(gc::kSampleS),
            kYielder[r.yielder < 3 ? r.yielder : 0], double(r.cap), double(r.d));
    }
    for (std::uint32_t w = seq_row_write_.load(std::memory_order_acquire); seq_row_read_ != w; ++seq_row_read_) {
        const SeqRow& r = seq_rows_[seq_row_read_ & 7];
        static const char* const kTri[3] = {"0", "1", "-"};
        x3m::log("ship_gate_ctl_seq frame=%llu t_ms=%lu gate=0x%08lx holders=%u inbound=%u cand=0x%08lx key=%lu role=%s stage=%s ringed=%u "
            "busy_by=0x%08lx window=%s timing=%s free_ms=%ld dx_m=%.0f v_feas=%.0f block_ms=%ld skips=%lu releases=%lu",
            frame, static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.gate), unsigned(r.s.holders), unsigned(r.s.inbound),
            static_cast<unsigned long>(r.s.cand), static_cast<unsigned long>(r.s.key), gc::role_name(gc::Role(r.s.role)),
            gc::sb_name(gc::Sb(r.s.stage)), unsigned(r.s.ringed), static_cast<unsigned long>(r.s.busy_by), kTri[r.s.window < 3 ? r.s.window : 2],
            kTri[r.s.timing < 3 ? r.s.timing : 2], static_cast<long>(r.s.free_ms), double(r.s.dx), double(r.s.v_feas),
            static_cast<long>(r.s.block_ms), static_cast<unsigned long>(r.skips), static_cast<unsigned long>(r.releases));
    }
    for (std::uint32_t w = member_row_write_.load(std::memory_order_acquire); member_row_read_ != w; ++member_row_read_) { // B.4 item 2
        const MemberRow& r = member_rows_[member_row_read_ & 31];
        x3m::log("ship_gate_ctl_member frame=%llu t_ms=%lu gate=0x%08lx ship=0x%08lx class=%s role=%s stage=%s order=0x%x target=0x%08lx "
            "p_m=%.0f,%.0f,%.0f nose_f=%.3f v_ms=%.1f petal=%d key=%lu traced=%u st_age_ms=%ld dec_age_ms=%ld live=%u",
            frame, static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.gate), static_cast<unsigned long>(r.ship),
            r.code < core::kClassCount ? core::kClassNames[r.code] : "?", gc::role_name(gc::Role(r.role)), gc::sb_name(gc::Sb(r.stage)),
            unsigned(r.order), static_cast<unsigned long>(r.target), double(r.p[0]), double(r.p[1]), double(r.p[2]), double(r.nose_f),
            double(r.v), int(r.petal), static_cast<unsigned long>(r.key), unsigned(r.traced), static_cast<long>(r.st_age_ms),
            static_cast<long>(r.dec_age_ms), unsigned(r.live));
    }
    for (std::uint32_t w = launch_row_write_.load(std::memory_order_acquire); launch_row_read_ != w; ++launch_row_read_) {
        const LaunchRow& r = launch_rows_[launch_row_read_ & 7];
        x3m::log("ship_gate_launch frame=%llu t_ms=%lu ship=0x%08lx gate=0x%08lx class=%s capital=%u slot=%ld gate_type=%u kind=%s p_m=%.0f,%.0f,%.0f "
            "off_r_m=%.0f off_u_m=%.0f r_gate_m=%.0f r_m=%.0f w_m=%.0f",
            frame, static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.ship), static_cast<unsigned long>(r.gate),
            r.code < core::kClassCount ? core::kClassNames[r.code] : "?", unsigned(r.capital), static_cast<long>(r.slot), unsigned(r.type),
            kLaunchKind[r.kind < 4 ? r.kind : 0], double(r.P[0]), double(r.P[1]), double(r.P[2]), double(r.off[0]), double(r.off[1]),
            double(r.r_gate), double(r.r), double(r.w));
    }
}
}
namespace x3m::ship_steering {
bool initialize() {
    const DWORD error = GetLastError();
    if (initialized_) {
        SetLastError(error);
        return installed_.load(std::memory_order_acquire);
    }
    initialized_ = true;
    wchar_t text[8]{};
    const DWORD length = x3m::config::get(L"X3M_SHIP_STEERING", text, 8);
    bool on = false;
    const char* reason = nullptr;
    char detail[64] = {};
    if (length >= 8 || !core::parse_switch(text, length, &on))
        reason = "invalid_setting";
    else if (!on) {
        state_ = "off";
        SetLastError(error);
        return false; // unset or 0: nothing claimed, no row
    }
    wchar_t classes[sites::kSettingCapacity]{};
    const DWORD class_length = x3m::config::get(L"X3M_SHIP_STEERING_CLASSES", classes, sites::kSettingCapacity);
    if (!reason) {
        if (class_length >= sites::kSettingCapacity || !core::parse_class_mask(classes, class_length, &class_mask_))
            reason = "invalid_classes";
        else if (!object_trace::executable_verified())
            reason = "executable_mismatch";
        else if (install_all(&reason, detail))
            reason = nullptr;
    }
    if (reason) {
        state_ = reason;
        log("ship_steering_refused reason=%s%s%s sites=%u", reason, detail[0] ? "_" : "", detail, unsigned(sites::Count));
        SetLastError(error);
        return false;
    }
    // Step 2: X3M_SHIP_AVOIDANCE=1 (requires the steering above). Site E is claimed separately; a refusal there
    // leaves avoidance running without the suppression (the reactive loop stays as today).
    wchar_t avoid_text[8]{};
    const DWORD avoid_length = x3m::config::get(L"X3M_SHIP_AVOIDANCE", avoid_text, 8);
    bool avoid_on = false;
    if (avoid_length >= 8 || !core::parse_switch(avoid_text, avoid_length, &avoid_on)) {
        avoid_on = false;
        log("ship_avoidance_refused reason=invalid_setting");
    }
    avoidance_on_ = avoid_on;
    if (avoidance_on_) {
        warn_site_ = engine_patch::Site{};
        if (!engine_patch::verify_bytes(sites::kWarnSite.address, sites::kWarnSite.expected, sites::kWarnSite.length))
            warn_state_ = "bytes_mismatch";
        else if (engine_patch::claim(warn_site_, sites::kWarnSite)) {
            x3m_ship_avoid_next_e = *warn_site_.entry;
            std::atomic_thread_fence(std::memory_order_release);
            if (engine_patch::push_front(warn_site_, reinterpret_cast<void*>(&x3m_ship_avoid_e_stub))) {
                warn_state_ = "active";
                warn_live_.store(true, std::memory_order_release);
            } else {
                warn_state_ = engine_patch::restore(warn_site_) ? "stub_chain_failed" : "stub_chain_failed_live_pass_through";
            }
        } else
            warn_state_ = warn_site_.status;
    }
    // Step 3: X3M_SHIP_STEERING_GATES (default on with the steering). The three gate sites are claimed as a group:
    // every span preflighted, claims in table order, every claimed gate site rolled back on the first failure; the
    // gate policy (P1) runs only with all three live. Steering and avoidance are unaffected by a refusal here.
    wchar_t gates_text[8]{};
    const DWORD gates_length = x3m::config::get(L"X3M_SHIP_STEERING_GATES", gates_text, 8);
    bool gates_on = true;
    if (gates_length == 0)
        gates_on = true;
    else if (gates_length >= 8 || !core::parse_switch(gates_text, gates_length, &gates_on)) {
        gates_on = false;
        gates_state_ = "invalid_setting";
        log("ship_steering_gates_refused reason=invalid_setting");
    }
    gates_on_ = gates_on;
    if (gates_on_) {
        const char* why = nullptr;
        char gate_detail[64] = {};
        for (unsigned i = 0; i < sites::GateCount && !why; ++i)
            if (!engine_patch::verify_bytes(sites::kGateSites[i].address, sites::kGateSites[i].expected, sites::kGateSites[i].length)) {
                why = "bytes_mismatch";
                std::snprintf(gate_detail, sizeof gate_detail, "%s", sites::kGateSites[i].name);
            }
        for (unsigned i = 0; i < sites::GateCount && !why; ++i) {
            gate_patches_[i] = engine_patch::Site{};
            void** slot = gate_slot_of(i);
            void* stub = gate_stub_of(i);
            if (engine_patch::claim(gate_patches_[i], sites::kGateSites[i])) {
                *slot = *gate_patches_[i].entry;
                std::atomic_thread_fence(std::memory_order_release);
                if (engine_patch::push_front(gate_patches_[i], stub)) continue;
                why = "stub_chain_failed";
            } else
                why = gate_patches_[i].status;
            std::snprintf(gate_detail, sizeof gate_detail, "%s", sites::kGateSites[i].name);
            bool restored = true;
            for (unsigned j = sites::GateCount; j-- > 0;)
                if (gate_patches_[j].patched_in && !engine_patch::restore(gate_patches_[j])) restored = false;
            if (!restored) why = "rollback_failed_live_sites_pass_through"; // the stubs pass through: the live byte stays 0
        }
        if (why) {
            gates_state_ = why;
            log("ship_steering_gates_refused reason=%s%s%s sites=%u", why, gate_detail[0] ? "_" : "", gate_detail, unsigned(sites::GateCount));
        } else {
            gates_state_ = "active";
            gates_live_.store(true, std::memory_order_release);
            x3m_ship_gates_live = 1;
        }
    } else if (gates_state_[0] != 'i')
        gates_state_ = "off";
    // Amendment 13: X3M_SHIP_STEERING_GATES_CAPITAL (default on with the gates; needs the gate group live: P2 is where the
    // speed is kept). Set without the gates, or with the gates refused, it is refused with a row; off = the amendment-10 path.
    {
        wchar_t cap_text[8]{};
        const DWORD cap_length = x3m::config::get(L"X3M_SHIP_STEERING_GATES_CAPITAL", cap_text, 8);
        bool cap_on = true;
        const char* cap_why = nullptr;
        if (cap_length == 0)
            cap_on = true;
        else if (cap_length >= 8 || !core::parse_switch(cap_text, cap_length, &cap_on)) {
            cap_on = false;
            cap_why = "invalid_setting";
        }
        if (cap_on && !gates_on_) {
            cap_on = false;
            if (cap_length) cap_why = "needs_gates"; // set explicitly without the gates: the refusal row; unset: silently off
        } else if (cap_on && !gates_live_.load(std::memory_order_relaxed)) {
            cap_on = false;
            cap_why = "gates_refused";
        }
        gates_capital_on_ = cap_on;
        if (cap_why) {
            gates_capital_state_ = cap_why;
            log("ship_steering_gates_capital_refused reason=%s", cap_why);
        } else
            gates_capital_state_ = cap_on ? "active" : "off";
        // amendment 16b: the sequencing key (default on; only under the capital key)
        wchar_t seq_text[8]{};
        const DWORD seq_length = x3m::config::get(L"X3M_SHIP_STEERING_GATES_CAPITAL_SEQ", seq_text, 8);
        bool seq_on = true;
        if (seq_length && (seq_length >= 8 || !core::parse_switch(seq_text, seq_length, &seq_on))) {
            seq_on = false;
            log("ship_steering_gates_capital_seq_refused reason=invalid_setting");
        }
        seq_on_ = seq_on && gates_capital_on_;
    }
    // The gate traffic controller, stage A (gate-traffic-controller.md D.8): X3M_SHIP_STEERING_GATES_TRAFFIC (default on; needs the
    // capital key active). The launch window 0x00444606 is claimed alone, all or nothing: byte-verified, claimed and chained, or rolled
    // back and the controller stays off (amendment 17 exactly); refused outside the install window. Set without the capital key: the
    // refusal row; unset: silently off.
    {
        wchar_t tr_text[8]{};
        const DWORD tr_length = x3m::config::get(L"X3M_SHIP_STEERING_GATES_TRAFFIC", tr_text, 8);
        bool tr_on = true;
        const char* tr_why = nullptr;
        if (tr_length && (tr_length >= 8 || !core::parse_switch(tr_text, tr_length, &tr_on))) {
            tr_on = false;
            tr_why = "invalid_setting";
        }
        if (tr_on && !gates_capital_on_) {
            tr_on = false;
            if (tr_length) tr_why = "needs_gates_capital";
        }
        if (tr_on && !avoidance_on_) { // B.5: the live set is filled by the avoidance walks; without them no member is ever proven
            tr_on = false;
            tr_why = "needs_avoidance";
        }
        if (tr_on && !engine_patch::install_window_open()) {
            tr_on = false;
            tr_why = "late_claim";
        }
        if (tr_on && !engine_patch::verify_bytes(sites::kArrivalSite.address, sites::kArrivalSite.expected, sites::kArrivalSite.length)) {
            tr_on = false;
            tr_why = "bytes_mismatch";
        }
        if (tr_on) {
            traffic_patch_ = engine_patch::Site{};
            if (engine_patch::claim(traffic_patch_, sites::kArrivalSite)) {
                x3m_ship_gate_next_t = *traffic_patch_.entry; // the tail: the engine's own bytes until the stub is chained
                std::atomic_thread_fence(std::memory_order_release);
                if (!engine_patch::push_front(traffic_patch_, reinterpret_cast<void*>(&x3m_ship_gate_arrival_stub))) {
                    tr_on = false;
                    tr_why = "stub_chain_failed";
                }
            } else {
                tr_on = false;
                tr_why = traffic_patch_.status;
            }
            // the stub passes through while the live byte is 0: a site whose restore failed stays harmless; shutdown() retries
            if (!tr_on && traffic_patch_.patched_in && !engine_patch::restore(traffic_patch_)) tr_why = "rollback_failed_live_site_passes_through";
        }
        traffic_on_ = tr_on;
        if (tr_on) {
            traffic_state_ = "active";
            traffic_live_.store(true, std::memory_order_release);
            x3m_ship_traffic_live = 1;
        } else if (tr_why) {
            traffic_state_ = tr_why;
            log("ship_steering_gates_traffic_refused reason=%s", tr_why);
        } else
            traffic_state_ = "off";
    }
    // Amendment 18 (docking-approach.md item 6): X3M_SHIP_STEERING_DOCK (default on; needs X3M_SHIP_AVOIDANCE). No site of its own: the
    // row runs in site E's handler (inert while E is not chained), the part obstacles in the avoidance. Set without the avoidance: the
    // refusal row; unset: silently off.
    {
        wchar_t dk_text[8]{};
        const DWORD dk_length = x3m::config::get(L"X3M_SHIP_STEERING_DOCK", dk_text, 8);
        bool dk_on = true;
        const char* dk_why = nullptr;
        if (dk_length && (dk_length >= 8 || !core::parse_switch(dk_text, dk_length, &dk_on))) {
            dk_on = false;
            dk_why = "invalid_setting";
        }
        if (dk_on && !avoidance_on_) {
            dk_on = false;
            if (dk_length) dk_why = "needs_avoidance";
        }
        dock_on_ = dk_on;
        if (dk_why) {
            dock_state_ = dk_why;
            log("ship_steering_dock_refused reason=%s", dk_why);
        } else
            dock_state_ = dk_on ? (warn_live_.load(std::memory_order_relaxed) ? "active" : "active_no_e") : "off";
        if (dk_on)
            log("ship_steering_dock installed=1 status=%s margin=%.1f*r+%.0f brake_cap_m=%.0f carrier_inner_r=%.1f idle_near_r=%.1f row_ring=32",
                dock_state_, double(av::kDockMarginFactor), double(av::kDockMarginM), double(av::kDockBrakeCapM), double(av::kDockCarrierInner),
                double(av::kDockIdleNear));
    }
    LARGE_INTEGER f{};
    qpc_frequency_ = QueryPerformanceFrequency(&f) && f.QuadPart > 0 ? std::uint64_t(f.QuadPart) : 0;
    ctl_ceiling_ticks_ = qpc_frequency_ * gc::kFrameCeilingUs / 1000000u; // B.3: the decisions' time ceiling (0 without QPC)
    perf_ = log_tier::cached_perf;
    debug_ = log_tier::cached_debug;
    // Step 4 item 7: X3M_SHIP_TRACE (default off; --debug tier) and X3M_SHIP_TRACE_MAX (1..16, default 8 since amendment 17)
    {
        wchar_t trace_text[8]{}, max_text[8]{};
        const DWORD trace_length = x3m::config::get(L"X3M_SHIP_TRACE", trace_text, 8);
        bool trace_on = false;
        if (trace_length && (trace_length >= 8 || !core::parse_switch(trace_text, trace_length, &trace_on))) {
            trace_on = false;
            log("ship_trace_refused reason=invalid_setting");
        }
        trace_on_ = trace_on && debug_;
        if (trace_on && !debug_) log("ship_trace_refused reason=needs_debug");
        const DWORD max_length = x3m::config::get(L"X3M_SHIP_TRACE_MAX", max_text, 8);
        if (max_length) {
            wchar_t* end = nullptr;
            const unsigned long m = max_length < 8 ? wcstoul(max_text, &end, 10) : 0;
            if (max_length >= 8 || !end || *end != 0 || m < 1 || m > kTraceMax) {
                trace_max_ = 8u;
                log("ship_trace_refused reason=invalid_max");
            } else
                trace_max_ = unsigned(m);
        }
        if (trace_on_)
            log("ship_trace installed=1 max_ships=%u ring=%u idle_ms=%lu bytes_per_row_est=390", trace_max_, kTraceRing,
                static_cast<unsigned long>(kTraceIdleMs));
    }
    timing_ = (perf_ || debug_) && qpc_frequency_ != 0;
    state_ = "installed";
    installed_.store(true, std::memory_order_release);
    log("ship_steering installed=1 sites=%u class_mask=0x%04lx timing=%u tau_s=%.2f tick_s=%.2f floor_ms=%.0f bank_deg=35 "
        "handler_a=0x%08lx handler_b=0x%08lx",
        unsigned(sites::Count), static_cast<unsigned long>(class_mask_), timing_ ? 1u : 0u, double(core::kTau),
        double(core::kTickS), double(core::kFloorMs),
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_ship_steering_enter)),
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_ship_steering_clamp)));
    for (unsigned i = 0; i < sites::Count; ++i)
        log("ship_steering_site index=%u site=0x%08lx name=%s status=%s write=%s stub=0x%08lx tail=0x%08lx", i,
            static_cast<unsigned long>(sites::kSites[i].address), sites::kSites[i].name, patches_[i].status,
            write_name(patches_[i]), static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(stub_of(i))),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(*slot_of(i))));
    if (avoidance_on_)
        log("ship_avoidance installed=1 warn_site=0x%08lx warn_status=%s write=%s snapshot_capacity=%u snapshot_slots=%u "
            "station_capacity=%u parts_per_station=%u candidates=%u resolved_age_ms=%lu handler_e=0x%08lx",
            static_cast<unsigned long>(sites::kWarnSite.address), warn_state_, write_name(warn_site_), av::kSnapshotCapacity,
            sites::kSnapshotSlots, av::kStationCapacity, av::kMaxParts, av::kMaxCandidates,
            static_cast<unsigned long>(av::kResolvedAgeMs),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_ship_avoid_suppress)));
    if (gates_on_) {
        log("ship_steering_gates installed=%u status=%s sites=%u lead_in_deg=14.17 cross_fraction=%.2f look_s=%.2f look_min_r=%.2f join_r=%.1f "
            "large_cap_ms=%.0f spread_r=%.2f exit_spread_r=%.2f ring=%u capital_tol_deg=%.2f handler_p2=0x%08lx handler_p4=0x%08lx "
            "handler_p5=0x%08lx handler_w=0x%08lx",
            gates_live_.load(std::memory_order_relaxed) ? 1u : 0u, gates_state_, unsigned(sites::GateCount), double(core::kGateCrossFraction),
            double(core::kGateLookS), double(core::kGateLookMinR), double(core::kGateJoinR), double(core::kGateLargeCapMs),
            double(core::kGateSpreadR), double(core::kGateExitSpreadR), core::kGateSpreadRing, double(core::kGateRotateTolDeg),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_ship_gate_stop_a)),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_ship_gate_stop_b)),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_ship_gate_launch)),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_ship_gate_warp_in)));
        for (unsigned i = 0; i < sites::GateCount; ++i) {
            const std::uintptr_t resume = i == sites::GateStopA ? sites::kGateStopAResume : i == sites::GateStopB ? sites::kGateStopBResume
                                          : i == sites::GateLaunch ? sites::kGateLaunchResume : 0; // the warp-in stub continues into its tail
            log("ship_steering_gate_site index=%u site=0x%08lx name=%s status=%s write=%s stub=0x%08lx tail=0x%08lx resume=0x%08lx", i,
                static_cast<unsigned long>(sites::kGateSites[i].address), sites::kGateSites[i].name, gate_patches_[i].status,
                write_name(gate_patches_[i]), static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(gate_stub_of(i))),
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(*gate_slot_of(i))), static_cast<unsigned long>(resume));
        }
        log("ship_steering_gates_capital installed=%u status=%s join_r=%.1f inner_r=%.1f look_turns=%.1f tol_deg=%.2f small_ratio=%.3f "
            "fresh_ms=%lu fresh_frames=4 join_candidates_ms=100,60,40,25 join_eval_ms=%lu join_step_s=%.2f join_margin=%.0f/%.0f/%.0f/%.0f "
            "fallback_entry_ms=%.0f keep_vel_deg=%.0f member_capacity=%u member_expiry_ms=%lu lane_gates=%u lane_entries=%u lane_in_m=%.0f "
            "lane_gap_m=%.0f corridor_m=%.0f slow_join_nose_deg=%.0f slow_join_over_m=%.0f slow_join_offset_m=%.0f guard_r=%.2f/%.2f width=lod_all "
            "width_retry_ms=%lu width_retries=%u seq=%u seq_lat_gap_m=%.0f seq_along_m=%.0f lane_hold_m=%.0f lane_hold_deg=12 lane_apart=drop "
            "small_dock_legs=%u small_any_age=%u",
            gates_capital_on_ ? 1u : 0u, gates_capital_state_, double(core::kGateCapitalJoinR), double(core::kGateCapitalJoinInnerR),
            double(core::kGateCapitalLookTurns), double(core::kGateRotateTolDeg), double(av::kSmallShipRatio),
            static_cast<unsigned long>(av::kSteeredFreshMs), static_cast<unsigned long>(core::kJoinEvalMs), double(core::kJoinStepS),
            double(core::kJoinNoseDeg), double(core::kJoinVelDeg), double(core::kJoinLatM), double(core::kJoinOverM), double(core::kJoinEntryMs),
            double(core::kJoinKeepVelDeg), av::kMemberCapacity, static_cast<unsigned long>(av::kMemberExpiryMs), ln::kLaneGates, ln::kLaneEntries,
            double(ln::kInLaneM), double(ln::kExemptGapM), double(ln::kCorridorM), double(core::kSlowJoinNoseDeg), double(core::kSlowJoinOverM),
            double(core::kSlowJoinOffsetM), double(ln::kCapitalGuardR), double(ln::kSmallGuardR), static_cast<unsigned long>(ln::kWidthRetryMs),
            ln::kWidthRetries, seq_on_ ? 1u : 0u, double(ln::kSeqLatGapM), double(ln::kSeqAlongM), double(ln::kHoldM), gates_capital_on_ ? 1u : 0u, gates_capital_on_ ? 1u : 0u);
        log("ship_steering_gates_traffic installed=%u status=%s site=0x%08lx name=%s write=%s stub=0x%08lx tail=0x%08lx resume=0x%08lx "
            "boards=%u members=%u samples=%u step_s=%.2f sample_s=%.2f margin_m=%.0f drift_ms=%.1f cert_ms=%lu exclude_ms=%lu decide_ms=%lu "
            "state_ms=%lu decisions_per_frame=%u rollouts_per_frame=%u yield=%.1fv,%.0f,%.0f arrival_window_ms=%lu evict_keep_ms=%lu arrival_offset_r=%.2f "
            "terran_h_r=%.3f side_spawn_r=%.2f,%.2f side_spawn_max_w_r=%.2f snapshot_capacity=%u e_rows=%u e_pairs=%u warn_slots=%u "
            "handler=0x%08lx",
            traffic_live_.load(std::memory_order_relaxed) ? 1u : 0u, traffic_state_, static_cast<unsigned long>(sites::kArrivalSite.address),
            sites::kArrivalSite.name, write_name(traffic_patch_),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_ship_gate_arrival_stub)),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(x3m_ship_gate_next_t)), static_cast<unsigned long>(sites::kArrivalResume),
            gc::kGates, gc::kMembers, gc::kSamples, double(gc::kStepS), double(gc::kSampleS), double(gc::kMarginM), double(gc::kDriftMs),
            static_cast<unsigned long>(gc::kCertMaxAgeMs), static_cast<unsigned long>(gc::kExcludeMaxAgeMs),
            static_cast<unsigned long>(gc::kDecideMs), static_cast<unsigned long>(gc::kStateMs), gc::kDecisionsPerFrame, gc::kRolloutsPerFrame, double(gc::kYieldFrac),
            double(gc::kYieldMidMs), double(gc::kYieldFloorMs), static_cast<unsigned long>(gc::kArrivalWindowMs),
            static_cast<unsigned long>(gc::kEvictKeepMs), double(gc::kArrivalOffsetR), double(gc::kTerranHoleR), double(gc::kSideSpawnR),
            double(gc::kSideSpawnUpR), double(gc::kSideSpawnMaxWR), av::kSnapshotCapacity, kERows, kELogged, kCapWarnSlots,
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_ship_gate_arrival)));
    }
    SetLastError(error);
    return true;
}
bool installed() {
    return installed_.load(std::memory_order_acquire);
}
const char* state() {
    return state_;
}
void frame_boundary(std::uint64_t frame) {
    if (!installed_.load(std::memory_order_relaxed)) return;
    // Always tier: the stuck witnesses (first 8 per session).
    const std::uint32_t stuck = stuck_count_.load(std::memory_order_acquire);
    while (stuck_reported_ < stuck && stuck_reported_ < 8) {
        const Stuck& s = stuck_[stuck_reported_++];
        log("ship_steering_stuck frame=%llu obj=0x%08lx order=0x%lx d_m=%.1f best_d_m=%.1f no_progress_s=%.0f",
            static_cast<unsigned long long>(frame), static_cast<unsigned long>(s.obj), static_cast<unsigned long>(s.order),
            double(s.d), double(s.best_d), double(core::kStuckMs / 1000.0f));
    }
    // Amendment 15 (section 15.3): the join predictor, at most one capital per frame (round robin over the registry), each at 1 Hz
    // of game time; never in the steering handler. A slot whose ship left join mode (or whose state was reused) leaves the registry.
    if (gates_capital_on_ && join_reg_.count) {
        const std::uint32_t t_now = game_time_ms();
        for (unsigned i = 0; i < join_reg_.count;) { // the sweep: a completed, dead, redirected or reused slot leaves (review blocker)
            GateState& g = gate_[join_reg_.slots[i]];
            const bool same_ship = g.obj && g.join_registered && table_.entries[join_reg_.slots[i]].obj == g.obj;
            if (!core::join_registry_keep16(g.join, same_ship, t_now)) { // amendment 16: slow_join and the pred_none fallback stay
                g.join_registered = 0;
                join_reg_.remove_at(i);
                continue;
            }
            ++i;
        }
        for (unsigned n = 0; n < join_reg_.count; ++n) {
            const unsigned idx = (join_reg_.next + n) % join_reg_.count;
            GateState& g = gate_[join_reg_.slots[idx]];
            const std::uint64_t t0 = timing_ ? qpc() : 0;
            core::JoinEval ev;
            if (!core::join_evaluate16(g.join, t_now, av::fresh_window_ms(av::kSteeredFreshMs, frame_step_ms()), &ev)) continue;
            const bool fell = ev.fell_back, slowed = ev.slowed;
            join_reg_.next = idx + 1;
            ++join_evals_;
            join_predictions_ += ev.predictions;
            if (timing_) {
                const std::uint64_t dt = qpc() - t0;
                const std::uint32_t ticks = dt > 0xffffffffull ? 0xffffffffu : std::uint32_t(dt);
                if (ticks > join_eval_max_ticks_) join_eval_max_ticks_ = ticks;
            }
            if (slowed) gate_capital_slowed_.add();
            if (fell) gate_capital_fallback_pred_.add(); // amendment 16: stays registered (1 Hz re-evaluation, the un-latch)
            if (ev.slow_entered) gate_capital_slow_join_.add();
            if (ev.unlatched) gate_capital_unlatch_.add();
            if (debug_ && (fell || ev.slow_entered || ev.unlatched)) latch_row(g, join_reg_.slots[idx], ev);
            if ((fell || slowed || ev.slow_entered || ev.unlatched) && trace_on_ && trace_slot_of(g.obj)) {
                join_row(g, g.obj, t_now, false, -1.0f, -1.0f, -1.0f, 0, 0);
                g.join_row_ms = t_now;
                g.join_logged_mode = std::uint8_t(g.join.mode);
            }
            break; // one capital per frame
        }
    }
    if (traffic_live_.load(std::memory_order_relaxed)) ctl_frame(); // the gate traffic controller's decisions (D.1), never in a handler
    if (!perf_ && !debug_) return;
    if (debug_ && traffic_on_) ctl_rows(frame);
    if (debug_ && dock_on_) dock_rows(frame);
    const unsigned long long now = GetTickCount64();
    if (debug_) {
        // One sampled decision per second: request it, log the previous one when complete.
        const std::uint32_t seq = sample_seq_.load(std::memory_order_acquire);
        if (seq && !(seq & 1u) && seq != last_logged_seq_ && now - last_sample_tick_ >= 1000) {
            Sample S = sample_;
            if (sample_seq_.load(std::memory_order_acquire) == seq) {
                last_sample_tick_ = now;
                last_logged_seq_ = seq;
                log("ship_steering_decision frame=%llu obj=0x%08lx order=0x%lx class=%s leaf=%lu held=%u theta_deg=%.2f n_yaw=%.3f "
                    "n_pitch=%.3f omega_dps=%.2f e=%ld,%ld,%ld w=%ld,%ld,%ld W_dps=%.1f,%.1f,%.1f k=%.3f d_m=%.1f d_arr_m=%.1f "
                    "v_ms=%.1f v_cmd_ms=%.1f v_corner_ms=%.1f v_ref_ms=%.1f P_m=%.1f,%.1f,%.1f x_m=%.1f,%.1f,%.1f flags=0x%08lx "
                    "a_dps2=%.2f,%.2f,%.2f f2b0=0x%08lx rot_pct=%ld omega_cmd_dps=%.2f,%.2f,%.2f lag_deg=%.1f aim_off_deg=%.1f v_slip_ms=%.1f "
                    "slip_ms=%.1f,%.1f,%.1f",
                    static_cast<unsigned long long>(frame), static_cast<unsigned long>(S.obj), static_cast<unsigned long>(S.order),
                    S.code < core::kClassCount ? core::kClassNames[S.code] : "?", static_cast<unsigned long>(S.leaf), S.held ? 1u : 0u,
                    double(S.theta_deg), double(S.n_y), double(S.n_p), double(S.omega_dps), long(S.e[0]), long(S.e[1]), long(S.e[2]),
                    long(S.w[0]), long(S.w[1]), long(S.w[2]), double(S.W_dps[0]), double(S.W_dps[1]), double(S.W_dps[2]), double(S.k),
                    double(S.d), double(S.d_arr), double(S.v), double(S.v_cmd), double(S.v_corner), double(S.v_ref), double(S.P[0]),
                    double(S.P[1]), double(S.P[2]), double(S.x[0]), double(S.x[1]), double(S.x[2]), static_cast<unsigned long>(S.flags),
                    double(S.a_dps[0]), double(S.a_dps[1]), double(S.a_dps[2]), static_cast<unsigned long>(S.rot_accel_raw), long(S.rot_pct),
                    double(S.omega_axis_dps[0]), double(S.omega_axis_dps[1]), double(S.omega_axis_dps[2]), double(S.lag_deg),
                    double(core::acos_deg(S.aim_cos)), double(S.v_slip), double(S.slip[0]), double(S.slip[1]), double(S.slip[2]));
                sample_request_.store(true, std::memory_order_relaxed);
            }
        } else if ((!seq || seq == last_logged_seq_) && now - last_sample_tick_ >= 1000) {
            last_sample_tick_ = now;
            sample_request_.store(true, std::memory_order_relaxed);
        }
    }
    if (debug_ && avoidance_on_) {
        // the census rows (once per sector per session) and the detour samples (at most 32 rows per second)
        for (std::uint32_t w = census_write_.load(std::memory_order_acquire); census_read_ != w; ++census_read_) {
            const Census& c = census_[census_read_ & 3];
            char classes[256], buckets[256];
            int n = 0;
            for (unsigned k = 0; k < 32; ++k)
                if (c.classes[k]) {
                    const int m = std::snprintf(classes + n, sizeof classes - size_t(n), "%s%u:%u", n ? "," : "", k, unsigned(c.classes[k]));
                    if (m > 0 && n + m < int(sizeof classes)) n += m;
                }
            if (!n) classes[0] = 0;
            n = 0;
            for (unsigned k = 0; k < 32; ++k) {
                const int m = std::snprintf(buckets + n, sizeof buckets - size_t(n), "%s%u", k ? "," : "", unsigned(c.buckets[k]));
                if (m > 0 && n + m < int(sizeof buckets)) n += m;
            }
            float root_r;
            std::memcpy(&root_r, &c.station_root_r, 4);
            log("ship_avoid_census frame=%llu sector=0x%08lx objects=%lu overflow=%lu classes=%s buckets=%s", static_cast<unsigned long long>(frame),
                static_cast<unsigned long>(c.sector), static_cast<unsigned long>(c.objects), static_cast<unsigned long>(c.overflow), classes, buckets);
            if (c.station)
                log("ship_avoid_station frame=%llu sector=0x%08lx obj=0x%08lx root_r_m=%.1f node_scale_0x70=0x%08lx own_r_units=%lu children=%lu "
                    "unfilled=%lu scaled=%lu extent_units=%lu model=%s parts_offset_units:radius_units:cache_units:own_units:scale:flags="
                    "%ld,%ld,%ld:%ld:%ld:%ld:%08lx:%08lx|%ld,%ld,%ld:%ld:%ld:%ld:%08lx:%08lx|%ld,%ld,%ld:%ld:%ld:%ld:%08lx:%08lx|"
                    "%ld,%ld,%ld:%ld:%ld:%ld:%08lx:%08lx",
                    static_cast<unsigned long long>(frame), static_cast<unsigned long>(c.sector), static_cast<unsigned long>(c.station), double(root_r),
                    static_cast<unsigned long>(c.station_scale), static_cast<unsigned long>(c.station_own_r), static_cast<unsigned long>(c.station_children),
                    static_cast<unsigned long>(c.station_dirty), static_cast<unsigned long>(c.station_scaled), static_cast<unsigned long>(c.station_extent),
                    c.station_valid ? "parts" : "fallback",
                    long(c.part_offset[0][0]), long(c.part_offset[0][1]), long(c.part_offset[0][2]), long(c.part_r[0]), long(c.part_cache[0]),
                    long(c.part_own[0]), static_cast<unsigned long>(c.part_scale[0]), static_cast<unsigned long>(c.part_flags[0]),
                    long(c.part_offset[1][0]), long(c.part_offset[1][1]), long(c.part_offset[1][2]), long(c.part_r[1]), long(c.part_cache[1]),
                    long(c.part_own[1]), static_cast<unsigned long>(c.part_scale[1]), static_cast<unsigned long>(c.part_flags[1]),
                    long(c.part_offset[2][0]), long(c.part_offset[2][1]), long(c.part_offset[2][2]), long(c.part_r[2]), long(c.part_cache[2]),
                    long(c.part_own[2]), static_cast<unsigned long>(c.part_scale[2]), static_cast<unsigned long>(c.part_flags[2]),
                    long(c.part_offset[3][0]), long(c.part_offset[3][1]), long(c.part_offset[3][2]), long(c.part_r[3]), long(c.part_cache[3]),
                    long(c.part_own[3]), static_cast<unsigned long>(c.part_scale[3]), static_cast<unsigned long>(c.part_flags[3]));
        }
        if (now - last_detour_tick_ >= 1000) {
            last_detour_tick_ = now;
            detour_rows_this_second_ = 0;
        }
        for (std::uint32_t w = detour_write_.load(std::memory_order_acquire); detour_read_ != w; ++detour_read_) {
            if (detour_rows_this_second_ >= 32) continue;
            ++detour_rows_this_second_;
            const DetourEvent& e = detours_[detour_read_ & 15];
            log("ship_avoid_detour frame=%llu obj=0x%08lx order=0x%lx obstacle=0x%08lx kind=%lu side=%ld vertical=%lu t_cpa_s=%.2f miss_m=%.1f "
                "C_m=%.1f L_m=%.0f P_m=%.1f,%.1f,%.1f x_m=%.1f,%.1f,%.1f leader=0x%08lx obstacle_order=0x%lx",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(e.obj), static_cast<unsigned long>(e.order),
                static_cast<unsigned long>(e.obstacle), static_cast<unsigned long>(e.kind), long(std::int32_t(e.side)),
                static_cast<unsigned long>(e.vertical), double(e.t_cpa), double(e.miss), double(e.C), double(e.L), double(e.P[0]), double(e.P[1]),
                double(e.P[2]), double(e.x[0]), double(e.x[1]), double(e.x[2]), static_cast<unsigned long>(e.obstacle_leader),
                static_cast<unsigned long>(e.obstacle_order));
        }
    }
    if (debug_) { // amendment 16 (sections 16.2, 16.5): the latch rows of every capital and the width census per capital type
        static const char* const kLatchEvent[3] = {"pred_none", "slow_join", "unlatch"};
        static const char* const kLatchModes[5] = {"none", "out", "join", "fallback", "slow_join"};
        for (std::uint32_t w = latch_row_write_.load(std::memory_order_acquire); latch_row_read_ != w; ++latch_row_read_) {
            const LatchRow& r = latch_rows_[latch_row_read_ & 7];
            char cands[320] = "", lanes[200] = "";
            int n = 0;
            for (unsigned i = 0; i < core::kJoinCandidateCount; ++i) {
                const core::JoinPred& p = r.cand[i];
                const int m = std::snprintf(cands + n, sizeof cands - size_t(n), "%s%.0f:%d:%.2f:%.2f:%.0f:%.0f:%.0f:%u:%u", i ? "|" : "",
                                            double(core::kJoinCandidates[i]), int(p.grade), double(p.nose), double(p.vel), double(p.y),
                                            double(p.over), double(p.e_max), unsigned(p.reached), unsigned(p.steps));
                if (m > 0 && n + m < int(sizeof cands)) n += m;
            }
            n = 0;
            for (unsigned i = 0; i < r.n_lanes && i < 6; ++i) {
                const int m = std::snprintf(lanes + n, sizeof lanes - size_t(n), "%s0x%08lx:%s:%.0f", i ? "|" : "", static_cast<unsigned long>(r.lane_other[i]),
                                            ln::verdict_name(ln::Verdict(r.lane_verdict[i])), double(r.lane_gap[i]));
                if (m > 0 && n + m < int(sizeof lanes)) n += m;
            }
            log("ship_gate_capital_latch frame=%llu obj=0x%08lx class=%s slot=%u event=%s from=%s to=%s v_cmd=%.0f s_m=%.0f e_m=%.0f psi_deg=%.2f "
                "upsi_deg=%.2f w_dps=%.3f v_ms=%.1f W_dps=%.3f alpha_dps2=%.4f a_ms2=%.2f r_gate_m=%.0f "
                "cands_v:grade:nose:vel:e:over:e_max:reached:steps=%s obstacle=0x%08lx lanes_other:verdict:gap=%s",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(r.obj), r.code < core::kClassCount ? core::kClassNames[r.code] : "?",
                unsigned(r.slot), r.event < 3 ? kLatchEvent[r.event] : "?", r.before < 5 ? kLatchModes[r.before] : "?",
                r.after < 5 ? kLatchModes[r.after] : "?", double(r.v_cmd), double(r.plane.s), double(r.plane.y),
                double(r.plane.psi) * 180.0 / double(core::kPi), double(r.plane.upsi) * 180.0 / double(core::kPi),
                double(r.plane.w) * 180.0 / double(core::kPi), double(r.plane.v), double(r.params.W) * 180.0 / double(core::kPi),
                double(r.params.alpha) * 180.0 / double(core::kPi), double(r.params.a_lin), double(r.params.r_gate), cands[0] ? cands : "-",
                static_cast<unsigned long>(r.obstacle), lanes[0] ? lanes : "-");
        }
        for (std::uint32_t w = width_census_write_.load(std::memory_order_acquire); width_census_read_ != w; ++width_census_read_) {
            const WidthCensus& c = width_census_[width_census_read_ & 7];
            // amendment 16b: h_rw_m = sqrt(r^2 + w^2) (the note's h; l_m must not exceed it), len_m = max(r, l) and h_m = sqrt(len^2 + w^2),
            // the half-length and h the rule uses
            const float len = c.l > c.r ? c.l : c.r;
            const double h_rw = core::sqrt_ss(c.r * c.r + c.w * c.w), h = core::sqrt_ss(len * len + c.w * c.w);
            log("ship_capital_census frame=%llu type=%lu class=%s r_m=%.0f w_m=%.0f w_r=%.3f w_src=%s retries=%u l_m=%.0f h_rw_m=%.0f len_m=%.0f h_m=%.0f "
                "part_id=%ld part_lod=%ld o_x=%.0f o_y=%.0f e_x=%.0f e_y=%.0f l_r=%.3f capsule_L_m=%.0f",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(c.type), c.code < core::kClassCount ? core::kClassNames[c.code] : "?",
                double(c.r), double(c.w), c.r > 0.0f ? double(c.w / c.r) : 0.0, ln::width_src_name(ln::WidthSrc(c.w_src)), unsigned(c.retries),
                double(c.l), h_rw, double(len), h, static_cast<long>(c.part_id), static_cast<long>(c.part_lod), double(c.o_x), double(c.o_y),
                double(c.e_x), double(c.e_y), c.r > 0.0f ? double(c.l / c.r) : 0.0, double(len)); // the controller's capsule L = max(r, l)
        }
    }
    if (trace_on_) {
        const std::uint32_t t_now = game_time_ms();
        for (unsigned i = 0; i < kTraceMax; ++i) // a followed ship not seen for kTraceIdleMs frees its slot (gone, or no longer steered)
            if (trace_slots_[i].obj && t_now - trace_slots_[i].last_seen_ms > kTraceIdleMs) trace_slots_[i].obj = 0;
        if (cap_warn_used_) // review F3: a watched capital not hit for kCapWarnExpiryMs (docked, dead, gone from site B) frees its slot
            for (unsigned k = 0; k < kCapWarnSlots; ++k)
                if (cap_warn_[k].obj && t_now - cap_warn_[k].seen_ms > kCapWarnExpiryMs) cap_warn_free(k);
        for (std::uint32_t w = engine_warn_write_.load(std::memory_order_acquire); engine_warn_read_ != w; ++engine_warn_read_) {
            const EngineWarn& e = engine_warns_[engine_warn_read_ & 7];
            log("ship_avoid_engine_warn frame=%llu obj=0x%08lx order=0x12 count=%lu other=0x%08lx class=%lu code=%s r_m=%.1f d_m=%.1f",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(e.obj), static_cast<unsigned long>(e.count),
                static_cast<unsigned long>(e.other), static_cast<unsigned long>(e.cls),
                e.code < core::kClassCount ? core::kClassNames[e.code] : "-", double(e.r), double(e.d));
        }
        // amendment 15 (section 15.4): the capital join, site E's verdicts for slot 0, the engine's list on the warn frame
        static const char* const kModes[5] = {"none", "out", "join", "fallback", "slow_join"};
        static const char* const kReasons[4] = {"-", "pred_none", "past_x", "grid_invalid"};
        static const char* const kWhy[5] = {"-", "mode", "nose", "vel", "bound"};
        for (std::uint32_t w = join_row_write_.load(std::memory_order_acquire); join_row_read_ != w; ++join_row_read_) {
            const JoinRow& r = join_rows_[join_row_read_ & 7];
            char cross[160] = "";
            if (r.crossing)
                std::snprintf(cross, sizeof cross, " cross_nose_deg=%.2f cross_vel_deg=%.2f cross_v=%.1f kept=%u why_not=%s window_s=%.1f",
                              double(r.cross_nose), double(r.cross_vel), double(r.cross_v), unsigned(r.kept), r.why_not < 5 ? kWhy[r.why_not] : "?",
                              double(r.ms_in_window) / 1000.0);
            log("ship_gate_capital_join frame=%llu obj=0x%08lx class=%s slot=%u mode=%s reason=%s s_m=%.0f e_m=%.0f nose_deg=%.2f vel_deg=%.2f "
                "rate_dps=%.2f v_ms=%.1f v_cmd=%.0f carrot_m=%.0f pred_nose_deg=%.2f pred_e_m=%.0f%s",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(r.obj), r.code < core::kClassCount ? core::kClassNames[r.code] : "?",
                unsigned(r.slot), r.mode < 5 ? kModes[r.mode] : "?", r.latched ? "latched" : r.reason < 4 ? kReasons[r.reason] : "?", double(r.s),
                double(r.e), double(r.nose), double(r.vel), double(r.rate_dps), double(r.v), double(r.v_cmd), double(r.carrot), double(r.pred_nose),
                double(r.pred_e), cross);
        }
        // amendment 16 (section 16.5): the lane verdicts of the traced capitals (1 Hz per pair and on every change)
        static const char* const kFoot[4] = {"-", "lane", "joining", "segment"};
        for (std::uint32_t w = lane_row_write_.load(std::memory_order_acquire); lane_row_read_ != w; ++lane_row_read_) {
            const LaneRow& r = lane_rows_[lane_row_read_ & 15];
            log("ship_capital_lane frame=%llu A=0x%08lx B=0x%08lx verdict=%s gap_m=%.0f w_A=%.0f w_B=%.0f w_src=%s,%s foot=%s,%s e_m=%.0f theta_deg=%.2f "
                "ds_m=%.0f v_cap=%.1f D_m=%.0f route=%s watch=%u lat_gap=%.0f mot=%.0f h_T=%.0f h_L=%.0f trail=0x%08lx bind=%u held=%u",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(r.a), static_cast<unsigned long>(r.b),
                ln::verdict_name(ln::Verdict(r.verdict)), double(r.gap), double(r.w_a), double(r.w_b), ln::width_src_name(ln::WidthSrc(r.w_src_a)),
                ln::width_src_name(ln::WidthSrc(r.w_src_b)), r.foot_a < 4 ? kFoot[r.foot_a] : "?", r.foot_b < 4 ? kFoot[r.foot_b] : "?", double(r.e_a),
                double(r.theta_a), double(r.ds), double(r.v_cap), double(r.D), ln::route_name(ln::Route(r.route)), unsigned(r.watch), double(r.lat_gap),
                double(r.mot), double(r.h_t), double(r.h_l), static_cast<unsigned long>(r.trail), unsigned(r.bind), unsigned(r.held));
        }
        for (std::uint32_t w = e_row_write_.load(std::memory_order_acquire); e_row_read_ != w; ++e_row_read_) {
            const EVerdictRow& r = e_rows_[e_row_read_ & (kERows - 1)];
            // B.3 item 6: an own-gate row of a member A carries why the row passed (gc::GateRow), A's state age and the game step
            char gate_tail[96] = "";
            if (r.gate_row != 0xff)
                std::snprintf(gate_tail, sizeof gate_tail, " gate_row=%s ctl_age_ms=%ld step_ms=%lu", gc::gate_row_name(gc::GateRow(r.gate_row)),
                              long(r.gate_age_ms), static_cast<unsigned long>(r.step_ms));
            log("ship_avoid_e_verdict frame=%llu A=0x%08lx A_order=0x%x B=0x%08lx B_class=%lu B_code=%s B_order=0x%x B_leader=0x%08lx B_flags=0x%08lx "
                "member_leader=0x%08lx member_age_ms=%ld d_m=%.0f r_B=%.1f A_age_ms=%ld B_age_ms=%ld verdict=%s%s",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(r.a), unsigned(r.a_order), static_cast<unsigned long>(r.b),
                static_cast<unsigned long>(r.b_class), r.b_code < core::kClassCount ? core::kClassNames[r.b_code] : "-", unsigned(r.b_order),
                static_cast<unsigned long>(r.b_leader), static_cast<unsigned long>(r.b_flags), static_cast<unsigned long>(r.member_leader),
                long(r.member_age_ms), double(r.d), double(r.r_b), long(r.a_age_ms), long(r.b_age_ms),
                av::e_verdict_name(EV(r.verdict)), gate_tail);
        }
        for (std::uint32_t w = warn_pre_write_.load(std::memory_order_acquire); warn_pre_read_ != w; ++warn_pre_read_) {
            const WarnPreRow& p = warn_pre_[warn_pre_read_ & 7];
            char entries[720] = "";
            int n = 0;
            for (unsigned i = 0; i < 5 && i < p.count; ++i) {
                const int m = std::snprintf(entries + n, sizeof entries - size_t(n), "%s0x%08lx:%lu:%s:0x%lx:0x%08lx:0x%08lx:%.0f:%.0f:%s", i ? "|" : "",
                                            static_cast<unsigned long>(p.other[i]), static_cast<unsigned long>(p.cls[i]),
                                            p.code[i] < core::kClassCount ? core::kClassNames[p.code[i]] : "-", static_cast<unsigned long>(p.o_order[i]),
                                            static_cast<unsigned long>(p.leader[i]), static_cast<unsigned long>(p.member[i]), double(p.d[i]),
                                            double(p.r[i]), av::e_verdict_name(EV(p.verdict[i])));
                if (m > 0 && n + m < int(sizeof entries)) n += m;
            }
            log("ship_avoid_engine_warn_pre frame=%llu obj=0x%08lx order=0x%lx tgt=0x%08lx count=%lu "
                "entries_other:class:code:order:leader:member:d_m:r_m:e_verdict=%s",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(p.obj), static_cast<unsigned long>(p.order),
                static_cast<unsigned long>(p.tgt), static_cast<unsigned long>(p.count), n ? entries : "-");
        }
        for (std::uint32_t w = trace_write_.load(std::memory_order_acquire); trace_read_ != w; ++trace_read_) {
            const TraceRow& r = trace_ring_[trace_read_ & (kTraceRing - 1)];
            trace_rows_.add();
            if (r.has_e)
                log("ship_trace frame=%llu t_ms=%lu obj=0x%08lx class=%s order=0x%lx tgt=0x%08lx tgt_class=%u pos=%ld,%ld,%ld "
                    "fwd=%.5f,%.5f,%.5f up=%.5f,%.5f,%.5f rate=%ld,%ld,%ld v=%ld des=%ld des_engine=%ld point=%ld,%ld,%ld given=%ld,%ld,%ld "
                    "e=%ld,%ld,%ld dt_ms=%lu W=%ld,%ld,%ld rot=0x%08lx rot_pct=%ld vmax=%ld acc=%ld emg=%u omega_cmd=%.2f,%.2f,%.2f obst=0x%08lx slip=%ld,%ld,%ld cap_ms=%.1f cap_age_ms=%ld",
                    static_cast<unsigned long long>(frame), static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.obj),
                    r.code < core::kClassCount ? core::kClassNames[r.code] : "?", static_cast<unsigned long>(r.order),
                    static_cast<unsigned long>(r.tgt), unsigned(r.tgt_class), long(r.pos[0]), long(r.pos[1]), long(r.pos[2]), double(r.fwd[0]),
                    double(r.fwd[1]), double(r.fwd[2]), double(r.up[0]), double(r.up[1]), double(r.up[2]), long(r.rate[0]), long(r.rate[1]),
                    long(r.rate[2]), long(r.v), long(r.des), long(r.des_engine), long(r.point[0]), long(r.point[1]), long(r.point[2]), long(r.given[0]), long(r.given[1]),
                    long(r.given[2]), long(r.e[0]), long(r.e[1]), long(r.e[2]), static_cast<unsigned long>(r.dt_ms), long(r.W[0]), long(r.W[1]),
                    long(r.W[2]), static_cast<unsigned long>(r.rot_raw), long(r.rot_pct), long(r.vmax), long(r.accel), unsigned(r.emergency),
                    double(r.omega_cmd[0]), double(r.omega_cmd[1]), double(r.omega_cmd[2]), static_cast<unsigned long>(r.obstacle), long(r.slip[0]),
                    long(r.slip[1]), long(r.slip[2]), double(r.cap), long(r.cap_age_ms));
            else
                log("ship_trace frame=%llu t_ms=%lu obj=0x%08lx class=%s order=0x%lx tgt=0x%08lx tgt_class=%u pos=%ld,%ld,%ld "
                    "fwd=%.5f,%.5f,%.5f up=%.5f,%.5f,%.5f rate=%ld,%ld,%ld v=%ld des=%ld des_engine=%ld point=%ld,%ld,%ld given=%ld,%ld,%ld "
                    "e=- dt_ms=%lu W=%ld,%ld,%ld rot=0x%08lx rot_pct=%ld vmax=%ld acc=%ld emg=%u omega_cmd=%.2f,%.2f,%.2f obst=0x%08lx slip=%ld,%ld,%ld "
                    "cap_ms=%.1f cap_age_ms=%ld",
                    static_cast<unsigned long long>(frame), static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.obj),
                    r.code < core::kClassCount ? core::kClassNames[r.code] : "?", static_cast<unsigned long>(r.order),
                    static_cast<unsigned long>(r.tgt), unsigned(r.tgt_class), long(r.pos[0]), long(r.pos[1]), long(r.pos[2]), double(r.fwd[0]),
                    double(r.fwd[1]), double(r.fwd[2]), double(r.up[0]), double(r.up[1]), double(r.up[2]), long(r.rate[0]), long(r.rate[1]),
                    long(r.rate[2]), long(r.v), long(r.des), long(r.des_engine), long(r.point[0]), long(r.point[1]), long(r.point[2]), long(r.given[0]), long(r.given[1]),
                    long(r.given[2]), static_cast<unsigned long>(r.dt_ms), long(r.W[0]), long(r.W[1]), long(r.W[2]), static_cast<unsigned long>(r.rot_raw),
                    long(r.rot_pct), long(r.vmax), long(r.accel), unsigned(r.emergency), double(r.omega_cmd[0]), double(r.omega_cmd[1]),
                    double(r.omega_cmd[2]), static_cast<unsigned long>(r.obstacle), long(r.slip[0]), long(r.slip[1]), long(r.slip[2]), double(r.cap), long(r.cap_age_ms));
        }
    }
    if (debug_ && gates_on_) {
        for (std::uint32_t w = gate_trans_write_.load(std::memory_order_acquire); gate_trans_read_ != w; ++gate_trans_read_) {
            const GateTransition& e = gate_trans_[gate_trans_read_ & 7];
            log("ship_gate_transition frame=%llu obj=0x%08lx class=%s capital=%u validated=%s crossed=%u last_dA_r=%.2f err_B_deg=%.1f "
                "v_ms=%.1f desired_ms=%.1f B_gate_r=%.2f,%.2f,%.2f A_gate_r=%.2f,%.2f,%.2f dock_blocked=%u "
                "p2_pos_r=%.2f,%.2f,%.2f p2_fwd=%.2f,%.2f,%.2f p2_carrot_r=%.2f,%.2f,%.2f o5_pos_r=%.2f,%.2f,%.2f o5_fwd=%.2f,%.2f,%.2f "
                "o5_dA_r=%.2f o5_dB_r=%.2f ms_since_p2=%lu carrot_age_ms=%ld",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(e.obj),
                e.code < core::kClassCount ? core::kClassNames[e.code] : "?", unsigned(e.capital),
                e.validated == 1 ? "matched" : e.validated == 2 ? "fallback" : "none", unsigned(e.crossed), double(e.dA_r), double(e.err_B_deg),
                double(e.v_ms), double(e.desired_ms), double(e.B_local[0]), double(e.B_local[1]), double(e.B_local[2]), double(e.A_local[0]),
                double(e.A_local[1]), double(e.A_local[2]), unsigned(e.dock_blocked), double(e.p2_pos[0]), double(e.p2_pos[1]), double(e.p2_pos[2]),
                double(e.p2_fwd[0]), double(e.p2_fwd[1]), double(e.p2_fwd[2]), double(e.p2_carrot[0]), double(e.p2_carrot[1]), double(e.p2_carrot[2]),
                double(e.o5_pos[0]), double(e.o5_pos[1]), double(e.o5_pos[2]), double(e.o5_fwd[0]), double(e.o5_fwd[1]), double(e.o5_fwd[2]),
                double(e.o5_dA_r), double(e.o5_dB_r), static_cast<unsigned long>(e.ms_since_p2), long(e.carrot_age_ms));
        }
        for (std::uint32_t w = gate_event_write_.load(std::memory_order_acquire); gate_event_read_ != w; ++gate_event_read_) {
            const GateEvent& e = gate_events_[gate_event_read_ & 7];
            log("ship_gate_approach frame=%llu obj=0x%08lx class=%s capital=%u ring=%u offset_m=%.0f,%.0f cross_v_ms=%.1f "
                "cross_heading_err_deg=%.1f cross_to_launch_ms=%lu v0_ms=%.1f",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(e.obj),
                e.code < core::kClassCount ? core::kClassNames[e.code] : "?", unsigned(e.capital), unsigned(e.spread_k), double(e.off[0]),
                double(e.off[1]), double(e.cross_v), double(e.cross_err_deg), static_cast<unsigned long>(e.cross_to_launch_ms),
                double(e.v0_units) / double(core::kUnitsPerMetre));
        }
    }
    if (now - last_summary_tick_ < 1000) return;
    const unsigned long long interval = last_summary_tick_ ? now - last_summary_tick_ : 1000;
    last_summary_tick_ = now;
    const std::uint32_t calls = calls_.get(), handled = handled_.get(), ccalls = clamp_calls_.get(),
                        capplied = clamp_applied_.get(), cstale = clamp_stale_.get(), redirected = redirected_.get();
    const std::uint32_t d_calls = calls - rep_calls_;
    if (!d_calls && ccalls == rep_clamp_calls_) return; // nothing happened: no row
    // Percentiles from the log2 histogram (bucket b holds ticks in [2^b, 2^(b+1))).
    std::uint32_t hist[32], total = 0;
    for (unsigned b = 0; b < 32; ++b) {
        hist[b] = qpc_hist_[b].exchange(0, std::memory_order_relaxed);
        total += hist[b];
    }
    const std::uint32_t max_ticks = qpc_max_.exchange(0, std::memory_order_relaxed);
    auto percentile = [&](double p) {
        std::uint32_t seen = 0;
        for (unsigned b = 0; b < 32; ++b) {
            seen += hist[b];
            if (total && double(seen) >= p * double(total)) return double(1ull << (b + 1)) * 1e6 / double(qpc_frequency_);
        }
        return 0.0;
    };
    const double us_max = qpc_frequency_ ? double(max_ticks) * 1e6 / double(qpc_frequency_) : 0.0;
    // the percentiles are bucket upper bounds (powers of two): never above the measured maximum
    auto clamped = [&](double p) {
        const double v = percentile(p);
        return v > us_max ? us_max : v;
    };
    const unsigned ships = table_.live(game_time_ms(), 1000);
    const double per_ship = ships ? double(d_calls) * 1000.0 / double(interval) / double(ships) : 0.0;
    char reasons[512];
    int n = 0;
    for (unsigned r = 1; r < core::kReasonCount; ++r) {
        const std::uint32_t v = reasons_[r].get();
        const int k = std::snprintf(reasons + n, sizeof reasons - size_t(n), " pass_%s=%lu", core::reason_name(core::Reason(r)),
                                    static_cast<unsigned long>(v - rep_reasons_[r]));
        rep_reasons_[r] = v;
        if (k > 0 && n + k < int(sizeof reasons)) n += k;
    }
    char orders[256];
    n = 0;
    for (unsigned o = 0; o < 0x40; ++o) {
        const std::uint32_t v = handled_by_order_[o].get();
        if (!v) continue;
        const int k = std::snprintf(orders + n, sizeof orders - size_t(n), "%s0x%x:%lu", n ? "," : "", o, static_cast<unsigned long>(v));
        if (k > 0 && n + k < int(sizeof orders)) n += k;
    }
    const std::uint32_t l0 = leaf_calls_[0].get(), l1 = leaf_calls_[1].get(), l2 = leaf_calls_[2].get();
    char avoidance[2048] = "";
    if (avoidance_on_) {
        const Counter* cs[13] = {&av_calls_, &av_conflicts_, &av_detours_, &av_unresolved_, &av_overflow_, &av_snapshots_, &av_walk_objects_,
                                 &av_emergency_, &e_calls_, &e_suppressed_, &part_built_, &part_fallback_, &part_invalid_};
        std::uint32_t d[13];
        for (unsigned i = 0; i < 13; ++i) {
            const std::uint32_t v = cs[i]->get();
            d[i] = v - rep_av_[i];
            rep_av_[i] = v;
        }
        unsigned snap_max = 0;
        for (unsigned i = 0; i < sites::kSnapshotSlots; ++i)
            if (snaps_[i].count > snap_max) snap_max = snaps_[i].count;
        std::uint32_t why[unsigned(av::Unresolved::count)];
        for (unsigned i = 0; i < unsigned(av::Unresolved::count); ++i) {
            const std::uint32_t v = av_unresolved_why_[i].get();
            why[i] = v - rep_av_why_[i];
            rep_av_why_[i] = v;
        }
        const Counter* ep[4] = {&av_conflict_episodes_, &av_unresolved_episodes_, &part_dirty_, &part_scaled_};
        std::uint32_t e[4];
        for (unsigned i = 0; i < 4; ++i) {
            const std::uint32_t v = ep[i]->get();
            e[i] = v - rep_av_ep_[i];
            rep_av_ep_[i] = v;
        }
        // amendments 13/15: site E's capital rows and the membership memory, totals since install (per reason: the pass-throughs)
        const std::uint32_t es_small = e_suppressed_small_.get(), ep_small = e_small_pass_.get();
        std::uint32_t er_small[unsigned(av::SmallPair::count)];
        for (unsigned i = 0; i < unsigned(av::SmallPair::count); ++i) er_small[i] = e_small_pass_reason_[i].get();
        const av::MemberStats& ms = member_stats_;
        std::snprintf(avoidance, sizeof avoidance,
                      " avoid_calls=%lu conflicts=%lu conflict_episodes=%lu detours=%lu unresolved=%lu unresolved_episodes=%lu "
                      "unresolved_inside_stop=%lu unresolved_inside_turn=%lu unresolved_blocked=%lu unresolved_behind=%lu unresolved_degenerate=%lu "
                      "snapshot_overflow=%lu snapshots=%lu walked=%lu emergency=%lu e_calls=%lu e_suppressed=%lu part_models=%lu "
                      "part_fallback=%lu part_invalid=%lu part_unfilled=%lu part_scaled=%lu snapshot_max=%u warn=%s "
                      "e_suppressed_small=%lu e_small_pass=%lu e_small_pass_no_autopilot=%lu e_small_pass_not_steerable=%lu e_small_pass_class=%lu "
                      "e_small_pass_b_dock_leg=%lu e_small_pass_own_leader=%lu e_suppressed_member=%lu e_suppressed_gate=%lu e_suppressed_gate_small=%lu "
                      "avoid_excluded_member=%lu member_set=%u member_refresh=%u member_replace=%u member_expire=%u member_leader_gone=%u "
                      "member_id_mismatch=%u member_reversed=%u member_player=%u member_cleared=%u member_full=%u e_small_gap=%lu",
                      static_cast<unsigned long>(d[0]), static_cast<unsigned long>(d[1]), static_cast<unsigned long>(e[0]),
                      static_cast<unsigned long>(d[2]), static_cast<unsigned long>(d[3]), static_cast<unsigned long>(e[1]),
                      static_cast<unsigned long>(why[1]), static_cast<unsigned long>(why[2]), static_cast<unsigned long>(why[3]),
                      static_cast<unsigned long>(why[4]), static_cast<unsigned long>(why[5]), static_cast<unsigned long>(d[4]),
                      static_cast<unsigned long>(d[5]), static_cast<unsigned long>(d[6]), static_cast<unsigned long>(d[7]),
                      static_cast<unsigned long>(d[8]), static_cast<unsigned long>(d[9]), static_cast<unsigned long>(d[10]),
                      static_cast<unsigned long>(d[11]), static_cast<unsigned long>(d[12]), static_cast<unsigned long>(e[2]),
                      static_cast<unsigned long>(e[3]), snap_max, warn_state_, static_cast<unsigned long>(es_small),
                      static_cast<unsigned long>(ep_small), static_cast<unsigned long>(er_small[1]), static_cast<unsigned long>(er_small[2]),
                      static_cast<unsigned long>(er_small[3]), static_cast<unsigned long>(er_small[4]), static_cast<unsigned long>(er_small[5]),
                      static_cast<unsigned long>(e_suppressed_member_.get()), static_cast<unsigned long>(e_suppressed_gate_.get()),
                      static_cast<unsigned long>(e_suppressed_gate_small_.get()), static_cast<unsigned long>(avoid_excluded_member_.get()), ms.set,
                      ms.refresh, ms.replace, ms.expire, ms.leader_gone, ms.id_mismatch, ms.reversed, ms.player, ms.cleared, ms.full,
                      static_cast<unsigned long>(e_small_gap_.get())); // B.4: suppress_small on a stale A (the order-0 gap)
    }
    char gates[kSummaryGatesBytes] = "";
    if (gates_on_) {
        const Counter* gc[32] = {&gate_lead_, &gate_cross_, &gate_p2_, &gate_stops_skipped_, &gate_p4_, &gate_v0_kept_, &gate_p5_,
                                 &gate_launch_continued_, &gate_speed_writes_, &gate_spread_offsets_, &gate_exit_offsets_,
                                 &gate_capital_aligned_, &gate_warp_calls_, &gate_lead_validated_, &gate_lead_fallback_,
                                 &gate_lead_window_rejected_, &gate_dock_writes_, &gate_dock_blocked_, &gate_p2_no_gate_target_,
                                 &gate_p2_capital_, &gate_p2_no_state_, &gate_p4_no_gate_target_, &gate_p4_capital_, &gate_p4_no_state_,
                                 &gate_dock_invalid_, &gate_warp_skipped_, &gate_rows_dropped_, &avoid_excluded_followers_,
                                 &avoid_skipped_small_, &gate_capital_kept_, &gate_capital_lead_validated_, &gate_capital_lead_fallback_};
        std::uint32_t d[32];
        for (unsigned i = 0; i < 32; ++i) {
            const std::uint32_t v = gc[i]->get();
            d[i] = v - rep_gate_[i];
            rep_gate_[i] = v;
        }
        std::snprintf(gates, sizeof gates,
                      " gates=%s gate_lead_calls=%lu gate_crossings=%lu gate_p2=%lu gate_stops_skipped=%lu gate_p4=%lu gate_v0_kept=%lu "
                      "gate_p5=%lu gate_launch_continued=%lu gate_speed_writes=%lu gate_spread_offsets=%lu gate_exit_offsets=%lu "
                      "gate_capital_aligned=%lu gate_warp_calls=%lu gate_lead_validated=%lu gate_lead_fallback=%lu "
                      "gate_lead_window_rejected=%lu gate_dock_writes=%lu gate_dock_blocked=%lu gate_p2_no_gate_target=%lu "
                      "gate_p2_capital=%lu gate_p2_no_state=%lu gate_p4_no_gate_target=%lu gate_p4_capital=%lu gate_p4_no_state=%lu "
                      "gate_dock_invalid=%lu gate_warp_skipped=%lu gate_rows_dropped=%lu avoid_excluded_followers=%lu avoid_skipped_small=%lu "
                      "gates_capital=%s gate_capital_kept=%lu gate_capital_lead_validated=%lu gate_capital_lead_fallback=%lu",
                      gates_state_, static_cast<unsigned long>(d[0]), static_cast<unsigned long>(d[1]), static_cast<unsigned long>(d[2]),
                      static_cast<unsigned long>(d[3]), static_cast<unsigned long>(d[4]), static_cast<unsigned long>(d[5]),
                      static_cast<unsigned long>(d[6]), static_cast<unsigned long>(d[7]), static_cast<unsigned long>(d[8]),
                      static_cast<unsigned long>(d[9]), static_cast<unsigned long>(d[10]), static_cast<unsigned long>(d[11]),
                      static_cast<unsigned long>(d[12]), static_cast<unsigned long>(d[13]), static_cast<unsigned long>(d[14]),
                      static_cast<unsigned long>(d[15]), static_cast<unsigned long>(d[16]), static_cast<unsigned long>(d[17]),
                      static_cast<unsigned long>(d[18]), static_cast<unsigned long>(d[19]), static_cast<unsigned long>(d[20]),
                      static_cast<unsigned long>(d[21]), static_cast<unsigned long>(d[22]), static_cast<unsigned long>(d[23]),
                      static_cast<unsigned long>(d[24]), static_cast<unsigned long>(d[25]), static_cast<unsigned long>(d[26]),
                      static_cast<unsigned long>(d[27]), static_cast<unsigned long>(d[28]), gates_capital_state_,
                      static_cast<unsigned long>(d[29]), static_cast<unsigned long>(d[30]), static_cast<unsigned long>(d[31]));
        // amendment 15 (section 15.4): the join, totals since install; the predictor's cost (the slowest evaluation, us)
        const std::size_t used = std::strlen(gates);
        std::snprintf(gates + used, sizeof gates - used,
                      " gate_capital_join=%lu gate_capital_slowed=%lu gate_capital_fallback_pred=%lu gate_capital_fallback_past_x=%lu "
                      "gate_capital_not_kept_mode=%lu gate_capital_not_kept_nose=%lu gate_capital_not_kept_vel=%lu gate_capital_not_kept_bound=%lu "
                      "join_evals=%lu join_predictions=%lu join_eval_us_max=%.1f join_slots=%u join_slots_dropped=%u join_slots_refused=%u",
                      static_cast<unsigned long>(gate_capital_join_.get()), static_cast<unsigned long>(gate_capital_slowed_.get()),
                      static_cast<unsigned long>(gate_capital_fallback_pred_.get()), static_cast<unsigned long>(gate_capital_fallback_past_x_.get()),
                      static_cast<unsigned long>(gate_capital_not_kept_[1].get()), static_cast<unsigned long>(gate_capital_not_kept_[2].get()),
                      static_cast<unsigned long>(gate_capital_not_kept_[3].get()), static_cast<unsigned long>(gate_capital_not_kept_[4].get()),
                      static_cast<unsigned long>(join_evals_), static_cast<unsigned long>(join_predictions_),
                      // via int32 (cvtsi2sd): a 64-bit integer -> double conversion is x87 fild on i686
                      qpc_frequency_ ? double(std::int32_t(join_eval_max_ticks_ > 0x7fffffffu ? 0x7fffffffu : join_eval_max_ticks_)) * 1e6 /
                                           double(std::int32_t(qpc_frequency_ > 0x7fffffffull ? 0x7fffffffu : std::uint32_t(qpc_frequency_)))
                                     : 0.0,
                      join_reg_.count, join_reg_.dropped, join_reg_.refused);
        // amendment 16 (section 16.5): the lane rule, the slow join and the un-latch, totals since install
        const std::size_t used16 = std::strlen(gates);
        std::snprintf(gates + used16, sizeof gates - used16,
                      " lane_exempt=%lu lane_follow=%lu lane_cross=%lu lane_capped=%lu e_suppressed_lane=%lu gate_capital_slow_join=%lu "
                      "gate_capital_unlatch=%lu gate_capital_detour_write=%lu lane_board_full=%u,%u lane_seq_lat=%lu lane_seq_along=%lu "
                      "lane_seq_cross=%lu lane_apart=%lu lane_cap_carried=%lu lane_hysteresis_hold=%lu",
                      static_cast<unsigned long>(lane_exempt_.get()), static_cast<unsigned long>(lane_follow_.get()),
                      static_cast<unsigned long>(lane_cross_.get()), static_cast<unsigned long>(lane_capped_.get()),
                      static_cast<unsigned long>(e_suppressed_lane_.get()), static_cast<unsigned long>(gate_capital_slow_join_.get()),
                      static_cast<unsigned long>(gate_capital_unlatch_.get()), static_cast<unsigned long>(gate_capital_detour_write_.get()),
                      lanes_.full_gates, lanes_.full_entries, static_cast<unsigned long>(lane_seq_lat_.get()),
                      static_cast<unsigned long>(lane_seq_along_.get()), static_cast<unsigned long>(lane_seq_cross_.get()),
                      static_cast<unsigned long>(lane_apart_.get()), static_cast<unsigned long>(lane_cap_carried_.get()),
                      static_cast<unsigned long>(lane_hysteresis_hold_.get()));
        // the gate traffic controller's ring changes (D.7): the per-source drop counters, totals since install (their sum is
        // gate_rows_dropped's total)
        // every append is bounded by the buffer: snprintf truncates, and used is clamped below its size (a full buffer ends the row; the
        // longest row is bounded at 3,614 characters by summary_bound.py, each conversion at its widest: gate-traffic-controller/)
        auto clamp_used = [&](std::size_t u) { return u < sizeof gates - 1 ? u : sizeof gates - 1; };
        std::size_t used_d = std::strlen(gates);
        used_d = clamp_used(used_d + std::size_t(std::snprintf(gates + used_d, sizeof gates - used_d, " rows_dropped=")));
        for (unsigned i = 0; i < unsigned(Drop::count); ++i)
            used_d = clamp_used(used_d + std::size_t(std::snprintf(gates + used_d, sizeof gates - used_d, "%s%s:%lu", i ? "," : "", kDropNames[i],
                                                                   static_cast<unsigned long>(rows_dropped_[i].get()))));
        if (traffic_on_) { // the controller (D.7), totals since install
            unsigned members = 0;
            for (const gc::Gate& cg : ctl_.g)
                for (unsigned i = 0; i < cg.n; ++i) members += gc::pair_role(cg.m[i].role) ? 1u : 0u;
            std::snprintf(gates + used_d, sizeof gates - used_d,
                          " traffic=%s ctl_members=%u ctl_decisions=%lu ctl_rollouts=%lu ctl_sweeps=%lu ctl_narrow=%lu ctl_conflicts=%lu "
                          "ctl_yields=%lu ctl_capped=%lu ctl_released=%lu ctl_unresolved=%lu ctl_entered=%lu ctl_left=%lu ctl_evicted=%lu "
                          "ctl_full=%u,%u e_suppressed_ctl=%lu e_pass_ctl=%lu e_suppressed_arrival_gate=%lu avoid_ctl_excluded=%lu "
                          "arr_launches=%lu arr_offset_capital=%lu arr_small_side=%lu arr_small_unseparated=%lu arr_skipped=%lu "
                          "arr_overwritten=%u ctl_decide_us_max=%.1f ctl_frame_us_max=%.1f ctl_frame_decisions_max=%u ctl_arrival_us_max=%.1f "
                          "ctl_releases=%lu ctl_go_around=%lu ctl_x_refused=%lu ctl_x_wait_s=%lu ctl_sb_steered=%lu ctl_sb_pred=%lu ctl_guarded=%lu "
                          "ctl_p2_zeroed=%lu small_b_rewrites=%lu ctl_sb_us_max=%.1f e_pass_ctl_why=%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu e_gate_small_gap=%lu "
                          "ctl_sb_misc=%lu,%lu,%lu,%lu ctl_evict_ignored_gate=%lu ctl_budget_cut=%lu ctl_live=%lu,%lu,%lu,%lu,%lu,%lu "
                          "ctl_live_us_max=%.1f",
                          traffic_state_, members, static_cast<unsigned long>(ctl_decisions_.get()), static_cast<unsigned long>(ctl_rollouts_.get()),
                          static_cast<unsigned long>(ctl_sweeps_.get()), static_cast<unsigned long>(ctl_narrow_.get()),
                          static_cast<unsigned long>(ctl_conflicts_.get()), static_cast<unsigned long>(ctl_yields_.get()),
                          static_cast<unsigned long>(ctl_capped_.get()), static_cast<unsigned long>(ctl_released_.get()),
                          static_cast<unsigned long>(ctl_unresolved_.get()), static_cast<unsigned long>(ctl_entered_.get()),
                          static_cast<unsigned long>(ctl_left_.get()), static_cast<unsigned long>(ctl_evicted_.get()), ctl_.full_gates,
                          ctl_.full_members, static_cast<unsigned long>(e_suppressed_ctl_.get()), static_cast<unsigned long>(e_pass_ctl_.get()),
                          static_cast<unsigned long>(e_suppressed_arrival_gate_.get()), static_cast<unsigned long>(avoid_ctl_excluded_.get()),
                          static_cast<unsigned long>(arr_launches_.get()), static_cast<unsigned long>(arr_offset_capital_.get()),
                          static_cast<unsigned long>(arr_small_side_.get()), static_cast<unsigned long>(arr_small_unseparated_.get()),
                          static_cast<unsigned long>(arr_skipped_.get()), arrivals_.overwritten, ticks_us(ctl_decide_max_ticks_),
                          ticks_us(ctl_frame_max_ticks_), ctl_frame_max_decisions_, ticks_us(ctl_arrival_max_ticks_),
                          static_cast<unsigned long>(ctl_releases_.get()), static_cast<unsigned long>(ctl_go_around_.get()),
                          static_cast<unsigned long>(ctl_x_refused_.get()), static_cast<unsigned long>(ctl_x_wait_ms_ / 1000u),
                          static_cast<unsigned long>(ctl_sb_steered_.get()), static_cast<unsigned long>(ctl_sb_predictions_.get()),
                          static_cast<unsigned long>(ctl_guarded_.get()), static_cast<unsigned long>(ctl_p2_zeroed_.get()),
                          static_cast<unsigned long>(small_b_rewrites_.get()), ticks_us(ctl_sb_max_ticks_),
                          // e_pass_ctl_why: none, unresolved, stale, starved, on_0x12, rows, order0, not_member (gc::EWhy 1..8)
                          static_cast<unsigned long>(e_pass_ctl_why_[1].get()), static_cast<unsigned long>(e_pass_ctl_why_[2].get()),
                          static_cast<unsigned long>(e_pass_ctl_why_[3].get()), static_cast<unsigned long>(e_pass_ctl_why_[4].get()),
                          static_cast<unsigned long>(e_pass_ctl_why_[5].get()), static_cast<unsigned long>(e_pass_ctl_why_[6].get()),
                          static_cast<unsigned long>(e_pass_ctl_why_[7].get()), static_cast<unsigned long>(e_pass_ctl_why_[8].get()),
                          static_cast<unsigned long>(e_gate_small_gap_.get()),
                          // ctl_sb_misc: seq_skipped (the 60 s tie-break), go-arounds within 1 km of X, game-clock resets, small B both sides taken
                          static_cast<unsigned long>(ctl_seq_skipped_.get()), static_cast<unsigned long>(ctl_go_around_near_.get()),
                          static_cast<unsigned long>(ctl_clock_resets_.get()), static_cast<unsigned long>(small_b_both_.get()),
                          // B.3: 0x12 episodes kept through (item 2), frames whose decisions stopped at the 1 ms ceiling
                          static_cast<unsigned long>(ctl_evict_ignored_gate_.get()), static_cast<unsigned long>(ctl_budget_cut_.get()),
                          // B.5 ctl_live: members left unproven (gone), launched members left, sector resets, entries they dropped,
                          // member-frames without a proof, frames whose set overflowed; the proofs' longest frame
                          static_cast<unsigned long>(ctl_live_gone_.get()), static_cast<unsigned long>(ctl_live_launch_.get()),
                          static_cast<unsigned long>(ctl_sector_resets_.get()), static_cast<unsigned long>(ctl_sector_dropped_.get()),
                          static_cast<unsigned long>(ctl_unproven_.get()), static_cast<unsigned long>(ctl_live_overflow_.get()),
                          ticks_us(ctl_live_max_ticks_));
            ctl_sb_max_ticks_ = 0; // B.2 item 5: ctl_sb_us_max is the summary interval's maximum (the other maxima stay session-lifetime)
        }
    }
    char dock[640] = "";
    if (avoidance_on_) { // amendment 18, totals since install
        std::snprintf(dock, sizeof dock,
                      " dock=%s e_suppressed_dock=%lu e_pass_dock_parts=%lu e_pass_dock_path=%lu e_pass_dock_ray=%lu e_pass_dock_zone=%lu "
                      "dock_part_obstacles=%lu dock_detours=%lu idle_steered=%lu dock_models_built=%lu dock_no_model=%lu dock_rows_dropped=%lu "
                      "dock_parts_evicted=%lu dock_e_us_max=%.1f",
                      dock_state_, static_cast<unsigned long>(e_suppressed_dock_.get()),
                      static_cast<unsigned long>(e_pass_dock_[unsigned(av::DockE::no_parts)].get()),
                      static_cast<unsigned long>(e_pass_dock_[unsigned(av::DockE::path)].get()),
                      static_cast<unsigned long>(e_pass_dock_[unsigned(av::DockE::ray)].get()),
                      static_cast<unsigned long>(e_pass_dock_[unsigned(av::DockE::zone)].get()),
                      static_cast<unsigned long>(dock_part_obstacles_.get()), static_cast<unsigned long>(dock_detours_.get()),
                      static_cast<unsigned long>(idle_steered_.get()), static_cast<unsigned long>(dock_models_built_.get()),
                      static_cast<unsigned long>(dock_no_model_.get()), static_cast<unsigned long>(dock_rows_dropped_.get()),
                      static_cast<unsigned long>(dock_parts_evicted_.get()), ticks_us(dock_e_max_ticks_));
    }
    char trace[96] = "";
    if (trace_on_) {
        const std::uint32_t rows = trace_rows_.get(), dropped = trace_dropped_.get();
        std::snprintf(trace, sizeof trace, " ship_trace_rows=%lu ship_trace_dropped=%lu", static_cast<unsigned long>(rows - rep_trace_[0]),
                      static_cast<unsigned long>(dropped - rep_trace_[1]));
        rep_trace_[0] = rows;
        rep_trace_[1] = dropped;
    }
    log("ship_steering_summary frame=%llu interval_ms=%llu claims=%u calls=%lu handled=%lu ships=%u calls_per_ship_s=%.1f "
        "leaf_calls=%lu,%lu,%lu%s clamp_calls=%lu clamp_applied=%lu clamp_stale=%lu redirected=%lu timed=%lu "
        "handler_us_p50=%.2f handler_us_p99=%.2f handler_us_max=%.2f handled_by_order_total=%s stuck_total=%lu%s%s%s%s",
        static_cast<unsigned long long>(frame), interval, unsigned(sites::Count), static_cast<unsigned long>(d_calls),
        static_cast<unsigned long>(handled - rep_handled_), ships, per_ship, static_cast<unsigned long>(l0 - rep_leaf_[0]),
        static_cast<unsigned long>(l1 - rep_leaf_[1]), static_cast<unsigned long>(l2 - rep_leaf_[2]), reasons,
        static_cast<unsigned long>(ccalls - rep_clamp_calls_), static_cast<unsigned long>(capplied - rep_clamp_applied_),
        static_cast<unsigned long>(cstale - rep_clamp_stale_), static_cast<unsigned long>(redirected - rep_redirected_),
        static_cast<unsigned long>(total), clamped(0.5), clamped(0.99), us_max, orders,
        static_cast<unsigned long>(stuck_count_.load(std::memory_order_relaxed)), avoidance, gates, dock, trace);
    rep_calls_ = calls;
    rep_handled_ = handled;
    rep_clamp_calls_ = ccalls;
    rep_clamp_applied_ = capplied;
    rep_clamp_stale_ = cstale;
    rep_redirected_ = redirected;
    rep_leaf_[0] = l0;
    rep_leaf_[1] = l1;
    rep_leaf_[2] = l2;
}
bool shutdown() {
    // Every site still registered as patched (an installed module, or a site left live by a failed rollback) is
    // restored in reverse order; a site whose restore fails again stays registered for the next attempt.
    bool any = warn_site_.patched_in;
    for (unsigned i = 0; i < sites::Count; ++i) any = any || patches_[i].patched_in;
    for (unsigned i = 0; i < sites::GateCount; ++i) any = any || gate_patches_[i].patched_in;
    any = any || traffic_patch_.patched_in;
    if (!any) return true;
    const DWORD error = GetLastError();
    installed_.store(false, std::memory_order_release); // the handlers pass through from here on
    warn_live_.store(false, std::memory_order_release);
    gates_live_.store(false, std::memory_order_release);
    traffic_live_.store(false, std::memory_order_release);
    x3m_ship_gates_live = 0; // the gate stubs pass through to their tails from here on
    x3m_ship_traffic_live = 0;
    bool restored = true;
    if (traffic_patch_.patched_in && !engine_patch::restore(traffic_patch_)) restored = false;
    for (unsigned i = sites::GateCount; i-- > 0;)
        if (gate_patches_[i].patched_in && !engine_patch::restore(gate_patches_[i])) restored = false;
    if (warn_site_.patched_in && !engine_patch::restore(warn_site_)) restored = false;
    for (unsigned i = sites::Count; i-- > 0;)
        if (patches_[i].patched_in && !engine_patch::restore(patches_[i])) restored = false;
    state_ = restored ? "restored" : "restore_failed_retry_at_next_shutdown";
    SetLastError(error);
    return restored;
}
}
