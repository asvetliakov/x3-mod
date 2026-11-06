#pragma once
#include "weapon_effects_stage.h"
#include "engine_shimmer_core.h" // engine_shimmer::WeaponRequest (the shimmer rects' hand-off)
#include <xmmintrin.h>

// The hull hit's scene-end step (docs/architecture/weapon-look.md 2.4-2.11 and 3.1, step 2; the per-class anatomy of
// docs/architecture/hull-impact-classes.md): the ring's stage-owned hull hits (role_hull_hit with flag_stage,
// shields,hull mode) become the frame's flash, streak and spark quads (the sprite batch, renderer::WeaponSpriteVertex),
// glow and ember boxes (the glow primitive, renderer::WeaponGlowVertex), puffs (the fireball impostor,
// renderer::WeaponFireballVertex), surface rings and filament glows (renderer::WeaponShapeVertex), multiply scorches
// (glow boxes drawn ZERO / INVSRCCOLOR) and heat-shimmer rects (ShimmerRect, the engine shimmer pass's list).
//
// Per record, at the contact point Event::pos in its owner's frame, sized in the owner's cell unit U = cell_m[tier]
// and the impact's calibre k (calibre_of: the running damage total, or the override's): r = r_hit = r_scale U (r_base +
// r_k k). Its anatomy (anatomy_of: the asteroid victim, then repair, tug, a missile source, the class nibble of
// Event::detail the hook wrote) selects one row of each core::hull::class_* table:
// - flash: a white disc core in a halo at the class's heat or colour, linear over its life, biased towards the eye by
//   bias r; the heavy class adds the entry streak along the shot (P - streak_back r a to P + streak_front r a; without
//   Event::dir the flash at streak_fallback_halo r instead);
// - puff: the fireball impostor riding the owner, R(t) = R_b (1 - e^(-t / (rise L))) (the plasma globe shrinking over
//   the last 0.7 L), heat T0 -> 0, the tint mixed in for the plasma globe; the lane clips its chord;
// - spark sets (fast, slow, chips): per hit round(n (1 +- spark_count_spread)) about n = n0 + nk k, in antipodal pairs
//   of hashed isotropic directions u, each launched along normalize(+-u + b a) (a the shot heading in world, b the set's
//   bias: b < 0 back towards the shooter, b > 0 through the hull; b 0 without Event::dir), each spark's speed, life,
//   rest length and width x its own hashed factors, a quarter curling, plus the owner's velocity at the first stage
//   frame, closed form p(t) = p0 + v0 tau (1 - e^(-t / tau)) + w t in world (they leave the owner and outlive it); drawn
//   from the previous frame's screen position to the current one (the streak rule); radiance E f, heat T0 f with f the
//   life fraction (the ion's in ion_colour); a set with a duty is lit in that share of flicker_ms slots;
// - glow and ember (the glow primitive at the hit), the surface ring (the sphere shell cut by the lane), the ion's
//   filament glow (noise in the owner's frame, re-hashed every rehash_ms) and the plasma / heavy shimmer rect;
// - the scorch: at a record's first frame a mark enters the per-owner scorch store (scorch_per_owner, scorch_store; it
//   outlives the record), drawn every frame as a multiply decal s0 min(1, t / L_ember) (1 - t / L_s) until L_s.
// Beams, repair and tug follow the hold rule (section 3.9): a new record of an owner and (table, row) within hold_r r of
// a live spot of theirs re-lights it (its newest tick) and leaves the ring; otherwise it becomes a new spot, flashing
// only when the owner held no spot of that (table, row). A spot is held while now - t_last <= max(hold_ms, 2 steps),
// then falls; the beam's spot draws a core, a boiling puff, spatter (closed form: two sparks per spatter_period_ms of
// its hold), a glow and a trail ember from its fall, and lays a scorch mark every trail_step_r r it crawls; repair
// draws a tinted glow and ember, tug a pale glow.
// Every life is game time from the record's first stage frame (age 0 on its first drawn frame) and never shorter than
// min_frames x its first frame's game step (the three-frame floor), each element's life x (1 +- stagger / 2) by a hash
// of its record and index. The owner-attached elements ride the owner (re-posed every frame through its rows, validated
// as the shield stage validates); a record whose owner went after its first frame keeps its sparks only (orphan), one
// whose owner never validated leaves the ring (retired); a spot or scorch whose owner went is freed. The own ship in
// the cockpit view: every element of a hit on it at own_cockpit_gain (look 2.9).
// The shared budget (look 2.11 as amended by hull-impact-classes.md section 6): each box is charged its screen rectangle
// x its family weight; past the screens the patches left: (0) the shimmer rects of other owners, (1) the puffs' second
// octave, then other owners' puffs past two per owner, (2) other owners' scorches, oldest first, (3) their rings and
// filament glows, (4) their glows and embers, oldest first, then their remaining puffs, last their flashes. Entry
// streaks, sparks, chips, beam spot cores and anything of the own ship are never shed (count caps: hull::max_sparks;
// renderer::weapon_sprite_quads, weapon_glow_boxes, weapon_fireball_boxes, weapon_ring_quads, weapon_filament_boxes,
// weapon_scorch_boxes; hull::max_spots, scorch_store, shimmer_max).
//
// State: HullState holds, per ring slot, the record's first stage frame (its game ms, its world contact point, the
// owner's velocity then, the shot heading in world, the three-frame floor of that frame's step, kept for the record's
// life so a SETA change never lengthens a fading element back into view) keyed by the record's sequence, the owners'
// last positions for the velocity, the beam spots and the scorch store. Past hull::max_owners owners a frame, those
// whose newest hull hit is the oldest are not drawn (ties by address: a stable choice). CPU data naming engine objects
// only: a Reset leaves it valid; a cleared or reused ring slot is recognised by its sequence. No allocation, no libm,
// SSE2 floats and doubles.
namespace x3m::weapon_effects::stage {
namespace core = x3m::weapon_effects::core;
// The pair directions a record keeps from its first drawn frame (the performance pass: the rejection draw once, not
// every frame), every set's pairs in order; a record with more pairs draws the rest every frame.
constexpr unsigned hull_cached_pairs = 36;
struct HullSlot {
    std::uint32_t sequence = 0, first_ms = 0;
    float pair_dir[hull_cached_pairs][3]; // world, unit (pair_direction), valid below `pairs`
    std::uint8_t pairs = 0;
    double p0[3]{};   // the contact point in world (view units) at the record's first stage frame
    float v[3]{};     // the owner's velocity then (world, view units per game second)
    float axis[3]{};  // the shot heading in world (Event::dir through the owner's rows then), unit; has_axis
    float floor_ms = 0.f; // the three-frame floor latched at the first drawn frame (min_frames x that frame's step)
    std::uint8_t latched = 0, orphan = 0, has_axis = 0;
};
struct OwnerTrack {
    std::uint32_t owner = 0, node = 0, handle = 0, ms = 0, used = 0;
    std::int32_t pos[3]{};
};
// A beam spot (the hold rule): its newest tick's record, its birth (first stage frame) and that tick's game ms, the seed
// of its spatter, its floor, the last scorch mark (owner frame) and whether its birth flashed (a pulse start).
struct Spot {
    core::Event e{};
    std::uint32_t first_ms = 0, last_ms = 0, seed = 0, merges = 0;
    float floor_ms = 0.f;
    std::int32_t trail[3]{};
    std::uint8_t used = 0, flash = 0;
};
// A scorch mark (the per-owner store, 36 bytes): its owner (validated as a record's), the game ms it was laid, the
// owner-frame point, r_hit then (view units), the development time (the ember's law, game ms), the anatomy and its
// kinds (1 the scorch).
struct ScorchMark {
    std::uint32_t owner = 0, node = 0, handle = 0, t0 = 0;
    std::int32_t pos[3]{};
    float r = 0.f;
    std::uint16_t ember_ms = 0;
    std::uint8_t anatomy = 0, kinds = 0;
};
constexpr std::uint8_t mark_scorch = 1;
// A heat-shimmer rect for the engine shimmer pass (view space; the glue projects it with the unjittered projection):
// the hit point, the shot heading (has_axis) or none (the view's x), the square's side (view units), the strength (1 at
// birth, 0 at shimmer_ms), a seed in [0, 1), its age and whether it belongs to the own ship.
using ShimmerRect = x3m::engine_shimmer::WeaponRequest;
static_assert(core::hull::shimmer_max <= x3m::engine_shimmer::weapon_max_rects, "the shimmer hand-off's capacity");
// The owners' positions at their last stage frame: every owner with a stage-owned record (a shield hit's too) is
// tracked, so a first hull hit on an owner drawn in the previous stage frame takes its velocity at once.
constexpr unsigned owner_tracks = 2 * core::hull::max_owners;
struct HullState {
    HullSlot slots[core::ring_capacity];
    OwnerTrack tracks[owner_tracks];
    Spot spots[core::hull::max_spots];
    ScorchMark scorches[core::hull::scorch_store];
    std::uint32_t use = 0;
    std::uint32_t prev_now = 0; // the previous stage call's game ms (has_prev)
    bool has_prev = false;
};
// A load, a sector change or a new load epoch (the ring's scope): the spots and the scorch marks name engine objects of
// the old scope and go with the ring's records.
inline void hull_rescope(HullState& s) noexcept {
    for (auto& p : s.spots) p = Spot{};
    for (auto& m : s.scorches) m = ScorchMark{};
}
inline void hull_clear(HullState& s) noexcept {
    for (auto& v : s.slots) v = HullSlot{};
    for (auto& t : s.tracks) t = OwnerTrack{};
    for (auto& p : s.spots) p = Spot{};
    for (auto& m : s.scorches) m = ScorchMark{};
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
    bool streak = true, filament = true, scorch = core::hull::scorch, shimmer = true, spots = true;
    float cell_m[core::patch_tier_count] = {core::cell_m[0], core::cell_m[1], core::cell_m[2]};
    float stagger = core::stagger;
    float own_gain = core::own_cockpit_gain;
    unsigned min_frames = core::look::min_frames;
    float spark_count_x = core::hull::spark_count_x; // the kill criterion's halving (a header decision, never a setting)
    unsigned set_mask = 7; // the spark sets drawn (bit 0 fast, 1 slow, 2 chips; the fixture isolates one)
};
struct HullCounts {
    unsigned hits = 0, flashes = 0, sparks = 0, glows = 0, embers = 0, bursts = 0, rings = 0;
    unsigned orphans = 0, retired = 0, shed = 0, capped = 0, floored = 0, owners_dropped = 0;
    unsigned bursts_shed = 0;
    unsigned streaks = 0, chips = 0, filaments = 0, scorches = 0, marks = 0, spots = 0, merges = 0, spot_flashes = 0;
    unsigned pulses = 0; // spots whose pulse started this frame (a new spot, or a re-lit one after a gap)
    unsigned done = 0, flashes_shed = 0; // records released at their anatomy's end; flashes the budget shed (last)
    unsigned shimmer = 0, shimmer_shed = 0, scorches_shed = 0, rings_shed = 0, filaments_shed = 0, dir_fallback = 0;
    unsigned anatomy[core::hull::anatomy_count]{}; // live records (and spots) per anatomy this frame
    unsigned octaves = core::look::fireball_octaves; // the puffs' noise octaves this frame (1: the budget's LOD)
    float screens = 0.f; // the boxes' charged screens after shedding
    float burst_screens = 0.f; // of them the puffs'
    float flash_screens = 0.f; // of them the flashes' (charged, shed last)
    float max_floor_ms = 0.f; // the largest floor latched by a live record (Present's keep, HookState::life_floor_ms)
};
// The frame's outputs (four vertices an element; nullptr / 0 capacity: none built).
struct HullBuffers {
    renderer::WeaponSpriteVertex* sprites = nullptr;
    unsigned sprite_cap = 0, sprite_quads = 0;
    renderer::WeaponGlowVertex* glows = nullptr;
    unsigned glow_cap = 0, glow_boxes = 0;
    renderer::WeaponFireballVertex* fireballs = nullptr;
    unsigned fireball_cap = 0, fireball_boxes = 0;
    renderer::WeaponShapeVertex* rings = nullptr;
    unsigned ring_cap = 0, ring_quads = 0;
    renderer::WeaponShapeVertex* filaments = nullptr;
    unsigned filament_cap = 0, filament_boxes = 0;
    renderer::WeaponGlowVertex* scorches = nullptr;
    unsigned scorch_cap = 0, scorch_boxes = 0;
    ShimmerRect* shimmer = nullptr;
    unsigned shimmer_cap = 0, shimmer_count = 0;
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
// The record's anatomy (hull-impact-classes.md section 2): the victim first (an asteroid takes rock dust whatever the
// weapon), then the variants (repair, tug), a missile source, the class nibble the hook wrote.
inline unsigned anatomy_of(const core::Event& e) noexcept {
    namespace hl = core::hull;
    if (e.detail & core::detail_asteroid) return hl::an_asteroid;
    if (e.detail & core::detail_repair) return hl::an_repair;
    if (e.detail & core::detail_tug) return hl::an_tug;
    if (e.table == core::table_missiles) return hl::an_missile;
    return core::hull_class_of(e);
}
// The hold rule's records (section 3.9): beams, repair and tug, whatever the target's look (review 2026-11-07: a beam
// on an asteroid kept one record per tick in the ring for its whole life); a beam's spot on rock draws the asteroid's
// dust puff and rock spatter.
inline bool holds(const core::Event& e) noexcept {
    return (e.detail & (core::detail_repair | core::detail_tug)) || core::hull_class_of(e) == core::class_beam;
}
// One spark set's row (core::hull::class_sparks, 3 anatomy + set).
struct SparkSet {
    float n0, nk, speed, speed_spread, life, life_k, width, rest_w, tau, e, t0, bias, duty;
};
inline SparkSet spark_set(unsigned anatomy, unsigned set) noexcept {
    const float* r = core::hull::class_sparks[3 * anatomy + set];
    return SparkSet{r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12]};
}
// A set's spark count of a record (before the frame's cap): the law n = (n0 + nk k) x spark_count_x x (1 +
// spark_count_spread (2 u - 1)), u hashed from the sequence (element 16 + set), rounded; at least one pair; none in the
// simple mode or for a set the anatomy does not have.
inline unsigned spark_count(const core::Event& e, float k, const HullTuning& t, unsigned anatomy, unsigned set) noexcept {
    if (t.mode == core::hull::simple || !(t.set_mask & (1u << set))) return 0;
    const SparkSet ss = spark_set(anatomy, set);
    float n = (ss.n0 + ss.nk * k) * t.spark_count_x;
    if (!(n > 0.f)) return 0;
    n *= 1.f + core::hull::spark_count_spread * (2.f * unit_of(element_hash(e.sequence, 16u + set)) - 1.f);
    const unsigned c = unsigned(n + 0.5f);
    return c < 2u ? 2u : c;
}
// A spark's own factors (a hash of the record's sequence and the spark's index s over every set, element 64 + s, and one
// mix of it whose four bytes give the four factors at 1/256 steps: two hashes a spark, the performance pass): speed (x
// 1 +- its set's spread), life, rest length and width against their laws, and whether it curls (the element hash's top
// bits).
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
inline SparkFactors spark_factors(std::uint32_t sequence, unsigned s, float speed_spread) noexcept {
    namespace hl = core::hull;
    const std::uint32_t h0 = element_hash(sequence, 64u + s), h1 = mix32(h0 + 0x9e3779b9u);
    SparkFactors f;
    f.speed_x = 1.f + speed_spread * (2.f * byte_unit(h1, 0) - 1.f);
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
// A puff's radius at age t (game ms) of a life L (anatomy row of class_puff): R_b (1 - e^(-t / (rise L))) x (1 - shrink
// max(0, (t / L - 0.3) / 0.7)) (the plasma globe shrinks to (1 - shrink) R_b over the last 0.7 L).
inline float puff_radius(unsigned anatomy, float R_b, float age_ms, float life_ms) noexcept {
    const float* p = core::hull::class_puff[anatomy];
    float R = R_b * (1.f - core::exp_neg(-age_ms / (p[7] * life_ms)));
    const float late = (age_ms / life_ms - 0.3f) * (1.f / 0.7f);
    if (p[8] > 0.f && late > 0.f) R *= 1.f - p[8] * (late < 1.f ? late : 1.f);
    return R;
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
inline float spark_reach_factor(float t_s, float tau) noexcept {
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
// 1.5 x the near plane for the whole screen), each corner carrying the sphere, its look (T, E, advection, seed) and its
// tint (rgb, share).
inline void fireball_quad(const HullView& f, const float c[3], float R, const float rect[4], bool full, const float look[4],
                          const float tint[4], renderer::WeaponFireballVertex* out) noexcept {
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
        for (unsigned i = 0; i < 4; ++i) out[k].tint[i] = tint[i];
    }
}
// A shaped glow's four corners (weapon_shape_vs.hlsl): the glow's rectangle with the profile's terms (ring: r(t), w;
// filament: scale, seed, width, R and the owner's axes in view).
inline void shape_quad(const HullView& f, const float c[3], float R, const float rect[4], bool full, const float colour[3],
                       const float shape[4], const float* frame, renderer::WeaponShapeVertex* out) noexcept {
    const float zq = full ? 1.5f * f.near_z : c[2] - R;
    const float kx = zq / f.cam.m00, ky = zq / f.cam.m11;
    for (unsigned k = 0; k < 4; ++k) {
        const float nx = rect[k & 1u ? 2 : 0], ny = rect[k & 2u ? 3 : 1];
        out[k].corner[0] = (nx - f.cam.m20) * kx;
        out[k].corner[1] = (ny - f.cam.m21) * ky;
        out[k].corner[2] = zq;
        for (unsigned i = 0; i < 3; ++i) out[k].light[i] = c[i];
        out[k].light[3] = R;
        for (unsigned i = 0; i < 3; ++i) out[k].colour[i] = colour[i];
        for (unsigned i = 0; i < 4; ++i) out[k].shape[i] = shape[i];
        for (unsigned i = 0; i < 9; ++i) out[k].frame[i] = frame ? frame[i] : 0.f;
    }
}
// A flash's quad (the sprite batch's disc kind): the core core_frac r_hit (>= floor_flash_px) in a halo of halo_frac
// r_hit, centred on c (view) biased towards the eye by bias r_hit, the halo's colour, its radiance. False when behind the
// near plane or past the cap (*capped).
inline bool flash_quad(const HullView& f, const Screen& sc, const float c_view[3], float r_hit, float core_frac,
                       float halo_frac, const float halo[3], float radiance, renderer::WeaponSpriteVertex* v,
                       float* screens) noexcept {
    const float px_x = 0.5f * f.width * f.cam.m00;
    const float dist = scalar::sqrt(dot3(c_view, c_view));
    const float b = dist > 1e-6f ? 1.f - core::hull::bias * r_hit / dist : 1.f;
    const float c[3] = {c_view[0] * b, c_view[1] * b, c_view[2] * b};
    if (!(c[2] > f.near_z)) return false;
    const Px p = to_px(f, sc, c);
    const float r_px = r_hit * px_x * p.iz;
    float core_px = core_frac * r_px;
    if (core_px < 0.5f * core::look::floor_flash_px) core_px = 0.5f * core::look::floor_flash_px;
    const float halo_over_core = core_frac > 0.f ? halo_frac / core_frac : 1.f;
    const float halo_law = halo_frac * r_px;
    float halo_px = halo_law > core_px * halo_over_core ? halo_law : core_px * halo_over_core;
    if (halo_px < core_px) halo_px = core_px;
    const float r_view = halo_px * c[2] / px_x;
    const std::uint32_t colour = pack_colour(halo, core_px / halo_px);
    // its charge: the halo's square clipped to the screen, in screens
    const float x0 = p.x - halo_px > 0.f ? p.x - halo_px : 0.f, x1 = p.x + halo_px < f.width ? p.x + halo_px : f.width;
    const float y0 = p.y - halo_px > 0.f ? p.y - halo_px : 0.f, y1 = p.y + halo_px < f.height ? p.y + halo_px : f.height;
    *screens = x1 > x0 && y1 > y0 ? (x1 - x0) * (y1 - y0) / (f.width * f.height) : 0.f;
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
    return true;
}
// The bookkeeping of a box the budget may shed: its age (game ms), its owner, its charged screens, whether it is the own
// ship's, and its rank among its owner's boxes of the kind (0 the newest; the puffs' "past two per owner").
struct BoxMeta {
    std::uint32_t age, owner;
    float area;
    std::uint8_t own, rank;
};
// Sheds the oldest boxes of other owners (rank >= min_rank) while the charged screens exceed the budget; the last box
// moves into the freed place. Returns the boxes shed.
template <class V>
inline unsigned shed_oldest(V* verts, unsigned* count, BoxMeta* meta, float* total, float budget, std::uint8_t min_rank,
                            float* kind_total = nullptr) noexcept {
    unsigned shed = 0;
    while (*total > budget && *count) {
        unsigned old = *count;
        for (unsigned g = 0; g < *count; ++g)
            if (!meta[g].own && meta[g].rank >= min_rank && (old == *count || meta[g].age > meta[old].age)) old = g;
        if (old == *count) break; // only the own ship's (or the kept ranks): never shed
        *total -= meta[old].area;
        if (kind_total) *kind_total -= meta[old].area;
        const unsigned last = --*count;
        if (old != last) {
            for (unsigned c = 0; c < 4; ++c) verts[4 * old + c] = verts[4 * last + c];
            meta[old] = meta[last];
        }
        ++shed;
    }
    return shed;
}
// Lays a scorch mark (the per-owner store): an owner past scorch_per_owner marks retires its oldest; else a free entry,
// else the store's oldest.
inline void lay_mark(HullState& s, const core::Event& e, const std::int32_t pos[3], std::uint32_t now, float r,
                     float ember_ms, unsigned anatomy, std::uint8_t kinds) noexcept {
    constexpr unsigned cap = core::hull::scorch_store;
    unsigned same = 0, oldest_same = cap, free_at = cap, oldest = cap;
    for (unsigned i = 0; i < cap; ++i) {
        const ScorchMark& m = s.scorches[i];
        if (!m.kinds) {
            if (free_at == cap) free_at = i;
            continue;
        }
        const std::uint32_t age = now - m.t0;
        if (oldest == cap || age > now - s.scorches[oldest].t0) oldest = i;
        if (m.owner == e.owner && m.node == e.node && m.handle == e.handle) {
            ++same;
            if (oldest_same == cap || age > now - s.scorches[oldest_same].t0) oldest_same = i;
        }
    }
    const unsigned at = same >= core::hull::scorch_per_owner ? oldest_same : free_at != cap ? free_at : oldest;
    if (at == cap) return;
    ScorchMark& m = s.scorches[at];
    m.owner = e.owner;
    m.node = e.node;
    m.handle = e.handle;
    m.t0 = now;
    for (unsigned k = 0; k < 3; ++k) m.pos[k] = pos[k];
    m.r = r;
    m.ember_ms = std::uint16_t(ember_ms < 65535.f ? ember_ms : 65535.f);
    m.anatomy = std::uint8_t(anatomy);
    m.kinds = kinds;
}
// The record's calibre with the tug's (0.1 unless the override pins one).
inline float record_calibre(const core::Event& e) noexcept {
    return (e.detail & core::detail_tug) && !(e.flags & core::flag_calibre_override) ? 0.1f : hull_calibre(e);
}
// A weapon tint counts as saturated (the colour sources of section 5) when it came from the light colour or the override.
inline bool tint_saturated(const core::Event& e) noexcept {
    return e.tint_source == core::tint_light || e.tint_source == core::tint_override;
}
// Builds the frame's hull-hit elements (see the header) into the buffers. Returns the counts.
inline HullCounts build_hull(core::Ring& ring, HullState& state, Reader rd, const HullView& f, const HullTuning& t,
                             HullBuffers& out) noexcept {
    namespace hl = core::hull;
    HullCounts n{};
    unsigned nq = 0, ng = 0, nb = 0, nr = 0, nf = 0, ns = 0;
    out.sprite_quads = out.glow_boxes = out.fireball_boxes = out.ring_quads = out.filament_boxes = out.scorch_boxes =
        out.shimmer_count = 0;
    auto cap_of = [](const void* p, unsigned cap, unsigned most) { return p ? (cap < most ? cap : most) : 0u; };
    const unsigned sprite_cap = cap_of(out.sprites, out.sprite_cap, renderer::weapon_sprite_quads);
    const unsigned glow_cap = cap_of(out.glows, out.glow_cap, renderer::weapon_glow_boxes);
    const unsigned fireball_cap = cap_of(out.fireballs, out.fireball_cap, renderer::weapon_fireball_boxes);
    const unsigned ring_cap = cap_of(out.rings, out.ring_cap, renderer::weapon_ring_quads);
    const unsigned filament_cap = cap_of(out.filaments, out.filament_cap, renderer::weapon_filament_boxes);
    const unsigned scorch_cap = cap_of(out.scorches, out.scorch_cap, renderer::weapon_scorch_boxes);
    const unsigned shimmer_cap = cap_of(out.shimmer, out.shimmer_cap, hl::shimmer_max);
    if (!(f.width > 0.f) || !(f.height > 0.f) || !(f.cam.m00 > 0.f) || !(f.cam.m11 > 0.f)) return n;
    const float frame_floor_ms = float(t.min_frames) * float(f.step_ms);
    const std::uint32_t hold_window = hl::hold_ms > 2u * f.step_ms ? hl::hold_ms : 2u * f.step_ms;
    const float engine_per_view = 1.f / core::world_scale;
    // (A) The hold rule (section 3.9): every stage-owned beam, repair or tug record re-lights a spot of its owner and
    // (table, row) within hold_r r, or becomes one; either way it leaves the ring (one record per beam per frame).
    if (t.spots) {
        for (unsigned i = 0; i < core::ring_capacity; ++i) {
            const core::Event& e = ring.slots[i];
            if (!e.sequence || e.role != core::role_hull_hit || !(e.flags & core::flag_stage)) continue;
            if (!holds(e)) continue;
            const unsigned a = anatomy_of(e);
            const float r_eng = hull_r_hit(e, t) * engine_per_view, hold2 = hl::hold_r * r_eng * hl::hold_r * r_eng;
            Spot* hit = nullptr;
            bool held_any = false;
            for (Spot& sp : state.spots) {
                if (!sp.used || sp.e.owner != e.owner || sp.e.node != e.node || sp.e.handle != e.handle ||
                    sp.e.table != e.table || sp.e.row != e.row)
                    continue;
                const std::uint32_t rel = f.now - sp.last_ms;
                const bool held = std::int32_t(rel) <= std::int32_t(hold_window);
                held_any = held_any || held;
                float d2 = 0.f;
                for (unsigned k = 0; k < 3; ++k) {
                    const float d = float(core::wrap_sub(e.pos[k], sp.e.pos[k]));
                    d2 += d * d;
                }
                if (d2 <= hold2 && (!hit || sp.last_ms - hit->last_ms < 0x80000000u)) hit = &sp;
            }
            const std::uint32_t tick = std::int32_t(f.now - e.t0_game_ms) >= 0 ? e.t0_game_ms : f.now;
            // the scorch line (section 4.5: the dwell path as scorches trail_step_r r apart; the trail ember is the
            // spot's own, from its fall)
            const bool beam = a == hl::an_beam;
            const bool marks = beam && t.scorch;
            const std::uint8_t kinds = mark_scorch;
            if (hit) { // re-lit: its newest tick, never added; after a gap (no held spot) a pulse start
                if (!held_any) {
                    hit->first_ms = f.now;
                    hit->flash = 1;
                    hit->seed = e.sequence;
                    ++n.pulses;
                }
                float d2 = 0.f;
                for (unsigned k = 0; k < 3; ++k) {
                    const float d = float(core::wrap_sub(e.pos[k], hit->trail[k]));
                    d2 += d * d;
                }
                hit->e = e;
                hit->last_ms = tick;
                ++hit->merges;
                ++n.merges;
                if (marks && d2 >= hl::trail_step_r * r_eng * hl::trail_step_r * r_eng) {
                    lay_mark(state, e, e.pos, f.now, r_eng * core::world_scale, hl::class_ember[a][1], a, kinds);
                    for (unsigned k = 0; k < 3; ++k) hit->trail[k] = e.pos[k];
                    ++n.marks;
                }
            } else {
                Spot* slot = nullptr;
                for (Spot& sp : state.spots)
                    if (!sp.used) {
                        slot = &sp;
                        break;
                    }
                if (!slot) { // full: the spot whose newest tick is the oldest
                    slot = &state.spots[0];
                    for (Spot& sp : state.spots)
                        if (f.now - sp.last_ms > f.now - slot->last_ms) slot = &sp;
                }
                *slot = Spot{};
                slot->e = e;
                slot->first_ms = f.now;
                slot->last_ms = tick;
                slot->seed = e.sequence;
                slot->floor_ms = frame_floor_ms;
                for (unsigned k = 0; k < 3; ++k) slot->trail[k] = e.pos[k];
                slot->used = 1;
                slot->flash = held_any ? 0 : 1; // a pulse start: the owner held no spot of that (table, row)
                n.pulses += slot->flash;
                if (marks) {
                    lay_mark(state, e, e.pos, f.now, r_eng * core::world_scale, hl::class_ember[a][1], a, kinds);
                    ++n.marks;
                }
            }
            core::ring_release(ring, i);
            state.slots[i] = HullSlot{};
        }
    }
    // (B) The owners (review 3): every owner of a stage-owned record, its newest hull hit's age (t0, game ms; a
    // shield-only owner after them) and whether it has a hull hit; past hull::max_owners the owners whose newest hit is
    // the oldest are not drawn this frame (ties by the object address), a stable choice from frame to frame (no
    // blinking).
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
    const unsigned keep = cand_count < hl::max_owners ? cand_count : hl::max_owners;
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
        std::uint32_t owner, node, handle;
        OwnerStatus status;
        Owner data;
        float vel[3];
        float centre[3], frame[9]; // its frame in view (owner_frame), once per frame when a filament needs it
        std::uint8_t frame_state;  // 0 not taken, 1 valid, 2 degenerate
    };
    // The ring's kept owners, then the spots' and marks' owners read on demand (owner_extra at most).
    constexpr unsigned owner_extra = hl::max_owners;
    Seen seen[hl::max_owners + owner_extra];
    unsigned seen_count = keep;
    for (unsigned k = 0; k < keep; ++k) { // read the kept owners, track them, take their velocity
        Seen& sn = seen[k];
        sn.owner = cand[k].owner;
        sn.node = cand[k].node;
        sn.handle = cand[k].handle;
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
    // An owner of a spot or a mark: the kept owners first (a spot's or mark's owner past them is read here, at most
    // owner_extra a frame; past that it is not drawn this frame, never freed).
    auto lookup = [&](std::uint32_t owner, std::uint32_t node, std::uint32_t handle) -> Seen* {
        for (unsigned k = 0; k < seen_count; ++k)
            if (seen[k].owner == owner && seen[k].node == node && seen[k].handle == handle) return &seen[k];
        if (seen_count >= hl::max_owners + owner_extra) return nullptr;
        core::Event probe{};
        probe.owner = owner;
        probe.node = node;
        probe.handle = handle;
        Seen& sn = seen[seen_count++];
        sn.owner = owner;
        sn.node = node;
        sn.handle = handle;
        sn.status = read_owner(rd, probe, &sn.data);
        sn.vel[0] = sn.vel[1] = sn.vel[2] = 0.f;
        sn.frame_state = 0;
        return &sn;
    };
    // The budget's bookkeeping per box.
    static_assert(renderer::weapon_glow_boxes <= 0xffff && renderer::weapon_fireball_boxes <= 0xffff, "box indices");
    BoxMeta glow_meta[renderer::weapon_glow_boxes];
    BoxMeta burst_meta[renderer::weapon_fireball_boxes];
    BoxMeta ring_meta[renderer::weapon_ring_quads];
    BoxMeta filament_meta[renderer::weapon_filament_boxes];
    BoxMeta scorch_meta[renderer::weapon_scorch_boxes];
    ShimmerRect shimmer_cand[16];
    unsigned shimmer_cands = 0;
    const Camera& prev = f.prev_valid ? f.prev : f.cam;
    const float step_s = float(f.step_ms) * 1e-3f;
    const Screen sc = screen_of(f);
    unsigned sparks_left = hl::max_sparks;
    float flash_area = 0.f; // the flashes' charge (the shed order's last step: other owners' flashes, oldest first)
    // The flashes' bookkeeping (their quad, age, owner): the emitter reads the record's while it draws.
    constexpr unsigned flash_meta_cap = core::ring_capacity + hl::max_spots;
    BoxMeta flash_meta[flash_meta_cap];
    std::uint16_t flash_quad_of[flash_meta_cap];
    unsigned nfl = 0;
    std::uint32_t flash_age = 0, flash_owner = 0;
    bool flash_own = false;
    const float lod_px = hl::puff_lod_px * f.height * (1.f / 1440.f);
    // The element emitters (each counts its cap).
    auto glow_box = [&](const float c[3], float R, const float colour[3], std::uint32_t age_ms, bool own, std::uint32_t owner,
                        float weight) -> bool {
        if (!(c[2] + R > f.near_z)) return false;
        float rect[4];
        bool full = false;
        if (!sphere_rect(f, c, R, rect, &full)) return false; // off screen
        if (ng >= glow_cap) {
            ++n.capped;
            return false;
        }
        glow_quad(f, c, R, rect, full, colour, out.glows + 4 * ng);
        glow_meta[ng] = BoxMeta{age_ms, owner, rect_screens(rect) * weight, std::uint8_t(own), 0};
        ++ng;
        return true;
    };
    auto puff_box = [&](const float c[3], float R, float T, float E, float advect, float seed, const float tint4[4],
                        std::uint32_t age_ms, bool own, std::uint32_t owner) -> bool {
        float rect[4];
        bool full = false;
        if (!(R > 0.f) || !(c[2] + R > f.near_z) || !sphere_rect(f, c, R, rect, &full)) return false;
        if (nb >= fireball_cap) {
            ++n.capped;
            return false;
        }
        // the octave LOD at fighter range: a puff under lod_px draws one octave (a negative seed)
        const bool one = !full && R * f.width * 0.5f * f.cam.m00 < lod_px * c[2];
        const float look[4] = {T, E, advect, one ? -seed : seed}; // seed > 0: the sign alone carries the LOD
        fireball_quad(f, c, R, rect, full, look, tint4, out.fireballs + 4 * nb);
        burst_meta[nb] = BoxMeta{age_ms, owner,
                                 rect_screens(rect) * (one ? core::look::budget_weight_fireball_lod : core::look::budget_weight_fireball),
                                 std::uint8_t(own), std::uint8_t(one ? 1 : 0)};
        ++nb;
        ++n.bursts;
        return true;
    };
    auto emit_flash = [&](const float c_view[3], float r_hit, float core_frac, float halo_frac, const float halo[3],
                          float radiance) -> bool {
        if (nq >= sprite_cap) {
            if (c_view[2] > f.near_z) ++n.capped;
            return false;
        }
        float screens = 0.f;
        if (!flash_quad(f, sc, c_view, r_hit, core_frac, halo_frac, halo, radiance, out.sprites + 4 * nq, &screens)) return false;
        flash_area += screens * hl::budget_weight_flash;
        if (nfl < flash_meta_cap) {
            flash_quad_of[nfl] = std::uint16_t(nq);
            flash_meta[nfl++] = BoxMeta{flash_age, flash_owner, screens * hl::budget_weight_flash, std::uint8_t(flash_own), 0};
        }
        ++nq;
        return true;
    };
    float white[3] = {1.f, 1.f, 1.f};
    // (C) The records (every anatomy but the spots').
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
            for (unsigned k = 0; k < keep && !s; ++k)
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
        const unsigned a = anatomy_of(e);
        const float k = record_calibre(e);
        const float U = hull_unit(e, t), r_hit = hl::r_scale * U * (hl::r_base + hl::r_k * k);
        if (!hs.latched) {
            hit_world(s->data, e.pos, hs.p0);
            for (unsigned q = 0; q < 3; ++q) hs.v[q] = s->vel[q];
            hs.first_ms = f.now;
            hs.floor_ms = frame_floor_ms; // review 1: the floor of its first drawn frame, kept for its whole life
            // The shot heading (section 3.4: data, never an estimate): Event::dir through the owner's rows.
            if (e.dir[0] | e.dir[1] | e.dir[2]) {
                for (unsigned q = 0; q < 3; ++q) {
                    float w = 0.f;
                    for (unsigned j = 0; j < 3; ++j) w += float(s->data.rows[4 * j + q]) * (1.f / 65536.f) * float(e.dir[j]);
                    hs.axis[q] = w;
                }
                hs.has_axis = normalize3(hs.axis) ? 1 : 0;
            }
            hs.latched = 1;
            // Its scorch: a mark in the per-owner store (it outlives the record).
            if (t.scorch && hl::class_scorch[a][0] > 0.f) {
                lay_mark(state, e, e.pos, f.now, r_hit, hl::class_ember[a][1] + hl::class_ember[a][2] * k, a, mark_scorch);
                ++n.marks;
            }
        }
        const float floor_ms = hs.floor_ms;
        const std::uint32_t age_ms = f.now - hs.first_ms;
        if (age_ms > 60000u) continue; // a clock reset: Present releases it
        // Its anatomy's slowest element over (core::hull_lives, review 2026-11-07): the record leaves the ring at its own
        // end, not at the slowest class's (Present's hull_lifetime_ms bound stays for frames the stage does not draw).
        {
            const float life_end = float(core::hull_lives.v[a]) > floor_ms ? float(core::hull_lives.v[a]) : floor_ms;
            if (float(age_ms) > life_end) {
                core::ring_release(ring, i);
                hs = HullSlot{};
                ++n.done;
                continue;
            }
        }
        if (floor_ms > n.max_floor_ms) n.max_floor_ms = floor_ms;
        ++n.hits;
        ++n.anatomy[a];
        if (hs.orphan) ++n.orphans;
        const float age = float(age_ms);
        const bool own = f.own_node && e.node == f.own_node;
        const float gain = (f.cockpit && own) ? t.own_gain : 1.f;
        flash_age = age_ms;
        flash_owner = e.owner;
        flash_own = own;
        const bool jets = a == hl::an_ballistic || a == hl::an_heavy || a == hl::an_missile;
        if (jets && !hs.has_axis) ++n.dir_fallback;
        float tint[3];
        core::event_tint(e, 0.f, tint);
        const bool saturated = tint_saturated(e);
        // The current contact point (view) from the owner's rows: every owner-attached element rides it.
        float c_view[3] = {0.f, 0.f, 0.f};
        float a_view[3] = {0.f, 0.f, 0.f};
        if (!hs.orphan) {
            double w[3];
            hit_world(s->data, e.pos, w);
            to_view(f.cam, w, c_view);
        }
        if (hs.has_axis) rotate_world_to_view(f.cam, hs.axis, a_view);
        bool fl = false;
        // Flash (and the heavy class's entry streak along the shot).
        const float* fr = hl::class_flash[a];
        if (!hs.orphan && fr[2] > 0.f) {
            const bool streak = a == hl::an_heavy && hs.has_axis && t.streak;
            const float life = element_life_ms(fr[2], element_hash(e.sequence, 0), t.stagger, floor_ms, &fl);
            if (t.flash && age < life) {
                float halo[3];
                const unsigned mode = unsigned(fr[4]);
                if (mode == 1 && saturated) {
                    core::heat_colour(1.f, halo);
                    for (unsigned q = 0; q < 3; ++q) halo[q] = 0.5f * (halo[q] + tint[q]);
                } else if (mode == 2) { // the ion's fixed electric blue-white (ratified 2026-11-06, question 4)
                    for (unsigned q = 0; q < 3; ++q) halo[q] = hl::ion_colour[q];
                } else if (mode == 3) {
                    for (unsigned q = 0; q < 3; ++q) halo[q] = tint[q];
                } else
                    core::heat_colour(fr[3], halo);
                const float halo_r = a == hl::an_heavy && !hs.has_axis ? hl::streak_fallback_halo : fr[1];
                if (emit_flash(c_view, r_hit, fr[0], halo_r, halo, core::look::core_e * (1.f - age / life) * gain)) {
                    ++n.flashes;
                    n.floored += fl;
                }
            }
            if (streak) {
                const float slife = element_life_ms(float(hl::streak_ms), element_hash(e.sequence, 5), t.stagger, floor_ms, &fl);
                if (age < slife) {
                    float head[3], tail[3];
                    for (unsigned q = 0; q < 3; ++q) {
                        head[q] = c_view[q] + a_view[q] * hl::streak_front * r_hit;
                        tail[q] = c_view[q] - a_view[q] * hl::streak_back * r_hit;
                    }
                    if (head[2] > f.near_z && tail[2] > f.near_z) {
                        if (nq < sprite_cap) {
                            const float rad = hl::streak_e * (1.f - age / slife) * gain;
                            streak_vertices(head, tail, rad, rad, hl::streak_width * r_hit, pack_colour(white, 0.f),
                                            out.sprites + 4 * nq);
                            ++nq;
                            ++n.streaks;
                            n.floored += fl;
                        } else
                            ++n.capped;
                    }
                }
            }
        }
        // Puff: the fireball impostor at the contact point riding the owner.
        const float* pf = hl::class_puff[a];
        const float R_b = pf[0] * r_hit + U * (pf[1] + pf[2] * k);
        if (t.burst && fireball_cap && !hs.orphan && R_b > 0.f) {
            const float life = element_life_ms(pf[3] + pf[4] * k, element_hash(e.sequence, 3), t.stagger, floor_ms, &fl);
            if (age < life) {
                const float R = puff_radius(a, R_b, age, life);
                const float x = age / life;
                const float mix = pf[9] > 0.f && saturated ? pf[9] * (1.f - x) : 0.f;
                const float tint4[4] = {tint[0], tint[1], tint[2], mix};
                if (puff_box(c_view, R, pf[5] * (1.f - x), pf[6] * gain, core::look::fireball_advect * x,
                             64.f * unit_of(element_hash(e.sequence, 4)) + 1e-3f, tint4, age_ms, own, e.owner))
                    n.floored += fl;
            }
        }
        // Surface ring: the expanding sphere shell cut by the lane (no normal).
        const float* rg = hl::class_ring[a];
        if (t.ring && ring_cap && !hs.orphan && rg[0] > 0.f) {
            const float life = element_life_ms(rg[2], element_hash(e.sequence, 6), t.stagger, floor_ms, &fl);
            if (age < life) {
                const float x = age / life;
                const float rr = rg[0] * r_hit * (1.f - core::exp_neg(-x / hl::ring_rise)), w = rg[1] * r_hit;
                const float R = rr + 3.f * w; // the shell's profile at the box edge e^-9
                float rect[4];
                bool full = false;
                if (c_view[2] + R > f.near_z && sphere_rect(f, c_view, R, rect, &full)) {
                    if (nr < ring_cap) {
                        float colour[3];
                        core::heat_colour(hl::ring_t, colour);
                        const float e0 = rg[3] * hl::ring_t * (1.f - x) * (1.f - x) * gain;
                        for (unsigned q = 0; q < 3; ++q) colour[q] *= e0;
                        const float shape[4] = {rr, w, 0.f, 0.f};
                        shape_quad(f, c_view, R, rect, full, colour, shape, nullptr, out.rings + 4 * nr);
                        ring_meta[nr] = BoxMeta{age_ms, e.owner, rect_screens(rect) * hl::budget_weight_ring, std::uint8_t(own), 0};
                        ++nr;
                        ++n.rings;
                        n.floored += fl;
                    } else
                        ++n.capped;
                }
            }
        }
        // Filament glow (ion): noise in the owner's frame, re-hashed every rehash_ms.
        if (t.filament && filament_cap && !hs.orphan && a == hl::an_ion) {
            const float life = element_life_ms(float(hl::filament_ms), element_hash(e.sequence, 8), t.stagger, floor_ms, &fl);
            if (age < life) {
                if (!s->frame_state) s->frame_state = owner_frame(s->data, f.cam, s->centre, s->frame) ? 1 : 2;
                const float R = hl::filament_r * r_hit;
                float rect[4];
                bool full = false;
                if (s->frame_state == 1 && c_view[2] + R > f.near_z && sphere_rect(f, c_view, R, rect, &full)) {
                    if (nf < filament_cap) {
                        float colour[3];
                        const float e0 = hl::filament_e * (1.f - age / life) * gain;
                        for (unsigned q = 0; q < 3; ++q) colour[q] = hl::ion_colour[q] * e0;
                        const unsigned slot = age_ms / hl::rehash_ms;
                        const float shape[4] = {hl::filament_scale / r_hit,
                                                64.f * unit_of(element_hash(e.sequence, 512u + (slot & 255u))),
                                                hl::filament_width, R};
                        shape_quad(f, c_view, R, rect, full, colour, shape, s->frame, out.filaments + 4 * nf);
                        filament_meta[nf] = BoxMeta{age_ms, e.owner, rect_screens(rect) * hl::budget_weight_filament,
                                                    std::uint8_t(own), 0};
                        ++nf;
                        ++n.filaments;
                        n.floored += fl;
                    } else
                        ++n.capped;
                }
            }
        }
        // Heat shimmer (plasma, heavy): a candidate rect; the frame keeps shimmer_max (the own ship's, then the newest).
        if (t.shimmer && shimmer_cap && !hs.orphan && hl::class_shimmer[a] > 0.f && age < float(hl::shimmer_ms) &&
            shimmer_cands < 16 && c_view[2] > f.near_z) {
            ShimmerRect& sr = shimmer_cand[shimmer_cands++];
            for (unsigned q = 0; q < 3; ++q) {
                sr.centre[q] = c_view[q];
                sr.axis[q] = a_view[q];
            }
            sr.size = hl::class_shimmer[a] * r_hit;
            sr.strength = 1.f - age / float(hl::shimmer_ms);
            sr.seed = unit_of(element_hash(e.sequence, 9));
            sr.age = age_ms;
            sr.own = own;
            sr.has_axis = hs.has_axis;
        }
        // Spark sets: per set a hashed count, per pair a hashed isotropic direction u (the second spark -u), each launched
        // along normalize(+-u + b a), its speed, life, length, width and curl (spark_factors); closed form in game time
        // from the contact point at the record's first frame, p(t) = p0 + (d speed + v_owner) tau (1 - e^(-t / tau)) + w t.
        unsigned counts[3] = {0, 0, 0}, pairs_total = 0;
        if (t.sparks)
            for (unsigned set = 0; set < 3; ++set) {
                counts[set] = spark_count(e, k, t, a, set);
                pairs_total += (counts[set] + 1) / 2;
            }
        if (pairs_total) {
            const float t_now = age * 1e-3f, t_prev = t_now > step_s ? t_now - step_s : 0.f;
            float v_now[3], v_prev[3], p_now[3], p_prev[3], ax_now[3] = {0.f, 0.f, 0.f}, ax_prev[3] = {0.f, 0.f, 0.f};
            rotate_world_to_view(f.cam, hs.v, v_now);
            rotate_world_to_view(prev, hs.v, v_prev);
            to_view(f.cam, hs.p0, p_now);
            to_view(prev, hs.p0, p_prev);
            if (hs.has_axis) {
                for (unsigned q = 0; q < 3; ++q) ax_now[q] = a_view[q];
                rotate_world_to_view(prev, hs.axis, ax_prev);
            }
            if (!hs.pairs) { // the record's pair directions, once
                const unsigned cache = pairs_total < hull_cached_pairs ? pairs_total : hull_cached_pairs;
                for (unsigned j = 0; j < cache; ++j) pair_direction(e.sequence, j, hs.pair_dir[j]);
                hs.pairs = std::uint8_t(cache);
            }
            const bool ion = a == hl::an_ion;
            const std::uint32_t ion_packed = pack_colour(hl::ion_colour, 0.f);
            unsigned g0 = 0;
            for (unsigned set = 0; set < 3; ++set) {
                const unsigned count = counts[set];
                if (!count) continue;
                const SparkSet ss = spark_set(a, set);
                const unsigned base = g0;
                g0 += (count + 1) & ~1u; // every set starts on a pair
                const float f_now = spark_reach_factor(t_now, ss.tau), f_prev = spark_reach_factor(t_prev, ss.tau);
                const float speed0 = ss.speed * U;
                const float life_law = ss.life + ss.life_k * k;
                const float width_view = ss.width * U, len_scale = ss.rest_w * (1.f / hl::spark_rest_w);
                const float b = hs.has_axis ? ss.bias : 0.f;
                const bool first = !(f_now - f_prev > 1e-6f); // the first frame: no motion yet
                const unsigned slot = age_ms / hl::flicker_ms;
                float d[3] = {0.f, 0.f, 1.f}, d_now[3] = {0.f, 0.f, 1.f}, d_prev[3] = {0.f, 0.f, 1.f};
                float ua = 0.f;
                unsigned d_pair = ~0u;
                for (unsigned sp = 0; sp < count; ++sp) {
                    const unsigned g = base + sp;
                    const SparkFactors sf = spark_factors(e.sequence, g, ss.speed_spread);
                    const float law = life_law * sf.life_x;
                    const bool sfl = floor_ms > law;
                    const float life = sfl ? floor_ms : law;
                    if (!(age < life)) continue;
                    if (ss.duty > 0.f && !(unit_of(element_hash(e.sequence ^ (slot * 0x9e3779b9u), 8192u + g)) < ss.duty))
                        continue; // a flicker slot it is dark in
                    const unsigned pair = g >> 1;
                    if (pair != d_pair) { // the pair's direction in world and in both views, once
                        if (pair < hs.pairs)
                            for (unsigned q = 0; q < 3; ++q) d[q] = hs.pair_dir[pair][q];
                        else
                            pair_direction(e.sequence, pair, d);
                        rotate_world_to_view(f.cam, d, d_now);
                        rotate_world_to_view(prev, d, d_prev);
                        ua = dot3(d, hs.axis);
                        d_pair = pair;
                    }
                    const float sgn = (sp & 1u) ? -1.f : 1.f;
                    // The launch direction normalize(sgn u + b a) (|sgn u + b a|^2 = 1 + b^2 + 2 sgn b u.a), in both views.
                    float inv = 1.f;
                    if (b != 0.f) {
                        const float len2 = 1.f + b * b + 2.f * sgn * b * ua;
                        inv = len2 > 1e-6f ? rsqrt_nr(len2) : 0.f;
                    }
                    const float speed = speed0 * sf.speed_x;
                    const float fr2 = 1.f - age / life;
                    const float e_raw = ss.e * fr2;
                    const std::uint32_t packed = ion ? ion_packed : heat_packed(ss.t0 * fr2);
                    const float hold = e_raw < core::look::streak_core_min ? e_raw : core::look::streak_core_min;
                    const float sv = speed * inv;
                    float dn[3], dp[3];
                    for (unsigned q = 0; q < 3; ++q) {
                        dn[q] = (sgn * d_now[q] + b * ax_now[q]) * sv;
                        dp[q] = (sgn * d_prev[q] + b * ax_prev[q]) * sv;
                    }
                    float wn[3] = {0.f, 0.f, 0.f}, wp[3] = {0.f, 0.f, 0.f};
                    if (sf.curl) {
                        float ds[3];
                        for (unsigned q = 0; q < 3; ++q) ds[q] = (sgn * d[q] + b * hs.axis[q]) * inv;
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
                    streak_vertices(head, tail, e_raw * gain, hold * gain, width_view * sf.width_x, packed, out.sprites + 4 * nq,
                                    sf.len_x * len_scale);
                    ++nq;
                    ++n.sparks;
                    if (set == 2) ++n.chips;
                    --sparks_left;
                    n.floored += sfl;
                }
            }
        }
        // Glow and ember (the owner live).
        if (!hs.orphan) {
            const float* gw = hl::class_glow[a];
            const float* em = hl::class_ember[a];
            const bool repair = a == hl::an_repair, tug = a == hl::an_tug;
            for (unsigned el = 0; el < 2; ++el) {
                const bool is_glow = el == 0;
                if (is_glow ? !(t.glow && gw[0] > 0.f) : !(t.ember && em[0] > 0.f)) continue;
                const float law = is_glow ? gw[1] : em[1] + em[2] * k;
                const float life = element_life_ms(law, element_hash(e.sequence, 1u + el), t.stagger, floor_ms, &fl);
                if (!(age < life)) continue;
                const float fade = 1.f - age / life;
                const float R = (is_glow ? gw[0] : em[0]) * r_hit;
                float colour[3];
                if (repair || tug) {
                    const float e0 = is_glow ? gw[3] : em[4];
                    for (unsigned q = 0; q < 3; ++q) colour[q] = (tug ? hl::tug_colour[q] : tint[q]) * e0 * fade;
                } else if (is_glow) {
                    core::heat_colour(gw[2], colour);
                    for (unsigned q = 0; q < 3; ++q) colour[q] *= gw[3] * gw[2] * fade;
                } else {
                    const float T = em[3] * fade;
                    core::heat_colour(T, colour);
                    for (unsigned q = 0; q < 3; ++q) colour[q] *= em[4] * T;
                }
                for (unsigned q = 0; q < 3; ++q) colour[q] *= gain;
                if (glow_box(c_view, R, colour, age_ms, own, e.owner, core::look::budget_weight_glow)) {
                    if (is_glow)
                        ++n.glows;
                    else
                        ++n.embers;
                    n.floored += fl;
                }
            }
        }
    }
    // (D) The spots (the hold rule's beams, repair and tug).
    for (Spot& sp : state.spots) {
        if (!sp.used) continue;
        const core::Event& e = sp.e;
        const unsigned a = anatomy_of(e);
        const std::uint32_t rel_u = f.now - sp.last_ms;
        const std::uint32_t rel = std::int32_t(rel_u) > 0 ? rel_u : 0u;
        const bool held = rel <= hold_window;
        const float fall_age = held ? 0.f : float(rel - hold_window);
        const float tail_ms = a == hl::an_beam       ? 1500.f + float(hl::spot_fall_ms)
                              : a == hl::an_repair   ? hl::class_ember[a][1] + hl::class_glow[a][1]
                              : a == hl::an_asteroid ? hl::class_puff[a][3] + float(hl::spot_fall_ms)
                                                     : hl::class_glow[a][1];
        if (fall_age > tail_ms + sp.floor_ms || f.now - sp.first_ms > 600000u) {
            sp = Spot{};
            continue;
        }
        Seen* s = lookup(e.owner, e.node, e.handle);
        if (!s) continue;
        if (s->status != OwnerStatus::ok) { // its owner went: the spot with it
            sp = Spot{};
            continue;
        }
        ++n.spots;
        ++n.anatomy[a];
        const float k = record_calibre(e);
        const float U = hull_unit(e, t), r_hit = hl::r_scale * U * (hl::r_base + hl::r_k * k);
        const bool own = f.own_node && e.node == f.own_node;
        const float gain = (f.cockpit && own) ? t.own_gain : 1.f;
        const std::uint32_t age_first = f.now - sp.first_ms;
        flash_age = rel;
        flash_owner = e.owner;
        flash_own = own;
        float tint[3];
        core::event_tint(e, 0.f, tint);
        float c_view[3];
        {
            double w[3];
            hit_world(s->data, e.pos, w);
            to_view(f.cam, w, c_view);
        }
        bool fl = false;
        if (a == hl::an_beam || a == hl::an_asteroid) {
            // The pulse start's flash, then the held spot's core (re-posed each tick: the beam's crawl), falling after
            // (not on rock: the asteroid takes no flash).
            const float* fr = hl::class_flash[a];
            if (t.flash && sp.flash && a == hl::an_beam) {
                const float life = element_life_ms(fr[2], element_hash(sp.seed, 0), t.stagger, sp.floor_ms, &fl);
                float halo[3];
                core::heat_colour(fr[3], halo);
                if (float(age_first) < life &&
                    emit_flash(c_view, r_hit, fr[0], fr[1], halo, core::look::core_e * (1.f - float(age_first) / life) * gain)) {
                    ++n.flashes;
                    ++n.spot_flashes;
                }
            }
            const float env = 1.f - fall_age / float(hl::spot_fall_ms);
            if (t.flash && env > 0.f && a == hl::an_beam) {
                float halo[3];
                core::heat_colour(1.f, halo);
                if (emit_flash(c_view, r_hit, hl::spot_core, hl::spot_halo, halo, core::look::core_e * env * gain)) ++n.flashes;
            }
            // The boiling puff: grown from the spot's birth, its heat re-lit at each tick (never added).
            const float* pf = hl::class_puff[a];
            const float L = pf[3];
            const float T = pf[5] * (1.f - fall_age / L);
            if (t.burst && fireball_cap && T > 0.f) {
                const float R = pf[0] * r_hit * (1.f - core::exp_neg(-float(age_first) / (pf[7] * L)));
                const float tint4[4] = {0.f, 0.f, 0.f, 0.f};
                puff_box(c_view, R, T, pf[6] * gain, core::look::fireball_advect * float(age_first % 1000u) * 1e-3f,
                         64.f * unit_of(element_hash(sp.seed, 4)) + 1e-3f, tint4, rel, own, e.owner);
            }
            // The spatter: two sparks born every spatter_period_ms of the hold (closed form from the spot's birth), each
            // from the spot's current point (owner-anchored), at most spatter_cap live; on rock the asteroid's dust row.
            const SparkSet ss = spark_set(a, 1);
            if (t.sparks && t.mode != hl::simple && ss.n0 > 0.f && (t.set_mask & 2u)) {
                const std::uint32_t lit_end = held ? age_first : (rel - hold_window < age_first ? age_first - (rel - hold_window) : 0u);
                const float life_max = ss.life * hl::spark_life_hi;
                const unsigned m_hi = lit_end / hl::spatter_period_ms;
                const unsigned m_lo = float(age_first) > life_max ? unsigned((float(age_first) - life_max) / float(hl::spatter_period_ms)) : 0u;
                const unsigned per = unsigned(spark_set(hl::an_beam, 1).n0);
                unsigned drawn = 0;
                for (unsigned m = m_hi + 1; m-- > m_lo && drawn < hl::spatter_cap;) {
                    const float born = float(m * hl::spatter_period_ms);
                    const float sage = float(age_first) - born;
                    if (sage < 0.f) continue;
                    for (unsigned j = 0; j < per && drawn < hl::spatter_cap; ++j) {
                        const unsigned g = (m * per + j) & 1023u;
                        const SparkFactors sf = spark_factors(sp.seed, 2048u + g, ss.speed_spread);
                        const float life = ss.life * sf.life_x;
                        if (!(sage < life)) continue;
                        float d[3];
                        pair_direction(sp.seed, 1024u + (g >> 1), d);
                        const float sgn = (g & 1u) ? -1.f : 1.f;
                        float dv[3];
                        rotate_world_to_view(f.cam, d, dv);
                        const float speed = ss.speed * U * sf.speed_x * sgn;
                        const float ts = sage * 1e-3f, tp = ts > step_s ? ts - step_s : 0.f;
                        const float fn = spark_reach_factor(ts, ss.tau), fp = spark_reach_factor(tp, ss.tau);
                        float head[3], tail[3];
                        for (unsigned q = 0; q < 3; ++q) {
                            head[q] = c_view[q] + dv[q] * speed * fn;
                            tail[q] = c_view[q] + dv[q] * speed * (fn - fp > 1e-6f ? fp : fn - 1e-3f);
                        }
                        if (!(head[2] > f.near_z) || !(tail[2] > f.near_z)) continue;
                        if (!sparks_left || nq >= sprite_cap) {
                            ++n.capped;
                            continue;
                        }
                        const float fr2 = 1.f - sage / life, e_raw = ss.e * fr2;
                        const float hold = e_raw < core::look::streak_core_min ? e_raw : core::look::streak_core_min;
                        streak_vertices(head, tail, e_raw * gain, hold * gain, ss.width * U * sf.width_x,
                                        heat_packed(ss.t0 * fr2), out.sprites + 4 * nq, sf.len_x);
                        ++nq;
                        ++n.sparks;
                        --sparks_left;
                        ++drawn;
                    }
                }
            }
        }
        // The glow: held, then its fall (repair: the weapon tint, tug: tug_colour).
        const float* gw = hl::class_glow[a];
        const float genv = 1.f - fall_age / gw[1];
        if (t.glow && gw[0] > 0.f && genv > 0.f) {
            float colour[3];
            if (a == hl::an_repair)
                for (unsigned q = 0; q < 3; ++q) colour[q] = tint[q] * gw[3] * genv;
            else if (a == hl::an_tug)
                for (unsigned q = 0; q < 3; ++q) colour[q] = hl::tug_colour[q] * gw[3] * genv;
            else {
                core::heat_colour(gw[2], colour);
                for (unsigned q = 0; q < 3; ++q) colour[q] *= gw[3] * gw[2] * genv;
            }
            for (unsigned q = 0; q < 3; ++q) colour[q] *= gain;
            if (glow_box(c_view, gw[0] * r_hit, colour, rel, own, e.owner, core::look::budget_weight_glow)) ++n.glows;
        }
        // The ember: the beam's trail ember from the spot's fall; repair's for ember life after its last tick.
        const float* em = hl::class_ember[a];
        if (t.ember && em[0] > 0.f) {
            float colour[3];
            bool lit = false;
            if (a == hl::an_repair) {
                const float fade = 1.f - float(rel) / em[1];
                if (fade > 0.f) {
                    for (unsigned q = 0; q < 3; ++q) colour[q] = tint[q] * em[4] * fade * gain;
                    lit = true;
                }
            } else if (!held) {
                const float fade = 1.f - fall_age / em[1];
                if (fade > 0.f) {
                    const float T = em[3] * fade;
                    core::heat_colour(T, colour);
                    for (unsigned q = 0; q < 3; ++q) colour[q] *= em[4] * T * gain;
                    lit = true;
                }
            }
            if (lit && glow_box(c_view, em[0] * r_hit, colour, rel, own, e.owner, core::look::budget_weight_glow)) ++n.embers;
        }
    }
    // (E) The scorch store: the marks of live owners, drawn first by the pass (multiply), and the trail marks' embers.
    for (ScorchMark& m : state.scorches) {
        if (!m.kinds) continue;
        const std::uint32_t age_ms = f.now - m.t0;
        const float age = float(age_ms);
        const unsigned a = m.anatomy < hl::anatomy_count ? m.anatomy : unsigned(hl::an_energy);
        const float* sr = hl::class_scorch[a];
        if ((m.kinds & mark_scorch) && !(age < sr[1])) m.kinds = std::uint8_t(m.kinds & ~mark_scorch);
        if (!m.kinds || age_ms > 600000u) {
            m = ScorchMark{};
            continue;
        }
        Seen* s = lookup(m.owner, m.node, m.handle);
        if (!s) continue;
        if (s->status != OwnerStatus::ok) { // a mark never stays on a dead, reused or unreadable owner
            m = ScorchMark{};
            continue;
        }
        const bool own = f.own_node && m.node == f.own_node;
        const float gain = (f.cockpit && own) ? t.own_gain : 1.f;
        float c_view[3];
        {
            double w[3];
            hit_world(s->data, m.pos, w);
            to_view(f.cam, w, c_view);
        }
        if ((m.kinds & mark_scorch) && t.scorch && scorch_cap) {
            const float dev = m.ember_ms > 0 ? age / float(m.ember_ms) : 1.f;
            const float sv = sr[2] * (dev < 1.f ? dev : 1.f) * (1.f - age / sr[1]) * gain;
            const float R = sr[0] * m.r;
            float rect[4];
            bool full = false;
            if (sv > 0.f && c_view[2] + R > f.near_z && sphere_rect(f, c_view, R, rect, &full)) {
                if (ns < scorch_cap) {
                    float colour[3];
                    for (unsigned q = 0; q < 3; ++q) colour[q] = sv * hl::scorch_colour[q];
                    glow_quad(f, c_view, R, rect, full, colour, out.scorches + 4 * ns);
                    scorch_meta[ns] = BoxMeta{age_ms, m.owner, rect_screens(rect) * hl::budget_weight_scorch,
                                              std::uint8_t(own), 0};
                    ++ns;
                    ++n.scorches;
                } else
                    ++n.capped;
            }
        }
    }
    // (F) The shared budget (look 2.11 as amended): past the screens the patches left, (0) the shimmer rects of other
    // owners, (1) the puffs' second octave, then other owners' puffs past two per owner, (2) other owners' scorches, (3)
    // their rings and filament glows, (4) their glows and embers, then their remaining puffs (oldest first, O(boxes x
    // shed), only while over budget), last other owners' flashes. Streaks, sparks, chips, spot cores and anything of the
    // own ship are never shed.
    float area = 0.f, burst_area = 0.f;
    for (unsigned g = 0; g < ng; ++g) area += glow_meta[g].area;
    for (unsigned g = 0; g < nb; ++g) burst_area += burst_meta[g].area;
    for (unsigned g = 0; g < nr; ++g) area += ring_meta[g].area;
    for (unsigned g = 0; g < nf; ++g) area += filament_meta[g].area;
    for (unsigned g = 0; g < ns; ++g) area += scorch_meta[g].area;
    area += burst_area + flash_area;
    const bool over = area > f.budget_left;
    // The shimmer rects: the own ship's first, then the newest, at most shimmer_max; over budget only the own ship's.
    bool taken[16] = {};
    for (unsigned c = 0; c < shimmer_cands; ++c)
        if (over && !shimmer_cand[c].own) {
            taken[c] = true;
            ++n.shimmer_shed;
        }
    while (out.shimmer_count < shimmer_cap) {
        unsigned best = 16;
        for (unsigned c = 0; c < shimmer_cands; ++c)
            if (!taken[c] && (best == 16 || shimmer_cand[c].own > shimmer_cand[best].own ||
                              (shimmer_cand[c].own == shimmer_cand[best].own && shimmer_cand[c].age < shimmer_cand[best].age)))
                best = c;
        if (best == 16) break;
        taken[best] = true;
        out.shimmer[out.shimmer_count++] = shimmer_cand[best];
    }
    for (unsigned c = 0; c < shimmer_cands; ++c) n.shimmer_shed += !taken[c];
    n.shimmer = out.shimmer_count;
    if (area > f.budget_left && nb) { // the puffs not already at one octave drop theirs
        constexpr float lod = core::look::budget_weight_fireball_lod / core::look::budget_weight_fireball;
        for (unsigned g = 0; g < nb; ++g)
            if (!burst_meta[g].rank) {
                const float cut = burst_meta[g].area * (1.f - lod);
                area -= cut;
                burst_area -= cut;
                burst_meta[g].area -= cut;
            }
        n.octaves = 1;
    }
    if (area > f.budget_left && nb) { // each owner's puffs ranked newest first
        for (unsigned g = 0; g < nb; ++g) {
            unsigned rank = 0;
            for (unsigned q = 0; q < nb; ++q)
                if (q != g && burst_meta[q].owner == burst_meta[g].owner &&
                    (burst_meta[q].age < burst_meta[g].age || (burst_meta[q].age == burst_meta[g].age && q < g)))
                    ++rank;
            burst_meta[g].rank = std::uint8_t(rank < 255 ? rank : 255);
        }
        n.bursts_shed += shed_oldest(out.fireballs, &nb, burst_meta, &area, f.budget_left, 2, &burst_area);
    }
    n.scorches_shed += shed_oldest(out.scorches, &ns, scorch_meta, &area, f.budget_left, 0);
    n.rings_shed += shed_oldest(out.rings, &nr, ring_meta, &area, f.budget_left, 0);
    n.filaments_shed += shed_oldest(out.filaments, &nf, filament_meta, &area, f.budget_left, 0);
    n.shed += shed_oldest(out.glows, &ng, glow_meta, &area, f.budget_left, 0);
    for (unsigned g = 0; g < nb; ++g) burst_meta[g].rank = 0;
    n.bursts_shed += shed_oldest(out.fireballs, &nb, burst_meta, &area, f.budget_left, 0, &burst_area);
    // (5) last, other owners' flashes, oldest first (review 2026-11-07: about 80 plasma blooms on a close capital passed
    // the budget's 1.42 ms with nothing else left to shed): the freed quad takes the batch's last one.
    while (area > f.budget_left && nfl) {
        unsigned old = nfl;
        for (unsigned g = 0; g < nfl; ++g)
            if (!flash_meta[g].own && (old == nfl || flash_meta[g].age > flash_meta[old].age)) old = g;
        if (old == nfl) break;
        area -= flash_meta[old].area;
        flash_area -= flash_meta[old].area;
        const unsigned q = flash_quad_of[old], last = --nq;
        if (q != last) {
            for (unsigned c = 0; c < 4; ++c) out.sprites[4 * q + c] = out.sprites[4 * last + c];
            for (unsigned g = 0; g < nfl; ++g)
                if (flash_quad_of[g] == last) flash_quad_of[g] = std::uint16_t(q);
        }
        flash_meta[old] = flash_meta[nfl - 1];
        flash_quad_of[old] = flash_quad_of[nfl - 1];
        --nfl;
        ++n.flashes_shed;
    }
    n.screens = area;
    n.burst_screens = burst_area;
    n.flash_screens = flash_area;
    out.sprite_quads = nq;
    out.glow_boxes = ng;
    out.fireball_boxes = nb;
    out.ring_quads = nr;
    out.filament_boxes = nf;
    out.scorch_boxes = ns;
    return n;
}
} // namespace x3m::weapon_effects::stage
