#pragma once
#include "weapon_effects_stage.h"
#include <xmmintrin.h>

// The hull hit's scene-end step (docs/architecture/weapon-look.md 2.4-2.11 and 3.1, step 2): the ring's stage-owned hull
// hits (role_hull_hit with flag_stage, shields,hull mode) become the frame's flash and spark quads (the sprite batch,
// renderer::WeaponSpriteVertex) and glow and ember boxes (the glow primitive, renderer::WeaponGlowVertex).
//
// Per record, at the contact point Event::pos in its owner's frame, sized in the owner's cell unit U = cell_m[tier]
// and the impact's calibre k (calibre_of: the running damage total, or the override's): r_hit = r_scale U (r_base + r_k k);
// - flash: a white disc core of flash_core r_hit (>= floor_flash_px) in a halo of r_hit at heat flash_halo_t (a missile
//   source: its tint), radiance core_e, fading linearly over flash_ms; its centre biased towards the eye by bias r_hit;
// - sparks: n = round(spark_n0 + spark_nk k) (x missile_spark_x for a missile source, x asteroid_dust_x for an asteroid)
//   in antipodal pairs of a Fibonacci hemisphere turned by a per-record rotation (isotropic: the lane hides the half
//   that flies into the hull), speed spark_speed_u U/s x (1 +- spark_speed_spread), plus the owner's velocity at the
//   record's first stage frame, closed form p(t) = p0 + v0 tau (1 - e^(-t / tau)) in world space from the contact point
//   at that frame (they leave the owner and outlive it); each drawn from its previous frame's screen position (p(t -
//   step) through the previous frame's view) to its current one, never shorter than its rest length, capped at
//   streak_cap rest lengths, its radiance x rest / (rest + streak) but not below streak_core_min; cooling on the heat
//   ramp over its life (spark_life_ms + spark_life_k_ms k, x (1 +- stagger / 2));
// - glow: the glow primitive at the contact point, R = glow_r r_hit, heat glow_t x glow_e, linear over glow_ms;
// - ember: R = ember_r r_hit, heat ember_t cooling to 0 over ember_ms (radiance ramp(T) x ember_e x T).
// Variants (Event::detail): repair (the override): glow and ember only, the weapon tint; tug: the glow only, pale blue;
// asteroid: no flash, the sparks x 2 on the ramp's last stops. Every life is game time from the record's first stage frame
// (age 0 on its first drawn frame) and never shorter than min_frames x its first frame's game step (the three-frame floor),
// each element's life x (1 +- stagger / 2) by a hash of its record and index. The flash, glow and ember ride the owner
// (re-posed every frame through its rows, validated as the shield stage validates); a record whose owner went after
// its first frame keeps its sparks only (orphan), one whose owner never validated leaves the ring (retired). The own
// ship in the cockpit view: every element of a hit on it at own_cockpit_gain (look 2.9).
// The shared budget (look 2.11): each glow is charged its screen rectangle x budget_weight_glow; past the screens
// the patches left, the glows and embers of owners other than the own ship go, oldest first (shed). Sparks and flashes
// are never shed (count caps: hull::max_sparks spark quads; renderer::weapon_sprite_quads, weapon_glow_boxes).
//
// State: HullState holds, per ring slot, the record's first stage frame (its game ms, its world contact point, the
// owner's velocity then, the three-frame floor of that frame's step, kept for the record's life so a SETA change never
// lengthens a fading element back into view) keyed by the record's sequence, and the owners' last positions for the
// velocity: every owner of a stage-owned record (a shield hit's too) is tracked each frame, so a first hull hit on an
// owner drawn in the previous stage frame takes the node's +0x30 delta since then, else 0. Past hull::max_owners
// owners a frame, those whose newest hull hit is the oldest are not drawn (ties by address: a stable choice). CPU data
// naming engine objects only: a Reset leaves it valid; a cleared or reused ring slot is recognised by its sequence. No
// allocation, no libm, SSE2 floats and doubles.
namespace x3m::weapon_effects::stage {
namespace core = x3m::weapon_effects::core;
struct HullSlot {
    std::uint32_t sequence = 0, first_ms = 0;
    double p0[3]{};   // the contact point in world (view units) at the record's first stage frame
    float v[3]{};     // the owner's velocity then (world, view units per game second)
    float floor_ms = 0.f; // the three-frame floor latched at the first drawn frame (min_frames x that frame's step)
    std::uint8_t latched = 0, orphan = 0;
};
struct OwnerTrack {
    std::uint32_t owner = 0, node = 0, handle = 0, ms = 0, used = 0;
    std::int32_t pos[3]{};
};
// The owners' positions at their last stage frame: every owner with a stage-owned record (a shield hit's too) is
// tracked, so a first hull hit on an owner drawn in the previous stage frame takes its velocity at once.
constexpr unsigned owner_tracks = 2 * core::hull::max_owners;
struct HullState {
    HullSlot slots[core::ring_capacity];
    OwnerTrack tracks[owner_tracks];
    std::uint32_t use = 0;
    std::uint32_t prev_now = 0; // the previous stage call's game ms (has_prev)
    bool has_prev = false;
};
inline void hull_clear(HullState& s) noexcept {
    for (auto& v : s.slots) v = HullSlot{};
    for (auto& t : s.tracks) t = OwnerTrack{};
    s.use = 0;
    s.prev_now = 0;
    s.has_prev = false;
}
// The frame: the current view with its jittered projection (cam.m00, m11, m20, m21) and the previous frame's view
// (prev_valid false: the current view stands for it), the target's size, the near plane, the game clock and step, the
// own ship (its root node; 0 unknown) and the cockpit view, and the screens of the budget the patches left.
struct HullView {
    Camera cam{}, prev{};
    bool prev_valid = false;
    float width = 0.f, height = 0.f, near_z = 1.f;
    std::uint32_t now = 0, step_ms = 0;
    std::uint32_t own_node = 0;
    bool cockpit = false;
    float budget_left = core::screen_budget;
};
// The look's terms (header defaults; the fixture isolates one element or changes a constant against its law).
struct HullTuning {
    core::hull::Mode mode = core::hull::mode;
    bool flash = true, sparks = true, glow = core::hull::glow, ember = true;
    float cell_m[core::patch_tier_count] = {core::cell_m[0], core::cell_m[1], core::cell_m[2]};
    float stagger = core::stagger;
    float own_gain = core::own_cockpit_gain;
    unsigned min_frames = core::look::min_frames;
    float spark_count_x = core::hull::spark_count_x; // the kill criterion's halving (a header decision, never a setting)
};
struct HullCounts {
    unsigned hits = 0, flashes = 0, sparks = 0, glows = 0, embers = 0;
    unsigned orphans = 0, retired = 0, shed = 0, capped = 0, floored = 0, owners_dropped = 0;
    float screens = 0.f; // the glows' charged screens after shedding
    float max_floor_ms = 0.f; // the largest floor latched by a live record (Present's keep, HookState::life_floor_ms)
};
inline std::uint32_t mix32(std::uint32_t x) noexcept {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
inline float unit_of(std::uint32_t h) noexcept {
    return float(h >> 8) * (1.f / 16777216.f);
}
// The record's element seed (sequence, element index).
inline std::uint32_t element_hash(std::uint32_t sequence, std::uint32_t index) noexcept {
    return mix32(sequence * 0x9e3779b9u + index * 0x85ebca6bu + 0x632be5abu);
}
// An element's life (game ms): its law x (1 + stagger (h - 1/2)), never below the floor; *floored when the floor won.
inline float element_life_ms(float law_ms, std::uint32_t h, float stagger, float floor_ms, bool* floored) noexcept {
    const float l = law_ms * (1.f + stagger * (unit_of(h) - 0.5f));
    *floored = floor_ms > l;
    return *floored ? floor_ms : l;
}
// The spark count of a record (before the frame's cap).
inline unsigned spark_count(const core::Event& e, float k, const HullTuning& t) noexcept {
    if (t.mode == core::hull::simple || (e.detail & (core::detail_repair | core::detail_tug))) return 0;
    float n = (float(core::hull::spark_n0) + float(core::hull::spark_nk) * k) * t.spark_count_x;
    if (e.table == core::table_missiles) n *= core::hull::missile_spark_x;
    if (e.detail & core::detail_asteroid) n *= core::hull::asteroid_dust_x;
    return unsigned(n + 0.5f);
}
// The record's calibre and r_hit (view units).
inline float hull_calibre(const core::Event& e) noexcept {
    return core::calibre_of(e);
}
inline float hull_unit(const core::Event& e, const HullTuning& t) noexcept {
    const unsigned tier = e.patch_tier < core::patch_tier_count ? e.patch_tier : unsigned(core::patch_capital);
    return metres_to_view(t.cell_m[tier]);
}
inline float hull_r_hit(const core::Event& e, const HullTuning& t) noexcept {
    return core::hull::r_scale * hull_unit(e, t) * (core::hull::r_base + core::hull::r_k * hull_calibre(e));
}
// The world point (view units, double) of an owner-frame contact point.
inline void hit_world(const Owner& o, const std::int32_t pos[3], double out[3]) noexcept {
    const double s = double(core::world_scale);
    for (unsigned i = 0; i < 3; ++i) {
        double v = double(o.pos[i]);
        for (unsigned j = 0; j < 3; ++j) v += double(o.rows[4 * j + i]) * double(pos[j]) / 65536.0;
        out[i] = v * s;
    }
}
// The spark's closed-form displacement factor tau (1 - e^(-t / tau)), t in game seconds.
inline float spark_reach_factor(float t_s) noexcept {
    const float tau = core::hull::spark_tau_s;
    return t_s > 0.f ? tau * (1.f - core::exp_neg(-t_s / tau)) : 0.f;
}
// The per-record rotation (a hashed unit quaternion) as rows: world = R fib.
inline void record_rotation(std::uint32_t sequence, float r[9]) noexcept {
    float q[4];
    for (unsigned k = 0; k < 4; ++k) q[k] = 2.f * unit_of(element_hash(sequence, 1000u + k)) - 1.f;
    float n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if (!(n > 1e-4f)) {
        q[0] = 1.f;
        q[1] = q[2] = q[3] = 0.f;
        n = 1.f;
    }
    const float s = 2.f / n, w = q[0], x = q[1], y = q[2], z = q[3];
    r[0] = 1.f - s * (y * y + z * z), r[1] = s * (x * y - w * z), r[2] = s * (x * z + w * y);
    r[3] = s * (x * y + w * z), r[4] = 1.f - s * (x * x + z * z), r[5] = s * (y * z - w * x);
    r[6] = s * (x * z - w * y), r[7] = s * (y * z + w * x), r[8] = 1.f - s * (x * x + y * y);
}
// The camera's rotation as rows of world -> view (view_j = sum_i world_i r[i*3+j]): M = Cam^T-ish product with R.
inline void view_of_rotation(const Camera& c, const float r[9], float out[9]) noexcept {
    // out maps a fib vector f to view: view_j = sum_i (sum_k r[i*3+k] f_k) c.r[i*3+j] = sum_k f_k (sum_i r[i*3+k] c.r[i*3+j])
    for (unsigned j = 0; j < 3; ++j)
        for (unsigned k = 0; k < 3; ++k) {
            float v = 0.f;
            for (unsigned i = 0; i < 3; ++i) v += r[i * 3 + k] * c.r[i * 3 + j];
            out[j * 3 + k] = v; // view_j = sum_k out[j*3+k] f_k
        }
}
inline void rotate_world_to_view(const Camera& c, const float w[3], float out[3]) noexcept {
    for (unsigned j = 0; j < 3; ++j) out[j] = w[0] * c.r[j] + w[1] * c.r[3 + j] + w[2] * c.r[6 + j];
}
// The spark loop's reciprocals (the performance pass: a division or a square root's latency was its largest cost, the
// branch on the streak length waiting on it): the SSE estimate refined by one Newton step (relative error below 2e-7,
// far below a pixel at any depth the stage draws), documented SSE intrinsics only.
inline float rcp_nr(float x) noexcept {
    const float y = _mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss(x)));
    return y * (2.f - x * y);
}
inline float rsqrt_nr(float x) noexcept {
    const float y = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x)));
    return y * (1.5f - 0.5f * x * y * y);
}
// Screen pixels of a view point (z > near) under the jittered projection: one reciprocal per point, the scale factors
// once per frame.
struct Px {
    float x, y, z, iz;
};
struct Screen {
    float hw, hh, sx, sy; // W / 2, H / 2, 2 / W, 2 / H
};
inline Screen screen_of(const HullView& f) noexcept {
    return Screen{0.5f * f.width, 0.5f * f.height, 2.f / f.width, 2.f / f.height};
}
inline Px to_px(const HullView& f, const Screen& s, const float v[3]) noexcept {
    const float iz = rcp_nr(v[2]);
    return Px{(f.cam.m00 * v[0] * iz + f.cam.m20 + 1.f) * s.hw, (1.f - f.cam.m11 * v[1] * iz - f.cam.m21) * s.hh, v[2], iz};
}
inline Px to_px(const HullView& f, const float v[3]) noexcept {
    return to_px(f, screen_of(f), v);
}
inline void px_clip(const HullView& f, const Screen& s, float x, float y, float z, float out[4]) noexcept {
    out[0] = (x * s.sx - 1.f) * z;
    out[1] = (1.f - y * s.sy) * z;
    out[2] = z - f.near_z;
    out[3] = z;
}
inline void px_clip(const HullView& f, float x, float y, float z, float out[4]) noexcept {
    px_clip(f, screen_of(f), x, y, z, out);
}
inline std::uint32_t pack_colour(const float c[3], float alpha) noexcept {
    std::uint32_t w = 0;
    for (unsigned k = 0; k < 3; ++k) {
        const float v = c[k] > 0.f ? (c[k] < 1.f ? c[k] : 1.f) : 0.f;
        w |= std::uint32_t(v * 255.f + 0.5f) << (16 - 8 * k); // D3DCOLOR: A R G B
    }
    const float a = alpha > 0.f ? (alpha < 1.f ? alpha : 1.f) : 0.f;
    std::uint32_t q = std::uint32_t(a * 255.f + 0.5f);
    if (alpha > 0.f && !q) q = 1;
    return w | q << 24;
}
// One streak's four vertices (the vertex program expands them, weapon_sprite_vs.hlsl): its head (view, this frame),
// its tail (view, the previous frame's position through the previous view), its radiance and the hold a diluted
// streak keeps, its width (view units), its colour; corners tail-left, tail-right, head-left, head-right.
inline void streak_vertices(const float head[3], const float tail[3], float radiance, float hold, float width_view,
                            std::uint32_t colour, renderer::WeaponSpriteVertex* out) noexcept {
    for (unsigned c = 0; c < 4; ++c) {
        renderer::WeaponSpriteVertex& v = out[c];
        v.a[0] = head[0];
        v.a[1] = head[1];
        v.a[2] = head[2];
        v.a[3] = radiance;
        v.b[0] = tail[0];
        v.b[1] = tail[1];
        v.b[2] = tail[2];
        v.b[3] = width_view;
        v.c[0] = c >= 2 ? 1.f : -1.f;
        v.c[1] = (c & 1u) ? -1.f : 1.f;
        v.c[2] = hold;
        v.colour = colour;
    }
}
// The CPU twin of weapon_sprite_vs.hlsl's streak expansion (the fixture measures the drawn quads through it): each
// corner's pixel position and view depth, and the radiance the program draws.
struct StreakCorners {
    float x[4], y[4], z[4];
    float radiance, motion, rest, lit;
};
inline StreakCorners streak_corners(const HullView& f, const renderer::WeaponSpriteVertex* v) noexcept {
    StreakCorners o{};
    const Screen sc = screen_of(f);
    const Px head = to_px(f, sc, v[0].a), tail = to_px(f, sc, v[0].b);
    float dx = head.x - tail.x, dy = head.y - tail.y;
    const float len2 = dx * dx + dy * dy;
    o.motion = scalar::sqrt(len2);
    if (len2 > 1e-6f) {
        dx /= o.motion;
        dy /= o.motion;
    } else {
        dx = 1.f;
        dy = 0.f;
    }
    float lit = v[0].b[3] * sc.hw * f.cam.m00 / v[0].a[2];
    o.lit = lit > core::look::floor_lit_px ? lit : core::look::floor_lit_px;
    const float rest_w = core::hull::spark_rest_w * o.lit;
    o.rest = rest_w > core::look::floor_len_px ? rest_w : core::look::floor_len_px;
    const float cap = core::look::streak_cap * o.rest;
    const float drawn = o.motion < o.rest ? o.rest : o.motion > cap ? cap : o.motion;
    const float hw = 0.5f * o.lit, nx = -dy * hw, ny = dx * hw;
    for (unsigned c = 0; c < 4; ++c) {
        const bool at_head = v[c].c[0] > 0.f;
        const float along = at_head ? hw : -(drawn + hw);
        o.x[c] = head.x + dx * along + nx * v[c].c[1];
        o.y[c] = head.y + dy * along + ny * v[c].c[1];
        o.z[c] = at_head ? v[c].a[2] : v[c].b[2];
    }
    const float r = v[0].a[3] * o.rest / (o.rest + o.motion);
    o.radiance = r > v[0].c[2] ? r : v[0].c[2];
    return o;
}
// The glow primitive's raster: the light sphere's exact screen rectangle (per axis the two tangent planes through the
// eye: tan = (a z +- R sqrt(a^2 + z^2 - R^2)) / (z^2 - R^2)), clipped to the screen, in ndc (x0, y0, x1, y1); the whole
// screen when the sphere reaches the near plane (the eye inside it, or nearly). False when it lies off screen. Its area
// is the budget's charge (4 / pi of the disc's at most; a view-aligned box's silhouette was up to twice the disc).
inline bool sphere_rect(const HullView& f, const float c[3], float R, float r[4], bool* full) noexcept {
    *full = !(c[2] - R > f.near_z);
    if (*full) {
        r[0] = r[1] = -1.f;
        r[2] = r[3] = 1.f;
        return true;
    }
    const float zz = c[2] * c[2] - R * R, inv = 1.f / zz;
    const float m[2] = {f.cam.m00, f.cam.m11}, o[2] = {f.cam.m20, f.cam.m21};
    for (unsigned a = 0; a < 2; ++a) {
        const float s = R * scalar::sqrt(c[a] * c[a] + zz);
        float lo = m[a] * (c[a] * c[2] - s) * inv + o[a], hi = m[a] * (c[a] * c[2] + s) * inv + o[a];
        lo = lo > -1.f ? lo : -1.f;
        hi = hi < 1.f ? hi : 1.f;
        if (!(hi > lo)) return false;
        r[a] = lo;
        r[2 + a] = hi;
    }
    return true;
}
inline float rect_screens(const float r[4]) noexcept {
    return 0.25f * (r[2] - r[0]) * (r[3] - r[1]);
}
// The glow's four corners: the rectangle at view depth z_q (the sphere's front, or 1.5 x the near plane for the whole
// screen), each corner carrying the light (centre, R) and its radiance colour.
inline void glow_quad(const HullView& f, const float c[3], float R, const float rect[4], bool full, const float colour[3],
                      renderer::WeaponGlowVertex* out) noexcept {
    const float zq = full ? 1.5f * f.near_z : c[2] - R;
    const float kx = zq / f.cam.m00, ky = zq / f.cam.m11;
    for (unsigned k = 0; k < 4; ++k) {
        const float nx = rect[k & 1u ? 2 : 0], ny = rect[k & 2u ? 3 : 1];
        out[k].corner[0] = (nx - f.cam.m20) * kx;
        out[k].corner[1] = (ny - f.cam.m21) * ky;
        out[k].corner[2] = zq;
        out[k].light[0] = c[0];
        out[k].light[1] = c[1];
        out[k].light[2] = c[2];
        out[k].light[3] = R;
        for (unsigned i = 0; i < 3; ++i) out[k].colour[i] = colour[i];
    }
}
// Builds the frame's hull-hit sprites and glows (see the header). Returns the counts; *sprite_quads and *glow_boxes the
// outputs written (four vertices each: a sprite quad, a glow's screen rectangle).
inline HullCounts build_hull(core::Ring& ring, HullState& state, Reader rd, const HullView& f, const HullTuning& t,
                             renderer::WeaponSpriteVertex* sprites, unsigned sprite_cap, renderer::WeaponGlowVertex* glows,
                             unsigned glow_cap, unsigned* sprite_quads, unsigned* glow_boxes) noexcept {
    HullCounts n{};
    unsigned nq = 0, ng = 0;
    *sprite_quads = *glow_boxes = 0;
    if (!(f.width > 0.f) || !(f.height > 0.f) || !(f.cam.m00 > 0.f) || !(f.cam.m11 > 0.f)) return n;
    // The owners (review 3): every owner of a stage-owned record, its newest hull hit's age (t0, game ms; a shield-only
    // owner after them) and whether it has a hull hit; past hull::max_owners the owners whose newest hit is the oldest
    // are not drawn this frame (ties by the object address), a stable choice from frame to frame (no blinking).
    struct Candidate {
        std::uint32_t owner, node, handle, newest, index; // index: a record of the owner (the reader's validation)
        bool hull;
    };
    Candidate cand[core::ring_capacity];
    unsigned cand_count = 0;
    for (unsigned i = 0; i < core::ring_capacity; ++i) {
        const core::Event& e = ring.slots[i];
        if (!e.sequence || !(e.flags & core::flag_stage) || (e.role != core::role_hull_hit && e.role != core::role_shield_hit))
            continue;
        const bool is_hull = e.role == core::role_hull_hit;
        if (is_hull && state.slots[i].sequence == e.sequence && state.slots[i].orphan) continue; // needs no owner
        const std::uint32_t age = f.now - e.t0_game_ms;
        Candidate* c = nullptr;
        for (unsigned k = 0; k < cand_count && !c; ++k)
            if (cand[k].owner == e.owner) c = &cand[k];
        if (!c) {
            c = &cand[cand_count++];
            *c = Candidate{e.owner, e.node, e.handle, 0xffffffffu, i, false};
        }
        if (is_hull && (!c->hull || age < c->newest)) {
            c->newest = age;
            c->index = i;
        }
        c->hull = c->hull || is_hull;
    }
    auto before = [](const Candidate& a, const Candidate& b) {
        if (a.hull != b.hull) return a.hull;
        if (a.newest != b.newest) return a.newest < b.newest;
        return a.owner < b.owner;
    };
    const unsigned keep = cand_count < core::hull::max_owners ? cand_count : core::hull::max_owners;
    for (unsigned a = 0; a < keep; ++a) { // the first `keep` in order (a partial selection sort, keep <= 32)
        unsigned best = a;
        for (unsigned b = a + 1; b < cand_count; ++b)
            if (before(cand[b], cand[best])) best = b;
        if (best != a) {
            const Candidate tmp = cand[a];
            cand[a] = cand[best];
            cand[best] = tmp;
        }
    }
    for (unsigned k = keep; k < cand_count; ++k) n.owners_dropped += cand[k].hull;
    struct Seen {
        std::uint32_t owner;
        OwnerStatus status;
        Owner data;
        float vel[3];
    };
    Seen seen[core::hull::max_owners];
    const unsigned seen_count = keep;
    for (unsigned k = 0; k < keep; ++k) { // read the kept owners, track them, take their velocity
        Seen& sn = seen[k];
        sn.owner = cand[k].owner;
        sn.status = read_owner(rd, ring.slots[cand[k].index], &sn.data);
        sn.vel[0] = sn.vel[1] = sn.vel[2] = 0.f;
        if (sn.status != OwnerStatus::ok) continue;
        OwnerTrack* tr = nullptr;
        OwnerTrack* lru = &state.tracks[0];
        for (auto& c : state.tracks) {
            if (c.owner == cand[k].owner && c.node == cand[k].node && c.handle == cand[k].handle) tr = &c;
            if (c.used < lru->used) lru = &c;
        }
        // The node's delta since the previous stage frame, when this owner was drawn in it (else 0).
        if (tr && state.has_prev && tr->ms == state.prev_now && tr->ms != f.now) {
            const std::uint32_t dt = f.now - tr->ms;
            if (dt < 2000u)
                for (unsigned q = 0; q < 3; ++q)
                    sn.vel[q] = float(double(core::wrap_sub(sn.data.pos[q], tr->pos[q])) * double(core::world_scale) *
                                      1000.0 / double(dt));
        }
        if (!tr) {
            tr = lru;
            tr->owner = cand[k].owner;
            tr->node = cand[k].node;
            tr->handle = cand[k].handle;
        }
        for (unsigned q = 0; q < 3; ++q) tr->pos[q] = sn.data.pos[q];
        tr->ms = f.now;
        tr->used = ++state.use;
    }
    if (!state.has_prev || state.prev_now != f.now) {
        state.prev_now = f.now;
        state.has_prev = true;
    }
    // The budget's bookkeeping per glow box: its age (game ms), own, charged screens.
    static_assert(renderer::weapon_glow_boxes <= 0xffff, "glow indices");
    std::uint32_t glow_age[renderer::weapon_glow_boxes];
    float glow_area[renderer::weapon_glow_boxes];
    std::uint8_t glow_own[renderer::weapon_glow_boxes];
    const float frame_floor_ms = float(t.min_frames) * float(f.step_ms);
    const Camera& prev = f.prev_valid ? f.prev : f.cam;
    const float step_s = float(f.step_ms) * 1e-3f;
    const float px_x = 0.5f * f.width * f.cam.m00; // pixels per view unit at depth 1 (x)
    const Screen sc = screen_of(f);
    unsigned sparks_left = core::hull::max_sparks;
    for (unsigned i = 0; i < core::ring_capacity; ++i) {
        core::Event& e = ring.slots[i];
        if (!e.sequence || e.role != core::role_hull_hit || !(e.flags & core::flag_stage)) continue;
        HullSlot& hs = state.slots[i];
        if (hs.sequence != e.sequence) {
            hs = HullSlot{};
            hs.sequence = e.sequence;
        }
        Seen* s = nullptr;
        if (!hs.orphan) {
            for (unsigned k = 0; k < seen_count && !s; ++k)
                if (seen[k].owner == e.owner) s = &seen[k];
            if (!s) continue; // its owner past max_owners this frame (counted once per owner above)
        }
        const bool live = s && s->status == OwnerStatus::ok;
        if (!live) {
            if (!hs.latched) { // never drawn: nothing left the owner yet
                core::ring_release(ring, i);
                hs = HullSlot{};
                ++n.retired;
                continue;
            }
            hs.orphan = 1;
        }
        if (!hs.latched) {
            hit_world(s->data, e.pos, hs.p0);
            for (unsigned k = 0; k < 3; ++k) hs.v[k] = s->vel[k];
            hs.first_ms = f.now;
            hs.floor_ms = frame_floor_ms; // review 1: the floor of its first drawn frame, kept for its whole life
            hs.latched = 1;
        }
        const float floor_ms = hs.floor_ms;
        if (floor_ms > n.max_floor_ms) n.max_floor_ms = floor_ms;
        const std::uint32_t age_ms = f.now - hs.first_ms;
        if (age_ms > 60000u) continue; // a clock reset: Present releases it
        ++n.hits;
        if (hs.orphan) ++n.orphans;
        const float age = float(age_ms);
        const float k = (e.detail & core::detail_tug) && !(e.flags & core::flag_calibre_override) ? 0.1f : hull_calibre(e);
        const float U = hull_unit(e, t), r_hit = core::hull::r_scale * U * (core::hull::r_base + core::hull::r_k * k);
        const bool repair = (e.detail & core::detail_repair) != 0, tug = (e.detail & core::detail_tug) != 0;
        const bool asteroid = (e.detail & core::detail_asteroid) != 0, missile = e.table == core::table_missiles;
        const float gain = (f.cockpit && f.own_node && e.node == f.own_node) ? t.own_gain : 1.f;
        const bool own = f.own_node && e.node == f.own_node;
        float tint[3];
        core::event_tint(e, 0.f, tint);
        // The current contact point (view) from the owner's rows: flash, glow and ember ride it.
        float c_view[3] = {0.f, 0.f, 0.f};
        if (!hs.orphan) {
            double w[3];
            hit_world(s->data, e.pos, w);
            to_view(f.cam, w, c_view);
        }
        bool floored = false;
        // Flash (not repair, tug, asteroid).
        if (t.flash && !hs.orphan && !repair && !tug && !asteroid) {
            const float life = element_life_ms(float(core::hull::flash_ms), element_hash(e.sequence, 0), t.stagger, floor_ms,
                                               &floored);
            if (age < life) {
                const float dist = scalar::sqrt(dot3(c_view, c_view));
                const float b = dist > 1e-6f ? 1.f - core::hull::bias * r_hit / dist : 1.f;
                const float c[3] = {c_view[0] * b, c_view[1] * b, c_view[2] * b};
                if (c[2] > f.near_z && nq < sprite_cap) {
                    const Px p = to_px(f, sc, c);
                    const float r_px = r_hit * px_x * p.iz;
                    float core_px = core::hull::flash_core * r_px;
                    if (core_px < 0.5f * core::look::floor_flash_px) core_px = 0.5f * core::look::floor_flash_px;
                    const float halo_px = r_px > core_px / core::hull::flash_core ? r_px : core_px / core::hull::flash_core;
                    const float r_view = halo_px * c[2] / px_x;
                    float halo[3];
                    if (missile)
                        for (unsigned q = 0; q < 3; ++q) halo[q] = tint[q];
                    else
                        core::heat_colour(core::hull::flash_halo_t, halo);
                    const std::uint32_t colour = pack_colour(halo, core_px / halo_px);
                    const float radiance = core::look::core_e * (1.f - age / life) * gain;
                    renderer::WeaponSpriteVertex* v = sprites + 4 * nq;
                    const float sx[4] = {-1.f, 1.f, -1.f, 1.f}, sy[4] = {1.f, 1.f, -1.f, -1.f};
                    for (unsigned q = 0; q < 4; ++q) {
                        px_clip(f, sc, p.x + sx[q] * halo_px, p.y - sy[q] * halo_px, c[2], v[q].a);
                        v[q].b[0] = sx[q];
                        v[q].b[1] = sy[q];
                        v[q].b[2] = radiance;
                        v[q].b[3] = r_view;
                        v[q].c[0] = v[q].c[1] = v[q].c[2] = 0.f;
                        v[q].colour = colour;
                    }
                    ++nq;
                    ++n.flashes;
                    n.floored += floored;
                } else if (c[2] > f.near_z)
                    ++n.capped;
            }
        }
        // Sparks.
        const unsigned count = t.sparks ? spark_count(e, k, t) : 0;
        if (count) {
            const float t_now = age * 1e-3f, t_prev = t_now > step_s ? t_now - step_s : 0.f;
            const float f_now = spark_reach_factor(t_now), f_prev = spark_reach_factor(t_prev);
            float rot[9], m_now[9], m_prev[9], v_now[3], v_prev[3], p_now[3], p_prev[3];
            record_rotation(e.sequence, rot);
            view_of_rotation(f.cam, rot, m_now);
            view_of_rotation(prev, rot, m_prev);
            rotate_world_to_view(f.cam, hs.v, v_now);
            rotate_world_to_view(prev, hs.v, v_prev);
            to_view(f.cam, hs.p0, p_now);
            to_view(prev, hs.p0, p_prev);
            const float speed0 = core::hull::spark_speed_u * U;
            const float life_law = float(core::hull::spark_life_ms) + float(core::hull::spark_life_k_ms) * k;
            const float width_view = core::hull::spark_width_u * U;
            const unsigned pairs = (count + 1) / 2;
            // The Fibonacci hemisphere: z = 1 - (j + 1/2) / pairs, the azimuth stepped by the golden angle.
            float ca = 1.f, sa = 0.f;
            constexpr float cg = -0.73736887f, sg = 0.67549029f;
            for (unsigned j = 0; j < pairs; ++j) {
                const float z = 1.f - (float(j) + 0.5f) / float(pairs), r = scalar::sqrt(1.f - z * z);
                const float fib[3] = {r * ca, r * sa, z};
                const float nca = ca * cg - sa * sg;
                sa = ca * sg + sa * cg;
                ca = nca;
                const std::uint32_t h = element_hash(e.sequence, 16u + j);
                const float speed = speed0 * (1.f + core::hull::spark_speed_spread * (2.f * unit_of(h) - 1.f));
                bool fl = false;
                const float life = element_life_ms(life_law, mix32(h), t.stagger, floor_ms, &fl);
                if (!(age < life)) continue;
                const float T = 1.f - age / life;
                float colour[3];
                float e_raw;
                if (asteroid) {
                    core::heat_colour(core::hull::asteroid_t * T + 0.05f, colour);
                    e_raw = core::look::body_max * T;
                } else {
                    core::heat_colour(T, colour);
                    e_raw = core::hull::spark_e * T;
                }
                const std::uint32_t packed = pack_colour(colour, 0.f);
                const float hold = e_raw < core::look::streak_core_min ? e_raw : core::look::streak_core_min;
                // The pair's direction in both views (the side flips its sign).
                float dn[3], dp[3];
                for (unsigned q = 0; q < 3; ++q) {
                    dn[q] = (m_now[q * 3] * fib[0] + m_now[q * 3 + 1] * fib[1] + m_now[q * 3 + 2] * fib[2]) * speed;
                    dp[q] = (m_prev[q * 3] * fib[0] + m_prev[q * 3 + 1] * fib[1] + m_prev[q * 3 + 2] * fib[2]) * speed;
                }
                for (unsigned side = 0; side < 2 && side < count - 2 * j; ++side) {
                    const float sgn = side ? -1.f : 1.f;
                    float head[3], tail[3];
                    for (unsigned q = 0; q < 3; ++q) {
                        head[q] = p_now[q] + (dn[q] * sgn + v_now[q]) * f_now;
                        tail[q] = p_prev[q] + (dp[q] * sgn + v_prev[q]) * f_prev;
                    }
                    if (!(head[2] > f.near_z) || !(tail[2] > f.near_z)) continue;
                    if (!sparks_left || nq >= sprite_cap) {
                        ++n.capped;
                        continue;
                    }
                    if (!(f_now - f_prev > 1e-6f)) // the first frame: no motion yet; the direction is the spark's own
                        for (unsigned q = 0; q < 3; ++q) tail[q] = head[q] - dn[q] * sgn * 1e-3f;
                    streak_vertices(head, tail, e_raw * gain, hold * gain, width_view, packed, sprites + 4 * nq);
                    ++nq;
                    ++n.sparks;
                    --sparks_left;
                    n.floored += fl;
                }
            }
        }
        // Glow and ember (the owner live).
        if (!hs.orphan) {
            const unsigned elements = (t.glow ? 1u : 0u) | (t.ember && !tug ? 2u : 0u);
            for (unsigned el = 0; el < 2; ++el) {
                if (!(elements & (1u << el))) continue;
                const bool is_glow = el == 0;
                bool fl = false;
                const float life = element_life_ms(float(is_glow ? core::hull::glow_ms : core::hull::ember_ms),
                                                   element_hash(e.sequence, 1u + el), t.stagger, floor_ms, &fl);
                if (!(age < life)) continue;
                const float fade = 1.f - age / life;
                const float R = (is_glow ? core::hull::glow_r : core::hull::ember_r) * r_hit;
                float colour[3];
                if (repair || tug) {
                    const float e0 = is_glow ? core::hull::glow_e : core::hull::ember_e;
                    for (unsigned q = 0; q < 3; ++q) colour[q] = (tug ? core::hull::tug_colour[q] : tint[q]) * e0 * fade;
                } else if (is_glow) {
                    core::heat_colour(core::hull::glow_t, colour);
                    for (unsigned q = 0; q < 3; ++q) colour[q] *= core::hull::glow_e * core::hull::glow_t * fade;
                } else {
                    const float T = core::hull::ember_t * fade;
                    core::heat_colour(T, colour);
                    for (unsigned q = 0; q < 3; ++q) colour[q] *= core::hull::ember_e * T;
                }
                for (unsigned q = 0; q < 3; ++q) colour[q] *= gain;
                if (!(c_view[2] + R > f.near_z)) continue;
                float rect[4];
                bool full = false;
                if (!sphere_rect(f, c_view, R, rect, &full)) continue; // off screen
                if (ng >= glow_cap) {
                    ++n.capped;
                    continue;
                }
                glow_quad(f, c_view, R, rect, full, colour, glows + 4 * ng);
                glow_age[ng] = age_ms;
                glow_own[ng] = own;
                glow_area[ng] = rect_screens(rect) * core::look::budget_weight_glow;
                ++ng;
                if (is_glow)
                    ++n.glows;
                else
                    ++n.embers;
                n.floored += fl;
            }
        }
    }
    // The shared budget: past the screens the patches left, the glows of owners other than the own ship go, oldest
    // first (selection by age; O(boxes x shed), only while over budget).
    float area = 0.f;
    for (unsigned g = 0; g < ng; ++g) area += glow_area[g];
    while (area > f.budget_left && ng) {
        unsigned old = ng;
        for (unsigned g = 0; g < ng; ++g)
            if (!glow_own[g] && (old == ng || glow_age[g] > glow_age[old])) old = g;
        if (old == ng) break; // only the own ship's: never shed
        area -= glow_area[old];
        --ng;
        if (old != ng) {
            for (unsigned c = 0; c < 4; ++c) glows[4 * old + c] = glows[4 * ng + c];
            glow_age[old] = glow_age[ng];
            glow_own[old] = glow_own[ng];
            glow_area[old] = glow_area[ng];
        }
        ++n.shed;
    }
    n.screens = area;
    *sprite_quads = nq;
    *glow_boxes = ng;
    return n;
}
} // namespace x3m::weapon_effects::stage
