#include "ship_steering.h"
#include "ship_steering_core.h"
#include "ship_avoidance_core.h"
#include "ship_avoidance_walk_core.h"
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
wk::PartCache part_cache_;                       // station part models, keyed on object, root radius and position/basis
av::ShipAvoid avoid_[core::kShipCapacity];      // parallel to table_.entries (same slot index)
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
Counter e_calls_, e_suppressed_, part_built_, part_fallback_, part_invalid_;
Counter gate_lead_, gate_cross_, gate_p2_, gate_stops_skipped_, gate_p4_, gate_v0_kept_, gate_p5_, gate_launch_continued_, gate_speed_writes_;
Counter gate_spread_offsets_, gate_exit_offsets_, gate_capital_aligned_, gate_warp_calls_;
Counter gate_lead_validated_, gate_lead_fallback_, gate_lead_window_rejected_, gate_dock_writes_, gate_dock_blocked_;
Counter gate_p2_no_gate_target_, gate_p2_capital_, gate_p2_no_state_, gate_p4_no_gate_target_, gate_p4_capital_, gate_p4_no_state_;
Counter gate_dock_invalid_, gate_warp_skipped_, gate_rows_dropped_, avoid_excluded_followers_, avoid_skipped_small_;
std::uint32_t rep_gate_[29] = {};
// Step 4 item 7: ship_trace (--debug tier, X3M_SHIP_TRACE=1, X3M_SHIP_TRACE_MAX ships): the module follows the first N
// non-capital class-7 autopilot ships that enter a gate approach (order 4 with a class-0x12 target) or a dock approach
// (order 4 with a class-5/6 target, or order 5) and writes one row per frame for each from a ring drained at the frame
// boundary: the engine's state the fixture's model needs as input (position, basis rows, rates, speeds, the point the
// engine gave the leaf, the order, dt) and what the module handed back (the point, the written remainders); the next
// row of the same ship is the oracle. A slot is freed when the ship launches (P5), docks, or has not been seen for 5 s.
constexpr unsigned kTraceMax = 16, kTraceRing = 128;
constexpr std::uint32_t kTraceIdleMs = 5000;
bool trace_on_ = false;
unsigned trace_max_ = 6;
struct TraceSlot {
    std::uint32_t obj, last_seen_ms;
    std::int32_t W[3], rot_raw, rot_pct, vmax, accel; // the dynamics the leaf call read (the last known)
    std::int32_t point[3], given[3], e[3];           // this frame's leaf call: ours, the engine's, the written remainders
    std::uint32_t obstacle;                          // the current detour's obstacle at the last leaf call (0 = none)
    float omega_cmd[3];                              // the commanded rate per axis at the last leaf call, deg/s
    std::uint8_t leaf_this_frame, emergency;         // emergency: the avoidance braking ramp was active at the last leaf call
};
TraceSlot trace_slots_[kTraceMax];
struct TraceRow {
    std::uint32_t obj, order, tgt, dt_ms, t_ms, obstacle;
    std::uint16_t code, tgt_class;
    std::int32_t pos[3], rate[3], v, des, des_engine, point[3], given[3], e[3], W[3], rot_raw, rot_pct, vmax, accel;
    float fwd[3], up[3], omega_cmd[3];
    std::uint8_t has_e, emergency;
};
TraceRow trace_ring_[kTraceRing];
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
inline TraceSlot* trace_claim(std::uint32_t obj, std::uint32_t now, bool capital) noexcept {
    const unsigned first = capital ? 0u : 1u, end = capital ? 1u : (trace_max_ < kTraceMax ? trace_max_ : kTraceMax);
    for (unsigned i = first; i < end; ++i)
        if (!trace_slots_[i].obj) {
            trace_slots_[i] = TraceSlot{};
            trace_slots_[i].obj = obj;
            trace_slots_[i].last_seen_ms = now;
            return &trace_slots_[i];
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
    wk::fill_snapshot(mem, sector, now, s, part_cache_, stats, census);
    av_snapshots_.add();
    av_walk_objects_.add(stats.visited);
    if (s.overflow) av_overflow_.add();
    part_built_.add(stats.part_built);
    part_fallback_.add(stats.part_fallback);
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
// The avoidance adjustment of the order's point for one steering call (av::resolve over the sector snapshot).
void avoid_adjust(std::uint32_t obj, std::uint32_t fl, const core::Inputs& in, const core::Dynamics& dyn, std::uint32_t code,
                  unsigned slot, std::uint32_t now, float P[3], std::uint32_t* v_emergency_out) noexcept {
    const std::uint32_t sector = rd32(obj + sites::kObjSector);
    av_calls_.add();
    if (!sector) return;
    const av::Snapshot* snap = snapshot_for(sector, now);
    av::Self self;
    for (unsigned c = 0; c < 3; ++c) {
        self.x[c] = in.x[c];
        self.f[c] = in.row[2][c];
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
    self.leader_x[0] = self.leader_x[1] = self.leader_x[2] = 0.0f;
    self.L_leader = 0.0f;
    if (self.leader) {
        const std::uint32_t lph = rd32(self.leader + sites::kObjPhys);
        if (lph)
            for (unsigned c = 0; c < 3; ++c) self.leader_x[c] = float(rds32(lph + sites::kPhysPosition + 4 * c)) / core::kUnitsPerMetre;
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
        gate_rows_dropped_.add();
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
// ---- the steering decision -------------------------------------------
core::Reason decide(sites::Frame* f, unsigned leaf) noexcept {
    const std::uint32_t obj = f->obj;
    if (!obj || rd16(obj + sites::kObjClass) != sites::kClassShip) return core::Reason::not_ship;
    const std::uint32_t flags = rd32(obj + sites::kObjFlags);
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
    if (avoidance_on_ && policy.avoidance) {
        float P[3] = {in.P[0], in.P[1], in.P[2]};
        avoid_adjust(obj, fl, in, dyn, code, slot, now, P, &v_emergency_bits);
        if (P[0] != in.P[0] || P[1] != in.P[1] || P[2] != in.P[2]) {
            std::int32_t detour[3];
            const float dd[3] = {order_point[0] - in.x[0], order_point[1] - in.x[1], order_point[2] - in.x[2]};
            in.d_order = core::sqrt_ss(core::dot3(dd, dd)); // the hold and the ramp keep measuring the order's point
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
        if (gs->obj != obj || gs->gate != gate_tgt) { // another ship in the slot, or another gate: a new approach
            *gs = GateState{};
            gs->obj = obj;
            gs->gate = gate_tgt;
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
    // Amendment 10: a capital keeps every vanilla step on its gate legs (orders 4 and 5): no lead-in and no site-B
    // write (its arrival radius is clamp(r_own, 22200, 111000) units, which vanilla reaches with its 1 m/s alignment
    // creep inside 3 r; the write overrode that creep: Run154, the M1 leader 263 s on order 4). Its alignment at the
    // crossing is still measured for the `gate_capital_aligned` diagnostic.
    const bool capital_gate = capital && gph != 0;
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
            // diagnostic only (amendment 10): the alignment a capital would need for the rotate step, no steering change
            float c = core::dot3(in.row[2], g.h);
            if (c > 1.0f) c = 1.0f;
            if (c < -1.0f) c = -1.0f;
            gs->last_err_deg = core::acos_deg(c);
        }
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
            if (!v_emergency_bits && in.d_order < 0.0f) {
                if (core::gate_lead_in(g, in.x, in.v, lead, &s_line)) {
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
                } else
                    gate_lead_window_rejected_.add();
            }
        } else if (!capital)
            gs->last_err_deg = -1.0f;
    } else if (gph && order == core::kOrderDock && !capital) {
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
            if (w - gate_trans_read_ >= 8) gate_rows_dropped_.add();
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
    in.arrival_ramp = policy.arrival_ramp && !gate_approach;
    if (in.arrival_ramp) {
        const std::uint32_t tgt = rd32(fl + sites::kFlightTarget);
        if (order == core::kOrderFlyBefore) {
            std::int32_t units;
            const std::uint16_t tclass = tgt ? rd16(tgt + sites::kObjClass) : 0;
            if (tgt && tclass == sites::kClassShip)
                units = 111000;
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
            }
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
    s->order = order;
    // the gate speed policy: the non-capital approach (no ramp) and the capital lead-in (its ramp kept: the engine's
    // alignment rule, 1 m/s beyond 45.6 deg off A, would otherwise stall the lateral join); the order-5 leg
    s->flags = capital_gate ? std::uint16_t(0) // amendment 10: no site-B write on a capital's gate legs
                            : std::uint16_t((in.arrival_ramp ? core::kShipFlagRamp : 0) | (cmd.v_cmd > 0.0f ? core::kShipFlagClamp : 0) |
                                            (gate_lead ? (core::kShipFlagGate | core::kShipFlagClamp) : 0) |
                                            (gate_dock ? (core::kShipFlagGateDock | core::kShipFlagClamp) : 0));
    if (order == core::kOrderMovePos && s->order == order &&
        (s->P[0] != order_point[0] || s->P[1] != order_point[1] || s->P[2] != order_point[2]))
        s->best_d = -1.0f; // a new MovePos point: the stuck witness restarts (Run154: 8 false rows while d fell 34.5 -> 10.3 km)
    for (unsigned i = 0; i < 3; ++i) s->P[i] = order_point[i]; // site B measures the order's point, not a detour
    s->a_lin = dyn.a_lin;
    s->d_arr = gate_dock ? r_gate : in.d_arr; // order 5 to a gate: the engine's ramp runs over r_gate
    // the gate approach: site B's speed (never under an avoidance emergency: the emergency ramp is site B's value then);
    // order 5 to a gate: vmax for the ramp
    s->v_ref = gate_lead && !v_emergency_bits ? core::gate_speed(dyn.v_max, dyn.large, cmd.v_corner) : gate_dock ? dyn.v_max : in.v_ref;
    s->v_corner = cmd.v_corner;
    std::memcpy(&s->v_emergency, &v_emergency_bits, 4);
    s->calls = s->calls + 1;
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
            if (approach) ts = trace_claim(obj, now, capital);
        }
        if (ts) {
            ts->last_seen_ms = now;
            ts->leaf_this_frame = 1;
            ts->emergency = v_emergency_bits ? 1 : 0;
            ts->obstacle = avoid_[slot].obj == obj ? avoid_[slot].obstacle : 0;
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
// The ship_trace row of this frame for a followed ship, pushed at site B after the module's speed write (the row carries
// `des` as left by the write and `des_engine` as the engine set it), before the slew. An order outside 0 (the KC's
// hand-over), 4, 5 and 8 ends the approach and frees the slot.
inline void trace_frame(std::uint32_t obj, std::uint32_t fl, std::uint32_t ph, std::int32_t des_engine) noexcept {
    TraceSlot* ts = trace_slot_of(obj);
    if (!ts) return;
    const std::uint32_t now = game_time_ms();
    const std::uint16_t order_now = rd16(fl + sites::kFlightOrder);
    // The approach passes through order 0 (the arrival, and SA_StopObject after a collision warn), the engine's own avoid
    // order 0x12 (Run155: every trace gap before order 5 was a stop row followed by 70-900 frames on 0x12, the slot
    // released here and re-claimed only at order 5) and RotateToAngles 0x1f (the capital's alignment step).
    if (order_now != core::kOrderNone && order_now != core::kOrderFlyBefore && order_now != core::kOrderDock && order_now != core::kOrderWarpAway &&
        order_now != core::kOrderAvoid && order_now != core::kOrderRotate) {
        ts->obj = 0; // the approach is over (another order)
        return;
    }
    ts->last_seen_ms = now;
    const std::uint32_t w = trace_write_.load(std::memory_order_relaxed);
    if (w - trace_read_ >= kTraceRing) {
        trace_dropped_.add();
        ts->leaf_this_frame = 0;
        return;
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
}
}
namespace {
// Site B's write for one ship (the clamp from its last decision), split out so the trace row follows it.
inline void clamp_apply(std::uint32_t obj, std::uint32_t fl, std::uint32_t ph) noexcept {
    const core::Ship* s = table_.find(obj);
    if (!s) return;
    const std::uint16_t order = rd16(fl + sites::kFlightOrder);
    float x[3];
    for (unsigned i = 0; i < 3; ++i) x[i] = float(rds32(ph + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
    const float v = core::clamp_for(*s, x, order, game_time_ms(), sites::kClampMaxAgeMs);
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
    // Lowering only (compared in float before any conversion: core::clamp_units), never below the engine's own floor.
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
    if (trace_on_) trace_frame(obj, fl, ph, des_engine); // after the write: the row carries des as the module left it
}
// Step 3: the gate sites' handlers. Each runs on the light envelope inside the order handlers of the command pass
// (no log, no allocation, no lock); the stubs pass through to the claim tails when the group is not live.
namespace {
// The shared rule (core::gate_target_noncapital over the engine's memory): a class-7 non-capital ship with a gate target.
inline bool gate_target_noncapital(std::uint32_t obj) noexcept {
    RawMem mem;
    return core::gate_target_noncapital(mem, obj, rd32(sites::kShipTypeTableSlot));
}
inline GateState* gate_state_of(std::uint32_t obj) noexcept {
    const core::Ship* s = table_.find(obj);
    if (!s) return nullptr;
    GateState* g = &gate_[unsigned(s - table_.entries)];
    return g->obj == obj ? g : nullptr;
}
}
// P2 (0x00454b91): the order-4 arrival. [obj+0x14] = 0 always; [obj+0x10] = 0 unless a non-capital ship arrived at a
// gate's approach point (its speed carries into the KC's order 5).
extern "C" void __cdecl x3m_ship_gate_stop_a(std::uint32_t obj) {
    Envelope env;
    gate_p2_.add();
    wr32(obj + sites::kObjDesiredSpeed, 0);
    const bool live = installed_.load(std::memory_order_relaxed);
    GateState* g = live ? gate_state_of(obj) : nullptr;
    const std::uint32_t fl = rd32(obj + sites::kObjFlight), tgt = fl ? rd32(fl + sites::kFlightTarget) : 0;
    const bool gate_target = tgt && rd16(tgt + sites::kObjClass) == sites::kClassGate;
    // the crossing: the engine's arrival test fired for this ship (its own 0.75 r test runs before the frame's leaf call)
    if (g && g->validated && !g->capital && !g->lead_counted) { // per approach, non-capital: the lead-in verdict is final here
        g->lead_counted = 1;
        if (g->validated == 1) gate_lead_validated_.add();
        else gate_lead_fallback_.add();
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
    if (g && g->validated == 1 && !g->crossed) {
        g->crossed = 1;
        g->launched = 0;
        g->cross_stamp = game_time_ms();
        g->cross_v = float(rds32(obj + sites::kObjSpeed)) / core::kUnitsPerMetre;
        g->cross_err_deg = g->last_err_deg;
        gate_cross_.add();
        gate_spread_offsets_.add();
    }
    if (g && g->capital && !g->crossed) { // amendment 10: a capital's would-be alignment at its arrival (diagnostic only)
        g->crossed = 1;
        g->cross_stamp = game_time_ms();
        g->cross_v = float(rds32(obj + sites::kObjSpeed)) / core::kUnitsPerMetre;
        g->cross_err_deg = g->last_err_deg;
        if (g->cross_err_deg >= 0.0f && g->cross_err_deg <= core::kGateRotateTolDeg) gate_capital_aligned_.add();
        if (debug_) gate_event(*g, obj, 0, 0);
    }
    // fail closed: only a ship the policy steered (a gate state with the validated geometry) with a gate target and a
    // non-capital record keeps its speed; the reasons for the vanilla stop are counted
    const bool noncap = gate_target_noncapital(obj);
    if (live && core::gate_stop_keeps_speed(noncap, g != nullptr && g->validated == 1)) {
        gate_stops_skipped_.add();
        return;
    }
    if (!gate_target) gate_p2_no_gate_target_.add();
    else if (!noncap) gate_p2_capital_.add();
    else gate_p2_no_state_.add();
    wr32(obj + sites::kObjSpeed, 0);
}
// P4 (0x00454700): the order-5 arrival. [obj+0x14] = 0 always; for a non-capital ship at a gate's pass point the
// speed is kept and recorded for the launch (P5), else zeroed as the engine does.
extern "C" void __cdecl x3m_ship_gate_stop_b(std::uint32_t obj) {
    Envelope env;
    gate_p4_.add();
    wr32(obj + sites::kObjDesiredSpeed, 0);
    GateState* g = installed_.load(std::memory_order_relaxed) ? gate_state_of(obj) : nullptr;
    const bool noncap = gate_target_noncapital(obj);
    if (core::gate_stop_keeps_speed(noncap, g != nullptr && g->validated == 1)) { // the same per-approach verdict as P2
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
// Site E: 1 = suppress the reactive warn for (A, B) (B is in A's fresh resolved set and A is steered by us and
// not under the engine's own avoid order), 0 = the original warn test runs.
extern "C" int __cdecl x3m_ship_avoid_suppress(std::uint32_t a, std::uint32_t b) {
    if (!installed_.load(std::memory_order_relaxed) || !warn_live_.load(std::memory_order_relaxed)) return 0;
    Envelope env;
    e_calls_.add();
    if (!a || !b || rd16(a + sites::kObjClass) != sites::kClassShip) return 0;
    const std::uint32_t fl = rd32(a + sites::kObjFlight);
    if (!fl || rd16(fl + sites::kFlightOrder) == core::kOrderAvoid) return 0;
    const core::Ship* s = table_.find(a);
    if (!s) return 0;
    if (!av::suppressed(avoid_[unsigned(s - table_.entries)], a, b, game_time_ms())) return 0;
    e_suppressed_.add();
    return 1;
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
    LARGE_INTEGER f{};
    qpc_frequency_ = QueryPerformanceFrequency(&f) && f.QuadPart > 0 ? std::uint64_t(f.QuadPart) : 0;
    perf_ = log_tier::cached_perf;
    debug_ = log_tier::cached_debug;
    // Step 4 item 7: X3M_SHIP_TRACE (default off; --debug tier) and X3M_SHIP_TRACE_MAX (1..16, default 6)
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
                trace_max_ = 6u;
                log("ship_trace_refused reason=invalid_max");
            } else
                trace_max_ = unsigned(m);
        }
        if (trace_on_)
            log("ship_trace installed=1 max_ships=%u ring=%u idle_ms=%lu bytes_per_row_est=370", trace_max_, kTraceRing,
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
    if (!perf_ && !debug_) return;
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
                    "a_dps2=%.2f,%.2f,%.2f f2b0=0x%08lx rot_pct=%ld omega_cmd_dps=%.2f,%.2f,%.2f",
                    static_cast<unsigned long long>(frame), static_cast<unsigned long>(S.obj), static_cast<unsigned long>(S.order),
                    S.code < core::kClassCount ? core::kClassNames[S.code] : "?", static_cast<unsigned long>(S.leaf), S.held ? 1u : 0u,
                    double(S.theta_deg), double(S.n_y), double(S.n_p), double(S.omega_dps), long(S.e[0]), long(S.e[1]), long(S.e[2]),
                    long(S.w[0]), long(S.w[1]), long(S.w[2]), double(S.W_dps[0]), double(S.W_dps[1]), double(S.W_dps[2]), double(S.k),
                    double(S.d), double(S.d_arr), double(S.v), double(S.v_cmd), double(S.v_corner), double(S.v_ref), double(S.P[0]),
                    double(S.P[1]), double(S.P[2]), double(S.x[0]), double(S.x[1]), double(S.x[2]), static_cast<unsigned long>(S.flags),
                    double(S.a_dps[0]), double(S.a_dps[1]), double(S.a_dps[2]), static_cast<unsigned long>(S.rot_accel_raw), long(S.rot_pct),
                    double(S.omega_axis_dps[0]), double(S.omega_axis_dps[1]), double(S.omega_axis_dps[2]));
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
    if (trace_on_) {
        const std::uint32_t t_now = game_time_ms();
        for (unsigned i = 0; i < kTraceMax; ++i) // a followed ship not seen for kTraceIdleMs frees its slot (gone, or no longer steered)
            if (trace_slots_[i].obj && t_now - trace_slots_[i].last_seen_ms > kTraceIdleMs) trace_slots_[i].obj = 0;
        for (std::uint32_t w = trace_write_.load(std::memory_order_acquire); trace_read_ != w; ++trace_read_) {
            const TraceRow& r = trace_ring_[trace_read_ & (kTraceRing - 1)];
            trace_rows_.add();
            if (r.has_e)
                log("ship_trace frame=%llu t_ms=%lu obj=0x%08lx class=%s order=0x%lx tgt=0x%08lx tgt_class=%u pos=%ld,%ld,%ld "
                    "fwd=%.5f,%.5f,%.5f up=%.5f,%.5f,%.5f rate=%ld,%ld,%ld v=%ld des=%ld des_engine=%ld point=%ld,%ld,%ld given=%ld,%ld,%ld "
                    "e=%ld,%ld,%ld dt_ms=%lu W=%ld,%ld,%ld rot=0x%08lx rot_pct=%ld vmax=%ld acc=%ld emg=%u omega_cmd=%.2f,%.2f,%.2f obst=0x%08lx",
                    static_cast<unsigned long long>(frame), static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.obj),
                    r.code < core::kClassCount ? core::kClassNames[r.code] : "?", static_cast<unsigned long>(r.order),
                    static_cast<unsigned long>(r.tgt), unsigned(r.tgt_class), long(r.pos[0]), long(r.pos[1]), long(r.pos[2]), double(r.fwd[0]),
                    double(r.fwd[1]), double(r.fwd[2]), double(r.up[0]), double(r.up[1]), double(r.up[2]), long(r.rate[0]), long(r.rate[1]),
                    long(r.rate[2]), long(r.v), long(r.des), long(r.des_engine), long(r.point[0]), long(r.point[1]), long(r.point[2]), long(r.given[0]), long(r.given[1]),
                    long(r.given[2]), long(r.e[0]), long(r.e[1]), long(r.e[2]), static_cast<unsigned long>(r.dt_ms), long(r.W[0]), long(r.W[1]),
                    long(r.W[2]), static_cast<unsigned long>(r.rot_raw), long(r.rot_pct), long(r.vmax), long(r.accel), unsigned(r.emergency),
                    double(r.omega_cmd[0]), double(r.omega_cmd[1]), double(r.omega_cmd[2]), static_cast<unsigned long>(r.obstacle));
            else
                log("ship_trace frame=%llu t_ms=%lu obj=0x%08lx class=%s order=0x%lx tgt=0x%08lx tgt_class=%u pos=%ld,%ld,%ld "
                    "fwd=%.5f,%.5f,%.5f up=%.5f,%.5f,%.5f rate=%ld,%ld,%ld v=%ld des=%ld des_engine=%ld point=%ld,%ld,%ld given=%ld,%ld,%ld "
                    "e=- dt_ms=%lu W=%ld,%ld,%ld rot=0x%08lx rot_pct=%ld vmax=%ld acc=%ld emg=%u omega_cmd=%.2f,%.2f,%.2f obst=0x%08lx",
                    static_cast<unsigned long long>(frame), static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.obj),
                    r.code < core::kClassCount ? core::kClassNames[r.code] : "?", static_cast<unsigned long>(r.order),
                    static_cast<unsigned long>(r.tgt), unsigned(r.tgt_class), long(r.pos[0]), long(r.pos[1]), long(r.pos[2]), double(r.fwd[0]),
                    double(r.fwd[1]), double(r.fwd[2]), double(r.up[0]), double(r.up[1]), double(r.up[2]), long(r.rate[0]), long(r.rate[1]),
                    long(r.rate[2]), long(r.v), long(r.des), long(r.des_engine), long(r.point[0]), long(r.point[1]), long(r.point[2]), long(r.given[0]), long(r.given[1]),
                    long(r.given[2]), static_cast<unsigned long>(r.dt_ms), long(r.W[0]), long(r.W[1]), long(r.W[2]), static_cast<unsigned long>(r.rot_raw),
                    long(r.rot_pct), long(r.vmax), long(r.accel), unsigned(r.emergency), double(r.omega_cmd[0]), double(r.omega_cmd[1]),
                    double(r.omega_cmd[2]), static_cast<unsigned long>(r.obstacle));
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
    char avoidance[640] = "";
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
        std::snprintf(avoidance, sizeof avoidance,
                      " avoid_calls=%lu conflicts=%lu conflict_episodes=%lu detours=%lu unresolved=%lu unresolved_episodes=%lu "
                      "unresolved_inside_stop=%lu unresolved_inside_turn=%lu unresolved_blocked=%lu unresolved_behind=%lu unresolved_degenerate=%lu "
                      "snapshot_overflow=%lu snapshots=%lu walked=%lu emergency=%lu e_calls=%lu e_suppressed=%lu part_models=%lu "
                      "part_fallback=%lu part_invalid=%lu part_unfilled=%lu part_scaled=%lu snapshot_max=%u warn=%s",
                      static_cast<unsigned long>(d[0]), static_cast<unsigned long>(d[1]), static_cast<unsigned long>(e[0]),
                      static_cast<unsigned long>(d[2]), static_cast<unsigned long>(d[3]), static_cast<unsigned long>(e[1]),
                      static_cast<unsigned long>(why[1]), static_cast<unsigned long>(why[2]), static_cast<unsigned long>(why[3]),
                      static_cast<unsigned long>(why[4]), static_cast<unsigned long>(why[5]), static_cast<unsigned long>(d[4]),
                      static_cast<unsigned long>(d[5]), static_cast<unsigned long>(d[6]), static_cast<unsigned long>(d[7]),
                      static_cast<unsigned long>(d[8]), static_cast<unsigned long>(d[9]), static_cast<unsigned long>(d[10]),
                      static_cast<unsigned long>(d[11]), static_cast<unsigned long>(d[12]), static_cast<unsigned long>(e[2]),
                      static_cast<unsigned long>(e[3]), snap_max, warn_state_);
    }
    char gates[1200] = "";
    if (gates_on_) {
        const Counter* gc[29] = {&gate_lead_, &gate_cross_, &gate_p2_, &gate_stops_skipped_, &gate_p4_, &gate_v0_kept_, &gate_p5_,
                                 &gate_launch_continued_, &gate_speed_writes_, &gate_spread_offsets_, &gate_exit_offsets_,
                                 &gate_capital_aligned_, &gate_warp_calls_, &gate_lead_validated_, &gate_lead_fallback_,
                                 &gate_lead_window_rejected_, &gate_dock_writes_, &gate_dock_blocked_, &gate_p2_no_gate_target_,
                                 &gate_p2_capital_, &gate_p2_no_state_, &gate_p4_no_gate_target_, &gate_p4_capital_, &gate_p4_no_state_,
                                 &gate_dock_invalid_, &gate_warp_skipped_, &gate_rows_dropped_, &avoid_excluded_followers_,
                                 &avoid_skipped_small_};
        std::uint32_t d[29];
        for (unsigned i = 0; i < 29; ++i) {
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
                      "gate_dock_invalid=%lu gate_warp_skipped=%lu gate_rows_dropped=%lu avoid_excluded_followers=%lu avoid_skipped_small=%lu",
                      gates_state_, static_cast<unsigned long>(d[0]), static_cast<unsigned long>(d[1]), static_cast<unsigned long>(d[2]),
                      static_cast<unsigned long>(d[3]), static_cast<unsigned long>(d[4]), static_cast<unsigned long>(d[5]),
                      static_cast<unsigned long>(d[6]), static_cast<unsigned long>(d[7]), static_cast<unsigned long>(d[8]),
                      static_cast<unsigned long>(d[9]), static_cast<unsigned long>(d[10]), static_cast<unsigned long>(d[11]),
                      static_cast<unsigned long>(d[12]), static_cast<unsigned long>(d[13]), static_cast<unsigned long>(d[14]),
                      static_cast<unsigned long>(d[15]), static_cast<unsigned long>(d[16]), static_cast<unsigned long>(d[17]),
                      static_cast<unsigned long>(d[18]), static_cast<unsigned long>(d[19]), static_cast<unsigned long>(d[20]),
                      static_cast<unsigned long>(d[21]), static_cast<unsigned long>(d[22]), static_cast<unsigned long>(d[23]),
                      static_cast<unsigned long>(d[24]), static_cast<unsigned long>(d[25]), static_cast<unsigned long>(d[26]),
                      static_cast<unsigned long>(d[27]), static_cast<unsigned long>(d[28]));
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
        "handler_us_p50=%.2f handler_us_p99=%.2f handler_us_max=%.2f handled_by_order_total=%s stuck_total=%lu%s%s%s",
        static_cast<unsigned long long>(frame), interval, unsigned(sites::Count), static_cast<unsigned long>(d_calls),
        static_cast<unsigned long>(handled - rep_handled_), ships, per_ship, static_cast<unsigned long>(l0 - rep_leaf_[0]),
        static_cast<unsigned long>(l1 - rep_leaf_[1]), static_cast<unsigned long>(l2 - rep_leaf_[2]), reasons,
        static_cast<unsigned long>(ccalls - rep_clamp_calls_), static_cast<unsigned long>(capplied - rep_clamp_applied_),
        static_cast<unsigned long>(cstale - rep_clamp_stale_), static_cast<unsigned long>(redirected - rep_redirected_),
        static_cast<unsigned long>(total), clamped(0.5), clamped(0.99), us_max, orders,
        static_cast<unsigned long>(stuck_count_.load(std::memory_order_relaxed)), avoidance, gates, trace);
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
    if (!any) return true;
    const DWORD error = GetLastError();
    installed_.store(false, std::memory_order_release); // the handlers pass through from here on
    warn_live_.store(false, std::memory_order_release);
    gates_live_.store(false, std::memory_order_release);
    x3m_ship_gates_live = 0; // the gate stubs pass through to their tails from here on
    bool restored = true;
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
