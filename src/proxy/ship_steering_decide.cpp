// Ship steering, site A: the leaf handler decide() (x3m_ship_steering_enter) with the avoidance adjustment, the gate approach and
// the capital join rows it writes.
#include "ship_dock_glue.h"
namespace {
unsigned census_logged_count_ = 0;
std::int32_t target_ring_[4][3];  // the step-2 seam: a redirected EDX points here, never at the caller's buffer
unsigned target_depth_ = 0;
inline void record_time(std::uint64_t ticks) noexcept {
    const std::uint32_t t = ticks > 0xffffffffull ? 0xffffffffu : std::uint32_t(ticks);
    const unsigned bucket = t ? 31u - unsigned(__builtin_clz(t)) : 0u;
    qpc_hist_[bucket].store(qpc_hist_[bucket].load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    if (t > qpc_max_.load(std::memory_order_relaxed)) qpc_max_.store(t, std::memory_order_relaxed);
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
} // namespace
namespace x3m_ship_steering_rt {
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
} // namespace x3m_ship_steering_rt
namespace {
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
// Site A's gate branch (decide() below, between the gate frame and the arrival parameters; extracted on 2026-10-15 by the source
// split, docs/architecture/ship-steering-holistic-review.md rank 3): the lane's order-4 point (lane stage 1), the order-4 approach to
// a gate (step 3 P1; amendments 8-16 for a capital, the commit to the traffic controller) and order 5 to a gate. The locals it shares
// with decide() are its parameters, the const ones by value and the rest by reference (the same objects). Every statement of the branch
// tests gph (the target is a gate), so decide() calls it only then: a ship that is not on a gate approach pays one test, a gate approach
// one call (out of line; forcing it inline made GCC stop inlining the small helpers of decide()'s common path, measured in
// verification/results/ship-steering/source-split/gate_decide.txt).
void gate_decide(sites::Frame*& f, const std::uint32_t obj, const bool player, const std::uint32_t fl, const std::uint16_t order,
                 const std::uint32_t code, core::Dynamics& dyn, const bool capital, core::Inputs& in, const std::uint32_t now,
                 core::Ship*& s, const unsigned slot, std::uint32_t& v_emergency_bits, float (&order_point)[3], bool& gate_approach,
                 bool& gate_lead, bool& gate_dock, GateState*& gs, const std::uint32_t gate_tgt, const std::uint32_t gph, float (&G)[3],
                 float (&row0)[3], float (&row1)[3], float (&row2)[3], float& r_gate, const bool capital_policy, bool& capital_window,
                 bool& capital_fb_write, bool& capital_detour, float& capital_fb_v, bool& lane_ours) noexcept {
    // Lane stage 1 (convoy-gate-transit.md section 6.4): the order-4 point. A field still holding the last rewrite (or, after a slot reset
    // or a load, this hull's lane point L_i) is ours: the order's grid geometry is the KC grid point it replaced (slot 0's after a reset),
    // so the validation, the commit and P2 below see the KC's grid as before; a call whose leaf point is not the field (the engine's
    // container detour, ship-navigation-ai.md section 10.12 item 2) stays the fallback it is today. Reads: the live order's own f+0x240.
    if (lane_on_ && traffic_on_ && capital && !player && gph && order == core::kOrderFlyBefore && gs) {
        const std::int32_t cur[3] = {rds32(fl + sites::kFlightPoint), rds32(fl + sites::kFlightPoint + 4), rds32(fl + sites::kFlightPoint + 8)};
        float cp[3];
        for (unsigned i = 0; i < 3; ++i) cp[i] = float(cur[i]) / core::kUnitsPerMetre;
        const float dd[3] = {cp[0] - in.P[0], cp[1] - in.P[1], cp[2] - in.P[2]};
        if (core::dot3(dd, dd) <= 1.0f) {
            if (gc::small_b_ours(gs->lane_w, cur)) {
                lane_ours = true;
                for (unsigned i = 0; i < 3; ++i) order_point[i] = gs->lane_grid[i];
            } else {
                int ci = -1;
                const gc::Gate* cg = ctl_lookup(obj, &ci);
                const bool member = cg && cg->gate == gate_tgt && cg->lane && ci >= 0 && cg->m[ci].ln && cg->m[ci].q_ok &&
                                    cg->m[ci].id == rd32(obj + sites::kObjId);
                bool adopt = false;
                if (member) lane_ours = gc::lane_point_ours(*cg, cg->m[ci], cp); // (a tolerance, not the live l_c within 1 m: review item 4)
                else { // review item 4: no lane member (a load, a slot reset): the hull's own L_i, from its own hull and the gate's frame;
                       // final review item 4: an escort's L_i on some l_c when the board has none (a load), and a negative result memoised
                       // per field value (gc::lane_adopt_memo: every non-member capital on order 4 reaches this on every call)
                    const gc::Gate* bg = gc::gate_of(ctl_, gate_tgt);
                    const float* lc = bg && bg->lane_c_ok ? bg->lane_c : nullptr;
                    adopt = gc::lane_adopt_memo(gs->lane_noadopt, cur, type_width_gen_, lc != nullptr, [&]() -> bool {
                        const std::uint16_t gtype = rd16(gate_tgt + sites::kObjType); // (lane fix round 2 item 4: d_end by the gate's row)
                        if (!lc && gc::lane_point_lc_env(G, row0, row1, row2, r_gate, cp, gtype)) return true;
                        float w = 0.0f, l = 0.0f;
                        if (!type_width_get(std::int16_t(rd16(obj + sites::kObjType)), &w, &l)) w = l = 0.0f;
                        gc::Member hull{};
                        gc::hull_from(hull, dyn.r_own, w, l);
                        gc::params_from(hull.params, dyn, r_gate);
                        return gc::lane_point_own(G, row0, row1, row2, r_gate, hull.r, hull.w, hull.L, hull.params.v_max, gc::gate_terran(gtype), lc,
                                                  cp, gtype);
                    });
                    lane_ours = adopt;
                }
                if (lane_ours) { // the order's geometry: the KC's slot-0 grid point (the replaced slot is not known)
                    for (unsigned i = 0; i < 3; ++i) {
                        order_point[i] = G[i] - 2.0f * (r_gate + dyn.r_own) * row2[i];
                        gs->lane_grid[i] = order_point[i];
                        gs->lane_w.w[i] = cur[i];
                    }
                    gs->lane_w.ok = 1;
                }
                if (adopt) {
                    ctl_point_adopts_.add();
                    const gc::Gate* bg = gc::gate_of(ctl_, gate_tgt);
                    if (bg) lane_rewrite_row(*bg, obj, dyn.r_own, cur, cur, 2, now);
                }
            }
        }
    }
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
        if (capital_policy && !gate_lead && traffic_on_) { // the controller: not committed (no validated grid point)
            int ci = -1;
            gc::Gate* cg = ctl_lookup(obj, &ci);
            if (cg && cg->gate == gate_tgt && cg->m[ci].role == gc::Role::committed) ctl_leave(*cg, unsigned(ci), false, gc::Why::target, now);
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
            // D.6: the controller's committed member is entered first; a lane member's order-4 leg has no join mode or aim-at-A latch (the
            // lane flies it); the key off, a ship the board refuses or one outside a lane stage keeps the amendment-15 join
            bool sb_member = false;
            if (capital && traffic_on_ && !player) {
                const gc::Member* cm = ctl_commit(*gs, obj, in, dyn, &g, G, row0, row1, row2, r_gate, gate_tgt, now);
                sb_member = cm && gc::sb_managed(*cm); // review F8: else the stage-A / amendment-17 join
                // lane stage 1 (section 6.4): a lane member's f+0x240 on one of the KC's 20 grid points (within 1 m; the closed form of
                // section 10.2 with this hull's radius) becomes its lane point L_i, d_end - 0.75 r_gate before the plane on its final; the
                // engine's next re-plan tick (0-49 ms) tests and steers at it (section 10.12 item 2). A KC re-issue writes a grid point
                // again and is caught again; a field holding our write is left alone. One 12-byte store.
                if (lane_on_ && cm && cm->ln && cm->q_ok && cm->role == gc::Role::committed && !lane_ours && !in.detour) {
                    const std::int32_t cur[3] = {rds32(fl + sites::kFlightPoint), rds32(fl + sites::kFlightPoint + 4), rds32(fl + sites::kFlightPoint + 8)};
                    float cp[3];
                    for (unsigned i = 0; i < 3; ++i) cp[i] = float(cur[i]) / core::kUnitsPerMetre;
                    const gc::Gate* cg = gc::gate_of(ctl_, gate_tgt);
                    if (cg && gc::kc_grid_slot(G, row0, row1, row2, r_gate, dyn.r_own, cp) >= 0) {
                        float L[3];
                        gc::lane_order_point(*cg, *cm, L);
                        std::int32_t units[3];
                        bool ok = true;
                        for (unsigned i = 0; i < 3; ++i) {
                            ok = ok && core::finite_f(L[i]) && L[i] > -4.0e6f && L[i] < 4.0e6f; // the int32 range guard (units = 500 x m)
                            units[i] = ok ? core::round_to_int(L[i] * core::kUnitsPerMetre) : 0;
                        }
                        if (ok) {
                            for (unsigned i = 0; i < 3; ++i) {
                                wr32(fl + sites::kFlightPoint + 4 * i, units[i]);
                                gs->lane_w.w[i] = units[i];
                                gs->lane_grid[i] = cp[i];
                            }
                            gs->lane_w.ok = 1;
                            ctl_point_rewrites_.add();
                            lane_rewrite_row(*cg, obj, dyn.r_own, cur, units, 0, now);
                        }
                    } else if (cg && !gc::small_b_ours(gs->lane_miss, cur)) { // review item 4: neither a grid point nor ours, once per value
                        for (unsigned i = 0; i < 3; ++i) gs->lane_miss.w[i] = cur[i];
                        gs->lane_miss.ok = 1;
                        ctl_grid_miss_.add();
                        lane_rewrite_row(*cg, obj, dyn.r_own, cur, cur, 3, now);
                    }
                }
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
                // also after a sweep dropped it (a stale gap, then the same leg)
                if (gs->join.mode == core::JoinMode::join) join_register(slot);
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
                // a detour hands the leaf another point (the lead-in is skipped below); the leg is still steered, so site B keeps the
                // join's speed both ways instead of the engine's 1 m/s alignment creep (Run158 crowd; fixture scenario M)
                capital_detour = cap_join && in.detour;
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
        // a capital's order-5 leg on its validated approach keeps its controller entry current; the engine's speed law stands (no
        // site-B write)
        if (capital_policy && gs->validated == 1 && traffic_on_ && !player)
            ctl_commit(*gs, obj, in, dyn, nullptr, G, row0, row1, row2, r_gate, gate_tgt, now);
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
}
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
    if (avoidance_on_ && policy.avoidance) {
        float P[3] = {in.P[0], in.P[1], in.P[2]};
        avoid_adjust(obj, fl, in, dyn, code, slot, now, P, &v_emergency_bits);
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
    bool capital_detour = false;   // amendment 16 fix: a join on an avoidance detour: the join's speed write is kept
    float capital_fb_v = 0.0f;
    bool lane_ours = false; // lane stage 1: the order-4 point is the lane's rewrite (gate_decide)
    if (gph) // the gate branch (every statement of it tests gph)
        gate_decide(f, obj, player, fl, order, code, dyn, capital, in, now, s, slot, v_emergency_bits, order_point, gate_approach,
                    gate_lead, gate_dock, gs, gate_tgt, gph, G, row0, row1, row2, r_gate, capital_policy, capital_window, capital_fb_write,
                    capital_detour, capital_fb_v, lane_ours);
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
    // The lane (convoy-gate-transit.md section 6.1): a member it flies (a committed member on order 4, an escort on 0x14 / 0x15, the capital
    // order-5 leg on Lambda_5) gets the lane's leaf point (an avoidance detour keeps its point) and its speed at site B in both directions
    // (no arrival ramp, no formation law); the key off flies none
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
            if (!in.detour && !sbs.keep_point) { // (keep_point: lane fix round 2 item 2, the speed only)
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
        // and the join (the registry's sweep drops it) and the fallback
        if (gate_[slot].join.mode == core::JoinMode::join || gate_[slot].join.mode == core::JoinMode::fallback)
            gate_[slot].join.mode = core::JoinMode::none;
    }
    // amendment 14: the formation law's |N| rides in the cornering slot (no ramp: site B returns it floored); the closing-speed
    // correction applies to the ramp/cornering schedule only (not on the gate legs, whose speed is the module's own)
    s->v_corner = cmd.formation_speed ? cmd.v_cmd : cmd.v_corner;
    s->v_slip = (speed_flags & (core::kShipFlagGate | core::kShipFlagGateDock)) || cmd.formation_speed ? 0.0f : cmd.v_slip;
    std::memcpy(&s->v_emergency, &v_emergency_bits, 4);
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
                if (m.cap >= 0.0f && now - m.cap_ms <= gc::age_bound(*cg, gc::kCapMaxAgeMs) && gc::can_yield_on(m, order) && ln::apply_cap(*s, m.cap)) ctl_capped_.add();
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
} // namespace

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
