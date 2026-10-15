// Ship steering, site B: the per-frame speed clamp (clamp_apply, lane_clamp, the controller's observation), and the gate sites
// P2 / P4 / P5 (x3m_ship_gate_stop_a, _stop_b, _launch).
#include "ship_dock_glue.h"
namespace {
inline void trace_release(std::uint32_t obj) noexcept {
    if (TraceSlot* s = trace_slot_of(obj)) s->obj = 0;
}
std::uint32_t defer_logged_ship_[8] = {}, defer_logged_ms_[8] = {}; // the last defer row per ship (one per game second)
unsigned defer_logged_next_ = 0;
// A ship_gate_ctl_defer row (lane fix round 1 item 5; --debug).
inline void lane_defer_row(const gc::Gate& g, std::uint32_t ship, std::uint16_t order, std::uint32_t read, std::uint32_t written, unsigned kind,
                           std::uint32_t now) noexcept {
    if (!debug_) return;
    if (kind == 0) { // (a defer write: the episode's first, then one per ship per game second)
        unsigned k = 0;
        while (k < 8 && defer_logged_ship_[k] != ship) ++k;
        if (k < 8 && now - defer_logged_ms_[k] < 1000) return;
        if (k == 8) {
            k = defer_logged_next_;
            defer_logged_next_ = (defer_logged_next_ + 1) & 7;
            defer_logged_ship_[k] = ship;
        }
        defer_logged_ms_[k] = now;
    } else
        for (unsigned k = 0; k < 8; ++k)
            if (defer_logged_ship_[k] == ship) defer_logged_ship_[k] = 0; // (the next episode's first write is logged)
    const std::uint32_t w = defer_row_write_.load(std::memory_order_relaxed);
    if (w - defer_row_read_ >= 8) {
        row_dropped(Drop::ctl);
        return;
    }
    defer_rows_[w & 7] = DeferRow{g.gate, ship, now, read, written, order, std::uint8_t(kind)};
    defer_row_write_.store(w + 1, std::memory_order_release);
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
// Lane stage 1 (convoy-gate-transit.md sections 6.3-6.5, ship-navigation-ai.md section 10.12 item 4): site B's lane write for a lane
// member after the clamp, on every frame of the update (order 0 zeroes the desired speed every frame): a stop in force writes 0; on order
// 0 / 0x1f with the nose within 8 deg of F the lane speed stands (gc::lane_site_b). Reads: the ship's own flight block of this update.
// Review item 2 (gc::lane_defer_step): on a lane escort's order-0 frame in its lost-leader wait, f+0x210 (the re-plan stamp order 0's tail
// has just set to now) is moved forward; on the next order's first frame its point is rewritten if it is a KC grid point and the stamp
// is put back, so the order's first tick tests L_i one frame later. Reads and writes: the ship's own flight block of this update.
inline void lane_clamp(std::uint32_t obj, std::uint32_t fl, std::uint32_t ph) noexcept {
    int ci = -1;
    gc::Gate* g = ctl_lookup(obj, &ci);
    if (!g || !g->lane || ci < 0 || g->m[ci].id != rd32(obj + sites::kObjId)) return;
    const std::uint16_t order = rd16(fl + sites::kFlightOrder);
    const std::uint32_t now = game_time_ms();
    if (now && (order == core::kOrderNone || g->m[ci].defer_w)) { // (no clock: no deferral)
        const std::int32_t cur[3] = {rds32(fl + sites::kFlightPoint), rds32(fl + sites::kFlightPoint + 4), rds32(fl + sites::kFlightPoint + 8)};
        float p[3];
        for (unsigned i = 0; i < 3; ++i) p[i] = float(cur[i]) / core::kUnitsPerMetre;
        const std::uint32_t target = rd32(fl + sites::kFlightTarget), f210 = rd32(fl + sites::kFlightReplan);
        const gc::LaneDefer d = gc::lane_defer_step(*g, g->m[ci], order, target, f210, now, p);
        if (d.rewrite) {
            std::int32_t units[3];
            bool ok = true;
            for (unsigned i = 0; i < 3; ++i) {
                ok = ok && core::finite_f(d.L[i]) && d.L[i] > -4.0e6f && d.L[i] < 4.0e6f; // the int32 range guard (units = 500 x m)
                units[i] = ok ? core::round_to_int(d.L[i] * core::kUnitsPerMetre) : 0;
            }
            if (ok) {
                for (unsigned i = 0; i < 3; ++i) wr32(fl + sites::kFlightPoint + 4 * i, units[i]);
                // final review item 3: the rewrite is recorded as site A's is (the written field and the grid point it replaced), so
                // site A's next call finds the field ours directly and keeps the replaced slot's geometry; the slot's state is begun for
                // this ship and gate as site A begins it (its next call keeps it)
                if (const core::Ship* sh = table_.find(obj)) {
                    GateState& gs = gate_[unsigned(sh - table_.entries)];
                    const std::uint32_t id = rd32(obj + sites::kObjId);
                    if (gs.obj != obj || gs.gate != target || gs.id != id) {
                        gs = GateState{};
                        gs.obj = obj;
                        gs.gate = target;
                        gs.id = id;
                    }
                    for (unsigned i = 0; i < 3; ++i) {
                        gs.lane_w.w[i] = units[i];
                        gs.lane_grid[i] = p[i];
                    }
                    gs.lane_w.ok = 1;
                }
                ctl_point_rewrites_.add();
                ctl_defer_rewrites_.add();
                lane_rewrite_row(*g, obj, g->m[ci].r, cur, units, 1, now);
            }
        }
        if (d.write210) {
            wr32(fl + sites::kFlightReplan, std::int32_t(d.v210));
            if (order == core::kOrderNone) ctl_lane_defers_.add();
            lane_defer_row(*g, obj, order, f210, d.v210, order == core::kOrderNone ? 0u : d.rewrite ? 2u : 1u, now); // (item 5)
        }
    }
    float x[3], f[3];
    for (unsigned i = 0; i < 3; ++i) {
        x[i] = float(rds32(ph + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
        f[i] = fixed16(rds32(fl + sites::kFlightBasis + 0x20 + 4 * i));
    }
    float v = 0.0f;
    gc::LaneWrite kind = gc::LaneWrite::none;
    gc::Member& mm = g->m[ci];
    mm.sb_wv = -1.0f;
    mm.sb_wk = 0;
    if (!gc::lane_site_b(*g, mm, order, x, f, float(rds32(obj + sites::kObjSpeed)) / core::kUnitsPerMetre, now, &v, &kind)) return;
    const float u = v * core::kUnitsPerMetre;
    if (!(u >= 0.0f) || u >= 2147483520.0f) return;
    wr32(obj + sites::kObjDesiredSpeed, core::round_to_int(u));
    mm.sb_wv = v;
    mm.sb_wk = std::uint8_t(kind);
    ctl_lane_writes_.add();
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
} // namespace
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
    if (lane_on_ && traffic_live_.load(std::memory_order_relaxed) && ctl_ix_.maybe(obj)) lane_clamp(obj, fl, ph);
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
namespace {
// Run161 item 4 (--debug): at every order-0 frame of a member, the pair's E reason against each live partner within 5 km (the engine's
// warn range of two capitals; kind 2 rows, the ctl_pair ring).
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
// Step 3: the gate sites' handlers. Each runs on the light envelope inside the order handlers of the command pass
// (no log, no allocation, no lock); the stubs pass through to the claim tails when the group is not live.
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
} // namespace
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
        // D.6: a lane member crossing from its final takes the join mode's place in the verdict
        bool sb_funnel = false;
        if (live && traffic_on_ && cap && g->validated == 1) {
            int ci = -1;
            const gc::Gate* cg = ctl_lookup(obj, &ci);
            sb_funnel = cg && cg->gate == tgt && cg->m[ci].id == rd32(obj + sites::kObjId) && gc::sb_funnel(cg->m[ci]);
        }
        const bool join_mode = cap && gates_capital_on_ && g->validated == 1 && (g->join.mode == core::JoinMode::join || sb_funnel);
        float keep_bound = core::join_coast_bound(g->xg_m, g->r_ship, v_ms, g->a_lin);
        if (sb_funnel && lane_on_) { // lane stage 1 (section 6.4): site B's hold and the refusal's brake bound replace the coast bound
            int ci = -1;
            if (const gc::Gate* cg = ctl_lookup(obj, &ci)) keep_bound = gc::lane_p2_bound(cg->m[ci], keep_bound);
        }
        const core::JoinKeep why = core::join_keep_verdict(join_mode, nose_deg, vel_deg, v_ms, keep_bound);
        cap_keep = live && why == core::JoinKeep::kept;
        if (cap_keep && sb_funnel) { // the P2 stub's contract (ship_steering_sites.h (h)): on a kept stage-B hand-over the three turn
            wr32(obj + sites::kObjRemaining, 0);      // remainders obj+0x30/+0x34/+0x38 are zeroed, so order 0 and a first-frame 0x1f
            wr32(obj + sites::kObjRemaining + 4, 0);  // hold the attitude the funnel delivered (ship-navigation-ai.md 10.11 Q4, D.12)
            wr32(obj + sites::kObjRemaining + 8, 0);
            ctl_p2_zeroed_.add();
        }
        g->kept = cap_keep ? 1 : 0;
        g->handover = cap_keep ? 1 : 0; // site E's gate row covers 0x1f / order 0 until order 5 (audit G3)
        // amendment 16 (sections 16.1, 16.3): the hand-over after P2 kept or not (E's gate row under the path guard; the controller's
        // member stays committed on 0x1f / order 0)
        if (live && gates_capital_on_ && cap && g->validated == 1 && g->gate == tgt) {
            g->p2_seen = 1;
            if (traffic_on_) { // the controller: the committed member stays committed through order 0 / 0x1f (kHandoverMaxMs)
                int ci = -1;
                gc::Gate* cg = ctl_lookup(obj, &ci);
                if (cg && cg->gate == g->gate && cg->m[ci].role == gc::Role::committed && cg->m[ci].id == rd32(obj + sites::kObjId)) {
                    const std::uint32_t t = game_time_ms();
                    cg->m[ci].handover_ms = t ? t : 1;
                    cg->m[ci].p2_kept = cap_keep ? 1 : 0; // lane fix round 1 item 3: site B never latches a speed P2 did not keep
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
