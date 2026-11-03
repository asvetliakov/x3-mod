#pragma once
// Internal to the FL base ship steering and avoidance (src/engine/fl/ship_steering_fl.cpp, ship_avoidance_fl.cpp; the ap target
// keeps src/proxy/ship_steering_*.cpp): the module state and the inline helpers both units share. The AP glue's base path is
// transcribed onto the FL bindings (ship_steering_sites_fl.h, ship_avoidance_sites_fl.h): the decision core::steer, the clamp
// core::clamp_for / clamp_units, the snapshot walk wk::fill_snapshot, the target adjustment av::resolve and the E verdict
// av::site_e_verdict are the shared AP cores, called with the same inputs (the native fixtures compare every output with the AP
// glue run on the same objects: docs/reverse-engineering/fl/ship-steering.md, "Port (2026-11-03)"). The gate, capital,
// traffic, lane and dock groups are not ported (one ap_only row each at initialize).
// FL differences, each measured by verification/probe/fl_ship_steering.cpp / fl_ship_avoidance.cpp:
//  * the getters take obj in ECX (call_ecx for all five); the clock is [base+0x1a83b0] +0x708 / +0x704; the TShips slot is
//    base+0x1a83e4 (the snapshot reader's ship_table());
//  * the B clamp's decision age, the port hold's gap and the leader's lookahead age scale with the game-time frame step
//    (max(250 ms, 4 steps), av::fresh_window_ms; the user flies with SETA): AP keeps the fixed 250 ms;
//  * a recycled object pointer (another native id obj+0x08) starts a new ship: its steering and avoidance state are reset at A,
//    B does not clamp from the previous object's decision and E does not answer from it; a resolved B whose id changed since
//    the resolve is not suppressed (ship-avoidance.md section 4).
// Hot path: no log call, no allocation, no lock; counters are relaxed stores from the one writing thread; the frame boundary
// formats. Every engine read is a plain load of an address the native code itself reads in the same pass.
#include "../../proxy/ship_steering.h"
#include "../../proxy/ship_steering_core.h"
#include "../../proxy/ship_avoidance_core.h"
#include "../../proxy/ship_avoidance_walk_core.h"
#include "../../proxy/cpu_state.h"
#include "ship_steering_sites_fl.h"
#include "ship_avoidance_sites_fl.h"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace x3m {
void log(const char* format, ...);
}
extern "C" {
// Continuation slots (the claim tails, jmp [slot] in the stubs), written before the stub is the chain head.
extern void* x3m_fl_ship_steering_next[4];
extern void* x3m_fl_ship_avoid_next_e;
void x3m_fl_ship_steering_a0_stub();
void x3m_fl_ship_steering_a1_stub();
void x3m_fl_ship_steering_a2_stub();
void x3m_fl_ship_steering_b_stub();
void x3m_fl_ship_avoid_e_stub();
int __cdecl x3m_fl_ship_steering_enter(x3m::fl::ship_steering_fl::Frame* frame, int leaf);
void __cdecl x3m_fl_ship_steering_clamp(std::uint32_t obj);
int __cdecl x3m_fl_ship_avoid_suppress(std::uint32_t a, std::uint32_t b);
}

