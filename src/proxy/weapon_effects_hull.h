#pragma once
#include "weapon_effects_stage.h"
#include "weapon_effects_bubble.h"
#include <xmmintrin.h>

// The hull hit's scene-end step (docs/architecture/weapon-look.md 2.4-2.11 and 3.1, step 2): the ring's stage-owned hull
// hits (role_hull_hit with flag_stage, shields,hull mode) become the frame's flash and spark quads (the sprite batch,
// renderer::WeaponSpriteVertex) and glow and ember boxes (the glow primitive, renderer::WeaponGlowVertex).
//
// Per record, at the contact point Event::pos in its owner's frame, sized in the owner's cell unit U = cell_m[tier]
// and the impact's calibre k (calibre_of: the running damage total, or the override's): r_hit = r_scale U (r_base + r_k k);
// - flash: a white disc core of flash_core r_hit (>= floor_flash_px) in a halo of flash_halo_r r_hit at heat
//   flash_halo_t (a missile source: its tint), radiance core_e, fading linearly over flash_ms; its centre biased towards
//   the eye by bias r_hit: a short core inside the burst;
// - burst (2026-11-06, every hit but the repair, tug and asteroid variants): the fireball impostor
//   (renderer::WeaponFireballVertex, weapon_fireball_ps.hlsl) at the contact point, R(t) = burst_r r_hit (1 - e^(-t /
//   (burst_rise L))), L = burst_ms + burst_k_ms k, heat 1 -> 0 over L, body burst_e; the lane clips its chord;
// - ring (heavy hits, hull_heavy): a shock ring in the hull's tangent plane (the bubble's normal at the hit for a ship,
//   the face axis for a station, latched at the record's first frame), radius 0 -> ring_r r_hit over ring_rise L;
// - sparks: per hit round(n (1 +- spark_count_spread)) about n = spark_n0 + spark_nk k (x missile_spark_x for a missile
//   source, x asteroid_dust_x for an asteroid), in antipodal pairs of hashed isotropic directions (the lane hides the
//   half that flies into the hull), each spark's speed spark_speed_u U/s, life, rest length and width x its own hashed
//   factors (spark_factors), a quarter curling by a constant sideways drift, plus the owner's velocity at the record's
//   first stage frame, closed form p(t) = p0 + v0 tau (1 - e^(-t / tau)) + w t in world space from the contact point
//   at that frame (they leave the owner and outlive it); each drawn from its previous frame's screen position (p(t -
//   step) through the previous frame's view) to its current one, never shorter than its rest length, capped at
//   streak_cap rest lengths, its radiance x rest / (rest + streak) but not below streak_core_min; cooling on the heat
//   ramp over its life ((spark_life_ms + spark_life_k_ms k) x its factor);
// - glow: the glow primitive at the contact point, R = glow_r r_hit, heat glow_t x glow_e, linear over glow_ms;
// - ember: R = ember_r r_hit, heat ember_t cooling to 0 over ember_ms (radiance ramp(T) x ember_e x T).
// Variants (Event::detail): repair (the override): glow and ember only, the weapon tint; tug: the glow only, pale blue;
// asteroid: no flash, the sparks x 2 on the ramp's last stops. Every life is game time from the record's first stage frame
// (age 0 on its first drawn frame) and never shorter than min_frames x its first frame's game step (the three-frame floor),
// each element's life x (1 +- stagger / 2) by a hash of its record and index. The flash, glow and ember ride the owner
// (re-posed every frame through its rows, validated as the shield stage validates); a record whose owner went after
// its first frame keeps its sparks only (orphan), one whose owner never validated leaves the ring (retired). The own
// ship in the cockpit view: every element of a hit on it at own_cockpit_gain (look 2.9).
// The shared budget (look 2.11): each glow is charged its screen rectangle x budget_weight_glow, each burst x
// budget_weight_fireball; past the screens the patches left, the bursts drop to one octave, then the glows and embers
// of owners other than the own ship go, oldest first (shed), then those owners' bursts. Sparks, flashes and rings are
// never shed (count caps: hull::max_sparks spark quads; renderer::weapon_sprite_quads, weapon_glow_boxes,
// weapon_fireball_boxes, weapon_ring_quads).
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
// The pair directions a record keeps from its first drawn frame (the performance pass: the rejection draw once, not
// every frame); a record with more pairs draws the rest every frame.
constexpr unsigned hull_cached_pairs = 36;
struct HullSlot {
    std::uint32_t sequence = 0, first_ms = 0;
    float pair_dir[hull_cached_pairs][3]; // world, unit (pair_direction), valid below `pairs`
    std::uint8_t pairs = 0;
    double p0[3]{};   // the contact point in world (view units) at the record's first stage frame
    float v[3]{};     // the owner's velocity then (world, view units per game second)
    float floor_ms = 0.f; // the three-frame floor latched at the first drawn frame (min_frames x that frame's step)
    float normal[3]{};    // a heavy hit's ring normal in the owner frame (latched at its first drawn frame)
    std::uint8_t latched = 0, orphan = 0, has_normal = 0, normal_fallback = 0;
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
    bool flash = true, sparks = true, glow = core::hull::glow, ember = true, burst = true, ring = true;
    float cell_m[core::patch_tier_count] = {core::cell_m[0], core::cell_m[1], core::cell_m[2]};
    float stagger = core::stagger;
    float own_gain = core::own_cockpit_gain;
    unsigned min_frames = core::look::min_frames;
    float spark_count_x = core::hull::spark_count_x; // the kill criterion's halving (a header decision, never a setting)
    std::uintptr_t body_cache_root = core::layout::body_cache_root; // the ring normal's hull walk (the fixture's own)
};
struct HullCounts {
    unsigned hits = 0, flashes = 0, sparks = 0, glows = 0, embers = 0, bursts = 0, rings = 0;
    unsigned orphans = 0, retired = 0, shed = 0, capped = 0, floored = 0, owners_dropped = 0;
    unsigned bursts_shed = 0, ring_walks = 0, ring_fallback = 0;
    unsigned octaves = core::look::fireball_octaves; // the bursts' noise octaves this frame (1: the budget's LOD)
    float screens = 0.f; // the glows' and bursts' charged screens after shedding
    float burst_screens = 0.f; // of them the bursts'
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
// The spark count of a record (before the frame's cap): the law n = (spark_n0 + spark_nk k) x the variant's factors x
// (1 + spark_count_spread (2 u - 1)), u hashed from the sequence (element 7), rounded; at least one pair.
inline unsigned spark_count(const core::Event& e, float k, const HullTuning& t) noexcept {
    if (t.mode == core::hull::simple || (e.detail & (core::detail_repair | core::detail_tug))) return 0;
    float n = (float(core::hull::spark_n0) + float(core::hull::spark_nk) * k) * t.spark_count_x;
    if (e.table == core::table_missiles) n *= core::hull::missile_spark_x;
    if (e.detail & core::detail_asteroid) n *= core::hull::asteroid_dust_x;
    if (!(n > 0.f)) return 0;
    n *= 1.f + core::hull::spark_count_spread * (2.f * unit_of(element_hash(e.sequence, 7u)) - 1.f);
    const unsigned c = unsigned(n + 0.5f);
    return c < 2u ? 2u : c;
}
// A spark's own factors (a hash of the record's sequence and the spark's index s, element 64 + s, and one mix of it
// whose four bytes give the four factors at 1/256 steps: two hashes a spark, the performance pass): speed, life, rest
// length and width against their laws, and whether it curls (the element hash's top bits).
struct SparkFactors {
    float speed_x, life_x, len_x, width_x;
    bool curl;
    std::uint32_t curl_hash;
};
inline float byte_unit(std::uint32_t h, unsigned shift) noexcept {
    return (float((h >> shift) & 0xffu) + 0.5f) * (1.f / 256.f);
}
inline float lerp_byte(float lo, float hi, std::uint32_t h, unsigned shift) noexcept {
    return lo + (hi - lo) * byte_unit(h, shift);
}
inline SparkFactors spark_factors(std::uint32_t sequence, unsigned s) noexcept {
    namespace hl = core::hull;
    const std::uint32_t h0 = element_hash(sequence, 64u + s), h1 = mix32(h0 + 0x9e3779b9u);
    SparkFactors f;
    f.speed_x = 1.f + hl::spark_speed_spread * (2.f * byte_unit(h1, 0) - 1.f);
    f.life_x = lerp_byte(hl::spark_life_lo, hl::spark_life_hi, h1, 8);
    f.len_x = lerp_byte(hl::spark_len_lo, hl::spark_len_hi, h1, 16);
    f.width_x = lerp_byte(hl::spark_width_lo, hl::spark_width_hi, h1, 24);
    f.curl = unit_of(h0) < hl::spark_curl_share;
    f.curl_hash = f.curl ? mix32(h0 ^ 0x5bd1e995u) : 0u; // only a curled spark draws its drift
    return f;
}
// A point of the unit cube [-1, 1]^3 from one hash: three 10-bit fields at their cells' centres.
inline void cube_point(std::uint32_t h, float out[3]) noexcept {
    for (unsigned k = 0; k < 3; ++k) out[k] = (float((h >> (10 * k)) & 0x3ffu) + 0.5f) * (2.f / 1024.f) - 1.f;
}
// Pair j's launch direction (world, unit): an isotropic direction by rejection from the unit cube (eight draws, the
// last accepted whatever its length: a bias below 1e-2 of the time), hashed from the sequence and j (elements 4096 +
// 8 j ...); the pair's second spark flies the opposite way (the lane hides one of each pair on a flat hull).
inline void pair_direction(std::uint32_t sequence, unsigned j, float d[3]) noexcept {
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        float q[3];
        cube_point(element_hash(sequence, 4096u + 8u * j + attempt), q);
        const float r2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
        if (r2 > 1e-4f && (r2 <= 1.f || attempt == 7)) {
            const float inv = rsqrt_nr(r2);
            for (unsigned k = 0; k < 3; ++k) d[k] = q[k] * inv;
            return;
        }
    }
    d[0] = d[1] = 0.f;
    d[2] = 1.f;
}
// A curled spark's drift direction x spark_curl_x (world): a hashed direction less its part along the launch direction
// d, normalised (any perpendicular when the draw is degenerate).
inline void curl_drift(std::uint32_t h, const float d[3], float out[3]) noexcept {
    float q[3];
    cube_point(h, q);
    float dq = q[0] * d[0] + q[1] * d[1] + q[2] * d[2];
    for (unsigned k = 0; k < 3; ++k) q[k] -= dq * d[k];
    float r2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
    if (!(r2 > 1e-6f)) {
        const float a[3] = {d[1] * d[1] + d[2] * d[2] > 0.5f ? 1.f : 0.f, 0.f, d[1] * d[1] + d[2] * d[2] > 0.5f ? 0.f : 1.f};
        dq = a[0] * d[0] + a[2] * d[2];
        for (unsigned k = 0; k < 3; ++k) q[k] = a[k] - dq * d[k];
        r2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
    }
    const float s = core::hull::spark_curl_x * rsqrt_nr(r2);
    for (unsigned k = 0; k < 3; ++k) out[k] = q[k] * s;
}
// The heavy families (the ring): a missile source, the burst style bit (flak and area shots), a calibre of heavy_k or
// more; never the repair, tug or asteroid variants.
inline bool hull_heavy(const core::Event& e, float k) noexcept {
    if (e.detail & (core::detail_repair | core::detail_tug | core::detail_asteroid)) return false;
    return e.table == core::table_missiles || (e.style & core::style_burst) || k >= core::hull::heavy_k;
}
// The burst's life law (game ms, before the stagger and the floor): burst_ms + burst_k_ms k.
inline float burst_law_ms(float k) noexcept {
    return float(core::hull::burst_ms) + float(core::hull::burst_k_ms) * k;
}
// The burst's radius at age t (game ms) of a life L: burst_r r_hit (1 - e^(-t / (burst_rise L))).
inline float burst_radius(float r_hit, float age_ms, float life_ms) noexcept {
    return core::hull::burst_r * r_hit * (1.f - core::exp_neg(-age_ms / (core::hull::burst_rise * life_ms)));
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
inline void rotate_world_to_view(const Camera& c, const float w[3], float out[3]) noexcept {
    for (unsigned j = 0; j < 3; ++j) out[j] = w[0] * c.r[j] + w[1] * c.r[3 + j] + w[2] * c.r[6 + j];
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
// The heat ramp packed as a streak's D3DCOLOR (alpha 0) at heat_lut_steps + 1 temperatures (built at compile time from
// core::look::heat_ramp, as pack_colour packs it: the performance pass, one lookup a spark instead of the ramp and
// the pack; the nearest step's colour is within a third of an 8-bit code of the exact one's).
constexpr unsigned heat_lut_steps = 1024;
struct HeatLut {
    std::uint32_t v[heat_lut_steps + 1];
};
constexpr HeatLut make_heat_lut() noexcept {
    HeatLut l{};
    for (unsigned n = 0; n <= heat_lut_steps; ++n) {
        const float x = float(n) * (4.f / float(heat_lut_steps));
        unsigned i = unsigned(x);
        if (i > 3) i = 3;
        const float f = x - float(i);
        std::uint32_t w = 0;
        for (unsigned c = 0; c < 3; ++c) {
            float v = core::look::heat_ramp[i][c] + (core::look::heat_ramp[i + 1][c] - core::look::heat_ramp[i][c]) * f;
            v = v > 0.f ? (v < 1.f ? v : 1.f) : 0.f;
            w |= std::uint32_t(v * 255.f + 0.5f) << (16 - 8 * c);
        }
        l.v[n] = w;
    }
    return l;
}
constexpr HeatLut heat_lut = make_heat_lut();
inline std::uint32_t heat_packed(float t) noexcept {
    t = t > 0.f ? (t < 1.f ? t : 1.f) : 0.f;
    return heat_lut.v[int(t * float(heat_lut_steps) + 0.5f)];
}
// One streak's four vertices (the vertex program expands them, weapon_sprite_vs.hlsl): its head (view, this frame),
// its tail (view, the previous frame's position through the previous view), its radiance and the hold a diluted
// streak keeps, its width (view units), its colour and its rest-length factor (c[0] = -+ len_x); corners tail-left,
// tail-right, head-left, head-right.
inline void streak_vertices(const float head[3], const float tail[3], float radiance, float hold, float width_view,
                            std::uint32_t colour, renderer::WeaponSpriteVertex* out, float len_x = 1.f) noexcept {
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
        v.c[0] = c >= 2 ? len_x : -len_x;
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
    const float rest_w = core::hull::spark_rest_w * o.lit, len_x = v[0].c[0] < 0.f ? -v[0].c[0] : v[0].c[0];
    o.rest = (rest_w > core::look::floor_len_px ? rest_w : core::look::floor_len_px) * len_x;
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
// A fireball's four corners (the glow's rectangle, weapon_fireball_vs.hlsl): at view depth z_q (the sphere's front, or
// 1.5 x the near plane for the whole screen), each corner carrying the sphere and its look (T, E, advection, seed).
inline void fireball_quad(const HullView& f, const float c[3], float R, const float rect[4], bool full, const float look[4],
                          renderer::WeaponFireballVertex* out) noexcept {
    const float zq = full ? 1.5f * f.near_z : c[2] - R;
    const float kx = zq / f.cam.m00, ky = zq / f.cam.m11;
    for (unsigned k = 0; k < 4; ++k) {
        const float nx = rect[k & 1u ? 2 : 0], ny = rect[k & 2u ? 3 : 1];
        out[k].corner[0] = (nx - f.cam.m20) * kx;
        out[k].corner[1] = (ny - f.cam.m21) * ky;
        out[k].corner[2] = zq;
        for (unsigned i = 0; i < 3; ++i) out[k].sphere[i] = c[i];
        out[k].sphere[3] = R;
        for (unsigned i = 0; i < 4; ++i) out[k].look[i] = look[i];
    }
}
// A ring's plane square (centre c, unit tangents t1, t2, radius rho; view space) as a sprite quad of the ring kind:
// corners (u, v) = (-1, -1), (1, -1), (-1, 1), (1, 1) in clip space (perspective-correct u, v), b = (u, v, radiance,
// band), c = (2, soft, 0), the colour's alpha 255. False when every corner lies behind the near plane.
inline bool ring_quad(const HullView& f, const float c[3], const float t1[3], const float t2[3], float rho, float radiance,
                      float band, float soft, std::uint32_t colour, renderer::WeaponSpriteVertex* out) noexcept {
    static constexpr float us[4] = {-1.f, 1.f, -1.f, 1.f}, vs[4] = {-1.f, -1.f, 1.f, 1.f};
    bool any = false;
    for (unsigned k = 0; k < 4; ++k) {
        float P[3];
        for (unsigned i = 0; i < 3; ++i) P[i] = c[i] + rho * (us[k] * t1[i] + vs[k] * t2[i]);
        any = any || P[2] > f.near_z;
        renderer::WeaponSpriteVertex& v = out[k];
        v.a[0] = f.cam.m00 * P[0] + f.cam.m20 * P[2];
        v.a[1] = f.cam.m11 * P[1] + f.cam.m21 * P[2];
        v.a[2] = P[2] - f.near_z;
        v.a[3] = P[2];
        v.b[0] = us[k];
        v.b[1] = vs[k];
        v.b[2] = radiance;
        v.b[3] = band;
        v.c[0] = 2.f;
        v.c[1] = soft;
        v.c[2] = 0.f;
        v.colour = colour | 0xff000000u;
    }
    return any;
}
// The ring's normal in the owner frame (look 3.1, heavy hits): a ship's bubble (the cache's hull walk, the radius sphere
// without one) at the hit: the ellipsoid's normal at the hit's bubble point (bubble_point's Q / e, normalised); a
// station (TierSource tier_by_station, the face lattices): the owner axis of the hit's largest component, signed.
inline void ring_normal_local(const core::Event& e, const float p[3], const Bubble& b, float n[3]) noexcept {
    if (e.tier_source == core::tier_by_station) {
        unsigned a = 0;
        for (unsigned k = 1; k < 3; ++k)
            if ((p[k] < 0.f ? -p[k] : p[k]) > (p[a] < 0.f ? -p[a] : p[a])) a = k;
        n[0] = n[1] = n[2] = 0.f;
        n[a] = p[a] < 0.f ? -1.f : 1.f;
        return;
    }
    float B[3], Q[3];
    bubble_point(b, p, B, Q);
    for (unsigned k = 0; k < 3; ++k) n[k] = Q[k] / b.e[k];
    if (!normalize3(n)) {
        n[0] = n[1] = 0.f;
        n[2] = 1.f;
    }
}
// Builds the frame's hull-hit sprites and glows (see the header). Returns the counts; *sprite_quads and *glow_boxes the
// outputs written (four vertices each: a sprite quad, a glow's screen rectangle).
// The bursts (fireball impostors) and heavy rings (sprite quads of the ring kind) go to their own arrays when given
// (nullptr: none built); bubbles is the shield stage's cache (the rings' normals; nullptr walks without caching).
inline HullCounts build_hull(core::Ring& ring, HullState& state, Reader rd, const HullView& f, const HullTuning& t,
                             renderer::WeaponSpriteVertex* sprites, unsigned sprite_cap, renderer::WeaponGlowVertex* glows,
                             unsigned glow_cap, unsigned* sprite_quads, unsigned* glow_boxes,
                             renderer::WeaponFireballVertex* fireballs = nullptr, unsigned fireball_cap = 0,
                             unsigned* fireball_boxes = nullptr, renderer::WeaponSpriteVertex* rings = nullptr,
                             unsigned ring_cap = 0, unsigned* ring_quads = nullptr, BubbleCache* bubbles = nullptr) noexcept {
    HullCounts n{};
    unsigned nq = 0, ng = 0, nb = 0, nr = 0;
    *sprite_quads = *glow_boxes = 0;
    if (fireball_boxes) *fireball_boxes = 0;
    if (ring_quads) *ring_quads = 0;
    if (!fireballs || !fireball_boxes) fireball_cap = 0;
    if (!rings || !ring_quads) ring_cap = 0;
    unsigned walks_left = core::hull::ring_walks_per_frame;
    BubbleStats bubble_stats{};
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
        float centre[3], frame[9]; // its frame in view (owner_frame), once per frame when a ring needs it
        std::uint8_t frame_state;  // 0 not taken, 1 valid, 2 degenerate
    };
    Seen seen[core::hull::max_owners];
    const unsigned seen_count = keep;
    for (unsigned k = 0; k < keep; ++k) { // read the kept owners, track them, take their velocity
        Seen& sn = seen[k];
        sn.owner = cand[k].owner;
        sn.status = read_owner(rd, ring.slots[cand[k].index], &sn.data);
        sn.vel[0] = sn.vel[1] = sn.vel[2] = 0.f;
        sn.frame_state = 0;
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
    static_assert(renderer::weapon_fireball_boxes <= 0xffff, "fireball indices");
    std::uint32_t burst_age[renderer::weapon_fireball_boxes];
    float burst_area[renderer::weapon_fireball_boxes];
    std::uint8_t burst_own[renderer::weapon_fireball_boxes];
    if (fireball_cap > renderer::weapon_fireball_boxes) fireball_cap = renderer::weapon_fireball_boxes;
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
                    constexpr float halo_over_core = core::hull::flash_halo_r / core::hull::flash_core;
                    const float halo_law = core::hull::flash_halo_r * r_px;
                    const float halo_px = halo_law > core_px * halo_over_core ? halo_law : core_px * halo_over_core;
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
        // The hot-gas burst (the owner live; not the repair, tug or asteroid variants): the fireball impostor at the
        // contact point, R(t) = burst_r r_hit (1 - e^(-t / (burst_rise L))), heat 1 -> 0 over L, the lane clipping it.
        const bool burst_kind = !repair && !tug && !asteroid;
        bool burst_life_known = false;
        float burst_life = 0.f;
        if (burst_kind && (fireball_cap || ring_cap)) {
            bool fl = false;
            burst_life = element_life_ms(burst_law_ms(k), element_hash(e.sequence, 3), t.stagger, floor_ms, &fl);
            burst_life_known = true;
            if (t.burst && fireball_cap && !hs.orphan && age < burst_life) {
                const float R = burst_radius(r_hit, age, burst_life);
                float rect[4];
                bool full = false;
                if (R > 0.f && c_view[2] + R > f.near_z && sphere_rect(f, c_view, R, rect, &full)) {
                    if (nb < fireball_cap) {
                        const float T = 1.f - age / burst_life;
                        const float look[4] = {T, core::hull::burst_e * gain, core::look::fireball_advect * age / burst_life,
                                               64.f * unit_of(element_hash(e.sequence, 4))};
                        fireball_quad(f, c_view, R, rect, full, look, fireballs + 4 * nb);
                        burst_age[nb] = age_ms;
                        burst_own[nb] = own;
                        burst_area[nb] = rect_screens(rect) * core::look::budget_weight_fireball;
                        ++nb;
                        ++n.bursts;
                        n.floored += fl;
                    } else
                        ++n.capped;
                }
            }
        }
        // The heavy ring: in the hull's tangent plane at the hit, lifted ring_lift r_hit along the normal, radius 0 ->
        // ring_r r_hit over ring_rise x the burst's life, heat ring_t fading linearly.
        if (t.ring && ring_cap && burst_life_known && !hs.orphan && hull_heavy(e, k)) {
            if (!hs.has_normal) { // latched at the record's first drawn frame: a stable plane for its life
                float local[3];
                hit_local(e.pos, local);
                bool fallback = true;
                Bubble b = fallback_bubble(s->data.radius);
                if (e.tier_source != core::tier_by_station) {
                    const unsigned before = bubble_stats.walks;
                    b = owner_bubble(bubbles, rd, t.body_cache_root, e.owner, e.node, e.handle, s->data.radius, f.now,
                                     &walks_left, &bubble_stats, &fallback);
                    n.ring_walks += bubble_stats.walks - before;
                } else
                    fallback = false;
                ring_normal_local(e, local, b, hs.normal);
                hs.has_normal = 1;
                hs.normal_fallback = fallback;
            }
            n.ring_fallback += hs.normal_fallback;
            const float law = core::hull::ring_rise * burst_life;
            const bool fl = floor_ms > law;
            const float life = fl ? floor_ms : law;
            if (age < life && !s->frame_state) s->frame_state = owner_frame(s->data, f.cam, s->centre, s->frame) ? 1 : 2;
            const float* frame = s->frame;
            if (age < life && s->frame_state == 1) {
                float nv[3] = {0.f, 0.f, 0.f};
                for (unsigned q = 0; q < 3; ++q)
                    for (unsigned a = 0; a < 3; ++a) nv[a] += hs.normal[q] * frame[3 * q + a];
                const float rho = core::hull::ring_r * r_hit * age / life;
                if (normalize3(nv) && rho > 0.f) {
                    float rc[3], t1[3], t2[3];
                    for (unsigned q = 0; q < 3; ++q) rc[q] = c_view[q] + nv[q] * core::hull::ring_lift * r_hit;
                    const bool x_axis = (nv[0] < 0.f ? -nv[0] : nv[0]) < 0.6f;
                    const float ax[3] = {x_axis ? 1.f : 0.f, x_axis ? 0.f : 1.f, 0.f};
                    cross3(nv, ax, t1);
                    normalize3(t1);
                    cross3(nv, t1, t2);
                    const float dist = scalar::sqrt(dot3(rc, rc)), nd = dot3(nv, rc);
                    const float cosv = dist > 1e-6f ? (nd < 0.f ? -nd : nd) / dist : 1.f;
                    const float limb = cosv * core::hull::ring_limb_max > 1.f ? 1.f / cosv : core::hull::ring_limb_max;
                    const float rho_px = rc[2] > f.near_z ? rho * px_x / rc[2] : 1e30f;
                    float band = core::look::floor_lit_px / rho_px;
                    band = band > core::hull::ring_band ? (band < 1.f ? band : 1.f) : core::hull::ring_band;
                    float colour[3];
                    core::heat_colour(core::hull::ring_t, colour);
                    const float radiance = core::hull::ring_e * core::hull::ring_t * (1.f - age / life) * limb * gain;
                    if (nr < ring_cap) {
                        if (ring_quad(f, rc, t1, t2, rho, radiance, band, core::look::soft_core * r_hit,
                                      pack_colour(colour, 0.f), rings + 4 * nr)) {
                            ++nr;
                            ++n.rings;
                            n.floored += fl;
                        }
                    } else
                        ++n.capped;
                }
            }
        }
        // Sparks: per hit a hashed count, per pair a hashed isotropic direction (the second spark the opposite one), per
        // spark its speed, life, length, width and curl (spark_factors); closed form in game time from the contact
        // point at the record's first frame, p(t) = p0 + (d speed + v_owner) tau (1 - e^(-t / tau)) + w t.
        const unsigned count = t.sparks ? spark_count(e, k, t) : 0;
        if (count) {
            const float t_now = age * 1e-3f, t_prev = t_now > step_s ? t_now - step_s : 0.f;
            const float f_now = spark_reach_factor(t_now), f_prev = spark_reach_factor(t_prev);
            float v_now[3], v_prev[3], p_now[3], p_prev[3];
            rotate_world_to_view(f.cam, hs.v, v_now);
            rotate_world_to_view(prev, hs.v, v_prev);
            to_view(f.cam, hs.p0, p_now);
            to_view(prev, hs.p0, p_prev);
            const float speed0 = core::hull::spark_speed_u * U;
            const float life_law = float(core::hull::spark_life_ms) + float(core::hull::spark_life_k_ms) * k;
            const float width_view = core::hull::spark_width_u * U;
            const bool first = !(f_now - f_prev > 1e-6f); // the first frame: no motion yet; the direction is the spark's own
            if (!hs.pairs) { // the record's pair directions, once
                const unsigned want = (count + 1) / 2;
                const unsigned keep = want < hull_cached_pairs ? want : hull_cached_pairs;
                for (unsigned j = 0; j < keep; ++j) pair_direction(e.sequence, j, hs.pair_dir[j]);
                hs.pairs = std::uint8_t(keep);
            }
            float d[3] = {0.f, 0.f, 1.f}, d_now[3] = {0.f, 0.f, 1.f}, d_prev[3] = {0.f, 0.f, 1.f};
            unsigned d_pair = ~0u;
            for (unsigned sp = 0; sp < count; ++sp) {
                const SparkFactors sf = spark_factors(e.sequence, sp);
                const float law = life_law * sf.life_x;
                const bool fl = floor_ms > law;
                const float life = fl ? floor_ms : law;
                if (!(age < life)) continue;
                const unsigned pair = sp >> 1;
                if (pair != d_pair) { // the pair's direction in world and in both views, once
                    if (pair < hs.pairs)
                        for (unsigned q = 0; q < 3; ++q) d[q] = hs.pair_dir[pair][q];
                    else
                        pair_direction(e.sequence, pair, d);
                    rotate_world_to_view(f.cam, d, d_now);
                    rotate_world_to_view(prev, d, d_prev);
                    d_pair = pair;
                }
                const float sgn = (sp & 1u) ? -1.f : 1.f;
                const float speed = speed0 * sf.speed_x;
                const float T = 1.f - age / life;
                const float e_raw = (asteroid ? core::look::body_max : core::hull::spark_e) * T;
                const std::uint32_t packed = heat_packed(asteroid ? core::hull::asteroid_t * T + 0.05f : T);
                const float hold = e_raw < core::look::streak_core_min ? e_raw : core::look::streak_core_min;
                // The launch velocity in both views (the pair's direction x its signed speed) and a curled spark's drift.
                const float sv = speed * sgn;
                const float dn[3] = {d_now[0] * sv, d_now[1] * sv, d_now[2] * sv};
                const float dp[3] = {d_prev[0] * sv, d_prev[1] * sv, d_prev[2] * sv};
                float wn[3] = {0.f, 0.f, 0.f}, wp[3] = {0.f, 0.f, 0.f};
                if (sf.curl) {
                    const float ds[3] = {d[0] * sgn, d[1] * sgn, d[2] * sgn};
                    float ww[3];
                    curl_drift(sf.curl_hash, ds, ww);
                    for (unsigned q = 0; q < 3; ++q) ww[q] *= speed;
                    rotate_world_to_view(f.cam, ww, wn);
                    rotate_world_to_view(prev, ww, wp);
                }
                float head[3], tail[3];
                for (unsigned q = 0; q < 3; ++q) {
                    head[q] = p_now[q] + (dn[q] + v_now[q]) * f_now + wn[q] * t_now;
                    tail[q] = p_prev[q] + (dp[q] + v_prev[q]) * f_prev + wp[q] * t_prev;
                }
                if (!(head[2] > f.near_z) || !(tail[2] > f.near_z)) continue;
                if (!sparks_left || nq >= sprite_cap) {
                    ++n.capped;
                    continue;
                }
                if (first)
                    for (unsigned q = 0; q < 3; ++q) tail[q] = head[q] - (dn[q] + wn[q]) * 1e-3f;
                streak_vertices(head, tail, e_raw * gain, hold * gain, width_view * sf.width_x, packed, sprites + 4 * nq,
                                sf.len_x);
                ++nq;
                ++n.sparks;
                --sparks_left;
                n.floored += fl;
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
    // The shared budget (look 2.11): past the screens the patches left, (1) the bursts drop to one noise octave (the
    // fireball's LOD, budget_weight_fireball_lod), (2) the glows of owners other than the own ship go, oldest first, (3)
    // then those owners' bursts, oldest first (selections by age; O(boxes x shed), only while over budget). Sparks,
    // flashes, rings and anything of the own ship are never shed.
    float area = 0.f, burst_area_sum = 0.f;
    for (unsigned g = 0; g < ng; ++g) area += glow_area[g];
    for (unsigned g = 0; g < nb; ++g) burst_area_sum += burst_area[g];
    area += burst_area_sum;
    if (area > f.budget_left && nb) {
        constexpr float lod = core::look::budget_weight_fireball_lod / core::look::budget_weight_fireball;
        area -= burst_area_sum * (1.f - lod);
        burst_area_sum *= lod;
        for (unsigned g = 0; g < nb; ++g) burst_area[g] *= lod;
        n.octaves = 1;
    }
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
    while (area > f.budget_left && nb) {
        unsigned old = nb;
        for (unsigned g = 0; g < nb; ++g)
            if (!burst_own[g] && (old == nb || burst_age[g] > burst_age[old])) old = g;
        if (old == nb) break; // only the own ship's: never shed
        area -= burst_area[old];
        burst_area_sum -= burst_area[old];
        --nb;
        if (old != nb) {
            for (unsigned c = 0; c < 4; ++c) fireballs[4 * old + c] = fireballs[4 * nb + c];
            burst_age[old] = burst_age[nb];
            burst_own[old] = burst_own[nb];
            burst_area[old] = burst_area[nb];
        }
        ++n.bursts_shed;
    }
    n.screens = area;
    n.burst_screens = burst_area_sum;
    *sprite_quads = nq;
    *glow_boxes = ng;
    if (fireball_boxes) *fireball_boxes = nb;
    if (ring_quads) *ring_quads = nr;
    return n;
}
} // namespace x3m::weapon_effects::stage
