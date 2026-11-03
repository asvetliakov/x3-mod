// FL edition of x3m::ship_steering (src/proxy/ship_steering.h; the ap target keeps src/proxy/ship_steering_*.cpp): the base
// steering X3M_SHIP_STEERING (+ X3M_SHIP_STEERING_CLASSES) on X3FL.exe, and the entry of X3M_SHIP_AVOIDANCE (its handlers:
// ship_avoidance_fl.cpp). Facts: docs/reverse-engineering/fl/ship-steering.md and ship-avoidance.md, "Port (2026-11-03)";
// sites, windows and bindings: ship_steering_sites_fl.h; the decision, the clamp and every policy constant are the AP cores
// (src/proxy/ship_steering_core.h), the glue is the AP base path on the FL bindings (ship_steering_fl_internal.h lists the
// FL differences). The gate, capital, traffic, lane and dock groups are AP only: one ap_only row each, nothing claimed.
//
// Install (backend-load path, inside the engine_patch install window; later: late_claim): X3FL.exe identity
// (fl::module_base() != 0), the 24 steering windows at base + RVA with their fixups bound to base, before any write; the four
// claims in table order (each read back: this module's jump at the site, written through lock cmpxchg8b as its qword geometry
// predicts), every continuation slot set before its stub becomes the chain head; a failure rolls every claimed site back in
// reverse (a restore that cannot complete keeps the site chained, its stub passing through while installed_ is false). The
// stubs are file-scope asm in this DLL, so the module is pinned. The FL loader has no unload path: shutdown() is for the
// fixture.
//
// Stubs: PUSHFD, EAX/ECX/EDX, XMM0..7 (all 128 bits), FNSAVE (the whole x87 image; the handler runs on a reset FPU), STMXCSR,
// CLD; the handler (C, LightCallBoundary: LastError and MXCSR, MXCSR 0x1f80 inside); then LDMXCSR, FRSTOR, XMM0..7, EDX/ECX/EAX
// and POPFD, so every GPR, EFLAGS (DF included), XMM, the x87 state and MXCSR leave as they entered. A: handled -> `ret 4`
// (obj+0x30/+0x34/+0x38 written), refused -> the claim tail (the stack argument possibly redirected to the target ring). B: the
// class-7 + autopilot filter under PUSHFD/POPFD before the envelope (three instructions for every other object), then the clamp
// and the claim tail.
#include "../../proxy/config.h"
#include "../../proxy/engine_patch.h"
#include "../../proxy/log_tiers.h"
#include "executable_identity_fl.h"
#include "ship_steering_fl_internal.h"
#include <cstdio>

using namespace x3m_fl_ship_steering_rt;
namespace engine_patch = x3m::engine_patch;

extern "C" {
void* x3m_fl_ship_steering_next[4] = {};
}

asm(R"(
    .intel_syntax noprefix
    .text
    .macro X3M_FL_SS_SAVE
    push eax
    push ecx
    push edx
    sub esp, 0xf0
    movdqu [esp+0x00], xmm0
    movdqu [esp+0x10], xmm1
    movdqu [esp+0x20], xmm2
    movdqu [esp+0x30], xmm3
    movdqu [esp+0x40], xmm4
    movdqu [esp+0x50], xmm5
    movdqu [esp+0x60], xmm6
    movdqu [esp+0x70], xmm7
    fnsave [esp+0x80]
    stmxcsr [esp+0xec]
    cld
    .endm
    .macro X3M_FL_SS_RESTORE
    ldmxcsr [esp+0xec]
    frstor [esp+0x80]
    movdqu xmm0, [esp+0x00]
    movdqu xmm1, [esp+0x10]
    movdqu xmm2, [esp+0x20]
    movdqu xmm3, [esp+0x30]
    movdqu xmm4, [esp+0x40]
    movdqu xmm5, [esp+0x50]
    movdqu xmm6, [esp+0x60]
    movdqu xmm7, [esp+0x70]
    lea esp, [esp+0xf0]
    pop edx
    pop ecx
    pop eax
    popfd
    .endm
    .macro X3M_FL_SS_LEAF name, k
    .p2align 4
    .globl \name
\name:
    pushfd
    X3M_FL_SS_SAVE
    mov eax, esp
    push \k
    push eax
    call _x3m_fl_ship_steering_enter
    add esp, 8
    test eax, eax
    jnz 2f
    X3M_FL_SS_RESTORE
    jmp dword ptr [_x3m_fl_ship_steering_next + 4*\k]
2:  X3M_FL_SS_RESTORE
    ret 4
    .endm
    X3M_FL_SS_LEAF _x3m_fl_ship_steering_a0_stub, 0
    X3M_FL_SS_LEAF _x3m_fl_ship_steering_a1_stub, 1
    X3M_FL_SS_LEAF _x3m_fl_ship_steering_a2_stub, 2
    .p2align 4
    .globl _x3m_fl_ship_steering_b_stub
_x3m_fl_ship_steering_b_stub:
    pushfd
    cmp word ptr [ebx+0x48], 7
    jne 1f
    test dword ptr [ebx+0x40], 0x80000
    jz 1f
    X3M_FL_SS_SAVE
    push ebx
    call _x3m_fl_ship_steering_clamp
    add esp, 4
    X3M_FL_SS_RESTORE
    jmp dword ptr [_x3m_fl_ship_steering_next + 12]
1:  popfd
    jmp dword ptr [_x3m_fl_ship_steering_next + 12]
    .p2align 4
    .globl _x3m_fl_ship_avoid_e_stub
_x3m_fl_ship_avoid_e_stub:
    pushfd
    X3M_FL_SS_SAVE
    push dword ptr [esp+0x104]
    push dword ptr [esp+0xf8]
    call _x3m_fl_ship_avoid_suppress
    add esp, 8
    test eax, eax
    jnz 2f
    X3M_FL_SS_RESTORE
    jmp dword ptr [_x3m_fl_ship_avoid_next_e]
2:  X3M_FL_SS_RESTORE
    mov eax, 0
    ret 0xc
    .att_syntax
)");

