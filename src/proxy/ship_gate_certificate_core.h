#pragma once
// The gate traffic controller: stage A's certificate. The rollout (the plans and their speeds, rollout), the pair sweep (sweep*,
// gap_scan), one decision (decide, try_yield), the frame's order and budget (decide_order, frame_budget), site E's reads (e_certified,
// e_reason, e_obst_row, sb_gate_why / e_gate_row) and the KC's warn (note_warn, keep_through_avoid).
#include "ship_gate_lane_core.h"
namespace x3m::ship_avoidance::ctl {
// ---- the rollout ------------------------------------------------------------------------------------------------------------------
// A member on order 0 inside the script's re-issue gap whose last steered plan is one the rollout can continue.
__attribute__((always_inline)) inline bool gap_plan(const Member& m) noexcept;
// A member rolling out the script's order-0 gap on its last plan (the drift's gap case).
__attribute__((always_inline)) inline bool in_gap(const Member& m) noexcept {
    return m.st.order == sc::kOrderNone && !(m.role == Role::committed && m.handover_ms) && gap_plan(m);
}
// The order-0 gap's expected rest (s): the member brakes at a (order 0: desired 0) until the script re-issues its order, at most 3 s after
// its last steered read for an escort (the template's 2-3 s wait) and 5 s for a committed ship; the rollout then resumes its last plan at
// the plan's speed. 0 outside the gap.
__attribute__((always_inline)) inline float gap_brake_s(const Member& m) noexcept {
    if (!in_gap(m)) return 0.0f;
    const std::uint32_t gap = m.role == Role::escort ? kGapEscortMs : kGapMs, el = m.st.ms - m.steered_ms;
    return el < gap ? float(gap - el) * 0.001f : 0.0f;
}
__attribute__((always_inline)) inline bool gap_plan(const Member& m) noexcept {
    if (!m.steered_ms || m.st.ms - m.steered_ms > kGapMs) return false;
    if (m.last_plan == Plan::lane) return m.ln != 0;
    if (m.last_plan == Plan::line) return m.line_ok != 0;
    return m.last_plan == Plan::follow;
}
inline Plan plan_of(const Member& m) noexcept;
// The state read's bookkeeping (site B, the decision): a read on the role's steered order stamps steered_ms and records its plan.
inline void note_state(Member& m) noexcept {
    const std::uint16_t o = m.st.order;
    const bool steered = (m.role == Role::committed && (o == sc::kOrderFlyBefore || o == sc::kOrderDock)) ||
                         (m.role == Role::escort && (o == sc::kOrderEscort || o == sc::kOrderFollow));
    if (!steered) return;
    m.steered_ms = m.st.ms;
    m.last_plan = plan_of(m);
}
inline Plan plan_of(const Member& m) noexcept {
    const std::uint16_t o = m.st.order;
    if (o == sc::kOrderWarpIn) return Plan::warp_in;
    if (o == sc::kOrderUndock) return Plan::exit;
    if (o == sc::kOrderWarpAway) return Plan::launch;
    if (m.ln) { // lane stage 1: order 4 toward Lambda or on it, the hand-over at the held speed (P2 keeps it, site B holds it on 0 / 0x1f),
                // an escort's 0x14 / 0x15 (the module's lane leaf); the order-0 gap below resumes the lane
        if (m.exit_ms && m.role == Role::escort) return Plan::drift; // lane fix round 2 item 2: handed to another leader, it flies the
                                                                     // engine's leg to it (round 2 review item 2: an escort's hand only)
        if (m.role == Role::committed && o == sc::kOrderFlyBefore && (m.sb == Sb::inbound || m.sb == Sb::funnel)) return Plan::lane;
        if (m.role == Role::committed && (o == sc::kOrderNone || o == sc::kOrderRotate) && m.handover_ms) return Plan::lane;
        if (m.role == Role::committed && o == sc::kOrderNone && m.sb == Sb::dock) return Plan::launch; // P4's frame: order 8 next
        if (m.role == Role::escort && (o == sc::kOrderEscort || o == sc::kOrderFollow)) return Plan::lane;
        // the lost-leader wait (section 6.5): an escort on order 0 after its leader's order 5 / 8 (the reservation) is held at its
        // lane law's speed by site B (lane_site_b) for as long as the script's lambda lasts, not only for kGapMs: the lane rollout, which
        // brakes for the whole horizon when the nose is beyond site B's 8 deg (lane_hold_o0)
        if (m.role == Role::escort && o == sc::kOrderNone && m.res_until && (m.sb == Sb::inbound || m.sb == Sb::funnel)) return Plan::lane;
    }
    if (m.role == Role::committed) {
        if (o == sc::kOrderFlyBefore) return m.line_ok ? Plan::line : Plan::drift; // a lane funnel flies Lambda_4 (line_of)
        if (o == sc::kOrderDock) return m.sb == Sb::dock && m.q_ok ? Plan::line5 : Plan::dock;
        if (o == sc::kOrderRotate) return Plan::hold;
    }
    // order 0 (Run161): the P2 hand-over brakes (order 0 sets the desired speed 0; 0x1f holds); a committed member or an escort in the
    // script's re-issue gap (<= kGapMs after its last steered read) rolls out on its last steered plan at its current speed (an escort:
    // following its leader; a committed ship: its line); anything else brakes along the nose
    if (o == sc::kOrderNone) {
        if (m.role == Role::committed && m.handover_ms) return Plan::brake;
        if ((m.role == Role::committed || m.role == Role::escort) && gap_plan(m)) return m.last_plan;
        return Plan::brake;
    }
    if (m.role == Role::escort && (o == sc::kOrderEscort || o == sc::kOrderFollow)) return Plan::follow;
    return Plan::drift;
}
// The plan's speed: the module's own speed before a yield cap when known and fresh, else the desired speed site B left.
inline float plan_speed(const Gate& g, const Member& m, std::uint32_t now = 0) noexcept {
    const std::uint32_t cap_age = age_bound(g, kCapMaxAgeMs); // B.3 item 1
    if (m.st.order == sc::kOrderNone) { // the order-0 gap: the last plan's speed (the rollout brakes for the gap's rest first)
        // B.9: the resume speed is the one site B writes once the script re-issues the order: the module's plan speed, under the yield
        // cap when it is still in force (cap_in_force; the first decision on the steered order after the gap re-yields or releases it)
        if (m.v_plan > 0.0f && m.v_plan_ms && std::int32_t(m.st.ms - m.v_plan_ms) <= std::int32_t(kGapMs + cap_age)) {
            const float c = cap_in_force(g, m, now);
            return c >= 0.0f && c < m.v_plan ? c : m.v_plan;
        }
        return m.st.v > 0.0f ? m.st.v : 0.0f;
    }
    // B.9 review: the age is signed: site A records v_plan at each decision, after site B's last state read (st.ms, every 100 ms), so
    // a fresh v_plan is often newer than the state; unsigned, that wrapped and fell back to the capped v_des (the yielder's ladder then
    // started from its own cap)
    if (m.v_plan > 0.0f && m.v_plan_ms && std::int32_t(m.st.ms - m.v_plan_ms) <= std::int32_t(cap_age)) return m.v_plan;
    return m.st.v_des > 0.0f ? m.st.v_des : 0.0f;
}
__attribute__((always_inline)) inline void put(Sample& s, const float c[3], const float f[3]) noexcept {
    for (unsigned i = 0; i < 3; ++i) {
        s.c[i] = c[i];
        s.f[i] = f[i];
    }
}
// The state in the plane of the member's line (sc::join_plane), with the plane's lateral unit n (the ship's side); false when the
// plane is not finite.
inline bool line_plane(const Member& m, const float X[3], const float h[3], sc::JoinPlane* st, float n[3]) noexcept {
    sc::GateGeometry g;
    for (unsigned i = 0; i < 3; ++i) {
        g.X[i] = X[i];
        g.h[i] = h[i];
        g.A[i] = X[i] + sc::kGateCrossFraction * m.params.r_gate * h[i];
    }
    g.r = m.params.r_gate;
    if (!sc::join_plane(g, m.st.x, m.st.rows, m.st.w, m.st.u, m.st.v, st)) return false;
    const float dx[3] = {m.st.x[0] - X[0], m.st.x[1] - X[1], m.st.x[2] - X[2]};
    const float s = sc::dot3(dx, h);
    for (unsigned i = 0; i < 3; ++i) n[i] = dx[i] - s * h[i];
    float e = sc::sqrt_ss(sc::dot3(n, n));
    if (e > 1.0f) {
        for (unsigned i = 0; i < 3; ++i) n[i] /= e;
        return true;
    }
    const float fh = sc::dot3(m.st.rows[2], h); // on the line: the plane of the nose (join_plane's choice)
    for (unsigned i = 0; i < 3; ++i) n[i] = m.st.rows[2][i] - fh * h[i];
    e = sc::sqrt_ss(sc::dot3(n, n));
    if (e > 1e-4f) {
        for (unsigned i = 0; i < 3; ++i) n[i] /= e;
    } else {
        for (unsigned i = 0; i < 3; ++i) n[i] = m.st.rows[0][i];
    }
    return true;
}
// The order-4 line a member's rollout flies: the grid line (X, h), or in the lane's funnel Lambda_4 (through the point at X's depth,
// along F).
inline void line_of(const Member& m, const Gate& g, float X[3], float h[3]) noexcept {
    if (m.sb == Sb::funnel && m.q_ok) {
        lambda4(g, m, X);
        for (unsigned i = 0; i < 3; ++i) h[i] = g.F[i];
        return;
    }
    for (unsigned i = 0; i < 3; ++i) {
        X[i] = m.X[i];
        h[i] = m.h[i];
    }
}
// moves follow its kinematics (section 10.10): 0x21 the warp-in path to f+0x240, then 0x1e; 0x1e axis-parallel out at 120 / 240 m/s
// until |dz| > r (r / 4 for small hulls), then straight on; order 8 along +F for 0.9 s, then gone; order 0 braking along the nose at
// a; 0x1f in place; order 5 at its speed toward f+0x240 until the order's arrival radius (|v| / 10 + r), then gone.
inline unsigned rollout(const Member& m, const Gate& g, float v_cmd, Sample* out, float* vmax, std::uint32_t now = 0) noexcept {
    const Plan plan = plan_of(m);
    float f0[3] = {m.st.rows[2][0], m.st.rows[2][1], m.st.rows[2][2]};
    {
        const float n2 = sc::dot3(f0, f0);
        if (sc::finite_f(n2) && n2 > 0.25f) {
            const float inv = 1.0f / sc::sqrt_ss(n2);
            for (float& v : f0) v *= inv;
        } else
            for (unsigned i = 0; i < 3; ++i) f0[i] = g.F[i];
    }
    float c[3] = {m.st.x[0], m.st.x[1], m.st.x[2]};
    float u0 = sc::sqrt_ss(sc::dot3(m.st.u, m.st.u));
    if (!sc::finite_f(u0)) u0 = 0.0f;
    *vmax = u0;
    unsigned n = 0;
    switch (plan) {
    case Plan::line: {
        sc::JoinPlane st;
        float nn[3], LX[3], Lh[3];
        line_of(m, g, LX, Lh); // the grid line, or (in the lane's funnel) Lambda_4
        if (!line_plane(m, LX, Lh, &st, nn)) break;
        const float v_des = v_cmd >= 0.0f ? v_cmd : plan_speed(g, m, now);
        const float t_brake = gap_brake_s(m); // the order-0 gap: desired 0 for its expected rest, then the plan (Opus question 5)
        const float k_lag = sc::one_minus_exp_neg(kStepS / sc::kSlipTau), a_along = sc::kGateCrossFraction * m.params.r_gate;
        float x = st.s, y = st.y, psi = st.psi, upsi = st.upsi, w = st.w, v = st.v;
        // the nose: the live nose turned about the plane's normal h x n by the plant's in-plane turn (the plant has no out-of-plane
        // axis; the out-of-plane part of the live nose is kept, not projected away: the capsule at sample 0 is the true hull)
        float ax[3] = {Lh[1] * nn[2] - Lh[2] * nn[1], Lh[2] * nn[0] - Lh[0] * nn[2], Lh[0] * nn[1] - Lh[1] * nn[0]};
        {
            const float an = sc::sqrt_ss(sc::dot3(ax, ax));
            for (float& q : ax) q = an > 1e-6f ? q / an : 0.0f;
        }
        const float psi0 = psi;
        bool past = false;
        float dir[3] = {0, 0, 0};
        for (unsigned i = 0; i < 3; ++i) c[i] = LX[i] + x * Lh[i] + y * nn[i];
        for (unsigned k = 0; k < kSamples; ++k) {
            for (unsigned step = 0; k && step < 2; ++step) {
                if (!past && x < 0.0f && m.params.W > 1e-4f && m.params.alpha > 1e-6f) {
                    sc::plant_step(m.params, float(k) * kSampleS < t_brake ? 0.0f : v_des, kStepS, k_lag, a_along, x, y, psi, upsi, w, v);
                    for (unsigned i = 0; i < 3; ++i) c[i] = LX[i] + x * Lh[i] + y * nn[i];
                } else { // past the crossing plane (P2): the hand-over brakes on its last velocity (order 0, desired 0; 0x1f in place)
                    if (!past) {
                        past = true;
                        float su, cu;
                        sc::sincos_f(upsi, &su, &cu);
                        for (unsigned i = 0; i < 3; ++i) dir[i] = cu * Lh[i] + su * nn[i];
                    }
                    const float a = m.params.a_lin > 0.0f ? m.params.a_lin : 1.0f;
                    const float v1 = v > a * kStepS ? v - a * kStepS : 0.0f;
                    for (unsigned i = 0; i < 3; ++i) c[i] += dir[i] * 0.5f * (v + v1) * kStepS;
                    v = v1;
                }
            }
            float sp, cp;
            sc::sincos_f(psi - psi0, &sp, &cp); // Rodrigues: f0 cos + (a x f0) sin + a (a . f0)(1 - cos)
            const float axf[3] = {ax[1] * f0[2] - ax[2] * f0[1], ax[2] * f0[0] - ax[0] * f0[2], ax[0] * f0[1] - ax[1] * f0[0]};
            const float af = sc::dot3(ax, f0);
            const float f[3] = {f0[0] * cp + axf[0] * sp + ax[0] * af * (1.0f - cp), f0[1] * cp + axf[1] * sp + ax[1] * af * (1.0f - cp),
                                f0[2] * cp + axf[2] * sp + ax[2] * af * (1.0f - cp)};
            put(out[n++], c, f);
            if (v > *vmax) *vmax = v;
        }
        break;
    }
    case Plan::lane: { // lane stage 1 (section 6.3, "the certificate's view"): the lane carrot (lane_carrot) in the plane of F and the
                       // ship's lateral from Lambda, at the lane laws on the rolled-out state, coupled to the predecessor's own rollout
                       // (front ship first: its samples at the same game time), the head's pacing and the escort gate from lane_frame's
                       // moments, the cornering cap of site B's write; past d_end the hand-over holds the speed (P2, the site-B hold; the
                       // refusal's brake) to the order-5 trigger, then order 8 for 0.9 s and the end point held. A candidate v_cmd >= 0
                       // caps the law (the merge floor); v_cmd == 0 is the brief stop for its rest (kLaneStopMs + the step from stop_ms)
        if (!(m.params.W > 1e-4f) || !(m.params.alpha > 1e-6f)) break;
        const float d0v[3] = {m.st.x[0] - g.G[0], m.st.x[1] - g.G[1], m.st.x[2] - g.G[2]};
        float lat0[2], LX[3], nn[3];
        lane_lat_at(g, m, sc::dot3(d0v, g.F), lat0);
        { // review item 5: a member near its Lambda with a blend still ahead takes the plane through the blend's end (its Q at d_end),
          // which then contains the whole blend l_c -> Q (lane_lat_at's points lie on that segment)
            float latE[2];
            lane_lat_at(g, m, -dend(m), latE);
            const float bR = latE[0] - lat0[0], bU = latE[1] - lat0[1], oR = sc::dot3(d0v, g.R) - lat0[0], oU = sc::dot3(d0v, g.U) - lat0[1];
            if (bR * bR + bU * bU > 1.0f && oR * oR + oU * oU < bR * bR + bU * bU) {
                lat0[0] = latE[0];
                lat0[1] = latE[1];
            }
        }
        for (unsigned i = 0; i < 3; ++i) LX[i] = g.G[i] + lat0[0] * g.R[i] + lat0[1] * g.U[i] - dend(m) * g.F[i];
        sc::JoinPlane st;
        if (!line_plane(m, LX, g.F, &st, nn)) break;
        const float k_lag = sc::one_minus_exp_neg(kStepS / sc::kSlipTau);
        float x = st.s, y = st.y, psi = st.psi, upsi = st.upsi, w = st.w, v = st.v;
        float ax[3] = {g.F[1] * nn[2] - g.F[2] * nn[1], g.F[2] * nn[0] - g.F[0] * nn[2], g.F[0] * nn[1] - g.F[1] * nn[0]};
        {
            const float an = sc::sqrt_ss(sc::dot3(ax, ax));
            for (float& q : ax) q = an > 1e-6f ? q / an : 0.0f;
        }
        const float psi0 = psi;
        // review item 5: Lambda's lateral moves from l_c to the member's Q over the blend (lane_lat_at, as lane_steer flies it live): in
        // the plane, Lambda lies at yL(s) = (lat(s) - lat0) . n (the out-of-plane part of the blend is not rolled out)
        const float nR = sc::dot3(nn, g.R), nU = sc::dot3(nn, g.U);
        // (always_inline: a float return out of line goes through st(0) on i686, which the x87 audit refuses; with round C's rollout
        // body GCC 16 emitted this lambda as an .isra clone)
        auto y_lane = [&](float s_abs) __attribute__((always_inline)) {
            float l[2];
            lane_lat_at(g, m, s_abs, l);
            return (l[0] - lat0[0]) * nR + (l[1] - lat0[1]) * nU;
        };
        // the order-0 gap: desired 0 for its rest unless site B holds the lane speed (the nose within 8 deg of F)
        // (the lost-leader wait outside the re-issue gap: order 0 brakes until the script's order 4, beyond this horizon at worst)
        const bool o0 = m.st.order == sc::kOrderNone && !(m.role == Role::committed && m.handover_ms);
        const LaneHold hold = o0 ? lane_hold_o0(g, m, m.st.x, f0, m.st.ms) : LaneHold{};
        const bool o0_held = !o0 || hold.v >= 0.0f;
        const float t_brake = o0_held ? 0.0f
                              : m.st.order == sc::kOrderNone && m.role == Role::escort && !in_gap(m) ? float(kSamples) * kSampleS
                                                                                                    : gap_brake_s(m);
        const int pi = m.lane_pred ? find(g, m.lane_pred) : -1;
        // (signed: the predecessor's state may be a few ms newer than this member's; live() on m.st.ms would wrap)
        const Member* P = pi >= 0 && pair_role(g.m[pi].role) && g.m[pi].st.ms && g.m[pi].n > 1 &&
                                  std::int32_t(m.st.ms - g.m[pi].st.ms) <= std::int32_t(age_bound(g, kStateMaxAgeMs)) ? &g.m[pi] : nullptr;
        std::int32_t koff = 0;
        if (P) {
            const std::int32_t dms = std::int32_t(m.st.ms - P->roll_ms);
            koff = dms >= 0 ? (dms + std::int32_t(kSampleMs / 2)) / std::int32_t(kSampleMs) : -((-dms + std::int32_t(kSampleMs / 2)) / std::int32_t(kSampleMs));
        }
        auto p_along = [&](std::int32_t j) {
            const unsigned jj = j < 0 ? 0u : unsigned(j) < P->n ? unsigned(j) : P->n - 1u;
            const float d[3] = {P->s[jj].c[0] - g.G[0], P->s[jj].c[1] - g.G[1], P->s[jj].c[2] - g.G[2]};
            return sc::dot3(d, g.F);
        };
        LaneCtx cx;
        cx.v_lane = lane_vmax(m);
        cx.a = m.params.a_lin;
        cx.pred = P != nullptr; // (lane fix round 2 item 3d: cx.granted stays false: a granted predecessor's rollout shows its order-5 run, and
                                // at a SETA step's trigger order 8's lane rollout holds its point (v0 0): the follower's rollout stops there)
        cx.head = m.lane_free_ms != 0;
        cx.gate = m.lane_gate_ms != 0;
        cx.dt = float(g.step_ms > kStepClampMs ? kStepClampMs : g.step_ms) * 0.001f;
        // (lane fix round 2 item 3c: an overshoot that has not turned in flies its re-departure in the rollout, the out-leg to its turn point
        // and the U-turn, as lane_steer flies it, never the hand-over's straight run past d_end; its law runs from its U-turn's apex, where
        // it rejoins, as lane_frame's)
        bool redep = m.redep != 0 || (m.over && !m.over_in), past = false, band = m.band != 0;
        const float s_ov_ap = -(dend(m) + m.redep_out + kLaneOverApexR * m.redep_r);
        const bool escort = m.role == Role::escort;
        const float join_end = lane_join_end(m);
        const float loop_nn = m.loop ? m.loop_n[0] * nR + m.loop_n[1] * nU : 0.0f; // (round C: the loop's side in the rollout's plane)
        const float stop_rest = v_cmd == 0.0f ? float(std::int32_t(m.stop_ms ? m.stop_ms + kLaneStopMs + g.step_ms - m.st.ms : kLaneStopMs + g.step_ms)) * 0.001f : 0.0f;
        // (lane fix round 1 item 4: the law's stop episode, lane_frame's lstop_ms: what is left of T_stop + the step at the state)
        const float stop_left0 = float(std::int32_t((m.lstop_ms ? m.lstop_ms : m.st.ms) + kLaneStopMs + g.step_ms - m.st.ms)) * 0.001f;
        const float omega = m.params.W < sc::kSlipCornerRate ? m.params.W : sc::kSlipCornerRate;
        const float top = lane_vmax(m);
        float dir[3] = {g.F[0], g.F[1], g.F[2]}, cpast[3] = {0, 0, 0}, ctrig[3] = {0, 0, 0}, t_launch = -1.0f;
        for (unsigned i = 0; i < 3; ++i) c[i] = LX[i] + x * g.F[i] + y * nn[i];
        // lane fix round 2 item 3d: a member already in its hand-over (past d_end after P2) rolls out the hand-over from its state (not one
        // plant step of its lane law first: a refused leader stopped at d_end crept 2.5 m/s in its rollout and its follower's floor ran into
        // it); the speed site B writes there (lane_site_b): P2 kept, the held speed (the live one, which site B keeps under the brake bound
        // from P2; the rollout predicts the grant, not the bound's stop: measured in round A, a leader rolled out braking to the bound's
        // stop made every follower yield at the floor and brake 30 m/s for a launch that came 10 s later); P2 not kept (round A, rank 4),
        // no write: order 0 brakes it at a_lin to rest. A refused slot shows in the live state the next rollout starts from
        const bool hand = !escort && m.role == Role::committed && m.handover_ms && m.sb == Sb::handover;
        if (hand && x >= 0.0f) {
            past = true;
            float su, cu;
            sc::sincos_f(upsi, &su, &cu);
            for (unsigned i = 0; i < 3; ++i) {
                dir[i] = cu * g.F[i] + su * nn[i];
                cpast[i] = c[i];
            }
        }
        const float a_hand = m.params.a_lin > 0.1f ? m.params.a_lin : 0.1f;
        for (unsigned k = 0; k < kSamples; ++k) {
            const float t = float(k) * kSampleS;
            if (t_launch >= 0.0f) { // order 8, then gone: straight on along +F at its speed (as line5 under the lane)
                float p[3];
                for (unsigned i = 0; i < 3; ++i) p[i] = ctrig[i] + v * (t - t_launch) * g.F[i];
                put(out[n++], p, f0);
                continue;
            }
            for (unsigned step = 0; k && step < 2; ++step) {
                const float ts = float(k - 1u) * kSampleS + float(step) * kStepS; // the step's start (s from the state)
                if (past) { // the hand-over: the held speed along the last velocity to the trigger; P2 not kept: no write, order 0's brake
                            // to rest
                    if (hand && !m.p2_kept) v = v > a_hand * kStepS ? v - a_hand * kStepS : 0.0f;
                    for (unsigned i = 0; i < 3; ++i) cpast[i] += dir[i] * v * kStepS;
                    continue;
                }
                cx.s = x - dend(m);
                cx.d_end = dend(m);
                const float s_act = cx.s;
                const float yl = y_lane(cx.s), y_rel = y - yl;
                cx.y = y_rel > 0.0f ? y_rel : -y_rel;
                cx.loss = lane_join_loss(x - dend(m), cx.y, join_end, dend(m), lane_turn_r(m));
                {
                    float su_, cu_;
                    sc::sincos_f(upsi, &su_, &cu_);
                    cx.v_self = v * cu_;
                }
                if (redep && cx.s < -(dend(m) + m.redep_out)) {
                    float sp_, cp_;
                    sc::sincos_f(psi, &sp_, &cp_);
                    if (cp_ >= 0.0f) redep = false;
                }
                if ((m.over || m.loop) && redep && cx.s > s_ov_ap) cx.s = s_ov_ap; // (item 3c, round C's loop: the law's s; the carrot below flies from s_act)
                if (P) {
                    const std::int32_t j = koff + std::int32_t(k) - 1;
                    const float sp = p_along(j), sp1 = p_along(j + 1);
                    if (P->ln && P->lane_rank != 0xff) { // the predecessor's own join still to come (from its sample's lateral)
                        const unsigned jj = j < 0 ? 0u : unsigned(j) < P->n ? unsigned(j) : P->n - 1u;
                        const float dp[3] = {P->s[jj].c[0] - g.G[0], P->s[jj].c[1] - g.G[1], P->s[jj].c[2] - g.G[2]};
                        float lp[2];
                        lane_lat_at(g, *P, sp, lp);
                        const float pr = sc::dot3(dp, g.R) - lp[0], pu = sc::dot3(dp, g.U) - lp[1];
                        cx.loss -= lane_join_loss(sp, sc::sqrt_ss(pr * pr + pu * pu), lane_join_end(*P), dend(*P), lane_turn_r(*P));
                    }
                    cx.v_pred = (sp1 - sp) / kSampleS;
                    cx.block = (cx.v_pred < kLaneStopV && cx.v_pred > -kLaneStopV) || ((P->over || P->loop) && P->redep);
                    // (round A: the ship ahead before its P2 on its final: the stop-room bound, as lane_frame's, on its live stage, until its
                    // sample reaches d_end: its P2 there is rolled out as kept (a P2 not kept shows in the live state the next rollout
                    // starts from); measured in slow_hull, a bound kept past d_end rolled every follower of a ship in its final out to a
                    // stop at spacing, and the joiner behind them into a conflict)
                    cx.stop_room = (P->sb == Sb::inbound || P->sb == Sb::funnel) && !P->over && !P->loop && sp > -(dend(*P) + lane_blend_settle(g, *P)) &&
                                           sp < -dend(*P)
                                       ? (-dend(*P) - P->L - kLaneGapM) - (cx.s + m.L)
                                       : 3.4e38f;
                    if (cx.v_pred < 0.0f) cx.v_pred = 0.0f;
                    // (lane fix round 1 item 4: an overshoot ahead turns back no farther out than its U-turn's apex: no nearer than that)
                    const bool p_ret = (P->over || P->loop) && sp > -(dend(*P) + P->redep_out + kLaneOverApexR * P->redep_r); // (round C: a loop too)
                    const float s_rj = -(dend(*P) + P->redep_out + kLaneOverApexR * P->redep_r), spp = p_ret ? s_rj : sp;
                    cx.gap = (spp + (p_ret ? 0.0f : (sp1 - sp) * float(step) * 0.5f) - P->L) - (cx.s + m.L);
                }
                cx.pred = P != nullptr && !(m.loop && redep); // (round C: a loop flies its out-leg and U-turn at the lane speed, as lane_frame)
                cx.t_free_s = cx.head ? float(std::int32_t(m.lane_free_ms - m.st.ms)) * 0.001f - ts : 0.0f;
                cx.t_gate_s = cx.gate ? float(std::int32_t(m.lane_gate_ms - m.st.ms)) * 0.001f - ts : 0.0f;
                cx.stop_left_s = stop_left0 - ts;
                Law law;
                float settle;
                float vd = lane_law(cx, &law, nullptr, &settle);
                if (v_cmd > 0.0f && v_cmd < vd) vd = v_cmd;
                if (v_cmd == 0.0f && ts < stop_rest) vd = 0.0f;
                if (band && lane_band_free(cx.gap, settle)) band = false; // (the lateral rule's release, as lane_frame)
                const bool hold_lat = band && !(cx.y <= kLaneOnM);
                float sc_c, yc;
                lane_carrot(s_act, cx.y, v, m.params.W, m.params.r_gate, hold_lat, redep, &sc_c, &yc,
                            redep ? (s_act > -(dend(m) + m.redep_out) ? -m.redep_side : kLaneTurnM) * (m.loop ? -loop_nn : sc::dot3(g.U, nn))
                                  : P ? P->w + m.w + kLaneSideM : 0.0f,
                            join_end, m.redep_out, m.redep_r, dend(m), m.loop ? lane_blend_settle(g, m) : 0.0f);
                float ta = sc_c + dend(m), tb = y_lane(sc_c) + (y_rel < 0.0f ? -yc : yc);
                const bool coast = o0 && ts < hold.t_s; // review item 5: the order-0 hold's straight coast (no leaf on order 0)
                if (coast) {
                    float sp_, cp_;
                    sc::sincos_f(psi, &sp_, &cp_);
                    ta = x + 10000.0f * cp_;
                    tb = y + 10000.0f * sp_;
                }
                const float d0 = sc::sqrt_ss((ta - x) * (ta - x) + (tb - y) * (tb - y));
                if (d0 > 1.0f) { // the cornering cap on the nose toward the carrot (core::steer's rule)
                    float sp, cp;
                    sc::sincos_f(psi, &sp, &cp);
                    const float cs = (cp * (ta - x) + sp * (tb - y)) / d0;
                    float vc = 3.4e38f;
                    if (cs < 0.0f) vc = omega * d0 * 0.5f;
                    else {
                        const float sn2 = 1.0f - cs * cs, sn = sn2 > 0.0f ? sc::sqrt_ss(sn2) : 0.0f;
                        if (sn > sc::kCornerSinMin) vc = omega * d0 / (2.0f * sn);
                    }
                    if (vc < sc::kFloorMs) vc = sc::kFloorMs;
                    if (vc < vd) vd = vc;
                }
                if (vd > top) vd = top;
                if (ts < hold.t_s && vd > hold.v) vd = hold.v; // the order-0 hold's straight-run cap (lane_hold_o0)
                if (ts < t_brake) vd = 0.0f;
                plant_step_to(m.params, vd, kStepS, k_lag, ta, tb, x, y, psi, upsi, w, v);
                if (!escort && !redep && x >= 0.0f) { // order 4 ends at d_end (L_i's 0.75 r_gate sphere): the hand-over from here
                    past = true;
                    float su, cu;
                    sc::sincos_f(upsi, &su, &cu);
                    for (unsigned i = 0; i < 3; ++i) {
                        dir[i] = cu * g.F[i] + su * nn[i];
                        cpast[i] = LX[i] + x * g.F[i] + y * nn[i];
                    }
                }
            }
            if (past) {
                for (unsigned i = 0; i < 3; ++i) c[i] = cpast[i];
                const float dd[3] = {c[0] - g.G[0], c[1] - g.G[1], c[2] - g.G[2]};
                if (sc::dot3(dd, dd) <= (v / 10.0f + m.r) * (v / 10.0f + m.r)) { // the order-5 trigger (tf 1: the latest), order 8 next
                    t_launch = t;
                    for (unsigned i = 0; i < 3; ++i) ctrig[i] = c[i];
                }
            } else
                for (unsigned i = 0; i < 3; ++i) c[i] = LX[i] + x * g.F[i] + y * nn[i];
            float sp, cp; // the live nose turned about F x n by the plant's in-plane turn (as the line plan)
            sc::sincos_f(psi - psi0, &sp, &cp);
            const float axf[3] = {ax[1] * f0[2] - ax[2] * f0[1], ax[2] * f0[0] - ax[0] * f0[2], ax[0] * f0[1] - ax[1] * f0[0]};
            const float af = sc::dot3(ax, f0);
            const float f[3] = {f0[0] * cp + axf[0] * sp + ax[0] * af * (1.0f - cp), f0[1] * cp + axf[1] * sp + ax[1] * af * (1.0f - cp),
                                f0[2] * cp + axf[2] * sp + ax[2] * af * (1.0f - cp)};
            put(out[n++], c, f);
            if (v > *vmax) *vmax = v;
        }
        break;
    }
    case Plan::line5: { // order 5 (the lane): the pursuit of Lambda_5 (through Q along F) at the order's aligned ramp (site B's write)
                        // until the trigger |v| / 10 + r around the gate origin, then order 8 along +F for 0.9 s
        float LX[3], nn[3];
        lambda5(g, m, LX);
        sc::JoinPlane st;
        if (!line_plane(m, LX, g.F, &st, nn) || !(m.params.W > 1e-4f) || !(m.params.alpha > 1e-6f)) break;
        const float k_lag = sc::one_minus_exp_neg(kStepS / sc::kSlipTau), a_along = 2.0f * m.params.r_gate;
        float x = st.s, y = st.y, psi = st.psi, upsi = st.upsi, w = st.w, v = st.v;
        float ax[3] = {g.F[1] * nn[2] - g.F[2] * nn[1], g.F[2] * nn[0] - g.F[0] * nn[2], g.F[0] * nn[1] - g.F[1] * nn[0]};
        {
            const float an = sc::sqrt_ss(sc::dot3(ax, ax));
            for (float& q : ax) q = an > 1e-6f ? q / an : 0.0f;
        }
        const float psi0 = psi;
        float t_launch = -1.0f, v0 = 0.0f;
        for (unsigned i = 0; i < 3; ++i) c[i] = LX[i] + x * g.F[i] + y * nn[i];
        for (unsigned k = 0; k < kSamples; ++k) {
            const float t = float(k) * kSampleS;
            if (t_launch >= 0.0f) { // order 8: 900 ms along +F, then gone
                // then the ship is gone: the rollout holds the launch's end point beyond the plane (no hull is there; the pair's overlap stays
                // complete, review F13). Lane stage 1: straight on along +F at its speed (a ship behind on the same final: the launch only
                // moves it away faster, and order 8's 3 km/s would widen every sweep margin of the pair to a conflict)
                const float tl = g.lane ? t - t_launch : t - t_launch > 0.9f ? 0.9f : t - t_launch;
                const float tm = tl * 1000.0f, z = v0 * tl + (g.lane ? 0.0f : kWarpInK * tm * tm);
                float p[3];
                for (unsigned i = 0; i < 3; ++i) p[i] = c[i] + z * g.F[i];
                put(out[n++], p, f0);
                if (!g.lane && 2697.0f / 0.9f + v0 > *vmax) *vmax = 2697.0f / 0.9f + v0;
                continue;
            }
            for (unsigned step = 0; k && step < 2; ++step) {
                const float dd[3] = {c[0] - g.G[0], c[1] - g.G[1], c[2] - g.G[2]};
                const float d = sc::sqrt_ss(sc::dot3(dd, dd));
                sc::plant_step(m.params, dock_ramp(d, m.params.r_gate, m.params.v_max), kStepS, k_lag, a_along, x, y, psi, upsi, w, v);
                for (unsigned i = 0; i < 3; ++i) c[i] = LX[i] + x * g.F[i] + y * nn[i];
            }
            float sp, cp;
            sc::sincos_f(psi - psi0, &sp, &cp);
            const float axf[3] = {ax[1] * f0[2] - ax[2] * f0[1], ax[2] * f0[0] - ax[0] * f0[2], ax[0] * f0[1] - ax[1] * f0[0]};
            const float af = sc::dot3(ax, f0);
            const float f[3] = {f0[0] * cp + axf[0] * sp + ax[0] * af * (1.0f - cp), f0[1] * cp + axf[1] * sp + ax[1] * af * (1.0f - cp),
                                f0[2] * cp + axf[2] * sp + ax[2] * af * (1.0f - cp)};
            put(out[n++], c, f);
            if (v > *vmax) *vmax = v;
            const float dd[3] = {c[0] - g.G[0], c[1] - g.G[1], c[2] - g.G[2]};
            if (k && sc::dot3(dd, dd) <= (v / 10.0f + m.r) * (v / 10.0f + m.r)) { // the trigger: P4, one order-0 frame, order 8
                t_launch = t;
                v0 = v;
            }
        }
        break;
    }
    case Plan::follow: {
        const int li = find(g, m.leader);
        // (signed, as the lane rollout's predecessor: the leader's state may be a few ms newer than this member's; live() on m.st.ms
        // would wrap and drop the leader)
        const Member* L = li >= 0 && g.m[li].ship && pair_role(g.m[li].role) && g.m[li].st.ms && g.m[li].n &&
                                  std::int32_t(m.st.ms - g.m[li].st.ms) <= std::int32_t(age_bound(g, kStateMaxAgeMs))
                              ? &g.m[li]
                              : nullptr;
        if (L) {
            const std::int32_t dms = std::int32_t(m.st.ms - L->roll_ms);
            const std::int32_t koff = dms <= 0 ? 0 : (dms + std::int32_t(kSampleMs / 2)) / std::int32_t(kSampleMs);
            const unsigned last = L->n - 1u, k0 = unsigned(koff) < L->n ? unsigned(koff) : last;
            const float off[3] = {c[0] - L->s[k0].c[0], c[1] - L->s[k0].c[1], c[2] - L->s[k0].c[2]};
            float lv = 0.0f, lag = 0.0f; // a yield candidate v_cmd: the escort falls back along the leader's path by the leader's speed
                                         // above it (the formation follows the same track, later)
            for (unsigned k = 0; k < kSamples; ++k) {
                const unsigned j = k0 + k < L->n ? k0 + k : last; // the leader's rollout ends (a launch): the escort holds its slot there
                float back[3] = {0, 0, 0};
                if (v_cmd >= 0.0f && k) {
                    const unsigned jp = k0 + k - 1 < L->n ? k0 + k - 1 : last;
                    const float dl[3] = {L->s[j].c[0] - L->s[jp].c[0], L->s[j].c[1] - L->s[jp].c[1], L->s[j].c[2] - L->s[jp].c[2]};
                    const float step = sc::sqrt_ss(sc::dot3(dl, dl));
                    const float excess = step - v_cmd * kSampleS;
                    if (excess > 0.0f) lag += excess;
                    if (step > 1e-3f)
                        for (unsigned q = 0; q < 3; ++q) back[q] = dl[q] / step * lag;
                }
                const float p[3] = {L->s[j].c[0] + off[0] - back[0], L->s[j].c[1] + off[1] - back[1], L->s[j].c[2] + off[2] - back[2]};
                if (k) {
                    const float d[3] = {p[0] - out[n - 1].c[0], p[1] - out[n - 1].c[1], p[2] - out[n - 1].c[2]};
                    lv = sc::sqrt_ss(sc::dot3(d, d)) / kSampleS;
                    if (lv > *vmax) *vmax = lv;
                }
                put(out[n++], p, f0);
            }
            break;
        }
        [[fallthrough]];
    }
    case Plan::drift: {
        float dir[3] = {f0[0], f0[1], f0[2]};
        if (u0 > 0.5f)
            for (unsigned i = 0; i < 3; ++i) dir[i] = m.st.u[i] / u0;
        float v = u0;
        const float v_des = v_cmd >= 0.0f ? v_cmd : v;
        const float a = m.params.a_lin > 0.0f ? m.params.a_lin : 1.0f;
        for (unsigned k = 0; k < kSamples; ++k) {
            if (k)
                for (unsigned step = 0; step < 2; ++step) {
                    if (v > v_des) {
                        v -= a * kStepS;
                        if (v < v_des) v = v_des;
                    }
                    for (unsigned i = 0; i < 3; ++i) c[i] += dir[i] * v * kStepS;
                }
            put(out[n++], c, f0);
        }
        break;
    }
    case Plan::dock: {
        float d[3] = {m.st.point[0] - c[0], m.st.point[1] - c[1], m.st.point[2] - c[2]};
        const float dist = sc::sqrt_ss(sc::dot3(d, d));
        const float v = m.st.v > 0.0f ? m.st.v : 0.0f;
        const float radius = v / 10.0f + m.r;
        if (dist > 1e-3f)
            for (unsigned i = 0; i < 3; ++i) d[i] /= dist;
        for (unsigned k = 0; k < kSamples; ++k) {
            const float t = float(k) * kSampleS, along = v * t;
            if (dist - along <= radius && k) break; // the order's arrival test fired: order 8 and gone within 0.9 s
            for (unsigned i = 0; i < 3; ++i) c[i] = m.st.x[i] + d[i] * along;
            put(out[n++], c, f0);
        }
        break;
    }
    case Plan::brake: {
        const float a = m.params.a_lin > 0.0f ? m.params.a_lin : 1.0f;
        float v = m.st.v > 0.0f ? m.st.v : 0.0f;
        for (unsigned k = 0; k < kSamples; ++k) {
            if (k)
                for (unsigned step = 0; step < 2; ++step) {
                    const float v1 = v > a * kStepS ? v - a * kStepS : 0.0f;
                    for (unsigned i = 0; i < 3; ++i) c[i] += f0[i] * 0.5f * (v + v1) * kStepS;
                    v = v1;
                }
            put(out[n++], c, f0);
        }
        break;
    }
    case Plan::hold:
        for (unsigned k = 0; k < kSamples; ++k) put(out[n++], c, f0);
        break;
    case Plan::warp_in: case Plan::exit: {
        // 0x21: p = point + 1.665 u^2 F (u = ms left) until the deadline, at rest at the point; then 0x1e out along the ship's own
        // axis-parallel line, away from the plane, at 120 / 240 m/s until |dz| > r_gate (r_gate / 4 for a small hull), where the order
        // ends with no speed write and order 0 brakes it at a to rest (round A, section 12.1a; ship-navigation-ai.md section 10.14 Q5).
        // The whole plan moves along F only (its lateral is the exit point's: the sweep's axial drift, sweep_all). An unknown gate radius
        // (fail closed): straight on at the exit speed
        const float ve = m.large ? kExitSpeedLargeMs : kExitSpeedSmallMs;
        float t_left = plan == Plan::warp_in && m.st.left_ms > 0 ? float(m.st.left_ms) * 0.001f : 0.0f;
        if (t_left > 1.0f) t_left = 1.0f;
        const float* P = plan == Plan::warp_in ? m.st.point : m.st.x;
        const float dz0 = (P[0] - g.G[0]) * g.F[0] + (P[1] - g.G[1]) * g.F[1] + (P[2] - g.G[2]) * g.F[2];
        const float sgn = dz0 > 0.0f ? 1.0f : -1.0f; // away from the plane on the side it is on
        const float r_end = m.params.r_gate > 0.0f ? (m.large ? m.params.r_gate : 0.25f * m.params.r_gate) : 3.4e38f;
        const float a_e = m.params.a_lin > 0.0f ? m.params.a_lin : 1.0f;
        if (ve > *vmax) *vmax = ve;
        float s_out = 0.0f, v_e = ve; // the 0x1e leg's run from P (m) and its speed
        for (unsigned k = 0; k < kSamples; ++k) {
            const float t = float(k) * kSampleS;
            if (t < t_left) {
                const float u = (t_left - t) * 1000.0f, z = kWarpInK * u * u;
                for (unsigned i = 0; i < 3; ++i) c[i] = P[i] + z * g.F[i];
            } else {
                const float te = t - t_left, t_prev = k ? t - kSampleS : 0.0f; // (t - kSampleS, not float(k - 1): x87 audit)
                float dt = te - (t_prev > t_left ? t_prev - t_left : 0.0f); // this sample's share of the leg
                while (dt > 0.0f) { // the leg at ve until |dz| > r_end, then the brake at a (plant steps)
                    const float h = dt > kStepS ? kStepS : dt;
                    const float dz = dz0 + sgn * s_out;
                    if ((dz > 0.0f ? dz : -dz) > r_end) {
                        const float v1 = v_e > a_e * h ? v_e - a_e * h : 0.0f;
                        s_out += 0.5f * (v_e + v1) * h;
                        v_e = v1;
                    } else
                        s_out += v_e * h;
                    dt -= h;
                }
                for (unsigned i = 0; i < 3; ++i) c[i] = P[i] + sgn * s_out * g.F[i];
            }
            put(out[n++], c, f0);
        }
        if (plan == Plan::warp_in && 2697.0f / 0.9f > *vmax) *vmax = 2697.0f / 0.9f; // the warp-in path's peak speed bound
        break;
    }
    case Plan::launch: {
        // order 8: 900 ms along +F from where the trigger fired (at most 2697 m plus v0 t), then the ship leaves the sector
        const float v0 = m.st.v > 0.0f ? m.st.v : 0.0f;
        for (unsigned k = 0; k < kSamples; ++k) { // then gone: the rollout holds the end point (review F13, as line5); the lane: straight on
            const float t = g.lane ? float(k) * kSampleS : float(k) * kSampleS > 0.9f ? 0.9f : float(k) * kSampleS;
            const float tm = t * 1000.0f, z = v0 * t + (g.lane ? 0.0f : kWarpInK * tm * tm);
            for (unsigned i = 0; i < 3; ++i) c[i] = m.st.x[i] + z * g.F[i];
            put(out[n++], c, f0);
        }
        if (!g.lane && 2697.0f / 0.9f + v0 > *vmax) *vmax = 2697.0f / 0.9f + v0;
        break;
    }
    default: break;
    }
    return n;
}

// ---- the pair sweep ---------------------------------------------------------------------------------------------------------------
struct Sweep {
    bool clear = false;
    unsigned t_c = 255;      // the first conflict sample (255 none)
    float min_gap = 1e30f;   // the conflict sample's surface gap minus its margin (m, < 0; 1e30 when clear)
    unsigned narrow = 0;     // samples that needed the narrow test
    bool stale = false;      // fail closed: a partner rollout more than 1 s older, or fewer paired samples than the warn reach needs
};
// One member's rollout (samples sa, na, t0 a_ms, the largest speed a_vmax) against another's. B's sample for A's sample i is
// i + round((a_ms - b_ms) / 500 ms); the residual offset (<= 250 ms) adds |offset| x B's largest speed to the margin. The broad test
// uses the per-pair bound (v_A,max + v_B,max) dt / 2 on |u_A - u_B| dt / 2 (no square root); a sample that fails it is retested
// with the samples' own relative motion, then narrowly.
// B.3: kLim > 0 compiles the sweep for a constant time-offset bound, which bounds the sample offset for the compiler (measured on the
// host: 73 ns per broad-only sweep against 130 ns compiled for a runtime bound); kLim 0 takes the runtime max_dms (a long step).
// Round A (convoy-gate-transit.md section 12.1a): axial (bit 0: A, bit 1: B) marks a side whose plan moves along its own axis only (an
// arrival on 0x21 / 0x1e / the order-0 brake at the exit point; a lane member on Lambda or past d_end): its share of the margin (the
// drift k (t + offset), B's time-offset term, half its own inter-sample motion) extends its capsule's segment along its axis instead of
// widening the pair's isotropic margin, which keeps kMarginM and the other side's share. Neither axial: the isotropic sweep as before.
template <std::int32_t kLim> // kLim > 0: a compile-time bound; 0: the runtime max_dms
inline Sweep sweep_impl(const Sample* sa, unsigned na, std::uint32_t a_ms, float a_vmax, float La, float wa, const Sample* sb, unsigned nb,
                        std::uint32_t b_ms, float b_vmax, float Lb, float wb, unsigned min_samples, bool ends, float drift,
                        std::uint32_t max_dms, unsigned axial = 0) noexcept {
    Sweep out;
    const std::int32_t dms = std::int32_t(a_ms - b_ms), lim = kLim > 0 ? kLim : std::int32_t(max_dms);
    if (dms > lim || dms < -lim) { // the rollouts are too far apart in time
        out.stale = true;
        return out;
    }
    const std::int32_t koff = dms >= 0 ? (dms + std::int32_t(kSampleMs / 2)) / std::int32_t(kSampleMs)
                                       : -((-dms + std::int32_t(kSampleMs / 2)) / std::int32_t(kSampleMs));
    const float resid = sc::fabs_f(float(dms - koff * std::int32_t(kSampleMs))) * 0.001f;
    const float Lsum = La + Lb, wsum = wa + wb;
    const float ha = La > wa ? La - wa : 0.0f, hb = Lb > wb ? Lb - wb : 0.0f; // the capsules' half segments
    const bool caps = ha > 1.0f && hb > 1.0f;
    const float inv2ha = caps ? 0.5f / ha : 0.0f, inv2hb = caps ? 0.5f / hb : 0.0f;
    const float m_bound = (a_vmax + b_vmax) * (0.5f * kSampleS);
    // A's samples i0 .. i1 - 1 pair with B's i + koff (both on the 0.5 s grid)
    const std::int32_t i0 = koff < 0 ? -koff : 0;
    std::int32_t i1 = std::int32_t(na);
    if (std::int32_t(nb) - koff < i1) i1 = std::int32_t(nb) - koff;
    // a rollout that ends because its ship leaves the sector (order 8's 0.9 s, then gone) is complete: one paired sample suffices
    if (i1 - i0 < (ends ? 1 : std::int32_t(min_samples < kMinOverlapSamples ? kMinOverlapSamples : min_samples))) { // too short: no
        out.stale = true;                                                                                  // certificate (fail closed)
        return out;
    }
    const Sample* B = sb + koff;
    // the drift charges each side from its own rollout's t0: k (t + |t0_A - t0_B|), t from A's t0
    float base = kMarginM + resid * b_vmax + drift * (kSampleS * float(i0 > 0 ? i0 : 0) + float(dms >= 0 ? dms : -dms) * 0.001f);
    for (std::int32_t i = i0; i < i1; ++i, base += drift * kSampleS) {
        const Sample& A = sa[i];
        const Sample& Bi = B[i];
        const float d[3] = {A.c[0] - Bi.c[0], A.c[1] - Bi.c[1], A.c[2] - Bi.c[2]};
        const float d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        const float reach = Lsum + base + m_bound;
        if (d2 >= reach * reach) continue; // broad, with the per-pair bound on the relative motion
        // narrow, with the samples' own relative motion over the interval (forward difference; the last sample backward):
        // |segment_A - segment_B| >= w_A + w_B + base + |du| dt / 2, |du| taken as its L1 norm (an upper bound within sqrt 3: the
        // margin errs wide, never narrow), on squares without a square root
        const std::int32_t nx = i + 1 < i1 ? i + 1 : i > i0 ? i - 1 : i;
        ++out.narrow;
        float g2, rn;
        if (axial) { // an axial side: its terms along its segment (the drift and the offset charged to each side, never less than before)
            const float dA1 = sc::fabs_f(sa[nx].c[0] - A.c[0]) + sc::fabs_f(sa[nx].c[1] - A.c[1]) + sc::fabs_f(sa[nx].c[2] - A.c[2]);
            const float dB1 = sc::fabs_f(B[nx].c[0] - Bi.c[0]) + sc::fabs_f(B[nx].c[1] - Bi.c[1]) + sc::fabs_f(B[nx].c[2] - Bi.c[2]);
            const float drift_t = base - kMarginM - resid * b_vmax; // the pair's drift at this sample
            const float tA = drift_t + 0.5f * dA1, tB = drift_t + resid * b_vmax + 0.5f * dB1;
            const float hai = ha + ((axial & 1u) ? tA : 0.0f), hbi = hb + ((axial & 2u) ? tB : 0.0f);
            rn = wsum + kMarginM + ((axial & 1u) ? 0.0f : tA) + ((axial & 2u) ? 0.0f : tB);
            const float p1[3] = {A.c[0] - hai * A.f[0], A.c[1] - hai * A.f[1], A.c[2] - hai * A.f[2]};
            const float q1[3] = {A.c[0] + hai * A.f[0], A.c[1] + hai * A.f[1], A.c[2] + hai * A.f[2]};
            const float p2[3] = {Bi.c[0] - hbi * Bi.f[0], Bi.c[1] - hbi * Bi.f[1], Bi.c[2] - hbi * Bi.f[2]};
            const float q2[3] = {Bi.c[0] + hbi * Bi.f[0], Bi.c[1] + hbi * Bi.f[1], Bi.c[2] + hbi * Bi.f[2]};
            g2 = seg_seg_d2(p1, q1, p2, q2);
        } else {
            const float du1 = sc::fabs_f((sa[nx].c[0] - A.c[0]) - (B[nx].c[0] - Bi.c[0])) + sc::fabs_f((sa[nx].c[1] - A.c[1]) - (B[nx].c[1] - Bi.c[1])) +
                              sc::fabs_f((sa[nx].c[2] - A.c[2]) - (B[nx].c[2] - Bi.c[2]));
            rn = wsum + base + 0.5f * du1;
            if (caps)
                g2 = capsule_d2(A.c, A.f, ha, inv2ha, Bi.c, Bi.f, hb, inv2hb);
            else { // a sphere hull (L = w): the general segment distance
                const float p1[3] = {A.c[0] - ha * A.f[0], A.c[1] - ha * A.f[1], A.c[2] - ha * A.f[2]};
                const float q1[3] = {A.c[0] + ha * A.f[0], A.c[1] + ha * A.f[1], A.c[2] + ha * A.f[2]};
                const float p2[3] = {Bi.c[0] - hb * Bi.f[0], Bi.c[1] - hb * Bi.f[1], Bi.c[2] - hb * Bi.f[2]};
                const float q2[3] = {Bi.c[0] + hb * Bi.f[0], Bi.c[1] + hb * Bi.f[1], Bi.c[2] + hb * Bi.f[2]};
                g2 = seg_seg_d2(p1, q1, p2, q2);
            }
        }
        if (!(g2 >= rn * rn)) {
            out.t_c = unsigned(i);
            out.min_gap = sc::sqrt_ss(g2) - rn;
            return out;
        }
    }
    out.clear = true;
    return out;
}
inline Sweep sweep(const Sample* sa, unsigned na, std::uint32_t a_ms, float a_vmax, float La, float wa, const Sample* sb, unsigned nb,
                   std::uint32_t b_ms, float b_vmax, float Lb, float wb, unsigned min_samples = kMinOverlapSamples, bool ends = false,
                   float drift = kDriftMs) noexcept {
    return sweep_impl<std::int32_t(kRolloutMaxAgeMs)>(sa, na, a_ms, a_vmax, La, wa, sb, nb, b_ms, b_vmax, Lb, wb, min_samples, ends, drift, 0);
}
constexpr std::uint32_t kSweepNormalMs = kRolloutMaxAgeMs + 250; // the constant-bound sweep's reach (normal steps up to 250 ms)
inline Sweep sweep_seta(const Sample* sa, unsigned na, std::uint32_t a_ms, float a_vmax, float La, float wa, const Sample* sb, unsigned nb,
                        std::uint32_t b_ms, float b_vmax, float Lb, float wb, unsigned min_samples, bool ends, float drift,
                        std::uint32_t max_dms, unsigned axial = 0) noexcept {
    return sweep_impl<0>(sa, na, a_ms, a_vmax, La, wa, sb, nb, b_ms, b_vmax, Lb, wb, min_samples, ends, drift, max_dms, axial);
}
// Round A (section 12.1a): whether a member's rollout moves along its own axis only, so the sweep charges its terms along it (axial above):
// an arrival on a drift-free plan (0x21 to the exit point, 0x1e out, the order-0 brake, 0x1f in place: its lateral is the exit point's), or a
// lane member on Lambda within kLaneOnM or past d_end (the hand-over, order 5, order 8: the lane's lateral). Anything else (a joiner, an
// arrival given a steered order, a non-lane member): the isotropic drift as before (fail closed: the engine's warn where it does not clear).
__attribute__((always_inline)) inline bool axial_plan(const Member& m) noexcept {
    if (m.role == Role::arrival) {
        const Plan p = plan_of(m);
        return p == Plan::warp_in || p == Plan::exit || p == Plan::brake || p == Plan::hold;
    }
    if (!m.ln || !(m.role == Role::committed || m.role == Role::escort)) return false;
    if (m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch) return true;
    return m.lane_on && m.lane_lat <= kLaneOnM;
}
// B.9 (diagnostic, off the decision path): the sweep's minimum gap over the paired samples of two stored rollouts, every sample narrow:
// min over i of (the capsules' surface gap - the sweep's margin at i: kMarginM + the residual offset x B's largest speed + the drift
// k (t + |t0_A - t0_B|) + |du|_1 dt / 2), its sample, and the first sample below 0 (255 none). ok = false when the rollouts are too far
// apart in time (max_dms) or share fewer paired samples than the sweep needs (min_samples, never below kMinOverlapSamples; one with ends:
// overlap_min; B.9 review: gap_m is na where the verdict is stale). The pair row (on a verdict change) and the fixture call it (pair_scan);
// ~61 capsule tests.
struct GapScan {
    bool ok = false;
    unsigned i_min = 255, t_c = 255;
    float min_gap = 1e30f;
};
inline GapScan gap_scan(const Sample* sa, unsigned na, std::uint32_t a_ms, float La, float wa, const Sample* sb, unsigned nb, std::uint32_t b_ms,
                        float b_vmax, float Lb, float wb, std::uint32_t max_dms, unsigned min_samples, bool ends, float drift = kDriftMs) noexcept {
    GapScan out;
    const std::int32_t dms = std::int32_t(a_ms - b_ms), lim = std::int32_t(max_dms);
    if (dms > lim || dms < -lim) return out;
    const std::int32_t koff = dms >= 0 ? (dms + std::int32_t(kSampleMs / 2)) / std::int32_t(kSampleMs)
                                       : -((-dms + std::int32_t(kSampleMs / 2)) / std::int32_t(kSampleMs));
    const float resid = sc::fabs_f(float(dms - koff * std::int32_t(kSampleMs))) * 0.001f;
    const std::int32_t i0 = koff < 0 ? -koff : 0;
    std::int32_t i1 = std::int32_t(na);
    if (std::int32_t(nb) - koff < i1) i1 = std::int32_t(nb) - koff;
    if (i1 - i0 < (ends ? 1 : std::int32_t(min_samples < kMinOverlapSamples ? kMinOverlapSamples : min_samples))) return out;
    out.ok = true;
    const Sample* B = sb + koff;
    float base = kMarginM + resid * b_vmax + drift * (kSampleS * float(i0) + float(dms >= 0 ? dms : -dms) * 0.001f);
    for (std::int32_t i = i0; i < i1; ++i, base += drift * kSampleS) {
        const std::int32_t nx = i + 1 < i1 ? i + 1 : i > i0 ? i - 1 : i;
        const float du1 = sc::fabs_f((sa[nx].c[0] - sa[i].c[0]) - (B[nx].c[0] - B[i].c[0])) + sc::fabs_f((sa[nx].c[1] - sa[i].c[1]) - (B[nx].c[1] - B[i].c[1])) +
                          sc::fabs_f((sa[nx].c[2] - sa[i].c[2]) - (B[nx].c[2] - B[i].c[2]));
        const float gap = capsule_gap(sa[i].c, sa[i].f, La, wa, B[i].c, B[i].f, Lb, wb) - base - 0.5f * du1;
        if (gap < out.min_gap) {
            out.min_gap = gap;
            out.i_min = unsigned(i);
        }
        if (gap < 0.0f && out.t_c == 255) out.t_c = unsigned(i);
    }
    return out;
}

// ---- one decision --------------------------------------------------------------------------------------------------------------
// A verdict change for the pair rows (the DLL logs it at most once per pair per second; the fixture counts it).
struct PairEvent {
    std::uint32_t a = 0, b = 0;
    std::uint8_t verdict = 0, t_c = 255, yielder = 0; // yielder: 0 none, 1 a, 2 b
    float gap = 0, cap = -1.0f;
};
struct DecideStats {
    unsigned rollouts = 0, sweeps = 0, narrow = 0, conflicts = 0, yields = 0, unresolved = 0, candidates = 0, released = 0;
    unsigned replans = 0; // B.9: decisions whose member's plan changed since its previous rollout (plan_changed)
};
constexpr unsigned kMaxEvents = 2 * kMembers;
// The paired samples a sweep needs (fail closed below it): the engine's warn reach in time, 1 s plus its braking term min(600 m, v^2 / 2a)
// at the speed v of either ship (section 3.1), and never fewer than kMinOverlapSamples.
inline unsigned warn_reach_samples(const Member& a, const Member& b) noexcept {
    float t = 0.0f;
    const Member* both[2] = {&a, &b};
    for (const Member* m : both) {
        const float v = sc::sqrt_ss(sc::dot3(m->st.u, m->st.u)), acc = m->params.a_lin > 0.1f ? m->params.a_lin : 1.0f;
        if (!sc::finite_f(v) || v <= 0.1f) continue;
        float tb = v / (2.0f * acc);
        if (600.0f / v < tb) tb = 600.0f / v;
        if (tb > t) t = tb;
    }
    unsigned n = unsigned(int((1.0f + t) / kSampleS)) + 2u; // via int: unsigned(float) is x87 fistpll in GCC's cold placement (x87 audit)
    if (n < kMinOverlapSamples) n = kMinOverlapSamples;
    return n > kSamples ? kSamples : n;
}
// The paired samples a sweep of A's rollout (na samples) against B's needs (sweep_all; pair_scan uses the same): a rollout that ends because
// order 8 is live on the leaving ship is complete with one paired sample (*ends); one that ends at the order-5 trigger (line5) keeps the
// 4-sample minimum but not the warn reach (review F13); else the warn reach.
inline unsigned overlap_min(const Member& A, unsigned na, const Member& B, bool* ends) noexcept {
    const bool launching = (B.n < kSamples && plan_of(B) == Plan::launch) || (na < kSamples && plan_of(A) == Plan::launch);
    const bool triggering = (B.n < kSamples && plan_of(B) == Plan::line5) || (na < kSamples && plan_of(A) == Plan::line5);
    *ends = launching;
    return triggering && !launching ? kMinOverlapSamples : warn_reach_samples(A, B);
}
// Every pair of member i with the other live members, swept against the rollout s (n samples, t0 = i's state). write: the matrix row
// of i takes the verdicts (clear, or none for a conflict) and *conflicts the bit of each conflicting partner; !write (a yield
// candidate): stops at the first conflict. Returns whether every pair cleared.
// Lane stage 1 (skip_behind): a lane yielder's candidate skips the members behind it in the lane order: their rollouts follow its old
// plan (front ship first) and are re-made on its new one at their next decision (decide_order puts the lane order first); their live
// follow law reacts in the same frame (lane_frame). Their pair verdicts keep their stamps (they age out under kCertMaxAgeMs).
inline bool sweep_all(Gate& g, unsigned i, const Sample* s, unsigned n, float vmax, std::uint32_t now, bool write, std::uint32_t* conflicts,
                      DecideStats* st, PairWhy clear_why = PairWhy::cert, bool skip_behind = false) noexcept {
    const Member& A = g.m[i];
    bool all = true;
    for (unsigned j = 0; j < g.n; ++j) {
        if (j == i || !live(g, g.m[j], now)) continue;
        const Member& B = g.m[j];
        if (skip_behind && B.ln && B.lane_rank != 0xff && A.lane_rank != 0xff && B.lane_rank > A.lane_rank) continue;
        ++st->sweeps;
        // fail closed: no samples on either side, a rollout more than 1 s from the other, too short an overlap -> no verdict (none:
        // the engine's warn and the sphere avoidance), not a conflict to resolve
        // the overlap's minimum (overlap_min: one sample when a rollout ends at a launch, review F13)
        bool launching;
        const unsigned min_s = overlap_min(A, n, B, &launching);
        // (a member in the order-0 gap brakes in its rollout for the gap's expected rest: gap_brake_s; the drift k stays kDriftMs)
        const float drift = kDriftMs;
        // B.3: the rollouts' time offset bound is SETA-aware; the nominal bound keeps the constant sweep (sweep_impl)
        // review F5: the bound is nominal + step on every frame; at normal steps (bound <= kSweepNormalMs) the exact bound is checked here
        // and the sweep compiled for the constant kSweepNormalMs runs (its own check never fires then)
        const std::uint32_t max_dms = age_bound(g, kRolloutMaxAgeMs);
        const std::int32_t dms = std::int32_t(A.st.ms - B.roll_ms);
        const bool apart = dms > std::int32_t(max_dms) || dms < -std::int32_t(max_dms);
        // round A (section 12.1a): a pair with an arrival on its drift-free plan is swept with each axial side's terms along its axis
        const unsigned axial = (A.role == Role::arrival || B.role == Role::arrival) ? (axial_plan(A) ? 1u : 0u) | (axial_plan(B) ? 2u : 0u) : 0u;
        const Sweep w = !(B.n && n) || apart ? Sweep{false, 255, 1e30f, 0, true}
                        : max_dms <= kSweepNormalMs
                            ? sweep_impl<std::int32_t(kSweepNormalMs)>(s, n, A.st.ms, vmax, A.L, A.w, B.s, B.n, B.roll_ms, B.v_roll_max, B.L, B.w,
                                                                        min_s, launching, drift, 0, axial)
                            : sweep_seta(s, n, A.st.ms, vmax, A.L, A.w, B.s, B.n, B.roll_ms, B.v_roll_max, B.L, B.w, min_s, launching, drift, max_dms,
                                         axial);
        st->narrow += w.narrow;
        if (write) {
            g.v[i][j] = g.v[j][i] = std::uint8_t(w.clear ? PairV::clear : PairV::none);
            g.tc[i][j] = g.tc[j][i] = std::uint8_t(w.t_c);
            const PairWhy why = w.clear ? clear_why : !(B.n && n) ? PairWhy::zero : w.stale ? PairWhy::stale : PairWhy::contact; // B.9
            g.why[i][j] = g.why[j][i] = std::uint8_t(std::uint8_t(why) | g.why_plan);
            g.vms[i][j] = g.vms[j][i] = now ? now : 1;
        }
        if (!w.clear) {
            all = false;
            if (conflicts && !w.stale) *conflicts |= 1u << j;
            if (!write) return false;
        }
    }
    return all;
}
// B.9 review (item 2): a member in the script's order-0 re-issue gap on a steered plan (in_gap) yields too, deferred: nothing is written
// on order 0 (site B applies a cap only on the steered order, can_yield), the cap holds the gap's resume, which site B writes once the
// script re-issues the order; the candidate's rollout brakes for the gap's rest, then flies the candidate (and decide keeps a cap the
// gap's rollout cleared on in force through the gap). A committed member needs its validated line (can_yield's).
__attribute__((always_inline)) inline bool gap_yield(const Member& m) noexcept {
    return in_gap(m) && (m.role == Role::escort || (m.role == Role::committed && m.line_ok));
}
// Y's yield by speed against its conflict partner p: one candidate, one rollout (round A, rank 4; ship-steering-holistic-review.md
// section 2 c). Under the lane: the law at its floor (10 m/s: the gap target raised past the horizon; a candidate's cap, which site B
// applies and the rollout flies) while Y's plan speed is above the floor; else the brief stop (cap 0), which lasts at most kLaneStopMs + the
// game step from its start, or against an arrival (the hull-fit fallback, section 12.1a) the arrival window + the step from its launch;
// a member past the bound hands the pair back (cause=noyield). (Deviation from the design's raised-target law: measured in
// overshoot_near, the law of a yielder whose settling gap is wide is the lane speed with any target raise, and the conflict 26 s out
// cleared only on the floor.) Key off: the 10 m/s floor. The candidate's sweeps against every member; clearing takes it (Y's rollout,
// Y's cap, Y's row of the matrix; the row's entries carry plan_flag, Y's own plan change). *swept (B.9 review): whether a candidate was
// rolled out at all (false: Y cannot yield, no steered order or no candidate).
inline bool try_yield(Gate& g, unsigned y, unsigned p, std::uint32_t now, DecideStats* st, std::uint8_t plan_flag, bool* swept) noexcept {
    Member& Y = g.m[y];
    const Member& Pm = g.m[p];
    *swept = false;
    if (!can_yield(Y) && !gap_yield(Y)) return false;
    const float vp = plan_speed(g, Y, now), v = vp > 0.0f ? vp : Y.st.v;
    const bool lane = plan_of(Y) == Plan::lane;
    float c = kYieldFloorMs;
    if (lane && !(c < v)) { // (the plan already at or under the floor: the stop)
        const bool ok = Pm.role == Role::arrival ? std::int32_t(now - Pm.launch_ms) <= std::int32_t(kArrivalWindowMs + g.step_ms)
                                                 : !Y.stop_ms || std::int32_t(now - Y.stop_ms) <= std::int32_t(kLaneStopMs + g.step_ms);
        if (!ok) return false;
        c = 0.0f;
    }
    if (!(c < v || c == 0.0f)) return false;
    Sample tmp[kSamples];
    ++st->candidates;
    ++st->rollouts;
    *swept = true;
    float vmax = 0.0f;
    const unsigned n = rollout(Y, g, c, tmp, &vmax, now);
    if (!n || !sweep_all(g, y, tmp, n, vmax, now, false, nullptr, st, PairWhy::cert, lane)) return false;
    for (unsigned k = 0; k < n; ++k) Y.s[k] = tmp[k];
    Y.n = std::uint8_t(n);
    Y.roll_ms = Y.st.ms;
    Y.v_roll_max = vmax;
    Y.cap = c;
    Y.cap_ms = Y.yield_ms = now ? now : 1;
    if (lane && c == 0.0f && !Y.stop_ms) {
        Y.stop_ms = now ? now : 1;
        ++g.lane_stops;
    }
    const std::uint8_t decider_flag = g.why_plan;
    g.why_plan = plan_flag;
    sweep_all(g, y, Y.s, Y.n, Y.v_roll_max, now, true, nullptr, st, !lane ? PairWhy::yield : c > 0.0f ? PairWhy::merge : PairWhy::stop, lane);
    g.why_plan = decider_flag;
    ++st->yields;
    return true;
}
// B.9 (diagnostic): whether a member's plan changed since its own rollout: another plan or stage, or a plan speed more than kPlanDeltaMs
// from the rollout's (a law step, the order-0 gap's resume). The
// verdicts its next decision writes carry kPairWhyPlan (the pair row's trig=plan). It schedules no decision (gate-traffic-controller.md
// B.9: the plan speed's fallbacks flip it every ~100 ms). The inbound law's own decrease (a_lin x 250 ms <= 2.5 m/s) stays under it.
constexpr float kPlanDeltaMs = 5.0f;
inline bool plan_changed(const Gate& g, const Member& m, std::uint32_t now = 0) noexcept {
    if (!m.n || !m.decided_ms) return false;
    if (plan_of(m) != m.roll_plan || m.sb != m.roll_sb) return true;
    return sc::fabs_f(plan_speed(g, m, now) - m.roll_v) > kPlanDeltaMs;
}
// Member i's decision (D.1): its rollout on its plan's speed (a yield cap lapses when that clears every pair), the sweeps against every
// live member, the yield of the lower-priority ship of each conflict pair (at most kMaxYieldsPerDecision resolutions; i itself at most
// once), and the hand-back of what stays in conflict (unresolved: site E passes, the sphere avoidance applies). Verdict changes of
// i's row go to ev (at most kMaxEvents).
inline void decide(Gate& g, unsigned i, std::uint32_t now, DecideStats* st, PairEvent* ev, unsigned* n_ev) noexcept {
    Member& A = g.m[i];
    const bool replan = plan_changed(g, A, now); // B.9: before this decision's stamps
    A.decided_ms = now ? now : 1;
    note_state(A); // Run161: the order-0 gap's steered stamp and plan
    std::uint8_t before[kMembers];
    for (unsigned j = 0; j < kMembers; ++j) before[j] = g.v[i][j];
    if (!live(g, A, now)) {
        clear_pairs(g, i);
        A.n = 0;
        return;
    }
    if (replan) ++st->replans;
    g.why_plan = replan ? kPairWhyPlan : 0;
    ++st->rollouts;
    float vmax = 0.0f;
    A.n = std::uint8_t(rollout(A, g, -1.0f, A.s, &vmax, now));
    A.roll_ms = A.st.ms;
    A.v_roll_max = vmax;
    A.roll_v = plan_speed(g, A, now); // B.9: the plan the rollout flew (plan_changed)
    A.roll_plan = plan_of(A);
    A.roll_sb = A.sb;
    std::uint32_t conflicts = 0;
    sweep_all(g, i, A.s, A.n, vmax, now, true, &conflicts, st); // A.n == 0 (a refused plane, a NaN state): every pair none
    if (!conflicts && A.cap >= 0.0f) {
        // B.9 review (item 3): the gap's rollout resumed at the cap and cleared: kept in force for site B's first write after the gap (no
        // release on order 0). Lane stage 1: the lane rollout flies its law, not the cap (v_cmd < 0), so a clear one releases it
        if (in_gap(A) && cap_in_force(g, A, now) >= 0.0f && A.roll_plan != Plan::lane)
            A.cap_ms = now ? now : 1;
        else {
            A.cap = -1.0f;
            A.stop_ms = 0; // lane stage 1: the brief stop is over
            ++st->released;
        }
    }
    unsigned tries = 0;
    bool self_tried = false;
    for (unsigned j = 0; j < g.n && conflicts; ++j) {
        if (!((conflicts >> j) & 1u)) continue;
        ++st->conflicts;
        if (g.v[i][j] == std::uint8_t(PairV::clear)) continue; // a yield earlier in this decision already cleared it
        const bool a_yields = yields_to(A, g.m[j]);
        bool ok = false, swept = false;
        const bool tried = tries < kMaxYieldsPerDecision && (!a_yields || !self_tried);
        if (tried) {
            ++tries;
            if (a_yields) self_tried = true;
            // B.9 review (item 4): a partner's yield writes its row with its own plan flag; the shared pair keeps the decider's too
            const std::uint8_t flag = a_yields ? g.why_plan : plan_changed(g, g.m[j], now) ? kPairWhyPlan : std::uint8_t(0);
            ok = try_yield(g, a_yields ? i : j, a_yields ? j : i, now, st, flag, &swept);
            if (ok && !a_yields) {
                g.why[i][j] |= g.why_plan;
                g.why[j][i] |= g.why_plan;
            }
        }
        if (!ok || g.v[i][j] != std::uint8_t(PairV::clear)) {
            g.v[i][j] = g.v[j][i] = std::uint8_t(PairV::unresolved);
            g.vms[i][j] = g.vms[j][i] = now ? now : 1;
            const PairWhy why = !tried ? PairWhy::budget : swept ? PairWhy::contact : PairWhy::noyield; // B.9 (review item 4: noyield)
            g.why[i][j] = g.why[j][i] = std::uint8_t(std::uint8_t(why) | g.why_plan);
            ++st->unresolved;
        }
    }
    g.why_plan = 0;
    if (!ev || !n_ev) return;
    for (unsigned j = 0; j < g.n; ++j) {
        if (j == i || g.v[i][j] == before[j] || *n_ev >= kMaxEvents) continue;
        PairEvent& e = ev[(*n_ev)++];
        e.a = A.ship;
        e.b = g.m[j].ship;
        e.verdict = g.v[i][j];
        e.t_c = g.tc[i][j];
        e.yielder = A.cap >= 0.0f && A.yield_ms == now ? 1 : g.m[j].cap >= 0.0f && g.m[j].yield_ms == now ? 2 : 0;
        e.cap = e.yielder == 1 ? A.cap : e.yielder == 2 ? g.m[j].cap : -1.0f;
    }
}

// Whether member i is due for a decision: live, and never decided, or decided at least kDecideMs ago with a state newer than its rollout.
__attribute__((always_inline)) inline bool due(const Gate& g, const Member& m, std::uint32_t now) noexcept {
    return live(g, m, now) && (!m.decided_ms || (now - m.decided_ms >= kDecideMs && m.st.ms != m.roll_ms));
}
// Whether the frame may start another decision (the frame's bound): the first always; then within kDecisionsPerFrame and while a
// decision with every yield still fits the rollout budget.
// B.2 review F2: the members that occupy the funnel (committed in funnel, hand-over, order 5 or order 8) are decided with priority: the
// funnel's occupancy needs their decision at most kOccupyDecisionMs (5 s) old, and the round robin of a full board under SETA can take
// longer (96 members at 18 decisions per game second: 5.3 s).
__attribute__((always_inline)) inline bool sb_priority(const Member& m) noexcept {
    return m.ship && m.role == Role::committed && (m.sb == Sb::funnel || m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch);
}
// The frame's decision order (the DLL's ctl_frame, the fixture's frame end): the due priority members first, then the due others in round
// robin from the cursor. idx: k = gate * kMembers + member (kGates * kMembers entries at most); *n_prio: the leading priority entries (the
// cursor advances only on the others).
inline unsigned decide_order(const Board& b, unsigned cursor, std::uint32_t now, std::uint16_t* idx, unsigned* n_prio) noexcept {
    constexpr unsigned total = kGates * kMembers;
    unsigned n = 0;
    std::uint8_t listed[total] = {}; // review item 8: the lane phase's entries (a flag per slot, not a scan of the list per member)
    for (unsigned gi = 0; gi < kGates; ++gi) { // lane stage 1: the lane order, front ship first (a follower's rollout reads its
        const Gate& g = b.g[gi];                // predecessor's of the same frame)
        if (!g.gate || !g.lane) continue;
        // the committed members past d_end first (the hand-over, order 5, order 8: the head's predecessor, lane_frame's gp), front first
        const unsigned n0 = n;
        for (unsigned i = 0; i < g.n; ++i) {
            const Member& o = g.m[i];
            if (!(o.ship && o.role == Role::committed && (o.sb == Sb::handover || o.sb == Sb::dock || o.sb == Sb::launch)) || !due(g, o, now)) continue;
            unsigned j = n++;
            for (; j > n0 && g.m[idx[j - 1] % kMembers].lane_s < o.lane_s; --j) idx[j] = idx[j - 1];
            idx[j] = std::uint16_t(gi * kMembers + i);
            listed[gi * kMembers + i] = 1;
        }
        for (unsigned q = 0; q < g.lane_n; ++q) {
            const int i = find(g, g.lane_ord[q]);
            if (i >= 0 && due(g, g.m[i], now) && !listed[gi * kMembers + unsigned(i)]) {
                idx[n++] = std::uint16_t(gi * kMembers + unsigned(i));
                listed[gi * kMembers + unsigned(i)] = 1;
            }
        }
    }
    for (unsigned k = 0; k < total; ++k) {
        const Gate& g = b.g[k / kMembers];
        const unsigned i = k % kMembers;
        if (g.gate && i < g.n && sb_priority(g.m[i]) && due(g, g.m[i], now) && !(g.lane && listed[k]))
            idx[n++] = std::uint16_t(k);
    }
    *n_prio = n;
    for (unsigned q = 0; q < total; ++q) {
        const unsigned k = (cursor + q) % total;
        const Gate& g = b.g[k / kMembers];
        const unsigned i = k % kMembers;
        if (g.gate && i < g.n && !sb_priority(g.m[i]) && due(g, g.m[i], now) && !(g.lane && g.m[i].lane_rank != 0xff && listed[k]))
            idx[n++] = std::uint16_t(k);
    }
    return n;
}
__attribute__((always_inline)) inline bool frame_room(unsigned decisions, unsigned rollouts) noexcept {
    return !decisions || (decisions < kDecisionsPerFrame && rollouts + kMaxRolloutsPerDecision <= kRolloutsPerFrame);
}
// B.3 (user decision 2026-10-13): the frame's budget follows game time, not the frame count: decisions = max(kDecisionsPerFrame,
// ceil(members x kRefreshPerS x the frame's game step)), rollouts in the same proportion (kRolloutsPerFrame per kDecisionsPerFrame);
// at 60 fps normal play the budget stays 6 / 14, at SETA x10 and 30 fps with 24 members it is 32 / 75. The DLL's ctl_frame also stops
// at kFrameCeilingUs (1 ms) of measured time (ctl_budget_cut); the funnel's occupants are still decided first (decide_order).
constexpr unsigned kRefreshPerS = 4;             // a member's decision record is refreshed 4 times per game second (250 ms)
constexpr unsigned kFrameCeilingUs = 1000;       // the measured-time ceiling of one frame's decisions (DLL; 3 % of a 33 ms SETA frame)
struct FrameBudget {
    unsigned decisions = kDecisionsPerFrame, rollouts = kRolloutsPerFrame;
};
inline unsigned members_live(const Board& b) noexcept {
    unsigned n = 0;
    for (const Gate& g : b.g)
        if (g.gate)
            for (unsigned i = 0; i < g.n; ++i) n += g.m[i].ship && pair_role(g.m[i].role) ? 1u : 0u;
    return n;
}
inline FrameBudget frame_budget(unsigned members, std::uint32_t step_ms) noexcept {
    FrameBudget fb;
    if (step_ms > kStepClampMs) step_ms = kStepClampMs;
    const std::uint32_t need = (members * kRefreshPerS * step_ms + 999u) / 1000u; // ceil(members x 4 / s x step)
    if (need > fb.decisions) {
        fb.decisions = need;
        fb.rollouts = (need * kRolloutsPerFrame + kDecisionsPerFrame - 1u) / kDecisionsPerFrame;
    }
    return fb;
}
__attribute__((always_inline)) inline bool frame_room(unsigned decisions, unsigned rollouts, const FrameBudget& fb) noexcept {
    return !decisions || (decisions < fb.decisions && rollouts + kMaxRolloutsPerDecision <= fb.rollouts);
}

// B.4 item 2 (Run164: the three holders untraced, all 8 ship_trace slots held by small ships): under the traffic key the last
// kTraceMemberSlots of the trace_max slots are reserved for board members (pair roles); a member searches them first, then the shared
// slots [first, max - reserve). A non-member: a capital from slot 0, a non-capital from slot 1, up to max - reserve. With fewer than
// kTraceMemberSlots + 2 slots nothing is reserved. Key off: amendment 17's rule (slot 0 for one non-escort capital, others 1..max).
constexpr unsigned kTraceMemberSlots = 2;
struct TraceRange {
    unsigned first = 0, end = 0;       // the shared slots
    unsigned res_first = 0, res_end = 0; // the reserved slots (a member's first search; empty for a non-member)
};
inline TraceRange trace_slot_range(unsigned max, bool traffic, bool capital, bool escort, bool member) noexcept {
    TraceRange r;
    if (!traffic) {
        r.first = capital && !escort ? 0u : 1u;
        r.end = capital && !escort ? 1u : max;
        return r;
    }
    const unsigned reserve = max >= kTraceMemberSlots + 2 ? kTraceMemberSlots : 0u;
    r.first = capital || member ? 0u : 1u;
    r.end = max - reserve;
    if (member) {
        r.res_first = max - reserve;
        r.res_end = max;
    }
    return r;
}

// ---- reads for site E and the avoidance ---------------------------------------------------------------------------------------
// B.9: gap_scan over the two members' stored rollouts with the sweep's own bounds (overlap_min, the SETA-aware time offset); pair_gap is
// the pair row's gap_m (m; 1e30 when either has none, they are too far apart or overlap too little: a stale verdict).
inline GapScan pair_scan(const Gate& g, unsigned i, unsigned j) noexcept {
    const Member& a = g.m[i];
    const Member& b = g.m[j];
    if (!a.n || !b.n) return GapScan{};
    bool ends;
    const unsigned min_s = overlap_min(a, a.n, b, &ends);
    return gap_scan(a.s, a.n, a.roll_ms, a.L, a.w, b.s, b.n, b.roll_ms, b.v_roll_max, b.L, b.w, age_bound(g, kRolloutMaxAgeMs), min_s, ends);
}
inline float pair_gap(const Gate& g, unsigned i, unsigned j) noexcept {
    const GapScan sc = pair_scan(g, i, j);
    return sc.ok ? sc.min_gap : 1e30f;
}
// The pair verdict for (i, j) as site E takes it: clear only while at most kCertMaxAgeMs old.
inline PairV pair_verdict(const Gate& g, unsigned i, unsigned j, std::uint32_t now, std::uint32_t max_age_ms) noexcept {
    if (i >= g.n || j >= g.n || i == j || !live(g, g.m[i], now) || !live(g, g.m[j], now)) return PairV::none;
    const PairV v = PairV(g.v[i][j]);
    const std::uint32_t dec = age_bound(g, kDecisionMaxAgeMs); // B.3 item 1: the SETA-aware bounds
    if (v == PairV::clear && (now - g.vms[i][j] > age_bound(g, max_age_ms) || !g.m[i].decided_ms || !g.m[j].decided_ms ||
                              now - g.m[i].decided_ms > dec || now - g.m[j].decided_ms > dec))
        return PairV::none; // a stale verdict, or a member whose decisions starved beyond 1 s: no certificate
    return v;
}
// Site E for a member pair (D.1): suppress iff the verdict is clear and fresh (<= 250 ms, both members decided within 1 s), both live
// forward rows are readable unit vectors and neither ship is on 0x12 (the live orders).
inline bool e_certified(const Gate& g, unsigned i, unsigned j, std::uint32_t now, bool fa_ok, bool fb_ok, std::uint16_t a_order,
                        std::uint16_t b_order) noexcept {
    if (!fa_ok || !fb_ok || a_order == sc::kOrderAvoid || b_order == sc::kOrderAvoid) return false;
    return pair_verdict(g, i, j, now, kCertMaxAgeMs) == PairV::clear;
}
// Why site E passes a member pair (Run161; the reason of every pass_ctl, counted): certified (suppress_ctl), none (no verdict: a sweep
// that failed closed), unresolved (a conflict handed back), stale (the clear verdict older than 250 ms), starved (a member's decisions
// more than 1 s old), on_0x12, rows (a forward row not a unit vector), order0 (no certificate while one of the pair is on order 0).
enum class EWhy : std::uint8_t { certified = 0, none, unresolved, stale, starved, on_0x12, rows, order0, not_member, count };
inline const char* ewhy_name(EWhy w) noexcept {
    static const char* const n[unsigned(EWhy::count)] = {"certified", "none", "unresolved", "stale", "starved", "on_0x12", "rows", "order0",
                                                         "not_member"};
    return unsigned(w) < unsigned(EWhy::count) ? n[unsigned(w)] : "?";
}
inline EWhy e_reason(const Gate& g, unsigned i, unsigned j, std::uint32_t now, bool fa_ok, bool fb_ok, std::uint16_t a_order,
                     std::uint16_t b_order) noexcept {
    if (a_order == sc::kOrderAvoid || b_order == sc::kOrderAvoid) return EWhy::on_0x12;
    if (!fa_ok || !fb_ok) return EWhy::rows;
    if (i >= g.n || j >= g.n || !live(g, g.m[i], now) || !live(g, g.m[j], now)) return EWhy::not_member;
    const PairV v = PairV(g.v[i][j]);
    if (v == PairV::clear) {
        const std::uint32_t dec = age_bound(g, kDecisionMaxAgeMs);
        if (!g.m[i].decided_ms || !g.m[j].decided_ms || now - g.m[i].decided_ms > dec || now - g.m[j].decided_ms > dec)
            return EWhy::starved;
        return now - g.vms[i][j] > age_bound(g, kCertMaxAgeMs) ? EWhy::stale : EWhy::certified;
    }
    if (a_order == sc::kOrderNone || b_order == sc::kOrderNone) return EWhy::order0;
    return v == PairV::unresolved ? EWhy::unresolved : EWhy::none;
}
// Site E's obstacle row as the DLL calls it (the fixture's engine model calls the same): the membership bitmap, the board of A, the
// object id; then lane_obst_why on the live position and velocity read (x_ok: the reads succeeded).
inline bool e_obst_row(Board& b, const Index& ix, std::uint32_t a, std::uint32_t id_a, std::uint16_t order, std::uint32_t target, const float x[3],
                       const float u[3], bool x_ok, const ObstBound& o, const float* detour, bool manual, std::uint32_t now,
                       ObstRow* why = nullptr) noexcept {
    if (why) *why = ObstRow::no;
    if (!b.lane || !a || !ix.maybe(a) || !x_ok) return false;
    int i = -1;
    const Gate* g = locate(b, a, &i);
    if (!g || i < 0 || g->m[i].id != id_a) return false;
    const ObstRow w = lane_obst_why(*g, g->m[i], order, target, x, u, o, detour, manual, now);
    if (why) *why = w;
    return w == ObstRow::lane;
}
// B.2 item 3 (Run162: 3 of 4 funnel releases evicted by the engine's 0x12 against their own gate): site E's gate row for a lane member.
// A (committed, Q known) in funnel / handover / dock / launch on B's board, B = its own gate as the live target, or the target zeroed on
// order 0 / 0x1f (the script's gap, the P2 hand-over), whatever the order; under the path guard dist(G, segment[x, P]) >= r_gate + r_A +
// 0.25 r_gate with P = Lambda_4's point at X's depth (the funnel's point). True: suppress_gate.
constexpr float kSbGateGuardR = 0.25f; // ship_avoidance_core.h lane::kCapitalGuardR (the capital rows' path guard)
// Review N6: fails closed unless the member is live, its radius is positive and the position read is finite and within reach of the
// member's last state read ((v_max + 50 m/s) per second of its age + 500 m). The hull radius is the member's own (validated at entry).
// B.3 item 2: also a member inbound within kSbGateNearR r_gate of its gate (a ship re-committed near the gate after an
// eviction), on the same orders and under the same path guard. B.3 item 6: the reason of a pass (the DLL's E row: gate_row=).
constexpr float kSbGateNearR = 6.0f;
// (u, f, detour: the live velocity and nose of A and its avoidance's fresh detour point (lane fix round 2 item 4; null: the member's
// state's, the fixture's units))
inline GateRow sb_gate_why(const Gate& g, const Member& m, std::uint32_t gate, std::uint16_t order, std::uint32_t target, const float x[3],
                           std::uint32_t now, const float* u = nullptr, const float* f = nullptr, const float* detour = nullptr) noexcept {
    if (!gate || g.gate != gate || m.role != Role::committed || !m.q_ok || !(g.r_gate > 0.0f)) return GateRow::not_member;
    // re-review R1: the row's own liveness, uncapped: the 1 s state age + the last step + site B's read interval (the read's reach below
    // bounds the position; the 2 s cap of age_bound stays on the certificates and the exclusion)
    if (!m.st.ms || now - m.st.ms > kStateMaxAgeMs + g.step_ms + kStateMs) return GateRow::stale;
    const float r_a = m.r;
    if (!(r_a > 0.0f) || !sc::finite_f(r_a) || !sc::finite_f(x[0]) || !sc::finite_f(x[1]) || !sc::finite_f(x[2])) return GateRow::unreadable;
    {
        const float dx[3] = {x[0] - m.st.x[0], x[1] - m.st.x[1], x[2] - m.st.x[2]};
        const float reach = ((m.params.v_max > 0.0f ? m.params.v_max : 0.0f) + 50.0f) * float(now - m.st.ms) * 0.001f + 500.0f;
        if (!(sc::dot3(dx, dx) <= reach * reach)) return GateRow::reach;
    }
    const bool lane = g.lane && m.ln;
    if (!(m.sb == Sb::funnel || m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch)) {
        if (m.sb != Sb::inbound) return GateRow::stage;
        const float dg[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]}, near = kSbGateNearR * g.r_gate;
        if (!lane && !(sc::dot3(dg, dg) <= near * near)) return GateRow::stage;
    }
    // review F3: never on the engine's own avoid order (a member kept through a 0x12 keeps its board state, not the suppression)
    if (order == sc::kOrderAvoid || !(target == gate || (!target && (order == sc::kOrderNone || order == sc::kOrderRotate)))) return GateRow::order;
    if (lane) { // lane fix round 1 item 2 (lane_gate_why; count: the path guard below)
        const GateRow w = lane_gate_why(g, m, x, u ? u : m.st.u, f ? f : m.st.rows[2], detour, now);
        if (w != GateRow::count) return w;
    }
    float P[3];
    lambda4(g, m, P);
    const float ap[3] = {g.G[0] - x[0], g.G[1] - x[1], g.G[2] - x[2]}, ab[3] = {P[0] - x[0], P[1] - x[1], P[2] - x[2]};
    const float l2 = sc::dot3(ab, ab);
    float t = l2 > 1e-6f ? sc::dot3(ap, ab) / l2 : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    const float d[3] = {ap[0] - t * ab[0], ap[1] - t * ab[1], ap[2] - t * ab[2]};
    const float dist = sc::sqrt_ss(sc::dot3(d, d));
    return sc::finite_f(dist) && dist >= g.r_gate + r_a + kSbGateGuardR * g.r_gate ? GateRow::suppress : GateRow::guard;
}
inline bool sb_gate_row(const Gate& g, const Member& m, std::uint32_t gate, std::uint16_t order, std::uint32_t target, const float x[3],
                        std::uint32_t now, const float* u = nullptr, const float* f = nullptr, const float* detour = nullptr) noexcept {
    return gate_row_suppresses(sb_gate_why(g, m, gate, order, target, x, now, u, f, detour));
}
// Review N8: site E's own-gate row as the DLL's ctl_e_fields reads it (the fixture's own-gate model calls the same): the membership
// bitmap, the board of A, B its gate, the object id; then sb_gate_row on the live position read (x_ok: the read succeeded).
inline bool e_gate_row(Board& b, const Index& ix, std::uint32_t a, std::uint32_t id_a, std::uint32_t gate, std::uint16_t order,
                       std::uint32_t target, const float x[3], bool x_ok, std::uint32_t now, GateRow* why = nullptr, const float* u = nullptr,
                       const float* f = nullptr, const float* detour = nullptr) noexcept {
    if (why) *why = GateRow::not_member;
    if (!a || !ix.maybe(a)) return false;
    int i = -1;
    const Gate* g = locate(b, a, &i);
    if (!g || i < 0 || g->gate != gate || g->m[i].id != id_a) return false;
    if (!x_ok) {
        if (why) *why = GateRow::unreadable;
        return false;
    }
    const GateRow w = sb_gate_why(*g, g->m[i], gate, order, target, x, now, u, f, detour);
    if (why) *why = w;
    return gate_row_suppresses(w);
}
// B.3 item 2: the KC's warn of a member on the frame its CollisionWarn ran (site B; the warn bits set, the order not yet 0x12): own_gate
// = the obstacle list held the member's own gate and nothing else (the caller's test), and the member's stage then.
inline void note_warn(Gate& g, Member& m, bool own_gate, std::uint32_t now) noexcept {
    (void)g;
    m.warn_ms = now ? now : 1;
    m.warn_gate = own_gate ? 1 : 0;
    m.warn_stage = std::uint8_t(m.sb);
}
// B.3 item 2 (Run163: the funnel escort evicted twice by the engine's 0x12 against its own gate on the SETA frames our row should have
// suppressed): a 0x12 is not an eviction when the KC's last warn of the member (within the SETA-aware 1 s before the episode) named its
// own gate while the member was in funnel or hand-over; the member keeps its role and funnel state through the episode (at most
// kAvoidKeepMaxMs; the order 0 after it keeps it like the script's gap: validity). *first: the episode's first kept observation (the
// caller counts ctl_evict_ignored_gate once per episode).
constexpr std::uint32_t kAvoidKeepMaxMs = 60000;
inline bool keep_through_avoid(const Gate& g, Member& m, std::uint32_t now, bool* first) noexcept {
    *first = false;
    if (m.role != Role::committed) return false;
    if (m.avoid_ms) { // an episode already kept: to its end, within the bound
        if (now - m.avoid_ms > kAvoidKeepMaxMs) return false;
        m.keep_ms = now ? now : 1;
        return true;
    }
    const Sb at = Sb(m.warn_stage);
    if (!m.warn_gate || !m.warn_ms || now - m.warn_ms > age_bound(g, kDecisionMaxAgeMs) || !(at == Sb::funnel || at == Sb::handover) ||
        !(m.sb == Sb::funnel || m.sb == Sb::handover))
        return false;
    m.avoid_ms = m.keep_ms = now ? now : 1;
    *first = true;
    return true;
}
// The episode is over (any order but 0x12 read at site B). Review F6: a kept episode refreshes the commit stamp, so the KC's order 4
// after it does not expire the member (its first order-4 decision commits again).
__attribute__((always_inline)) inline void avoid_over(Member& m, std::uint32_t now) noexcept {
    if (m.avoid_ms) m.commit_ms = now ? now : 1;
    m.avoid_ms = 0;
}
} // namespace x3m::ship_avoidance::ctl
