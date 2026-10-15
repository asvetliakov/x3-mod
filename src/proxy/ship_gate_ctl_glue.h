#pragma once
// The gate traffic controller's state, its --debug row rings and the board helpers that more than one ship steering unit calls
// (inline; ship_steering_internal.h). The controller's out-of-line glue (the frame, the commit, the arrival handler, the rows) is
// ship_gate_ctl_glue.cpp.
#include "ship_steering_internal.h"
namespace x3m_ship_steering_rt {
// ---- the gate traffic controller, stage A (gate-traffic-controller.md D.1, D.4, D.7; ship_gate_ctl_core.h) -------------------------
// X3M_SHIP_STEERING_GATES_TRAFFIC (default on; needs ship_steering_gates_capital live): the per-gate member boards replace the lane
// board for capitals at a gate (no lane verdicts, no certificates, no (b) cap), site E owns member pairs (suppress_ctl / pass_ctl)
// and the arrival-gate row, the avoidance drops member pairs with a clear verdict, the launch window 0x00444606 places arrivals.
// Off: amendment 17 exactly (no claim, no board, no E row, no exclusion). Game thread only; no allocation, no lock.
namespace gc = x3m::ship_avoidance::ctl;
inline std::atomic<bool> traffic_live_{false};
inline const char* traffic_state_ = "off";
inline gc::Board ctl_;
inline gc::Index ctl_ix_;      // member ships (every board, every role but none)
inline gc::Arrivals arrivals_; // every launch at a gate within 60 s (the arrival-gate row; capital arrivals for the small-ship row)
inline gc::Index arr_ix_;      // their ships (rebuilt once per second)
inline std::uint32_t ctl_decide_max_ticks_ = 0, ctl_frame_max_ticks_ = 0, ctl_arrival_max_ticks_ = 0;
// Run176 diagnostics item 1 (--debug with timing): the ctl_decide_us_max holder's thread cycles (0: not read) and its gate and member
inline std::uint64_t ctl_decide_max_cycles_ = 0;
inline std::uint32_t ctl_decide_max_gate_ = 0, ctl_decide_max_ship_ = 0, ctl_decide_max_id_ = 0;
inline void ctl_decide_max_note(std::uint32_t ticks, std::uint64_t cyc0, const gc::Gate& g, unsigned i) noexcept {
    ctl_decide_max_ticks_ = ticks;
    const std::uint64_t cyc1 = cyc0 ? thread_cycles() : 0;
    ctl_decide_max_cycles_ = cyc1 > cyc0 ? cyc1 - cyc0 : 0;
    ctl_decide_max_gate_ = g.gate;
    ctl_decide_max_ship_ = g.m[i].ship;
    ctl_decide_max_id_ = g.m[i].id;
}
inline unsigned ctl_frame_max_decisions_ = 0;
inline Counter ctl_decisions_, ctl_rollouts_, ctl_sweeps_, ctl_narrow_, ctl_conflicts_, ctl_yields_, ctl_unresolved_, ctl_released_;
inline Counter ctl_entered_, ctl_left_, ctl_evicted_, ctl_capped_, ctl_deferred_;
inline Counter e_suppressed_ctl_, e_pass_ctl_, e_suppressed_arrival_gate_, avoid_ctl_excluded_;
inline Counter arr_launches_, arr_offset_capital_, arr_small_side_, arr_small_unseparated_, arr_skipped_;
// KC refusals at d_end (and their wait beyond 3 s), the decisions the lane flew, the P2 remainder zeroing, the small ships' f+0x240
// rewrites (D.5)
inline Counter ctl_x_refused_, ctl_sb_steered_, ctl_p2_zeroed_, small_b_rewrites_;
// lane stage 1 (convoy-gate-transit.md section 6.9): merges started, brief stops, f+0x240 rewrites, site B's lane writes (order-0 holds
// and stops), the KC refusals at d_end come through ctl_x_refused_
inline Counter ctl_merge_yield_, ctl_merge_stop_, ctl_point_rewrites_, ctl_lane_writes_;
// review item 2: order-0 frames with site B's tick deferral written, and the order-4 points rewritten at site B on a deferred tick;
// review item 4: fields adopted as the hull's own L_i without a lane member, and grid misses (a lane member's field neither a KC grid
// point nor ours)
inline Counter ctl_lane_defers_, ctl_defer_rewrites_, ctl_point_adopts_, ctl_grid_miss_;
inline Counter ctl_recommit_held_; // lane fix round 1 item 4: commits held by the re-commit hysteresis
inline Counter ctl_mouth_expired_; // review of the arrival round item 3: gate-mouth holds ended by their game-time bound
inline Counter e_gate_ring_, e_gate_ring_kept_; // the arrival round item 4: site E's ring row (suppressed; kept: refused_hull / roll)
inline Counter e_lane_obst_, e_lane_obst_roll_; // lane fix round 2 item 1: site E's lane obstacle row (suppressed; passed: the path does not clear)
inline Counter e_pass_ctl_why_[unsigned(gc::EWhy::count)]; // Run161: why site E passed a member pair (not_member: a member against a capital
                                                    // that is not on its board)
inline Counter ctl_evict_ignored_gate_, ctl_budget_cut_; // B.3: 0x12 episodes kept through (item 2); frames cut at the 1 ms ceiling
inline std::uint64_t ctl_ceiling_ticks_ = 0;             // B.3: gc::kFrameCeilingUs in QPC ticks (0: no ceiling, no QPC)
inline Counter e_small_gap_; // B.4: suppress_small with A's decision older than the fresh window (the order-0 gap)
// B.5 (ship_gate_ctl_live_core.h): the per-frame live set, the controller's sector, the frame's proofs and leaves; counters: members left
// unproven (gone), launched members left (leg over or missing from the snapshot), sector resets and the entries they dropped, member-frames
// without a proof, frames whose set overflowed (nothing proven); the proofs' longest frame (QPC ticks, timing tier)
inline gc::LiveSet live_set_;
inline Counter ctl_live_gone_, ctl_live_launch_, ctl_sector_resets_, ctl_sector_dropped_, ctl_unproven_, ctl_live_overflow_;
inline std::uint32_t ctl_live_max_ticks_ = 0;
inline Counter e_gate_small_gap_, small_b_both_, ctl_clock_resets_;                         // Run161 item 5: the small-ship gate row on order 0 in the hand-over gap
// convoy-gate-transit.md section 12.2 (round B): small-ship order-4 decisions on the side lead-in, D.5 edge rewrites, holds begun
inline Counter ctl_small_side_, ctl_small_edge_, ctl_small_hold_;
// section 12.3 (round B): the lane profile's builds, builds with a shift, the statics a member's sphere avoidance left to it, the longest
// build (QPC ticks, timing tier)
inline Counter ctl_prof_builds_, ctl_prof_shifted_, avoid_lane_skipped_;
inline std::uint32_t ctl_prof_max_ticks_ = 0;
inline std::uint32_t ctl_x_wait_ms_ = 0, ctl_sb_max_ticks_ = 0;
// The widest-part read per capital type (the record index [obj+0x4a]) for the arrival placement: a launched capital has no gate
// state yet; w and l from the last successful LOD read of a ship of that type (w = r, l = r when the type was never read).
struct TypeWidth {
    std::int32_t type;
    float w, l;
};
inline TypeWidth type_widths_[32];
inline unsigned type_width_count_ = 0;
inline std::uint32_t type_width_gen_ = 0; // final review item 4: changes with every new or changed width (gc::lane_adopt_memo's key)
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
};
inline CtlRow ctl_rows_[32];
inline std::atomic<std::uint32_t> ctl_row_write_{0};
inline std::uint32_t ctl_row_read_ = 0;
struct CtlPairRow {
    std::uint32_t gate, a, b, t_ms, seq_a, seq_b;
    std::uint8_t verdict, before, t_c, yielder, rank_a, rank_b, kind, why; // kind: 0 a verdict change, 1 site E passed the pair (why: the
    std::uint8_t cause;                                                    // gc::EWhy), 2 a member's order-0 frame (the pair's E reason);
    float cap, d, gap;                                                     // B.9: cause = g.why (gc::PairWhy | kPairWhyPlan), gap = pair_gap
};
inline CtlPairRow ctl_pair_rows_[64];
inline std::atomic<std::uint32_t> ctl_pair_write_{0};
inline std::uint32_t ctl_pair_read_ = 0;
// Review item 9 (--debug): every order-4 point decision of a lane member (ship_gate_ctl_rewrite): site A's rewrite, site B's on a
// deferred tick, a field adopted as the hull's own L_i, a grid miss; the field before and after in the gate frame (R, U, F; m), the
// KC slot the field held (-1 none)
struct RewriteRow {
    std::uint32_t gate, ship, t_ms;
    std::int8_t slot;
    std::uint8_t site; // 0 a, 1 b, 2 adopt, 3 miss
    float from[3], to[3];
    float r, implied, kc; // Run175 round item 1: the hull's radius, the size the field's depth implies (-1: off the pattern), the KC's size
};
inline RewriteRow rewrite_rows_[8];
inline std::atomic<std::uint32_t> rewrite_row_write_{0};
inline std::uint32_t rewrite_row_read_ = 0;
// Lane fix round 1 item 5 (--debug): the tick deferral per ship (ship_gate_ctl_defer): the order, f+0x210 as read and the stamp written;
// kind defer (an order-0 frame's write: the episode's first, then at most one row per ship per game second), restore (the next order's
// first frame put the stamp back), rewrite (... and rewrote a grid point to L_i)
struct DeferRow {
    std::uint32_t gate, ship, t_ms, read, written;
    std::uint16_t order;
    std::uint8_t kind; // 0 defer, 1 restore, 2 rewrite
};
inline DeferRow defer_rows_[8];
inline std::atomic<std::uint32_t> defer_row_write_{0};
inline std::uint32_t defer_row_read_ = 0;
inline void ctl_row(const gc::Gate& g, const gc::Member& m, gc::Role from, gc::Role to, gc::Why why, std::uint32_t now) noexcept {
    if (!debug_) return;
    const std::uint32_t w = ctl_row_write_.load(std::memory_order_relaxed);
    if (w - ctl_row_read_ >= 32) {
        row_dropped(Drop::ctl);
        return;
    }
    ctl_rows_[w & 31] = CtlRow{g.gate, m.ship, m.id, m.seq, now, m.key, m.st.order, std::uint8_t(from), std::uint8_t(to), std::uint8_t(why),
                               std::uint8_t(gc::rank(m)), std::uint8_t(m.sb), std::uint8_t(m.sb)};
    ctl_row_write_.store(w + 1, std::memory_order_release);
}
// A ship_gate_ctl_rewrite row (review item 9; --debug): the field before (cur) and after (to; the same for adopt / miss), units.
inline void lane_rewrite_row(const gc::Gate& g, std::uint32_t ship, float r_s, const std::int32_t cur[3], const std::int32_t to[3], unsigned site,
                             std::uint32_t now, float implied = -1.0f, float kc = -1.0f) noexcept {
    if (!debug_) return;
    const std::uint32_t w = rewrite_row_write_.load(std::memory_order_relaxed);
    if (w - rewrite_row_read_ >= 8) {
        row_dropped(Drop::ctl);
        return;
    }
    RewriteRow& r = rewrite_rows_[w & 7];
    r.gate = g.gate;
    r.ship = ship;
    r.t_ms = now;
    r.site = std::uint8_t(site);
    r.r = r_s;
    r.implied = implied;
    r.kc = kc;
    float pc[3], pt[3];
    for (unsigned i = 0; i < 3; ++i) {
        pc[i] = float(cur[i]) / core::kUnitsPerMetre;
        pt[i] = float(to[i]) / core::kUnitsPerMetre;
    }
    r.slot = std::int8_t(gc::kc_grid_slot_any(g.G, g.R, g.U, g.F, g.r_gate, r_s, pc)); // (Run175 round item 1: a script point of any size)
    const float dc[3] = {pc[0] - g.G[0], pc[1] - g.G[1], pc[2] - g.G[2]}, dt[3] = {pt[0] - g.G[0], pt[1] - g.G[1], pt[2] - g.G[2]};
    r.from[0] = core::dot3(dc, g.R);
    r.from[1] = core::dot3(dc, g.U);
    r.from[2] = core::dot3(dc, g.F);
    r.to[0] = core::dot3(dt, g.R);
    r.to[1] = core::dot3(dt, g.U);
    r.to[2] = core::dot3(dt, g.F);
    rewrite_row_write_.store(w + 1, std::memory_order_release);
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
    if (why == gc::Why::target && g.lane) gc::note_target_leave(ctl_, before.ship, now); // lane fix round 1 item 4 (lane boards only)
    if (evict) ctl_evicted_.add();
    else ctl_left_.add();
    ctl_row(g, before, before.role, evict ? gc::Role::evicted : gc::Role::none, why, now);
    if (!evict) gc::rebuild(ctl_ix_, ctl_);
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
gc::Member* ctl_commit(GateState& g, std::uint32_t obj, const core::Inputs& in, const core::Dynamics& dyn, const core::GateGeometry* geo,
                       const float G[3], const float R[3], const float U[3], const float F[3], float r_gate, std::uint32_t gate,
                       std::uint32_t now) noexcept;
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
                                        std::uint8_t(gc::rank(b)), kind, std::uint8_t(why), g.why[i][j], m.cap, core::sqrt_ss(core::dot3(dx, dx)),
                                        gc::pair_gap(g, i, j)};
    ctl_pair_write_.store(w + 1, std::memory_order_release);
}
} // namespace x3m_ship_steering_rt
