#pragma once
// The gate traffic controller: the departure lane (convoy-gate-transit.md). The lane laws (LaneCtx, lane_reserve, lane_law,
// lane_hand_bound, lane_hold_o0), the lane member's path against an obstacle and the gate's solid (lane_path*, lane_obst_*, solid_gap_*,
// lane_gate_why), the predicted launch, the lane frame (lane_frame), site B's lane write (lane_site_b, lane_defer_*) and site A's lane
// steer (lane_steer).
#include "ship_gate_geometry_core.h"
namespace x3m::ship_avoidance::ctl {
// One member's law at one moment (lane_frame on the live state, the rollout on its samples): its along position s, the lane speed, the
// predecessor's gap and along speed, the head's pacing (seconds until the through slot is free) and an escort's gate (seconds until its
// leader's launch + lambda_max). Returns the speed (0: a stop) and the law. Round A (rank 4, ship-steering-holistic-review.md section 2 b):
// one gap law, no merge state: v = min(v_lane, v_P + (gap - g_req) / tau), floor 10 m/s, gap = the raw gap + the joins' cost - the own
// closing distance; "merge" is the row label while the raw gap is short of g_req - 50 m (a yield is the law at its floor: try_yield's
// single candidate).
struct LaneCtx {
    float s = 0, v_lane = 0, a = 1.0f, v_self = 0, y = 0; // y: the lateral still to join (m; 0 on Lambda)
    float loss = 0; // the along-track distance the pair's joins will still cost the gap: the member's own less its predecessor's (lane_join_loss)
    bool pred = false;
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
    // round A (the no-write hand-over): the ship ahead is on its final before its P2, which may stop it at d_end within a frame (P2 not
    // kept: the vanilla stop): the room from this member's nose to g_req behind the ship ahead's tail at d_end (m; 3.4e38 none): the
    // member stays on the braking curve to a stop there (lane_law's stop-room bound) so an instant stop ahead leaves g_req
    float stop_room = 3.4e38f;
    // lane fix round 2 item 3d: the ship ahead has its grant (order 5 or 8 seen): it leaves along F, so its standstill is no stop at
    // spacing (the queue hold behind a refused leader ends within the step of its grant, not once its speed passes kLaneStopV)
    bool granted = false;
    // Run175 round item 3: the path still to fly to d_end through the member's join point (m; lane_path_len; 0: along F, or the straight
    // join when y is set): every pacing law's distance, never the F component alone
    float path = 0.0f;
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
// (*settle, optional: the gap the law read, m; 3.4e38 without a predecessor. always_inline: a float return on i686 goes through st(0)
// when the function is emitted out of line, which the x87 audit refuses on the hook paths; its two callers inline it)
__attribute__((always_inline)) inline float lane_law(const LaneCtx& c, Law* law, bool* stop_cond = nullptr, float* settle = nullptr) noexcept {
    float v = c.v_lane;
    if (stop_cond) *stop_cond = false;
    if (settle) *settle = 3.4e38f;
    *law = Law::lane;
    if (c.pred) {
        // the gap the law reads: the raw gap (tail of the ship ahead to the own nose along F) plus the along-track distance the joins onto
        // Lambda will still cost at the shared cap (its own less the predecessor's), less the own closing distance when this member is the
        // faster (the braking at a to the ship ahead's speed: a follower behind a stopped or returning ship brakes on its stopping
        // distance, measured in redeparture). Round A: no credit for the ship ahead's speed advantage (the merge law's settling term let a
        // yielder at the floor re-accelerate while still level with its passer, 1.2 km before it was behind, so it joined at the lane
        // speed from 1.4 km beside: a 23-25 deg nose at 7 km and, on the Terran ring, the hand-over's hull swept into the ring's hole)
        const float am = c.a > 0.1f ? c.a : 0.1f, dv = c.v_self - c.v_pred;
        const float gap = c.gap + c.loss - (dv > 0.0f ? dv * dv / (2.0f * am) : 0.0f);
        if (settle) *settle = gap;
        // the one gap law (round A, rank 4): behind a predecessor inside its final the gap also covers the own stop (lane fix round 2 item
        // 3b). The label: merge while the raw gap is short of g_req - 50 m, follow after
        const float g_need = c.pred_final ? kLaneGapM + c.v_self * (c.v_self > 0.0f ? c.v_self : 0.0f) / (2.0f * am) : kLaneGapM;
        const Law bound = c.gap < kLaneGapM - 50.0f ? Law::merge : Law::follow;
        float vf = c.v_pred + (gap - g_need) / kLaneTauFollowS;
        if (vf < kYieldFloorMs) vf = kYieldFloorMs;
        if (vf < v - 0.5f) {
            v = vf;
            *law = bound;
        }
        if (c.stop_room < 3.0e38f) { // the ship ahead before its P2 on its final (round A's no-write hand-over: it may stop at d_end within a
                                     // frame): the braking curve to a stop g_req behind its tail at d_end (measured in p2_miss_near: the gap
                                     // law alone let the follower run at 69 m/s 667 m behind, and its stop at spacing left 177 m, under the
                                     // sweep's margin)
            float vr = c.stop_room > 0.0f ? sc::sqrt_ss(2.0f * am * c.stop_room) : 0.0f;
            if (vr > 0.0f && vr < kYieldFloorMs) vr = kYieldFloorMs; // (the law's floor, as the gap law's; the room has g_req behind it)
            if (vr < v) {
                v = vr;
                *law = v > 0.0f ? bound : Law::stop;
            }
        }
        if (((c.v_pred < kLaneStopV && !c.granted) || c.closing) && gap < kLaneGapM) { // the ship ahead stopped (a block) or flying back: stop at spacing
            if (stop_cond) *stop_cond = true;                              // behind it (its braking counted)
            if (c.block || c.closing || c.stop_left_s > 0.0f) {
                v = 0.0f;
                *law = Law::stop;
            } else if (v > kYieldFloorMs) { // (lane fix round 1 item 4: not a block, past T_stop + the step: the floor)
                v = kYieldFloorMs;
                if (*law == Law::lane) *law = bound;
            }
        }
    }
    const float dist = -c.d_end - c.s; // to d_end along F
    // the path still to fly to d_end (every pacing law, Run175 round item 3): through the join point (c.path, lane_path_len), else the
    // straight join, never the along-track distance of a joining ship (lane_eta's)
    const float path = c.path > 0.0f ? c.path : c.y > kLaneOnM && dist > 0.0f ? sc::sqrt_ss(dist * dist + c.y * c.y) : dist;
    const float a = c.a > 0.1f ? c.a : 0.1f;
    const float vb = path > 50.0f ? sc::sqrt_ss(2.0f * a * (path - 50.0f)) : 0.0f; // the braking curve to a stop at d_end
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
        const float vg = lane_reserve(path, a, c.v_self, c.t_gate_s, c.dt);
        if (vg < v) {
            v = vg;
            *law = v > 0.0f ? Law::gate : Law::stop;
        }
    }
    return v;
}
// The lateral rule's release (Member::band; lane_frame on the live state, the rollout on its samples): the yielder is behind its
// predecessor (the raw gap >= 0; the law then never demands more than the ship ahead's speed while the gap is short of g_req, so it
// stays behind), or the raw gap is g_req - 100 m. (Before round A the merge latch released at a settling gap of g_req - 50 m with the
// raw gap >= 0, the same moment under the merge law.) gap_law: the gap the law read (the joins' cost included).
__attribute__((always_inline)) inline bool lane_band_free(float gap_raw, float gap_law) noexcept {
    return (gap_raw >= 0.0f && gap_law >= 0.0f) || gap_raw >= kLaneGapM - 100.0f;
}
// The order-0 / 0x1f hold of a member in the hand-over after P2 (section 6.4; round A, rank 4: from P2 on, not only once refused): the
// speed stands while the KC's longest poll keeps the ship outside its trigger sphere + kLaneTrigMarginM, else the brake at a to a stop
// there (a refused through slot never carries the ship through the gate; a granted one starts order 5 within the poll). The travel of one
// game step at v is taken off the room (review of rounds A-C item 6: a fixed 100 ms before, a SETA step's 2 s carried the ship 20x that;
// the 100 ms stays the floor).
__attribute__((always_inline)) inline float lane_hand_bound(const Gate& g, const Member& m, const float x[3], float v) noexcept {
    const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
    const std::uint32_t step_ms = g.step_ms > kStepClampMs ? kStepClampMs : g.step_ms > 100u ? g.step_ms : 100u;
    const float room = sc::sqrt_ss(sc::dot3(d, d)) - (m.r + v * float(int(step_ms)) * 0.001f) - kLaneTrigMarginM;
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
        if (m.band && m.lane_pred && en < 0.0f && ee > kLaneOnM * kLaneOnM) { // (beside it in the band, off Lambda)
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
// The arrival round item 4 (gate_ring_why, any AI ship against any gate): escort / arrival / other the ring row's suppression by the
// ship's kind (escort: on 0x14 / 0x15; arrival: a launch-hook arrival), refused_hull its hull would reach the gate's solid on the path
// (the warn kept); roll there unreadable or unknown (fail closed). The member row's suppress / lane are the kind member.
enum class GateRow : std::uint8_t { suppress = 0, not_member, stale, unreadable, reach, stage, order, guard, lane, roll, escort, arrival, other,
                                    refused_hull, count };
inline const char* gate_row_name(GateRow w) noexcept {
    static const char* const n[unsigned(GateRow::count)] = {"suppress", "not_member", "stale", "unreadable", "reach", "stage", "order", "guard",
                                                            "lane", "roll", "escort", "arrival", "other", "refused_hull"};
    return unsigned(w) < unsigned(GateRow::count) ? n[unsigned(w)] : "?";
}
__attribute__((always_inline)) inline bool gate_row_suppresses(GateRow w) noexcept {
    return w == GateRow::suppress || w == GateRow::lane || w == GateRow::escort || w == GateRow::arrival || w == GateRow::other;
}
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
// (Fr: a Gate, or a GateFrame for a gate without a board: the ring row of the arrival round's item 4; only G, R, U, F are read)
struct GateFrame {
    float G[3], R[3], U[3], F[3];
};
template <class Fr> __attribute__((always_inline)) inline float solid_dist(const Fr& g, const GateSolid& gs, const float p[3]) noexcept {
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
template <class Fr>
__attribute__((always_inline)) inline float solid_gap_seg(const Fr& g, const GateSolid& gs, const float a[3], const float b[3], float LA,
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
template <class Fr>
__attribute__((always_inline)) inline float solid_gap_sweep(const Fr& g, const GateSolid& gs, const float x[3], const float f[3],
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
// ---- The arrival round item 4: the own-gate warn suppression as a property of the gate and the ship (decision with the user,
// 2026-10-15; convoy-gate-transit.md section 12.6) -------------------------------------------------------------------------------
// The gate's solid is decided at runtime by whether its body carries a collision tree (Gate::ring / the glue's read of the gate's scene
// nodes, [node+0x12c] & 0x1000000 without 0x8000000 below the root's test body, ship-navigation-ai.md section 10.9): none, no solid (the
// installed Mayhem gates: every placed gate is TGates rows 0-3, the blended bubble without a tree): the warn is vacuous for every AI
// ship on any order; a tree, or an unknown read (fail closed), the ring of its TGates row (gate_ring): rows 21-24 the Terran ring
// (gate_hole.txt, installed), 5-8 the stock catapult's tube, 4 the stock destroyed gate (no hole), every other row the stock Argon /
// terraformer ring at its most conservative (hole 0.605 r, outer 1.026 r, z -0.651..+0.655 r; vanilla's ring numbers are untested in
// flight: platform-portability.md). The ship's predicted path over 1 s + v / a (the engine's warn reach and the ship's stop): its live
// velocity (the nose when slower than 1 m/s) and, when the order's target is known (0x14 / 0x15: the leader; order 4: the order's
// point), the same run toward it; its hull (L, w) swept along both (solid_gap_sweep) must clear the solid by kMarginM, else refused_hull
// (the warn kept). Unreadable or non-finite reads: roll (kept).
constexpr unsigned kGateRingNone = 1, kGateRingTree = 2; // Gate::ring (0: not read / unknown)
inline GateSolid gate_ring(std::uint16_t type, float r_gate) noexcept {
    GateSolid s;
    if (!(r_gate > 0.0f)) return s;
    s.solid = true;
    float h = 0.605f, ro = 1.026f, z0 = -0.651f, z1 = 0.655f;
    if (gate_terran(type)) h = 0.663f, z0 = -0.498f, z1 = 0.179f;
    else if (type >= 5 && type <= 8) h = 0.127f, ro = 0.169f, z0 = -0.001f, z1 = 0.539f;
    else if (type == 4) h = 0.0f, ro = 1.013f, z0 = -0.411f, z1 = 0.246f;
    s.h = h * r_gate;
    s.r_out = ro * r_gate;
    s.z0 = z0 * r_gate;
    s.z1 = z1 * r_gate;
    return s;
}
constexpr float kGateRingHorizonS = 1.0f; // + the frame's game step (clamped, kStepClampMs; review of the arrival round item 5: a SETA frame
                                          // moves the ship a whole step before the next warn) + v / a: the path's horizon
// Whether the segment x + dir t, 0 <= t <= run, stays farther than rc from G (the ring's bounding sphere + the hull + the margin)
__attribute__((always_inline)) inline bool ring_far(const float G[3], const float x[3], const float dir[3], float run, float rc) noexcept {
    const float d[3] = {G[0] - x[0], G[1] - x[1], G[2] - x[2]};
    float t = sc::dot3(d, dir);
    t = t < 0.0f ? 0.0f : t > run ? run : t;
    const float e[3] = {d[0] - dir[0] * t, d[1] - dir[1] * t, d[2] - dir[2] * t};
    return sc::dot3(e, e) > rc * rc;
}
inline GateRow gate_ring_why(const GateFrame& fr, float r_gate, std::uint16_t type, unsigned ring, GateRow kind, const float x[3], const float u[3],
                             const float f[3], const float* tgt, float LA, float wA, float a_lin, float step_s) noexcept {
    for (unsigned i = 0; i < 3; ++i)
        if (!sc::finite_f(x[i]) || !sc::finite_f(u[i]) || !sc::finite_f(f[i])) return GateRow::roll;
    if (!(r_gate > 0.0f) || !(LA > 0.0f) || !(wA > 0.0f)) return GateRow::roll;
    if (ring == kGateRingNone) return kind; // (no collision tree: nothing to touch)
    if (!(a_lin > 0.0f) || !sc::finite_f(a_lin)) return GateRow::roll;
    const GateSolid gs = gate_ring(type, r_gate);
    const float st = step_s > 0.0f ? (step_s < float(kStepClampMs) * 0.001f ? step_s : float(kStepClampMs) * 0.001f) : 0.0f;
    const float v = sc::sqrt_ss(sc::dot3(u, u)), run = v * (kGateRingHorizonS + st + v / (a_lin > 0.1f ? a_lin : 0.1f));
    float dir[3];
    const float fl = sc::sqrt_ss(sc::dot3(f, f));
    for (unsigned i = 0; i < 3; ++i) dir[i] = v >= 1.0f ? u[i] / v : fl > 0.5f ? f[i] / fl : 0.0f;
    const float zc = -gs.z0 > gs.z1 ? -gs.z0 : gs.z1, rc = sc::sqrt_ss(gs.r_out * gs.r_out + zc * zc) + LA + kMarginM;
    if (!ring_far(fr.G, x, dir, run + 1.0f, rc) && !(solid_gap_sweep(fr, gs, x, f, dir, run, LA, wA) - kMarginM >= 0.0f)) return GateRow::refused_hull;
    if (tgt) {
        if (!sc::finite_f(tgt[0]) || !sc::finite_f(tgt[1]) || !sc::finite_f(tgt[2])) return GateRow::roll;
        const float d[3] = {tgt[0] - x[0], tgt[1] - x[1], tgt[2] - x[2]}, dl = sc::sqrt_ss(sc::dot3(d, d));
        if (dl > 1.0f) {
            const float d2[3] = {d[0] / dl, d[1] / dl, d[2] / dl}, run2 = run < dl ? run : dl;
            if (!ring_far(fr.G, x, d2, run2 + 1.0f, rc) && !(solid_gap_sweep(fr, gs, x, f, d2, run2, LA, wA) - kMarginM >= 0.0f))
                return GateRow::refused_hull;
        }
    }
    return kind;
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
    unsigned mouth_expired = 0; // review of the arrival round item 3: gate-mouth holds ended by their bound (lane_frame's block)
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
// The U-turn's apex of an overshoot or a loop (s along F, m; negative): kLaneOverApexR R beyond its turn point (d_end + redep_out), and for
// a loop the script's order-0 re-issue gap's straight coast at the lane speed on top (review of rounds A-C item 9: measured in x12_near at
// tf 1, the gap fell inside the U-turn and carried it 66 m past the turn point + 3 R; the fixture's own bound already allowed 3 s at
// 100 m/s): the law's s while it re-departs, a follower's block, the loop's room against the statics
constexpr float kLaneLoopO0S = 3.0f; // the script's order-0 re-issue gap at most (no leaf: the ship coasts straight)
__attribute__((always_inline)) inline float lane_apex_ext(const Member& m, float R) noexcept {
    return kLaneOverApexR * R + (m.loop ? kLaneLoopO0S * lane_vmax(m) : 0.0f);
}
__attribute__((always_inline)) inline float lane_apex_s(const Member& m) noexcept {
    return -(dend(m) + m.redep_out + lane_apex_ext(m, m.redep_r));
}
// Section 12.4 (round C): a loop's out distance beyond d_end before the spacing behind the ship ahead: d_end + settle + R +
// D_need(kLaneLoopResR R) (kLaneRedepM at least), pushed out while the ship is not yet on its out-leg (loop_out)
// The arrival round item 3 (Run174: a lone arriving Colossus given the 3 km out-leg 1 km below Lambda, 150 deg away to 6.5 km): a lone
// re-departure (loop_min) flies the loop's geometry with the residual of its U-turn from the out-leg (2.5 R off Lambda, the U-turn's
// measured extent: it ends on Lambda) taken as the lane's on-band (kLaneOnM) and no kLaneRedepM floor: out to d_end + settle + R +
// D_need(kLaneOnM), what P2 kept within 8 deg needs (without the R an M2 at 70 m/s reached P2 6.3 deg off F with its velocity outside the
// keep tolerance in 1 of 9 runs) (X3M_LANE_RMIN=0 builds the plain re-departure's out-leg instead)
#ifndef X3M_LANE_RMIN
#define X3M_LANE_RMIN 1
#endif
#ifndef X3M_LANE_MOUTH // (item 5's gate-mouth hold, lane_mouth_hold; 0 builds the lane before it)
#define X3M_LANE_MOUTH 1
#endif
constexpr float kLaneMinSideK = 1.2f; // the lone out-leg's offset at least this x kLaneTurnM (lane_frame)
constexpr float kLaneMinEtaK = 1.5f;  // a member reaching d_end within this x the lone turn-around (+ the spacing) is in its room
__attribute__((always_inline)) inline float lane_loop_base(const Gate& g, const Member& m, float R) noexcept {
    if (m.loop_min) return lane_blend_settle(g, m) + R + lane_join_need(kLaneOnM, R);
    const float out = lane_blend_settle(g, m) + R + lane_join_need(kLaneLoopResR * R, R);
    return out > kLaneRedepM ? out : kLaneRedepM;
}
__attribute__((always_inline)) inline float lane_loop_out(const Gate& g, const Member& m) noexcept {
    const float out = lane_loop_base(g, m, m.redep_r);
    return m.loop_out > out ? m.loop_out : out;
}
// ... and the largest it may fly (3.4e38: no bound; < 0: no loop): its U-turn's apex (kLaneOverApexR R beyond the turn point) stays the
// hull's half-width + kMarginM short of the statics' envelope (the profile's 1 Hz scan) beyond the ship. Measured in lane_station: the
// loop flew back out past the station on Lambda 8 km out that had evicted it and was evicted again (6 loops, 15 flown 0x12 in one run):
// the design's "a static there is the sphere avoidance's" did not hold. Unknown statics (no fresh profile, kLaneProfMaxAgeMs + the step,
// or the scan overflowed) and an envelope around the ship fail closed (no loop: today's join, the no-write hand-over behind it)
__attribute__((always_inline)) inline float lane_loop_cap(const Gate& g, const Member& m, float R, std::uint32_t now) noexcept {
    const LaneProfile& p = g.prof;
    if (!p.built_ms || !p.st_ok || p.overflow || std::int32_t(now - p.built_ms) > std::int32_t(age_bound(g, kLaneProfMaxAgeMs))) return -1.0f;
    if (!p.st_n) return 3.4e38f;
    const float c = m.w + kMarginM, s = m.lane_s < 0.0f ? m.lane_s : 0.0f;
    if (p.st_lo - c > s) return 3.4e38f; // every static between the ship and the gate: none on its loop
    if (p.st_hi + c >= s) return -1.0f;  // the envelope reaches the ship
    return -(p.st_hi + c) - dend(m) - kLaneOverApexR * R - kLaneLoopO0S * lane_vmax(m); // (review item 9: the order-0 gap's coast too)
}
// Review of rounds A-C item 9: the loop-back join's state for the ship_gate_ctl_lane row (loop=): 0 none, 1 leg (flying onto its out-leg,
// the turn point pushed ahead of it), 2 out (on the leg, out to the turn point), 3 turn (past the turn point, the U-turn before the nose is
// back toward +F, where the loop ends)
__attribute__((always_inline)) inline unsigned lane_loop_state(const Member& m) noexcept {
    return !m.loop ? 0u : !m.loop_conv ? 1u : m.lane_s > -(dend(m) + m.redep_out) ? 2u : 3u;
}
inline const char* lane_loop_name(unsigned k) noexcept {
    static const char* const n[4] = {"none", "leg", "out", "turn"};
    return k < 4 ? n[k] : "?";
}
// The arrival round item 5: whether member m, re-departing at the gate mouth (inside d_end, its nose away: the caller's test), holds behind o: o is live, on
// a lane stage, the hand-over, order 5 or order 8 (to its end: the pair's rollout then has no hold to model), not itself returning (re-departing, an overshoot or a loop), and farther out
// than m along F (m is nearer the gate: o passes it on its final). Out of o's path it is at rest (an arrival's exit
// point, 1.5 km above Lambda, or its order-0 stop), the pair certifies on the rest, and nothing it could fly meanwhile is shorter (its own
// re-departure crosses o's final). Bounded by o's launch (+ the step at site B).
// Review of the arrival round item 3: only while o is on its final: past d_end (the hand-over, order 5, order 8 to its end), or on a lane
// stage (joining or on Lambda) heading in (its nose along +F) and not returning; bounded in game time (the block in lane_frame): its launch
// ETA at the hold's start + T_stop + the step; past it the hold is over for that o (counted, LaneStats::mouth_expired) and the law's stop
// cap (T_stop + the step from the stop's start) applies again. An o that never launches ends the hold with its membership (killed: not
// live; refused beyond kLaneRefusedMaxMs + the step: no longer a member), or earlier at the bound.
__attribute__((always_inline)) inline bool lane_mouth_hold(const Gate& g, const Member& m, const Member& o, std::uint32_t now) noexcept {
    if (!X3M_LANE_MOUTH || m.over || !(m.lane_s > -dend(m)) || !o.ship || !live(g, o, now) || !pair_role(o.role)) return false;
    if (o.redep || o.over || o.loop) return false;
    if (!(o.sb == Sb::handover || o.sb == Sb::dock || o.sb == Sb::launch || ((o.sb == Sb::inbound || o.sb == Sb::funnel) && sc::dot3(o.st.rows[2], g.F) > 0.0f)))
        return false;
    const float d[3] = {o.st.x[0] - g.G[0], o.st.x[1] - g.G[1], o.st.x[2] - g.G[2]};
    return sc::dot3(d, g.F) < m.lane_s;
}
// ... and in force: its bound for this o not passed (read-only: site A's lone decision and the law's block read the same answer for the same
// o, review item 4)
__attribute__((always_inline)) inline bool lane_mouth_on(const Gate& g, const Member& m, const Member& o, std::uint32_t now) noexcept {
    if (!lane_mouth_hold(g, m, o, now)) return false;
    return !(m.mouth_pred == o.ship && (m.mouth_exp || (m.mouth_ms && std::int32_t(now - m.mouth_ms) > 0)));
}
// The ETA to d_end the lane order ranks by (s, at the lane speed): along Lambda when on it; the straight join plus the rest when joining;
// out to the turn point and back (with a half turn at the hull's rate) when re-departing.
inline float lane_eta(const Member& m) noexcept {
    const float v = lane_vmax(m) > kYieldFloorMs ? lane_vmax(m) : kYieldFloorMs, s_end = -dend(m), s = m.lane_s;
    float path;
    if (m.redep) { // (section 12.4, round C: a loop's lead is its path to the turn point, the nose's turn onto it included)
        const float s_out = -(dend(m) + m.redep_out), W = m.params.W > 1e-3f ? m.params.W : 1e-3f;
        path = (m.loop ? m.loop_lead : s > s_out ? s - s_out : 0.0f) + (s_end - s_out) + sc::kPi * v / W;
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
        // (section 12.4, round C: a loop ranks ahead of a ship only when it reaches d_end earlier by the spacing, g_req and two of its
        // hulls at the common speed: one nearer in time keeps its place and the loop turns in behind it, never a follower braked or
        // stopped behind the loop's apex; measured in x12_far, a loop 9 s ahead stopped its follower 20 s)
        return ((m.loop ? m.loop_lead : s > s_out ? s - s_out : 0.0f) + (s_end - s_out) + sc::kPi * v / W) / v +
               (m.loop ? (kLaneGapM + 2.0f * m.L) / v : 0.0f);
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
// Run175 round item 4: whether follower m's path crosses the swept footprint of the loop P flies (P's out-leg 2.5 R beside Lambda, its
// U-turn R beyond, its rejoin at the apex): m within the loop's lateral band (P's offset + its turn radius + both half-widths + the margin)
// of Lambda. Outside it the loop is transparent to m (its predecessor is the next member ahead); Run175: a follower 10 km beside Lambda
// was held 107 s at spacing behind the apex of a loop 6 km away.
#ifndef X3M_LANE_LOOP_ZONE
#define X3M_LANE_LOOP_ZONE 1
#endif
__attribute__((always_inline)) inline bool lane_loop_zone(const Gate& g, const Member& P, const Member& m) noexcept {
    if (!X3M_LANE_LOOP_ZONE) return true;
    (void)g;
    return m.lane_lat <= P.redep_side + P.redep_r + P.w + m.w + kMarginM;
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
        g.slot_block_ms = g.funnel_end_ms = g.last_arrival_seen = g.ord_retake_ms = 0;
        g.lane_n = 0;
        for (unsigned i = 0; i < g.n; ++i) {
            Member& m = g.m[i];
            m.x_ms = m.o8_ms = 0;
            m.x_counted = 0;
            m.cap = -1.0f;
            m.cap_ms = m.yield_ms = m.v_plan_ms = 0;
            m.merge_ms = m.stop_ms = m.stop_end_ms = m.lane_free_ms = m.lane_gate_ms = 0;
            m.lane_pred = 0;
            m.band = 0;
            m.lstop_ms = m.lstop_last_ms = m.x_acc_ms = 0; // (lane fix round 1)
            m.defer_w = 0;      // final review item 2 (ship-navigation-ai.md section 10.13 item 4): no deferral survives a load
            m.lane_ord_ok = 0; // final review item 1: the sticky order moments are of the old clock
            m.roll_ms = 0;     // lane fix round 2 item 4 (review 7): a rollout of the old clock is no certificate
            m.mouth_pred = m.mouth_ms = 0; // (review of the arrival round item 3: a hold's bound of the old clock; re-taken from here)
            m.mouth_exp = 0;
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
                    m.x_counted = 0;
                    m.sb = Sb::inbound;
                }
            } else if ((o == sc::kOrderNone || o == sc::kOrderRotate) && m.handover_ms) {
                if (m.sb != Sb::handover) {
                    m.sb = Sb::handover;
                    m.x_ms = m.handover_ms;
                    m.x_counted = 0;
                    m.o8_ms = 0;
                    m.x_acc_ms = 0;
                }
            } else if (o == sc::kOrderDock) {
                if (m.sb == Sb::handover && m.x_ms) lane_x_wait(m, now, st); // (the rest of the wait)
                m.sb = Sb::dock;
                m.handover_ms = m.x_ms = 0;
                m.x_counted = 0;
            } else if (o == sc::kOrderWarpAway) {
                m.sb = Sb::launch;
                m.handover_ms = m.x_ms = 0;
                m.x_counted = 0;
                if (!m.o8_ms) m.o8_ms = m.st.ms;
                const std::uint32_t end = m.o8_ms + 900u;
                if (!g.funnel_end_ms || std::int32_t(end - g.funnel_end_ms) > 0) g.funnel_end_ms = end;
            }
            // waiting at d_end for the through slot: a refusal is counted once beyond kLaneRefuseMs + the step (review item 8: a signed
            // game-time bound plus the frame step; round A, rank 4: a counter, no law reads it) and the wait beyond 3 s accrues every frame
            // of the hand-over (lane fix round 1 item 3: Run173's 116 s refusal left ctl_x_wait_s at 0)
            if (m.sb == Sb::handover && m.x_ms) {
                if (!m.x_counted && std::int32_t(now - m.x_ms) > std::int32_t(age_bound(g, kLaneRefuseMs))) {
                    m.x_counted = 1;
                    if (st) ++st->x_refused;
                }
                lane_x_wait(m, now, st);
            }
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
        // Run175 round item 3: the join funnel (lane_funnel_y, with hysteresis) decides whether the lane owns the member on a lane stage;
        // on Lambda, re-departing (the overshoot, the loop, the gate mouth) or past the lane stages the ownership stands
        if (!X3M_LANE_FUNNEL || m.lane_on || m.over || m.loop || !(m.sb == Sb::inbound || m.sb == Sb::funnel)) m.funnel = 1;
        else {
            const float yf = lane_funnel_y(m, m.lane_s), yo = yf + kLaneOffM > kLaneFunnelHyst * yf ? yf + kLaneOffM : kLaneFunnelHyst * yf;
            if (m.funnel) {
                if (m.lane_lat > yo) m.funnel = 0;
            } else if (m.lane_lat <= yf)
                m.funnel = 1;
        }
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
        // section 12.4 (round C, the loop-back join): decided once at the re-commit after an eviction: the join from here does not fit
        // when lane_join_span takes its residual branch (neither Lambda's far end nor the 8 km merge point is in reach of the 30 deg join,
        // and the room to the join's end is short of 2 y). The loop lasts until the member has turned in (or leaves the lane stages, or
        // is on Lambda). Its out-leg lies on the ship's own side of Lambda (never above it: the arrivals' side): an out-leg on the fixed
        // -U side crossed Lambda and the join of a follower coming up from below (x12_far: contact, two further evictions)
        const bool lane_stage = m.sb == Sb::inbound || m.sb == Sb::funnel;
        if (m.x12_rc && lane_laws(m)) { // (Run175 round: a body decides nothing; its flag waits)
            m.x12_rc = 0;
            const float yy = m.lane_lat, Rj = lane_turn_r(m), need = lane_join_need(yy, Rj);
            const bool fits = -kLaneFarM - m.lane_s >= need || -kLaneMergeM - m.lane_s >= need || -lane_join_end(m) - m.lane_s >= 2.0f * yy;
            const float Rl = m.params.W > 1e-3f ? lane_vmax(m) / m.params.W : 0.0f;
            if (lane_stage && !m.lane_on && !fits && lane_loop_cap(g, m, Rl, now) >= lane_loop_base(g, m, Rl)) { // (its corridor clear)
                m.loop = 1;
                m.loop_conv = 0;
                m.loop_out = m.loop_sp = 0.0f;
                m.over_ret_ms = 0;
                const float nu = eu < 0.0f ? eu : 0.0f, nl = sc::sqrt_ss(er * er + nu * nu);
                m.loop_n[0] = nl > 1.0f ? er / nl : 0.0f;
                m.loop_n[1] = nl > 1.0f ? nu / nl : -1.0f;
            }
        }
        // the arrival round item 3: the lone re-departure. A member about to re-depart (in front of the plane, inside the plain re-departure's
        // turn point, the nose away from F, not on Lambda, not an overshoot or a loop) with no other member in its join room (any live member
        // in front of the plane nearer the gate than its U-turn's apex + the spacing that is an overshoot; one on order 5 / 8 leaves along F,
        // one joining, on Lambda or looping the loop turns in behind, as round C's) flies
        // the minimal turn-around: the loop's out-leg on its own side (above Lambda too: an arrival at P is 1.5 km above it, its out-leg at
        // 2.5 R is its own line), the U-turn at R onto Lambda, the straight run (lane_loop_base: no kLaneRedepM floor). Decided once; with a
        // member in its room, the plain re-departure (kLaneRedepM out, kLaneRedepSideM below Lambda). The corridor as the loop's
        // the corridor as the loop's; statics not yet known (no fresh profile): decided at a later frame, the plain re-departure meanwhile
        if (X3M_LANE_RMIN && lane_laws(m) && !m.loop && !m.over && !m.loop_min_done && lane_stage && !m.lane_on && fF < 0.0f && m.lane_s < 0.0f &&
            m.lane_s > -(dend(m) + kLaneRedepM) && m.params.W > 1e-3f) {
            const float Rl = lane_vmax(m) / m.params.W;
            // (item 5: at the gate mouth (inside d_end) a re-departure behind a ship farther out that is not itself returning holds until
            // that ship's launch, lane_mouth_hold; its own re-departure is decided from there. Review of the arrival round item 4: the
            // predecessor the law's block reads, m.lane_pred (the lane order is built after this pass: the previous frame's; a decision
            // taken here while this frame's predecessor holds is undone at the block, loop_min_done 2)
            bool held = false;
            if (m.lane_pred) {
                const int pj = find(g, m.lane_pred);
                held = pj >= 0 && &g.m[pj] != &m && lane_mouth_on(g, m, g.m[pj], now);
            }
            m.loop_min = held ? 0 : 1; // (lane_loop_base's minimal turn point for the room and the corridor below)
            const float base = lane_loop_base(g, m, Rl), s_room = -(dend(m) + base + kLaneOverApexR * Rl + kLaneGapM + m.L);
            // (the minimal turn-around's time to d_end at the lane speed: out to the turn point, the pursuit U-turn, back; a member whose
            // ETA to d_end is under kLaneMinEtaK of it plus the spacing is in its room by time: it goes first, the plain re-departure merges
            // behind it. Measured in cast redeparture: a convoy 16 km out, ETA 137 s against the loop's 135 s with the spacing, reached the
            // loop's U-turn as it ended, the pair uncertified 343 frames)
            const float vl = lane_vmax(m) > kYieldFloorMs ? lane_vmax(m) : kYieldFloorMs, s_t = -(dend(m) + base);
            const float side = 2.5f * Rl > kLaneMinSideK * kLaneTurnM ? 2.5f * Rl : kLaneMinSideK * kLaneTurnM;
            const float t_min = ((m.lane_s > s_t ? m.lane_s - s_t : 0.0f) + 2.0f * kLaneOverApexR * Rl + 0.5f * sc::kPi * side + base) / vl;
            bool lone = true;
            for (unsigned j = 0; j < g.n && lone; ++j) {
                const Member& o = g.m[j];
                if (&o == &m || !o.ship || !live(g, o, now) || !(pair_role(o.role)) || o.sb == Sb::dock || o.sb == Sb::launch) continue;
                const float d2[3] = {o.st.x[0] - g.G[0], o.st.x[1] - g.G[1], o.st.x[2] - g.G[2]}, so = sc::dot3(d2, g.F);
                if (so < 0.0f && so > s_room - o.L && o.over) lone = false; // (a ship ahead on its join, on Lambda or on its own
                // loop is the loop's own case: its turn point moves out until the U-turn ends g_req behind it, loop_sp; an overshoot ranks
                // as the head wherever it is)
                if (!o.redep && !o.over && !o.loop && (o.sb == Sb::inbound || o.sb == Sb::funnel) && so < s_room - o.L &&
                    o.lane_eta < kLaneMinEtaK * t_min + (kLaneGapM + m.L + o.L) / vl)
                    lone = false;
            }
            const float cap = lone && !held ? lane_loop_cap(g, m, Rl, now) : 0.0f;
            if (held)
                ; // (decided at a later frame; the plain re-departure's state meanwhile, the hold below)
            else if (lone && cap >= base) {
                m.loop = 1;
                m.loop_min_done = 2; // (decided on this frame: the block confirms it, review item 4)
                m.loop_conv = 0;
                m.loop_out = m.loop_sp = 0.0f;
                m.over_ret_ms = 0;
                const float nl = sc::sqrt_ss(er * er + eu * eu);
                m.loop_n[0] = nl > 1.0f ? er / nl : 0.0f;
                m.loop_n[1] = nl > 1.0f ? eu / nl : -1.0f;
            } else {
                m.loop_min = 0;
                if (!lone || cap >= 0.0f) m.loop_min_done = 2; // (a member in its room, or a static on the corridor: the plain one)
            }
        }
        if (m.loop && (m.lane_on || !lane_stage)) m.loop = 0;
        if (m.loop) { // the corridor every frame: a static onto it (the 1 Hz scan) or the statics no longer known: today's join from here
            const float Rc = m.params.W > 1e-3f ? lane_vmax(m) / m.params.W : 0.0f;
            if (lane_loop_cap(g, m, Rc, now) < lane_loop_base(g, m, Rc)) m.loop = 0;
        }
        // (its out-leg runs 2.5 R below Lambda, the U-turn's measured lateral extent, so the U-turn ends on Lambda heading +F: with 2 R it
        // ended about R / 2 above it and P2 came 8.5 deg off F in 2 of 9 runs; round C: a loop's the same on its own side)
        m.redep_r = (m.over || m.loop) && m.params.W > 1e-3f ? lane_vmax(m) / m.params.W : 0.0f;
        m.redep_out = m.over ? lane_blend_settle(g, m) + m.redep_r : kLaneRedepM;
        m.redep_side = m.over || m.loop ? 2.5f * m.redep_r : kLaneRedepSideM;
        // (the arrival round item 3: the pursuit U-turn's lateral extent is the turn carrot's (kLaneTurnM ahead at 45 deg) for a tight
        // hull: measured 1.19 km for an M2 of R 297 m at 70 m/s, which overshot Lambda by 450 m from a 2.5 R out-leg and reached P2 off F)
        if (m.loop && m.loop_min && m.redep_side < kLaneMinSideK * kLaneTurnM) m.redep_side = kLaneMinSideK * kLaneTurnM;
        if (m.loop) { // the out-leg 2.5 R off Lambda on its side (loop_n), its turn point
            const float R = m.redep_r, oR = er - m.redep_side * m.loop_n[0], oU = eu - m.redep_side * m.loop_n[1];
            const float dl = sc::sqrt_ss(oR * oR + oU * oU); // (to the out-leg's line)
            if (!m.loop_conv && dl <= kLaneLoopConvR * R && fF <= -kLaneLoopConvCos) m.loop_conv = 1; // on the leg: the turn point stays
            if (!m.loop_conv) { // not yet on the leg: the turn point R ahead of the ship (the U-turn from the leg only)
                const float o = -m.lane_s - dend(m) + R;
                if (o > m.loop_out) m.loop_out = o;
            }
            float out = lane_loop_out(g, m);
            if (m.loop_sp > out) out = m.loop_sp;
            const float cap = lane_loop_cap(g, m, R, now); // (>= the base: checked above)
            if (out > cap) out = cap;
            m.redep_out = out;
            // its lead (lane_eta, lane_score): the straight to the turn point and the nose's turn onto it at R ((1 - cos) pi R / 2)
            const float s_out = -(dend(m) + out), ds = m.lane_s > s_out ? m.lane_s - s_out : 0.0f, d2 = sc::sqrt_ss(ds * ds + dl * dl);
            const float cs = d2 > 1.0f ? -(fF * ds + sc::dot3(m.st.rows[2], g.R) * oR + sc::dot3(m.st.rows[2], g.U) * oU) / d2 : 1.0f;
            m.loop_lead = d2 + (1.0f - cs) * 0.5f * sc::kPi * R;
            m.redep = 1;
            if (!m.over && m.lane_s > -(dend(m) + out)) { // its return for a follower's block (as an overshoot's: the lead, the U-turn
                                                           // and the rejoin to its apex at the lane speed), until it reaches its turn point
                const float vl = lane_vmax(m) > kYieldFloorMs ? lane_vmax(m) : kYieldFloorMs;
                const float t_ret = (m.loop_lead + (sc::kPi + kLaneOverApexR) * R) / vl;
                m.over_ret_ms = now + std::uint32_t(int(1000.0f * t_ret));
                if (!m.over_ret_ms) m.over_ret_ms = 1;
            }
        }
        if (m.redep && m.lane_s < -(dend(m) + m.redep_out) && fF >= 0.0f) { // turned in: the join from here
            m.redep = 0;
            if (m.over) m.over_in = 1; // (item 3c: an overshoot's law runs from its own s from here)
            m.loop = 0;
        }
        if (!m.loop) m.loop_min = 0;
        if (!m.lane_on && fF < 0.0f && m.lane_s > -(dend(m) + m.redep_out) && m.funnel) m.redep = 1; // nose away inside the turn point: out
                                                                                                   // first (Run175 round item 3: inside the
                                                                                                   // funnel; an escort 39 km beside the gate
                                                                                                   // flying at its leader is no re-departure)
        if (m.redep && !m.funnel && !m.over && !m.loop) m.redep = 0;
        if (m.lane_on) m.redep = 0;
        if (!m.redep && !m.loop) m.loop_min_done = 0; // (the arrival round item 3: a later re-departure decides anew)
        if (!m.redep) { // (review of the arrival round item 3: the gate-mouth hold's bound belongs to this re-departure)
            m.mouth_pred = m.mouth_ms = 0;
            m.mouth_exp = 0;
        }
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
    // (Run175 round item 3: the joiners outside the funnel re-take their sticky moments together, on the gate's clock every kLaneKeepS + the
    // step: a consistent snapshot, so two far-off joiners change order only when their live ETAs cross at one; two near-equal ones are a tie
    // cluster, the lower key first)
    const bool retake = !g.ord_retake_ms || std::int32_t(now - g.ord_retake_ms) >= 2 * kKeepHalfMs + std::int32_t(g.step_ms);
    if (retake) g.ord_retake_ms = now ? now : 1;
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
        } else if (!m.funnel) { // (Run175 round item 3: outside the funnel, on the gate's clock)
            if (retake) m.lane_ord_ms = t;
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
    bool head_done = false;
    for (unsigned k = 0; k < n; ++k) {
        Member& m = g.m[idx[k]];
        g.lane_ord[k] = m.ship;
        m.lane_rank = std::uint8_t(k);
        // Run175 round (items 1-3, 5): a member the lane does not own (its width unread, outside the join funnel, on an unrewritten script
        // point) is a body for the certificate's sweep only: no law, no predecessor or follower, no pacing, no gate, no hold; it keeps
        // its rank (the row) and the module's own legs fly it
        if (!lane_laws(m)) {
            m.lane_pred = 0;
            m.merge_done = 1;
            m.band = 0;
            m.lane_free_ms = m.lane_gate_ms = 0;
            m.lstop_ms = m.lstop_last_ms = 0;
            m.mouth_pred = m.mouth_ms = 0;
            m.mouth_exp = 0;
            m.law = Law::join;
            m.v_sb = lane_vmax(m);
            m.lane_gap = 3.4e38f;
            continue;
        }
        // the predecessor: the nearest member ahead in the order that the lane owns (item 4: a loop whose zone this member's path does not
        // cross is transparent, lane_loop_zone), else the head's predecessor past d_end
        const Member* P = nullptr;
        for (unsigned kp = k; kp > 0 && !P;) {
            const Member& c = g.m[idx[--kp]];
            if (lane_laws(c) && !(c.loop && c.redep && !lane_loop_zone(g, c, m))) P = &c;
        }
        const bool k_has_p = P != nullptr; // (a ranked predecessor, not the head's one past d_end)
        if (!P && gp >= 0) P = &g.m[gp];
        // lane fix round 2 item 2: leaving (handed to another leader): its laws as before (lane_steer: their speed on the engine's leg to
        // the new leader); clear once its hull is a lane footprint (kLaneOffM) off Lambda
        if (m.exit_ms && m.role == Role::escort) m.exit_clear = m.lane_lat >= kLaneOffM + m.w ? 1 : 0;
        LaneCtx cx;
        // (lane fix round 2 items 3a / 5: an overshoot behind a member inside its return zone runs its law from its U-turn's apex, where it
        // rejoins)
        // (lane fix round 2 item 3c: the head too, until it has turned in: its pacing and its follow law measured from where it rejoins, not
        // from its position past d_end, where the pacing's stop at d_end held it 19 s in its turn back)
        const float s_self_ap = lane_apex_s(m);
        // (section 12.4, round C: a loop the same until it has turned in: it rejoins at its U-turn)
        // (the arrival round items 3 / 5: a plain re-departure the same from its turn point, where it turns in: before, its s inside d_end
        // gave the pacing and the escort gate a braking curve to d_end of 0 (Run174's 8.6 s stop of the lone leader, the escorts held at the
        // gate mouth past their leader's grant))
        const float s_rd = -(dend(m) + m.redep_out);
        // (item 5: held, its gap from where it is; review of the arrival round item 3: the hold's game-time bound, taken at its start for
        // this P: P's launch ETA (lane_launch_eta; before d_end no earlier than the through slot's known free time t_free, the KC's
        // refusal after a partner-side arrival included, + the hand-over and the order-5 leg) + T_stop + the step; on expiry counted and
        // over for this P, the law's stop cap from there)
        if (m.redep && P && lane_mouth_hold(g, m, *P, now)) {
            if (m.mouth_pred != P->ship) {
                std::uint32_t eta = lane_launch_eta(g, *P, now);
                if (t_free && (P->sb == Sb::inbound || P->sb == Sb::funnel)) {
                    const float vl = lane_vmax(*P) > kYieldFloorMs ? lane_vmax(*P) : kYieldFloorMs;
                    const std::uint32_t e2 = t_free + std::uint32_t(std::int32_t(1000.0f * (1.0f + leg5_s(*P, dend(*P), vl))));
                    if (std::int32_t(e2 - eta) > 0) eta = e2;
                }
                m.mouth_pred = P->ship;
                m.mouth_ms = eta + kLaneStopMs + g.step_ms;
                if (!m.mouth_ms) m.mouth_ms = 1;
                m.mouth_exp = 0;
            } else if (!m.mouth_exp && std::int32_t(now - m.mouth_ms) > 0) {
                m.mouth_exp = 1;
                if (st) ++st->mouth_expired;
            }
        }
        const bool mouth = m.redep && P && lane_mouth_on(g, m, *P, now);
        if (m.loop_min_done == 2) { // (review item 4: the lone decision of this frame read the previous frame's predecessor; this P holds it:
                                    // undone, decided once the hold is over)
            if (mouth) {
                m.loop = m.loop_min = m.loop_min_done = m.loop_conv = 0;
                m.loop_out = m.loop_sp = 0.0f;
                m.over_ret_ms = 0;
                m.redep_out = kLaneRedepM;
            } else
                m.loop_min_done = 1;
        }
        const float s_self = ((m.over && !m.over_in) || m.loop) && m.lane_s > s_self_ap ? s_self_ap
                             : X3M_LANE_RMIN && m.redep && !mouth && m.lane_s > s_rd ? s_rd : m.lane_s;
        cx.s = s_self;
        cx.v_lane = lane_vmax(m);
        cx.a = m.params.a_lin;
        cx.v_self = sc::dot3(m.st.u, g.F);
        cx.y = m.lane_on ? 0.0f : m.lane_lat;
        float gap = 3.4e38f;
        cx.d_end = dend(m);
        cx.path = lane_path_len(s_self, cx.y, lane_turn_r(m), lane_join_end(m), dend(m)); // (item 3: the pacing laws' distance)
        cx.loss = lane_join_loss(m.lane_s, cx.y, lane_join_end(m), dend(m), lane_turn_r(m));
        if (P && k_has_p) cx.loss -= lane_join_loss(P->lane_s, P->lane_on ? 0.0f : P->lane_lat, lane_join_end(*P), dend(*P), lane_turn_r(*P));
        if (P) {
            // (lane fix round 1 item 4: an overshoot ahead turns back no farther out than its U-turn's apex: no nearer than that along F;
            // until it has turned in it is a block, its follower stops at spacing behind the apex)
            const bool p_ret = k_has_p && ((P->over && !P->over_in) || P->loop); // (round C: a loop ahead as an overshoot)
            const float s_rj = lane_apex_s(*P), sp = p_ret && P->lane_s > s_rj ? s_rj : P->lane_s;
            gap = (sp - P->L) - (s_self + m.L);
            float vp = sc::dot3(P->st.u, g.F);
            cx.closing = vp < -kLaneStopV && !(k_has_p && (P->over || P->loop)); // (lane fix round 2 item 5: flying back toward it; an overshoot's out-leg is
                                                               // its block, bounded below)
            if (P->v_sb >= 0.0f && P->v_sb < vp) vp = P->v_sb;
            cx.pred = true;
            cx.gap = gap;
            cx.v_pred = vp > 0.0f ? vp : 0.0f;
            // (a block: stopped, or an overshoot turning back; lane fix round 2 item 5: the overshoot's block lasts its predicted return,
            // the out-leg to its turn point, a U-turn of pi R and the rejoin to its apex at the lane speed, + kLaneStopMs + the step; past
            // it the pair is the certificate's and the law's stop is bounded by T_stop as for any non-block)
            const bool over_block = k_has_p && (P->over || P->loop) && P->redep && P->over_ret_ms &&
                                    std::int32_t(now - P->over_ret_ms) <= std::int32_t(kLaneStopMs + g.step_ms);
            // (lane fix round 2 item 3d: a predecessor stopped at d_end on a slot refusal is the queue hold's block, which lasts the refusal;
            // its grant, order 5 or 8 seen, releases it at once, not once its speed passes kLaneStopV: the follower's stop ends within the step)
            cx.granted = P->sb == Sb::dock || P->sb == Sb::launch;
            cx.block = (sc::dot3(P->st.u, P->st.u) < kLaneStopV * kLaneStopV && !cx.granted) || over_block;
            // the arrival round item 5 (Run174: escorts arriving while their leader re-departed, held 274 s at the gate mouth on 0x14 and
            // evicted against it): a re-departing member at the gate mouth behind its predecessor (lane_mouth_hold: an escort that arrived
            // in trail of its leader, a ship ranked behind one on its final) holds where it is, the queue hold, until the predecessor's
            // launch (order 8 seen: the stop ends within the step of it), then flies its own re-departure (the loop, item 3)
            if (mouth) {
                cx.block = true;
                cx.closing = true;
            }
            cx.pred_final = P->sb == Sb::handover || P->lane_s > -(dend(*P) + lane_blend_settle(g, *P));
            // (round A: before its P2 on its final, the room to g_req behind its tail at d_end: the stop-room bound)
            if ((P->sb == Sb::inbound || P->sb == Sb::funnel) && cx.pred_final && !(k_has_p && (P->over || P->loop)) && P->lane_s < -dend(*P))
                cx.stop_room = (-dend(*P) - P->L - kLaneGapM) - (s_self + m.L);
            // section 12.4 (round C): a loop behind P turns where its U-turn ends g_req behind P's tail, P at its along-F speed over the
            // out-leg and the U-turn: s_t (1 + v_P / v) <= s_P + v_P (s + pi R) / v - g_req - L - L_P. The turn point only moves out, and only
            // before the ship has reached it; the loop flies at the lane speed (no gap law: at the law's floor beside Lambda the loop sat in
            // its passer's path) until it has turned in, the law from P after the U-turn
            if (m.loop) {
                if (m.lane_s > -(dend(m) + m.redep_out)) {
                    const float v = lane_vmax(m) > kYieldFloorMs ? lane_vmax(m) : kYieldFloorMs;
                    const float s_t = (sp + cx.v_pred * (m.lane_s + sc::kPi * m.redep_r) / v - kLaneGapM - m.L - P->L) / (1.0f + cx.v_pred / v);
                    const float o = -s_t - dend(m);
                    m.loop_sp = o; // (re-taken: a ship ahead that stopped for a while does not carry the loop out for good)
                    const float out = lane_loop_out(g, m), cap = lane_loop_cap(g, m, m.redep_r, now);
                    m.redep_out = o > out ? o : out;
                    if (m.redep_out > cap) m.redep_out = cap;
                }
                cx.pred = false;
            }
        }
        if (m.lane_pred != (P ? P->ship : 0u)) { // a new pair: a merge (the row label and its bound) when the raw gap is short
            m.lane_pred = P ? P->ship : 0u;
            // (the arrival round item 5: a ship held at the gate mouth behind P does not merge onto Lambda: no merge, no lateral band)
            m.merge_done = !P || mouth || gap >= kLaneGapM - 50.0f ? 1 : 0;
            m.band = P && !mouth && gap < kLaneGapM - 100.0f ? 1 : 0; // (the lateral rule: the band until the raw gap is g_req - 100 m)
            m.merge_ms = 0;
            if (!m.merge_done) {
                m.merge_ms = now ? now : 1;
                ++g.lane_merges;
                if (st) ++st->merges;
                m.law = Law::merge;
                lane_event(st, m, false, gap, 0);
            }
        }
        // a merge behind a ship that is itself still merging starts when that one completes (its speed is near the floor until then; the
        // follower's 40 s bound of section 6.11 item 2 runs from there): the start is re-stamped meanwhile
        if (!m.merge_done && m.merge_ms && P && P->lane_rank != 0xff && !P->merge_done && P->merge_ms) m.merge_ms = now ? now : 1;
        if (!P) m.merge_done = 1;
        m.lane_free_ms = !head_done ? t_free : 0u; // (the head: the first member the lane owns)
        head_done = true;
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
            cx.gate = !(X3M_LANE_MOUTH && m.redep); // (the arrival round item 5: a re-departing escort flies away from d_end, no braking reserve)
            cx.t_gate_s = float(std::int32_t(m.lane_gate_ms - now)) * 0.001f;
            cx.dt = float(g.step_ms > kStepClampMs ? kStepClampMs : g.step_ms) * 0.001f;
        }
        // lane fix round 1 item 4: the law's stop lasts at most T_stop + the step unless the ship ahead is physically stopped (a block); each
        // stop episode counts in lane_stops (ctl_merge_stop); an episode ends once its condition has been off for kLaneStopEndMs + the step
        // (the follow law flickers across the stop's threshold while it brakes)
        cx.stop_left_s = float(std::int32_t((m.lstop_ms ? m.lstop_ms : now) + kLaneStopMs + g.step_ms - now)) * 0.001f;
        Law law;
        bool stop_cond;
        float settle;
        m.v_sb = lane_law(cx, &law, &stop_cond, &settle);
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
        if (m.band && lane_band_free(gap, settle)) m.band = 0;
        if (!m.merge_done && gap >= kLaneGapM - 50.0f) { // review item 6: the merge completes on the raw gap (the finish row; the law
                                                         // settles a few m short of g_req at the cap)
            m.merge_done = 1;
            if (st) ++st->merges_done;
            lane_event(st, m, true, gap, m.merge_ms ? now - m.merge_ms : 0u);
        }
    }
    if (st) st->members = n;
}
// Site B's lane write on a member's frame (section 6.3-6.5; the DLL's clamp hook, the fixture's site B): a stop in force (the law's
// stop, or the brief stop's cap 0) writes 0 on the steered orders and order 0; on order 0 / 0x1f with the live nose f within 8 deg of F
// the lane speed stands (the script's re-issue gaps, the lost-leader wait; the hand-over P2 kept under the brake bound from P2). Round A
// (rank 4, holistic review section 2 d): a hand-over P2 did not keep gets no write (the vanilla stop: 0x1f rotates, order 5 from rest);
// the refused timer and its accelerate-then-brake are gone. False: no write. (*kind, optional: the write's LaneWrite for the lane row)
enum class LaneWrite : std::uint8_t { none = 0, stop, kept, hold, count };
inline const char* lane_write_name(LaneWrite w) noexcept {
    static const char* const n[unsigned(LaneWrite::count)] = {"none", "stop", "kept", "hold"};
    return unsigned(w) < unsigned(LaneWrite::count) ? n[unsigned(w)] : "?";
}
inline bool lane_site_b(const Gate& g, const Member& m, std::uint16_t order, const float x[3], const float f[3], float v_now, std::uint32_t now,
                        float* v, LaneWrite* kind = nullptr) noexcept {
    if (kind) *kind = LaneWrite::none;
    if (!lane_laws(m) || !(m.role == Role::committed || m.role == Role::escort)) return false; // (Run175 round: no write on a body)
    const bool steered = (m.role == Role::committed && order == sc::kOrderFlyBefore) ||
                         (m.role == Role::escort && (order == sc::kOrderEscort || order == sc::kOrderFollow));
    const bool lane = m.sb == Sb::inbound || m.sb == Sb::funnel;
    const float cap = cap_in_force(g, m, now);
    // (review of rounds A-C item 2: the yield's stop ends at the bound its yield stored, whatever the cap's age)
    if (lane && (steered || order == sc::kOrderNone) &&
        (m.law == Law::stop || (cap == 0.0f && m.stop_ms && (!m.stop_end_ms || std::int32_t(now - m.stop_end_ms) <= 0)))) {
        *v = 0.0f;
        if (kind) *kind = LaneWrite::stop;
        return true;
    }
    if (order != sc::kOrderNone && order != sc::kOrderRotate) return false;
    if (m.role == Role::committed && m.handover_ms && m.sb != Sb::dock && m.sb != Sb::launch) { // the hand-over after P2 (from P2's own
                                                                                                 // frame: lane_frame moves the stage next)
        if (!m.p2_kept || !(sc::dot3(f, g.F) >= kLaneHoldCos)) return false; // P2 not kept (lane fix round 1 item 3, Run173's 12.2 m/s
                                                                             // crawl; round A): no write, order 0 brakes it
        *v = lane_hand_bound(g, m, x, v_now); // P2 kept the speed: it stands under the brake bound
        if (kind) *kind = LaneWrite::kept;
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
    if (order != sc::kOrderNone || !lane_laws(m) || !m.q_ok || m.role != Role::escort || !m.res_until || !(m.sb == Sb::inbound || m.sb == Sb::funnel))
        return 0;
    return kLaneDeferMs + (g.step_ms < kStepClampMs ? g.step_ms : kStepClampMs);
}
// Site B's order-4 point rewrite (review item 2; site A's rule at the frame of a deferred tick; lane_defer_step): a lane member (an escort just given its
// order 4, or a committed member) on order 4 to this gate whose f+0x240 (p, world m) is one of the KC's 20 grid points for its radius
// gets L_i in *L. False: leave the field.
// (Review of the Run175 round: the test runs whatever pt_ok, and a grid point clears the fallback: a KC point re-issued after a foreign
// field returns the member to the lane.)
inline bool lane_site_b_point(const Gate& g, Member& m, std::uint16_t order, std::uint32_t target, const float p[3], float L[3]) noexcept {
    if (order != sc::kOrderFlyBefore || target != g.gate || !m.ln || !m.w_ok || !m.funnel || !m.q_ok ||
        !(m.role == Role::escort || m.role == Role::committed))
        return false;
    if (kc_grid_slot_any(g.G, g.R, g.U, g.F, g.r_gate, m.r, p) < 0) return false; // (Run175 round item 1: a script point of any size)
    m.pt_ok = 0;
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
    if (!lane_steers(m)) return false; // Run175 round: no lane point outside the funnel, with the width unread, or on an unrewritten script point
    const bool o4 = m.role == Role::committed && order == sc::kOrderFlyBefore;
    if ((o4 || (m.role == Role::escort && (order == sc::kOrderEscort || order == sc::kOrderFollow))) && (m.sb == Sb::inbound || m.sb == Sb::funnel)) {
        const float d[3] = {x[0] - g.G[0], x[1] - g.G[1], x[2] - g.G[2]};
        const float s = sc::dot3(d, g.F);
        float lat[2];
        lane_lat_at(g, m, s, lat);
        const float er = sc::dot3(d, g.R) - lat[0], eu = sc::dot3(d, g.U) - lat[1];
        const float y = sc::sqrt_ss(er * er + eu * eu);
        const bool hold_lat = m.band && y > kLaneOnM; // (the lateral rule: the band until the raw gap is g_req - 100 m, once per pair)
        const int pi = hold_lat && m.lane_pred ? find(g, m.lane_pred) : -1;
        float sc_c, yc;
        lane_carrot(s, y, v_now, m.params.W, rg, hold_lat, m.redep != 0, &sc_c, &yc, pi >= 0 ? g.m[pi].w + m.w + kLaneSideM : 0.0f,
                    lane_join_end(m), m.redep_out, m.redep_r, dend(m), m.loop ? lane_blend_settle(g, m) : 0.0f);
        float latc[2];
        lane_lat_at(g, m, sc_c, latc);
        const float k = y > 1.0f ? yc / y : 0.0f;
        if (m.loop) { // section 12.4 (round C): the out-leg on the loop's side (loop_n), the turn back toward Lambda
            const float nR = m.loop_n[0], nU = m.loop_n[1];
            if (s <= -(dend(m) + m.redep_out))
                for (unsigned i = 0; i < 3; ++i) out->P[i] = x[i] + kLaneTurnM * (g.F[i] - nR * g.R[i] - nU * g.U[i]);
            else
                for (unsigned i = 0; i < 3; ++i)
                    out->P[i] = g.G[i] + (latc[0] + m.redep_side * nR) * g.R[i] + (latc[1] + m.redep_side * nU) * g.U[i] + sc_c * g.F[i];
        } else if (m.redep && s <= -(dend(m) + m.redep_out)) // the turn: ahead (+F) and above (+U) of the ship, onto Lambda
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
    return m.role == Role::committed && (m.sb == Sb::funnel || (lane_steers(m) && m.sb == Sb::inbound));
}
// Lane stage 1 (section 6.4): P2's coast bound (stop before the inflated trigger on a refusal) is replaced for a lane member by site B's
// hold with the brake bound from P2 (lane_hand_bound): its speed bound is open, the nose and velocity tolerances stay.
__attribute__((always_inline)) inline float lane_p2_bound(const Member& m, float bound) noexcept {
    return m.role == Role::committed && lane_steers(m) && (m.sb == Sb::funnel || m.sb == Sb::inbound) ? 3.4e38f : bound;
}
// Whether the lane owns a committed member's gate legs (review F8): Q known and a lane stage. Otherwise the member keeps the stage-A /
// amendment-15 join (join on, P2 kept when aligned), never "join off + vanilla stop".
__attribute__((always_inline)) inline bool sb_managed(const Member& m) noexcept {
    if (m.role != Role::committed || !m.q_ok) return false;
    if (m.ln) return lane_steers(m) && (m.sb == Sb::inbound || m.sb == Sb::funnel || m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch); // the lane
    return m.sb == Sb::funnel || m.sb == Sb::handover || m.sb == Sb::dock || m.sb == Sb::launch;
}
} // namespace x3m::ship_avoidance::ctl