namespace {
const char* state_ = "not_initialized";
engine_patch::Site patches_[F::count];
unsigned failed_window_ = F::window_count;
#ifdef X3M_SHIP_STEERING_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
unsigned fixture_fault_claim_ = ~0u; // one-shot: that claim's read-back sees a foreign byte
#endif
std::uintptr_t module_base() {
#ifdef X3M_SHIP_STEERING_FL_FIXTURE
    if (fixture_base_) return fixture_base_;
#endif
    return x3m::fl::module_base();
}
std::int32_t target_ring_[4][3]; // a redirected stack argument points here, never at the caller's buffer
unsigned target_depth_ = 0;
unsigned long long last_summary_tick_ = 0, last_sample_tick_ = 0;
std::uint32_t last_logged_seq_ = 0;
unsigned stuck_reported_ = 0;
std::uint32_t rep_calls_ = 0, rep_handled_ = 0, rep_reasons_[core::kReasonCount] = {}, rep_clamp_calls_ = 0, rep_clamp_applied_ = 0,
              rep_clamp_stale_ = 0, rep_leaf_[3] = {}, rep_redirected_ = 0;

inline void record_time(std::uint64_t ticks) noexcept {
    const std::uint32_t t = ticks > 0xffffffffull ? 0xffffffffu : std::uint32_t(ticks);
    const unsigned bucket = t ? 31u - unsigned(__builtin_clz(t)) : 0u;
    qpc_hist_[bucket].store(qpc_hist_[bucket].load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    if (t > qpc_max_.load(std::memory_order_relaxed)) qpc_max_.store(t, std::memory_order_relaxed);
}
// The step-2 seam: a changed point goes into the next slot of a 4-deep ring (DLL .data) and the leaf's stack argument is
// redirected to it, so the law and a pass-through leaf both see the adjusted point; the caller's buffer and f+0x240 stay unwritten.
inline void redirect_target(F::Frame* f, const std::int32_t point[3]) noexcept {
    std::int32_t* slot = target_ring_[target_depth_++ & 3u];
    slot[0] = point[0];
    slot[1] = point[1];
    slot[2] = point[2];
    f->target = std::uint32_t(reinterpret_cast<std::uintptr_t>(slot));
    redirected_.add();
}

// Site A: the AP decide()'s base path (no gate, traffic, lane, dock or trace branch) on the FL frame: ECX = obj, the target at
// the entry's stack argument.
core::Reason decide(F::Frame* f, unsigned leaf) noexcept {
    const std::uint32_t obj = f->ecx;
    if (!obj || rd16(obj + F::obj_class) != F::class_ship) return core::Reason::not_ship;
    const std::uint32_t flags = rd32(obj + F::obj_flags);
    if (!(flags & F::flag_autopilot)) return core::Reason::no_autopilot;
    if ((flags & (F::flag_deleted | F::flag_attached)) || rd32(obj + F::obj_docked)) return core::Reason::deleted;
    const std::uint32_t fl = rd32(obj + F::obj_flight);
    if (!fl) return core::Reason::no_flight;
    const std::uint32_t ph = rd32(obj + F::obj_phys);
    if (!ph) return core::Reason::no_phys;
    const std::uint16_t order = rd16(fl + F::flight_order);
    const core::OrderPolicy policy = core::order_policy(order);
    if (!policy.steered) return core::Reason::order_not_steered;
    const std::int32_t type = std::int16_t(rd16(obj + F::obj_type));
    const std::uint32_t table = rd32(table_slot_);
    if (!table || type < 0) return core::Reason::no_record;
    const std::uint32_t rec = table + std::uint32_t(type) * F::record_stride;
    const std::uint32_t code = rd32(rec + F::record_class_code);
    if (code < core::kClassCount && ((class_mask_ >> code) & 1u)) return core::Reason::class_excluded;
    const std::int32_t rot_pct = rds32(rec + F::record_rot_accel);
    if (rot_pct <= 0) return core::Reason::no_angular_accel;
    core::Dynamics dyn;
    read_dynamics(obj, fl, rot_pct, code, dyn);
    core::Inputs in;
    for (unsigned i = 0; i < 3; ++i) in.x[i] = float(rds32(ph + F::phys_position + 4 * i)) / core::kUnitsPerMetre;
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = 0; c < 3; ++c) in.row[r][c] = fixed16(rds32(fl + F::flight_basis + 0x10 * r + 4 * c));
    in.v = float(rds32(obj + F::obj_speed)) / core::kUnitsPerMetre;
    for (unsigned i = 0; i < 3; ++i) in.w[i] = float(rds32(obj + F::obj_rates + 4 * i)) * core::kRadPerBinary;
    for (unsigned i = 0; i < 3; ++i) in.slip[i] = float(rds32(obj + F::obj_slip + 4 * i)) / core::kUnitsPerMetre;
    if (order == core::kOrderDock || !core::slip_usable(in.slip, dyn.v_max)) in.slip[0] = in.slip[1] = in.slip[2] = 0.0f;
    // The target: the caller's buffer behind the stack argument, read only.
    const std::uint32_t target = f->target;
    if (!target) return core::Reason::not_finite;
    const std::int32_t point[3] = {rds32(target), rds32(target + 4), rds32(target + 8)};
    for (unsigned i = 0; i < 3; ++i) in.P[i] = float(point[i]) / core::kUnitsPerMetre;
    in.order = order;
    const std::uint32_t now = game_time_ms();
    core::Ship* s = table_.find_or_insert(obj, now);
    if (!s) return core::Reason::table_full;
    const unsigned slot = unsigned(s - table_.entries);
    // A recycled pointer (another native id at obj+0x08) is a new ship: nothing of the previous object's state steers it.
    const std::uint32_t id = rd32(obj + F::obj_id);
    if (table_id_[slot] != id) {
        if (s->stamp_ms || s->last_call_ms) {
            *s = core::Ship{};
            s->obj = obj;
            recycled_.add();
        }
        avoid_[slot] = av::ShipAvoid{};
        table_id_[slot] = id;
    }
    {
        const std::uint32_t clock = rd32(clock_slot_);
        std::uint32_t frame_ms = clock ? rd32(clock + F::clock_frame_step) : 0;
        if (frame_ms < 1 || frame_ms > 1000) frame_ms = 1;
        const std::uint32_t gap = s->last_call_ms && now >= s->last_call_ms ? now - s->last_call_ms : 50;
        in.tick_s = core::servo_tick(gap, frame_ms, s->per_frame_run);
    }
    std::uint32_t v_emergency_bits = 0;
    const float order_point[3] = {in.P[0], in.P[1], in.P[2]};
    if (avoidance_on_ && policy.avoidance) {
        float P[3] = {in.P[0], in.P[1], in.P[2]};
        avoid_adjust(obj, in, dyn, code, slot, now, P, &v_emergency_bits);
        if (P[0] != in.P[0] || P[1] != in.P[1] || P[2] != in.P[2]) {
            std::int32_t detour[3];
            const float dd[3] = {order_point[0] - in.x[0], order_point[1] - in.x[1], order_point[2] - in.x[2]};
            in.d_order = core::sqrt_ss(core::dot3(dd, dd));
            in.detour = true;
            for (unsigned i = 0; i < 3; ++i) {
                in.P[i] = P[i];
                detour[i] = core::round_to_int(P[i] * core::kUnitsPerMetre);
            }
            redirect_target(f, detour);
        }
    }
    // Arrival parameters (section 2.5).
    in.arrival_ramp = policy.arrival_ramp;
    if (in.arrival_ramp) {
        const std::uint32_t tgt = rd32(fl + F::flight_target);
        if (order == core::kOrderFlyBefore) {
            std::int32_t units;
            const std::uint16_t tclass = tgt ? rd16(tgt + F::obj_class) : 0;
            if (tgt && tclass == F::class_ship)
                units = 111000;
            else if (tgt && tclass == F::class_gate)
                units = rds32(tgt + F::obj_radius) / 4 * 3;
            else {
                units = (dyn.large ? 1 : 3) * rds32(obj + F::obj_radius);
                if (units < 22200) units = 22200;
                if (units > 111000) units = 111000;
            }
            in.d_arr = float(units) / core::kUnitsPerMetre;
        } else {
            in.d_arr = float(rds32(fl + F::flight_distance)) / core::kUnitsPerMetre;
            if (order == core::kOrderEscort) in.d_arr = sc_max(in.d_arr, dyn.r_own);
        }
        if (in.d_arr < 0.0f) in.d_arr = 0.0f;
        if ((order == core::kOrderEscort || order == core::kOrderFollow) && tgt && rd16(tgt + F::obj_class) == F::class_ship) {
            const std::uint32_t lf = rd32(tgt + F::obj_flight);
            if (lf) {
                float lead[3];
                for (unsigned c = 0; c < 3; ++c) lead[c] = fixed16(rds32(lf + F::flight_basis + 0x20 + 4 * c));
                const float v_lead = float(rds32(tgt + F::obj_speed)) / core::kUnitsPerMetre;
                const float along = core::dot3(lead, in.row[2]);
                in.v_ref = along > 0.0f ? v_lead * along : 0.0f;
                if (!(rd32(tgt + F::obj_flags) & F::flag_deleted)) {
                    in.formation = true;
                    float ls[3];
                    for (unsigned c = 0; c < 3; ++c) ls[c] = float(rds32(tgt + F::obj_slip + 4 * c)) / core::kUnitsPerMetre;
                    const bool ok = core::slip_usable(ls, dyn.v_max > v_lead ? dyn.v_max : v_lead);
                    for (unsigned c = 0; c < 3; ++c) {
                        in.nose_lead[c] = v_lead * lead[c];
                        in.u_lead[c] = in.nose_lead[c] + (ok ? ls[c] : 0.0f);
                    }
                }
            }
        }
    }
    // Amendment 12: order 5 to a port and the leg's hold from this ship's previous call (the gap scaled with the frame step).
    if (order == core::kOrderDock) {
        const std::uint32_t tg = rd32(fl + F::flight_target);
        in.port_leg = tg && rd16(tg + F::obj_class) != F::class_gate && rds32(fl + F::flight_port_index) >= 0;
        in.port_hold = in.port_leg && s->order == core::kOrderDock && (s->flags & core::kShipFlagPortHold) &&
                       now - s->stamp_ms <= step_window_ms(core::kPortHoldGapMs);
    }
    core::Command cmd;
    const core::Reason r = core::steer(in, dyn, &cmd);
    if (r != core::Reason::handled) return r;
    wr32(obj + F::obj_remaining, cmd.e[0]);
    wr32(obj + F::obj_remaining + 4, cmd.e[1]);
    wr32(obj + F::obj_remaining + 8, cmd.e[2]);
    s->stamp_ms = now;
    s->order = order;
    const std::uint16_t speed_flags =
        std::uint16_t((in.arrival_ramp && !cmd.formation_speed ? core::kShipFlagRamp : 0) | (cmd.v_cmd > 0.0f ? core::kShipFlagClamp : 0));
    s->flags = std::uint16_t(speed_flags | (cmd.port_aligned ? core::kShipFlagPortHold : 0));
    if (order == core::kOrderMovePos && (s->P[0] != order_point[0] || s->P[1] != order_point[1] || s->P[2] != order_point[2]))
        s->best_d = -1.0f; // a new MovePos point: the stuck witness restarts
    for (unsigned i = 0; i < 3; ++i) s->P[i] = order_point[i]; // site B measures the order's point, not a detour
    s->a_lin = dyn.a_lin;
    s->d_arr = in.d_arr;
    s->v_ref = in.v_ref;
    s->v_corner = cmd.formation_speed ? cmd.v_cmd : cmd.v_corner;
    s->v_slip = cmd.formation_speed ? 0.0f : cmd.v_slip;
    std::memcpy(&s->v_emergency, &v_emergency_bits, 4);
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
        s->best_d = -1.0f;
    handled_by_order_[order & 0x3f].add();
    if (sample_request_.load(std::memory_order_relaxed)) {
        sample_request_.store(false, std::memory_order_relaxed);
        const std::uint32_t seq = sample_seq_.load(std::memory_order_relaxed);
        sample_seq_.store(seq + 1, std::memory_order_release);
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
        S.lag_deg = core::lag_deg(cmd.u, in.row[2]);
        S.aim_cos = cmd.aim_cos;
        S.v_slip = cmd.v_slip;
        for (unsigned i = 0; i < 3; ++i) {
            S.slip[i] = in.slip[i];
            S.e[i] = cmd.e[i];
            S.w[i] = rds32(obj + F::obj_rates + 4 * i);
            S.P[i] = in.P[i];
            S.x[i] = in.x[i];
            S.W_dps[i] = dyn.W[i] * 180.0f / core::kPi;
            S.a_dps[i] = dyn.W[i] * dyn.k * 180.0f / core::kPi;
            S.omega_axis_dps[i] = cmd.omega_axis[i] * 180.0f / core::kPi;
        }
        S.k = dyn.k;
        S.rot_accel_raw = rds32(fl + F::flight_rot_accel);
        S.rot_pct = rot_pct;
        S.held = cmd.held;
        sample_seq_.store(seq + 2, std::memory_order_release);
    }
    return core::Reason::handled;
}
// Site B's write for one ship (the clamp from its last decision): lowering only, never below the engine's floor.
inline void clamp_apply(std::uint32_t obj, std::uint32_t ph, std::uint32_t fl) noexcept {
    const core::Ship* s = table_.find(obj);
    if (!s) return;
    if (table_id_[unsigned(s - table_.entries)] != rd32(obj + F::obj_id)) return; // the decision was another object's
    const std::uint16_t order = rd16(fl + F::flight_order);
    float x[3];
    for (unsigned i = 0; i < 3; ++i) x[i] = float(rds32(ph + F::phys_position + 4 * i)) / core::kUnitsPerMetre;
    const float v = core::clamp_for(*s, x, order, game_time_ms(), step_window_ms(F::clamp_max_age_ms), nullptr);
    if (v <= 0.0f) {
        clamp_stale_.add();
        return;
    }
    const std::int32_t units = core::clamp_units(v, rds32(obj + F::obj_desired_speed));
    if (units > 0) {
        wr32(obj + F::obj_desired_speed, units);
        clamp_applied_.add();
    }
}
} // namespace

