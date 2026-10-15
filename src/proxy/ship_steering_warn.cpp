// Ship steering, site E: the warn test's suppression (e_verdict, x3m_ship_avoid_suppress), its controller and lane rows
// (ctl_e_fields, ctl_e_obst), the e_verdict --debug rows (e_record) and the engine warn's rows (warn_pre, engine_warn).
#include "ship_dock_glue.h"
namespace {
std::uint8_t e_gate_why_ = 0xff;  // B.3 item 6: the own-gate row's reason of the current e_verdict call (ctl_e_fields), for e_record
std::int32_t e_gate_age_ = -1;
std::uint8_t e_obst_why_ = 0xff; // lane fix round 2 item 1: the lane obstacle row's reason of the current e_verdict call (gc::ObstRow)
std::uint8_t e_gate_member_why_ = 0xff; // the arrival round item 4: the member row's reason of this call (any trace state)
// The arrival round item 4: the ring row's verdict per ship per game-time frame (two-sided: an entry of another frame, older or newer,
// is not this frame's), and the collision-tree read of gates without a board
struct RingSeen {
    std::uint32_t a = 0, b = 0, now = 0;
    std::uint8_t why = 0;
};
RingSeen ring_seen_[32];
struct RingTree {
    std::uint32_t gate = 0, id = 0, ms = 0;
    std::uint16_t nodes = 0;
    std::uint8_t state = 0;
};
RingTree ring_tree_[8];
unsigned ring_tree_next_ = 0;
constexpr unsigned kRingLogged = 64; // (--debug: the gates with a ship_gate_ring_read row, once per gate)
std::uint32_t ring_logged_[kRingLogged];
unsigned ring_logged_n_ = 0;
struct EVerdictSeen { // the per-pair dedup (one row per pair per second of game time) and the last verdict per traced pair
    std::uint32_t a, b, ms;
    std::uint8_t verdict;
    std::uint8_t gate_row = 0xff; // lane fix round 1 item 5: the own-gate row's reason of that verdict (gc::GateRow; 0xff not evaluated)
    std::uint8_t obst_row = 0xff; // lane fix round 2 item 1: the lane obstacle row's reason of that verdict (gc::ObstRow; 0xff not evaluated)
};
EVerdictSeen e_logged_[kELogged];
EVerdictSeen e_last_[kELast];
unsigned e_last_next_ = 0;
constexpr std::uint32_t kSmallGapMs = 5000;        // ... while the order-4 decision or the lead-in is at most 5 s old
} // namespace
namespace x3m_ship_steering_rt {
// Amendment 15 (section 15.4): the frame after the KC's CollisionWarn ran ([obj+0x40] & 0x3000 both set, the order not yet 0x12;
// L0 of the next collision pass clears the bits): the engine's list with E's verdict per entry, one row per warn (edge-triggered by
// the caller: a traced ship's slot, or amendment 17's capital edge memory).
void push_warn_pre(std::uint32_t obj, std::uint32_t fl, std::uint16_t order_now, std::uint32_t now) noexcept {
    const std::uint32_t pw = warn_pre_write_.load(std::memory_order_relaxed);
    if (pw - warn_pre_read_ < 8) {
        WarnPreRow& p = warn_pre_[pw & 7];
        p = WarnPreRow{};
        p.gate_row = 0xff;
        p.obst_row = 0xff;
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
                if (e_last_[k].a == obj && e_last_[k].b == o && now - e_last_[k].ms <= 1000) {
                    p.verdict[i] = e_last_[k].verdict;
                    if (p.gate_row == 0xff && p.cls[i] == sites::kClassGate) p.gate_row = e_last_[k].gate_row;
                    if (p.obst_row == 0xff && p.cls[i] != sites::kClassGate) p.obst_row = e_last_[k].obst_row;
                }
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
} // namespace x3m_ship_steering_rt
namespace {
// Lane fix round 2 items 1 and 4: A's live velocity (speed x the flight basis' row 2 + the slip) and nose (E's reads of A, the engine's
// pointer of the call), and its avoidance's detour point while that call is within the lane rows' state bound (the module's own state).
inline void e_live_motion(std::uint32_t a, std::uint32_t fl, float u[3], float f[3]) noexcept {
    const float va = float(rds32(a + sites::kObjSpeed)) / core::kUnitsPerMetre;
    for (unsigned i = 0; i < 3; ++i) {
        f[i] = fixed16(rds32(fl + sites::kFlightBasis + 0x20 + 4 * i));
        u[i] = va * f[i] + float(rds32(a + sites::kObjSlip + 4 * i)) / core::kUnitsPerMetre;
    }
}
inline const float* e_detour(std::uint32_t a, const core::Ship* s, std::uint32_t now) noexcept {
    if (!s) return nullptr;
    const av::ShipAvoid& st = avoid_[unsigned(s - table_.entries)];
    const std::int32_t age = std::int32_t(now - st.stamp_ms);
    return st.obj == a && st.obstacle && st.stamp_ms && age >= 0 && age <= std::int32_t(gc::kStateMaxAgeMs + gc::kStateMs + frame_step_ms()) ? st.P
                                                                                                                                           : nullptr;
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
            float u[3], f[3]; // lane fix round 2 item 4: the lane row's live velocity and nose, A's fresh detour
            e_live_motion(a, fl, u, f);
            in.ctl_gate = gc::e_gate_row(ctl_, ctl_ix_, a, rd32(a + sites::kObjId), b, in.a_order, in.a_target, x, pa != 0, now, &why, u, f,
                                         e_detour(a, table_.find(a), now));
            e_gate_member_why_ = std::uint8_t(why);
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
        e_last_[k] = EVerdictSeen{a, b, now, std::uint8_t(v), e_gate_why_, e_obst_why_};
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
    e_logged_[k] = EVerdictSeen{a, b, now, std::uint8_t(v), e_gate_why_, e_obst_why_};
    EVerdictRow& r = e_rows_[w & (kERows - 1)];
    r = EVerdictRow{};
    r.a = a;
    r.b = b;
    r.verdict = std::uint8_t(v);
    r.gate_row = e_gate_why_;
    r.gate_age_ms = e_gate_age_;
    r.obst_row = e_obst_why_;
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
// Lane fix round 2 item 1 (gc::lane_obst_why): site E's lane obstacle row for a lane member A against B, a station (class 5 / 6, its part
// spheres from the walk's part cache when they lie within B's live bounding sphere, else the sphere), an asteroid (its sphere) or a ship
// that is not A's board partner (its hull capsule from the type widths, its velocity). Reads: A and B (the engine's pointers of this call)
// and their flight and physics blocks, the type table, the module's own part cache and A's avoidance state (its fresh detour); never the
// player's ship or a ship under manual control. B.5: no other object is read.
inline void ctl_e_obst(std::uint32_t a, std::uint32_t fl, std::uint32_t b, std::uint16_t cls_b, const core::Ship* s, std::uint32_t now,
                       av::SiteE& in) noexcept {
    if (!(cls_b == sites::kClassShip || cls_b == sites::kClassStationA || cls_b == sites::kClassStationB || cls_b == sites::kClassAsteroid)) return;
    if (!ctl_ix_.maybe(a)) return;
    // (round 2 review item 4: never the player's ship as B, the member exclusion's flag test)
    if (cls_b == sites::kClassShip && (rd32(b + sites::kObjFlags) & sites::kFlagPlayerObject)) return;
    const std::uint32_t flags = rd32(a + sites::kObjFlags);
    const bool manual = (flags & (sites::kFlagPlayerObject | sites::kFlagPlayerControl)) || !(flags & sites::kFlagAutopilot);
    const std::uint32_t pa = rd32(a + sites::kObjPhys), pb = rd32(b + sites::kObjPhys);
    if (!pa || !pb) return;
    float x[3], u[3], fa[3];
    e_live_motion(a, fl, u, fa);
    for (unsigned i = 0; i < 3; ++i) x[i] = float(rds32(pa + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
    gc::ObstBound o;
    o.r = float(rds32(b + sites::kObjRadius)) / core::kUnitsPerMetre;
    for (unsigned i = 0; i < 3; ++i) o.c[i] = float(rds32(pb + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
    if (cls_b == sites::kClassShip) {
        const std::uint32_t flb = rd32(b + sites::kObjFlight);
        if (!flb) return;
        const float vb = float(rds32(b + sites::kObjSpeed)) / core::kUnitsPerMetre;
        for (unsigned i = 0; i < 3; ++i) {
            o.f[i] = fixed16(rds32(flb + sites::kFlightBasis + 0x20 + 4 * i));
            o.v[i] = vb * o.f[i] + float(rds32(b + sites::kObjSlip + 4 * i)) / core::kUnitsPerMetre;
        }
        float w = 0.0f, l = 0.0f;
        if (type_width_get(std::int16_t(rd16(b + sites::kObjType)), &w, &l)) gc::obst_hull(o, w, l); // (round 2 review item 3)
    } else if (cls_b != sites::kClassAsteroid) { // a station: the walk's part spheres, only when they lie within its live sphere
        for (unsigned k = 0; k < part_cache_.count; ++k) {
            const av::PartSet& ps = part_cache_.sets[k];
            if (ps.station != b || !ps.count) continue;
            bool inside = true;
            for (unsigned q = 0; q < ps.count && inside; ++q) {
                const float d[3] = {ps.c[q][0] - o.c[0], ps.c[q][1] - o.c[1], ps.c[q][2] - o.c[2]};
                const float lim = o.r * 1.01f + 1.0f - ps.r[q];
                inside = lim > 0.0f && core::dot3(d, d) <= lim * lim;
            }
            if (inside) {
                o.pc = ps.c;
                o.pr = ps.r;
                o.np = ps.count;
            }
            break;
        }
    }
    const float* detour = e_detour(a, s, now); // (A's avoidance detour within the row's state bound, else the board's rollout alone)
    gc::ObstRow why = gc::ObstRow::no;
    in.lane_obst = gc::e_obst_row(ctl_, ctl_ix_, a, rd32(a + sites::kObjId), in.a_order, in.a_target, x, u, true, o, detour, manual, now, &why);
    e_obst_why_ = std::uint8_t(why);
    if (why == gc::ObstRow::roll) e_lane_obst_roll_.add();
}
// The arrival round item 4 (convoy-gate-transit.md section 12.6): the gate's collision tree, once per gate (its board's Gate::ring, else
// the small cache by pointer and id): kGateRingNone, kGateRingTree, or 3 (read, unknown: fails closed to the ring). The first verdict is
// kept for the gate's life (ship-navigation-ai.md section 10.9: the tree is complete before the gate is linked; no rebinding of a gate's
// scene after creation was found, 0x004506d0 is class 6 only). B is range- and alignment-checked before its id is read (wk::ptr_ok; the
// walk checks every node). --debug: one ship_gate_ring_read row per gate (the read's state, its guard, the nodes visited, the scene id
// seen, the TGates row), at most kRingLogged gates per session.
inline void ring_read_row(std::uint32_t b, std::uint32_t id, const wk::GateTreeRead& rd, bool board) noexcept {
    for (unsigned k = 0; k < ring_logged_n_; ++k)
        if (ring_logged_[k] == b) return;
    if (ring_logged_n_ >= kRingLogged) return;
    ring_logged_[ring_logged_n_++] = b;
    const std::uint32_t w = ring_read_write_.load(std::memory_order_relaxed);
    if (w - ring_read_read_ >= 8) {
        row_dropped(Drop::e_ring);
        return;
    }
    const std::uint32_t t = game_time_ms();
    RingReadRow& r = ring_read_rows_[w & 7];
    r = RingReadRow{t, b, id, rd.scene, rd16(b + sites::kObjType), rd.nodes, rd.state, std::uint8_t(rd.why), rd.seen,
                    std::uint8_t(board ? 1 : 0)};
    ring_read_write_.store(w + 1, std::memory_order_release);
    // Run175 round item 6: the visited nodes' fields, once per gate with more than kRingNodeRowsMin nodes (the ring holds one read)
    if (rd.out && rd.nodes > kRingNodeRowsMin) {
        const unsigned n = rd.nodes < rd.cap ? rd.nodes : rd.cap;
        std::uint32_t wn = ring_node_write_.load(std::memory_order_relaxed);
        for (unsigned i = 0; i < n; ++i, ++wn) {
            if (wn - ring_node_read_ >= kRingNodeRows) {
                row_dropped(Drop::e_ring);
                break;
            }
            const wk::GateTreeNode& nd = rd.out[i];
            ring_node_rows_[wn % kRingNodeRows] = RingNodeRow{t, b, nd.scene, nd.f25c, nd.f140, nd.flags, std::uint16_t(i), nd.depth};
        }
        ring_node_write_.store(wn, std::memory_order_release);
    }
}
// Run175 round item 6: a gate's tree re-read every kRingRereadMs + the step (on the warn path, the live gate pointer): the node count
// before and after and a row; the verdict stays the first one unless a tree appears (then the ring: fail closed). Returns the state to keep.
inline std::uint8_t ring_reread(std::uint32_t b, std::uint32_t id, std::uint8_t state, std::uint16_t* nodes, std::uint32_t* ms, std::uint32_t now) noexcept {
    if (std::int32_t(now - *ms) < std::int32_t(kRingRereadMs + frame_step_ms())) return state;
    *ms = now ? now : 1;
    RawMem mem;
    const wk::GateTreeRead rd = wk::gate_tree_read(mem, b);
    // (review: a tree or an unknown read supersedes a first "none", fail closed; a "none" never lifts a first tree or unknown)
    const std::uint8_t st = rd.state ? rd.state : 3u, keep = st == std::uint8_t(gc::kGateRingNone) ? state : st;
    const std::uint8_t changed = rd.nodes != *nodes || st != state ? 1 : 0;
    if (debug_) {
        const std::uint32_t w = ring_reread_write_.load(std::memory_order_relaxed);
        if (w - ring_reread_read_ < 8) {
            ring_reread_rows_[w & 7] = RingRereadRow{now, b, id, *nodes, rd.nodes, state, st, changed};
            ring_reread_write_.store(w + 1, std::memory_order_release);
        } else
            row_dropped(Drop::e_ring);
    }
    *nodes = rd.nodes;
    return keep;
}
inline wk::GateTreeRead ring_first_read(std::uint32_t b, bool board, std::uint32_t id) noexcept { // (the node records under --debug)
    RawMem mem;
    if (!debug_) return wk::gate_tree_read(mem, b);
    wk::GateTreeNode nodes[kRingNodeRows];
    const wk::GateTreeRead rd = wk::gate_tree_read(mem, b, nodes, kRingNodeRows);
    ring_read_row(b, id, rd, board);
    return rd;
}
inline unsigned gate_ring_state(std::uint32_t b) noexcept {
    if (!wk::ptr_ok(b, wk::kGateObjBytes)) return 3u;
    const std::uint32_t now = game_time_ms();
    if (gc::Gate* g = gc::gate_of(ctl_, b)) {
        if (!g->ring) {
            const wk::GateTreeRead rd = ring_first_read(b, true, rd32(b + sites::kObjId));
            g->ring = std::uint8_t(rd.state ? rd.state : 3u);
            g->ring_nodes = rd.nodes;
            g->ring_ms = now ? now : 1;
        } else
            g->ring = ring_reread(b, rd32(b + sites::kObjId), g->ring, &g->ring_nodes, &g->ring_ms, now); // (item 6: every 60 s + the step)
        return g->ring;
    }
    const std::uint32_t id = rd32(b + sites::kObjId);
    for (RingTree& t : ring_tree_)
        if (t.gate == b && t.id == id) {
            t.state = ring_reread(b, id, t.state, &t.nodes, &t.ms, now);
            return t.state;
        }
    const wk::GateTreeRead rd = ring_first_read(b, false, id);
    RingTree& t = ring_tree_[ring_tree_next_++ & 7u];
    t = RingTree{b, id, now ? now : 1, rd.nodes, std::uint8_t(rd.state ? rd.state : 3u)};
    return t.state;
}
// The ring row (gc::gate_ring_why) for A against the gate B on any order: never the player's ship or a ship under manual control, never A
// on the engine's own avoid order; a committed member only where its own row did not decide (not_member, order, stage). Reads: A's
// position, velocity, nose, type and radius, its order's target (the leader's position on 0x14 / 0x15, f+0x240 on order 4 to B), B's frame,
// row and scene nodes; A's a_lin from its table entry (none: fail closed on a gate with a solid). Cached per ship per game-time frame.
inline void e_gate_ring(std::uint32_t a, std::uint32_t fl, std::uint32_t b, const core::Ship* s, std::uint32_t now, av::SiteE& in) noexcept {
    const std::uint32_t flags = rd32(a + sites::kObjFlags);
    if ((flags & (sites::kFlagPlayerObject | sites::kFlagPlayerControl)) || !(flags & sites::kFlagAutopilot) || in.a_order == core::kOrderAvoid) return;
    const std::uint8_t mw = e_gate_member_why_;
    if (mw != 0xff && !(mw == std::uint8_t(gc::GateRow::not_member) || mw == std::uint8_t(gc::GateRow::order) || mw == std::uint8_t(gc::GateRow::stage)))
        return;
    RingSeen& c = ring_seen_[(a >> 4) & 31u];
    gc::GateRow why = gc::GateRow::roll;
    if (c.a == a && c.b == b && c.now == now)
        why = gc::GateRow(c.why);
    else {
        const std::uint32_t pa = rd32(a + sites::kObjPhys);
        float G[3], R[3], U[3], F[3], r_gate = 0.0f;
        if (pa && gate_frame(b, G, R, U, F, &r_gate)) {
            gc::GateFrame fr;
            for (unsigned i = 0; i < 3; ++i) {
                fr.G[i] = G[i];
                fr.R[i] = R[i];
                fr.U[i] = U[i];
                fr.F[i] = F[i];
            }
            float x[3], u[3], f[3], tp[3];
            for (unsigned i = 0; i < 3; ++i) x[i] = float(rds32(pa + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
            e_live_motion(a, fl, u, f);
            const float* tgt = nullptr;
            const bool esc = in.a_order == core::kOrderEscort || in.a_order == core::kOrderFollow;
            if (esc && in.a_target) {
                const std::uint32_t pt = rd32(in.a_target + sites::kObjPhys);
                if (pt) {
                    for (unsigned i = 0; i < 3; ++i) tp[i] = float(rds32(pt + sites::kPhysPosition + 4 * i)) / core::kUnitsPerMetre;
                    tgt = tp;
                }
            } else if (in.a_order == core::kOrderFlyBefore && in.a_target == b) {
                for (unsigned i = 0; i < 3; ++i) tp[i] = float(rds32(fl + sites::kFlightPoint + 4 * i)) / core::kUnitsPerMetre;
                tgt = tp;
            }
            const float r_a = float(rds32(a + sites::kObjRadius)) / core::kUnitsPerMetre;
            float w = 0.0f, l = 0.0f;
            if (!type_width_get(std::int16_t(rd16(a + sites::kObjType)), &w, &l)) w = l = 0.0f;
            const float LA = l > r_a ? l : r_a, wA = w > 0.0f && w <= r_a ? w : r_a;
            const bool arr = arr_ix_.maybe(a) && gc::find_arrival(arrivals_, a, now) != nullptr;
            const gc::GateRow kind = esc ? gc::GateRow::escort : arr ? gc::GateRow::arrival : gc::GateRow::other;
            const float step_s = float(std::int32_t(frame_step_ms())) * 0.001f; // (review item 5: the clamped game step; via int, x87 audit)
            why = gc::gate_ring_why(fr, r_gate, rd16(b + sites::kObjType), gate_ring_state(b), kind, x, u, f, tgt, LA, wA, s ? s->a_lin : 0.0f, step_s);
        }
        c = RingSeen{a, b, now, std::uint8_t(why)};
    }
    e_gate_why_ = std::uint8_t(why);
    in.gate_ring = gc::gate_row_suppresses(why);
    if (in.gate_ring) e_gate_ring_.add();
    else e_gate_ring_kept_.add();
}
inline int e_verdict(std::uint32_t a, std::uint32_t b, std::uint32_t now, EV* out) noexcept {
    *out = EV::none;
    e_gate_why_ = 0xff;
    e_obst_why_ = 0xff;
    e_gate_member_why_ = 0xff;
    if (!a || !b || rd16(a + sites::kObjClass) != sites::kClassShip) return 0;
    const std::uint32_t fl = rd32(a + sites::kObjFlight);
    if (!fl) return 0;
    av::SiteE in;
    in.a_order = rd16(fl + sites::kFlightOrder);
    in.a_target = rd32(fl + sites::kFlightTarget);
    const core::Ship* s = table_.find(a);
    in.a_in_table = s != nullptr;
    const std::uint16_t cls_b = rd16(b + sites::kObjClass);
    // review of the arrival round item 2: the player's ship as B is never suppressed (every row; the engine's warn stands)
    in.b_player = cls_b == sites::kClassShip && (rd32(b + sites::kObjFlags) & sites::kFlagPlayerObject) != 0;
    if (traffic_live_.load(std::memory_order_relaxed)) { // the gate traffic controller's pairs need no steering decision of A
        ctl_e_fields(a, fl, b, cls_b, now, in);
        if (lane_on_ && cls_b == sites::kClassGate && !in.arrival_gate && !in.ctl_gate) e_gate_ring(a, fl, b, s, now, in); // the arrival round item 4
        if (((in.arrival_gate || in.ctl_gate || in.gate_ring) && cls_b == sites::kClassGate) || (in.ctl_pair && cls_b == sites::kClassShip)) {
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
        if (lane_on_ && cls_b != sites::kClassGate) ctl_e_obst(a, fl, b, cls_b, s, now, in); // lane fix round 2 item 1
        if (in.lane_obst) {
            const EV v = av::site_e_verdict(in);
            *out = v;
            if (v == EV::suppress_lane_obst) {
                e_lane_obst_.add();
                return 1;
            }
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
} // namespace
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
