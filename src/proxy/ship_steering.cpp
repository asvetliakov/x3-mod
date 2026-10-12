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
    std::uint32_t obj, obstacle, kind, side, vertical, order;
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
Counter e_calls_, e_suppressed_, part_built_, part_fallback_, part_invalid_;
std::atomic<std::uint32_t> qpc_hist_[32];
std::atomic<std::uint32_t> qpc_max_{0};
// Reported totals (frame boundary only).
std::uint32_t rep_calls_ = 0, rep_handled_ = 0, rep_reasons_[core::kReasonCount] = {}, rep_clamp_calls_ = 0,
              rep_clamp_applied_ = 0, rep_clamp_stale_ = 0, rep_leaf_[3] = {}, rep_redirected_ = 0;
std::uint32_t rep_av_[13] = {};
unsigned long long last_detour_tick_ = 0;
unsigned detour_rows_this_second_ = 0;
unsigned long long last_summary_tick_ = 0, last_sample_tick_ = 0;
std::uint32_t last_logged_seq_ = 0; // the sample row is logged once per written sample
// The sampled decision (--debug): a seqlock written by the handler when requested.
struct Sample {
    std::uint32_t obj, order, code, leaf, flags;
    float theta_deg, n_y, n_p, omega_dps, d, v, v_cmd, v_corner, d_arr, v_ref;
    std::int32_t e[3], w[3];
    float P[3], x[3], W_dps[3], k;
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
    self.envelope = float(rds32(fl + sites::kFlightDistance)) / core::kUnitsPerMetre * float(av::kFormationEnvelopeFactor);
    if (self.envelope < 0.0f) self.envelope = 0.0f;
    self.order = in.order;
    self.capital = code < core::kClassCount && ((core::kCapitalMask >> code) & 1u);
    self.exclude_target = in.order == core::kOrderFlyBefore || in.order == core::kOrderDock || in.order == core::kOrderEscort ||
                          in.order == core::kOrderFollow || in.order == core::kOrderAttack || in.order == core::kOrderAttackBig ||
                          in.order == core::kOrderCapture;
    av::ShipAvoid& state = avoid_[slot];
    if (state.obj != obj) state = av::ShipAvoid{}; // the slot was another ship's: no stale sides or resolved set
    state.obj = obj;
    const av::Result r = av::resolve(*snap, part_cache_.sets, part_cache_.count, self, P, state, now);
    if (r.conflicts) av_conflicts_.add(r.conflicts);
    if (r.unresolved) av_unresolved_.add();
    if (r.emergency) av_emergency_.add();
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
    // Arrival parameters (section 2.5).
    in.arrival_ramp = policy.arrival_ramp;
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
    s->flags = std::uint16_t((in.arrival_ramp ? 1 : 0) | (cmd.v_cmd > 0.0f ? 2 : 0));
    for (unsigned i = 0; i < 3; ++i) s->P[i] = order_point[i]; // site B measures the order's point, not a detour
    s->a_lin = dyn.a_lin;
    s->d_arr = in.d_arr;
    s->v_ref = in.v_ref;
    s->v_corner = cmd.v_corner;
    std::memcpy(&s->v_emergency, &v_emergency_bits, 4);
    s->calls = s->calls + 1;
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
extern "C" void __cdecl x3m_ship_steering_clamp(std::uint32_t obj) {
    if (!installed_.load(std::memory_order_relaxed)) return;
    Envelope env;
    clamp_calls_.add();
    const std::uint32_t flags = rd32(obj + sites::kObjFlags);
    if ((flags & (sites::kFlagDeleted | sites::kFlagAttached)) || rd32(obj + sites::kObjDocked)) return;
    const std::uint32_t fl = rd32(obj + sites::kObjFlight), ph = rd32(obj + sites::kObjPhys);
    if (!fl || !ph) return;
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
    // Lowering only (compared in float before any conversion: core::clamp_units), never below the engine's own floor.
    const std::int32_t units = core::clamp_units(v, rds32(obj + sites::kObjDesiredSpeed));
    if (units > 0) {
        wr32(obj + sites::kObjDesiredSpeed, units);
        clamp_applied_.add();
    }
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
    LARGE_INTEGER f{};
    qpc_frequency_ = QueryPerformanceFrequency(&f) && f.QuadPart > 0 ? std::uint64_t(f.QuadPart) : 0;
    perf_ = log_tier::cached_perf;
    debug_ = log_tier::cached_debug;
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
                    "v_ms=%.1f v_cmd_ms=%.1f v_corner_ms=%.1f v_ref_ms=%.1f P_m=%.1f,%.1f,%.1f x_m=%.1f,%.1f,%.1f flags=0x%08lx",
                    static_cast<unsigned long long>(frame), static_cast<unsigned long>(S.obj), static_cast<unsigned long>(S.order),
                    S.code < core::kClassCount ? core::kClassNames[S.code] : "?", static_cast<unsigned long>(S.leaf), S.held ? 1u : 0u,
                    double(S.theta_deg), double(S.n_y), double(S.n_p), double(S.omega_dps), long(S.e[0]), long(S.e[1]), long(S.e[2]),
                    long(S.w[0]), long(S.w[1]), long(S.w[2]), double(S.W_dps[0]), double(S.W_dps[1]), double(S.W_dps[2]), double(S.k),
                    double(S.d), double(S.d_arr), double(S.v), double(S.v_cmd), double(S.v_corner), double(S.v_ref), double(S.P[0]),
                    double(S.P[1]), double(S.P[2]), double(S.x[0]), double(S.x[1]), double(S.x[2]), static_cast<unsigned long>(S.flags));
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
                log("ship_avoid_station frame=%llu sector=0x%08lx obj=0x%08lx root_r_m=%.1f scale=0x%08lx children=%lu model=%s "
                    "parts=%ld,%ld,%ld:%ld:%08lx|%ld,%ld,%ld:%ld:%08lx|%ld,%ld,%ld:%ld:%08lx|%ld,%ld,%ld:%ld:%08lx",
                    static_cast<unsigned long long>(frame), static_cast<unsigned long>(c.sector), static_cast<unsigned long>(c.station), double(root_r),
                    static_cast<unsigned long>(c.station_scale), static_cast<unsigned long>(c.station_children), c.station_valid ? "parts" : "fallback",
                    long(c.part_offset[0][0]), long(c.part_offset[0][1]), long(c.part_offset[0][2]), long(c.part_r[0]), static_cast<unsigned long>(c.part_flags[0]),
                    long(c.part_offset[1][0]), long(c.part_offset[1][1]), long(c.part_offset[1][2]), long(c.part_r[1]), static_cast<unsigned long>(c.part_flags[1]),
                    long(c.part_offset[2][0]), long(c.part_offset[2][1]), long(c.part_offset[2][2]), long(c.part_r[2]), static_cast<unsigned long>(c.part_flags[2]),
                    long(c.part_offset[3][0]), long(c.part_offset[3][1]), long(c.part_offset[3][2]), long(c.part_r[3]), static_cast<unsigned long>(c.part_flags[3]));
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
                "C_m=%.1f L_m=%.0f P_m=%.1f,%.1f,%.1f x_m=%.1f,%.1f,%.1f",
                static_cast<unsigned long long>(frame), static_cast<unsigned long>(e.obj), static_cast<unsigned long>(e.order),
                static_cast<unsigned long>(e.obstacle), static_cast<unsigned long>(e.kind), long(std::int32_t(e.side)),
                static_cast<unsigned long>(e.vertical), double(e.t_cpa), double(e.miss), double(e.C), double(e.L), double(e.P[0]), double(e.P[1]),
                double(e.P[2]), double(e.x[0]), double(e.x[1]), double(e.x[2]));
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
    char avoidance[400] = "";
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
        std::snprintf(avoidance, sizeof avoidance,
                      " avoid_calls=%lu conflicts=%lu detours=%lu unresolved=%lu snapshot_overflow=%lu snapshots=%lu walked=%lu "
                      "emergency=%lu e_calls=%lu e_suppressed=%lu part_models=%lu part_fallback=%lu part_invalid=%lu snapshot_max=%u warn=%s",
                      static_cast<unsigned long>(d[0]), static_cast<unsigned long>(d[1]), static_cast<unsigned long>(d[2]),
                      static_cast<unsigned long>(d[3]), static_cast<unsigned long>(d[4]), static_cast<unsigned long>(d[5]),
                      static_cast<unsigned long>(d[6]), static_cast<unsigned long>(d[7]), static_cast<unsigned long>(d[8]),
                      static_cast<unsigned long>(d[9]), static_cast<unsigned long>(d[10]), static_cast<unsigned long>(d[11]),
                      static_cast<unsigned long>(d[12]), snap_max, warn_state_);
    }
    log("ship_steering_summary frame=%llu interval_ms=%llu claims=%u calls=%lu handled=%lu ships=%u calls_per_ship_s=%.1f "
        "leaf_calls=%lu,%lu,%lu%s clamp_calls=%lu clamp_applied=%lu clamp_stale=%lu redirected=%lu timed=%lu "
        "handler_us_p50=%.2f handler_us_p99=%.2f handler_us_max=%.2f handled_by_order_total=%s stuck_total=%lu%s",
        static_cast<unsigned long long>(frame), interval, unsigned(sites::Count), static_cast<unsigned long>(d_calls),
        static_cast<unsigned long>(handled - rep_handled_), ships, per_ship, static_cast<unsigned long>(l0 - rep_leaf_[0]),
        static_cast<unsigned long>(l1 - rep_leaf_[1]), static_cast<unsigned long>(l2 - rep_leaf_[2]), reasons,
        static_cast<unsigned long>(ccalls - rep_clamp_calls_), static_cast<unsigned long>(capplied - rep_clamp_applied_),
        static_cast<unsigned long>(cstale - rep_clamp_stale_), static_cast<unsigned long>(redirected - rep_redirected_),
        static_cast<unsigned long>(total), percentile(0.5), percentile(0.99), us_max, orders,
        static_cast<unsigned long>(stuck_count_.load(std::memory_order_relaxed)), avoidance);
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
    if (!any) return true;
    const DWORD error = GetLastError();
    installed_.store(false, std::memory_order_release); // the handlers pass through from here on
    warn_live_.store(false, std::memory_order_release);
    bool restored = true;
    if (warn_site_.patched_in && !engine_patch::restore(warn_site_)) restored = false;
    for (unsigned i = sites::Count; i-- > 0;)
        if (patches_[i].patched_in && !engine_patch::restore(patches_[i])) restored = false;
    state_ = restored ? "restored" : "restore_failed_retry_at_next_shutdown";
    SetLastError(error);
    return restored;
}
}