extern "C" __attribute__((force_align_arg_pointer)) int __cdecl x3m_fl_ship_steering_enter(F::Frame* frame, int leaf) {
    if (!installed_.load(std::memory_order_relaxed)) return 0; // a site left chained by a failed rollback passes through
    Envelope env;
    const std::uint64_t t0 = timing_ ? qpc() : 0;
    const unsigned k = unsigned(leaf) < 3 ? unsigned(leaf) : 0u;
    const core::Reason r = decide(frame, k);
    calls_.add();
    leaf_calls_[k].add();
    if (r == core::Reason::handled)
        handled_.add();
    else
        reasons_[unsigned(r) < core::kReasonCount ? unsigned(r) : 0u].add();
    if (timing_) record_time(qpc() - t0);
    return r == core::Reason::handled ? 1 : 0;
}
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl x3m_fl_ship_steering_clamp(std::uint32_t obj) {
    if (!installed_.load(std::memory_order_relaxed)) return;
    Envelope env;
    clamp_calls_.add();
    const std::uint32_t flags = rd32(obj + F::obj_flags);
    if ((flags & (F::flag_deleted | F::flag_attached)) || rd32(obj + F::obj_docked)) return;
    const std::uint32_t fl = rd32(obj + F::obj_flight), ph = rd32(obj + F::obj_phys);
    if (!fl || !ph) return;
    clamp_apply(obj, ph, fl);
}

