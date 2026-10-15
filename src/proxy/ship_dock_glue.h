#pragma once
// Amendment 18 (docking): the dock state and the inline dock helpers site A (ship_steering_decide.cpp) and site E
// (ship_steering_warn.cpp) call; the --debug dock rows are ship_dock_glue.cpp.
#include "ship_gate_ctl_glue.h"
namespace x3m_ship_steering_rt {
// ---- Amendment 18: the docking approach (docking-approach.md; ship_avoidance_core.h "Amendment 18") ---------------------------------
// X3M_SHIP_STEERING_DOCK (default on; needs X3M_SHIP_AVOIDANCE): site E's own-container row (suppress_dock / pass_dock_*), the order's
// container kept as a part obstacle on order 4 (stations and carriers) and on 0x1d (stations), the carrier's hull parts through the
// station part walk, the ship_avoid_dock row (--debug) and the counters. Off: today's rules exactly (the target excluded on order 4, no
// row at E). No new hook site; reads only; game thread; no allocation.
inline bool dock_on_ = false;
inline const char* dock_state_ = "off";
inline Counter e_suppressed_dock_, e_pass_dock_[unsigned(av::DockE::count)], dock_part_obstacles_, dock_detours_, idle_steered_;
inline Counter dock_models_built_, dock_no_model_, dock_rows_dropped_;
inline std::uint32_t dock_e_max_ticks_ = 0;
inline std::uint32_t dock_row_ms_[core::kShipCapacity]; // per table slot: the last ship_avoid_dock row (1 Hz per ship)
inline float dock_w_[core::kShipCapacity][3];          // per table slot: the container's velocity of the last order-4 decision (0: none)
struct DockRow {
    std::uint32_t ship, cont, t_ms;
    std::uint16_t order, cls;
    std::uint8_t parts_ok, verdict, zone, kept, detour, pad[3];
    unsigned n_parts;
    float path_m, ray_m, margin_m, reach_m, d_c_m, r_c_m, v_ms;
};
inline DockRow dock_rows_[32];
inline std::atomic<std::uint32_t> dock_row_write_{0};
inline std::uint32_t dock_row_read_ = 0;
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
inline DockModelRec dock_models_[8];
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
// (Always inlined, as dock_bind: the single ship_steering.cpp inlined both, called once each; in the split units, where they have
// external linkage, GCC's called-once rule no longer applies and they became calls on site A's and E's paths.)
__attribute__((always_inline)) inline av::DockE dock_e(std::uint32_t a, std::uint32_t fl, std::uint32_t b, std::uint16_t cls_b, const core::Ship& s) noexcept {
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
__attribute__((always_inline)) inline bool dock_bind(av::Self& self, std::uint32_t obj, const core::Inputs& in, float a_lin, float r_own, unsigned slot, std::uint32_t now,
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
} // namespace x3m_ship_steering_rt
