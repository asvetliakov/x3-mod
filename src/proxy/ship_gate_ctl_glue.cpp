// The gate traffic controller's glue: the commit and escort entries site A calls, the arrival handler (site T), the frame
// (ctl_frame: the members' rows, the lane frame, the live proof) and the --debug controller rows.
#include "ship_gate_ctl_glue.h"
namespace {
std::int32_t width_census_types_[32];
unsigned width_census_count_ = 0;
unsigned ctl_cursor_ = 0;
std::uint32_t arr_ix_rebuilt_ms_ = 0;
gc::LiveTrack live_track_;
gc::LiveOut live_out_;
inline void type_width_put(std::int32_t type, float w, float l) noexcept {
    for (unsigned i = 0; i < type_width_count_; ++i)
        if (type_widths_[i].type == type) {
            if (type_widths_[i].w != w || type_widths_[i].l != l) ++type_width_gen_;
            type_widths_[i].w = w;
            type_widths_[i].l = l;
            return;
        }
    if (type_width_count_ < 32) {
        type_widths_[type_width_count_++] = TypeWidth{type, w, l};
        ++type_width_gen_;
    }
}
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
struct MergeRow { // lane stage 1 (--debug): a merge's start and completion (ship_gate_ctl_merge)
    std::uint32_t gate, t_ms;
    gc::LaneEvent e;
};
MergeRow merge_rows_[8];
std::atomic<std::uint32_t> merge_row_write_{0};
std::uint32_t merge_row_read_ = 0;
// B.4 item 2: ship_gate_ctl_member (--debug): one row per board member (pair role) per game second, traced or not (ring 32; drops
// count as ctl, as the seq rows). The position and the nose are the member's last state read (site B), in the gate frame (R, U, F; m).
struct MemberRow {
    std::uint32_t gate, ship, t_ms, target, key, code;
    std::int32_t st_age_ms, dec_age_ms;
    std::uint16_t order;
    std::uint8_t role, stage, traced;
    float p[3], nose_f, v;
    std::uint8_t live; // B.5: the member passed the liveness proof this frame (0: no engine field read; class=?)
    float zx; // X along F from the stored board state (B.8)
    // lane stage 1 (convoy-gate-transit.md section 6.9; printed under ship_steering_gates_lane only): along F, the lane rank (255 none),
    // the gap to the ship ahead (m; < -1e30 none), the law
    std::uint8_t lane, rank, law;
    float lane_s, gap;
    // review item 9: the lateral distance from Lambda (m), Lambda's l_c (R, U; m), the law's speed and site B's last write (m/s; < 0
    // none) with its kind, on Lambda, merged
    float lat, lc[2], v_law, v_w;
    std::uint8_t w_kind, on, merged;
    std::uint8_t loop; // review of rounds A-C item 9: the loop-back join (gc::lane_loop_state: none, leg, out, turn)
    float w;           // Run175 round item 2: the hull's lateral half-width (m) and whether it came from a read; item 3: inside the join funnel
    std::uint8_t w_ok, funnel;
    std::uint8_t pt, own; // item 1: the member stands on an unrewritten script point (its lateral the lane's); the lane owns it (lane_laws)
};
MemberRow member_rows_[32];
std::atomic<std::uint32_t> member_row_write_{0};
std::uint32_t member_row_read_ = 0;
std::uint32_t member_row_ship_[gc::kGates][gc::kMembers] = {}, member_row_ms_[gc::kGates][gc::kMembers] = {}; // the last row per slot
static_assert(pow2(sizeof ctl_rows_ / sizeof ctl_rows_[0]) && pow2(sizeof ctl_pair_rows_ / sizeof ctl_pair_rows_[0]) &&
                  pow2(sizeof launch_rows_ / sizeof launch_rows_[0]) &&
                  pow2(sizeof member_rows_ / sizeof member_rows_[0]),
              "masked rings are powers of two");
// Review of rounds A-C item 8: the launch hook's decisions (outside ctl_frame) charged to the next frame: their measured time against the
// 1 ms ceiling and the frame's figures, their rollouts against the frame's budget, their count in its decisions
std::uint64_t launch_pend_ticks_ = 0;
unsigned launch_pend_decisions_ = 0, launch_pend_rollouts_ = 0;
// The pair rows (--debug) of member i's row of the matrix after its decision (ctl_frame's, and the launch hook's first decision of an
// arrival; review of rounds A-C item 8: before, the hook's verdicts produced no row, and an "unresolved" there that the next decision
// cleared stayed invisible): a row when a pair's verdict differs from the last one logged, at most once per pair per second; the edges worth
// a row are into or out of a hand-back and a certificate lost (clear -> none); a first certificate (none -> clear) is recorded silently.
// Reads the board only.
void ctl_pair_rows_of(gc::Gate& g, unsigned i, std::uint32_t now) noexcept {
    const gc::Member& m = g.m[i];
    for (unsigned j = 0; j < g.n; ++j) {
        if (j == i || g.v[i][j] == g.logged[i][j] || (g.logged_ms[i][j] && now - g.logged_ms[i][j] < 1000)) continue;
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
        const std::uint8_t y = m.cap >= 0.0f && m.yield_ms == now ? 1 : b.cap >= 0.0f && b.yield_ms == now ? 2 : 0;
        ctl_pair_rows_[w & 63] = CtlPairRow{g.gate, m.ship, b.ship, now, m.seq, b.seq, g.v[i][j], g.logged[i][j], g.tc[i][j], y,
                                            std::uint8_t(gc::rank(m)), std::uint8_t(gc::rank(b)), 0, 0, g.why[i][j],
                                            y == 1 ? m.cap : y == 2 ? b.cap : -1.0f, core::sqrt_ss(core::dot3(dx, dx)), gc::pair_gap(g, i, j)};
        ctl_pair_write_.store(w + 1, std::memory_order_release);
        g.logged[i][j] = g.logged[j][i] = g.v[i][j];
        g.logged_ms[i][j] = g.logged_ms[j][i] = now ? now : 1;
    }
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
// ---- the hull width read (capital-gate-transit.md section 16.1; the controller's member hulls) --------------------------------
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
} // namespace
namespace x3m_ship_steering_rt {
// The gate traffic controller's commit refresh (the amendment-16 lane board's commit
// test: an order-4 / order-5 decision on the validated capital grid geometry of the target gate): the capital's member entry on that
// gate's board (role committed), its hull (w from the width read, w = r when unread; L = max(r, l)), the order-4 line (X, h) and the
// plant parameters for its rollout, and this decision's state (site B re-reads it every kStateMs).
gc::Member* ctl_commit(GateState& g, std::uint32_t obj, const core::Inputs& in, const core::Dynamics& dyn, const core::GateGeometry* geo,
                       const float G[3], const float R[3], const float U[3], const float F[3], float r_gate, std::uint32_t gate,
                       std::uint32_t now) noexcept {
    width_ensure(g, obj, g.join_code, dyn.r_own, now);
    gc::Gate* cg = gc::acquire_gate(ctl_, gate, now, G, R, U, F, r_gate, rd16(gate + sites::kObjType));
    if (!cg) return nullptr;
    // lane fix round 1 item 4 (Run173: E1 re-committed 15 times in 1.7 s, why=target): a ship that left on Why::target re-commits at most
    // once per kRecommitMs + the step (a committed member's refresh is never held); lane boards only
    if (cg->lane && !gc::recommit_ok(ctl_, *cg, obj, now)) {
        ctl_recommit_held_.add();
        return nullptr;
    }
    gc::Member* m = ctl_enter(*cg, obj, g.id, gc::Role::committed, gc::Why::commit, now);
    if (!m) return nullptr;
    // (review of the Run175 round: w = r after a failed read is no read: 0 unless the source is the LOD read, so a committed member whose
    // width is unread is a body, never w = r as a member)
    const bool w_read = g.w_src == std::uint8_t(ln::WidthSrc::lod);
    gc::commit_fill(*m, in, dyn, geo, w_read ? g.w : 0.0f, w_read ? g.w_len : 0.0f, r_gate, gate, now);
    return m;
}
// An escort (D.1): a capital on 0x14 / 0x15 whose leader is a committed member; it joins the leader's board (role escort). On order 0
// it stays through the formation memory (the 3 s order-0 gaps): ctl_observe. Run175 round item 2: its hull from its own width read at
// entry (width_ensure, the commit path's reader; Run175: the type's width was never read before the escort's entry, w = r = 755 m against
// 454 m made the certificate see contact with the leader's lane 1.6 km away, the pair stayed unresolved and the engine's warn stood);
// until the read succeeds the width is unread (w_ok 0: a body for the sweep with its sphere, no lane law or steer), never w = r as a member.
void ctl_escort(GateState& g, std::uint32_t obj, std::uint32_t code, std::uint32_t leader, const core::Inputs& in, const core::Dynamics& dyn,
                std::uint32_t now) noexcept {
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
    if (X3M_LANE_W_ENTRY) {
        width_ensure(g, obj, code, dyn.r_own, now);
        if (g.w_src == std::uint8_t(ln::WidthSrc::lod)) {
            w = g.w;
            l = g.w_len;
        } else if (!type_width_get(std::int16_t(rd16(obj + sites::kObjType)), &w, &l)) // (a read that failed: the type's last read)
            w = l = 0.0f;
    } else if (!type_width_get(std::int16_t(rd16(obj + sites::kObjType)), &w, &l))
        w = l = 0.0f;
    gc::escort_fill(*m, leader, in, dyn, w, l, cg->r_gate, now);
}
} // namespace x3m_ship_steering_rt
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
    float w = r, w_mem = 0.0f, l_mem = 0.0f; // (w: the placement's, r unread; w_mem, l_mem: the member's, 0 unread)
    if (capital) {
        float tw = 0.0f, tl = 0.0f;
        if (type_width_get(stype, &tw, &tl) && tw > 0.0f && tw <= r) {
            w = w_mem = tw;
            l_mem = tl > r ? tl : r;
        } else if (X3M_LANE_W_ENTRY) { // Run175 round item 2: the width read at the arrival's entry (the commit path's reader, once;
                                       // a failed read leaves the member's width unread: a body for the sweep, no lane law)
            ln::WidthSrc src = ln::WidthSrc::unread;
            float len = 0.0f;
            wk::HullWidth hw;
            const float rw = capital_half_width(obj, r, &src, &len, &hw);
            if (src == ln::WidthSrc::lod && rw > 0.0f && core::finite_f(rw) && rw <= r) {
                w = w_mem = rw;
                l_mem = len > r ? len : r;
                if (traffic_on_) type_width_put(stype, rw, len);
            }
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
            // review of rounds A-C item 1: the plant from the same read as a commit's (gc::params_from through gc::arrival_fill), so the
            // 0x1e leg's order-0 brake rolls out at the hull's a_lin (before: JoinParams{}, the 1 m/s^2 fallback); the state at P facing
            // out of the gate face
            core::Dynamics dyn;
            const std::int32_t rot_pct = table && stype >= 0 ? rds32(table + std::uint32_t(stype) * sites::kRecordStride + sites::kRecordRotAccel) : 0;
            read_dynamics(obj, rd32(obj + sites::kObjFlight), rot_pct, code, dyn);
            dyn.r_own = r;
            gc::arrival_fill(*m, dyn, w_mem, l_mem, r_gate, P, P, R, U, F, gate, now);
            // round A (convoy-gate-transit.md section 12.1a): the arrival's first decision at the launch itself (its rollout on 0x21 and the
            // sweeps against the board), so a departing member's site E verdict against it on the launch frame finds the pair certified
            // or handed back, never without a rollout (cause=zero: measured at a SETA step in the fixture's arrival_pass, the leader's
            // eviction 0.4 s after the launch). One rollout and up to 23 sweeps once per capital launch
            gc::DecideStats ds;
            const std::uint64_t td = timing_ || ctl_ceiling_ticks_ ? qpc() : 0;
            gc::decide(*g, unsigned(m - g->m), now, &ds, nullptr, nullptr);
            if (td) { // review of rounds A-C item 8: the decision's time in ctl_decide_max and charged to the next frame (ctl_frame)
                const std::uint64_t dt = qpc() - td;
                const std::uint32_t ticks = dt > 0xffffffffull ? 0xffffffffu : std::uint32_t(dt);
                if (timing_ && ticks > ctl_decide_max_ticks_) ctl_decide_max_ticks_ = ticks;
                launch_pend_ticks_ += dt;
            }
            ++launch_pend_decisions_;
            launch_pend_rollouts_ += ds.rollouts;
            if (debug_) ctl_pair_rows_of(*g, unsigned(m - g->m), now); // (its verdicts' rows, as a frame decision's)
            ctl_decisions_.add();
            ctl_rollouts_.add(ds.rollouts);
            ctl_sweeps_.add(ds.sweeps);
            ctl_narrow_.add(ds.narrow);
            if (ds.conflicts) ctl_conflicts_.add(ds.conflicts);
            if (ds.yields) ctl_yields_.add(ds.yields);
            if (ds.unresolved) ctl_unresolved_.add(ds.unresolved);
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
namespace {

// The gate traffic controller's frame work (D.1, D.8): member decisions from their stored states (never in a handler), round robin over
// every board, within the frame bound (gc::frame_room: 6 decisions, 14 rollouts); a member decides at most every kDecideMs and only on a state newer than its
// last rollout. A pair row (--debug) when a pair's verdict differs from the last one logged, at most once per pair per second.
// The lane's frame per lane gate (gc::lane_frame), its counters and its merge rows; the key off runs none (the stage-A certificate).
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
        const float d[3] = {m.st.x[0] - g.G[0], m.st.x[1] - g.G[1], m.st.x[2] - g.G[2]};
        r.p[0] = core::dot3(d, g.R);
        r.p[1] = core::dot3(d, g.U);
        r.p[2] = core::dot3(d, g.F);
        r.nose_f = core::dot3(m.st.rows[2], g.F);
        r.v = m.st.v;
        r.zx = m.zx; // B.8: the board's own fields (no engine read)
        r.lane = g.lane;
        r.rank = m.lane_rank;
        r.law = std::uint8_t(m.law);
        r.lane_s = m.lane_s;
        r.gap = m.lane_gap < 1e30f ? m.lane_gap : -3.4e38f;
        r.lat = m.lane_lat;
        r.lc[0] = g.lane_c[0];
        r.lc[1] = g.lane_c[1];
        r.v_law = m.v_sb;
        r.v_w = m.sb_wv;
        r.w_kind = m.sb_wk;
        r.on = m.lane_on;
        r.merged = m.merge_done;
        r.loop = std::uint8_t(gc::lane_loop_state(m));
        r.w = m.w;
        r.w_ok = m.w_ok;
        r.funnel = m.funnel;
        r.pt = m.pt_ok;
        r.own = gc::lane_laws(m) ? 1 : 0;
        member_row_write_.store(w + 1, std::memory_order_release);
    }
}
// Lane stage 1 (convoy-gate-transit.md, ship_steering_gates_lane): gc::lane_frame for a lane gate (the board's own state only, no engine
// read), its counters and the merge rows (--debug).
void lane_frame_gate(gc::Gate& g, std::uint32_t now) noexcept {
    gc::LaneStats st;
    const unsigned stops0 = g.lane_stops;
    gc::lane_frame(g, now, &st);
    if (st.merges) ctl_merge_yield_.add(st.merges);
    if (g.lane_stops != stops0) ctl_merge_stop_.add(g.lane_stops - stops0);
    if (st.x_refused) ctl_x_refused_.add(st.x_refused);
    if (st.clock_resets) ctl_clock_resets_.add(st.clock_resets);
    if (st.mouth_expired) ctl_mouth_expired_.add(st.mouth_expired);
    ctl_x_wait_ms_ += st.x_wait_ms;
    if (st.ev_dropped) for (unsigned k = 0; k < st.ev_dropped; ++k) row_dropped(Drop::ctl);
    if (!debug_) return;
    for (unsigned k = 0; k < st.n_ev; ++k) {
        const std::uint32_t w = merge_row_write_.load(std::memory_order_relaxed);
        if (w - merge_row_read_ >= 8) {
            row_dropped(Drop::ctl);
            break;
        }
        merge_rows_[w & 7] = MergeRow{g.gate, now, st.ev[k]};
        merge_row_write_.store(w + 1, std::memory_order_release);
    }
}
// Convoy-gate-transit.md section 12.3 (round B): the lane's lateral profile for statics on Lambda, rebuilt once per game second
// (gc::lane_profile_build) from the newest snapshot that lists the gate (this frame's walks; within kLaneProfMaxAgeMs + the step); with
// none, a profile older than that is dropped (the members' sphere detour as before). Reads the module's snapshot and part cache only.
void lane_profile_gate(gc::Gate& g, std::uint32_t now) noexcept {
    if (g.prof.built_ms && now - g.prof.built_ms < gc::kLaneProfPeriodMs) return;
    const av::Snapshot* snap = nullptr;
    for (unsigned i = 0; i < sites::kSnapshotSlots; ++i) {
        const av::Snapshot& s = snaps_[i];
        if (!s.count || now - s.epoch > gc::age_bound(g, gc::kLaneProfMaxAgeMs) || (snap && s.epoch <= snap->epoch)) continue;
        for (unsigned k = 0; k < s.count; ++k)
            if (s.items[k].ptr == g.gate) {
                snap = &s;
                break;
            }
    }
    if (!snap) {
        if (g.prof.built_ms && now - g.prof.built_ms > gc::age_bound(g, gc::kLaneProfMaxAgeMs)) g.prof = gc::LaneProfile{};
        return;
    }
    const std::uint64_t t0 = timing_ ? qpc() : 0;
    gc::LaneBody bodies[gc::kLaneProfScan];
    bool overflow = false;
    const unsigned n = gc::lane_bodies(g, *snap, part_cache_.sets, part_cache_.count, bodies, gc::kLaneProfScan, &overflow);
    gc::lane_profile_build(g, bodies, n, overflow, now);
    ctl_prof_builds_.add();
    if (g.prof.n) ctl_prof_shifted_.add();
    if (timing_) {
        const std::uint64_t dt = qpc() - t0;
        const std::uint32_t ticks = dt > 0xffffffffull ? 0xffffffffu : std::uint32_t(dt);
        if (ticks > ctl_prof_max_ticks_) ctl_prof_max_ticks_ = ticks;
    }
}
void lane_frame_all(std::uint32_t now) noexcept {
    const std::uint64_t t0 = timing_ ? qpc() : 0;
    for (gc::Gate& g : ctl_.g) {
        if (!g.gate) continue;
        if (g.lane) { // ship_steering_gates_lane; the key off: no stage frame (stage B was deleted on 2026-10-15)
            lane_profile_gate(g, now);
            lane_frame_gate(g, now);
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
} // namespace
namespace x3m_ship_steering_rt {
void ctl_frame() noexcept {
    const std::uint32_t now = game_time_ms();
    const std::uint32_t step = frame_step_ms();
    gc::note_step(ctl_, now, step); // B.3 item 1
    ctl_live_prove(now);            // B.5
    lane_frame_all(now);
    ctl_live_leaves(now);
    std::uint64_t t0 = timing_ || ctl_ceiling_ticks_ ? qpc() : 0;
    unsigned done = 0, rollouts = 0;
    // review of rounds A-C item 8: the launch hook's decisions since the last frame count against this one (their time against the ceiling,
    // their rollouts against the budget); the first regular decision still runs (frame_room's), so the funnel's occupants are decided
    const unsigned launch_dec = launch_pend_decisions_;
    if (t0 && launch_pend_ticks_ < t0) t0 -= launch_pend_ticks_;
    rollouts = launch_pend_rollouts_;
    launch_pend_ticks_ = 0;
    launch_pend_decisions_ = launch_pend_rollouts_ = 0;
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
        if (debug_) ctl_pair_rows_of(g, i, now);
    }
    if (timing_ && (done || launch_dec)) {
        const std::uint64_t dt = qpc() - t0;
        const std::uint32_t ticks = dt > 0xffffffffull ? 0xffffffffu : std::uint32_t(dt);
        if (ticks > ctl_frame_max_ticks_) ctl_frame_max_ticks_ = ticks;
    }
    if (done + launch_dec > ctl_frame_max_decisions_) ctl_frame_max_decisions_ = done + launch_dec;
    live_set_.clear(); // B.5: the next update fills the next frame's set
    if (now - arr_ix_rebuilt_ms_ >= 1000) { // the arrival bitmap keeps only the records of the last 60 s
        arr_ix_rebuilt_ms_ = now;
        for (std::uint32_t& w : arr_ix_.bits) w = 0;
        for (const gc::Arrival& a : arrivals_.a)
            if (a.ship && now - a.launch_ms <= gc::kArrivalWindowMs) arr_ix_.set(a.ship);
    }
}
// The --debug rows of the controller (D.7), drained at the frame boundary.
void ctl_rows(unsigned long long frame) noexcept {
    static const char* const kLaunchKind[4] = {"vanilla", "capital_offset", "small_side", "small_unseparated"};
    static const char* const kYielder[3] = {"none", "a", "b"};
    for (std::uint32_t w = ctl_row_write_.load(std::memory_order_acquire); ctl_row_read_ != w; ++ctl_row_read_) {
        const CtlRow& r = ctl_rows_[ctl_row_read_ & 31];
        x3m::log("ship_gate_ctl frame=%llu t_ms=%lu gate=0x%08lx ship=0x%08lx id=%lu seq=%lu key=%lu order=0x%x from=%s to=%s why=%s rank=%u "
            "stage=%s>%s", frame,
            static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.gate), static_cast<unsigned long>(r.ship),
            static_cast<unsigned long>(r.id), static_cast<unsigned long>(r.seq), static_cast<unsigned long>(r.key), unsigned(r.order),
            gc::role_name(gc::Role(r.from)), gc::role_name(gc::Role(r.to)), gc::why_name(gc::Why(r.why)), unsigned(r.rank),
            gc::sb_name(gc::Sb(r.sb_from)), gc::sb_name(gc::Sb(r.sb_to)));
    }
    for (std::uint32_t w = ctl_pair_write_.load(std::memory_order_acquire); ctl_pair_read_ != w; ++ctl_pair_read_) {
        const CtlPairRow& r = ctl_pair_rows_[ctl_pair_read_ & 63];
        static const char* const kPairKind[3] = {"verdict", "e_pass", "order0"};
        char gap[16] = "na"; // B.9: the sweep's minimum gap over the stored rollouts (na: no rollout pair)
        if (r.gap < 1e29f) std::snprintf(gap, sizeof gap, "%.0f", double(r.gap));
        x3m::log("ship_gate_ctl_pair frame=%llu t_ms=%lu gate=0x%08lx a=0x%08lx b=0x%08lx seq=%lu,%lu rank=%u,%u kind=%s verdict=%s before=%s "
            "why=%s t_c_s=%.1f yielder=%s cap_ms=%.1f d_m=%.0f cause=%s trig=%s gap_m=%s",
            frame, static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.gate), static_cast<unsigned long>(r.a),
            static_cast<unsigned long>(r.b), static_cast<unsigned long>(r.seq_a), static_cast<unsigned long>(r.seq_b), unsigned(r.rank_a),
            unsigned(r.rank_b), kPairKind[r.kind < 3 ? r.kind : 0], gc::pair_name(gc::PairV(r.verdict)), gc::pair_name(gc::PairV(r.before)),
            r.kind ? gc::ewhy_name(gc::EWhy(r.why)) : "-", r.t_c == 255 ? -1.0 : double(r.t_c) * double(gc::kSampleS),
            kYielder[r.yielder < 3 ? r.yielder : 0], double(r.cap), double(r.d), gc::pair_why_name(r.cause),
            (r.cause & gc::kPairWhyPlan) ? "plan" : "clock", gap);
    }
    for (std::uint32_t w = member_row_write_.load(std::memory_order_acquire); member_row_read_ != w; ++member_row_read_) { // B.4 item 2
        const MemberRow& r = member_rows_[member_row_read_ & 31];
        x3m::log("ship_gate_ctl_member frame=%llu t_ms=%lu gate=0x%08lx ship=0x%08lx class=%s role=%s stage=%s order=0x%x target=0x%08lx "
            "p_m=%.0f,%.0f,%.0f nose_f=%.3f v_ms=%.1f key=%lu traced=%u st_age_ms=%ld dec_age_ms=%ld live=%u zx=%.0f w_m=%.0f w_ok=%u funnel=%u",
            frame, static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.gate), static_cast<unsigned long>(r.ship),
            r.code < core::kClassCount ? core::kClassNames[r.code] : "?", gc::role_name(gc::Role(r.role)), gc::sb_name(gc::Sb(r.stage)),
            unsigned(r.order), static_cast<unsigned long>(r.target), double(r.p[0]), double(r.p[1]), double(r.p[2]), double(r.nose_f),
            double(r.v), static_cast<unsigned long>(r.key), unsigned(r.traced), static_cast<long>(r.st_age_ms),
            static_cast<long>(r.dec_age_ms), unsigned(r.live), double(r.zx), double(r.w), unsigned(r.w_ok), unsigned(r.funnel));
        if (r.lane) { // lane stage 1: the lane's own fields (a separate row)
            char gap[16] = "na", vw[16] = "na";
            if (r.gap > -1e30f) std::snprintf(gap, sizeof gap, "%.0f", double(r.gap));
            if (r.v_w >= 0.0f) std::snprintf(vw, sizeof vw, "%.1f", double(r.v_w));
            x3m::log("ship_gate_ctl_lane frame=%llu t_ms=%lu gate=0x%08lx ship=0x%08lx lane_s=%.0f rank=%u gap_m=%s law=%s lat_m=%.0f "
                     "lc_m=%.0f,%.0f v_law=%.1f v_w=%s w=%s on=%u merged=%u loop=%s pt=%s own=%u", frame,
                     static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.gate), static_cast<unsigned long>(r.ship), double(r.lane_s),
                     unsigned(r.rank), gap, gc::law_name(gc::Law(r.law)), double(r.lat), double(r.lc[0]), double(r.lc[1]), double(r.v_law), vw,
                     gc::lane_write_name(gc::LaneWrite(r.w_kind)), unsigned(r.on), unsigned(r.merged), gc::lane_loop_name(r.loop),
                     r.pt ? "point" : "lane", unsigned(r.own));
        }
    }
    for (std::uint32_t w = merge_row_write_.load(std::memory_order_acquire); merge_row_read_ != w; ++merge_row_read_) { // lane stage 1
        const MergeRow& r = merge_rows_[merge_row_read_ & 7];
        x3m::log("ship_gate_ctl_merge frame=%llu t_ms=%lu gate=0x%08lx ship=0x%08lx pred=0x%08lx event=%s gap_m=%.0f law=%s dur_ms=%lu", frame,
                 static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.gate), static_cast<unsigned long>(r.e.ship),
                 static_cast<unsigned long>(r.e.pred), r.e.finish ? "finish" : "start", double(r.e.gap), gc::law_name(r.e.law),
                 static_cast<unsigned long>(r.e.ms));
    }
    for (std::uint32_t w = defer_row_write_.load(std::memory_order_acquire); defer_row_read_ != w; ++defer_row_read_) { // round 1 item 5
        static const char* const kKind[3] = {"defer", "restore", "rewrite"};
        const DeferRow& r = defer_rows_[defer_row_read_ & 7];
        x3m::log("ship_gate_ctl_defer frame=%llu t_ms=%lu gate=0x%08lx ship=0x%08lx order=0x%x kind=%s f210_read=%lu f210_written=%lu ahead_ms=%ld",
                 frame, static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.gate), static_cast<unsigned long>(r.ship), unsigned(r.order),
                 kKind[r.kind < 3 ? r.kind : 0], static_cast<unsigned long>(r.read), static_cast<unsigned long>(r.written),
                 long(std::int32_t(r.written - r.t_ms)));
    }
    for (std::uint32_t w = rewrite_row_write_.load(std::memory_order_acquire); rewrite_row_read_ != w; ++rewrite_row_read_) { // item 9
        static const char* const kSite[4] = {"a", "b", "adopt", "miss"};
        const RewriteRow& r = rewrite_rows_[rewrite_row_read_ & 7];
        x3m::log("ship_gate_ctl_rewrite frame=%llu t_ms=%lu gate=0x%08lx ship=0x%08lx site=%s slot=%d from_m=%.0f,%.0f,%.0f to_m=%.0f,%.0f,%.0f "
                 "r_m=%.0f implied_m=%.0f kc_m=%.0f", frame,
                 static_cast<unsigned long>(r.t_ms), static_cast<unsigned long>(r.gate), static_cast<unsigned long>(r.ship),
                 kSite[r.site < 4 ? r.site : 3], int(r.slot), double(r.from[0]), double(r.from[1]), double(r.from[2]), double(r.to[0]),
                 double(r.to[1]), double(r.to[2]), double(r.r), double(r.implied), double(r.kc));
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
} // namespace x3m_ship_steering_rt