namespace {
bool pin_self() {
#ifdef X3M_SHIP_STEERING_FL_FIXTURE
    return true; // the fixture executable is never unloaded
#else
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&initialized_), &module) != FALSE &&
           module != nullptr;
#endif
}
void* stub_of(unsigned i) {
    switch (i) {
    case F::leaf_yaw_pitch: return reinterpret_cast<void*>(&x3m_fl_ship_steering_a0_stub);
    case F::leaf_roll_yaw: return reinterpret_cast<void*>(&x3m_fl_ship_steering_a1_stub);
    case F::leaf_roll_pitch: return reinterpret_cast<void*>(&x3m_fl_ship_steering_a2_stub);
    default: return reinterpret_cast<void*>(&x3m_fl_ship_steering_b_stub);
    }
}
bool straddles(std::uintptr_t at, unsigned n) {
    return (at & 7u) + n > 8u;
}
const char* write_name(const engine_patch::Site& s) {
    return s.patched_in ? (s.atomic_write ? "atomic" : "plain") : "none";
}
bool rollback() {
    bool restored = true;
    for (unsigned j = F::count; j-- > 0;)
        if (patches_[j].patched_in) {
            if (engine_patch::restore(patches_[j]))
                patches_[j] = engine_patch::Site{};
            else
                restored = false;
        }
    return restored;
}
// The install transaction: refused outside the install window, every window checked before any claim, claims in table order
// (each read back and its write path checked), every claimed site rolled back in reverse on the first failure.
bool install_all(const char** status, char (&detail)[64]) {
    detail[0] = 0;
    failed_window_ = F::window_count;
    base_ = module_base();
    if (!base_) {
        *status = "executable_mismatch";
        return false;
    }
    if (!engine_patch::install_window_open()) {
        *status = "late_claim";
        return false;
    }
    if (const char* refusal = x3m::fl::seta_windows_fl::plan(
            F::windows, 1, base_,
            [](std::uintptr_t at, void* out, unsigned n) { return engine_patch::read_code(at, static_cast<unsigned char*>(out), n); },
            &failed_window_)) {
        *status = refusal;
        if (failed_window_ < F::window_count) std::snprintf(detail, sizeof detail, "%s", F::windows[failed_window_].name);
        return false;
    }
    table_slot_ = base_ + F::ship_table_slot_rva;
    clock_slot_ = base_ + F::clock_slot_rva;
    get_max_speed_ = base_ + F::get_max_speed_rva;
    get_accel_ = base_ + F::get_accel_rva;
    get_pitch_cap_ = base_ + F::get_pitch_cap_rva;
    get_yaw_cap_ = base_ + F::get_yaw_cap_rva;
    get_roll_cap_ = base_ + F::get_roll_cap_rva;
    if (!pin_self()) {
        *status = "pin_failed";
        return false;
    }
    for (unsigned i = 0; i < F::count; ++i) {
        const F::Span& sp = F::spans[i];
        engine_patch::SiteSpec spec{};
        spec.name = sp.name;
        spec.address = base_ + sp.rva;
        spec.length = sp.length;
        spec.ret_pop = sp.ret_pop;
        std::memcpy(spec.expected, sp.bytes, sp.length);
        patches_[i] = engine_patch::Site{};
        const char* why = nullptr;
        if (!engine_patch::claim(patches_[i], spec))
            why = patches_[i].status;
        else {
            unsigned char now[5]{};
            const bool read = engine_patch::read_code(spec.address, now, 5);
#ifdef X3M_SHIP_STEERING_FL_FIXTURE
            if (fixture_fault_claim_ == i) {
                fixture_fault_claim_ = ~0u;
                now[1] ^= 0xff;
            }
#endif
            if (!read || std::memcmp(now, patches_[i].patched, 5))
                why = "readback_failed";
            else if (patches_[i].atomic_write == straddles(spec.address, 5))
                why = "write_path_mismatch";
            else {
                x3m_fl_ship_steering_next[i] = *patches_[i].entry; // the tail: the engine's own bytes until the stub is chained
                std::atomic_thread_fence(std::memory_order_release);
                if (!engine_patch::push_front(patches_[i], stub_of(i))) why = "stub_chain_failed";
            }
        }
        if (!why) continue;
        *status = why;
        std::snprintf(detail, sizeof detail, "%s", sp.name);
        // A site whose restore failed stays chained with our stub: every handler passes through while installed_ is false.
        if (!rollback()) *status = "rollback_failed_live_sites_pass_through";
        return false;
    }
    *status = "ok";
    return true;
}
// The groups this edition does not port: one row each, nothing read, nothing claimed.
void ap_only_rows() {
    static const char* const groups[5] = {"ship_steering_gates", "ship_steering_gates_capital", "ship_steering_gates_traffic",
                                          "ship_steering_gates_lane", "ship_steering_dock"};
    for (const char* g : groups) x3m::log("%s installed=0 edition=fl status=ap_only reason=not_ported", g);
}
} // namespace