namespace x3m_fl_ship_steering_rt {
namespace core = x3m::ship_steering::core;
namespace av = x3m::ship_avoidance::core;
namespace wk = x3m::ship_avoidance::walk;
namespace F = x3m::fl::ship_steering_fl;
namespace FA = x3m::fl::ship_avoidance_fl;

// ---- module state -------------------------------------------------------
inline std::atomic<bool> installed_{false};
inline bool initialized_ = false, timing_ = false, debug_ = false, perf_ = false;
inline std::uintptr_t base_ = 0;
inline std::uintptr_t table_slot_ = 0, clock_slot_ = 0; // base + RVA, bound at install
inline std::uintptr_t get_max_speed_ = 0, get_accel_ = 0, get_pitch_cap_ = 0, get_yaw_cap_ = 0, get_roll_cap_ = 0;
inline std::uint32_t class_mask_ = 0;
inline std::uint64_t qpc_frequency_ = 0;
inline core::ShipTable table_;
inline std::uint32_t table_id_[core::kShipCapacity]; // per table slot, [obj+0x08] of the slot's object (a recycled pointer is a new ship)
inline bool avoidance_on_ = false;
inline std::atomic<bool> warn_live_{false};
inline const char* avoid_state_ = "off";
inline const char* warn_state_ = "not_claimed";
inline av::Snapshot snaps_[FA::snapshot_slots];
inline wk::PartCache part_cache_;
inline av::ShipAvoid avoid_[core::kShipCapacity];
inline std::uint32_t resolved_id_[core::kShipCapacity][av::kMaxResolved]; // [obj+0x08] of each resolved B at its resolve
inline av::Members members_;
inline av::MemberStats member_stats_;
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
inline unsigned census_logged_count_ = 0;
struct Counter {
    std::atomic<std::uint32_t> v{0};
    void add(std::uint32_t n = 1) noexcept { v.store(v.load(std::memory_order_relaxed) + n, std::memory_order_relaxed); }
    std::uint32_t get() const noexcept { return v.load(std::memory_order_relaxed); }
};
inline Counter calls_, handled_, reasons_[core::kReasonCount], leaf_calls_[3], clamp_calls_, clamp_applied_, clamp_stale_;
inline Counter handled_by_order_[0x40], redirected_, recycled_;
inline Counter av_calls_, av_conflicts_, av_detours_, av_unresolved_, av_overflow_, av_snapshots_, av_walk_objects_, av_emergency_;
inline Counter av_unresolved_why_[unsigned(av::Unresolved::count)], av_unresolved_episodes_, av_conflict_episodes_, part_dirty_, part_scaled_;
inline Counter e_calls_, e_suppressed_, part_built_, part_fallback_, part_invalid_, part_pinned_full_;
inline Counter e_suppressed_small_, e_small_pass_, e_small_pass_reason_[unsigned(av::SmallPair::count)];
inline Counter e_suppressed_member_, e_suppressed_gate_, e_suppressed_gate_small_, avoid_excluded_member_, e_small_gap_;
inline Counter avoid_excluded_followers_, avoid_skipped_small_, e_recycled_a_, e_resolved_id_mismatch_;
inline std::atomic<std::uint32_t> qpc_hist_[32];
inline std::atomic<std::uint32_t> qpc_max_{0};
struct Sample { // the sampled decision (--debug): a seqlock written by the handler when requested
    std::uint32_t obj, order, code, leaf, flags;
    float theta_deg, n_y, n_p, omega_dps, d, v, v_cmd, v_corner, d_arr, v_ref;
    float lag_deg, aim_cos, v_slip, slip[3];
    std::int32_t e[3], w[3], rot_accel_raw, rot_pct;
    float P[3], x[3], W_dps[3], k, a_dps[3], omega_axis_dps[3];
    bool held;
};
inline Sample sample_;
inline std::atomic<std::uint32_t> sample_seq_{0};
inline std::atomic<bool> sample_request_{false};
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
    const std::uint32_t clock = rd32(clock_slot_);
    return clock ? rd32(clock + F::clock_game_time) : 0;
}
inline std::uint32_t frame_step_ms() noexcept { // the frame's game-time step [clock+0x704], clamped to 5 s (av::frame_step_clamp)
    const std::uint32_t clock = rd32(clock_slot_);
    return av::frame_step_clamp(clock ? rd32(clock + F::clock_frame_step) : 0);
}
// A decision-age bound scaled with the game-time frame step: max(base, 4 steps), the step capped at 250 ms (av::fresh_window_ms).
inline std::uint32_t step_window_ms(std::uint32_t base_ms) noexcept {
    return av::fresh_window_ms(base_ms, frame_step_ms());
}
// The five FL getters: obj in ECX, integer EAX, ECX/EDX/flags clobbered (ship_steering_sites_fl.h).
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
// The light envelope (LastError + MXCSR of the engine, restored on return) with the default MXCSR control bits for the SSE
// arithmetic; the stubs saved EFLAGS, EAX/ECX/EDX, XMM0-7, the x87 image (FNSAVE) and MXCSR before the call.
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
// The plant of a ship from its FL getters and its TShips record (the AP read_dynamics with every getter on ECX).
inline void read_dynamics(std::uint32_t obj, std::uint32_t fl, std::int32_t rot_pct, std::uint32_t code, core::Dynamics& dyn) noexcept {
    dyn.W[0] = float(call_ecx(get_yaw_cap_, obj)) * core::kRadPerBinary;
    dyn.W[1] = float(call_ecx(get_pitch_cap_, obj)) * core::kRadPerBinary;
    dyn.W[2] = float(call_ecx(get_roll_cap_, obj)) * core::kRadPerBinary;
    for (float& w : dyn.W)
        if (w < 0.0f) w = 0.0f;
    dyn.k = fl && rot_pct > 0 ? fixed16(rds32(fl + F::flight_rot_accel)) * float(rot_pct) * 0.01f : 0.0f;
    dyn.v_max = float(call_ecx(get_max_speed_, obj)) / core::kUnitsPerMetre;
    dyn.a_lin = float(call_ecx(get_accel_, obj)) / core::kUnitsPerMetre;
    if (dyn.a_lin < 0.0f) dyn.a_lin = 0.0f;
    dyn.r_own = float(rds32(obj + F::obj_radius)) / core::kUnitsPerMetre;
    dyn.large = code < core::kClassCount && ((core::kLargeMask >> code) & 1u);
}
// The snapshot reader: plain loads, and the FL TShips slot through ship_table() (ship_avoidance_walk_core.h ship_table_of).
struct RawMem {
    std::uint32_t u32(std::uint32_t at) const noexcept { return rd32(at); }
    std::int32_t s32(std::uint32_t at) const noexcept { return rds32(at); }
    std::uint16_t u16(std::uint32_t at) const noexcept { return rd16(at); }
    std::uint32_t ship_table() const noexcept { return rd32(table_slot_); }
};
static_assert(F::large_mask == core::kLargeMask && F::capital_mask == core::kCapitalMask, "the FL predicates' masks are the core's");
static_assert(F::obj_slip == wk::kObjSlip && F::obj_phys == wk::kObjPhys && F::obj_radius == wk::kObjRadius &&
                  F::obj_flight == wk::kObjFlight && F::flight_target == wk::kFlightTarget && F::flight_order == wk::kFlightOrder &&
                  F::obj_id == wk::kObjId && F::record_stride == wk::kRecordStride && F::record_class_code == wk::kRecordClassCode,
              "the walk core's field offsets are the FL bindings");

// ---- shared between the two units ----
void avoid_adjust(std::uint32_t obj, const core::Inputs& in, const core::Dynamics& dyn, std::uint32_t code, unsigned slot,
                  std::uint32_t now, float P[3], std::uint32_t* v_emergency_out) noexcept;
// Avoidance install: the part-2 windows (the snapshot) and site E (part 3), inside the install window; logs its rows.
void avoidance_initialize() noexcept;
bool avoidance_shutdown() noexcept;
void avoidance_rows(unsigned long long frame) noexcept;
void avoidance_summary(char* out, unsigned size) noexcept;
} // namespace x3m_fl_ship_steering_rt
