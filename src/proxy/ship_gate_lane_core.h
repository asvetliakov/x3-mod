#pragma once
// The gate traffic controller: the departure lane (convoy-gate-transit.md). The lane laws (LaneCtx, lane_reserve, lane_law,
// lane_refused_hold, lane_hold_o0), the lane member's path against an obstacle and the gate's solid (lane_path*, lane_obst_*, solid_gap_*,
// lane_gate_why), the predicted launch, the lane frame (lane_frame), site B's lane write (lane_site_b, lane_defer_*) and site A's lane
// steer (lane_steer).
#include "ship_gate_geometry_core.h"
namespace x3m::ship_avoidance::ctl {
// One member's law at one moment (lane_frame on the live state, the rollout on its samples): its along position s, the lane speed, the
// predecessor's gap and along speed, the head's pacing (seconds until the through slot is free) and an escort's gate (seconds until its
// leader's launch + lambda_max). Returns the speed (0: a stop) and the law; *merged (the law's state) becomes true once the settling gap is g_req - 50 m.
struct LaneCtx {
    float s = 0, v_lane = 0, a = 1.0f, v_self = 0, y = 0; // y: the lateral still to join (m; 0 on Lambda)
    float loss = 0; // the along-track distance the pair's joins will still cost the gap: the member's own less its predecessor's (lane_join_loss)
    bool pred = false, merged = true;
    float gap = 3.4e38f, v_pred = 0;
    bool head = false, gate = false;
    float t_free_s = 0, t_gate_s = 0; // t_gate_s: until the escort's latest expected order 4 (its leader's launch + lambda_max + the step)
    float dt = 0;                     // the frame's game step (s): the reserve's one step of reaction
    // lane fix round 1 item 4 (Run173: E1 written 0 for 32.6 s behind a predecessor flying away from the gate after its 0x12): the ship
    // ahead is physically stopped (a block: its speed under kLaneStopV), else the law's stop lasts at most T_stop + the step (stop_left_s:
    // what is left of it) and then releases to the floor
    bool block = false;
    float stop_left_s = 3.4e38f;
    float d_end = kLaneEndM; // lane fix round 2 item 4: the member's d_end (dend)
    // lane fix round 2: the ship ahead flies back toward the follower along F (its along speed below -kLaneStopV): never a release into it
    // (item 5); it is inside its final (past d_end - its settling distance, or in the hand-over): the follow gap covers the follower's own
    // stop, g_req + v^2 / 2a, so a P2 that drops its speed at d_end leaves a resolvable pair (item 3b)
    bool closing = false, pred_final = false;
    // lane fix round 2 item 3d: the ship ahead has its grant (order 5 or 8 seen): it leaves along F, so its standstill is no stop at
    // spacing (the queue hold behind a refused leader ends within the step of its grant, not once its speed passes kLaneStopV)
    bool granted = false;
};
// The escort gate's braking reserve (section 6.5, review item 1): the highest speed at dist (m along F to d_end) from which a ship at a
// (m/s^2) still stops kLaneStopShortM short of d_end: an escort without its order 4 never passes d_end whatever the script's latency.
// Before the latest expected order (t_left s, lambda_max after its leader's launch) the stop is preceded by a crawl at the 10 m/s floor
// as long as the time left allows (at most kLaneCrawlM): a late order finds it crawling, not stopped; lambda_max is only where the crawl
// runs out and the stop binds.
constexpr float kLaneStopShortM = 50.0f;
constexpr std::uint32_t kLaneCrawlSlackMs = 2000; // the crawl's budget runs this long past the latest expected order (the last metres
                                                  // before the stop are slower than 1 m/s)
// (each curve keeps one frame step dt of travel before the braking: v dt + (v^2 - v_f^2) / 2a <= D, so a SETA step cannot carry the ship
// past the stop)
__attribute__((always_inline)) inline float lane_reserve(float dist, float a, float v_self, float t_left, float dt) noexcept {
    const float am = a > 0.1f ? a : 0.1f, rest = dist - kLaneStopShortM, ad = am * (dt > 0.0f ? dt : 0.0f);
    if (!(rest > 0.0f)) return 0.0f;
    const float vb = sc::sqrt_ss(ad * ad + 2.0f * am * rest) - ad; // the stop
    const float t_br = v_self > kYieldFloorMs ? (v_self - kYieldFloorMs) / am : 0.0f;
    float crawl = kYieldFloorMs * (t_left - t_br);
    if (!(crawl > 0.0f)) return vb;
    if (crawl > kLaneCrawlM) crawl = kLaneCrawlM;
    const float vc = rest > crawl ? sc::sqrt_ss(ad * ad + 2.0f * am * (rest - crawl) + kYieldFloorMs * kYieldFloorMs) - ad : kYieldFloorMs;
    return vc < vb ? vc : vb;
}
inline float lane_law(const LaneCtx& c, Law* law, bool* merged, bool* stop_cond = nullptr) noexcept {
    float v = c.v_lane;
    if (stop_cond) *stop_cond = false;
    *law = Law::lane;
    *merged = c.merged;
    if (c.pred) {
        // the gap the pair settles at once the speeds match (the speed difference's distance at a: a yielder still slower than the ship
        // ahead keeps losing ground while it re-accelerates; the large cap is both ships' top speed, so a gap opened too wide never closes)
        // ... and the along-track distance the joins onto Lambda will still cost at the shared cap (its own less the predecessor's)
        const float am = c.a > 0.1f ? c.a : 0.1f, dv = c.v_pred - c.v_self;
        const float gap_v = c.gap + dv * (dv > 0.0f ? dv : -dv) / (2.0f * am), gap = gap_v + c.loss;
        // the speed law moves from merge to follow (and the yielder's lateral is released onto Lambda) once the settling gap is
        // g_req - 50 m and the yielder is no longer beside the ship ahead (the raw gap >= kLaneBesideM: review item 6; HEAD settled at a raw
        // gap of -85 m on the speed term of a yielder at the floor). While merging, the joins' loss is credited only up to what keeps the
        // settled raw gap >= kLaneBesideM (the yielder holds its lateral until then). *merged is the law's state; the merge's completion
        // for the row and the bounds is the raw gap's g_req - 50 m (lane_frame)
        const float credit_max = kLaneGapM - 50.0f - kLaneBesideM;
        const float gap_m = gap_v + (c.loss < credit_max ? c.loss : credit_max);
        if (!*merged && gap_m >= kLaneGapM - 50.0f && c.gap >= kLaneBesideM) *merged = true;
        if (!*merged) { // the merge law: open the gap at tau_m, the yielder's floor 10 m/s
            float vm = c.v_pred - (kLaneGapM - gap_m) / kLaneTauMergeS;
            if (vm < kYieldFloorMs) vm = kYieldFloorMs;
            if (vm < v) v = vm;
            *law = Law::merge;
        } else { // the follow law (lane fix round 2 item 3b: behind a predecessor inside its final the gap also covers the own stop)
            const float g_need = c.pred_final ? kLaneGapM + c.v_self * (c.v_self > 0.0f ? c.v_self : 0.0f) / (2.0f * am) : kLaneGapM;
            float vf = c.v_pred + (gap - g_need) / kLaneTauFollowS;
            if (vf < kYieldFloorMs) vf = kYieldFloorMs;
            if (vf < v - 0.5f) {
                v = vf;
                *law = Law::follow;
            }
        }
        if (((c.v_pred < kLaneStopV && !c.granted) || c.closing) && gap < kLaneGapM) { // the ship ahead stopped (a block) or flying back: stop at spacing
            if (stop_cond) *stop_cond = true;                              // behind it (its braking counted)
            if (c.block || c.closing || c.stop_left_s > 0.0f) {
                v = 0.0f;
                *law = Law::stop;
            } else if (v > kYieldFloorMs) { // (lane fix round 1 item 4: not a block, past T_stop + the step: the floor)
                v = kYieldFloorMs;
                if (*law == Law::lane) *law = *merged ? Law::follow : Law::merge;
            }
        }
    }
    const float dist = -c.d_end - c.s; // to d_end along F
    // the path still to fly to d_end (the pacing laws): a joining ship flies the straight join, not the along-track distance (lane_eta's)
    const float path = c.y > kLaneOnM && dist > 0.0f ? sc::sqrt_ss(dist * dist + c.y * c.y) : dist;
    const float a = c.a > 0.1f ? c.a : 0.1f;
    const float vb = dist > 50.0f ? sc::sqrt_ss(2.0f * a * (dist - 50.0f)) : 0.0f; // the braking curve to a stop at d_end
    if (c.head && c.t_free_s > 0.0f && v > 0.0f) { // head pacing: at d_end no earlier than the slot's free time, then the stop there
        float vp = dist > 0.0f ? path / c.t_free_s : 0.0f;
        if (vp < kYieldFloorMs) vp = kYieldFloorMs;
        if (vb < vp) vp = vb;
        if (vp < v) {
            v = vp;
            *law = v > 0.0f ? Law::pace : Law::stop;
        }
    }
    // the escort gate (section 6.5, review item 1): an escort without its order 4 flies the braking reserve to d_end (lane_reserve),
    // whatever its leader's launch and the script's latency; no pacing on a predicted moment
    if (c.gate && v > 0.0f) {
        const float vg = lane_reserve(dist, a, c.v_self, c.t_gate_s, c.dt);
        if (vg < v) {
            v = vg;
            *law = v > 0.0f ? Law::gate : Law::stop;
        }
    }
    return v;
}
// The order-0 / 0x1f hold of a member in the hand-over refused at the through slot (section 6.4): the speed stands while the longest poll
// keeps the ship outside its trigger sphere + kLaneTrigMarginM, else the brake at a to a stop there.
__attribute__((always_inline)) inline float lane_refused_hold(const Gate& g, const Member& m, const float x[3], float v) noexcept {
    const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
    const float room = sc::sqrt_ss(sc::dot3(d, d)) - (m.r + v * 0.1f) - kLaneTrigMarginM;
    if (room > v * float(kLanePollMs) * 0.001f) return v;
    const float a = m.params.a_lin > 0.1f ? m.params.a_lin : 0.1f, vb = room > 0.0f ? sc::sqrt_ss(2.0f * a * room) : 0.0f;
    return vb < v ? vb : v;
}
// Site B's order-0 hold (section 6.5) for a lane member at x with the nose f (site B on the live state, the rollout on the member's
// state): the speed it writes (< 0: none, order 0 brakes) and for how long the straight coast below applies (t_s). The nose within 8 deg
// of F: the lane law's speed v_sb. The nose within 45 deg of F (a join line, or a nose not yet settled on Lambda): on order 0 the engine
// consumes the last leaf's turn remainder and then flies straight (ship-navigation-ai.md section 10.11 Q4), so the hold writes the highest
// speed up to v_sb whose straight run over the hold's remaining horizon never takes the ship further from Lambda than it is or
// kLaneDriftM (it may cross Lambda: the 8 deg rule drifts sin 8 deg x 100 m/s x lambda_max = 237 m too) and, while it is level with its
// predecessor, never nearer than the clearance band w_P + w_Y + kLaneSideM. The horizon: the lost-leader wait to its leader's launch +
// lambda_max + the step (an escort with the reservation), else the script's re-issue gap (kGapMs after the last steered read,
// SETA-aware); past it only the 8 deg rule holds. The rollout coasts straight at this speed for t_s (review item 5).
#ifndef X3M_LANE_DRIFT_M
#define X3M_LANE_DRIFT_M 300.0f
#endif
constexpr float kLaneDriftM = X3M_LANE_DRIFT_M;
struct LaneHold {
    float v = -1.0f, t_s = 0.0f;
};
inline LaneHold lane_hold_o0(const Gate& g, const Member& m, const float x[3], const float f[3], std::uint32_t now) noexcept {
    LaneHold h;
    const float fF = sc::dot3(f, g.F), v = m.v_sb;
    if (!(v >= 0.0f) || !(fF >= kLaneJoinHoldCos)) return h;
    std::int32_t t_ms = m.steered_ms ? std::int32_t(age_bound(g, kGapMs)) - std::int32_t(now - m.steered_ms) : 0;
    if (m.role == Role::escort && m.res_until && m.res_o8_ms && std::int32_t(now - m.res_o8_ms) >= 0) { // the leader's order 8 began: the wait
        const std::int32_t tl = std::int32_t(m.res_o8_ms + 900u + kLaneLambdaMaxMs + g.step_ms - now);
        if (tl > t_ms) t_ms = tl;
    }
    if (fF >= kLaneHoldCos) { // (the rollout coasts straight over the horizon, a re-issue gap at least)
        h.v = v;
        h.t_s = float(t_ms > 0 ? t_ms : std::int32_t(age_bound(g, kGapMs))) * 0.001f;
        return h;
    }
    if (t_ms <= 0) return h;
    const float t = float(t_ms) * 0.001f;
    const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
    const float s = sc::dot3(d, g.F);
    float lat[2];
    lane_lat_at(g, m, s, lat);
    const float e[2] = {sc::dot3(d, g.R) - lat[0], sc::dot3(d, g.U) - lat[1]}; // the offset from Lambda
    const float n[2] = {sc::dot3(f, g.R), sc::dot3(f, g.U)};                     // the nose's lateral part
    const float ee = e[0] * e[0] + e[1] * e[1], en = e[0] * n[0] + e[1] * n[1], nn = n[0] * n[0] + n[1] * n[1];
    float vh = v;
    if (nn > 1e-8f) {
        // |e + n vh t|^2 <= Y^2 (Y = max(|e|, kLaneDriftM)): vh <= the larger root of nn t^2 vh^2 + 2 en t vh + ee - Y^2 = 0
        const float Y2 = ee > kLaneDriftM * kLaneDriftM ? ee : kLaneDriftM * kLaneDriftM;
        const float A = nn * t * t, B = en * t, disc = B * B - A * (ee - Y2);
        const float v_far = disc >= 0.0f ? (-B + sc::sqrt_ss(disc)) / A : 0.0f;
        if (v_far < vh) vh = v_far;
        // level with its predecessor: the run's closest approach to Lambda stays outside the clearance band (the smaller root)
        if (!m.merged && m.lane_gap < kLaneGapM - 100.0f && m.lane_pred && en < 0.0f && ee > kLaneOnM * kLaneOnM) { // (beside it, off Lambda)
            const int pi = find(g, m.lane_pred);
            const float y_min = pi >= 0 ? g.m[pi].w + m.w + kLaneSideM : kLaneOnM;
            const float perp2 = ee - en * en / nn; // the closest approach of the whole line
            if (perp2 < y_min * y_min) {
                if (ee <= y_min * y_min) vh = 0.0f;
                else {
                    const float disc2 = B * B - A * (ee - y_min * y_min);
                    const float v_near = disc2 >= 0.0f ? (-B - sc::sqrt_ss(disc2)) / A : vh;
                    if (v_near < vh) vh = v_near;
                }
            }
        }
    }
    if (!(vh > 0.0f)) return h;
    h.v = vh;
    h.t_s = t;
    return h;
}
// ---- Lane fix round 2 item 1: a lane member against a non-member obstacle (site E's obstacle row) --------------------------------
// Run173 and the round-1 review: the engine's own warn (ship-navigation-ai.md section 3.1: the close-range rule d <= 1.2 (r_A + r_B) for
// a station or a B at least 0.75 r_A, the box test for stations and capital-type ships, the cone for the rest) fires on a lane member
// beside a station whose bounding sphere is far wider than its parts, on asteroids the join passes, on a mid-size ship (an M7 beside an
// M2) whose crossing our avoidance has already resolved, and on the order-0 frames of the script's gaps where A has no fresh decision.
// The lane row suppresses that warn when the member's path as site A will fly it (lane_path: the live position and velocity, the
// avoidance's fresh detour when there is one, the board's rollout) clears the obstacle's bound by the sweep margin (kMarginM + the
// drift k (t + the rollout's age); a moving ship's extrapolation charged k t again) over the rollout's horizon, which reaches at least
// the ship's stop (1 s + v / a). Fail closed: no lane member, the player's ship or manual control, an
// order the lane does not fly (0x12, 5, 8, ...), a stale or future state or rollout, an unreadable bound: no (the existing rows decide);
// a path that does not clear: roll (the existing rows decide: the engine's warn as before).
enum class ObstRow : std::uint8_t { no = 0, lane, roll, count };
inline const char* obst_row_name(ObstRow w) noexcept {
    static const char* const n[unsigned(ObstRow::count)] = {"no", "lane", "roll"};
    return unsigned(w) < unsigned(ObstRow::count) ? n[unsigned(w)] : "?";
}
// The obstacle's bound: its centre and velocity at the read (m, m/s), the bounding sphere r; a ship's hull capsule (w > 0: half-width
// w, half-length L >= w along f); a station's part spheres (world, m; np 0: the sphere r).
struct ObstBound {
    float c[3] = {0, 0, 0}, v[3] = {0, 0, 0};
    float r = 0;
    float w = 0, L = 0, f[3] = {0, 0, 1};
    const float (*pc)[3] = nullptr;
    const float* pr = nullptr;
    unsigned np = 0;
};
// A ship obstacle's hull capsule from its type widths (w: the lateral half-width, l: the census half-length) about its nose f, against
// its live bounding radius r: w within (0, r] and f a unit row, else false (the bound stays the sphere r). Round 2 review item 3: L = l
// when it is longer than w (a census half-length beyond r kept, as hull_from keeps it for a member), else r; never w, which made a
// stopped ship whose census half-length exceeds its radius a sphere of radius w < r.
inline bool obst_hull(ObstBound& o, float w, float l) noexcept {
    if (!(w > 0.0f && w <= o.r && sc::finite_f(w)) || !unit_row(o.f)) return false;
    o.w = w;
    o.L = sc::finite_f(l) && l > w ? l : o.r;
    return true;
}
// A lane member's path from now (seconds t from now at each vertex): the live position, the live velocity up to the rollout's first
// sample at or after now, the rollout's samples (their times); with a fresh avoidance detour P_d the turn site A's leaf flies onto it
// instead (lane_turn: the live velocity's heading turned toward P_d every half second at the hull's rate W, reached at its angular
// acceleration alpha and braked on sqrt(2 alpha angle), after one frame step and a think tick straight), P_d, then the rollout from
// its first sample P_d's lateral offset / tan 30 deg beyond P_d along F (the join's steepest angle; the times shifted by the detour's
// extra length). age_s: the rollout's age at now (the drift
// charges from its t0).
constexpr unsigned kPathMax = 2 * kSamples + 4;
struct LanePath {
    float p[kPathMax][3];
    float t[kPathMax];
    unsigned n = 0;
    float age_s = 0, v_end = 0;
};
// The rollout's freshness, two-sided (lane fix round 2, review 7: a stamp from before a backward clock jump is stale)
__attribute__((always_inline)) inline bool roll_fresh(const Gate& g, const Member& m, std::uint32_t now) noexcept {
    const std::int32_t age = std::int32_t(now - m.roll_ms);
    return m.n && m.roll_ms && age >= 0 && age <= std::int32_t(kStateMaxAgeMs + g.step_ms + kStateMs);
}
// The turn onto the detour point P_d as site A's leaf flies it (lane_path): straight for one frame step and a think tick, then every
// kSampleS the heading turns toward P_d by w dt, w ramping at alpha to W and braked to stop on the target heading (sqrt(2 alpha
// angle)); the speed stays; until P_d is within a step, its closest approach (a turn wider than P_d's offset: the leaf's next detour
// point lies beyond it) or kSamples steps. Writes P.p / P.t from vertex 1 on (vertex 0 is x); returns the
// vertex count (0: no turn rate, fail closed).
inline unsigned lane_turn(const Gate& g, const Member& m, const float x[3], const float u[3], float v, const float* pd, LanePath& P) noexcept {
    const float W = m.params.W, al = m.params.alpha > 1e-4f ? m.params.alpha : W;
    if (!(W > 1e-4f)) return 0;
    float h[3] = {g.F[0], g.F[1], g.F[2]};
    if (v > 0.5f)
        for (unsigned i = 0; i < 3; ++i) h[i] = u[i] / v;
    const float vs = v > 1.0f ? v : 1.0f;
    float pos[3], t = float(g.step_ms) * 0.001f + 0.1f, w = 0.0f;
    for (unsigned i = 0; i < 3; ++i) pos[i] = x[i] + h[i] * v * t;
    unsigned n = 1;
    for (unsigned i = 0; i < 3; ++i) P.p[n][i] = pos[i];
    P.t[n++] = t;
    float dl_prev = 3.4e38f;
    for (unsigned step = 0; step < kSamples && n + 1 < kPathMax; ++step) {
        const float d[3] = {pd[0] - pos[0], pd[1] - pos[1], pd[2] - pos[2]};
        const float dl = sc::sqrt_ss(sc::dot3(d, d));
        if (!sc::finite_f(dl)) return 0;
        if (dl <= vs * kSampleS || dl > dl_prev) break; // (at P_d, or past its closest approach: the leaf's next point lies beyond it)
        dl_prev = dl;
        const float inv = 1.0f / dl, dh[3] = {d[0] * inv, d[1] * inv, d[2] * inv};
        const float c = sc::dot3(h, dh);
        const float ang = sc::acos_deg(c) * (sc::kPi / 180.0f);
        if (ang < 0.002f) break; // aligned: straight on to P_d
        const float wb = sc::sqrt_ss(2.0f * al * ang);
        w += al * kSampleS;
        if (w > W) w = W;
        if (w > wb) w = wb;
        float dA = w * kSampleS;
        if (dA > ang) dA = ang;
        float e[3] = {dh[0] - c * h[0], dh[1] - c * h[1], dh[2] - c * h[2]};
        const float el = sc::sqrt_ss(sc::dot3(e, e));
        if (el > 1e-6f) {
            const float a2 = dA * dA, cs = 1.0f - a2 * (0.5f - a2 * (1.0f / 24.0f)), sn = dA * (1.0f - a2 * (1.0f / 6.0f));
            for (unsigned i = 0; i < 3; ++i) h[i] = h[i] * cs + e[i] / el * sn;
            const float hl = sc::sqrt_ss(sc::dot3(h, h));
            for (float& q : h) q /= hl;
        }
        for (unsigned i = 0; i < 3; ++i) pos[i] += h[i] * v * kSampleS;
        t += kSampleS;
        for (unsigned i = 0; i < 3; ++i) P.p[n][i] = pos[i];
        P.t[n++] = t;
    }
    const float d[3] = {pd[0] - pos[0], pd[1] - pos[1], pd[2] - pos[2]};
    for (unsigned i = 0; i < 3; ++i) P.p[n][i] = pd[i];
    P.t[n] = t + sc::sqrt_ss(sc::dot3(d, d)) / vs;
    return n + 1;
}
inline bool lane_path(const Gate& g, const Member& m, const float x[3], const float u[3], const float* detour, std::uint32_t now, LanePath& P) noexcept {
    P.n = 0;
    if (!roll_fresh(g, m, now)) return false;
    const std::int32_t age = std::int32_t(now - m.roll_ms);
    const unsigned k0 = unsigned((age + std::int32_t(kSampleMs) - 1) / std::int32_t(kSampleMs));
    if (k0 >= m.n) return false;
    P.age_s = float(age) * 0.001f;
    const float v = sc::sqrt_ss(sc::dot3(u, u));
    if (!sc::finite_f(v)) return false;
    const float vt = v > 1.0f ? v : 1.0f; // (the times of the path's own legs)
    for (unsigned i = 0; i < 3; ++i) P.p[0][i] = x[i];
    P.t[0] = 0.0f;
    unsigned n = 1, k = k0;
    float shift = 0.0f;
    if (detour) {
        n = lane_turn(g, m, x, u, v, detour, P);
        if (!n) return false;
        // the rejoin: back onto the rollout at the join's steepest angle (30 deg, lane_join_span: its first sample the lateral offset of
        // P_d / tan 30 deg beyond P_d along F; the leg to it is tested like the rest)
        const float gF = sc::dot3(g.G, g.F), sd = sc::dot3(detour, g.F) - gF;
        while (k < m.n && sc::dot3(m.s[k].c, g.F) - gF < sd) ++k;
        if (k < m.n) {
            const float dq[3] = {m.s[k].c[0] - detour[0], m.s[k].c[1] - detour[1], m.s[k].c[2] - detour[2]};
            const float sj = sd + sc::sqrt_ss(sc::dot3(dq, dq)) * (1.0f / kLaneJoinTan);
            while (k < m.n && sc::dot3(m.s[k].c, g.F) - gF < sj) ++k;
        }
        if (k < m.n) {
            const float dj[3] = {m.s[k].c[0] - detour[0], m.s[k].c[1] - detour[1], m.s[k].c[2] - detour[2]};
            const float tk = float(std::int32_t(m.roll_ms + k * kSampleMs - now)) * 0.001f;
            const float tj = P.t[n - 1] + sc::sqrt_ss(sc::dot3(dj, dj)) / vt;
            shift = tj > tk ? tj - tk : 0.0f;
        }
    } else {
        const float tk = float(std::int32_t(m.roll_ms + k0 * kSampleMs - now)) * 0.001f;
        if (tk > 0.01f) {
            for (unsigned i = 0; i < 3; ++i) P.p[1][i] = x[i] + u[i] * tk;
            P.t[1] = tk;
            n = 2;
        }
    }
    for (; k < m.n && n < kPathMax; ++k, ++n) {
        for (unsigned i = 0; i < 3; ++i) P.p[n][i] = m.s[k].c[i];
        P.t[n] = float(std::int32_t(m.roll_ms + k * kSampleMs - now)) * 0.001f + shift;
    }
    P.n = n;
    if (n >= 2 && P.t[n - 1] > P.t[n - 2]) {
        const float e[3] = {P.p[n - 1][0] - P.p[n - 2][0], P.p[n - 1][1] - P.p[n - 2][1], P.p[n - 1][2] - P.p[n - 2][2]};
        P.v_end = sc::sqrt_ss(sc::dot3(e, e)) / (P.t[n - 1] - P.t[n - 2]);
    } else
        P.v_end = v;
    return true;
}
// Round 2 review item 7: whether lane_path would build a path, without building it (the own-gate row on a gate with no solid needs only
// that): its fail-closed tests (the rollout two-sided fresh, a sample at or after now, a finite speed; with a detour lane_turn's: a turn
// rate, and every position of its turn finite: they stay within v (kSamples + 2) kSampleS of x, so a finite bound on the distance to
// P_d from there and on that reach is a finite distance at every step).
inline bool lane_path_exists(const Gate& g, const Member& m, const float x[3], const float u[3], const float* detour, std::uint32_t now) noexcept {
    if (!roll_fresh(g, m, now)) return false;
    const std::int32_t age = std::int32_t(now - m.roll_ms);
    if (unsigned((age + std::int32_t(kSampleMs) - 1) / std::int32_t(kSampleMs)) >= m.n) return false;
    const float v = sc::sqrt_ss(sc::dot3(u, u));
    if (!sc::finite_f(v)) return false;
    if (!detour) return true;
    if (!(m.params.W > 1e-4f)) return false;
    const float d[3] = {detour[0] - x[0], detour[1] - x[1], detour[2] - x[2]};
    const float reach = v * (float(kSamples + 2u) * kSampleS + float(g.step_ms) * 0.001f + 0.1f);
    return sc::finite_f(sc::sqrt_ss(sc::dot3(d, d)) + reach);
}
// The gap (m) from A's hull swept along the path segment i (vertices i, i + 1; times ta..tb from now) to the obstacle's bound, against
// the margin at tb: >= 0 clear. A's hull: its capsule along the leg (the leg extended by L - w at both ends, radius w; a single vertex:
// the sphere L); ext adds to its radius (the horizon's stopping distance). A moving obstacle sweeps its centre over [ta - dl, tb + dl]
// (dl: 0.5 s + 10 % of tb, the path's timing slack) as a sphere of its half-length. (Always inlined: an out-of-line float return would
// travel through st(0) on i386, the x87 audit.)
__attribute__((always_inline)) inline float obst_gap(const LanePath& P, unsigned i, const ObstBound& o, float LA, float wA, float ext) noexcept {
    const bool single = i + 1 >= P.n;
    const float* p0 = P.p[i];
    const float* p1 = P.p[single ? i : i + 1];
    const float tb = P.t[single ? i : i + 1];
    float a[3], b[3], rad = LA + ext;
    {
        const float d[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
        const float dl = sc::sqrt_ss(sc::dot3(d, d));
        const float h = LA > wA ? LA - wA : 0.0f;
        if (!single && dl > 1.0f) {
            for (unsigned k = 0; k < 3; ++k) {
                a[k] = p0[k] - d[k] / dl * h;
                b[k] = p1[k] + d[k] / dl * h;
            }
            rad = wA + ext;
        } else
            for (unsigned k = 0; k < 3; ++k) a[k] = b[k] = p0[k];
    }
    float margin = kMarginM + kDriftMs * (tb + P.age_s);
    if (sc::dot3(o.v, o.v) > 0.25f) { // a moving ship: its centre's sweep, its extent the half-length
        margin += kDriftMs * tb;
        const float ta = P.t[i], dl = 0.5f + 0.1f * tb;
        float b0[3], b1[3];
        for (unsigned k = 0; k < 3; ++k) {
            b0[k] = o.c[k] + o.v[k] * (ta - dl);
            b1[k] = o.c[k] + o.v[k] * (tb + dl);
        }
        const float ext_b = o.w > 0.0f && o.L > o.w ? o.L : o.r;
        return sc::sqrt_ss(seg_seg_d2(a, b, b0, b1)) - rad - ext_b - margin;
    }
    if (o.w > 0.0f) { // a stopped ship: its hull capsule
        const float h = o.L > o.w ? o.L - o.w : 0.0f;
        const float b0[3] = {o.c[0] - h * o.f[0], o.c[1] - h * o.f[1], o.c[2] - h * o.f[2]};
        const float b1[3] = {o.c[0] + h * o.f[0], o.c[1] + h * o.f[1], o.c[2] + h * o.f[2]};
        return sc::sqrt_ss(seg_seg_d2(a, b, b0, b1)) - rad - o.w - margin;
    }
    const float root = sc::sqrt_ss(seg_seg_d2(a, b, o.c, o.c)) - rad - o.r - margin;
    if (root >= 0.0f || !o.np) return root; // (a station's parts only where the root sphere is reached)
    float best = 3.4e38f;
    for (unsigned k = 0; k < o.np; ++k) {
        const float gp = sc::sqrt_ss(seg_seg_d2(a, b, o.pc[k], o.pc[k])) - rad - o.pr[k] - margin;
        if (gp < best) best = gp;
    }
    return best;
}
// The path clears the bound on every leg over a horizon of at least one second and the stop at a_lin (the engine's own warn looks no
// further than its braking distance: when a conflict enters the horizon later, the row passes and the engine's warn still has its room).
// A path that ends earlier (a detour point beyond the rollout's end) runs on straight along its last leg to that horizon. P is extended
// in place (the caller's copy).
inline bool lane_obst_clear(LanePath& P, const Member& m, const ObstBound& o) noexcept {
    if (P.n < 2) return false;
    const float LA = m.L > m.w ? m.L : m.w, wA = m.w > 0.0f ? m.w : LA;
    const float a = m.params.a_lin > 0.1f ? m.params.a_lin : 0.1f, t_req = 1.0f + P.v_end / a;
    const unsigned l = P.n - 1;
    if (P.t[l] < t_req) {
        const float d[3] = {P.p[l][0] - P.p[l - 1][0], P.p[l][1] - P.p[l - 1][1], P.p[l][2] - P.p[l - 1][2]};
        const float dl = sc::sqrt_ss(sc::dot3(d, d));
        if (!(dl > 1.0f) || P.n >= kPathMax) return false;
        const float run = P.v_end * (t_req - P.t[l]);
        for (unsigned i = 0; i < 3; ++i) P.p[P.n][i] = P.p[l][i] + d[i] / dl * run;
        P.t[P.n] = t_req;
        ++P.n;
    }
    for (unsigned i = 0; i + 1 < P.n; ++i)
        if (!(obst_gap(P, i, o, LA, wA, 0.0f) >= 0.0f)) return false;
    return true;
}
inline ObstRow lane_obst_why(const Gate& g, const Member& m, std::uint16_t order, std::uint32_t target, const float x[3], const float u[3],
                             const ObstBound& o, const float* detour, bool manual, std::uint32_t now) noexcept {
    if (!g.lane || !m.ln || manual || !(m.role == Role::committed || m.role == Role::escort)) return ObstRow::no;
    const bool order_ok = (order == sc::kOrderFlyBefore && target == g.gate) || order == sc::kOrderNone || order == sc::kOrderRotate ||
                          (m.role == Role::escort && (order == sc::kOrderEscort || order == sc::kOrderFollow) && target == m.leader);
    if (!order_ok) return ObstRow::no;
    const std::int32_t st_age = std::int32_t(now - m.st.ms);
    if (!m.st.ms || st_age < 0 || st_age > std::int32_t(kStateMaxAgeMs + g.step_ms + kStateMs)) return ObstRow::no;
    for (unsigned i = 0; i < 3; ++i)
        if (!sc::finite_f(x[i]) || !sc::finite_f(u[i]) || !sc::finite_f(o.c[i]) || !sc::finite_f(o.v[i])) return ObstRow::no;
    if (!(o.r > 0.0f) || !sc::finite_f(o.r)) return ObstRow::no;
    {
        const float dx[3] = {x[0] - m.st.x[0], x[1] - m.st.x[1], x[2] - m.st.x[2]};
        const float reach = ((m.params.v_max > 0.0f ? m.params.v_max : 0.0f) + 50.0f) * float(st_age) * 0.001f + 500.0f;
        if (!(sc::dot3(dx, dx) <= reach * reach)) return ObstRow::no;
    }
    static_assert(sizeof(LanePath) < 4096, "the lane path stays a small stack object");
    LanePath P;
    if (!lane_path(g, m, x, u, detour, now, P)) return ObstRow::roll;
    return lane_obst_clear(P, m, o) ? ObstRow::lane : ObstRow::roll;
}
// Lane fix round 1 item 2: lane (suppressed by the lane row below), roll (the lane row passed: the member's rollout is stale); lane fix
// round 2 item 4: guard also when the lane row's path or the hand-over reaches the gate's solid
enum class GateRow : std::uint8_t { suppress = 0, not_member, stale, unreadable, reach, stage, order, guard, lane, roll, count };
inline const char* gate_row_name(GateRow w) noexcept {
    static const char* const n[unsigned(GateRow::count)] = {"suppress", "not_member", "stale", "unreadable", "reach", "stage", "order", "guard",
                                                            "lane", "roll"};
    return unsigned(w) < unsigned(GateRow::count) ? n[unsigned(w)] : "?";
}
__attribute__((always_inline)) inline bool gate_row_suppresses(GateRow w) noexcept { return w == GateRow::suppress || w == GateRow::lane; }
// Lane fix round 1 item 2 (Run173: the engine's own 0x12 against the gate fired at 6.85-6.98 km on joiners) with lane fix round 2 item 4
// (the review of round 1: the sphere-and-slab bound assumed the solid inside the bounding sphere (the Terran ring's outer radius is
// 1.026 r), never discriminated before d_end (every point there cleared the slab), returned guard on the Terran ring (its d_end point
// inside the sphere), and the hand-over branch certified a hull 200 m off its final with the board's 1.1 s old nose and no clearance
// test (on the Terran ring up to ~470 m beyond the hole)): the own-gate row of a lane member (Board::lane, lane_frame's ln) is one
// geometric test against the gate's solid (gate_solid: the ring by the gate's TGates row; the installed ordinary gates have none), on
// the ship's live position, velocity and nose (E's reads), its hull capsule, the sweep margin (kMarginM + k (t + the rollout's age)):
// * Before d_end (inbound / funnel, order 4 or the script's order-0 gap, at any distance): its path as site A flies it (lane_path: the
//   live position and velocity, the avoidance's fresh detour turn, the board's rollout over its horizon, fresh two-sided within the
//   row's state bound, else roll) clears the solid on every leg (else guard). On a gate with a solid d_end lies the ring's front + the
//   longest hull + a stop's run-out before the plane (lane_end), so a ship before d_end cannot reach the ring within a stop whatever
//   its heading; the path's legs past d_end (the hand-over, order 5, which the engine's pair filter exempts from contact with its own
//   gate, section 10.9 Q2, and the launch from the trigger sphere about the final Q, inside the hole by q_point's window: on the Terran
//   ring l + w <= h r - 50 m) are where the test discriminates.
// * The hand-over (order 0 / 0x1f after P2, kept or not; a lane frame past d_end before P2's tick): within kLaneHandM of its final line
//   with the live direction of travel (the velocity, or the nose below 1 m/s) within 8 deg of F, and its hull now and over its stop's
//   run-out along that direction (v^2 / 2a + 1 s) clear of the solid by the margin: lane; else the path guard below (no solid: the window
//   alone).
// On the installed ordinary gates (no solid) the row reduces to the path's freshness and the hand-over's window: those gates have no
// collision body, a ship can never touch them (section 10.9), so the engine's warn against them guards nothing physical.
constexpr float kLaneHandM = 200.0f;
constexpr float kSolidStepM = 50.0f; // the solid test's sample spacing along a leg (its gap less half of it)
constexpr unsigned kSolidMaxN = 256;  // round 2 review item 6: at most this many samples per leg (a longer leg: a wider spacing, charged)
constexpr unsigned kSolidMaxLines = 32; // round 2 review item 8: at most this many swept lines across a hull (solid_gap_sweep)
// The solid's distance from a point (the 2-D distance in (radial, axial) to the ring's rectangle [h, r_out] x [z0, z1]; 0 inside).
// (Always inlined: an out-of-line float return would travel through st(0) on i386, the x87 audit.)
__attribute__((always_inline)) inline float solid_dist(const Gate& g, const GateSolid& gs, const float p[3]) noexcept {
    const float d[3] = {p[0] - g.G[0], p[1] - g.G[1], p[2] - g.G[2]};
    const float z = sc::dot3(d, g.F), a = sc::dot3(d, g.R), b = sc::dot3(d, g.U), rho = sc::sqrt_ss(a * a + b * b);
    float dr = gs.h - rho, dz = gs.z0 - z;
    if (rho - gs.r_out > dr) dr = rho - gs.r_out;
    if (z - gs.z1 > dz) dz = z - gs.z1;
    if (dr < 0.0f) dr = 0.0f;
    if (dz < 0.0f) dz = 0.0f;
    return sc::sqrt_ss(dr * dr + dz * dz);
}
// A hull capsule swept along the segment ab (extended by L - w at both ends, radius w) against the solid: the least distance over samples
// kSolidStepM apart, less half the spacing and w (< 0: it reaches the solid). A segment entirely before the ring's front by more than its
// reach is not sampled. (Round 2 review item 6: a non-finite length fails closed, -1; at most kSolidMaxN + 1 samples, the wider spacing
// charged by the same half-spacing term.)
__attribute__((always_inline)) inline float solid_gap_seg(const Gate& g, const GateSolid& gs, const float a[3], const float b[3], float LA,
                                                          float wA) noexcept {
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float dl = sc::sqrt_ss(sc::dot3(d, d)), h = LA > wA ? LA - wA : 0.0f;
    float u[3] = {0.0f, 0.0f, 0.0f};
    if (dl > 1e-3f)
        for (unsigned i = 0; i < 3; ++i) u[i] = d[i] / dl;
    float p0[3], p1[3];
    for (unsigned i = 0; i < 3; ++i) {
        p0[i] = a[i] - u[i] * h;
        p1[i] = b[i] + u[i] * h;
    }
    const float gF = sc::dot3(g.G, g.F), z0 = sc::dot3(p0, g.F) - gF, z1 = sc::dot3(p1, g.F) - gF;
    if ((z0 > z1 ? z0 : z1) + wA < gs.z0 - 1000.0f) return 1000.0f; // (far before the ring's front)
    const float len = dl + 2.0f * h;
    if (!sc::finite_f(len)) return -1.0f;
    const unsigned n = len < kSolidStepM * float(kSolidMaxN - 1u) ? unsigned(len / kSolidStepM) + 1u : kSolidMaxN;
    float best = 3.4e38f;
    for (unsigned k = 0; k <= n; ++k) {
        const float t = float(k) / float(n);
        const float q[3] = {p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t, p0[2] + (p1[2] - p0[2]) * t};
        const float e = solid_dist(g, gs, q);
        if (e < best) best = e;
    }
    return best - 0.5f * len / float(n) - wA;
}
// Round 2 review item 8: the hand-over's hull, a capsule along the live nose f (half-length LA, radius wA), swept along the direction of
// travel dir (unit) over run: the parallelogram x + a f + b dir (|a| <= h = LA - wA, 0 <= b <= run) grown by wA. Covered by n lines along
// dir through the hull points a_k (the centres of n equal bins of width s = 2 h / n), each extended by (s / 2) |f.dir| at both ends: every
// point of the parallelogram lies within (s / 2) sin(theta) of one (theta: f to dir), which is charged; n = 2 h sin(theta) / kSolidStepM
// + 1 (at most kSolidMaxLines) keeps the charge under half the sample spacing. The nose along dir: one line, the capsule along dir as
// before. An unusable nose (|f| outside [0.5, 2] or not finite): the sphere LA swept along dir.
__attribute__((always_inline)) inline float solid_gap_sweep(const Gate& g, const GateSolid& gs, const float x[3], const float f[3],
                                                            const float dir[3], float run, float LA, float wA) noexcept {
    const float fl = sc::sqrt_ss(sc::dot3(f, f));
    if (!(fl >= 0.5f && fl <= 2.0f)) {
        const float e[3] = {x[0] + dir[0] * run, x[1] + dir[1] * run, x[2] + dir[2] * run};
        return solid_gap_seg(g, gs, x, e, LA, LA);
    }
    const float fu[3] = {f[0] / fl, f[1] / fl, f[2] / fl}, h = LA > wA ? LA - wA : 0.0f;
    const float c = sc::dot3(fu, dir), c_abs = c < 0.0f ? -c : c, s2 = 1.0f - c * c, sn = s2 > 0.0f ? sc::sqrt_ss(s2) : 0.0f;
    const float nf = 2.0f * h * sn / kSolidStepM;
    const unsigned n = nf < float(kSolidMaxLines - 1u) ? unsigned(nf) + 1u : kSolidMaxLines;
    const float sp = 2.0f * h / float(n), ext = 0.5f * sp * c_abs;
    float best = 3.4e38f;
    for (unsigned k = 0; k < n; ++k) {
        const float ak = -h + (float(k) + 0.5f) * sp;
        float p0[3], p1[3];
        for (unsigned i = 0; i < 3; ++i) {
            p0[i] = x[i] + fu[i] * ak - dir[i] * ext;
            p1[i] = x[i] + fu[i] * ak + dir[i] * (run + ext);
        }
        const float e = solid_gap_seg(g, gs, p0, p1, wA, wA);
        if (!(e >= best)) best = e; // (a NaN propagates: fail closed)
    }
    return best - 0.5f * sp * sn;
}
inline GateRow lane_gate_why(const Gate& g, const Member& m, const float x[3], const float u[3], const float f[3], const float* detour,
                             std::uint32_t now) noexcept {
    const GateSolid gs = gate_solid(g.type, g.r_gate);
    const float dE = dend(m), LA = m.L > m.w ? m.L : m.w, wA = m.w > 0.0f ? m.w : LA;
    const float dx[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]}, sx = sc::dot3(dx, g.F);
    const bool lane_stage = m.sb == Sb::inbound || m.sb == Sb::funnel;
    const float v = sc::sqrt_ss(sc::dot3(u, u));
    float dir[3];
    for (unsigned i = 0; i < 3; ++i) dir[i] = v >= 1.0f ? u[i] / v : f[i];
    if ((m.sb == Sb::handover || (lane_stage && sx > -dE)) && sx < 0.0f && sc::dot3(dir, g.F) >= kLaneHoldCos) {
        // on its final at or past d_end (the hand-over; an order-4 or order-0 frame before P2's tick)
        float lat[2];
        lane_lat_at(g, m, -dE, lat); // (its final: Q, or l_c for a member that entered as an escort)
        const float er = sc::dot3(dx, g.R) - lat[0], eu = sc::dot3(dx, g.U) - lat[1];
        if (er * er + eu * eu <= kLaneHandM * kLaneHandM) {
            if (!gs.solid) return GateRow::lane;
            // (round 2 review item 8: the hull along its live nose, swept along the direction of travel; the window above is the
            // velocity's, up to 8 deg off F, while the hull lies along the nose)
            const float a = m.params.a_lin > 0.1f ? m.params.a_lin : 0.1f, run = v * v / (2.0f * a) + v;
            return solid_gap_sweep(g, gs, x, f, dir, run, LA, wA) - kMarginM >= 0.0f ? GateRow::lane : GateRow::guard;
        }
    }
    if (!lane_stage) return GateRow::count; // (the path guard)
    if (!gs.solid) return lane_path_exists(g, m, x, u, detour, now) ? GateRow::lane : GateRow::roll; // (round 2 review item 7)
    LanePath P;
    if (!lane_path(g, m, x, u, detour, now, P)) return GateRow::roll;
    for (unsigned i = 0; i + 1 < P.n; ++i) {
        const float margin = kMarginM + kDriftMs * (P.t[i + 1] + P.age_s);
        if (!(solid_gap_seg(g, gs, P.p[i], P.p[i + 1], LA, wA) - margin >= 0.0f)) return GateRow::guard;
    }
    return GateRow::lane;
}
// ---- the predicted launch of a released member (lane_frame's escort gate and pace law; stage B's sequencer was deleted on
// 2026-10-15) --------------------------------------------------------------------------------------------------------------
// The order-5 leg from a centre distance d_gate at speed v0 to the end of order 8 (s): the distance to the trigger at the aligned
// ramp (v_max, the acceleration from v0 charged), plus order 8's 0.9 s.
__attribute__((always_inline)) inline float leg5_s(const Member& m, float d_gate, float v0) noexcept {
    const float vmax = m.params.v_max > 1.0f ? m.params.v_max : 1.0f, a = m.params.a_lin > 0.1f ? m.params.a_lin : 0.1f;
    float d = d_gate - (m.r + 0.05f * vmax);
    if (d < 0.0f) d = 0.0f;
    float t = d / vmax;
    if (v0 < vmax) t += (vmax - (v0 > 0.0f ? v0 : 0.0f)) * (vmax - (v0 > 0.0f ? v0 : 0.0f)) / (2.0f * a * vmax);
    return t + 0.9f;
}
// The predicted end of a released member's order 8 (game ms): the funnel to X at its speed (>= 10 m/s), the hand-over (1 s), the
// order-5 leg and order 8.
inline std::uint32_t eta_launch(const Gate& g, const Member& m, std::uint32_t now) noexcept {
    if (m.sb == Sb::launch) return (m.o8_ms ? m.o8_ms : now) + 900u;
    const float dg[3] = {m.st.x[0] - g.G[0], m.st.x[1] - g.G[1], m.st.x[2] - g.G[2]};
    if (m.sb == Sb::dock || m.sb == Sb::handover)
        return now + std::uint32_t(int(1000.0f * (leg5_s(m, sc::sqrt_ss(sc::dot3(dg, dg)), m.st.v) + (m.sb == Sb::handover ? 1.0f : 0.0f)))); // via int (x87 audit)
    if (m.sb == Sb::funnel) {
        float X[3];
        lambda4(g, m, X);
        const float dx[3] = {m.st.x[0] - X[0], m.st.x[1] - X[1], m.st.x[2] - X[2]}, xg[3] = {X[0] - g.G[0], X[1] - g.G[1], X[2] - g.G[2]};
        const float vx = m.v_sb > kYieldFloorMs ? m.v_sb : kYieldFloorMs;
        std::uint32_t t_x = now + std::uint32_t(int(1000.0f * sc::sqrt_ss(sc::dot3(dx, dx)) / vx));
        return t_x + std::uint32_t(int(1000.0f * (1.0f + leg5_s(m, sc::sqrt_ss(sc::dot3(xg, xg)), vx))));
    }
    return now;
}
// ---- Lane stage 1: the frame (convoy-gate-transit.md sections 6.1-6.6; stage B's sb_frame until 2026-10-15) -------
// Kept from stage B: the keys and the escorts' reservation (tie-breakers and the leader-first rule), the stages from the live orders
// (inbound = joining Lambda, funnel = on Lambda; hand-over, order 5, order 8), the refusal at the hand-over, the slot block from a capital
// arrival. Deleted: petals, rings, the release, the lap law, the funnel's one-ship rule, the head skip, the escort stage (section 6.9).
struct LaneEvent {
    std::uint32_t ship = 0, pred = 0;
    std::uint8_t finish = 0; // 0 the merge's start, 1 its completion (the ship_gate_ctl_merge row)
    Law law = Law::none;
    float gap = 0;
    std::uint32_t ms = 0; // the completion's duration since the start (game ms)
};
constexpr unsigned kLaneEvents = 16;
struct LaneStats {
    unsigned merges = 0, merges_done = 0, x_refused = 0, clock_resets = 0, members = 0;
    std::uint32_t x_wait_ms = 0;
    unsigned n_ev = 0, ev_dropped = 0;
    LaneEvent ev[kLaneEvents];
};
__attribute__((always_inline)) inline void lane_event(LaneStats* st, const Member& m, bool finish, float gap, std::uint32_t ms) noexcept {
    if (!st) return;
    if (st->n_ev >= kLaneEvents) {
        ++st->ev_dropped;
        return;
    }
    LaneEvent& e = st->ev[st->n_ev++];
    e.ship = m.ship;
    e.pred = m.lane_pred;
    e.finish = finish ? 1 : 0;
    e.law = m.law;
    e.gap = gap;
    e.ms = ms;
}
// Whether e is an escort of the leader L (its live 0x14 / 0x15 leader, or a reserved key of L's key class: an escort committed on its own
// order 4 keeps its place behind its leader through the key).
__attribute__((always_inline)) inline bool lane_escort_of(const Member& e, const Member& L) noexcept {
    if (&e == &L || !e.ship || !L.ship) return false;
    if (e.role == Role::escort && e.leader == L.ship) return true;
    return e.key_res && L.role == Role::committed && !L.key_res && L.key && (e.key & ~15u) == L.key && e.key != L.key;
}
// The ETA to d_end the lane order ranks by (s, at the lane speed): along Lambda when on it; the straight join plus the rest when joining;
// out to the turn point and back (with a half turn at the hull's rate) when re-departing.
inline float lane_eta(const Member& m) noexcept {
    const float v = lane_vmax(m) > kYieldFloorMs ? lane_vmax(m) : kYieldFloorMs, s_end = -dend(m), s = m.lane_s;
    float path;
    if (m.redep) {
        const float s_out = -(dend(m) + m.redep_out), W = m.params.W > 1e-3f ? m.params.W : 1e-3f;
        path = (s > s_out ? s - s_out : 0.0f) + (s_end - s_out) + sc::kPi * v / W;
    } else if (m.lane_on)
        path = s_end > s ? s_end - s : 0.0f;
    else {
        const float rest = s_end - s;
        path = rest > 0.0f ? sc::sqrt_ss(m.lane_lat * m.lane_lat + rest * rest) : m.lane_lat - rest;
    }
    return path / v;
}
// The lane order's score (review item 3): the ETA to d_end at the common speed kGateLargeCapMs (the large cap), from lane_eta's path; on
// Lambda the along-F distance (negative past d_end), so that the order on Lambda is the position order.
inline float lane_score(const Member& m) noexcept {
    const float v = sc::kGateLargeCapMs, s_end = -dend(m), s = m.lane_s;
    if (m.redep) {
        const float s_out = -(dend(m) + m.redep_out), W = m.params.W > 1e-3f ? m.params.W : 1e-3f;
        return ((s > s_out ? s - s_out : 0.0f) + (s_end - s_out) + sc::kPi * v / W) / v;
    }
    if (m.lane_on) return (s_end - s) / v;
    const float rest = s_end - s;
    return (rest > 0.0f ? sc::sqrt_ss(m.lane_lat * m.lane_lat + rest * rest) : m.lane_lat - rest) / v;
}
// A lane member's predicted end of order 8 (game ms): to d_end at the lane speed, the hand-over (1 s), the order-5 leg from d_end and
// order 8; the prediction (eta_launch) from the hand-over on.
inline std::uint32_t lane_launch_eta(const Gate& g, const Member& m, std::uint32_t now) noexcept {
    if (m.sb == Sb::inbound || m.sb == Sb::funnel) {
        const float v = lane_vmax(m) > kYieldFloorMs ? lane_vmax(m) : kYieldFloorMs;
        return now + std::uint32_t(std::int32_t(1000.0f * (m.lane_eta + 1.0f + leg5_s(m, dend(m), v))));
    }
    return eta_launch(g, m, now);
}
// Lane fix round 1 item 3: a hand-over's wait beyond 3 s (ctl_x_wait_s), accrued up to now (x_acc_ms: what is counted already).
__attribute__((always_inline)) inline void lane_x_wait(Member& m, std::uint32_t now, LaneStats* st) noexcept {
    const std::uint32_t w = now - m.x_ms > 3000u ? now - m.x_ms - 3000u : 0u;
    if (w > m.x_acc_ms) {
        if (st) st->x_wait_ms += w - m.x_acc_ms;
        m.x_acc_ms = w;
    }
}
inline void lane_frame(Gate& g, std::uint32_t now, LaneStats* st) noexcept {
    if (!g.gate || !(g.r_gate > 0.0f)) return;
    if (g.sb_last_ms && std::int32_t(now - g.sb_last_ms) < 0) { // a backward game-clock jump (a load): stamps from the future are void
        g.slot_block_ms = g.funnel_end_ms = g.last_arrival_seen = 0;
        g.lane_n = 0;
        for (unsigned i = 0; i < g.n; ++i) {
            Member& m = g.m[i];
            m.x_ms = m.o8_ms = 0;
            m.refused = 0;
            m.cap = -1.0f;
            m.cap_ms = m.yield_ms = m.v_plan_ms = 0;
            m.merge_ms = m.stop_ms = m.lane_free_ms = m.lane_gate_ms = 0;
            m.merged = 0;
            m.lane_pred = 0;
            m.lstop_ms = m.lstop_last_ms = m.x_acc_ms = 0; // (lane fix round 1)
            m.defer_w = 0;      // final review item 2 (ship-navigation-ai.md section 10.13 item 4): no deferral survives a load
            m.lane_ord_ok = 0; // final review item 1: the sticky order moments are of the old clock
            m.roll_ms = 0;     // lane fix round 2 item 4 (review 7): a rollout of the old clock is no certificate
            m.n = 0;
            if (m.exit_ms) m.exit_ms = now ? now : 1; // (item 2: the exit's bound restarts on the new clock)
            // (round 2 review item 1: an overshoot's predicted return is of the old clock: now - over_ret_ms would stay negative and
            // its block hold for the rest of the out-leg; the return is re-taken from here by the lane order's over_new pass)
            m.over_ret_ms = 0;
            m.over_ms = m.over ? (now ? now : 1) : 0;
            m.over_new = m.over;
        }
        if (st) ++st->clock_resets;
    }
    g.sb_last_ms = now ? now : 1;
    if (g.cap_arrival_ms && g.cap_arrival_ms != g.last_arrival_seen) { // a capital launch here: our side refused 60 s (section 10.2)
        g.last_arrival_seen = g.cap_arrival_ms;
        g.slot_block_ms = g.cap_arrival_ms + kSlotArrivalMs;
    }
    // the finals (Q, lane_q) and Lambda's common lateral l_c: the middle of the members' finals
    const bool terran = gate_terran(g.type);
    float lo[2] = {3.4e38f, 3.4e38f}, hi[2] = {-3.4e38f, -3.4e38f};
    for (unsigned i = 0; i < g.n; ++i) {
        Member& m = g.m[i];
        if (!m.ship || !(m.role == Role::committed || m.role == Role::escort)) continue;
        m.ln = 1;
        m.d_end = lane_end(g.r_gate, g.type); // (lane fix round 2 item 4: the gate's d_end)
        // lane fix round 2 item 2: an escort whose live 0x14 / 0x15 names another leader is leaving the lane (validity keeps it until clear)
        if (m.role == Role::escort && (m.st.order == sc::kOrderEscort || m.st.order == sc::kOrderFollow) && m.st.target && m.st.target != m.leader) {
            if (!m.exit_ms || m.exit_tgt != m.st.target) {
                m.exit_ms = now ? now : 1;
                m.exit_tgt = m.st.target;
                m.exit_clear = 0;
            }
        } else if (m.exit_ms && (m.role != Role::escort || m.st.target == m.leader))
            m.exit_ms = m.exit_tgt = m.exit_clear = 0; // (handed back; round 2 review item 2: or no longer an escort)
        if (m.role == Role::escort) m.lane_esc = 1;
        m.q_ok = lane_q(m.r, m.w, m.L, m.params.v_max, g.r_gate, terran, m.q) ? 1 : 0;
        m.lane_settle = pursuit_look(lane_vmax(m), m.params.W, g.r_gate); // (lane fix round 1: lane_blend_settle's cache)
        if (!m.q_ok) continue;
        m.l4[0] = m.a_lat[0] = m.q[0];
        m.l4[1] = m.a_lat[1] = m.q[1];
        m.zx = -dend(m); // lambda4 (eta_launch, site E's guard line): the lane point at d_end
        for (unsigned c = 0; c < 2; ++c) {
            if (m.q[c] < lo[c]) lo[c] = m.q[c];
            if (m.q[c] > hi[c]) hi[c] = m.q[c];
        }
    }
    if (lo[0] <= hi[0]) { // l_c: the middle of the members' finals, set at once on an empty lane, else rate-limited (kLaneShiftMs)
        float tc[2] = {0.5f * (lo[0] + hi[0]), 0.5f * (lo[1] + hi[1])};
        // (lane fix round 2 item 4: on a gate with a solid l_c stays the middle of the finals, inside the ring's hole where every
        // final's window lies: an escort holding on l_c at d_end, then order 5, then order 8 from its trigger, passes the hole; its
        // order 4's first tick is deferred by site B (lane_defer_step), the grid spheres' first row lies at 2 (r_gate + r_s), far
        // beyond d_end)
        const float clear = -(sc::kGateCrossFraction * g.r_gate + kLaneGridClearM);
        if (tc[1] > clear && !gate_solid(g.type, g.r_gate).solid) tc[1] = clear;
        if (!g.lane_c_ok) {
            g.lane_c[0] = tc[0];
            g.lane_c[1] = tc[1];
            g.lane_c_ok = 1;
        } else {
            const float lim = kLaneShiftMs * float(g.step_ms > kStepClampMs ? kStepClampMs : g.step_ms) * 0.001f;
            for (unsigned c = 0; c < 2; ++c) {
                const float d = tc[c] - g.lane_c[c];
                g.lane_c[c] += d > lim ? lim : d < -lim ? -lim : d;
            }
        }
    } else
        g.lane_c_ok = 0;
    // keys, reservations, stages, the position on Lambda
    for (unsigned i = 0; i < g.n; ++i) {
        Member& m = g.m[i];
        if (!m.ship || !(m.role == Role::committed || m.role == Role::escort)) {
            if (m.ship) m.sb = Sb::none;
            m.lane_rank = 0xff;
            m.law = Law::none;
            continue;
        }
        if (!m.st.ms) continue;
        if (!m.key) m.key = m.seq << 4;
        const Sb before = m.sb;
        if (m.role == Role::escort) {
            const int li = find(g, m.leader);
            if (!m.key_res && li >= 0 && g.m[li].role == Role::committed && g.m[li].key) { // the reservation, once (D.3): a tie-breaker
                unsigned rank = 0;
                for (unsigned j = 0; j < g.n; ++j)
                    if (j != i && g.m[j].role == Role::escort && g.m[j].leader == m.leader && g.m[j].seq < m.seq) ++rank;
                m.key = g.m[li].key + 1u + (rank < 14u ? rank : 14u);
                m.key_res = 1;
            }
            // the leader on order 5 or 8: its launch (predicted on order 5, the order-8 stamp once seen; a SETA step can hide the 0.9 s of
            // order 8 from every frame); the escort stays a member through the lost-leader wait until its own order 4 commits it
            if (li >= 0 && g.m[li].role == Role::committed && (g.m[li].sb == Sb::dock || (g.m[li].sb == Sb::launch && g.m[li].o8_ms))) {
                m.res_o8_ms = g.m[li].sb == Sb::launch ? g.m[li].o8_ms : eta_launch(g, g.m[li], now) - 900u;
                if (!m.res_o8_ms) m.res_o8_ms = 1;
                if (!m.res_until) m.res_until = m.res_o8_ms + kReserveMs;
            }
            if (m.res_until && m.key_res) { // B.3 item 4: refreshed while on 0x15 to the gate or within kReserveKeepM of it
                const float d[3] = {m.st.x[0] - g.G[0], m.st.x[1] - g.G[1], m.st.x[2] - g.G[2]};
                if ((m.st.order == sc::kOrderFollow && m.st.target == g.gate) || sc::dot3(d, d) <= kReserveKeepM * kReserveKeepM) {
                    const std::uint32_t until = now + kReserveMs;
                    if (std::int32_t(until - m.res_until) > 0) m.res_until = until;
                }
            }
            if (m.sb != Sb::funnel) m.sb = Sb::inbound;
        } else {
            const std::uint16_t o = m.st.order;
            if (o == sc::kOrderFlyBefore) {
                if (m.sb != Sb::inbound && m.sb != Sb::funnel) {
                    m.x_ms = m.o8_ms = 0;
                    m.refused = 0;
                    m.sb = Sb::inbound;
                }
            } else if ((o == sc::kOrderNone || o == sc::kOrderRotate) && m.handover_ms) {
                if (m.sb != Sb::handover) {
                    m.sb = Sb::handover;
                    m.x_ms = m.handover_ms;
                    m.refused = 0;
                    m.o8_ms = 0;
                    m.x_acc_ms = 0;
                }
            } else if (o == sc::kOrderDock) {
                if (m.sb == Sb::handover && m.x_ms) lane_x_wait(m, now, st); // (the rest of the wait)
                m.sb = Sb::dock;
                m.handover_ms = m.x_ms = 0;
                m.refused = 0;
            } else if (o == sc::kOrderWarpAway) {
                m.sb = Sb::launch;
                m.handover_ms = m.x_ms = 0;
                m.refused = 0;
                if (!m.o8_ms) m.o8_ms = m.st.ms;
                const std::uint32_t end = m.o8_ms + 900u;
                if (!g.funnel_end_ms || std::int32_t(end - g.funnel_end_ms) > 0) g.funnel_end_ms = end;
            }
            // waiting at d_end for the through slot (review item 8: a signed game-time bound plus the frame step)
            if (m.sb == Sb::handover && !m.refused && m.x_ms && std::int32_t(now - m.x_ms) > std::int32_t(age_bound(g, kLaneRefuseMs))) {
                m.refused = 1;
                if (st) ++st->x_refused;
            }
            // lane fix round 1 item 3 (Run173: a 116 s refusal left ctl_x_wait_s at 0, the wait was counted only on an observed order 5):
            // the wait beyond 3 s accrues every frame from the refusal
            if (m.sb == Sb::handover && m.refused && m.x_ms) lane_x_wait(m, now, st);
        }
        const float d[3] = {m.st.x[0] - g.G[0], m.st.x[1] - g.G[1], m.st.x[2] - g.G[2]};
        m.lane_s = sc::dot3(d, g.F);
        float lat[2];
        lane_lat_at(g, m, m.lane_s, lat);
        const float er = sc::dot3(d, g.R) - lat[0], eu = sc::dot3(d, g.U) - lat[1];
        m.lane_lat = sc::sqrt_ss(er * er + eu * eu);
        const float fF = sc::dot3(m.st.rows[2], g.F);
        if (m.lane_on) {
            if (m.lane_lat > kLaneOffM || fF < 0.0f) m.lane_on = 0;
        } else if (m.lane_lat <= kLaneOnM && fF >= kLaneOnCos)
            m.lane_on = 1;
        // lane fix round 1 item 4 (Run173: E1 past the plane at s = +703 m after an 0x12 flew out 6 km, the re-departure's leg, to turn):
        // a member past the plane without launching is an overshoot until it is back on Lambda; its turn point lies its settling distance
        // + its turn radius R = v / W beyond d_end (the U-turn ends near it heading +F, then the settling run to P2: without the radius the
        // nose came to P2 12-14 deg off F in scenario C), not a re-departure's kLaneRedepM; the U-turn's own extent beyond it is
        // kLaneOverApexR R at most (scenario C overshoot: 1.53-1.69 km for an M1 at 100 m/s, R = 637 m: the pursuit's turn and the slip)
        // (lane fix round 2 item 5: only a member seen in front of the plane since it entered: one that committed from behind the gate is a
        // re-departure, not an overshoot ranked as the head)
        if (m.lane_s < 0.0f) m.front_seen = 1;
        if (m.sb == Sb::inbound || m.sb == Sb::funnel) {
            if (m.lane_s > 0.0f && m.front_seen) {
                if (!m.over) {
                    m.over_ms = now ? now : 1;
                    m.over_new = 1;
                    m.over_in = 0;
                }
                m.over = 1;
            } else if (m.lane_on || !m.front_seen)
                m.over = 0;
        } else
            m.over = 0;
        // (its out-leg runs 2.5 R below Lambda, the U-turn's measured lateral extent, so the U-turn ends on Lambda heading +F: with 2 R it
        // ended about R / 2 above it and P2 came 8.5 deg off F in 2 of 9 runs)
        m.redep_r = m.over && m.params.W > 1e-3f ? lane_vmax(m) / m.params.W : 0.0f;
        m.redep_out = m.over ? lane_blend_settle(g, m) + m.redep_r : kLaneRedepM;
        m.redep_side = m.over ? 2.5f * m.redep_r : kLaneRedepSideM;
        if (m.redep && m.lane_s < -(dend(m) + m.redep_out) && fF >= 0.0f) { // turned in: the join from here
            m.redep = 0;
            if (m.over) m.over_in = 1; // (item 3c: an overshoot's law runs from its own s from here)
        }
        if (!m.lane_on && fF < 0.0f && m.lane_s > -(dend(m) + m.redep_out)) m.redep = 1; // nose away inside the turn point: out first
        if (m.lane_on) m.redep = 0;
        if (m.sb == Sb::inbound || m.sb == Sb::funnel) m.sb = m.lane_on ? Sb::funnel : Sb::inbound;
        m.lane_eta = lane_eta(m);
        (void)before;
    }
    // the lane order (section 6.2): members joining or on Lambda, front first, by one score each: the ETA to d_end at the common speed
    // (lane_score; review item 3: on Lambda that is the position, so a slow hull ahead is never ranked behind a fast one closing from
    // behind, and a joiner compares against the on-lane ship's position on the same scale). A strict order, rebuilt every frame (final
    // review item 1: HEAD's adjacent swaps from the previous order let a joiner inside the hysteresis of both neighbours hold two ships on
    // Lambda against their positions): (1) the members on Lambda by position (the exact score; no hysteresis ever reorders two of them);
    // (2) the joiners by a sticky score (lane_ord_ms: the absolute moment of the score, re-taken only when the live one drifts more than
    // kLaneKeepS / 2 from it, so two joiners keep their order unless their ETAs differ by up to kLaneKeepS), within a tie cluster (members
    // within kLaneTieS of the cluster's first) the lower key first; (3) the two chains merged, a joiner before an on-lane ship when its
    // score is earlier beyond the tie band or, within it, its key is lower; (4) a leader always before its own escorts. Every step keeps a
    // total order (ties: the key, then the ship). Cost: insertion sorts and the leader move, O(n^2) comparisons worst case for n members
    // (n <= kMembers = 24), O(n) on an already ordered lane.
    unsigned idx[kMembers], n = 0, on_ix[kMembers], jn_ix[kMembers], n_on = 0, n_jn = 0;
    std::uint8_t onf[kMembers];
    float score[kMembers];
    auto before = [&](unsigned a, unsigned b) -> bool { // the strict total order on (score, key, ship)
        if (score[a] != score[b]) return score[a] < score[b];
        if (g.m[a].key != g.m[b].key) return g.m[a].key < g.m[b].key;
        return g.m[a].ship < g.m[b].ship;
    };
    auto insert = [&](unsigned* arr, unsigned& cnt, unsigned i) { // insertion into a chain sorted by `before`
        unsigned j = cnt;
        while (j > 0 && before(i, arr[j - 1])) {
            arr[j] = arr[j - 1];
            --j;
        }
        arr[j] = i;
        ++cnt;
    };
    constexpr std::int32_t kKeepHalfMs = std::int32_t(kLaneKeepS * 500.0f);
    for (unsigned i = 0; i < g.n; ++i) {
        Member& m = g.m[i];
        onf[i] = m.ship && (m.role == Role::committed || m.role == Role::escort) && live(g, m, now) && m.q_ok &&
                 (m.sb == Sb::inbound || m.sb == Sb::funnel);
        if (!onf[i]) {
            m.lane_ord_ok = 0;
            m.lane_rank = 0xff;
            m.lane_pred = 0;
            m.lane_free_ms = m.lane_gate_ms = 0;
            if (m.ship && (m.role == Role::committed || m.role == Role::escort)) {
                m.law = m.sb == Sb::handover ? Law::hold : Law::none;
                m.v_sb = lane_vmax(m);
            }
            continue;
        }
        const float sc_live = lane_score(m);
        const float ms_f = 1000.0f * (sc_live > -2.0e6f ? (sc_live < 2.0e6f ? sc_live : 2.0e6f) : -2.0e6f); // (an int32 of game ms)
        const std::uint32_t t = now + std::uint32_t(std::int32_t(ms_f));
        if (m.exit_ms && m.role == Role::escort) { // lane fix round 2 item 2: a leaving escort keeps its place in the order (its moment
                                                   // frozen at the hand), so the ship behind it keeps following it until it is clear
            if (!m.lane_ord_ok) {
                m.lane_ord_ms = t;
                m.lane_ord_ok = 1;
            }
            score[i] = 0.001f * float(std::int32_t(m.lane_ord_ms - now));
            insert(jn_ix, n_jn, i);
            continue;
        }
        if (m.lane_on || !m.lane_ord_ok) {
            m.lane_ord_ms = t;
            m.lane_ord_ok = 1;
        } else {
            const std::int32_t drift = std::int32_t(t - m.lane_ord_ms);
            if (drift > kKeepHalfMs || drift < -kKeepHalfMs) m.lane_ord_ms = t;
        }
        score[i] = m.lane_on ? sc_live : 0.001f * float(std::int32_t(m.lane_ord_ms - now));
        if (m.lane_on) insert(on_ix, n_on, i);
        else insert(jn_ix, n_jn, i);
    }
    for (unsigned c = 0; c < n_jn;) { // the joiners' tie clusters: within kLaneTieS of the cluster's first, the lower key first
        unsigned e = c + 1;
        while (e < n_jn && score[jn_ix[e]] - score[jn_ix[c]] <= kLaneTieS) ++e;
        for (unsigned a = c + 1; a < e; ++a) { // insertion by (key, ship) inside [c, e)
            const unsigned x = jn_ix[a];
            unsigned j = a;
            while (j > c && (g.m[x].key != g.m[jn_ix[j - 1]].key ? g.m[x].key < g.m[jn_ix[j - 1]].key : g.m[x].ship < g.m[jn_ix[j - 1]].ship)) {
                jn_ix[j] = jn_ix[j - 1];
                --j;
            }
            jn_ix[j] = x;
        }
        c = e;
    }
    for (unsigned a = 0, b = 0; a < n_on || b < n_jn;) { // the merge: the on-lane chain keeps its position order whatever the joiners
        bool take_j = a >= n_on;
        if (!take_j && b < n_jn) {
            const Member &J = g.m[jn_ix[b]], &O = g.m[on_ix[a]];
            const float ds = score[jn_ix[b]] - score[on_ix[a]];
            take_j = sc::fabs_f(ds) <= kLaneTieS ? (J.key != O.key ? J.key < O.key : J.ship < O.ship) : ds < 0.0f;
        }
        idx[n++] = take_j ? jn_ix[b++] : on_ix[a++];
    }
    // lane fix round 1 item 4 (Run173: E1 past the plane, the closest to the gate, was ranked behind E2 4 km out as a re-departing joiner):
    // an overshoot never ranks behind a member farther from the gate (it rejoins as the head when it is the closest); O(n) per overshoot
    // (lane fix round 2 items 3a / 5: a member inside the overshoot's return zone when the overshoot began, nearer the gate than its U-turn's
    // apex less g_req, both half-lengths and that member's own stop, stays ahead of it: the overshoot rejoins behind it and follows it (its
    // law from the apex); the members farther out rank behind the overshoot and stop at spacing behind its apex. The zone is decided once:
    // a member's own stop toward the apex would otherwise flip it in and out)
    unsigned ov[kMembers], n_ov = 0;
    for (unsigned k = 0; k < n; ++k)
        if (g.m[idx[k]].over) ov[n_ov++] = idx[k];
    for (unsigned t = 0; t < n_ov; ++t) {
        const unsigned x = ov[t];
        Member& o = g.m[x];
        const float d[3] = {o.st.x[0] - g.G[0], o.st.x[1] - g.G[1], o.st.x[2] - g.G[2]}, dO = sc::dot3(d, d);
        const float s_ap = -(dend(o) + o.redep_out + kLaneOverApexR * o.redep_r);
        if (o.over_new) { // its return: the out-leg to its turn point, a U-turn of pi R and the rejoin to its apex at the lane speed
            const float vl = lane_vmax(o) > kYieldFloorMs ? lane_vmax(o) : kYieldFloorMs;
            const float t_ret = (o.lane_s + dend(o) + o.redep_out + (3.14159265f + kLaneOverApexR) * o.redep_r) / vl;
            o.over_ret_ms = now + std::uint32_t(int(1000.0f * (t_ret > 0.0f ? t_ret : 0.0f)));
        }
        if (o.over_new)
            for (unsigned q2 = 0; q2 < n; ++q2) {
                Member& a = g.m[idx[q2]];
                if (&a == &o) continue;
                const float va = sc::dot3(a.st.u, g.F), aa = a.params.a_lin > 0.1f ? a.params.a_lin : 0.1f;
                if (a.lane_s > s_ap - (kLaneGapM + o.L + a.L + (va > 0.0f ? va * va / (2.0f * aa) : 0.0f))) a.over_ahead_of = o.ship;
                else if (a.over_ahead_of == o.ship) a.over_ahead_of = 0;
            }
        unsigned k = 0;
        while (idx[k] != x) ++k;
        for (unsigned j = k; j + 1 < n; ++j) idx[j] = idx[j + 1]; // (out, then in at its place)
        unsigned q = 0;
        for (; q + 1 < n; ++q) {
            const Member& a = g.m[idx[q]];
            const float e[3] = {a.st.x[0] - g.G[0], a.st.x[1] - g.G[1], a.st.x[2] - g.G[2]};
            if (sc::dot3(e, e) > dO && a.over_ahead_of != o.ship) break;
            if (q >= k && a.over_ahead_of != o.ship) break; // (no further back than its own place but behind its zone's members)
        }
        for (unsigned j = n - 1; j > q; --j) idx[j] = idx[j - 1];
        idx[q] = x;
    }
    for (unsigned k = 0; k < n; ++k) g.m[idx[k]].over_new = 0;
    for (unsigned k = 0; k < n;) { // the leader-first rule: an escort ahead of its leader moves right behind it
        unsigned j = n;
        for (unsigned q = k + 1; q < n && j == n; ++q)
            if (lane_escort_of(g.m[idx[k]], g.m[idx[q]])) j = q;
        if (j == n) {
            ++k;
            continue;
        }
        const unsigned e = idx[k];
        for (unsigned q = k; q < j; ++q) idx[q] = idx[q + 1];
        idx[j] = e;
    }
    // the slot's free time (the head's pacing, section 6.6): the through slot's holder (a committed member at the hand-over, on order 5,
    // or on order 8 before its end) releases at its predicted launch + the hand-over gap; a capital arrival's block
    std::uint32_t t_free = 0;
    for (unsigned i = 0; i < g.n; ++i) {
        const Member& o = g.m[i];
        if (!o.ship || o.role != Role::committed || !live(g, o, now)) continue;
        if (!(o.sb == Sb::handover || o.sb == Sb::dock || (o.sb == Sb::launch && std::int32_t(now - (o.o8_ms + 900u)) < 0))) continue;
        const std::uint32_t t = eta_launch(g, o, now) + kFunnelGapMs;
        if (!t_free || std::int32_t(t - t_free) > 0) t_free = t;
    }
    if (g.slot_block_ms && std::int32_t(g.slot_block_ms - now) > 0 && (!t_free || std::int32_t(g.slot_block_ms - t_free) > 0)) t_free = g.slot_block_ms;
    if (t_free && std::int32_t(t_free - now) <= 0) t_free = 0;
    // the head's predecessor: the rearmost committed member past d_end (the hand-over, order 5, order 8 before its end): it is ahead on
    // the same final, so the head follows it as any follower (it is no lane rank: the stage-A rank rule keeps it ahead)
    int gp = -1;
    for (unsigned i = 0; i < g.n; ++i) {
        const Member& o = g.m[i];
        if (!o.ship || o.role != Role::committed || !live(g, o, now)) continue;
        if (!(o.sb == Sb::handover || o.sb == Sb::dock || (o.sb == Sb::launch && std::int32_t(now - (o.o8_ms + 900u)) < 0))) continue;
        const float d[3] = {o.st.x[0] - g.G[0], o.st.x[1] - g.G[1], o.st.x[2] - g.G[2]};
        g.m[i].lane_s = sc::dot3(d, g.F);
        if (gp < 0 || g.m[i].lane_s < g.m[gp].lane_s) gp = int(i);
    }
    // the laws, front first (the follower reads its predecessor's speed of this frame)
    g.lane_n = n;
    for (unsigned k = 0; k < n; ++k) {
        Member& m = g.m[idx[k]];
        g.lane_ord[k] = m.ship;
        m.lane_rank = std::uint8_t(k);
        const Member* P = k ? &g.m[idx[k - 1]] : gp >= 0 ? &g.m[gp] : nullptr;
        // lane fix round 2 item 2: leaving (handed to another leader): its laws as before (lane_steer: their speed on the engine's leg to
        // the new leader); clear once its hull is a lane footprint (kLaneOffM) off Lambda
        if (m.exit_ms && m.role == Role::escort) m.exit_clear = m.lane_lat >= kLaneOffM + m.w ? 1 : 0;
        LaneCtx cx;
        // (lane fix round 2 items 3a / 5: an overshoot behind a member inside its return zone runs its law from its U-turn's apex, where it
        // rejoins)
        // (lane fix round 2 item 3c: the head too, until it has turned in: its pacing and its follow law measured from where it rejoins, not
        // from its position past d_end, where the pacing's stop at d_end held it 19 s in its turn back)
        const float s_self_ap = -(dend(m) + m.redep_out + kLaneOverApexR * m.redep_r);
        const float s_self = m.over && !m.over_in && m.lane_s > s_self_ap ? s_self_ap : m.lane_s;
        cx.s = s_self;
        cx.v_lane = lane_vmax(m);
        cx.a = m.params.a_lin;
        cx.v_self = sc::dot3(m.st.u, g.F);
        cx.y = m.lane_on ? 0.0f : m.lane_lat;
        float gap = 3.4e38f;
        cx.d_end = dend(m);
        cx.loss = lane_join_loss(m.lane_s, cx.y, lane_join_end(m), dend(m), lane_turn_r(m));
        if (P && k) cx.loss -= lane_join_loss(P->lane_s, P->lane_on ? 0.0f : P->lane_lat, lane_join_end(*P), dend(*P), lane_turn_r(*P));
        if (P) {
            // (lane fix round 1 item 4: an overshoot ahead turns back no farther out than its U-turn's apex: no nearer than that along F;
            // until it has turned in it is a block, its follower stops at spacing behind the apex)
            const float s_rj = -(dend(*P) + P->redep_out + kLaneOverApexR * P->redep_r), sp = k && P->over && !P->over_in && P->lane_s > s_rj ? s_rj : P->lane_s;
            gap = (sp - P->L) - (s_self + m.L);
            float vp = sc::dot3(P->st.u, g.F);
            cx.closing = vp < -kLaneStopV && !(k && P->over); // (lane fix round 2 item 5: flying back toward it; an overshoot's out-leg is
                                                               // its block, bounded below)
            if (P->v_sb >= 0.0f && P->v_sb < vp) vp = P->v_sb;
            cx.pred = true;
            cx.gap = gap;
            cx.v_pred = vp > 0.0f ? vp : 0.0f;
            // (a block: stopped, or an overshoot turning back; lane fix round 2 item 5: the overshoot's block lasts its predicted return,
            // the out-leg to its turn point, a U-turn of pi R and the rejoin to its apex at the lane speed, + kLaneStopMs + the step; past
            // it the pair is the certificate's and the law's stop is bounded by T_stop as for any non-block)
            const bool over_block = k && P->over && P->redep && P->over_ret_ms &&
                                    std::int32_t(now - P->over_ret_ms) <= std::int32_t(kLaneStopMs + g.step_ms);
            // (lane fix round 2 item 3d: a predecessor stopped at d_end on a slot refusal is the queue hold's block, which lasts the refusal;
            // its grant, order 5 or 8 seen, releases it at once, not once its speed passes kLaneStopV: the follower's stop ends within the step)
            cx.granted = P->sb == Sb::dock || P->sb == Sb::launch;
            cx.block = (sc::dot3(P->st.u, P->st.u) < kLaneStopV * kLaneStopV && !cx.granted) || over_block;
            cx.pred_final = P->sb == Sb::handover || P->lane_s > -(dend(*P) + lane_blend_settle(g, *P));
        }
        if (m.lane_pred != (P ? P->ship : 0u)) { // a new pair: a merge when the gap is short
            m.lane_pred = P ? P->ship : 0u;
            m.merged = !P || gap >= kLaneGapM - 50.0f ? 1 : 0;
            m.merge_done = m.merged;
            m.merge_ms = 0;
            if (!m.merged) {
                m.merge_ms = now ? now : 1;
                ++g.lane_merges;
                if (st) ++st->merges;
                m.law = Law::merge;
                lane_event(st, m, false, gap, 0);
            }
        }
        // a merge behind a ship that is itself still merging starts when that one settles (its speed is the floor until then; the
        // follower's 40 s bound of section 6.11 item 2 runs from there): the start is re-stamped meanwhile
        if (!m.merged && m.merge_ms && P && P->lane_rank != 0xff && !P->merged && P->merge_ms) m.merge_ms = now ? now : 1;
        if (!P) m.merge_done = 1;
        cx.merged = m.merged != 0;
        m.lane_free_ms = k == 0 ? t_free : 0u;
        cx.head = m.lane_free_ms != 0;
        cx.t_free_s = cx.head ? float(std::int32_t(m.lane_free_ms - now)) * 0.001f : 0.0f;
        m.lane_gate_ms = 0;
        if (m.role == Role::escort) { // the escort gate (section 6.5): the braking reserve; lane_gate_ms (the row) its leader's launch +
                                      // lambda_max + the step, the latest moment the script's order 4 is expected
            std::uint32_t t_l = now;
            const int li = find(g, m.leader);
            if (m.res_until) t_l = m.res_o8_ms + 900u;
            else if (li >= 0 && g.m[li].role == Role::committed) t_l = lane_launch_eta(g, g.m[li], now);
            m.lane_gate_ms = t_l + kLaneLambdaMaxMs + g.step_ms + kLaneCrawlSlackMs;
            if (!m.lane_gate_ms) m.lane_gate_ms = 1;
            cx.gate = true;
            cx.t_gate_s = float(std::int32_t(m.lane_gate_ms - now)) * 0.001f;
            cx.dt = float(g.step_ms > kStepClampMs ? kStepClampMs : g.step_ms) * 0.001f;
        }
        // lane fix round 1 item 4: the law's stop lasts at most T_stop + the step unless the ship ahead is physically stopped (a block); each
        // stop episode counts in lane_stops (ctl_merge_stop); an episode ends once its condition has been off for kLaneStopEndMs + the step
        // (the follow law flickers across the stop's threshold while it brakes)
        cx.stop_left_s = float(std::int32_t((m.lstop_ms ? m.lstop_ms : now) + kLaneStopMs + g.step_ms - now)) * 0.001f;
        Law law;
        bool merged, stop_cond;
        m.v_sb = lane_law(cx, &law, &merged, &stop_cond);
        if (stop_cond) {
            if (!m.lstop_ms) {
                m.lstop_ms = now ? now : 1;
                ++g.lane_stops;
            }
            m.lstop_last_ms = now ? now : 1;
        } else if (m.lstop_ms && std::int32_t(now - m.lstop_last_ms) > std::int32_t(kLaneStopEndMs + g.step_ms))
            m.lstop_ms = m.lstop_last_ms = 0;
        if (law == Law::lane && !m.lane_on) law = Law::join;
        m.law = law;
        m.lane_gap = gap;
        if (merged && !m.merged) m.merged = 1;
        if (!m.merge_done && m.merged && gap >= kLaneGapM - 50.0f) { // review item 6: the merge completes on the raw gap (the finish
                                                                       // row; the follow law settles a few m short of g_req at the cap)
            m.merge_done = 1;
            if (st) ++st->merges_done;
            lane_event(st, m, true, gap, m.merge_ms ? now - m.merge_ms : 0u);
        }
    }
    if (st) st->members = n;
}
// Site B's lane write on a member's frame (section 6.3-6.5; the DLL's clamp hook, the fixture's site B): a stop in force (the law's
// stop, or the brief stop's cap 0) writes 0 on the steered orders and order 0; on order 0 / 0x1f with the live nose f within 8 deg of F
// the lane speed stands (the script's re-issue gaps, the lost-leader wait, the hand-over; the refusal's brake bound). False: no write.
// (*kind, optional: the write's LaneWrite for the lane row)
enum class LaneWrite : std::uint8_t { none = 0, stop, kept, hold, unkept, count };
inline const char* lane_write_name(LaneWrite w) noexcept {
    static const char* const n[unsigned(LaneWrite::count)] = {"none", "stop", "kept", "hold", "unkept"};
    return unsigned(w) < unsigned(LaneWrite::count) ? n[unsigned(w)] : "?";
}
inline bool lane_site_b(const Gate& g, const Member& m, std::uint16_t order, const float x[3], const float f[3], float v_now, std::uint32_t now,
                        float* v, LaneWrite* kind = nullptr) noexcept {
    if (kind) *kind = LaneWrite::none;
    if (!m.ln || !(m.role == Role::committed || m.role == Role::escort)) return false;
    const bool steered = (m.role == Role::committed && order == sc::kOrderFlyBefore) ||
                         (m.role == Role::escort && (order == sc::kOrderEscort || order == sc::kOrderFollow));
    const bool lane = m.sb == Sb::inbound || m.sb == Sb::funnel;
    const float cap = cap_in_force(g, m, now);
    if (lane && (steered || order == sc::kOrderNone) && (m.law == Law::stop || (cap == 0.0f && m.stop_ms))) {
        *v = 0.0f;
        if (kind) *kind = LaneWrite::stop;
        return true;
    }
    if (order != sc::kOrderNone && order != sc::kOrderRotate) return false;
    if (m.role == Role::committed && m.handover_ms && (m.sb == Sb::handover || m.sb == Sb::funnel)) { // the hand-over after P2
        if (!(sc::dot3(f, g.F) >= kLaneHoldCos)) return false;
        if (m.p2_kept) { // P2 kept the speed: it stands (the refusal's brake bound)
            *v = m.refused ? lane_refused_hold(g, m, x, v_now) : v_now;
            if (kind) *kind = LaneWrite::kept;
            return true;
        }
        // lane fix round 1 item 3 (Run173: P2 did not keep the leader's speed, the nose 22 deg off F; 0x1f and order 0 left it at 12.2 m/s
        // and this branch held that for the whole 116 s refusal, a crawl): never the live speed of a hand-over P2 did not keep: the lane
        // speed under the refusal's brake bound, refused the brake to a stop at d_end
        if (m.refused) {
            const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
            const float dist = -dend(m) - sc::dot3(d, g.F), a = m.params.a_lin > 0.1f ? m.params.a_lin : 0.1f;
            *v = dist > kLaneStopShortM ? sc::sqrt_ss(2.0f * a * (dist - kLaneStopShortM)) : 0.0f;
        } else
            *v = lane_refused_hold(g, m, x, m.v_sb >= 0.0f ? m.v_sb : lane_vmax(m));
        if (kind) *kind = LaneWrite::unkept;
        return true;
    }
    if (!lane || !(m.v_sb >= 0.0f)) return false;
    if (m.role == Role::escort ? !(m.res_until || (m.steered_ms && now - m.steered_ms <= age_bound(g, kGapMs)))
                               : !(m.steered_ms && now - m.steered_ms <= age_bound(g, kGapMs)))
        return false;
    const LaneHold h = lane_hold_o0(g, m, x, f, now);
    if (h.v < 0.0f) return false;
    *v = h.v;
    if (kind) *kind = LaneWrite::hold;
    return true;
}
// Review item 2: the KC grid's 20 spheres (0.75 r_gate = 829 m around points 666 m apart, rows at 0 / -666 / -1332 / -2000 m U) cover
// U in [-2827, +829] m and R in [-2161, +2161] m at the grid depth 2 (r_gate + r_s): no lane lateral inside the disc clears them (the
// row gap is 666 m against a 1658 m sphere diameter), so an escort whose order 4 is issued at the grid depth may be inside its slot's
// sphere, and order 4's first tick (which tests the KC's point, before site A's rewrite: ship-navigation-ai.md section 10.12 item 2)
// would end the order there. The tick deferral: on an order-0 frame of a lane escort in its lost-leader wait (the reservation set: the
// next order is the script's order 4), site B adds this to f+0x210, the order's re-plan stamp that order 0's tail has just set to the
// engine's now (section 10.11 Q4; SA_FlyObjectBeforeContainerPos does not write it, section 10.12 item 2). The order-4 handler's first
// frame then skips its tick (now - f+0x210 < 0: the jump to site B) and site B rewrites f+0x240 to L_i (lane_site_b_point) before the
// first tick tests it. The deferral covers the next frame's step up to kLaneDeferMs plus the recent step (a SETA frame of 1.5-2 s in
// scenario C); a longer step lets the tick run on the issue frame (the old behaviour). The first tick of the order comes that much later
// (the ship coasts at site B's last write meanwhile). 0: no deferral.
constexpr std::uint32_t kLaneDeferMs = 2500;
inline std::uint32_t lane_defer_ms(const Gate& g, const Member& m, std::uint16_t order) noexcept {
    if (order != sc::kOrderNone || !m.ln || !m.q_ok || m.role != Role::escort || !m.res_until || !(m.sb == Sb::inbound || m.sb == Sb::funnel))
        return 0;
    return kLaneDeferMs + (g.step_ms < kStepClampMs ? g.step_ms : kStepClampMs);
}
// Site B's order-4 point rewrite (review item 2; site A's rule at the frame of a deferred tick; lane_defer_step): a lane member (an escort just given its
// order 4, or a committed member) on order 4 to this gate whose f+0x240 (p, world m) is one of the KC's 20 grid points for its radius
// gets L_i in *L. False: leave the field.
inline bool lane_site_b_point(const Gate& g, const Member& m, std::uint16_t order, std::uint32_t target, const float p[3], float L[3]) noexcept {
    if (order != sc::kOrderFlyBefore || target != g.gate || !m.ln || !m.q_ok || !(m.role == Role::escort || m.role == Role::committed)) return false;
    if (kc_grid_slot(g.G, g.R, g.U, g.F, g.r_gate, m.r, p) < 0) return false;
    lane_order_point(g, m, L);
    return true;
}
// Site B's tick deferral on one frame of member m (review item 2), given the live order, its target, f+0x210 as read (f210), the game
// clock now (the engine's [[0x606f34]+0x718], the clock order 0's tail, the handlers and site B read) and f+0x240 (p, world m): on
// order 0 with lane_defer_ms, f+0x210 := now + the deferral (write210), on every such frame; on the first frame of the next order while
// f+0x210 still holds that value, the order-4 point rewrite when it is a grid point (rewrite, L) and f+0x210 := now (the next frame's
// handler ticks on the rewritten point: one frame of delay, whatever the step). Final review item 2 (ship-navigation-ai.md section 10.13
// items 3 and 6): the base and the restore are the clock, not the stamp read: on an order-0 frame whose tail did not run (another order
// ended into order 0 for site B) f210 still holds the previous deferral, and adding to it compounded; a restore of the stored base put
// back a stamp up to a deferral in the future. A lost restore (the next order's first frame never reaches site B's lane write) leaves
// f+0x210 at most kLaneDeferMs + the step after the last order-0 frame: the order's first tick comes at most that late; a later frame
// that finds f+0x210 changed drops the deferral without a write. The DLL applies the writes; the fixture models f+0x210 with the plant's
// re-plan time.
struct LaneDefer {
    bool write210 = false, rewrite = false;
    std::uint32_t v210 = 0;
    float L[3] = {0, 0, 0};
};
inline LaneDefer lane_defer_step(const Gate& g, Member& m, std::uint16_t order, std::uint32_t target, std::uint32_t f210, std::uint32_t now,
                                 const float p[3]) noexcept {
    LaneDefer out;
    if (order == sc::kOrderNone) {
        const std::uint32_t d = lane_defer_ms(g, m, order);
        if (!d) {
            m.defer_w = 0;
            return out;
        }
        m.defer_base = now;
        m.defer_w = now + d ? now + d : 1u;
        out.write210 = true;
        out.v210 = m.defer_w;
        return out;
    }
    if (!m.defer_w) return out;
    if (f210 == m.defer_w) {
        out.rewrite = lane_site_b_point(g, m, order, target, p, out.L);
        out.write210 = true;
        out.v210 = now;
    }
    m.defer_w = 0;
    return out;
}
// The lane's leaf point and site B's speed for a member at x flying at v on `order` (site A; the DLL's decide() and the fixture's
// steer_call; lane_steer). False: the module's other rules (always with the key off).
struct SbSteer {
    float P[3] = {0, 0, 0};
    float v = -1.0f;
    Sb sb = Sb::none;
    bool keep_point = false; // lane fix round 2 item 2: the speed only (the order's own point stands: a handed escort's leg to its new leader)
};
// Lane stage 1 (section 6.1): the leaf point and site B's speed of a lane member (sb_steer's branch under the key): a committed member on
// order 4 or an escort on 0x14 / 0x15 gets the lane carrot (lane_carrot: the join toward Lambda, the merging yielder's own lateral while
// it is level with its predecessor, the re-departure's turn point) and its law's speed (0: the stop, written by lane_site_b); a committed
// member on order 5 Lambda_5's carrot through Q at the aligned ramp.
inline bool lane_steer(const Gate& g, const Member& m, const float x[3], float v_now, std::uint16_t order, std::uint32_t target,
                       SbSteer* out) noexcept {
    out->sb = m.sb;
    const float rg = g.r_gate;
    if (!(rg > 0.0f) || !m.q_ok) return false;
    // lane fix round 2 item 2: an escort handed to another leader flies the engine's leg to it (the order's own point) at its lane law's
    // speed while it leaves the lane (its follow law behind the ship ahead: no closing on the convoy it leaves; its rollout drifts at it)
    if (m.exit_ms && m.role == Role::escort && (order == sc::kOrderEscort || order == sc::kOrderFollow) && target == m.exit_tgt &&
        (m.sb == Sb::inbound || m.sb == Sb::funnel)) {
        out->keep_point = true;
        out->v = m.v_sb;
        return m.v_sb >= 0.0f;
    }
    if (order == sc::kOrderFlyBefore || order == sc::kOrderDock) {
        if (target != g.gate) return false;
    } else if (order == sc::kOrderEscort) {
        if (target != m.leader) return false;
    } else if (order == sc::kOrderFollow) {
        if (target != m.leader && !(m.res_until && target == g.gate)) return false;
    }
    const bool o4 = m.role == Role::committed && order == sc::kOrderFlyBefore;
    if ((o4 || (m.role == Role::escort && (order == sc::kOrderEscort || order == sc::kOrderFollow))) && (m.sb == Sb::inbound || m.sb == Sb::funnel)) {
        const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
        const float s = sc::dot3(d, g.F);
        float lat[2];
        lane_lat_at(g, m, s, lat);
        const float er = sc::dot3(d, g.R) - lat[0], eu = sc::dot3(d, g.U) - lat[1];
        const float y = sc::sqrt_ss(er * er + eu * eu);
        const bool hold_lat = !m.merged && m.lane_gap < kLaneGapM - 100.0f && y > kLaneOnM;
        const int pi = hold_lat && m.lane_pred ? find(g, m.lane_pred) : -1;
        float sc_c, yc;
        lane_carrot(s, y, v_now, m.params.W, rg, hold_lat, m.redep != 0, &sc_c, &yc, pi >= 0 ? g.m[pi].w + m.w + kLaneSideM : 0.0f,
                    lane_join_end(m), m.redep_out, m.redep_r, dend(m));
        float latc[2];
        lane_lat_at(g, m, sc_c, latc);
        const float k = y > 1.0f ? yc / y : 0.0f;
        if (m.redep && s <= -(dend(m) + m.redep_out)) // the turn: ahead (+F) and above (+U) of the ship, onto Lambda
            for (unsigned i = 0; i < 3; ++i) out->P[i] = x[i] + kLaneTurnM * (g.F[i] + g.U[i]);
        else {
            const float side = m.redep ? m.redep_side : 0.0f; // out: the turn point below Lambda
            for (unsigned i = 0; i < 3; ++i)
                out->P[i] = g.G[i] + (latc[0] + (m.redep ? 0.0f : k * er)) * g.R[i] + (latc[1] - side + (m.redep ? 0.0f : k * eu)) * g.U[i] + sc_c * g.F[i];
        }
        out->v = m.law == Law::stop ? 0.0f : m.v_sb >= 0.0f ? m.v_sb : lane_vmax(m);
        return sc::finite_f(out->P[0]) && sc::finite_f(out->P[1]) && sc::finite_f(out->P[2]);
    }
    if (m.role == Role::committed && order == sc::kOrderDock && (m.sb == Sb::dock || m.sb == Sb::handover || m.sb == Sb::funnel)) {
        float X[3];
        lambda5(g, m, X);
        line_carrot(X, g.F, 2.0f * rg, rg, m.params.W, x, v_now, out->P);
        const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
        out->v = dock_ramp(sc::sqrt_ss(sc::dot3(d, d)), rg, m.params.v_max);
        return sc::finite_f(out->P[0]) && sc::finite_f(out->P[1]) && sc::finite_f(out->P[2]) && out->v > 0.0f;
    }
    return false;
}
inline bool sb_steer(const Gate& g, const Member& m, const float x[3], float v_now, std::uint16_t order, std::uint32_t target,
                     SbSteer* out) noexcept {
    return g.lane ? lane_steer(g, m, x, v_now, order, target, out) : false; // the key off: the stage-A certificate only (no steering)
}
// P2 (the order-4 arrival at the grid sphere): the funnel's crossing is the kept hand-over's precondition (D.6: the funnel replaces
// the join mode of P2's verdict); the stage moves to the hand-over at the next frame from the live order.
// Lane stage 1: a lane member on its final (on Lambda, or still converging onto it: the keep verdict's nose and velocity tolerances
// decide) is the funnel's equivalent.
__attribute__((always_inline)) inline bool sb_funnel(const Member& m) noexcept {
    return m.role == Role::committed && (m.sb == Sb::funnel || (m.ln && m.sb == Sb::inbound));
}
// Lane stage 1 (section 6.4): P2's coast bound (stop before the inflated trigger on a refusal) is replaced for a lane member by site B's
// hold with the refusal's brake bound (lane_refused_hold): its speed bound is open, the nose and velocity tolerances stay.
__attribute__((always_inline)) inline float lane_p2_bound(const Member& m, float bound) noexcept {
    return m.role == Role::committed && m.ln && (m.sb == Sb::funnel || m.sb == Sb::inbound) ? 3.4e38f : bound;
}
// Whether the lane owns a committed member's gate legs (review F8): Q known and a lane stage. Otherwise the member keeps the stage-A /
// amendment-15 join (join on, P2 kept when aligned), never "join off + vanilla stop".
__attribute__((always_inline)) inline bool sb_managed(const Member& m) noexcept {
    if (m.role != Role::committed || !m.q_ok) return false;
    if (m.ln) return m.sb == Sb::inbound || m.sb == Sb::funnel || m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch; // the lane
    return m.sb == Sb::funnel || m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch;
}
} // namespace x3m::ship_avoidance::ctl