namespace x3m::ship_steering {
bool initialize() {
    const DWORD error = GetLastError();
    if (initialized_) {
        SetLastError(error);
        return installed_.load(std::memory_order_acquire);
    }
    initialized_ = true;
    ap_only_rows();
    wchar_t text[8]{};
    const DWORD length = x3m::config::get(L"X3M_SHIP_STEERING", text, 8);
    bool on = true;
    const char* reason = nullptr;
    char detail[64] = {};
    if (length && (length >= 8 || !core::parse_switch(text, length, &on)))
        reason = "invalid_setting";
    else if (!on) {
        state_ = "off";
        SetLastError(error);
        return false; // 0: nothing claimed, no row (as on AP)
    }
    wchar_t classes[F::setting_capacity]{};
    const DWORD class_length = x3m::config::get(L"X3M_SHIP_STEERING_CLASSES", classes, F::setting_capacity);
    if (!reason) {
        if (class_length >= F::setting_capacity || !core::parse_class_mask(classes, class_length, &class_mask_))
            reason = "invalid_classes";
        else if (install_all(&reason, detail))
            reason = nullptr;
    }
    if (reason) {
        state_ = reason;
        x3m::log("ship_steering_refused reason=%s%s%s sites=%u edition=fl base=0x%08lx window=%s", reason, detail[0] ? "_" : "", detail,
                 unsigned(F::count), static_cast<unsigned long>(base_),
                 failed_window_ < F::window_count ? F::windows[failed_window_].name : "-");
        SetLastError(error);
        return false;
    }
    LARGE_INTEGER f{};
    qpc_frequency_ = QueryPerformanceFrequency(&f) && f.QuadPart > 0 ? std::uint64_t(f.QuadPart) : 0;
    perf_ = x3m::log_tier::cached_perf;
    debug_ = x3m::log_tier::cached_debug;
    timing_ = (perf_ || debug_) && qpc_frequency_ != 0;
    avoidance_initialize(); // X3M_SHIP_AVOIDANCE (needs the steering above): its own rows
    {
        wchar_t trace_text[8]{};
        if (x3m::config::get(L"X3M_SHIP_TRACE", trace_text, 8)) // the follow trace is part of the gate/dock diagnostics
            x3m::log("ship_trace installed=0 edition=fl status=ap_only reason=not_ported");
    }
    state_ = "installed";
    installed_.store(true, std::memory_order_release);
    x3m::log("ship_steering installed=1 edition=fl base=0x%08lx sites=%u class_mask=0x%04lx timing=%u tau_s=%.2f tick_s=%.2f "
             "floor_ms=%.0f bank_deg=35 clamp_age_ms=max(%lu,4*step) envelope=pushfd,eax-edx,xmm0-7,fnsave,mxcsr,cld,lasterror "
             "handler_a=0x%08lx handler_b=0x%08lx",
             static_cast<unsigned long>(base_), unsigned(F::count), static_cast<unsigned long>(class_mask_), timing_ ? 1u : 0u,
             double(core::kTau), double(core::kTickS), double(core::kFloorMs), static_cast<unsigned long>(F::clamp_max_age_ms),
             static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_fl_ship_steering_enter)),
             static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&x3m_fl_ship_steering_clamp)));
    for (unsigned i = 0; i < F::count; ++i)
        x3m::log("ship_steering_site index=%u edition=fl rva=0x%06lx site=0x%08lx name=%s status=%s write=%s stub=0x%08lx tail=0x%08lx",
                 i, static_cast<unsigned long>(F::spans[i].rva), static_cast<unsigned long>(base_ + F::spans[i].rva),
                 F::spans[i].name, patches_[i].status, write_name(patches_[i]),
                 static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(stub_of(i))),
                 static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(x3m_fl_ship_steering_next[i])));
    SetLastError(error);
    return true;
}
bool installed() {
    return installed_.load(std::memory_order_acquire);
}
const char* state() {
    return state_;
}
// Restores the five sites (the fixture: the FL loader has no unload path). The stubs pass through once installed_ is false.
bool shutdown() {
    const DWORD error = GetLastError();
    installed_.store(false, std::memory_order_release);
    const bool e_back = avoidance_shutdown();
    const bool back = rollback();
    SetLastError(error);
    return back && e_back;
}
void frame_boundary(std::uint64_t frame) {
    if (!installed_.load(std::memory_order_relaxed)) return;
    const std::uint32_t stuck = stuck_count_.load(std::memory_order_acquire);
    while (stuck_reported_ < stuck && stuck_reported_ < 8) {
        const Stuck& s = stuck_[stuck_reported_++];
        x3m::log("ship_steering_stuck frame=%llu edition=fl obj=0x%08lx order=0x%lx d_m=%.1f best_d_m=%.1f no_progress_s=%.0f",
                 static_cast<unsigned long long>(frame), static_cast<unsigned long>(s.obj), static_cast<unsigned long>(s.order),
                 double(s.d), double(s.best_d), double(core::kStuckMs / 1000.0f));
    }
    if (!perf_ && !debug_) return;
    const unsigned long long now = GetTickCount64();
    if (debug_) {
        const std::uint32_t seq = sample_seq_.load(std::memory_order_acquire);
        if (seq && !(seq & 1u) && seq != last_logged_seq_ && now - last_sample_tick_ >= 1000) {
            Sample S = sample_;
            if (sample_seq_.load(std::memory_order_acquire) == seq) {
                last_sample_tick_ = now;
                last_logged_seq_ = seq;
                x3m::log("ship_steering_decision frame=%llu edition=fl obj=0x%08lx order=0x%lx class=%s leaf=%lu held=%u theta_deg=%.2f "
                         "n_yaw=%.3f n_pitch=%.3f omega_dps=%.2f e=%ld,%ld,%ld w=%ld,%ld,%ld W_dps=%.1f,%.1f,%.1f k=%.3f d_m=%.1f "
                         "d_arr_m=%.1f v_ms=%.1f v_cmd_ms=%.1f v_corner_ms=%.1f v_ref_ms=%.1f P_m=%.1f,%.1f,%.1f x_m=%.1f,%.1f,%.1f "
                         "flags=0x%08lx a_dps2=%.2f,%.2f,%.2f f2b0=0x%08lx rot_pct=%ld omega_cmd_dps=%.2f,%.2f,%.2f lag_deg=%.1f "
                         "aim_off_deg=%.1f v_slip_ms=%.1f slip_ms=%.1f,%.1f,%.1f",
                         static_cast<unsigned long long>(frame), static_cast<unsigned long>(S.obj), static_cast<unsigned long>(S.order),
                         S.code < core::kClassCount ? core::kClassNames[S.code] : "?", static_cast<unsigned long>(S.leaf),
                         S.held ? 1u : 0u, double(S.theta_deg), double(S.n_y), double(S.n_p), double(S.omega_dps), long(S.e[0]),
                         long(S.e[1]), long(S.e[2]), long(S.w[0]), long(S.w[1]), long(S.w[2]), double(S.W_dps[0]),
                         double(S.W_dps[1]), double(S.W_dps[2]), double(S.k), double(S.d), double(S.d_arr), double(S.v),
                         double(S.v_cmd), double(S.v_corner), double(S.v_ref), double(S.P[0]), double(S.P[1]), double(S.P[2]),
                         double(S.x[0]), double(S.x[1]), double(S.x[2]), static_cast<unsigned long>(S.flags), double(S.a_dps[0]),
                         double(S.a_dps[1]), double(S.a_dps[2]), static_cast<unsigned long>(S.rot_accel_raw), long(S.rot_pct),
                         double(S.omega_axis_dps[0]), double(S.omega_axis_dps[1]), double(S.omega_axis_dps[2]), double(S.lag_deg),
                         double(core::acos_deg(S.aim_cos)), double(S.v_slip), double(S.slip[0]), double(S.slip[1]),
                         double(S.slip[2]));
                sample_request_.store(true, std::memory_order_relaxed);
            }
        } else if ((!seq || seq == last_logged_seq_) && now - last_sample_tick_ >= 1000) {
            last_sample_tick_ = now;
            sample_request_.store(true, std::memory_order_relaxed);
        }
        if (avoidance_on_) avoidance_rows(frame);
    }
    if (now - last_summary_tick_ < 1000) return;
    const unsigned long long interval = last_summary_tick_ ? now - last_summary_tick_ : 1000;
    last_summary_tick_ = now;
    const std::uint32_t calls = calls_.get(), handled = handled_.get(), ccalls = clamp_calls_.get(), capplied = clamp_applied_.get(),
                        cstale = clamp_stale_.get(), redirected = redirected_.get();
    const std::uint32_t d_calls = calls - rep_calls_;
    if (!d_calls && ccalls == rep_clamp_calls_) return; // nothing happened: no row
    std::uint32_t hist[32], total = 0;
    for (unsigned b = 0; b < 32; ++b) {
        hist[b] = qpc_hist_[b].exchange(0, std::memory_order_relaxed);
        total += hist[b];
    }
    const std::uint32_t max_ticks = qpc_max_.exchange(0, std::memory_order_relaxed);
    const double us_max = qpc_frequency_ ? double(max_ticks) * 1e6 / double(qpc_frequency_) : 0.0;
    auto clamped = [&](double p) { // the percentiles are bucket upper bounds (powers of two): never above the measured maximum
        std::uint32_t seen = 0;
        for (unsigned b = 0; b < 32; ++b) {
            seen += hist[b];
            if (total && double(seen) >= p * double(total)) {
                const double v = double(1ull << (b + 1)) * 1e6 / double(qpc_frequency_);
                return v > us_max ? us_max : v;
            }
        }
        return 0.0;
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
    orders[0] = 0;
    for (unsigned o = 0; o < 0x40; ++o) {
        const std::uint32_t v = handled_by_order_[o].get();
        if (!v) continue;
        const int k = std::snprintf(orders + n, sizeof orders - size_t(n), "%s0x%x:%lu", n ? "," : "", o, static_cast<unsigned long>(v));
        if (k > 0 && n + k < int(sizeof orders)) n += k;
    }
    const std::uint32_t l0 = leaf_calls_[0].get(), l1 = leaf_calls_[1].get(), l2 = leaf_calls_[2].get();
    char avoidance[2048] = "";
    if (avoidance_on_) avoidance_summary(avoidance, sizeof avoidance);
    x3m::log("ship_steering_summary frame=%llu edition=fl interval_ms=%llu claims=%u calls=%lu handled=%lu ships=%u "
             "calls_per_ship_s=%.1f leaf_calls=%lu,%lu,%lu%s clamp_calls=%lu clamp_applied=%lu clamp_stale=%lu redirected=%lu "
             "recycled=%lu timed=%lu handler_us_p50=%.2f handler_us_p99=%.2f handler_us_max=%.2f handled_by_order_total=%s "
             "stuck_total=%lu%s",
             static_cast<unsigned long long>(frame), interval, unsigned(F::count), static_cast<unsigned long>(d_calls),
             static_cast<unsigned long>(handled - rep_handled_), ships, per_ship, static_cast<unsigned long>(l0 - rep_leaf_[0]),
             static_cast<unsigned long>(l1 - rep_leaf_[1]), static_cast<unsigned long>(l2 - rep_leaf_[2]), reasons,
             static_cast<unsigned long>(ccalls - rep_clamp_calls_), static_cast<unsigned long>(capplied - rep_clamp_applied_),
             static_cast<unsigned long>(cstale - rep_clamp_stale_), static_cast<unsigned long>(redirected - rep_redirected_),
             static_cast<unsigned long>(recycled_.get()), static_cast<unsigned long>(total), clamped(0.5), clamped(0.99), us_max,
             orders, static_cast<unsigned long>(stuck_count_.load(std::memory_order_relaxed)), avoidance);
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
#ifdef X3M_SHIP_STEERING_FL_FIXTURE
void fixture_set_base(std::uintptr_t base) {
    fixture_base_ = base;
}
void fixture_fault_claim(unsigned index) {
    fixture_fault_claim_ = index;
}
void fixture_reset() { // after a shutdown: the next initialize() installs afresh
    initialized_ = false;
    state_ = "not_initialized";
    table_ = core::ShipTable{};
    for (std::uint32_t& id : table_id_) id = 0;
    for (av::ShipAvoid& a : avoid_) a = av::ShipAvoid{};
}
#endif
} // namespace x3m::ship_steering
